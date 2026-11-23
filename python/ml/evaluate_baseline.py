from __future__ import annotations

import argparse
import copy
import json
import sys
import tempfile
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import LeaveOneGroupOut

_PYTHON_ROOT = Path(__file__).resolve().parents[1]
if str(_PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(_PYTHON_ROOT))

from ml import scenario_model
from ml.baseline import (
    FLAG_FLOORS,
    PHYSICS_COLUMNS,
    TARGET_COLUMNS,
    ExpectedMetricBaseline,
    _target_values,
    build_run_features,
    event_pattern_of,
)

DESCRIPTION = """Measure how accurate the expected-baseline model is, and whether its flags mean anything.

    python -m ml.evaluate_baseline [--dataset data/dataset/runs_all.csv] [--cli PATH]

1. Held-out topology accuracy (leave-one-topology-out over the dataset) for:
     * train mean      - the floor any model must beat;
     * old model       - a random forest on the previous inputs (an assumed flow between
                         the first two hosts, the event *type* only);
     * physics         - ml.scenario_model on the run's real flows and events (the app's model);
     * physics+forest  - physics plus a random forest trained on its remaining error.
2. False-flag rate: share of dataset runs flagged although nothing unexpected happened.
3. Workbench-style runs the model never saw (the GraphRAG evaluation scenarios):
     * accuracy, and false flags on the runs as configured;
     * detection: the same runs with an extra on-path link failure that the model is
       *not* told about. A useful baseline should flag these.
"""

PROJECT_ROOT = _PYTHON_ROOT.parent
LEGACY_COLUMNS = ("node_count", "link_count", "average_degree", "mean_path_hops", "min_path_bottleneck_mbps",
                  "mean_path_latency_ms", "offered_rate_mbps")


def _local_topology_path(value: str) -> str:
    parts = Path(value.replace("\\", "/")).parts
    if "config" in parts:
        return str(PROJECT_ROOT.joinpath(*parts[parts.index("config"):]))
    return value


def load_runs(path: Path) -> pd.DataFrame:
    runs = pd.read_csv(path, keep_default_na=False)
    runs["topology_path"] = runs["topology_path"].map(_local_topology_path)
    runs["topology"] = runs["topology_path"].map(lambda p: Path(p).stem)
    return runs


def legacy_features(runs: pd.DataFrame) -> pd.DataFrame:
    features = build_run_features(runs.drop(columns=["scenario_json"], errors="ignore"))
    keep = [column for column in features.columns
            if column in LEGACY_COLUMNS or column.startswith(("algorithm_", "event_"))]
    return features[keep]


def scores(actual: pd.Series, predicted: pd.Series) -> dict[str, float]:
    error = actual - predicted
    denominator = ((actual - actual.mean()) ** 2).sum()
    relative = (error.abs() / actual.abs())[actual.abs() >= 0.5]
    return {
        "MAE": float(error.abs().mean()),
        "median_rel_%": float(relative.median() * 100) if len(relative) else float("nan"),
        "R2": float(1 - (error ** 2).sum() / denominator) if denominator > 0 else float("nan"),
    }


def held_out(runs: pd.DataFrame, features: pd.DataFrame, legacy: pd.DataFrame,
             targets: pd.DataFrame) -> tuple[pd.DataFrame, ExpectedMetricBaseline]:
    splits = list(LeaveOneGroupOut().split(features, groups=runs["topology"]))
    predictions = {name: pd.DataFrame(index=runs.index, columns=list(TARGET_COLUMNS), dtype=float)
                   for name in ("train mean", "old model")}
    for train, test in splits:
        for target in TARGET_COLUMNS:
            y = targets[target].iloc[train]
            column = predictions["train mean"].columns.get_loc(target)
            predictions["train mean"].iloc[test, column] = y.mean()
            model = RandomForestRegressor(n_estimators=150, random_state=42, n_jobs=-1).fit(legacy.iloc[train], y)
            predictions["old model"].iloc[test, column] = model.predict(legacy.iloc[test])

    predictions["physics"] = pd.DataFrame({t: features[PHYSICS_COLUMNS[t]] for t in TARGET_COLUMNS})
    corrected = ExpectedMetricBaseline(n_estimators=150, random_state=42, learn_correction=True).fit(runs, features)
    out_of_fold = corrected.validation_predictions_
    predictions["physics+forest"] = pd.DataFrame({t: out_of_fold[f"{t}_expected"] for t in TARGET_COLUMNS})
    model = ExpectedMetricBaseline().fit(runs, features)

    rows = []
    for target in TARGET_COLUMNS:
        for name, frame in predictions.items():
            rows.append({"target": target, "predictor": name, **scores(targets[target], frame[target].astype(float))})
    return pd.DataFrame(rows), model


