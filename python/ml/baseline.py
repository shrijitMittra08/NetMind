from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import GroupKFold

from ml import scenario_model

ALGORITHMS = scenario_model.ALGORITHMS
EVENT_PATTERNS = ("none", "link_failure", "router_failure", "congestion", "burst")
TARGET_COLUMNS = ("avg_latency_ms", "throughput_mbps", "loss_pct")
PHYSICS_COLUMNS = {
    "avg_latency_ms": "physics_latency_ms",
    "throughput_mbps": "physics_throughput_mbps",
    "loss_pct": "physics_loss_pct",
}
FLAG_FLOORS = {"avg_latency_ms": 0.5, "throughput_mbps": 0.5, "loss_pct": 1.0}
FLAG_RELATIVE = {"avg_latency_ms": 0.10, "throughput_mbps": 0.10, "loss_pct": 0.0}


def event_pattern_of(events: list[dict[str, Any]]) -> str:
    types = {str(event.get("type", "")) for event in events}
    if "TRAFFIC_BURST" in types:
        return "burst"
    if "CONGESTION" in types:
        return "congestion"
    if "ROUTER_FAILURE" in types or "ROUTER_RESTORE" in types:
        return "router_failure"
    if "LINK_FAILURE" in types or "LINK_RESTORE" in types:
        return "link_failure"
    return "none"


def _scenario_for(run: pd.Series, topology: scenario_model.Topology) -> dict[str, Any]:
    raw = run.get("scenario_json", "")
    if isinstance(raw, dict):
        return raw
    if isinstance(raw, str) and raw.strip():
        return json.loads(raw)
    hosts = topology.hosts
    if len(hosts) < 2:
        raise ValueError("Topology needs at least two hosts")
    return {
        "duration_s": 10.0,
        "routing": {"algorithm": str(run["algorithm"]), "weight": str(run.get("weight", "latency") or "latency")},
        "flows": [{"src": topology.names[hosts[0]], "dst": topology.names[hosts[1]], "pattern": "constant",
                   "rate_mbps": float(run["offered_rate_mbps"]), "packet_bytes": 1000}],
        "events": [],
    }


def build_run_features(runs: pd.DataFrame) -> pd.DataFrame:
    missing = sorted({"topology_path", "algorithm", "event_pattern"}.difference(runs.columns))
    if missing:
        raise ValueError(f"Run table is missing required columns: {missing}")
    if runs.empty:
        raise ValueError("Cannot build features from an empty run table")
    if "scenario_json" not in runs.columns and "offered_rate_mbps" not in runs.columns:
        raise ValueError("Run table needs 'scenario_json' or 'offered_rate_mbps'")

    rows: list[dict[str, float]] = []
    for _, run in runs.iterrows():
        topology = scenario_model.load_topology(str(run["topology_path"]))
        scenario = _scenario_for(run, topology)
        scenario.setdefault("routing", {}).setdefault("algorithm", str(run["algorithm"]))
        result = scenario_model.estimate(topology, scenario)
        features = {**result.features, "physics_latency_ms": result.latency_ms,
                    "physics_throughput_mbps": result.throughput_mbps, "physics_loss_pct": result.loss_pct}
        pattern = str(run["event_pattern"]).strip().lower()
        for name in EVENT_PATTERNS:
            features[f"event_{name}"] = float(pattern == name)
        rows.append(features)
    return pd.DataFrame(rows, index=runs.index)


def _target_values(runs: pd.DataFrame) -> pd.DataFrame:
    missing = sorted({"avg_latency_ms", "avg_throughput_mbps"}.difference(runs.columns))
    if missing:
        raise ValueError(f"Run table is missing target columns: {missing}")

    loss = None
    if "loss_pct" in runs.columns:
        loss = pd.to_numeric(runs["loss_pct"], errors="coerce")
    if "reliability_pct" in runs.columns:
        from_reliability = 100.0 - pd.to_numeric(runs["reliability_pct"], errors="coerce")
        loss = from_reliability if loss is None else loss.fillna(from_reliability)
    if loss is None:
        raise ValueError("Run table needs either 'loss_pct' or 'reliability_pct'")

    latency = pd.to_numeric(runs["avg_latency_ms"], errors="coerce")
    throughput = pd.to_numeric(runs["avg_throughput_mbps"], errors="coerce")
    targets = pd.DataFrame({"avg_latency_ms": latency, "throughput_mbps": throughput, "loss_pct": loss},
                           index=runs.index)
    if not np.isfinite(targets.to_numpy(dtype=float)).all():
        raise ValueError("Run target metrics must contain finite numbers")
    return targets


def _clip(target: str, values: np.ndarray) -> np.ndarray:
    return np.clip(values, 0.0, 100.0 if target == "loss_pct" else None)


