"""Deterministic retrieval and answering over NetMind run graphs and facts."""

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


def _as_graph_list(
    graphs: nx.MultiDiGraph | Iterable[nx.MultiDiGraph],
) -> list[nx.MultiDiGraph]:
    if isinstance(graphs, nx.MultiDiGraph):
        result = [graphs]
    else:
        result = list(graphs)

    if not result:
        raise ValueError("At least one run graph is required")
    if any(not isinstance(graph, nx.MultiDiGraph) for graph in result):
        raise TypeError("Every graph must be a NetworkX MultiDiGraph")
    return result


def _run_id(graph: nx.MultiDiGraph) -> str:
    if graph.graph.get("run_id"):
        return str(graph.graph["run_id"])

    for _, data in graph.nodes(data=True):
        if data.get("node_type") == "Run":
            return str(data.get("id", "unknown_run"))

    return "unknown_run"


def _fact_records(
    graphs: list[nx.MultiDiGraph],
    facts_by_run: Mapping[str, Sequence[Mapping[str, Any]]],
):
    records = []
    for graph in graphs:
        run_id = _run_id(graph)
        for fact in facts_by_run.get(run_id, ()):
            records.append((run_id, fact))
    return records


def _citation(run_id: str, fact: Mapping[str, Any]) -> str:
    return f"[{run_id}:{fact.get('id', 'unknown_fact')}]"


def _result(
    intent: str,
    answer: str,
    selected: list[tuple[str, Mapping[str, Any]]] | None = None,
) -> dict[str, Any]:
    selected = selected or []
    citations = [_citation(run_id, fact) for run_id, fact in selected]
    cited_answer = answer
    if citations:
        cited_answer += " " + " ".join(citations)

    return {
        "intent": intent,
        "answer": cited_answer,
        "citations": citations,
        "facts": [dict(fact) for _, fact in selected],
    }


def _tokens(text: str) -> set[str]:
    return {
        token
        for token in re.findall(r"[a-z0-9]+", text.lower())
        if token not in _STOP_WORDS and len(token) > 1
    }


def _time_from_question(question: str) -> float | None:
    match = re.search(
        r"\b(?:at|around|near|t\s*=?\s*)\s*"
        r"(\d+(?:\.\d+)?)\s*(?:s|sec(?:ond)?s?)?\b",
        question.lower(),
    )
    return float(match.group(1)) if match else None


def _intent(question: str) -> str:
    text = question.lower()

    if (
        re.search(r"\bmost\s+congested\b", text)
        or (
            re.search(r"\b(congested|congestion)\b", text)
            and re.search(r"\b(link|channel)\b", text)
        )
    ):
        return "most_congested_link"

    if (
        re.search(r"\b(drop|dropped|drops|packet loss)\b", text)
        and _time_from_question(text) is not None
    ):
        return "drops_at_time"

    if (
        "latency" in text
        and _time_from_question(text) is not None
    ):
        return "latency_change_at_time"

    if (
        re.search(r"\b(best|better|performed)\b", text)
        and re.search(r"\b(algorithm|routing)\b", text)
    ):
        return "best_algorithm"

    if re.search(r"\b(path|route)\b", text):
        return "flow_path"

    return "fact_search"


def _select_time_facts(
    records: list[tuple[str, Mapping[str, Any]]],
    target_time: float,
    fact_type: str,
    *,
    require_latency: bool = False,
    tolerance_seconds: float = 2.0,
) -> list[tuple[str, Mapping[str, Any]]]:
    candidates = []

    for run_id, fact in records:
        if fact.get("type") != fact_type:
            continue

        fact_time = fact.get("time")
        if not isinstance(fact_time, (int, float)):
            continue

        distance = abs(float(fact_time) - target_time)
        if distance > tolerance_seconds:
            continue

        if require_latency:
            evidence = fact.get("evidence", {})
            if (
                evidence.get("latency_before_ms") is None
                or evidence.get("latency_after_ms") is None
            ):
                continue

        candidates.append((distance, run_id, fact))

    candidates.sort(key=lambda item: (item[0], item[1], str(item[2].get("id"))))
    return [(run_id, fact) for _, run_id, fact in candidates]


def _link_name(
    graph: nx.MultiDiGraph,
    link_key: str,
) -> str:
    if link_key in graph:
        return str(graph.nodes[link_key].get("name", link_key))
    return link_key