def _with_hidden_failure(scenario: dict, topology: scenario_model.Topology, algorithm: str) -> dict | None:
    first = scenario["flows"][0]
    router = scenario_model.Router(topology, scenario_model.GraphState(), algorithm, scenario["weight"])
    path = router.path(topology.ids[first["src"]], topology.ids[first["dst"]])
    if not path:
        return None

    already_failed = set()
    for event in scenario["events"]:
        if "a" in event:
            already_failed.add(frozenset((topology.ids[event["a"]], topology.ids[event["b"]])))
    links = []
    for a, b in zip(path, path[1:]):
        link = topology.link_between(a, b)
        if frozenset((link.a, link.b)) not in already_failed:
            links.append(link)
    if not links:
        return None

    core = [link for link in links if topology.kinds[link.a] != "HOST" and topology.kinds[link.b] != "HOST"]
    candidates = core or links
    link = candidates[len(candidates) // 2]
    a, b = topology.names[link.a], topology.names[link.b]
    duration = float(scenario["duration_s"])
    variant = copy.deepcopy(scenario)
    variant["algorithms"] = [algorithm]
    variant["events"] = list(scenario["events"]) + [
        {"type": "LINK_FAILURE", "a": a, "b": b, "time_s": round(0.3 * duration, 3)},
        {"type": "LINK_RESTORE", "a": a, "b": b, "time_s": round(0.7 * duration, 3)},
    ]
    return variant


def _hidden_failure_spec(spec: dict[str, Any]) -> dict[str, Any]:
    hidden_spec: dict[str, Any] = {"scenarios": {}}
    for name, scenario in spec["scenarios"].items():
        topology = scenario_model.load_topology(PROJECT_ROOT / scenario["topology"])
        for algorithm in scenario["algorithms"]:
            variant = _with_hidden_failure(scenario, topology, algorithm)
            if variant:
                hidden_spec["scenarios"][f"{name}__hidden_{algorithm}"] = variant
    return hidden_spec


def _workbench_rows(generated: dict[str, Any], scenarios: dict[str, Any], spec: dict[str, Any], out: Path,
                    told: bool) -> pd.DataFrame:
    rows = []
    for name, by_algorithm in generated.items():
        original_name = name.split("__hidden")[0]
        described = scenarios[name] if told else spec["scenarios"][original_name]
        for algorithm, info in by_algorithm.items():
            network = pd.read_csv(out / f"{info['run_id']}_network.csv").iloc[0]
            rows.append({
                "scenario": original_name, "algorithm": algorithm, "topology_path": str(info["topology"]),
                "event_pattern": event_pattern_of(described["events"]),
                "scenario_json": json.dumps({
                    "duration_s": described["duration_s"], "flows": described["flows"],
                    "events": described["events"],
                    "routing": {"algorithm": algorithm, "weight": described["weight"]},
                }),
                "avg_latency_ms": network["avg_latency_ms"], "avg_throughput_mbps": network["avg_throughput_mbps"],
                "reliability_pct": network["reliability_pct"],
            })
    return pd.DataFrame(rows)


def _metric_changed(row: pd.Series, before: pd.Series) -> bool:
    changed = False
    for target, column in (("avg_latency_ms", "avg_latency_ms"), ("throughput_mbps", "avg_throughput_mbps")):
        delta = abs(row[column] - before[column])
        changed |= delta > max(FLAG_FLOORS[target], 0.10 * abs(before[column]))
    changed |= abs(row["reliability_pct"] - before["reliability_pct"]) > FLAG_FLOORS["loss_pct"]
    return bool(changed)


def workbench_check(model: ExpectedMetricBaseline, cli: Path | None) -> None:
    from graphrag.evaluation import SPEC_PATH, find_cli, generate_runs

    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    hidden_spec = _hidden_failure_spec(spec)
    executable = find_cli(str(cli) if cli else None)
    with tempfile.TemporaryDirectory(prefix="netmind_baseline_") as temporary:
        out = Path(temporary)
        honest = _workbench_rows(generate_runs(spec, executable, out), spec["scenarios"], spec, out, told=True)
        hidden = _workbench_rows(generate_runs(hidden_spec, executable, out), hidden_spec["scenarios"], spec, out,
                                 told=False)

    honest_result = model.predict(honest)
    hidden_result = model.predict(hidden)
    targets = _target_values(honest)
    print("\nWorkbench-style runs (never seen in training)")
    print(pd.DataFrame([
        {"target": t, **scores(targets[t], honest_result[f"{t}_expected"])} for t in TARGET_COLUMNS
    ]).round(3).to_string(index=False))
    detail = honest[["scenario", "algorithm"]].copy()
    for target in TARGET_COLUMNS:
        detail[f"{target} actual"] = honest_result[f"{target}_actual"].round(2)
        detail[f"{target} expected"] = honest_result[f"{target}_expected"].round(2)
    print(detail.to_string(index=False))

    flag_columns = [f"{t}_deviation_flag" for t in TARGET_COLUMNS]
    honest_flags = honest_result[flag_columns].any(axis=1)
    hidden_flags = hidden_result[flag_columns].any(axis=1)
    print(f"\nFalse alarms on workbench runs as configured: {int(honest_flags.sum())}/{len(honest_flags)}")
    keyed = honest.set_index(["scenario", "algorithm"])
    impact = [_metric_changed(row, keyed.loc[(row["scenario"], row["algorithm"])]) for _, row in hidden.iterrows()]
    impact = pd.Series(impact, index=hidden.index)
    print(f"Hidden on-path failures that changed a metric: {int(impact.sum())}/{len(impact)}; "
          f"detected: {int((hidden_flags & impact).sum())}/{int(impact.sum())}")

    summary = hidden[["scenario", "algorithm"]].copy()
    summary["changed"] = impact
    for target in TARGET_COLUMNS:
        summary[f"{target} actual/expected"] = (
            hidden_result[f"{target}_actual"].round(2).astype(str) + " / "
            + hidden_result[f"{target}_expected"].round(2).astype(str)
            + np.where(hidden_result[f"{target}_deviation_flag"], " !", "")
        )
    print(summary.to_string(index=False))


def main() -> int:
    parser = argparse.ArgumentParser(description=DESCRIPTION, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dataset", default=str(PROJECT_ROOT / "data" / "dataset" / "runs_all.csv"))
    parser.add_argument("--cli", help="netmind_cli executable for the workbench check")
    parser.add_argument("--skip-workbench", action="store_true")
    args = parser.parse_args()
    pd.set_option("display.width", 200)

    runs = load_runs(Path(args.dataset))
    features = build_run_features(runs)
    targets = _target_values(runs)
    print(f"{len(runs)} runs, {runs['topology'].nunique()} topologies: {dict(runs['topology'].value_counts())}\n")

    table, model = held_out(runs, features, legacy_features(runs), targets)
    print("Held-out topology accuracy (lower MAE / higher R2 is better)")
    print(table.round(3).to_string(index=False))

    out_of_fold = model.validation_predictions_
    flags = pd.DataFrame({t: out_of_fold[f"{t}_deviation_flag"] for t in TARGET_COLUMNS})
    print("\nShare of dataset runs flagged (held-out topology), by event pattern - these are false alarms,")
    print("since every dataset run behaved as configured:")
    flags["any"] = flags.any(axis=1)
    print((flags.groupby(runs["event_pattern"]).mean() * 100).round(1).to_string())
    print(f"overall: {flags['any'].mean() * 100:.1f}% of runs")

    if not args.skip_workbench:
        workbench_check(model, Path(args.cli) if args.cli else None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
