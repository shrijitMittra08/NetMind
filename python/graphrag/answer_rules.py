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

_TIME_PATTERN = r"\b(?:at|around|near|after|before|by)\s+t?\s*=?\s*(\d+(?:\.\d+)?)\s*(?:s\b|sec)"

_COMPARE_SUBJECT = r"\b(algorithm|routing|method|protocol|compare|comparison|best|worst|better)\b"
_COMPARE_RANKING = (
    r"\b(best|worst|better|lowest|highest|most|least|fastest|slowest|shortest"
    r"|compare|comparison|perform\w*|win)\b"
)
_PATH_WORDS = r"\b(path|route[ds]?|travel\w*|get\s+to|got\s+to|reach\w*|go\s+from|went\s+from)\b"
_WHICH_ELEMENT = r"\bwhich\s+(link|device|router)\b"
_FAILURE_WORDS = r"\b(fail\w*|went\s+down|go\s+down|outage|crash\w*|broke\w*|which\s+device|down\b)"
_CONGESTION_WORDS = r"\b(congest\w*|bottleneck|busiest|overload\w*|utili[sz]\w*|saturat\w*|heaviest|load)\b"
_HOW_MANY = r"\bhow\s+many\b"
_DROP_COUNT_WORDS = r"\b(drop\w*|lost|loss)\b"
_DROP_WORDS = r"\b(drop\w*|lost|loss|discard\w*)\b"
_LOOSE_PATH_WORDS = r"\b(path|route|routed|travel\w*|get\s+to|reach\w*|hops?)\b"

_DOWN_EVENTS = {"LINK_DOWN", "ROUTER_DOWN"}
_UP_EVENTS = {"LINK_UP", "ROUTER_UP"}
_FAILURE_REASONS = {"no_route", "link_down", "node_down"}


def _time(question: str) -> float | None:
    match = re.search(_TIME_PATTERN, question.lower())
    return float(match.group(1)) if match else None


def _fmt(value: float | None, digits: int = 2) -> str:
    if value is None:
        return "unknown"
    text = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return text or "0"


def _intent(question: str) -> str:
    text = question.lower()
    if re.search(_COMPARE_SUBJECT, text) and re.search(_COMPARE_RANKING, text):
        return "compare"
    if re.search(_PATH_WORDS, text) and not re.search(_WHICH_ELEMENT, text):
        return "path"
    if re.search(_FAILURE_WORDS, text):
        return "failure"
    if re.search(_CONGESTION_WORDS, text):
        return "congestion"
    if re.search(_HOW_MANY, text) and re.search(_DROP_COUNT_WORDS, text):
        return "drop_total"
    if re.search(_DROP_WORDS, text):
        return "drops"
    if re.search(_LOOSE_PATH_WORDS, text):
        return "path"
    return "summary"


def _metric(question: str) -> str:
    text = question.lower()
    for metric, pattern in _METRIC_WORDS:
        if re.search(pattern, text):
            return metric
    return "latency_ms"


def _and_join(names: list[str]) -> str:
    if len(names) == 1:
        return names[0]
    return ", ".join(names[:-1]) + " and " + names[-1]


def _answer_compare(session: QASession, question: str) -> tuple[str, list]:
    metric = _metric(question)
    result = tools.compare_runs(session.contexts, metric)
    calls = [("compare_runs", {"metric": metric}, result)]
    values = {item["algorithm"]: item["value"] for item in result["values"]}

    if len(values) < 2:
        only = list(values)[0]
        return (
            f"Only {label(only)} was run, so there is nothing to compare it with "
            f"({result['label']}: {_fmt(values[only])} {result['unit']})."
        ), calls

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

    others = [algorithm for algorithm in values if algorithm not in best]
    others.sort(key=lambda algorithm: values[algorithm] or 0, reverse=result["better"] == "lower")
    rest = ", ".join(f"{label(algorithm)} {show(algorithm)}" for algorithm in others)

    if result["tie"]:
        names = _and_join([label(algorithm) for algorithm in best])
        return f"{names} tied for the best {result['label']} at {show(best[0])}; the others were worse ({rest}).", calls
    return (
        f"{label(best[0])} had the best {result['label']} at {show(best[0])} ({result['better']} is better). "
        f"The others: {rest}."
    ), calls


