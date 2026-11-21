import json

import pytest

from graphrag import tools

TOPOLOGY = {
    "nodes": [
        {"id": 1, "type": "HOST", "name": "PC1", "ip": "10.0.0.1"},
        {"id": 2, "type": "ROUTER", "name": "R1", "ip": "10.0.0.2"},
        {"id": 3, "type": "ROUTER", "name": "R2", "ip": "10.0.0.3"},
        {"id": 4, "type": "ROUTER", "name": "R3", "ip": "10.0.0.4"},
        {"id": 5, "type": "HOST", "name": "PC2", "ip": "10.0.0.5"},
    ],
    "links": [
        {"a": 1, "b": 2, "bandwidthMbps": 100, "latencyMs": 1, "lossRate": 0},
        {"a": 2, "b": 3, "bandwidthMbps": 100, "latencyMs": 1, "lossRate": 0},
        {"a": 3, "b": 5, "bandwidthMbps": 100, "latencyMs": 1, "lossRate": 0},
        {"a": 2, "b": 4, "bandwidthMbps": 50, "latencyMs": 5, "lossRate": 0},
        {"a": 4, "b": 3, "bandwidthMbps": 50, "latencyMs": 5, "lossRate": 0},
    ],
}

TRACE = [
    (0.0, "FLOW_START", "-", 1, "PC1", "dst=PC2"),
    (0.0, "TX_START", 1, 1, "PC1>R1", "eta=0.001"),
    (0.001, "TX_START", 1, 1, "R1>R2", "eta=0.002"),
    (0.002, "TX_START", 1, 1, "R2>PC2", "eta=0.003"),
    (0.003, "DELIVERED", 1, 1, "PC2", "latency_ms=3;hops=3"),
    (1.0, "LINK_DOWN", "-", "-", 2, "state=down"),
    (1.0, "ROUTE_UPDATE", "-", "-", "network", "routers=3;entries_changed=4"),
    (1.0, "DROPPED", 2, 1, "R1", "reason=link_down"),
    (1.5, "TX_START", 3, 1, "PC1>R1", "eta=1.501"),
    (1.501, "TX_START", 3, 1, "R1>R3", "eta=1.506"),
    (1.506, "TX_START", 3, 1, "R3>R2", "eta=1.511"),
    (1.511, "TX_START", 3, 1, "R2>PC2", "eta=1.512"),
    (1.512, "DELIVERED", 3, 1, "PC2", "latency_ms=12;hops=4"),
    (1.6, "DROPPED", 4, 1, "R1", "reason=queue_full"),
    (2.5, "ROUTER_DOWN", "-", "-", 4, "state=down"),
    (3.0, "LINK_UP", "-", "-", 2, "state=up"),
    (3.5, "TX_START", 5, 1, "PC1>R1", "eta=3.501"),
    (3.501, "TX_START", 5, 1, "R1>R2", "eta=3.502"),
    (3.502, "TX_START", 5, 1, "R2>PC2", "eta=3.503"),
    (3.503, "DELIVERED", 5, 1, "PC2", "latency_ms=3;hops=3"),
]


def write_csv(path, header, rows):
    lines = [",".join(header)] + [",".join(str(v) for v in row) for row in rows]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_run(directory, run_id, algorithm, latency):
    (directory / f"{run_id}.json").write_text(
        json.dumps({"algorithm": algorithm, "weight": "latency", "seed": 1}), encoding="utf-8"
    )
    write_csv(directory / f"{run_id}_trace.csv", ["time_s", "event_type", "packet_id", "flow_id", "location", "detail"], TRACE)
    write_csv(
        directory / f"{run_id}_flows.csv",
        ["flow_id", "src", "dst", "sent", "delivered", "dropped", "loss_pct", "throughput_mbps", "goodput_mbps",
         "avg_latency_ms", "min", "max", "p95", "jitter_ms", "avg_hops"],
        [[1, 1, 5, 5, 3, 2, 40.0, 1.0, 1.0, 6.0, 3.0, 12.0, 12.0, 4.0, 3.333333]],
    )
    write_csv(
        directory / f"{run_id}_links.csv",
        ["channel", "utilization_pct", "avg_queue", "max_queue", "tx_pkts", "drops_queue", "drops_loss", "drops_down", "downtime_s"],
        [
            ["PC1>R1", 12.0, 0.1, 1, 4, 0, 0, 0, 0.0],
            ["R1>R3", 30.0, 2.0, 8, 1, 1, 0, 0, 0.0],
            ["R1>R2", 8.0, 0.0, 1, 2, 0, 0, 1, 2.0],
        ],
    )
    write_csv(
        directory / f"{run_id}_windows.csv",
        ["t_start", "sent", "delivered", "dropped", "loss_pct", "avg_latency_ms", "jitter_ms", "throughput_mbps",
         "max_queue", "max_util_pct", "active_failures"],
        [[0.0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 0], [1.0, 3, 1, 2, 66.7, 12.0, 0.0, 0.5, 8, 30.0, 1]],
    )
    write_csv(
        directory / f"{run_id}_network.csv",
        ["reliability_pct", "avg_throughput_mbps", "avg_latency_ms", "total_drops", "recovery_time_s", "events"],
        [[60.0, 1.0, latency, 2, 0.5, 4]],
    )


