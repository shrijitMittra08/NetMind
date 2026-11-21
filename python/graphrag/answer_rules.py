from __future__ import annotations

import re
from typing import Any

from graphrag import tools
from graphrag.session import QASession, label

_METRIC_WORDS = [
    ("avg_hops", r"\b(hops?|shortest\s+paths?|fewest\s+hops)\b"),
    ("recovery_time_s", r"\brecover(y|ed)?\b"),
    ("throughput_mbps", r"\b(throughput|bandwidth|mbps|data\s+rate)\b"),
    ("drops", r"\b(fewest|least|most)\s+(drops|dropped|lost)\b"),
    ("reliability_pct", r"\b(reliab\w*|delivered|delivery|loss)\b"),
    ("latency_ms", r"\b(latency|delay|fast\w*|slow\w*|quick\w*)\b"),
]


def _time(question: str) -> float | None:
    match = re.search(r"\b(?:at|around|near|after|before|by)\s+t?\s*=?\s*(\d+(?:\.\d+)?)\s*(?:s\b|sec)", question.lower())
    return float(match.group(1)) if match else None


def _fmt(value: float | None, digits: int = 2) -> str:
    if value is None:
        return "unknown"
    text = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return text or "0"


def _intent(question: str) -> str:
    text = question.lower()
    comparing = re.search(r"\b(algorithm|routing|method|protocol|compare|comparison|best|worst|better)\b", text)
    if comparing and re.search(r"\b(best|worst|better|lowest|highest|most|least|fastest|slowest|shortest|compare|comparison|perform\w*|win)\b", text):
        return "compare"
    if re.search(r"\b(path|route[ds]?|travel\w*|get\s+to|got\s+to|reach\w*|go\s+from|went\s+from)\b", text) and not re.search(
        r"\bwhich\s+(link|device|router)\b", text
    ):
        return "path"
    if re.search(r"\b(fail\w*|went\s+down|go\s+down|outage|crash\w*|broke\w*|which\s+device|down\b)", text):
        return "failure"
    if re.search(r"\b(congest\w*|bottleneck|busiest|overload\w*|utili[sz]\w*|saturat\w*|heaviest|load)\b", text):
        return "congestion"
    if re.search(r"\bhow\s+many\b", text) and re.search(r"\b(drop\w*|lost|loss)\b", text):
        return "drop_total"
    if re.search(r"\b(drop\w*|lost|loss|discard\w*)\b", text):
        return "drops"
    if re.search(r"\b(path|route|routed|travel\w*|get\s+to|reach\w*|hops?)\b", text):
        return "path"
    return "summary"


def _metric(question: str) -> str:
    text = question.lower()
    for metric, pattern in _METRIC_WORDS:
        if re.search(pattern, text):
            return metric
    return "latency_ms"


def _answer_compare(session: QASession, question: str) -> tuple[str, list]:
    metric = _metric(question)
    result = tools.compare_runs(session.contexts, metric)
    calls = [("compare_runs", {"metric": metric}, result)]
    values = {v["algorithm"]: v["value"] for v in result["values"]}
    if len(values) < 2:
        only = next(iter(values))
        return f"Only {label(only)} was run, so there is nothing to compare it with ({result['label']}: {_fmt(values[only])} {result['unit']}).", calls
    best = result["best"]
    digits = 0 if metric == "drops" else 2
    unit = result["unit"]

    def show(algorithm: str) -> str:
        value = _fmt(values[algorithm], digits)
        return f"{value}%" if unit == "%" else f"{value} {unit}"

    if result["all_equal"]:
        return (
            f"All {len(values)} algorithms tied on {result['label']}, each at {show(best[0])}. "
            "They chose equally good routes in this scenario, so none of them is better here."
        ), calls
    others = sorted((a for a in values if a not in best), key=lambda a: values[a] if values[a] is not None else 0,
                    reverse=result["better"] == "lower")
    rest = ", ".join(f"{label(a)} {show(a)}" for a in others)
    if result["tie"]:
        names = ", ".join(label(a) for a in best[:-1]) + f" and {label(best[-1])}"
        return f"{names} tied for the best {result['label']} at {show(best[0])}; the others were worse ({rest}).", calls
    return f"{label(best[0])} had the best {result['label']} at {show(best[0])} ({result['better']} is better). The others: {rest}.", calls


