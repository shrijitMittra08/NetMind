"""Expected-metric baseline for NetMind simulation runs."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from typing import Any

import joblib
import networkx as nx
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import GroupKFold


ALGORITHMS = ("bfs", "dfs", "dijkstra", "bellman-ford")
EVENT_PATTERNS = (
    "none",
    "link_failure",
    "router_failure",
    "congestion",
    "burst",
)
TARGET_COLUMNS = (
    "avg_latency_ms",
    "throughput_mbps",
    "loss_pct",
)


def _topology_features(topology_path: str | Path, algorithm: str) -> dict[str, float]:
    import json

    path = Path(topology_path).expanduser()
    if not path.is_file():
        raise FileNotFoundError(f"Topology file not found: {path}")

    try:
        document = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Could not read topology {path}: {error}") from error

    nodes = document.get("nodes")
    links = document.get("links")
    if not isinstance(nodes, list) or not isinstance(links, list) or not links:
        raise ValueError(f"Topology must contain nodes and links: {path}")

    normalized_algorithm = str(algorithm).strip().lower()
    if normalized_algorithm not in ALGORITHMS:
        raise ValueError(f"Unsupported routing algorithm: {algorithm!r}")

    graph = nx.Graph()
    node_by_id: dict[int, dict[str, Any]] = {}

    for node in nodes:
        try:
            node_id = int(node["id"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"Invalid node in topology {path}: {node!r}") from error

        node_by_id[node_id] = node
        graph.add_node(node_id)

    for link in links:
        try:
            a = int(link["a"])
            b = int(link["b"])
            bandwidth = float(link["bandwidthMbps"])
            latency = float(link.get("latencyMs", 0.0))
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"Invalid link in topology {path}: {link!r}") from error

        if a not in node_by_id or b not in node_by_id:
            raise ValueError(f"Link references an unknown node in {path}: {link!r}")
        if not np.isfinite(bandwidth) or bandwidth <= 0:
            raise ValueError(f"Link bandwidth must be positive in {path}: {link!r}")
        if not np.isfinite(latency) or latency < 0:
            raise ValueError(f"Link latency must be non-negative in {path}: {link!r}")

        graph.add_edge(
            a,
            b,
            bandwidth_mbps=bandwidth,
            latency_ms=latency,
        )

    hosts = sorted(
        (
            node_id
            for node_id, node in node_by_id.items()
            if str(node.get("type", "")).upper() == "HOST"
        )
    )
    if len(hosts) < 2:
        raise ValueError(f"Topology needs at least two hosts: {path}")

    source, destination = hosts[0], hosts[1]

    try:
        if normalized_algorithm == "bfs":
            route = nx.shortest_path(graph, source, destination)
        elif normalized_algorithm == "dfs":
            route = _depth_first_path(graph, source, destination)
        elif normalized_algorithm == "dijkstra":
            route = nx.shortest_path(
                graph,
                source,
                destination,
                weight="latency_ms",
                method="dijkstra",
            )
        else:
            route = nx.bellman_ford_path(
                graph,
                source,
                destination,
                weight="latency_ms",
            )
    except (nx.NetworkXNoPath, nx.NodeNotFound) as error:
        raise ValueError(
            f"No route between the first two hosts in topology {path}"
        ) from error

    route_edges = list(zip(route, route[1:]))
    route_bandwidths = [
        float(graph.edges[a, b]["bandwidth_mbps"])
        for a, b in route_edges
    ]
    route_latencies = [
        float(graph.edges[a, b]["latency_ms"])
        for a, b in route_edges
    ]

    node_count = graph.number_of_nodes()
    link_count = graph.number_of_edges()

    return {
        "node_count": float(node_count),
        "link_count": float(link_count),
        "average_degree": (
            2.0 * link_count / node_count if node_count else 0.0
        ),
        "path_hops": float(len(route_edges)),
        "bottleneck_bandwidth_mbps": min(route_bandwidths),
        "path_latency_ms": sum(route_latencies),
    }


def _depth_first_path(
    graph: nx.Graph,
    source: int,
    destination: int,
) -> list[int]:
    visited: set[int] = set()
    parent: dict[int, int | None] = {source: None}
    stack = [source]

    while stack:
        current = stack.pop()
        if current == destination:
            break
        if current in visited:
            continue

        visited.add(current)
                                                                           
        for neighbor in sorted(graph.neighbors(current), reverse=True):
            if neighbor not in visited and neighbor not in parent:
                parent[neighbor] = current
                stack.append(neighbor)

    if destination not in parent:
        raise nx.NetworkXNoPath(f"No path from {source} to {destination}")

    route = [destination]
    while route[-1] != source:
        previous = parent[route[-1]]
        if previous is None:
            raise nx.NetworkXNoPath(f"No path from {source} to {destination}")
        route.append(previous)

    route.reverse()
    return route


def build_run_features(runs: pd.DataFrame) -> pd.DataFrame:
    required = {
        "topology_path",
        "algorithm",
        "event_pattern",
        "offered_rate_mbps",
    }
    missing = sorted(required.difference(runs.columns))
    if missing:
        raise ValueError(f"Run table is missing required columns: {missing}")
    if runs.empty:
        raise ValueError("Cannot build features from an empty run table")

    topology_cache: dict[tuple[str, str], dict[str, float]] = {}
    rows: list[dict[str, float]] = []

    for _, run in runs.iterrows():
        topology_path = str(run["topology_path"])
        algorithm = str(run["algorithm"]).strip().lower()
        cache_key = (topology_path, algorithm)

        if cache_key not in topology_cache:
            topology_cache[cache_key] = _topology_features(
                topology_path,
                algorithm,
            )

        offered_rate = pd.to_numeric(
            pd.Series([run["offered_rate_mbps"]]),
            errors="coerce",
        ).iloc[0]
        if pd.isna(offered_rate) or not np.isfinite(float(offered_rate)):
            raise ValueError("offered_rate_mbps must contain finite numbers")

        features = dict(topology_cache[cache_key])
        bottleneck = features["bottleneck_bandwidth_mbps"]
        features["offered_rate_mbps"] = float(offered_rate)
        features["offered_to_bottleneck_ratio"] = (
            float(offered_rate) / bottleneck
        )

        for name in ALGORITHMS:
            features[f"algorithm_{name}"] = float(algorithm == name)

        event_pattern = str(run["event_pattern"]).strip().lower()
        for name in EVENT_PATTERNS:
            features[f"event_{name}"] = float(event_pattern == name)

        rows.append(features)

    return pd.DataFrame(rows, index=runs.index)


def _target_values(runs: pd.DataFrame) -> pd.DataFrame:
    required = {"avg_latency_ms", "avg_throughput_mbps"}
    missing = sorted(required.difference(runs.columns))
    if missing:
        raise ValueError(f"Run table is missing target columns: {missing}")

    if "loss_pct" in runs.columns:
        loss = pd.to_numeric(runs["loss_pct"], errors="coerce")
    elif "reliability_pct" in runs.columns:
                                                                               
        reliability = pd.to_numeric(runs["reliability_pct"], errors="coerce")
        loss = 100.0 - reliability
    else:
        raise ValueError(
            "Run table needs either 'loss_pct' or 'reliability_pct'"
        )

    targets = pd.DataFrame(
        {
            "avg_latency_ms": pd.to_numeric(
                runs["avg_latency_ms"], errors="coerce"
            ),
            "throughput_mbps": pd.to_numeric(
                runs["avg_throughput_mbps"], errors="coerce"
            ),
            "loss_pct": loss,
        },
        index=runs.index,
    )

    values = targets.to_numpy(dtype=float)
    if not np.isfinite(values).all():
        raise ValueError("Run target metrics must contain finite numbers")

    return targets


class ExpectedMetricBaseline:

    def __init__(
        self,
        *,
        n_estimators: int = 200,
        random_state: int = 42,
        deviation_multiplier: float = 2.0,
    ) -> None:
        if n_estimators < 1:
            raise ValueError("n_estimators must be at least 1")
        if deviation_multiplier <= 0:
            raise ValueError("deviation_multiplier must be greater than zero")

        self.n_estimators = n_estimators
        self.random_state = random_state
        self.deviation_multiplier = deviation_multiplier
        self.models_: dict[str, RandomForestRegressor] = {}
        self.feature_columns_: tuple[str, ...] | None = None
        self.residual_std_: dict[str, float] = {}
        self.validation_predictions_: pd.DataFrame | None = None

    def fit(self, runs: pd.DataFrame) -> "ExpectedMetricBaseline":
        if "topology_path" not in runs.columns:
            raise ValueError("Run table must contain 'topology_path'")

        features = build_run_features(runs)
        targets = _target_values(runs)
        groups = runs["topology_path"].astype(str)

        unique_groups = groups.nunique()
        if unique_groups < 2:
            raise ValueError(
                "At least two different topologies are required for "
                "topology-grouped validation"
            )

        splitter = GroupKFold(n_splits=min(5, unique_groups))
        out_of_fold = pd.DataFrame(index=runs.index)

        for target in TARGET_COLUMNS:
            predictions = pd.Series(index=runs.index, dtype=float)

            for train_positions, test_positions in splitter.split(
                features,
                targets[target],
                groups=groups,
            ):
                model = RandomForestRegressor(
                    n_estimators=self.n_estimators,
                    random_state=self.random_state,
                    n_jobs=1,
                )
                model.fit(
                    features.iloc[train_positions],
                    targets[target].iloc[train_positions],
                )
                predictions.iloc[test_positions] = model.predict(
                    features.iloc[test_positions]
                )

            residuals = targets[target] - predictions
            residual_std = float(residuals.std(ddof=0))
            self.residual_std_[target] = (
                residual_std if residual_std > 0 else 1e-12
            )
            out_of_fold[f"{target}_actual"] = targets[target]
            out_of_fold[f"{target}_expected"] = predictions
            out_of_fold[f"{target}_deviation_pct"] = (
                100.0
                * (targets[target] - predictions)
                / predictions.abs().clip(lower=1e-12)
            )
            out_of_fold[f"{target}_deviation_flag"] = (
                (targets[target] - predictions).abs()
                > self.deviation_multiplier * self.residual_std_[target]
            )

        self.validation_predictions_ = out_of_fold
        self.feature_columns_ = tuple(features.columns)
        self.models_ = {}

        for target in TARGET_COLUMNS:
            model = RandomForestRegressor(
                n_estimators=self.n_estimators,
                random_state=self.random_state,
                n_jobs=1,
            )
            model.fit(features, targets[target])
            self.models_[target] = model

        return self

    def predict(self, runs: pd.DataFrame) -> pd.DataFrame:
        if not self.models_ or self.feature_columns_ is None:
            raise RuntimeError("ExpectedMetricBaseline must be fitted first")

        features = build_run_features(runs)
        if tuple(features.columns) != self.feature_columns_:
            raise ValueError("Run features do not match the fitted model")

        result = pd.DataFrame(index=runs.index)
        if "run_id" in runs.columns:
            result["run_id"] = runs["run_id"]

        actual = _target_values(runs)
        for target in TARGET_COLUMNS:
            expected = self.models_[target].predict(features)
            observed = actual[target].to_numpy(dtype=float)
            deviation = observed - expected

            result[f"{target}_actual"] = observed
            result[f"{target}_expected"] = expected
            result[f"{target}_deviation_pct"] = (
                100.0 * deviation / np.maximum(np.abs(expected), 1e-12)
            )
            result[f"{target}_deviation_flag"] = (
                np.abs(deviation)
                > self.deviation_multiplier * self.residual_std_[target]
            )

        return result

    def save(self, path: str | Path) -> None:
        if not self.models_:
            raise RuntimeError("ExpectedMetricBaseline must be fitted first")
        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        joblib.dump(self, destination)

    @classmethod
    def load(cls, path: str | Path) -> "ExpectedMetricBaseline":
        """Load a previously saved expected/baseline model."""
        model = joblib.load(Path(path))
        if not isinstance(model, cls):
            raise TypeError("The saved file does not contain an expected baseline")
        return model


def evaluate_expected_baseline(
    runs: pd.DataFrame,
    *,
    n_estimators: int = 200,
    random_state: int = 42,
    deviation_multiplier: float = 2.0,
) -> tuple[ExpectedMetricBaseline, pd.DataFrame]:
    model = ExpectedMetricBaseline(
        n_estimators=n_estimators,
        random_state=random_state,
        deviation_multiplier=deviation_multiplier,
    ).fit(runs)

    assert model.validation_predictions_ is not None
    result = model.validation_predictions_.copy()
    if "run_id" in runs.columns:
        result.insert(0, "run_id", runs["run_id"])
    if "topology_path" in runs.columns:
        result.insert(1, "topology_path", runs["topology_path"])
    return model, result