from __future__ import annotations

from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

DISTURBED_PATTERNS = ("link_failure", "router_failure", "congestion", "burst")
COLORS = {"actual": "#1f2937", "flag": "#dc2626", "band": "#fde68a", "naive": "#9ca3af"}


def _save(figure: plt.Figure, path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    plt.close(figure)
    return path


def _shade_labels(axis: plt.Axes, run: pd.DataFrame) -> None:
    for start in run.loc[run["is_anomaly"] == 1, "t_start"]:
        axis.axvspan(start, start + 1.0, color=COLORS["band"], alpha=0.6, lw=0)


def _example_runs(frame: pd.DataFrame) -> list[str]:
    test = frame[frame["split"] == "test"] if "split" in frame.columns else frame
    chosen: list[str] = []
    for pattern in DISTURBED_PATTERNS:
        runs = test[test["event_pattern"] == pattern]
        if runs.empty:
            continue
        chosen.append(str(sorted(runs["run_id"].unique())[0]))
    return chosen


def plot_run_anomalies(axis: plt.Axes, run: pd.DataFrame, flag_column: str, title: str) -> None:
    run = run.sort_values("t_start")
    _shade_labels(axis, run)
    axis.plot(run["t_start"], run["avg_latency_ms"], color=COLORS["actual"], lw=1.6, label="latency (ms)")
    flagged = run[run[flag_column].astype(bool)]
    axis.scatter(flagged["t_start"], flagged["avg_latency_ms"], color=COLORS["flag"], zorder=3, s=22,
                 label="flagged")
    loss_axis = axis.twinx()
    loss_axis.plot(run["t_start"], run["loss_pct"], color=COLORS["flag"], lw=1, ls="--", alpha=0.6)
    loss_axis.set_ylabel("loss %", color=COLORS["flag"])
    axis.set_title(title, fontsize=10)
    axis.set_xlabel("time (s)")
    axis.set_ylabel("latency (ms)")


def plot_anomaly_timeline(
    frame: pd.DataFrame, path: Path, flag_column: str = "zscore_flag", detector: str = "z-score"
) -> Path:
    runs = _example_runs(frame)
    if not runs:
        raise ValueError("No disturbed runs to plot")
    columns = 2
    rows = (len(runs) + 1) // columns
    figure, axes = plt.subplots(rows, columns, figsize=(12, 3.6 * rows), squeeze=False)
    for axis, run_id in zip(axes.flat, runs):
        run = frame[frame["run_id"] == run_id]
        pattern = run["event_pattern"].iloc[0]
        plot_run_anomalies(axis, run, flag_column, f"{run_id} - {pattern} ({run['algorithm'].iloc[0]})")
    for axis in list(axes.flat)[len(runs):]:
        axis.axis("off")
    figure.suptitle(f"{detector} flags (red) vs injected disturbance (yellow); dashed = loss %", fontsize=11)
    return _save(figure, path)


def plot_anomaly_comparison(results: dict[str, Any], path: Path) -> Path:
    names = list(results["models"])
    metrics = ("precision", "recall", "f1")
    figure, axis = plt.subplots(figsize=(8, 4))
    width = 0.8 / len(metrics)
    for offset, metric in enumerate(metrics):
        values = [results["models"][name]["test"][metric] for name in names]
        positions = [i + (offset - 1) * width for i in range(len(names))]
        bars = axis.bar(positions, values, width, label=metric)
        axis.bar_label(bars, fmt="%.2f", fontsize=8)
    axis.set_xticks(range(len(names)), names)
    axis.set_ylim(0, 1.08)
    axis.set_ylabel("score on held-out runs")
    axis.set_title("Window-level anomaly detection")
    axis.legend(loc="upper right")
    return _save(figure, path)


def plot_algorithm_comparison(runs: pd.DataFrame, path: Path) -> Path:
    metrics = (("avg_latency_ms", "avg latency (ms)"), ("reliability_pct", "reliability %"),
               ("recovery_time_s", "recovery time (s)"))
    figure, axes = plt.subplots(1, len(metrics), figsize=(14, 4))
    for axis, (column, title) in zip(axes, metrics):
        table = runs.pivot_table(index="event_pattern", columns="algorithm", values=column, aggfunc="mean")
        table.plot.bar(ax=axis, rot=20, width=0.8, legend=axis is axes[-1])
        axis.set_title(title)
        axis.set_xlabel("")
    axes[-1].legend(title="algorithm", fontsize=8)
    figure.suptitle("Mean metrics per algorithm and event pattern (generated runs)")
    return _save(figure, path)