def _failure_sentences(events: list[dict[str, Any]]) -> list[str]:
    sentences, open_at = [], {}
    for event in events:
        kind, target, t = event["event"], event["target"], event["time_s"]
        if kind in {"LINK_DOWN", "ROUTER_DOWN"}:
            open_at[target] = (kind, t)
        elif kind in {"LINK_UP", "ROUTER_UP"} and target in open_at:
            down_kind, start = open_at.pop(target)
            what = f"The {target} link" if down_kind == "LINK_DOWN" else f"Router {target}"
            sentences.append(f"{what} went down at {_fmt(start)} s and came back up at {_fmt(t)} s.")
        elif kind == "LINK_DEGRADED":
            sentences.append(f"The {target} link was degraded (congested) from {_fmt(t)} s.")
    for target, (kind, start) in open_at.items():
        what = f"The {target} link" if kind == "LINK_DOWN" else f"Router {target}"
        sentences.append(f"{what} went down at {_fmt(start)} s and stayed down for the rest of the run.")
    return sentences


def _answer_failure(session: QASession, _question: str) -> tuple[str, list]:
    result = tools.network_events(session.run())
    sentences = _failure_sentences(result["events"])
    calls = [("network_events", {"algorithm": session.run().algorithm}, result)]
    if not sentences:
        return "Nothing failed in this run: there were no link or router failures.", calls
    return " ".join(sentences), calls


def _answer_congestion(session: QASession, question: str) -> tuple[str, list]:
    ctx = (session.mentioned_algorithms(question) or [session.run()])[0]
    result = tools.link_load(ctx)
    calls = [("link_load", {"algorithm": ctx.algorithm}, result)]
    busiest = result["busiest"]
    if not busiest:
        return "There is no link load data for this run.", calls
    overflow = result["queue_overflows"][0] if result["queue_overflows"] else None
    if overflow and overflow["channel"] != busiest["channel"]:
        return (f"The most congested link was {overflow['channel']} (the {overflow['link']} link): its queue "
                f"reached {overflow['max_queue']} packets and {overflow['drops_queue_full']} packets were dropped "
                f"there because the queue was full. By average load over the whole run, the busiest was "
                f"{busiest['channel']} at {_fmt(busiest['utilization_pct'], 1)}% of its capacity, "
                f"without overflowing."), calls
    text = (f"The busiest link was {busiest['channel']} (the {busiest['link']} link), "
            f"carrying {_fmt(busiest['utilization_pct'], 1)}% of its capacity on average.")
    if overflow:
        text += (f" It was a bottleneck: its queue reached {overflow['max_queue']} packets and "
                 f"{overflow['drops_queue_full']} packets were dropped there because the queue was full.")
    else:
        text += " No queue overflowed, so the network was not congested to the point of dropping packets."
    return text, calls


_REASON_TEXT = {
    "no_route": "no route to the destination",
    "queue_full": "queue full (the link could not keep up)",
    "link_down": "link down",
    "node_down": "node down",
    "random_loss": "random loss on the link",
    "ttl_expired": "TTL expired",
}


def _answer_drops(session: QASession, question: str) -> tuple[str, list]:
    contexts = session.mentioned_algorithms(question) or session.contexts
    t = _time(question)
    t_from, t_to = (max(0.0, t - 1.0), t + 2.0) if t is not None else (None, None)
    answers = [_drops_for_run(ctx, t_from, t_to) for ctx in contexts]
    calls = [call for _, run_calls in answers for call in run_calls]
    groups: dict[str, list[str]] = {}
    for ctx, (text, _) in zip(contexts, answers):
        groups.setdefault(text, []).append(label(ctx.algorithm))
    if len(groups) == 1:
        return answers[0][0], calls
    lines = [f"{_and_join(names)}: {text}" for text, names in groups.items()]
    return "\n".join(lines), calls


def _and_join(names: list[str]) -> str:
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " and " + names[-1]


def _drops_for_run(ctx, t_from: float | None, t_to: float | None) -> tuple[str, list]:
    result = tools.drops(ctx, t_from, t_to)
    events = tools.network_events(ctx, None, t_to)
    calls = [("drops", {"algorithm": ctx.algorithm, "t_from": t_from, "t_to": t_to}, result),
             ("network_events", {"algorithm": ctx.algorithm, "t_to": t_to}, events)]
    span = f" between {_fmt(t_from)} and {_fmt(t_to)} s" if t_from is not None else ""
    if result["total"] == 0:
        return f"No packets were dropped{span}.", calls
    reasons = sorted(result["by_reason"].items(), key=lambda kv: kv[1], reverse=True)
    parts = [f"{count} because of {reason.replace('_', ' ')}" for reason, count in reasons]
    where = result["by_location"][0]["location"] if result["by_location"] else "unknown"
    text = f"{result['total']} packets were dropped{span}: " + "; ".join(parts) + f". Most were dropped at {where}."
    failures = _failure_sentences(events["events"])
    if failures and reasons[0][0] in {"no_route", "link_down", "node_down"}:
        text += " This lines up with the failure: " + " ".join(failures)
    elif reasons[0][0] == "queue_full":
        text += " The traffic offered was more than the link could carry, so its queue overflowed."
    return text, calls


