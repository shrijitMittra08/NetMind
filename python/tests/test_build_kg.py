import csv
import json

from graphrag.build_kg import build_run_graph


def write_csv(path, columns, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def test_build_graph_connects_topology_run_flow_events_and_windows(tmp_path):
    run_id = "run_001"
    topology_path = tmp_path / "topology.json"
    topology_path.write_text(
        json.dumps({
            "nodes": [
                {"id": 1, "type": "HOST", "name": "PC1", "ip": "10.0.0.1"},
                {"id": 2, "type": "ROUTER", "name": "R1", "ip": "10.0.0.2"},
                {"id": 3, "type": "HOST", "name": "PC2", "ip": "10.0.0.3"},
            ],
            "links": [
                {
                    "a": 1,
                    "b": 2,
                    "bandwidthMbps": 100,
                    "latencyMs": 2,
                    "lossRate": 0.01,
                    "queueCapacity": 64,
                },
                {
                    "a": 2,
                    "b": 3,
                    "bandwidthMbps": 50,
                    "latencyMs": 4,
                    "lossRate": 0.0,
                    "queueCapacity": 32,
                },
            ],
        }),
        encoding="utf-8",
    )

    manifest_path = tmp_path / f"{run_id}_manifest.json"
    manifest_path.write_text(
        json.dumps({
            "algorithm": "dijkstra",
            "seed": 7,
            "dataset_metadata": {
                "topology_path": str(topology_path),
                "algorithm": "dijkstra",
                "seed": 7,
            },
        }),
        encoding="utf-8",
    )

    write_csv(
        tmp_path / f"{run_id}_trace.csv",
        ["time_s", "event_type", "packet_id", "flow_id",
         "location", "detail"],
        [
            {
                "time_s": "1.0",
                "event_type": "FORWARD",
                "packet_id": "10",
                "flow_id": "1",
                "location": "R1",
                "detail": "next=PC2",
            },
            {
                "time_s": "2.0",
                "event_type": "LINK_DOWN",
                "packet_id": "-",
                "flow_id": "-",
                "location": "PC1-R1",
                "detail": "-",
            },
        ],
    )
    write_csv(
        tmp_path / f"{run_id}_flows.csv",
        ["flow_id", "src", "dst", "sent", "delivered", "dropped",
         "loss_pct"],
        [{
            "flow_id": "1",
            "src": "1",
            "dst": "3",
            "sent": "10",
            "delivered": "9",
            "dropped": "1",
            "loss_pct": "10",
        }],
    )
    write_csv(
        tmp_path / f"{run_id}_links.csv",
        ["channel", "utilization_pct", "avg_queue"],
        [{"channel": "PC1-R1", "utilization_pct": "75", "avg_queue": "3"}],
    )
    write_csv(
        tmp_path / f"{run_id}_windows.csv",
        ["t_start", "avg_latency_ms", "loss_pct", "anomalous", "score"],
        [{
            "t_start": "2.0",
            "avg_latency_ms": "8.5",
            "loss_pct": "10",
            "anomalous": "true",
            "score": "0.91",
        }],
    )

    cache_path = tmp_path / "cache" / "graph.pkl"
    graph = build_run_graph(
        tmp_path,
        run_id,
        cache_path=cache_path,
    )

    assert graph.is_multigraph()
    assert graph.nodes["run:run_001"]["algo"] == "dijkstra"
    assert graph.nodes["device:2"]["name"] == "R1"
    assert graph.nodes["link:0"]["bw"] == 100
    assert graph.nodes["link:0"]["utilization_pct"] == 75

    flow_key = "flow:run_001:1"
    assert graph.has_edge(flow_key, "run:run_001")
    assert any(
        data["relation"] == "ROUTED_VIA"
        for _, _, data in graph.edges(flow_key, data=True)
    )

    event_nodes = [
        key for key, data in graph.nodes(data=True)
        if data.get("node_type") == "Event"
    ]
    assert len(event_nodes) == 1
    event_key = event_nodes[0]
    assert graph.has_edge(event_key, "run:run_001")
    assert any(
        data["relation"] == "TARGETS"
        for _, _, data in graph.edges(event_key, data=True)
    )

    anomaly_nodes = [
        key for key, data in graph.nodes(data=True)
        if data.get("node_type") == "Anomaly"
    ]
    assert len(anomaly_nodes) == 1
    assert graph.nodes[anomaly_nodes[0]]["score"] == 0.91
    assert cache_path.is_file()