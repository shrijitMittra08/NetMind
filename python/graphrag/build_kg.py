from __future__ import annotations

import csv
import json
import pickle
import re
from pathlib import Path
from typing import Any

import networkx as nx

_NETWORK_EVENTS = {
    "LINK_DOWN", "LINK_UP", "LINK_DEGRADED", "ROUTER_DOWN", "ROUTER_UP",
    "CONGESTION", "CONGESTION_END", "TRAFFIC_BURST", "ROUTE_UPDATE",
}

_NON_METRIC_WINDOW_COLUMNS = {"t_start", "anomalous", "predicted_anomalous"}


def _read_csv(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise FileNotFoundError(f"Run file not found: {path}")
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None:
            raise ValueError(f"CSV file has no header: {path}")
        return list(reader)


def _read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(f"JSON file not found: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        raise ValueError(f"Invalid JSON in {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object in {path}")
    return value


def _metadata_dict(manifest: dict[str, Any]) -> dict[str, Any]:
    metadata = manifest.get("dataset_metadata", {})
    if isinstance(metadata, str):
        try:
            metadata = json.loads(metadata)
        except json.JSONDecodeError:
            return {}
    return metadata if isinstance(metadata, dict) else {}


def _find_topology_path(manifest: dict[str, Any], metadata: dict[str, Any], explicit_path: str | Path | None) -> Path:
    candidates: list[str | Path] = []
    if explicit_path is not None:
        candidates.append(explicit_path)
    for key in ("topology_path", "topology_file"):
        if metadata.get(key):
            candidates.append(str(metadata[key]))
    value = manifest.get("topology")
    if value and str(value).lower() not in {"loaded topology", "unknown"}:
        candidates.append(str(value))

    for candidate in candidates:
        path = Path(candidate).expanduser()
        if path.is_file():
            return path.resolve()
    raise ValueError(
        "Could not locate the topology JSON. Pass topology_path explicitly "
        "or include topology_path in the manifest dataset_metadata."
    )


def _number(value: Any) -> int | float | None:
    if value is None or value == "":
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return int(number) if number.is_integer() else number


def _compact_location(value: str) -> str:
    return re.sub(r"[\s>.-]+", "", value).lower()


def _numeric_fields(row: dict[str, str], skip: set[str]) -> dict[str, int | float]:
    fields = {}
    for field, value in row.items():
        parsed = _number(value)
        if field not in skip and parsed is not None:
            fields[field] = parsed
    return fields


def _add_devices(graph: nx.MultiDiGraph, nodes: list[dict[str, Any]]) -> dict[str, str]:
    device_keys_by_name: dict[str, str] = {}
    for node in nodes:
        node_id, name = str(node["id"]), str(node["name"])
        device_keys_by_name[name] = f"device:{node_id}"
        graph.add_node(f"device:{node_id}", node_type="Device", id=node_id, name=name,
                       device_type=node.get("type"), ip=node.get("ip"))
    return device_keys_by_name


def _add_links(graph: nx.MultiDiGraph, links: list[dict[str, Any]], names_by_id: dict[str, str]) -> dict[str, str]:
    link_targets: dict[str, str] = {}
    for index, link in enumerate(links):
        a_name, b_name = names_by_id[str(link["a"])], names_by_id[str(link["b"])]
        link_key = f"link:{index}"
        graph.add_node(
            link_key,
            node_type="Link",
            name=f"{a_name}-{b_name}",
            bw=_number(link.get("bandwidthMbps")),
            latency=_number(link.get("latencyMs")),
            loss=_number(link.get("lossRate")),
            queue_capacity=_number(link.get("queueCapacity")),
        )
        device_a, device_b = f"device:{link['a']}", f"device:{link['b']}"
        graph.add_edge(device_a, link_key, relation="CONNECTED_TO")
        graph.add_edge(link_key, device_b, relation="CONNECTED_TO")
        graph.add_edge(device_b, link_key, relation="CONNECTED_TO")
        graph.add_edge(link_key, device_a, relation="CONNECTED_TO")

        link_targets[str(index + 1)] = link_key
        for first, second in ((a_name, b_name), (b_name, a_name)):
            for separator in ("-", ">", "->", ""):
                link_targets[_compact_location(f"{first}{separator}{second}")] = link_key
    return link_targets


def _add_flows(graph: nx.MultiDiGraph, run_id: str, run_key: str, flow_rows: list[dict[str, str]],
               names_by_id: dict[str, str]) -> dict[str, str]:
    flow_keys: dict[str, str] = {}
    for row in flow_rows:
        flow_id = row.get("flow_id", "").strip()
        if not flow_id:
            continue
        flow_key = f"flow:{run_id}:{flow_id}"
        flow_keys[flow_id] = flow_key
        src_id, dst_id = row.get("src", "").strip(), row.get("dst", "").strip()
        graph.add_node(flow_key, node_type="Flow", id=flow_id, src=src_id, dst=dst_id,
                       src_name=names_by_id.get(src_id), dst_name=names_by_id.get(dst_id))
        graph.add_edge(flow_key, run_key, relation="RAN_IN")
    return flow_keys


def _add_routed_via(graph: nx.MultiDiGraph, trace_rows: list[dict[str, str]], flow_keys: dict[str, str],
                    device_keys_by_name: dict[str, str]) -> None:
    routed_pairs: set[tuple[str, str, str, str]] = set()
    for row in trace_rows:
        if row.get("event_type", "").upper() != "FORWARD":
            continue
        flow_key = flow_keys.get(row.get("flow_id", "").strip())
        device_key = device_keys_by_name.get(row.get("location", "").strip())
        if flow_key is None or device_key is None:
            continue
        time_s = _number(row.get("time_s"))
        packet_id = row.get("packet_id", "")
        pair = (flow_key, device_key, str(time_s), packet_id)
        if pair in routed_pairs:
            continue
        routed_pairs.add(pair)
        graph.add_edge(flow_key, device_key, relation="ROUTED_VIA", time_s=time_s, packet_id=packet_id)


def _add_events(graph: nx.MultiDiGraph, run_id: str, run_key: str, trace_rows: list[dict[str, str]],
                flow_keys: dict[str, str], device_keys_by_name: dict[str, str], link_targets: dict[str, str]) -> None:
    for index, row in enumerate(trace_rows):
        event_type = row.get("event_type", "").upper()
        if event_type not in _NETWORK_EVENTS:
            continue
        event_key = f"event:{run_id}:{index}"
        location = row.get("location", "").strip()
        graph.add_node(event_key, node_type="Event", type=event_type, target=location or None,
                       time=_number(row.get("time_s")), detail=row.get("detail", ""))
        graph.add_edge(event_key, run_key, relation="RAN_IN")

        if location in device_keys_by_name:
            target_key = device_keys_by_name[location]
        else:
            target_key = link_targets.get(_compact_location(location))
        if target_key is not None:
            graph.add_edge(event_key, target_key, relation="TARGETS")

        flow_key = flow_keys.get(row.get("flow_id", "").strip())
        if flow_key is not None:
            graph.add_edge(event_key, flow_key, relation="AFFECTED", detail=row.get("detail", ""))


def _add_windows(graph: nx.MultiDiGraph, run_id: str, run_key: str, window_rows: list[dict[str, str]]) -> None:
    for index, row in enumerate(window_rows):
        window_key = f"window:{run_id}:{index}"
        metrics = _numeric_fields(row, _NON_METRIC_WINDOW_COLUMNS)
        graph.add_node(window_key, node_type="Window", t_start=_number(row.get("t_start")), **metrics)
        graph.add_edge(run_key, window_key, relation="HAS_WINDOW")

        anomaly_value = row.get("predicted_anomalous", row.get("anomalous", ""))
        if str(anomaly_value).strip().lower() in {"1", "true", "yes", "anomalous"}:
            anomaly_key = f"anomaly:{run_id}:{index}"
            score = _number(row.get("anomaly_score", row.get("score", "")))
            graph.add_node(anomaly_key, node_type="Anomaly", window=window_key, score=score)
            graph.add_edge(anomaly_key, window_key, relation="FLAGGED")


def build_run_graph(
    runs_directory: str | Path,
    run_id: str,
    topology_path: str | Path | None = None,
    cache_path: str | Path | None = None,
) -> nx.MultiDiGraph:
    if not run_id or Path(run_id).name != run_id:
        raise ValueError("run_id must be a non-empty filename component")

    directory = Path(runs_directory)
    manifest = _read_json(directory / f"{run_id}_manifest.json")
    metadata = _metadata_dict(manifest)
    topology_file = _find_topology_path(manifest, metadata, topology_path)
    topology = _read_json(topology_file)
    nodes, links = topology.get("nodes"), topology.get("links")
    if not isinstance(nodes, list) or not isinstance(links, list):
        raise ValueError("Topology JSON must contain nodes and links arrays")

    trace_rows = _read_csv(directory / f"{run_id}_trace.csv")
    flow_rows = _read_csv(directory / f"{run_id}_flows.csv")
    link_rows = _read_csv(directory / f"{run_id}_links.csv")
    window_rows = _read_csv(directory / f"{run_id}_windows.csv")

    graph = nx.MultiDiGraph()
    graph.graph.update(schema="Project901 GraphRAG run graph v1", run_id=run_id, topology_path=str(topology_file))

    run_key = f"run:{run_id}"
    graph.add_node(run_key, node_type="Run", id=run_id, algo=metadata.get("algorithm", manifest.get("algorithm")),
                   seed=_number(metadata.get("seed", manifest.get("seed"))))
    names_by_id = {str(node["id"]): str(node["name"]) for node in nodes}
    device_keys_by_name = _add_devices(graph, nodes)
    link_targets = _add_links(graph, links, names_by_id)
    for row in link_rows:
        target = link_targets.get(_compact_location(row.get("channel", "")))
        if target is not None:
            graph.nodes[target].update(_numeric_fields(row, {"channel"}))
    flow_keys = _add_flows(graph, run_id, run_key, flow_rows, names_by_id)
    _add_routed_via(graph, trace_rows, flow_keys, device_keys_by_name)
    _add_events(graph, run_id, run_key, trace_rows, flow_keys, device_keys_by_name, link_targets)
    _add_windows(graph, run_id, run_key, window_rows)

    if cache_path is not None:
        destination = Path(cache_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("wb") as stream:
            pickle.dump(graph, stream, protocol=pickle.HIGHEST_PROTOCOL)
    return graph
