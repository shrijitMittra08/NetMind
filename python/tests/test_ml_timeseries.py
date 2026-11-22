import numpy as np
import pandas as pd
import pytest

from ml import anomaly, features, plots
from ml.generate_dataset import disturbance_intervals, label_windows


def synthetic_windows(runs=40, length=30, seed=0):
    rng = np.random.default_rng(seed)
    patterns = ["none", "link_failure", "congestion", "burst"]
    rows = []
    for index in range(runs):
        pattern = patterns[index % len(patterns)]
        base_latency = rng.uniform(5, 20)
        base_throughput = rng.uniform(5, 50)
        base_util = rng.uniform(10, 80)
        base_queue = int(rng.integers(1, 20))
        start, end = 10, 18
        for t in range(length):
            disturbed = pattern != "none" and start <= t < end
            latency = base_latency * (2.5 if disturbed else 1.0) + rng.normal(0, 0.2)
            loss = (30.0 if disturbed else 0.0) + abs(rng.normal(0, 0.1))
            throughput = base_throughput * (0.6 if disturbed else 1.0) + rng.normal(0, 0.3)
            rows.append({
                "run_id": f"run_{index:03d}",
                "t_start": float(t),
                "sent": 100,
                "delivered": 70 if disturbed else 100,
                "dropped": 30 if disturbed else 0,
                "loss_pct": loss,
                "avg_latency_ms": latency,
                "jitter_ms": abs(rng.normal(0.1, 0.02)),
                "throughput_mbps": throughput,
                "max_queue": base_queue + (40 if disturbed else int(rng.integers(0, 3))),
                "max_util_pct": min(100.0, base_util + (30.0 if disturbed else rng.normal(0, 1))),
                "active_failures": int(disturbed),
                "algorithm": "dijkstra",
                "event_pattern": pattern,
                "is_anomaly": int(disturbed),
            })
    return pd.DataFrame(rows)


def test_link_failure_is_disturbed_until_matching_restore():
    events = [
        {"type": "LINK_FAILURE", "a": "R2", "b": "R1", "time_s": 5.5},
        {"type": "LINK_RESTORE", "a": "R3", "b": "R4", "time_s": 7.0},
        {"type": "LINK_RESTORE", "a": "R1", "b": "R2", "time_s": 9.0},
    ]
    assert disturbance_intervals(events, 30.0) == [(5.5, 9.0)]


def test_failure_without_restore_lasts_until_the_end_and_timed_events_use_duration():
    events = [
        {"type": "ROUTER_FAILURE", "node": "R3", "time_s": 12.0},
        {"type": "CONGESTION", "a": "R1", "b": "R2", "time_s": 3.0, "duration_s": 2.0},
        {"type": "TRAFFIC_BURST", "src": "PC1", "dst": "PC2", "time_s": 20.0, "duration_s": 50.0},
    ]
    assert disturbance_intervals(events, 30.0) == [(3.0, 5.0), (12.0, 30.0), (20.0, 30.0)]


def test_window_labels_mark_every_overlapping_bin():
    windows = pd.DataFrame({"t_start": [float(t) for t in range(10)]})
    events = [{"type": "CONGESTION", "a": "A", "b": "B", "time_s": 2.5, "duration_s": 2.0}]
    assert label_windows(windows, events, 10.0).tolist() == [0, 0, 1, 1, 1, 0, 0, 0, 0, 0]
    assert label_windows(windows, [], 10.0).sum() == 0


def test_latency_is_forward_filled_when_nothing_was_delivered():
    windows = synthetic_windows(runs=1, length=6)
    windows.loc[3, ["delivered", "avg_latency_ms"]] = [0, 0.0]
    frame = features.build_window_features(windows)
    assert frame.loc[3, "avg_latency_ms"] == pytest.approx(frame.loc[2, "avg_latency_ms"])
    assert frame.loc[3, "no_delivery"] == 1


def test_features_are_causal_and_exclude_label_columns():
    windows = synthetic_windows(runs=1, length=12)
    before = features.build_window_features(windows)
    changed = windows.copy()
    changed.loc[changed.index[-1], "avg_latency_ms"] = 9999.0
    after = features.build_window_features(changed)
    columns = features.feature_columns(before)
    pd.testing.assert_frame_equal(before[columns].iloc[:-1], after[columns].iloc[:-1])
    assert "active_failures" not in columns and "is_anomaly" not in columns
    assert (before.groupby("run_id").head(1)[[c for c in columns if c.endswith("_delta1")]] == 0).all().all()


def test_split_keeps_runs_disjoint_and_is_reproducible():
    ids = [f"run_{i:03d}" for i in range(50)]
    split = anomaly.split_runs(ids, seed=3)
    assert split == anomaly.split_runs(ids, seed=3)
    assert not set(split.train) & set(split.test)
    assert not set(split.validation) & set(split.test)
    assert len(split.train) + len(split.validation) + len(split.test) == 50


def test_primary_zscore_detector_flags_injected_windows(tmp_path):
    results, frame = anomaly.run_experiment(synthetic_windows(), seed=7, models_dir=tmp_path)
    assert results["primary"] == "zscore"
    primary = results["models"]["zscore"]
    assert primary["test"]["recall"] > 0.8
    assert primary["test_runs"]["detection_rate"] == 1.0
    assert set(results["models"]) == {"isolation_forest", "zscore", "zscore_fixed_3sigma"}
    assert results["models"]["isolation_forest"]["test_runs"]["detection_rate"] == 1.0


def test_saved_bundle_reproduces_flags_for_a_single_run(tmp_path):
    windows = synthetic_windows(seed=7)
    _, frame = anomaly.run_experiment(windows, seed=7, models_dir=tmp_path)
    bundle = anomaly.joblib.load(tmp_path / anomaly.BUNDLE_NAME)
    one_run = frame[frame["event_pattern"] == "link_failure"]["run_id"].iloc[0]
    flagged = anomaly.flag_windows(bundle, windows.query("run_id == @one_run"))
    expected = frame[frame["run_id"] == one_run].sort_values("t_start")
    assert flagged["zscore_flag"].tolist() == expected["zscore_flag"].tolist()
    assert flagged["isolation_forest_flag"].tolist() == expected["isolation_forest_flag"].tolist()
    assert flagged["anomaly_flag"].equals(flagged["zscore_flag"])
    assert flagged.loc[flagged["is_anomaly"] == 1, "anomaly_flag"].any()


def test_plots_are_written(tmp_path):
    windows = synthetic_windows()
    results, frame = anomaly.run_experiment(windows, seed=7)
    assert plots.plot_anomaly_timeline(frame, tmp_path / "timeline.png").stat().st_size > 0
    assert plots.plot_anomaly_comparison(results, tmp_path / "f1.png").stat().st_size > 0
