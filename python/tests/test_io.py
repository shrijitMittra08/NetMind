import json

import pandas as pd
import pytest

from common.io import (
    combine_windows,
    read_run,
    read_runs,
    read_trace,
)


def write_csv(path, header, rows):
    lines = [",".join(header)]
    lines.extend(",".join(str(value) for value in row) for row in rows)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def make_run(directory, run_id="run_001", seed=7):
    (directory / f"{run_id}.json").write_text(
        json.dumps(
            {
                "seed": seed,
                "algorithm": "dijkstra",
                "weight": "latency",
                "simulation_time_s": 10.0,
            }
        ),
        encoding="utf-8",
    )

    write_csv(
        directory / f"{run_id}_trace.csv",
        ["time_s", "event_type", "packet_id", "flow_id", "location", "detail"],
        [
            [0.5, "SEND", 12, 1, "PC1", "-"],
            [1.0, "LINK_DOWN", "-", "-", "R1-R2", "-"],
        ],
    )
    write_csv(
        directory / f"{run_id}_flows.csv",
        [
            "flow_id", "src", "dst", "sent", "delivered", "dropped", "loss_pct",
            "throughput_mbps", "goodput_mbps", "avg_latency_ms",
            "min", "max", "p95", "jitter_ms", "avg_hops",
        ],
        [[1, 1, 3, 10, 9, 1, 10, 4.5, 4.0, 8, 2, 20, 18, 1.5, 2]],
    )
    write_csv(
        directory / f"{run_id}_links.csv",
        [
            "channel", "utilization_pct", "avg_queue", "max_queue", "tx_pkts",
            "drops_queue", "drops_loss", "drops_down", "downtime_s",
        ],
        [["R1>R2", 75, 3, 5, 10, 0, 0, 1, 0.5]],
    )
    write_csv(
        directory / f"{run_id}_windows.csv",
        [
            "t_start", "sent", "delivered", "dropped", "loss_pct",
            "avg_latency_ms", "jitter_ms", "throughput_mbps", "max_queue",
            "max_util_pct", "active_failures",
        ],
        [[0, 10, 9, 1, 10, 8.5, 1.2, 4.5, 5, 75, 1]],
    )
    write_csv(
        directory / f"{run_id}_network.csv",
        [
            "reliability_pct", "avg_throughput_mbps", "avg_latency_ms",
            "total_drops", "recovery_time_s", "events",
        ],
        [[90, 4.5, 8, 1, 0.5, 1]],
    )


def test_read_run_returns_typed_frames_and_manifest(tmp_path):
    make_run(tmp_path)

    run = read_run(tmp_path, "run_001")

    assert run.manifest["algorithm"] == "dijkstra"
    assert run.trace.loc[0, "time_s"] == pytest.approx(0.5)
    assert run.trace.loc[0, "packet_id"] == 12
    assert pd.isna(run.trace.loc[1, "packet_id"])
    assert run.trace["packet_id"].dtype == "Int64"
    assert run.flows["sent"].dtype == "int64"
    assert run.windows["avg_latency_ms"].dtype == "float64"
    assert run.network.loc[0, "total_drops"] == 1


def test_read_trace_rejects_changed_schema(tmp_path):
    path = tmp_path / "trace.csv"
    write_csv(
        path,
        ["time_s", "event_type", "packet_id", "flow_id", "location"],
        [],
    )

    with pytest.raises(ValueError, match="Invalid trace CSV schema"):
        read_trace(path)


def test_read_runs_and_combine_windows_add_run_metadata(tmp_path):
    make_run(tmp_path, "run_001", seed=7)
    make_run(tmp_path, "run_002", seed=8)

                                                                           
    (tmp_path / "topology.json").write_text('{"nodes": []}', encoding="utf-8")

    runs = read_runs(tmp_path)
    combined = combine_windows(runs)

    assert set(runs) == {"run_001", "run_002"}
    assert len(combined) == 2
    assert set(combined["run_id"]) == {"run_001", "run_002"}
    assert set(combined["algorithm"]) == {"dijkstra"}
    assert set(combined["seed"]) == {7, 8}
    assert combined["t_start"].tolist() == [0.0, 0.0]