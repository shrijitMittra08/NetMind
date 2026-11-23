from __future__ import annotations

import re
from collections import defaultdict
from typing import Any, Iterable, Mapping, Sequence

import networkx as nx

_STOP_WORDS = {
    "a", "an", "and", "at", "did", "does", "for", "how", "i", "in",
    "is", "it", "me", "of", "on", "or", "the", "to", "was", "what",
    "when", "which", "why",
}

_TIME_PATTERN = (
    r"\b(?:at|around|near|t\s*=?\s*)\s*"
    r"(\d+(?:\.\d+)?)\s*(?:s|sec(?:ond)?s?)?\b"
)

_MOST_CONGESTED = r"\bmost\s+congested\b"
_CONGESTION_WORDS = r"\b(congested|congestion)\b"
_LINK_WORDS = r"\b(link|channel)\b"
_DROP_WORDS = r"\b(drop|dropped|drops|packet loss)\b"
_BEST_WORDS = r"\b(best|better|performed)\b"
_ALGORITHM_WORDS = r"\b(algorithm|routing)\b"
_PATH_WORDS = r"\b(path|route)\b"

_MAX_SEARCH_RESULTS = 3

Records = list[tuple[str, Mapping[str, Any]]]


def _as_graph_list(graphs: nx.MultiDiGraph | Iterable[nx.MultiDiGraph]) -> list[nx.MultiDiGraph]:
    result = [graphs] if isinstance(graphs, nx.MultiDiGraph) else list(graphs)
    if not result:
        raise ValueError("At least one run graph is required")
    for graph in result:
        if not isinstance(graph, nx.MultiDiGraph):
            raise TypeError("Every graph must be a NetworkX MultiDiGraph")
    return result


def _run_id(graph: nx.MultiDiGraph) -> str:
    if graph.graph.get("run_id"):
        return str(graph.graph["run_id"])
    for _, data in _nodes_of_type(graph, "Run"):
        return str(data.get("id", "unknown_run"))
    return "unknown_run"


def _nodes_of_type(graph: nx.MultiDiGraph, node_type: str) -> list[tuple[Any, Mapping[str, Any]]]:
    return [(key, data) for key, data in graph.nodes(data=True) if data.get("node_type") == node_type]


def _result(intent: str, answer: str, selected: Records | None = None) -> dict[str, Any]:
    selected = selected or []
    citations = [f"[{run_id}:{fact.get('id', 'unknown_fact')}]" for run_id, fact in selected]
    if citations:
        answer += " " + " ".join(citations)
    return {"intent": intent, "answer": answer, "citations": citations, "facts": [dict(fact) for _, fact in selected]}


def _tokens(text: str) -> set[str]:
    return {token for token in re.findall(r"[a-z0-9]+", text.lower()) if token not in _STOP_WORDS and len(token) > 1}


def _time_from_question(question: str) -> float | None:
    match = re.search(_TIME_PATTERN, question.lower())
    return float(match.group(1)) if match else None


def _intent(question: str) -> str:
    text = question.lower()
    has_time = _time_from_question(text) is not None
    if re.search(_MOST_CONGESTED, text):
        return "most_congested_link"
    if re.search(_CONGESTION_WORDS, text) and re.search(_LINK_WORDS, text):
        return "most_congested_link"
    if re.search(_DROP_WORDS, text) and has_time:
        return "drops_at_time"
    if "latency" in text and has_time:
        return "latency_change_at_time"
    if re.search(_BEST_WORDS, text) and re.search(_ALGORITHM_WORDS, text):
        return "best_algorithm"
    if re.search(_PATH_WORDS, text):
        return "flow_path"
    return "fact_search"


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float))


def _mean(values: list[float]) -> float | None:
    return sum(values) / len(values) if values else None


def _run_window_summary(graph: nx.MultiDiGraph) -> dict[str, Any] | None:
    run_nodes = _nodes_of_type(graph, "Run")
    if not run_nodes:
        return None
    algorithm = str(run_nodes[0][1].get("algo") or "").lower()
    window_rows = [data for _, data in _nodes_of_type(graph, "Window")]
    if not algorithm or not window_rows:
        return None

    def mean_metric(name: str) -> float | None:
        return _mean([float(row[name]) for row in window_rows if _is_number(row.get(name))])

    latency = mean_metric("avg_latency_ms")
    if latency is None:
        return None
    return {
        "run_id": _run_id(graph),
        "algorithm": algorithm,
        "avg_latency_ms": latency,
        "avg_throughput_mbps": mean_metric("throughput_mbps"),
        "loss_pct": mean_metric("loss_pct"),
    }


