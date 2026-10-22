import json

import numpy as np
import pandas as pd
import pytest

from ml.baseline import (
    ExpectedMetricBaseline,
    build_run_features,
    evaluate_expected_baseline,
)


def write_topology(path, extra_link=False):
    nodes = [
        {"id": 1, "type": "HOST", "name": "PC1"},
        {"id": 2, "type": "ROUTER", "name": "R1"},
        {"id": 3, "type": "HOST", "name": "PC2"},
    ]
    links = [
        {"a": 1, "b": 2, "bandwidthMbps": 100, "latencyMs": 1},
        {"a": 2, "b": 3, "bandwidthMbps": 50, "latencyMs": 2},
    ]
    if extra_link:
        nodes.append({"id": 4, "type": "ROUTER", "name": "R2"})
        links.extend(
            [
                {"a": 1, "b": 4, "bandwidthMbps": 80, "latencyMs": 5},
                {"a": 4, "b": 3, "bandwidthMbps": 80, "latencyMs": 5},
            ]
        )

    path.write_text(
        json.dumps({"nodes": nodes, "links": links}),
        encoding="utf-8",
    )
    return str(path)


def make_runs(topology_paths):
    algorithms = ("bfs", "dfs", "dijkstra", "bellman-ford")
    events = ("none", "link_failure", "congestion", "burst")
    rows = []

    for index in range(16):
        topology_path = topology_paths[index % len(topology_paths)]
        algorithm = algorithms[index % len(algorithms)]
        event = events[(index // 2) % len(events)]
        offered_rate = 5.0 + index

        rows.append(
            {
                "run_id": f"run_{index:03d}",
                "topology_path": topology_path,
                "algorithm": algorithm,
                "event_pattern": event,
                "offered_rate_mbps": offered_rate,
                "avg_latency_ms": 3.0 + index * 0.4,
                "avg_throughput_mbps": 2.0 + index * 0.2,
                "reliability_pct": 100.0 - index * 0.5,
            }
        )

    return pd.DataFrame(rows)


def test_build_run_features_includes_topology_algorithm_and_event_inputs(tmp_path):
    topology_path = write_topology(tmp_path / "topology.json")
    runs = make_runs([topology_path]).iloc[:1].copy()

    features = build_run_features(runs)

    assert features.loc[0, "node_count"] == 3
    assert features.loc[0, "link_count"] == 2
    assert features.loc[0, "path_hops"] == 2
    assert features.loc[0, "bottleneck_bandwidth_mbps"] == 50
    assert features.loc[0, "path_latency_ms"] == 3
    assert features.loc[0, "algorithm_bfs"] == 1
    assert features.loc[0, "event_none"] == 1
    assert features.loc[0, "offered_to_bottleneck_ratio"] == pytest.approx(0.1)


def test_grouped_baseline_produces_expected_metrics_and_deviation_flags(tmp_path):
    first = write_topology(tmp_path / "topology_a.json")
    second = write_topology(tmp_path / "topology_b.json", extra_link=True)
    runs = make_runs([first, second])

    model, validation = evaluate_expected_baseline(
        runs,
        n_estimators=20,
        random_state=7,
    )

    assert len(validation) == len(runs)
    assert set(validation["run_id"]) == set(runs["run_id"])
    for target in (
        "avg_latency_ms",
        "throughput_mbps",
        "loss_pct",
    ):
        assert f"{target}_actual" in validation
        assert f"{target}_expected" in validation
        assert f"{target}_deviation_flag" in validation

    predictions = model.predict(runs)
    assert len(predictions) == len(runs)
    assert np.isfinite(predictions["avg_latency_ms_expected"]).all()
    assert np.isfinite(predictions["throughput_mbps_expected"]).all()
    assert np.isfinite(predictions["loss_pct_expected"]).all()


def test_model_can_be_saved_and_loaded(tmp_path):
    first = write_topology(tmp_path / "topology_a.json")
    second = write_topology(tmp_path / "topology_b.json", extra_link=True)
    runs = make_runs([first, second])

    model = ExpectedMetricBaseline(
        n_estimators=10,
        random_state=3,
    ).fit(runs)
    model_path = tmp_path / "models" / "expected_baseline.joblib"
    model.save(model_path)

    loaded = ExpectedMetricBaseline.load(model_path)
    assert len(loaded.predict(runs)) == len(runs)


def test_grouped_validation_requires_multiple_topologies(tmp_path):
    topology_path = write_topology(tmp_path / "topology.json")
    runs = make_runs([topology_path])

    with pytest.raises(ValueError, match="two different topologies"):
        ExpectedMetricBaseline(n_estimators=5).fit(runs)