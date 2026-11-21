import pytest

from ml.scenario_model import GraphState, Router, estimate, parse_topology

TOPOLOGY = parse_topology({
    "nodes": [
        {"id": 1, "type": "HOST", "name": "PC1"},
        {"id": 2, "type": "ROUTER", "name": "R2"},
        {"id": 3, "type": "ROUTER", "name": "R3"},
        {"id": 4, "type": "HOST", "name": "PC4"},
        {"id": 5, "type": "ROUTER", "name": "R5"},
    ],
    "links": [
        {"a": 1, "b": 2, "bandwidthMbps": 100, "latencyMs": 1},
        {"a": 2, "b": 3, "bandwidthMbps": 10, "latencyMs": 20},
        {"a": 3, "b": 4, "bandwidthMbps": 100, "latencyMs": 1},
        {"a": 2, "b": 5, "bandwidthMbps": 100, "latencyMs": 2},
        {"a": 5, "b": 3, "bandwidthMbps": 100, "latencyMs": 2},
    ],
})


def names(path):
    return [TOPOLOGY.names[node] for node in path]


@pytest.mark.parametrize("algorithm, weight, expected", [
    ("bfs", "latency", ["PC1", "R2", "R3", "PC4"]),
    ("dijkstra", "latency", ["PC1", "R2", "R5", "R3", "PC4"]),
    ("bellman-ford", "latency", ["PC1", "R2", "R5", "R3", "PC4"]),
    ("dijkstra", "hops", ["PC1", "R2", "R3", "PC4"]),
])
def test_routes_follow_the_algorithm_and_weight(algorithm, weight, expected):
    assert names(Router(TOPOLOGY, GraphState(), algorithm, weight).path(1, 4)) == expected


def test_dfs_follows_the_depth_first_tree_from_the_lowest_id():
    assert names(Router(TOPOLOGY, GraphState(), "dfs", "latency").path(1, 4)) == ["PC1", "R2", "R3", "PC4"]


def test_failures_reroute_or_disconnect():
    detour_down = GraphState(down_links=frozenset({4}))
    assert names(Router(TOPOLOGY, detour_down, "dijkstra", "latency").path(1, 4)) == ["PC1", "R2", "R3", "PC4"]
    access_down = GraphState(down_links=frozenset({1}))
    assert Router(TOPOLOGY, access_down, "dijkstra", "latency").path(1, 4) is None


def flow(rate, **extra):
    return {"src": "PC1", "dst": "PC4", "rate_mbps": rate, "packet_bytes": 1000, **extra}


def test_overload_is_capped_at_the_bottleneck():
    result = estimate(TOPOLOGY, {"duration_s": 10, "routing": {"algorithm": "bfs"}, "flows": [flow(20)]})
    assert result.throughput_mbps == pytest.approx(10.0)
    assert result.loss_pct == pytest.approx(50.0)


def test_bursty_flows_lose_traffic_while_on_even_if_the_average_fits():
    bursty = flow(20, pattern="bursty", on_duration_s=1, off_duration_s=1)
    result = estimate(TOPOLOGY, {"duration_s": 10, "routing": {"algorithm": "bfs"}, "flows": [bursty]})
    assert result.loss_pct == pytest.approx(50.0)
    assert result.features["offered_rate_mbps"] == pytest.approx(10.0)


def test_failure_without_reconvergence_delay_reroutes_without_loss():
    scenario = {
        "duration_s": 10,
        "routing": {"algorithm": "dijkstra", "weight": "latency"},
        "flows": [flow(5)],
        "events": [{"type": "ROUTER_FAILURE", "node": "R5", "time_s": 5}],
    }
    result = estimate(TOPOLOGY, scenario)
    assert result.loss_pct == pytest.approx(0.0)
    assert result.features["rerouted_traffic_share"] == pytest.approx(0.5)


def test_reconvergence_delay_drops_traffic_on_the_stale_path():
    scenario = {
        "duration_s": 10,
        "routing": {"algorithm": "dijkstra", "weight": "latency", "reconvergence_delay_ms": 1000},
        "flows": [flow(5)],
        "events": [{"type": "ROUTER_FAILURE", "node": "R5", "time_s": 5}],
    }
    assert estimate(TOPOLOGY, scenario).loss_pct == pytest.approx(10.0)