def _algorithm_score(algorithm: str, runs: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "algorithm": algorithm,
        "avg_latency_ms": sum(run["avg_latency_ms"] for run in runs) / len(runs),
        "avg_throughput_mbps": _mean([run["avg_throughput_mbps"] for run in runs if run["avg_throughput_mbps"] is not None]),
        "loss_pct": _mean([run["loss_pct"] for run in runs if run["loss_pct"] is not None]),
        "runs": len(runs),
    }


def _ranking_key(score: dict[str, Any]) -> tuple:
    throughput = score["avg_throughput_mbps"]
    if throughput is None:
        throughput = float("-inf")
    loss = score["loss_pct"]
    if loss is None:
        loss = float("inf")
    return (score["avg_latency_ms"], -throughput, loss, score["algorithm"])


def _best_algorithm(graphs: list[nx.MultiDiGraph]) -> dict[str, Any]:
    runs_by_algorithm: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for graph in graphs:
        summary = _run_window_summary(graph)
        if summary is not None:
            runs_by_algorithm[summary["algorithm"]].append(summary)
    if not runs_by_algorithm:
        return _result("best_algorithm",
                       "Not enough data: the graphs need run algorithms and window latency measurements.")

    scores = [_algorithm_score(algorithm, runs) for algorithm, runs in runs_by_algorithm.items()]
    winner = min(scores, key=_ranking_key)
    answer = (
        f"{winner['algorithm']} had the lowest mean window latency "
        f"({winner['avg_latency_ms']:.3f} ms) across "
        f"{winner['runs']} supplied run(s). This comparison defines "
        f"'best' as lowest average latency."
    )
    if winner["avg_throughput_mbps"] is not None:
        answer += f" Its mean throughput was {winner['avg_throughput_mbps']:.3f} Mbps."
    if winner["loss_pct"] is not None:
        answer += f" Its mean loss was {winner['loss_pct']:.3f}%."
    return _result("best_algorithm", answer)


def _source_name(data: Mapping[str, Any], default: str) -> str:
    return str(data.get("src_name") or data.get("src") or default)


def _destination_name(data: Mapping[str, Any], default: str) -> str:
    return str(data.get("dst_name") or data.get("dst") or default)


def _flow_label(data: Mapping[str, Any]) -> str:
    return f"{_source_name(data, '?')}->{_destination_name(data, '?')}"


def _find_flow(graph: nx.MultiDiGraph, question: str) -> tuple[str, Mapping[str, Any]] | None:
    lowered_question = question.lower()
    question_without_spaces = re.sub(r"\s+", "", lowered_question)
    all_flows = _nodes_of_type(graph, "Flow")
    matches = []
    for key, data in all_flows:
        source = _source_name(data, "").lower()
        destination = _destination_name(data, "").lower()
        label = _flow_label(data).lower().replace(" ", "")
        if label and label in question_without_spaces:
            matches.append((key, data))
        elif source and destination and source in lowered_question and destination in lowered_question:
            matches.append((key, data))

    if len(matches) == 1:
        return matches[0]
    if not matches and len(all_flows) == 1:
        return all_flows[0]
    return None


def _observed_path(graph: nx.MultiDiGraph, flow_key: str, flow_data: Mapping[str, Any]) -> list[str] | None:
    packets: dict[str, list[tuple[float, str]]] = defaultdict(list)
    for _, device_key, edge in graph.out_edges(flow_key, data=True):
        if edge.get("relation") != "ROUTED_VIA":
            continue
        packet_id = str(edge.get("packet_id", "")).strip()
        try:
            time_s = float(edge.get("time_s"))
        except (TypeError, ValueError):
            continue
        if packet_id:
            packets[packet_id].append((time_s, str(graph.nodes[device_key].get("name", device_key))))

    candidates = []
    for packet_id, observations in packets.items():
        observations.sort(key=lambda item: item[0])
        candidates.append((observations[-1][0], packet_id, [name for _, name in observations]))
    if not candidates:
        return None

    _, _, routers = max(candidates, key=lambda item: (item[0], item[1]))
    path = [_source_name(flow_data, "?")]
    for device in routers + [_destination_name(flow_data, "?")]:
        if device != path[-1]:
            path.append(device)
    return path


def _fallback_search(records: Records, question: str) -> dict[str, Any]:
    query_tokens = _tokens(question)
    if not query_tokens:
        return _result("fact_search", "Not enough data: I could not identify useful search terms.")

    scored = []
    for run_id, fact in records:
        overlap = len(query_tokens & _tokens(str(fact.get("text", ""))))
        if overlap:
            scored.append((overlap, run_id, fact))
    scored.sort(key=lambda item: (-item[0], item[1], str(item[2].get("id"))))
    selected = [(run_id, fact) for _, run_id, fact in scored[:_MAX_SEARCH_RESULTS]]
    if not selected:
        return _result("fact_search", "Not enough data: no stored fact matched the question.")

    answer = "Related stored fact(s): " + " ".join(str(fact.get("text", "")) for _, fact in selected)
    return _result("fact_search", answer, selected)