def _congestion_candidates(
    graphs: list[nx.MultiDiGraph],
    records: list[tuple[str, Mapping[str, Any]]],
):
    graph_by_run = {_run_id(graph): graph for graph in graphs}
    candidates = []

    for run_id, fact in records:
        if fact.get("type") != "congestion":
            continue

        evidence = fact.get("evidence", {})
        link_key = str(evidence.get("link", ""))
        graph = graph_by_run.get(run_id)
        link_name = _link_name(graph, link_key) if graph else link_key

        maximum = evidence.get("max_queue")
        capacity = evidence.get("queue_capacity")
        if (
            isinstance(maximum, (int, float))
            and isinstance(capacity, (int, float))
            and capacity > 0
        ):
            saturation = float(maximum) / float(capacity)
        else:
            saturation = -1.0

        drops = evidence.get("queue_full_drop_count", 0)
        if not isinstance(drops, (int, float)):
            drops = 0

                                                               
        candidates.append((
            saturation,
            float(drops),
            run_id,
            link_name,
            fact,
        ))

    return candidates


def _run_window_summary(
    graph: nx.MultiDiGraph,
) -> dict[str, Any] | None:
    run_nodes = [
        data for _, data in graph.nodes(data=True)
        if data.get("node_type") == "Run"
    ]
    if not run_nodes:
        return None

    run = run_nodes[0]
    algorithm = str(run.get("algo") or "").lower()
    if not algorithm:
        return None

    window_rows = [
        data for _, data in graph.nodes(data=True)
        if data.get("node_type") == "Window"
    ]
    if not window_rows:
        return None

    def mean_metric(name: str) -> float | None:
        values = [
            float(row[name])
            for row in window_rows
            if isinstance(row.get(name), (int, float))
        ]
        return sum(values) / len(values) if values else None

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


def _best_algorithm(graphs: list[nx.MultiDiGraph]) -> dict[str, Any]:
    summaries = [
        summary
        for graph in graphs
        if (summary := _run_window_summary(graph)) is not None
    ]
    if not summaries:
        return _result(
            "best_algorithm",
            "Not enough data: the graphs need run algorithms and "
            "window latency measurements.",
        )

    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for summary in summaries:
        grouped[summary["algorithm"]].append(summary)

    algorithm_scores = []
    for algorithm, runs in grouped.items():
        latency = sum(run["avg_latency_ms"] for run in runs) / len(runs)
        throughput_values = [
            run["avg_throughput_mbps"]
            for run in runs
            if run["avg_throughput_mbps"] is not None
        ]
        loss_values = [
            run["loss_pct"]
            for run in runs
            if run["loss_pct"] is not None
        ]
        throughput = (
            sum(throughput_values) / len(throughput_values)
            if throughput_values else None
        )
        loss = sum(loss_values) / len(loss_values) if loss_values else None

        algorithm_scores.append({
            "algorithm": algorithm,
            "avg_latency_ms": latency,
            "avg_throughput_mbps": throughput,
            "loss_pct": loss,
            "runs": len(runs),
        })

                                                                  
                                                            
    winner = min(
        algorithm_scores,
        key=lambda row: (
            row["avg_latency_ms"],
            -(row["avg_throughput_mbps"]
              if row["avg_throughput_mbps"] is not None else float("-inf")),
            row["loss_pct"]
            if row["loss_pct"] is not None else float("inf"),
            row["algorithm"],
        ),
    )

    answer = (
        f"{winner['algorithm']} had the lowest mean window latency "
        f"({winner['avg_latency_ms']:.3f} ms) across "
        f"{winner['runs']} supplied run(s). This comparison defines "
        f"'best' as lowest average latency."
    )
    if winner["avg_throughput_mbps"] is not None:
        answer += (
            f" Its mean throughput was "
            f"{winner['avg_throughput_mbps']:.3f} Mbps."
        )
    if winner["loss_pct"] is not None:
        answer += f" Its mean loss was {winner['loss_pct']:.3f}%."

    return _result("best_algorithm", answer)


def _flow_label(data: Mapping[str, Any]) -> str:
    source = str(data.get("src_name") or data.get("src") or "?")
    destination = str(data.get("dst_name") or data.get("dst") or "?")
    return f"{source}->{destination}"


def _find_flow(
    graph: nx.MultiDiGraph,
    question: str,
) -> tuple[str, Mapping[str, Any]] | None:
    normalized_question = re.sub(r"\s+", "", question.lower())
    matches = []

    for key, data in graph.nodes(data=True):
        if data.get("node_type") != "Flow":
            continue

        source = str(data.get("src_name") or data.get("src") or "").lower()
        destination = str(data.get("dst_name") or data.get("dst") or "").lower()
        label = _flow_label(data).lower().replace(" ", "")

        if label and label in normalized_question:
            matches.append((key, data))
        elif (
            source
            and destination
            and source in question.lower()
            and destination in question.lower()
        ):
            matches.append((key, data))

    if len(matches) == 1:
        return matches[0]

                                                                           
    all_flows = [
        (key, data)
        for key, data in graph.nodes(data=True)
        if data.get("node_type") == "Flow"
    ]
    if not matches and len(all_flows) == 1:
        return all_flows[0]

    return None


