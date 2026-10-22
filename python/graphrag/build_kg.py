"""Build a deterministic NetworkX knowledge graph for one NetMind run."""

from __future__ import annotations

import csv
import json
import pickle
import re
from pathlib import Path
from typing import Any

import networkx as nx


_NETWORK_EVENTS = {
    "LINK_DOWN",
    "LINK_UP",
    "LINK_DEGRADED",
    "ROUTER_DOWN",
    "ROUTER_UP",
    "CONGESTION",
    "CONGESTION_END",
    "TRAFFIC_BURST",
    "ROUTE_UPDATE",
}


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
    if isinstance(metadata, dict):
        return metadata
    if isinstance(metadata, str):
        try:
            value = json.loads(metadata)
        except json.JSONDecodeError:
            return {}
        return value if isinstance(value, dict) else {}
    return {}


def _find_topology_path(
    manifest: dict[str, Any],
    metadata: dict[str, Any],
    explicit_path: str | Path | None,
) -> Path:
    candidates: list[str | Path] = []

    if explicit_path is not None:
        candidates.append(explicit_path)

    for key in ("topology_path", "topology_file"):
        value = metadata.get(key)
        if value:
            candidates.append(str(value))

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

    if number.is_integer():
        return int(number)
    return number


def _is_true(value: Any) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes", "anomalous"}