@pytest.fixture
def runs(tmp_path):
    topology_path = tmp_path / "topology.json"
    topology_path.write_text(json.dumps(TOPOLOGY), encoding="utf-8")
    contexts = []
    for run_id, algorithm, latency in [("run_001", "bfs", 5.0), ("run_002", "dijkstra", 5.004), ("run_003", "dfs", 7.0)]:
        make_run(tmp_path, run_id, algorithm, latency)
        contexts.append(tools.load_run(tmp_path, run_id, topology_path))
    return contexts


def test_run_summary_uses_simulator_totals_and_host_names(runs):
    summary = tools.run_summary(runs[0])
    assert summary["latency_ms"] == 5.0
    assert summary["drops"] == 2
    assert summary["flows"][0]["src"] == "PC1" and summary["flows"][0]["dst"] == "PC2"


def test_compare_runs_reports_ties_within_tolerance(runs):
    result = tools.compare_runs(runs, "latency_ms")
    assert result["best"] == ["bfs", "dijkstra"]
    assert result["tie"] is True
    assert result["all_equal"] is False


def test_compare_runs_single_winner_and_higher_is_better(runs):
    assert tools.compare_runs(runs[1:], "latency_ms")["best"] == ["dijkstra"]
    assert tools.compare_runs(runs, "reliability_pct")["better"] == "higher"
    with pytest.raises(ValueError):
        tools.compare_runs(runs, "speed")


def test_link_load_names_busiest_channel_and_queue_overflows(runs):
    load = tools.link_load(runs[0])
    assert load["busiest"]["channel"] == "R1→R3"
    assert load["queue_overflows"][0]["link"] == "R1–R3"


def test_drops_counts_every_reason_and_respects_time_range(runs):
    everything = tools.drops(runs[0])
    assert everything["total"] == 2
    assert everything["by_reason"] == {"link_down": 1, "queue_full": 1}
    assert tools.drops(runs[0], 1.5, 2.0)["by_reason"] == {"queue_full": 1}


def test_network_events_name_links_and_routers(runs):
    events = tools.network_events(runs[0])["events"]
    named = [(e["event"], e["target"]) for e in events]
    assert ("LINK_DOWN", "R1–R2") in named
    assert ("ROUTER_DOWN", "R3") in named
    assert ("LINK_UP", "R1–R2") in named


def test_flow_path_tracks_detour_and_recovery(runs):
    path = tools.flow_path(runs[0], "PC1", "PC2", at_time=1.2)
    assert path["path_at_time"] == "PC1 → R1 → R3 → R2 → PC2"
    assert [p["path"] for p in path["path_periods"]] == [
        "PC1 → R1 → R2 → PC2",
        "PC1 → R1 → R3 → R2 → PC2",
        "PC1 → R1 → R2 → PC2",
    ]
    assert path["path_changes"] == 2
    assert "error" in tools.flow_path(runs[0], "PC2", "PC1")


def test_window_metrics_flag_idle_windows_instead_of_zero_latency(runs):
    first, second = tools.window_metrics(runs[0])["windows"]
    assert first["idle"] is True and first["avg_latency_ms"] is None
    assert second["avg_latency_ms"] == 12.0


def test_congestion_answer_names_the_link_whose_queue_overflowed(runs):
    import pandas as pd

    from graphrag.answer_rules import answer_with_rules
    from graphrag.session import QASession

    ctx = runs[0]
    ctx.links = pd.DataFrame([
        {"channel": "PC1>R1", "utilization_pct": 60.0, "avg_queue": 0.6, "max_queue": 1, "tx_pkts": 9,
         "drops_queue": 0, "drops_loss": 0, "drops_down": 0, "downtime_s": 0.0},
        {"channel": "R1>R3", "utilization_pct": 54.0, "avg_queue": 19.4, "max_queue": 64, "tx_pkts": 8,
         "drops_queue": 7438, "drops_loss": 0, "drops_down": 0, "downtime_s": 0.0},
    ])
    answer = answer_with_rules(QASession([ctx]), "Which link was most congested?")["answer"]
    assert answer.startswith("The most congested link was R1→R3")
    assert "7438 packets were dropped there" in answer and "PC1→R1 at 60" in answer


def test_drop_answer_covers_every_run_not_just_the_first(runs):
    from graphrag.answer_rules import answer_with_rules
    from graphrag.session import QASession

    runs[0].trace = runs[0].trace[runs[0].trace["event_type"] != "DROPPED"]
    answer = answer_with_rules(QASession(runs), "Why were packets dropped?")["answer"]
    lines = answer.splitlines()
    assert lines[0] == "BFS: No packets were dropped."
    assert lines[1].startswith("Dijkstra and DFS: 2 packets were dropped")

    same = answer_with_rules(QASession(runs[1:]), "Why were packets dropped?")["answer"]
    assert same.startswith("2 packets were dropped") and "\n" not in same