def _down_subject(down_kind: str, target: str) -> str:
    return f"The {target} link" if down_kind == "LINK_DOWN" else f"Router {target}"


def _failure_sentences(events: list[dict[str, Any]]) -> list[str]:
    sentences = []
    open_failures: dict[str, tuple[str, float]] = {}
    for event in events:
        kind, target, time_s = event["event"], event["target"], event["time_s"]
        if kind in _DOWN_EVENTS:
            open_failures[target] = (kind, time_s)
        elif kind in _UP_EVENTS and target in open_failures:
            down_kind, start = open_failures.pop(target)
            sentences.append(
                f"{_down_subject(down_kind, target)} went down at {_fmt(start)} s and came back up at {_fmt(time_s)} s."
            )
        elif kind == "LINK_DEGRADED":
            sentences.append(f"The {target} link was degraded (congested) from {_fmt(time_s)} s.")

    for target, (kind, start) in open_failures.items():
        sentences.append(
            f"{_down_subject(kind, target)} went down at {_fmt(start)} s and stayed down for the rest of the run."
        )
    return sentences


def _answer_failure(session: QASession, _question: str) -> tuple[str, list]:
    result = tools.network_events(session.run())
    sentences = _failure_sentences(result["events"])
    calls = [("network_events", {"algorithm": session.run().algorithm}, result)]
    if not sentences:
        return "Nothing failed in this run: there were no link or router failures.", calls
    return " ".join(sentences), calls


def _answer_congestion(session: QASession, question: str) -> tuple[str, list]:
    mentioned = session.mentioned_algorithms(question)
    ctx = mentioned[0] if mentioned else session.run()
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


def _answer_drops(session: QASession, question: str) -> tuple[str, list]:
    contexts = session.mentioned_algorithms(question) or session.contexts
    t = _time(question)
    t_from, t_to = (max(0.0, t - 1.0), t + 2.0) if t is not None else (None, None)

    texts, calls = [], []
    groups: dict[str, list[str]] = {}
    for ctx in contexts:
        text, run_calls = _drops_for_run(ctx, t_from, t_to)
        texts.append(text)
        calls.extend(run_calls)
        groups.setdefault(text, []).append(label(ctx.algorithm))

    if len(groups) == 1:
        return texts[0], calls
    return "\n".join(f"{_and_join(names)}: {text}" for text, names in groups.items()), calls


def _drops_for_run(ctx, t_from: float | None, t_to: float | None) -> tuple[str, list]:
    result = tools.drops(ctx, t_from, t_to)
    events = tools.network_events(ctx, None, t_to)
    calls = [("drops", {"algorithm": ctx.algorithm, "t_from": t_from, "t_to": t_to}, result),
             ("network_events", {"algorithm": ctx.algorithm, "t_to": t_to}, events)]

    span = f" between {_fmt(t_from)} and {_fmt(t_to)} s" if t_from is not None else ""
    if result["total"] == 0:
        return f"No packets were dropped{span}.", calls

    reasons = sorted(result["by_reason"].items(), key=lambda pair: pair[1], reverse=True)
    parts = [f"{count} because of {reason.replace('_', ' ')}" for reason, count in reasons]
    where = result["by_location"][0]["location"] if result["by_location"] else "unknown"
    text = f"{result['total']} packets were dropped{span}: " + "; ".join(parts) + f". Most were dropped at {where}."

    top_reason = reasons[0][0]
    failures = _failure_sentences(events["events"])
    if failures and top_reason in _FAILURE_REASONS:
        text += " This lines up with the failure: " + " ".join(failures)
    elif top_reason == "queue_full":
        text += " The traffic offered was more than the link could carry, so its queue overflowed."
    return text, calls