def _most_congested_link(graph_list: list[nx.MultiDiGraph], records: Records, intent: str) -> dict[str, Any]:
    graph_by_run = {_run_id(graph): graph for graph in graph_list}
    candidates = []
    for run_id, fact in records:
        if fact.get("type") != "congestion":
            continue
        evidence = fact.get("evidence", {})
        link_name = link_key = str(evidence.get("link", ""))
        graph = graph_by_run.get(run_id)
        if graph and link_key in graph:
            link_name = str(graph.nodes[link_key].get("name", link_key))

        maximum = evidence.get("max_queue")
        capacity = evidence.get("queue_capacity")
        saturation = -1.0
        if _is_number(maximum) and _is_number(capacity) and capacity > 0:
            saturation = float(maximum) / float(capacity)
        drops = evidence.get("queue_full_drop_count", 0)
        if not _is_number(drops):
            drops = 0
        candidates.append((saturation, float(drops), run_id, link_name, fact))

    if not candidates:
        return _result(intent, "Not enough data: no congestion facts are available.")

    saturation, drops, run_id, link_name, fact = max(candidates, key=lambda item: item[:4])
    if saturation >= 0:
        score_text = f"queue reached {saturation * 100:.1f}% of capacity"
    else:
        score_text = "queue saturation could not be calculated"
    answer = (
        f"The most congested recorded link was {link_name} in {run_id}; "
        f"{score_text}, with {int(drops)} queue-full drop(s)."
    )
    return _result(intent, answer, [(run_id, fact)])


def _facts_at_time(records: Records, question: str, intent: str, time_tolerance_seconds: float) -> dict[str, Any]:
    target_time = _time_from_question(question)
    if target_time is None:
        return _result(intent, "Please include a simulation time, such as 'at 20 seconds'.")

    require_latency = intent == "latency_change_at_time"
    candidates = []
    for run_id, fact in records:
        fact_time = fact.get("time")
        if fact.get("type") != "failure_impact" or not _is_number(fact_time):
            continue
        distance = abs(float(fact_time) - target_time)
        if distance > time_tolerance_seconds:
            continue
        evidence = fact.get("evidence", {})
        if require_latency and (evidence.get("latency_before_ms") is None or evidence.get("latency_after_ms") is None):
            continue
        candidates.append((distance, run_id, fact))
    candidates.sort(key=lambda item: (item[0], item[1], str(item[2].get("id"))))
    selected = [(run_id, fact) for _, run_id, fact in candidates]

    if not selected:
        wanted = "latency-change" if require_latency else "failure/drop"
        return _result(
            intent,
            f"Not enough data: no matching {wanted} fact was recorded within "
            f"{time_tolerance_seconds:g} seconds of t={target_time:g}s.",
        )
    return _result(intent, " ".join(str(fact.get("text", "")) for _, fact in selected), selected)


def _flow_path_answer(graph_list: list[nx.MultiDiGraph], question: str, intent: str) -> dict[str, Any]:
    if len(graph_list) != 1:
        return _result(intent, "Please ask about a flow path in one run graph at a time.")
    graph = graph_list[0]
    match = _find_flow(graph, question)
    if match is None:
        return _result(intent, "Not enough data: name one flow's source and destination.")

    flow_key, flow_data = match
    path = _observed_path(graph, flow_key, flow_data)
    if path is None:
        return _result(intent, f"Not enough data: no observed forwarding path is available for {_flow_label(flow_data)}.")
    return _result(intent, f"Observed path for {_flow_label(flow_data)}: " + " -> ".join(path) + ".")


def answer_question(
    graphs: nx.MultiDiGraph | Iterable[nx.MultiDiGraph],
    facts_by_run: Mapping[str, Sequence[Mapping[str, Any]]],
    question: str,
    time_tolerance_seconds: float = 2.0,
) -> dict[str, Any]:
    if not question.strip():
        raise ValueError("question cannot be empty")
    if time_tolerance_seconds < 0:
        raise ValueError("time_tolerance_seconds cannot be negative")

    graph_list = _as_graph_list(graphs)
    records = []
    for graph in graph_list:
        run_id = _run_id(graph)
        records += [(run_id, fact) for fact in facts_by_run.get(run_id, ())]
    intent = _intent(question)

    if intent == "most_congested_link":
        return _most_congested_link(graph_list, records, intent)
    if intent in {"drops_at_time", "latency_change_at_time"}:
        return _facts_at_time(records, question, intent, time_tolerance_seconds)
    if intent == "best_algorithm":
        return _best_algorithm(graph_list)
    if intent == "flow_path":
        return _flow_path_answer(graph_list, question, intent)
    return _fallback_search(records, question)