def _detail_fields(detail: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for item in detail.split(";"):
        if "=" in item:
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
    return fields


def _compact_location(value: str) -> str:
    return re.sub(r"[\s>.-]+", "", value).lower()


def _build_link_target_lookup(
    links: list[dict[str, Any]],
    names_by_id: dict[str, str],
    link_keys: dict[int, str],
) -> dict[str, str]:
    lookup: dict[str, str] = {}

    for index, link in enumerate(links):
        a_name = names_by_id[str(link["a"])]
        b_name = names_by_id[str(link["b"])]
        node_key = link_keys[index]

        for first, second in ((a_name, b_name), (b_name, a_name)):
            for separator in ("-", ">", "->", ""):
                lookup[_compact_location(f"{first}{separator}{second}")] = (
                    node_key
                )

    return lookup


def _resolve_target(
    location: str,
    device_keys_by_name: dict[str, str],
    link_targets: dict[str, str],
) -> str | None:
    if location in device_keys_by_name:
        return device_keys_by_name[location]

    return link_targets.get(_compact_location(location))


def _add_run_node(
    graph: nx.MultiDiGraph,
    run_id: str,
    manifest: dict[str, Any],
    metadata: dict[str, Any],
) -> str:
    run_key = f"run:{run_id}"
    graph.add_node(
        run_key,
        node_type="Run",
        id=run_id,
        algo=metadata.get("algorithm", manifest.get("algorithm")),
        seed=_number(metadata.get("seed", manifest.get("seed"))),
    )
    return run_key


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
    topology_file = _find_topology_path(
        manifest,
        metadata,
        topology_path,
    )
    topology = _read_json(topology_file)

    nodes = topology.get("nodes")
    links = topology.get("links")
    if not isinstance(nodes, list) or not isinstance(links, list):
        raise ValueError("Topology JSON must contain nodes and links arrays")

    trace_rows = _read_csv(directory / f"{run_id}_trace.csv")
    flow_rows = _read_csv(directory / f"{run_id}_flows.csv")
    link_rows = _read_csv(directory / f"{run_id}_links.csv")
    window_rows = _read_csv(directory / f"{run_id}_windows.csv")

    graph = nx.MultiDiGraph()
    graph.graph.update(
        schema="Project901 GraphRAG run graph v1",
        run_id=run_id,
        topology_path=str(topology_file),
    )

    run_key = _add_run_node(graph, run_id, manifest, metadata)

    names_by_id = {
        str(node["id"]): str(node["name"])
        for node in nodes
    }
    device_keys_by_name: dict[str, str] = {}

    for node in nodes:
        node_id = str(node["id"])
        name = str(node["name"])
        device_key = f"device:{node_id}"
        device_keys_by_name[name] = device_key

        graph.add_node(
            device_key,
            node_type="Device",
            id=node_id,
            name=name,
            device_type=node.get("type"),
            ip=node.get("ip"),
        )

    link_keys_by_index: dict[int, str] = {}
    endpoint_to_link: dict[frozenset[str], str] = {}

    for index, link in enumerate(links):
        a_id = str(link["a"])
        b_id = str(link["b"])
        a_name = names_by_id[a_id]
        b_name = names_by_id[b_id]
        name = f"{a_name}-{b_name}"
        link_key = f"link:{index}"

        link_keys_by_index[index] = link_key
        endpoint_to_link[frozenset((a_id, b_id))] = link_key

        graph.add_node(
            link_key,
            node_type="Link",
            name=name,
            bw=_number(link.get("bandwidthMbps")),
            latency=_number(link.get("latencyMs")),
            loss=_number(link.get("lossRate")),
            queue_capacity=_number(link.get("queueCapacity")),
        )

        device_a = f"device:{a_id}"
        device_b = f"device:{b_id}"
        graph.add_edge(
            device_a,
            link_key,
            relation="CONNECTED_TO",
        )
        graph.add_edge(
            link_key,
            device_b,
            relation="CONNECTED_TO",
        )
        graph.add_edge(
            device_b,
            link_key,
            relation="CONNECTED_TO",
        )
        graph.add_edge(
            link_key,
            device_a,
            relation="CONNECTED_TO",
        )

    link_targets = _build_link_target_lookup(
        links,
        names_by_id,
        link_keys_by_index,
    )

                                                                                
    for row in link_rows:
        target = link_targets.get(_compact_location(row.get("channel", "")))
        if target is None:
            continue
        for field, value in row.items():
            if field == "channel":
                continue
            parsed = _number(value)
            if parsed is not None:
                graph.nodes[target][field] = parsed

    flow_keys: dict[str, str] = {}
    for row in flow_rows:
        flow_id = row.get("flow_id", "").strip()
        if not flow_id:
            continue

        flow_key = f"flow:{run_id}:{flow_id}"
        flow_keys[flow_id] = flow_key
        src_id = row.get("src", "").strip()
        dst_id = row.get("dst", "").strip()

        graph.add_node(
            flow_key,
            node_type="Flow",
            id=flow_id,
            src=src_id,
            dst=dst_id,
            src_name=names_by_id.get(src_id),
            dst_name=names_by_id.get(dst_id),
        )
        graph.add_edge(
            flow_key,
            run_key,
            relation="RAN_IN",
        )

                                                                       
    routed_pairs: set[tuple[str, str, str, str]] = set()
    for row in trace_rows:
        if row.get("event_type", "").upper() != "FORWARD":
            continue

        flow_id = row.get("flow_id", "").strip()
        location = row.get("location", "").strip()
        flow_key = flow_keys.get(flow_id)
        device_key = device_keys_by_name.get(location)
        if flow_key is None or device_key is None:
            continue

        time_s = _number(row.get("time_s"))
        packet_id = row.get("packet_id", "")
        pair = (flow_key, device_key, str(time_s), packet_id)
        if pair in routed_pairs:
            continue
        routed_pairs.add(pair)

        graph.add_edge(
            flow_key,
            device_key,
            relation="ROUTED_VIA",
            time_s=time_s,
            packet_id=packet_id,
        )

                                                                               
    for index, row in enumerate(trace_rows):
        event_type = row.get("event_type", "").upper()
        if event_type not in _NETWORK_EVENTS:
            continue

        event_key = f"event:{run_id}:{index}"
        location = row.get("location", "").strip()
        flow_id = row.get("flow_id", "").strip()
        fields = _detail_fields(row.get("detail", ""))

        graph.add_node(
            event_key,
            node_type="Event",
            type=event_type,
            target=location or None,
            time=_number(row.get("time_s")),
            detail=row.get("detail", ""),
        )
        graph.add_edge(
            event_key,
            run_key,
            relation="RAN_IN",
        )

        target_key = _resolve_target(
            location,
            device_keys_by_name,
            link_targets,
        )
        if target_key is not None:
            graph.add_edge(
                event_key,
                target_key,
                relation="TARGETS",
            )

        flow_key = flow_keys.get(flow_id)
        if flow_key is not None:
            graph.add_edge(
                event_key,
                flow_key,
                relation="AFFECTED",
                detail=row.get("detail", ""),
            )

                                                                            
                                                          
    for index, row in enumerate(window_rows):
        time_s = _number(row.get("t_start"))
        window_key = f"window:{run_id}:{index}"

        metrics: dict[str, int | float] = {}
        for field, value in row.items():
            if field in {"t_start", "anomalous", "predicted_anomalous"}:
                continue
            parsed = _number(value)
            if parsed is not None:
                metrics[field] = parsed

        graph.add_node(
            window_key,
            node_type="Window",
            t_start=time_s,
            **metrics,
        )
        graph.add_edge(
            run_key,
            window_key,
            relation="HAS_WINDOW",
        )

        anomaly_value = row.get(
            "predicted_anomalous",
            row.get("anomalous", ""),
        )
        if _is_true(anomaly_value):
            anomaly_key = f"anomaly:{run_id}:{index}"
            score_value = row.get(
                "anomaly_score",
                row.get("score", ""),
            )
            graph.add_node(
                anomaly_key,
                node_type="Anomaly",
                window=window_key,
                score=_number(score_value),
            )
            graph.add_edge(
                anomaly_key,
                window_key,
                relation="FLAGGED",
            )

    if cache_path is not None:
        destination = Path(cache_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("wb") as stream:
            pickle.dump(graph, stream, protocol=pickle.HIGHEST_PROTOCOL)

    return graph