def _observed_path(
    graph: nx.MultiDiGraph,
    flow_key: str,
    flow_data: Mapping[str, Any],
) -> list[str] | None:
    packets: dict[str, list[tuple[float, str]]] = defaultdict(list)

    for _, device_key, edge in graph.out_edges(flow_key, data=True):
        if edge.get("relation") != "ROUTED_VIA":
            continue

        packet_id = str(edge.get("packet_id", "")).strip()
        try:
            time_s = float(edge.get("time_s"))
        except (TypeError, ValueError):
            continue
        if not packet_id:
            continue

        device_name = str(graph.nodes[device_key].get("name", device_key))
        packets[packet_id].append((time_s, device_name))

    candidates = []
    for packet_id, observations in packets.items():
        observations.sort(key=lambda item: item[0])
        route = [name for _, name in observations]
        if route:
            candidates.append((observations[-1][0], packet_id, route))

    if not candidates:
        return None

    _, _, routers = max(candidates, key=lambda item: (item[0], item[1]))
    source = str(flow_data.get("src_name") or flow_data.get("src") or "?")
    destination = str(flow_data.get("dst_name") or flow_data.get("dst") or "?")

    path = [source]
    for device in routers:
        if device != path[-1]:
            path.append(device)
    if destination != path[-1]:
        path.append(destination)
    return path


def _fallback_search(
    records: list[tuple[str, Mapping[str, Any]]],
    question: str,
) -> dict[str, Any]:
    query_tokens = _tokens(question)
    if not query_tokens:
        return _result(
            "fact_search",
            "Not enough data: I could not identify useful search terms.",
        )

    scored = []
    for run_id, fact in records:
        text = str(fact.get("text", ""))
        overlap = len(query_tokens & _tokens(text))
        if overlap:
            scored.append((overlap, run_id, fact))

    scored.sort(
        key=lambda item: (
            -item[0],
            item[1],
            str(item[2].get("id")),
        )
    )
    selected = [(run_id, fact) for _, run_id, fact in scored[:3]]
    if not selected:
        return _result(
            "fact_search",
            "Not enough data: no stored fact matched the question.",
        )

    answer = "Related stored fact(s): " + " ".join(
        str(fact.get("text", "")) for _, fact in selected
    )
    return _result("fact_search", answer, selected)


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
    records = _fact_records(graph_list, facts_by_run)
    intent = _intent(question)

    if intent == "most_congested_link":
        candidates = _congestion_candidates(graph_list, records)
        if not candidates:
            return _result(
                intent,
                "Not enough data: no congestion facts are available.",
            )

        winner = max(
            candidates,
            key=lambda item: (
                item[0],
                item[1],
                item[2],
                item[3],
            ),
        )
        saturation, drops, run_id, link_name, fact = winner
        if saturation >= 0:
            score_text = f"queue reached {saturation * 100:.1f}% of capacity"
        else:
            score_text = "queue saturation could not be calculated"
        answer = (
            f"The most congested recorded link was {link_name} in {run_id}; "
            f"{score_text}, with {int(drops)} queue-full drop(s)."
        )
        return _result(intent, answer, [(run_id, fact)])

    if intent in {"drops_at_time", "latency_change_at_time"}:
        target_time = _time_from_question(question)
        if target_time is None:
            return _result(
                intent,
                "Please include a simulation time, such as 'at 20 seconds'.",
            )

        require_latency = intent == "latency_change_at_time"
        selected = _select_time_facts(
            records,
            target_time,
            "failure_impact",
            require_latency=require_latency,
            tolerance_seconds=time_tolerance_seconds,
        )
        if not selected:
            return _result(
                intent,
                f"Not enough data: no matching "
                f"{'latency-change' if require_latency else 'failure/drop'} "
                f"fact was recorded within "
                f"{time_tolerance_seconds:g} seconds of t={target_time:g}s.",
            )

        answer = " ".join(str(fact.get("text", "")) for _, fact in selected)
        return _result(intent, answer, selected)

    if intent == "best_algorithm":
        return _best_algorithm(graph_list)

    if intent == "flow_path":
        if len(graph_list) != 1:
            return _result(
                intent,
                "Please ask about a flow path in one run graph at a time.",
            )

        graph = graph_list[0]
        match = _find_flow(graph, question)
        if match is None:
            return _result(
                intent,
                "Not enough data: name one flow's source and destination.",
            )

        flow_key, flow_data = match
        path = _observed_path(graph, flow_key, flow_data)
        if path is None:
            return _result(
                intent,
                f"Not enough data: no observed forwarding path is available "
                f"for {_flow_label(flow_data)}.",
            )

        return _result(
            intent,
            f"Observed path for {_flow_label(flow_data)}: "
            + " -> ".join(path)
            + ".",
        )

    return _fallback_search(records, question)