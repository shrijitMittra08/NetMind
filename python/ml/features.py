from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd

BASE_METRICS = ("avg_latency_ms", "jitter_ms", "loss_pct", "throughput_mbps", "max_queue", "max_util_pct")
LATENCY_METRICS = ("avg_latency_ms", "jitter_ms")
SCALE_FLOORS = {"avg_latency_ms": 1.0, "jitter_ms": 0.1, "loss_pct": 1.0, "throughput_mbps": 1.0, "max_queue": 1.0,
                "max_util_pct": 1.0}
LEAKY_COLUMNS = ("active_failures", "is_anomaly", "event_pattern", "event_target")
REQUIRED_COLUMNS = ("run_id", "t_start", "delivered", *BASE_METRICS)
DERIVED_SUFFIXES = ("_delta1", "_delta3", "_vs_roll3", "_vs_ref")


def load_windows(path: str | Path) -> pd.DataFrame:
    frame = pd.read_csv(path, keep_default_na=False, na_values=[""])
    missing = sorted(set(REQUIRED_COLUMNS).difference(frame.columns))
    if missing:
        raise ValueError(f"{path} is missing window columns: {missing}")
    return frame


def _clean_run(run: pd.DataFrame) -> pd.DataFrame:
    run = run.sort_values("t_start").copy()
    no_delivery = run["delivered"] <= 0
    run["no_delivery"] = no_delivery.astype("int64")
    for column in LATENCY_METRICS:
        run[column] = run[column].astype("float64").mask(no_delivery).ffill().fillna(0.0)
    return run


def _expanding_reference(values: pd.Series) -> pd.Series:
    history = values.iloc[1:].expanding().median().shift(1)
    reference = pd.concat([pd.Series([np.nan], index=values.index[:1]), history])
    return reference.ffill().fillna(values.iloc[0])


def build_window_features(windows: pd.DataFrame) -> pd.DataFrame:
    missing = sorted(set(REQUIRED_COLUMNS).difference(windows.columns))
    if missing:
        raise ValueError(f"Window table is missing required columns: {missing}")
    if windows.empty:
        raise ValueError("Cannot build features from an empty window table")

    parts: list[pd.DataFrame] = []
    for _, run in windows.groupby("run_id", sort=True):
        run = _clean_run(run)
        result = run.copy()
        for metric in BASE_METRICS:
            values = run[metric].astype("float64")
            reference = _expanding_reference(values)
            scale = reference.abs() + SCALE_FLOORS[metric]
            previous_average = values.shift(1).rolling(3, min_periods=1).mean()
            result[f"{metric}_delta1"] = (values.diff(1) / scale).fillna(0.0)
            result[f"{metric}_delta3"] = ((values - values.shift(3)) / scale).fillna(0.0)
            result[f"{metric}_vs_roll3"] = ((values - previous_average) / scale).fillna(0.0)
            result[f"{metric}_vs_ref"] = (values - reference) / scale
        parts.append(result)
    return pd.concat(parts, ignore_index=True)


def feature_columns(frame: pd.DataFrame) -> list[str]:
    derived = [column for column in frame.columns if column.endswith(DERIVED_SUFFIXES)]
    columns = [*BASE_METRICS, "no_delivery", *derived]
    leaked = set(columns).intersection(LEAKY_COLUMNS)
    if leaked:
        raise AssertionError(f"Label columns leaked into features: {sorted(leaked)}")
    return columns


def signed_log1p(values: np.ndarray) -> np.ndarray:
    return np.sign(values) * np.log1p(np.abs(values))