class ExpectedMetricBaseline:
    def __init__(self, *, n_estimators: int = 200, random_state: int = 42, deviation_multiplier: float = 3.0,
                 learn_correction: bool = False) -> None:
        if n_estimators < 1:
            raise ValueError("n_estimators must be at least 1")
        if deviation_multiplier <= 0:
            raise ValueError("deviation_multiplier must be greater than zero")
        self.n_estimators = n_estimators
        self.random_state = random_state
        self.deviation_multiplier = deviation_multiplier
        self.learn_correction = learn_correction
        self.models_: dict[str, RandomForestRegressor] = {}
        self.feature_columns_: tuple[str, ...] | None = None
        self.residual_std_: dict[str, float] = {}
        self.validation_predictions_: pd.DataFrame | None = None

    def _forest(self) -> RandomForestRegressor:
        return RandomForestRegressor(n_estimators=self.n_estimators, random_state=self.random_state,
                                     min_samples_leaf=10, max_depth=8, n_jobs=-1)

    def _expected(self, model: RandomForestRegressor | None, features: pd.DataFrame, target: str) -> np.ndarray:
        physics = features[PHYSICS_COLUMNS[target]].to_numpy(dtype=float)
        if model is None:
            return _clip(target, physics)
        return _clip(target, physics + model.predict(features))

    def threshold(self, target: str, expected: np.ndarray) -> np.ndarray:
        return np.maximum.reduce([
            np.full_like(expected, self.deviation_multiplier * self.residual_std_[target]),
            FLAG_RELATIVE[target] * np.abs(expected),
            np.full_like(expected, FLAG_FLOORS[target]),
        ])

    def _rows(self, actual: pd.DataFrame, expected: dict[str, np.ndarray], index: pd.Index) -> pd.DataFrame:
        result = pd.DataFrame(index=index)
        for target in TARGET_COLUMNS:
            observed = actual[target].to_numpy(dtype=float)
            deviation = observed - expected[target]
            result[f"{target}_actual"] = observed
            result[f"{target}_expected"] = expected[target]
            result[f"{target}_deviation_pct"] = 100.0 * deviation / np.maximum(np.abs(expected[target]), 1e-12)
            result[f"{target}_deviation_flag"] = np.abs(deviation) > self.threshold(target, expected[target])
        return result

    def _held_out_expected(self, features: pd.DataFrame, targets: pd.DataFrame, groups: pd.Series,
                           target: str) -> np.ndarray:
        residual = targets[target] - features[PHYSICS_COLUMNS[target]]
        predicted = np.zeros(len(targets))
        splitter = GroupKFold(n_splits=min(5, groups.nunique()))
        for train, test in splitter.split(features, groups=groups):
            model = self._forest().fit(features.iloc[train], residual.iloc[train])
            predicted[test] = self._expected(model, features.iloc[test], target)
        return predicted

    def fit(self, runs: pd.DataFrame, features: pd.DataFrame | None = None) -> "ExpectedMetricBaseline":
        if "topology_path" not in runs.columns:
            raise ValueError("Run table must contain 'topology_path'")
        if features is None:
            features = build_run_features(runs)
        targets = _target_values(runs)
        groups = runs["topology_path"].astype(str).map(lambda p: Path(p).stem)
        if groups.nunique() < 2:
            raise ValueError("At least two different topologies are required for topology-grouped validation")

        out_of_fold: dict[str, np.ndarray] = {}
        for target in TARGET_COLUMNS:
            if self.learn_correction:
                predicted = self._held_out_expected(features, targets, groups, target)
            else:
                predicted = self._expected(None, features, target)
            out_of_fold[target] = predicted
            std = float(np.std(targets[target].to_numpy(dtype=float) - predicted))
            self.residual_std_[target] = std if std > 0 else 1e-12

        self.validation_predictions_ = self._rows(targets, out_of_fold, runs.index)
        self.feature_columns_ = tuple(features.columns)
        self.models_ = {}
        if self.learn_correction:
            for target in TARGET_COLUMNS:
                residual = targets[target] - features[PHYSICS_COLUMNS[target]]
                self.models_[target] = self._forest().fit(features, residual)
        return self

    def predict(self, runs: pd.DataFrame) -> pd.DataFrame:
        if self.feature_columns_ is None:
            raise RuntimeError("ExpectedMetricBaseline must be fitted first")
        features = build_run_features(runs)
        if tuple(features.columns) != self.feature_columns_:
            raise ValueError("Run features do not match the fitted model")
        expected = {target: self._expected(self.models_.get(target), features, target) for target in TARGET_COLUMNS}
        result = self._rows(_target_values(runs), expected, runs.index)
        if "run_id" in runs.columns:
            result.insert(0, "run_id", runs["run_id"])
        return result

    def save(self, path: str | Path) -> None:
        if self.feature_columns_ is None:
            raise RuntimeError("ExpectedMetricBaseline must be fitted first")
        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        joblib.dump(self, destination)

    @classmethod
    def load(cls, path: str | Path) -> "ExpectedMetricBaseline":
        model = joblib.load(Path(path))
        if not isinstance(model, cls):
            raise TypeError("The saved file does not contain an expected baseline")
        return model


def evaluate_expected_baseline(runs: pd.DataFrame, *, n_estimators: int = 200, random_state: int = 42,
                               deviation_multiplier: float = 3.0) -> tuple[ExpectedMetricBaseline, pd.DataFrame]:
    model = ExpectedMetricBaseline(n_estimators=n_estimators, random_state=random_state,
                                   deviation_multiplier=deviation_multiplier).fit(runs)
    result = model.validation_predictions_.copy()
    if "run_id" in runs.columns:
        result.insert(0, "run_id", runs["run_id"])
    if "topology_path" in runs.columns:
        result.insert(1, "topology_path", runs["topology_path"])
    return model, result
