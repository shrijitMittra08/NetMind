"""Extract deterministic, evidence-backed facts from a NetMind run graph."""

from __future__ import annotations

import math
import re
from typing import Any, Iterable, Mapping

import networkx as nx


def _time(value: Any) -> float | None:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def _compact(value: Any) -> str:
    """Normalize endpoint names such as 'A-B' and 'A>B' for comparison."""
    return re.sub(r"[\s>.-]+", "", str(value or "")).lower()


def _same_target(first: Any, second: Any) -> bool:
    first_key = _compact(first)
    second_key = _compact(second)
    return bool(first_key and second_key and first_key == second_key)


def _detail_fields(detail: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for item in detail.split(";"):
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
    return fields


def _nodes_of_type(graph: nx.MultiDiGraph, node_type: str):
    return [
        (key, data)
        for key, data in graph.nodes(data=True)
        if data.get("node_type") == node_type
    ]


def _relation_targets(
    graph: nx.MultiDiGraph,
    source: str,
    relation: str,
):
    for _, target, data in graph.out_edges(source, data=True):
        if data.get("relation") == relation:
            yield target, data


def _flow_name(data: Mapping[str, Any]) -> str:
    source = data.get("src_name") or data.get("src") or "?"
    destination = data.get("dst_name") or data.get("dst") or "?"
    return f"{source}->{destination}"


def _run_end_time(trace_rows: list[dict[str, Any]]) -> float:
    times = [
        value
        for row in trace_rows
        if (value := _time(row.get("time_s"))) is not None
    ]
    return max(times, default=0.0)


def _matching_event_end(
    graph: nx.MultiDiGraph,
    event_type: str,
    target: str,
    start_time: float,
) -> float | None:
    candidates = [
        event_time
        for _, event in _nodes_of_type(graph, "Event")
        if event.get("type") == event_type
        and _same_target(event.get("target"), target)
        and (event_time := _time(event.get("time"))) is not None
        and event_time > start_time
    ]
    return min(candidates) if candidates else None


def _trace_drops(
    trace_rows: list[dict[str, Any]],
    start_time: float,
    end_time: float,
    reason: str,
    target: str,
) -> list[dict[str, Any]]:
    matches = []

    for row in trace_rows:
        if str(row.get("event_type", "")).upper() != "DROPPED":
            continue

        event_time = _time(row.get("time_s"))
        if event_time is None or not start_time <= event_time <= end_time:
            continue

        fields = _detail_fields(str(row.get("detail", "")))
        if fields.get("reason", "").lower() != reason.lower():
            continue
        if not _same_target(row.get("location"), target):
            continue

        matches.append(row)

    return matches


def _packet_paths(
    graph: nx.MultiDiGraph,
    flow_key: str,
) -> dict[str, list[tuple[float, str]]]:
    """Group forwarding observations by packet, in time order."""
    packets: dict[str, list[tuple[float, str]]] = {}

    for device_key, edge in _relation_targets(graph, flow_key, "ROUTED_VIA"):
        observed_time = _time(edge.get("time_s"))
        packet_id = str(edge.get("packet_id", "")).strip()
        if observed_time is None or not packet_id:
            continue

        device_name = str(graph.nodes[device_key].get("name", device_key))
        packets.setdefault(packet_id, []).append((observed_time, device_name))

    for observations in packets.values():
        observations.sort(key=lambda item: item[0])

    return packets


def _path_before(
    packets: Mapping[str, list[tuple[float, str]]],
    boundary_time: float,
) -> tuple[str, ...] | None:
    candidates = []

    for packet_id, observations in packets.items():
        if observations and observations[-1][0] < boundary_time:
            path = tuple(name for _, name in observations)
            candidates.append((observations[-1][0], packet_id, path))

    if not candidates:
        return None

                                                                             
    return max(candidates, key=lambda item: (item[0], item[1]))[2]


def _path_after(
    packets: Mapping[str, list[tuple[float, str]]],
    boundary_time: float,
) -> tuple[str, ...] | None:
    candidates = []

    for packet_id, observations in packets.items():
        if observations and observations[0][0] >= boundary_time:
            path = tuple(name for _, name in observations)
            candidates.append((observations[0][0], packet_id, path))

    if not candidates:
        return None

                                                                                
    return min(candidates, key=lambda item: (item[0], item[1]))[2]


def _changed_flows(
    graph: nx.MultiDiGraph,
    failure_time: float,
    after_time: float,
) -> list[dict[str, Any]]:
    changed = []

    for flow_key, flow_data in _nodes_of_type(graph, "Flow"):
        packets = _packet_paths(graph, flow_key)
        before = _path_before(packets, failure_time)
        after = _path_after(packets, after_time)

        if before is not None and after is not None and before != after:
            changed.append({
                "name": _flow_name(flow_data),
                "before": list(before),
                "after": list(after),
            })

    return sorted(changed, key=lambda item: item["name"])


def _nearest_latency(
    graph: nx.MultiDiGraph,
    reference_time: float,
    *,
    before: bool,
) -> float | None:
    candidates: list[tuple[float, float]] = []

    for _, window in _nodes_of_type(graph, "Window"):
        window_time = _time(window.get("t_start"))
        latency = _time(window.get("avg_latency_ms"))
        if window_time is None or latency is None:
            continue

        if before and window_time < reference_time:
            candidates.append((window_time, latency))
        elif not before and window_time >= reference_time:
            candidates.append((window_time, latency))

    if not candidates:
        return None

    chosen = max(candidates) if before else min(candidates)
    return chosen[1]


def _failure_facts(
    graph: nx.MultiDiGraph,
    trace_rows: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    facts: list[dict[str, Any]] = []
    run_end = _run_end_time(trace_rows)

    restore_types = {
        "LINK_DOWN": ("LINK_UP", "link_down"),
        "ROUTER_DOWN": ("ROUTER_UP", "router_down"),
    }

    for _, event in _nodes_of_type(graph, "Event"):
        event_type = event.get("type")
        if event_type not in restore_types:
            continue

        start_time = _time(event.get("time"))
        if start_time is None:
            continue

        target = str(event.get("target") or "")
        if not target:
            continue

        restore_type, drop_reason = restore_types[event_type]
        restore_time = _matching_event_end(
            graph,
            restore_type,
            target,
            start_time,
        )
        end_time = restore_time if restore_time is not None else run_end

        drops = _trace_drops(
            trace_rows,
            start_time,
            end_time,
            drop_reason,
            target,
        )

        changed = (
            _changed_flows(graph, start_time, restore_time)
            if restore_time is not None
            else []
        )

        latency_before = _nearest_latency(
            graph,
            start_time,
            before=True,
        )
        latency_after = (
            _nearest_latency(graph, restore_time, before=False)
            if restore_time is not None
            else None
        )

        parts = [
            f"{event_type} on {target} at t={start_time:.3f}s",
            f"{len(drops)} matching {drop_reason} drop(s) recorded",
        ]

        if restore_time is not None:
            parts.append(
                f"{restore_type} was recorded at t={restore_time:.3f}s "
                f"({restore_time - start_time:.3f}s after the failure)"
            )
        else:
            parts.append("no matching restore event was recorded")

        if changed:
            descriptions = [
                f"{item['name']} ({' -> '.join(item['before'])} "
                f"to {' -> '.join(item['after'])})"
                for item in changed
            ]
            parts.append("observed packet paths changed for " + ", ".join(descriptions))

        if latency_before is not None and latency_after is not None:
            delta = latency_after - latency_before
            parts.append(
                f"nearest window latency changed from {latency_before:.3f} ms "
                f"to {latency_after:.3f} ms ({delta:+.3f} ms)"
            )

        facts.append({
            "time": start_time,
            "type": "failure_impact",
            "text": "; ".join(parts) + ".",
            "evidence": {
                "event_type": event_type,
                "target": target,
                "start_s": start_time,
                "restore_time_s": restore_time,
                "drop_count": len(drops),
                "drop_times_s": [
                    _time(row.get("time_s")) for row in drops
                ],
                "changed_flows": changed,
                "latency_before_ms": latency_before,
                "latency_after_ms": latency_after,
            },
        })

    return facts


def _targeted_links(
    graph: nx.MultiDiGraph,
    event_key: str,
) -> list[str]:
    return [
        target
        for target, _ in _relation_targets(graph, event_key, "TARGETS")
        if graph.nodes[target].get("node_type") == "Link"
    ]


def _flows_observed_on_target(
    trace_rows: list[dict[str, Any]],
    target: str,
    graph: nx.MultiDiGraph,
) -> list[str]:
    names_by_id = {
        str(data.get("id")): _flow_name(data)
        for _, data in _nodes_of_type(graph, "Flow")
    }
    flow_names = set()

    for row in trace_rows:
        event_type = str(row.get("event_type", "")).upper()
        if event_type not in {"ENQUEUE", "TX_START", "DROPPED"}:
            continue
        if not _same_target(row.get("location"), target):
            continue

        flow_id = str(row.get("flow_id", "")).strip()
        if flow_id and flow_id != "-" and flow_id in names_by_id:
            flow_names.add(names_by_id[flow_id])

    return sorted(flow_names)


def _congestion_facts(
    graph: nx.MultiDiGraph,
    trace_rows: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    facts: list[dict[str, Any]] = []
    run_end = _run_end_time(trace_rows)

    for event_key, event in _nodes_of_type(graph, "Event"):
        if event.get("type") != "CONGESTION":
            continue

        start_time = _time(event.get("time"))
        if start_time is None:
            continue

        target = str(event.get("target") or "")
        if not target:
            continue

        end_time = _matching_event_end(
            graph,
            "CONGESTION_END",
            target,
            start_time,
        )
        if end_time is None:
            end_time = run_end

        queue_drops = _trace_drops(
            trace_rows,
            start_time,
            end_time,
            "queue_full",
            target,
        )

        for link_key in _targeted_links(graph, event_key):
            link = graph.nodes[link_key]
            capacity = _time(link.get("queue_capacity"))
            maximum = _time(link.get("max_queue"))
            crossing_flows = _flows_observed_on_target(
                trace_rows,
                target,
                graph,
            )

            parts = [
                f"CONGESTION at t={start_time:.3f}s targeted link "
                f"{link.get('name', link_key)}"
            ]
            if capacity is not None and maximum is not None:
                parts.append(
                    f"maximum observed queue was {maximum:g} packets "
                    f"against capacity {capacity:g}"
                )
            else:
                parts.append("queue-capacity measurements were unavailable")

            parts.append(
                f"{len(queue_drops)} matching queue_full drop(s) recorded "
                f"between t={start_time:.3f}s and t={end_time:.3f}s"
            )
            if crossing_flows:
                parts.append(
                    "flow(s) observed on the affected channel: "
                    + ", ".join(crossing_flows)
                )

            facts.append({
                "time": start_time,
                "type": "congestion",
                "text": "; ".join(parts) + ".",
                "evidence": {
                    "link": link_key,
                    "target": target,
                    "start_s": start_time,
                    "end_s": end_time,
                    "queue_capacity": capacity,
                    "max_queue": maximum,
                    "queue_full_drop_count": len(queue_drops),
                    "crossing_flows": crossing_flows,
                },
            })

    return facts


def _anomaly_facts(
    graph: nx.MultiDiGraph,
    lookback_seconds: float,
) -> list[dict[str, Any]]:
    facts: list[dict[str, Any]] = []
    events = _nodes_of_type(graph, "Event")

    for anomaly_key, anomaly in _nodes_of_type(graph, "Anomaly"):
        window_key = anomaly.get("window")
        if window_key not in graph:
            continue

        window = graph.nodes[window_key]
        window_time = _time(window.get("t_start"))
        if window_time is None:
            continue

        preceding = []
        for event_key, event in events:
            event_time = _time(event.get("time"))
            if (
                event_time is not None
                and event_time <= window_time
                and window_time - event_time <= lookback_seconds
            ):
                preceding.append((event_time, event_key, event))

        if preceding:
            event_time, event_key, event = max(
                preceding,
                key=lambda item: item[0],
            )
            text = (
                f"Anomaly {anomaly_key} flags window at t={window_time:.3f}s; "
                f"nearest preceding event was {event.get('type')} on "
                f"{event.get('target') or 'an unspecified target'} at "
                f"t={event_time:.3f}s."
            )
            nearest_event = event_key
        else:
            text = (
                f"Anomaly {anomaly_key} flags window at t={window_time:.3f}s; "
                f"no recorded event occurred within the preceding "
                f"{lookback_seconds:.3f}s."
            )
            nearest_event = None

        facts.append({
            "time": window_time,
            "type": "anomaly_context",
            "text": text,
            "evidence": {
                "anomaly": anomaly_key,
                "window": window_key,
                "score": anomaly.get("score"),
                "nearest_event": nearest_event,
                "lookback_seconds": lookback_seconds,
            },
        })

    return facts


def extract_facts(
    graph: nx.MultiDiGraph,
    trace_rows: Iterable[dict[str, Any]],
    anomaly_lookback_seconds: float = 1.0,
) -> list[dict[str, Any]]:
    """Return stable, fact-cited explanations for one run graph.

    ``trace_rows`` should contain dictionaries read from that run's trace CSV.
    Each returned fact has ``id``, ``type``, ``text``, and ``evidence`` fields.
    """
    if not isinstance(graph, nx.MultiDiGraph):
        raise TypeError("graph must be a NetworkX MultiDiGraph")
    if (
        not math.isfinite(anomaly_lookback_seconds)
        or anomaly_lookback_seconds < 0
    ):
        raise ValueError(
            "anomaly_lookback_seconds must be finite and non-negative"
        )

    rows = list(trace_rows)
    facts = (
        _failure_facts(graph, rows)
        + _congestion_facts(graph, rows)
        + _anomaly_facts(graph, anomaly_lookback_seconds)
    )
    facts.sort(
        key=lambda fact: (
            fact["time"],
            fact["type"],
            fact["text"],
        )
    )

    for index, fact in enumerate(facts, start=1):
        fact["id"] = f"F{index:03d}"

    return facts