def _answer_drop_total(session: QASession, question: str) -> tuple[str, list]:
    contexts = session.mentioned_algorithms(question) or session.contexts
    summaries = [tools.run_summary(ctx) for ctx in contexts]
    calls = [("run_summary", {"algorithm": s["algorithm"]}, s) for s in summaries]
    if len({s["drops"] for s in summaries}) == 1:
        s = summaries[0]
        scope = "In every run" if len(summaries) > 1 else "In total"
        return f"{scope}, {s['drops']} packets were dropped, so {_fmt(s['reliability_pct'])}% of packets were delivered.", calls
    return " ".join(f"{label(s['algorithm'])} dropped {s['drops']} packets." for s in summaries), calls


def _answer_path(session: QASession, question: str) -> tuple[str, list]:
    nodes = session.mentioned_nodes(question)
    t = _time(question)
    if t is None and re.search(r"\bafter\b", question.lower()):
        failure = next((e for e in tools.network_events(session.run())["events"]
                        if e["event"] in {"LINK_DOWN", "ROUTER_DOWN"}), None)
        t = failure["time_s"] + 0.5 if failure else None
    contexts = session.mentioned_algorithms(question) or session.contexts
    first = tools.run_summary(contexts[0])["flows"]
    hosts = [n for n in nodes if any(n in (f["src"], f["dst"]) for f in first)]
    src, dst = (hosts[0], hosts[1]) if len(hosts) >= 2 else (first[0]["src"], first[0]["dst"])
    calls, lines = [], []
    for ctx in contexts:
        result = tools.flow_path(ctx, src, dst, at_time=t)
        calls.append(("flow_path", {"algorithm": ctx.algorithm, "src": src, "dst": dst, "at_time": t}, result))
        if "error" in result:
            lines.append(f"{label(ctx.algorithm)}: {result['error']}")
            continue
        if t is not None:
            line = f"{label(ctx.algorithm)}: at {_fmt(t)} s, traffic from {src} to {dst} went {result['path_at_time']}."
        else:
            line = f"{label(ctx.algorithm)}: traffic from {src} to {dst} went {result['path_periods'][0]['path']}."
        if result["path_changes"]:
            changes = "; ".join(f"from {_fmt(p['from_s'])} s via {p['path']}" for p in result["path_periods"][1:4])
            line += f" The route changed {result['path_changes']} time(s): {changes}."
        lines.append(line)
    if len({line.split(": ", 1)[1] for line in lines}) == 1 and len(lines) > 1:
        shared = lines[0].split(": ", 1)[1]
        return "All algorithms used the same route. " + shared[0].upper() + shared[1:], calls
    return "\n".join(lines), calls


def _answer_summary(session: QASession, _question: str) -> tuple[str, list]:
    summaries = [tools.run_summary(ctx) for ctx in session.contexts]
    calls = [("run_summary", {"algorithm": s["algorithm"]}, s) for s in summaries]
    lines = [
        f"{label(s['algorithm'])}: {_fmt(s['reliability_pct'])}% delivered, {_fmt(s['latency_ms'])} ms average latency, "
        f"{_fmt(s['throughput_mbps'])} Mbps, {s['drops']} drops."
        for s in summaries
    ]
    return "Here is how the runs went.\n" + "\n".join(lines), calls


_HANDLERS = {
    "compare": _answer_compare,
    "failure": _answer_failure,
    "congestion": _answer_congestion,
    "drops": _answer_drops,
    "drop_total": _answer_drop_total,
    "path": _answer_path,
    "summary": _answer_summary,
}


def answer_with_rules(session: QASession, question: str) -> dict[str, Any]:
    intent = _intent(question)
    answer, calls = _HANDLERS[intent](session, question)
    return {
        "engine": "rules",
        "intent": intent,
        "answer": answer,
        "tool_calls": [{"name": name, "args": {k: v for k, v in args.items() if v is not None}} for name, args, _ in calls],
        "verified": True,
    }