def _summaries(contexts: list) -> tuple[list[dict[str, Any]], list]:
    summaries = [tools.run_summary(ctx) for ctx in contexts]
    return summaries, [("run_summary", {"algorithm": summary["algorithm"]}, summary) for summary in summaries]


def _answer_drop_total(session: QASession, question: str) -> tuple[str, list]:
    summaries, calls = _summaries(session.mentioned_algorithms(question) or session.contexts)
    if len({summary["drops"] for summary in summaries}) == 1:
        summary = summaries[0]
        scope = "In every run" if len(summaries) > 1 else "In total"
        return (
            f"{scope}, {summary['drops']} packets were dropped, "
            f"so {_fmt(summary['reliability_pct'])}% of packets were delivered."
        ), calls
    return " ".join(f"{label(summary['algorithm'])} dropped {summary['drops']} packets." for summary in summaries), calls


def _first_failure(session: QASession) -> dict[str, Any] | None:
    for event in tools.network_events(session.run())["events"]:
        if event["event"] in _DOWN_EVENTS:
            return event
    return None


def _pick_flow_ends(nodes: list[str], flows: list[dict[str, Any]]) -> tuple[str, str]:
    hosts = [node for node in nodes if any(node in (flow["src"], flow["dst"]) for flow in flows)]
    if len(hosts) >= 2:
        return hosts[0], hosts[1]
    return flows[0]["src"], flows[0]["dst"]


def _answer_path(session: QASession, question: str) -> tuple[str, list]:
    nodes = session.mentioned_nodes(question)
    t = _time(question)
    if t is None and re.search(r"\bafter\b", question.lower()):
        failure = _first_failure(session)
        t = failure["time_s"] + 0.5 if failure else None

    contexts = session.mentioned_algorithms(question) or session.contexts
    src, dst = _pick_flow_ends(nodes, tools.run_summary(contexts[0])["flows"])

    calls, lines = [], []
    for ctx in contexts:
        name = label(ctx.algorithm)
        result = tools.flow_path(ctx, src, dst, at_time=t)
        calls.append(("flow_path", {"algorithm": ctx.algorithm, "src": src, "dst": dst, "at_time": t}, result))
        if "error" in result:
            lines.append(f"{name}: {result['error']}")
            continue
        if t is not None:
            line = f"{name}: at {_fmt(t)} s, traffic from {src} to {dst} went {result['path_at_time']}."
        else:
            line = f"{name}: traffic from {src} to {dst} went {result['path_periods'][0]['path']}."
        if result["path_changes"]:
            changes = "; ".join(f"from {_fmt(period['from_s'])} s via {period['path']}"
                                for period in result["path_periods"][1:4])
            line += f" The route changed {result['path_changes']} time(s): {changes}."
        lines.append(line)

    descriptions = {line.split(": ", 1)[1] for line in lines}
    if len(descriptions) == 1 and len(lines) > 1:
        shared = lines[0].split(": ", 1)[1]
        return "All algorithms used the same route. " + shared[0].upper() + shared[1:], calls
    return "\n".join(lines), calls


def _answer_summary(session: QASession, _question: str) -> tuple[str, list]:
    summaries, calls = _summaries(session.contexts)
    lines = [
        f"{label(summary['algorithm'])}: {_fmt(summary['reliability_pct'])}% delivered, "
        f"{_fmt(summary['latency_ms'])} ms average latency, "
        f"{_fmt(summary['throughput_mbps'])} Mbps, {summary['drops']} drops."
        for summary in summaries
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
    tool_calls = []
    for name, args, _result in calls:
        tool_calls.append({"name": name, "args": {key: value for key, value in args.items() if value is not None}})
    return {"engine": "rules", "intent": intent, "answer": answer, "tool_calls": tool_calls, "verified": True}
