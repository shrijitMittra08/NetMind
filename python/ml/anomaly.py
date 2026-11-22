"""Detect anomalous simulation windows with a z-score detector and compare it against IsolationForest."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

_PYTHON_ROOT = Path(__file__).resolve().parents[1]
if str(_PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(_PYTHON_ROOT))

import joblib
import numpy as np
import pandas as pd
from sklearn.ensemble import IsolationForest
from sklearn.metrics import precision_recall_fscore_support
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import FunctionTransformer, StandardScaler

from ml import features as feats

DEFAULT_WINDOWS = _PYTHON_ROOT.parent / "data" / "windows_dataset" / "windows_all.csv"
DEFAULT_MODELS = _PYTHON_ROOT.parent / "data" / "models"
DEFAULT_PLOTS = _PYTHON_ROOT.parent / "data" / "plots"
FIXED_Z = 3.0
PRIMARY = "zscore"
BUNDLE_NAME = "anomaly_detectors.joblib"


@dataclass(frozen=True)
class RunSplit:
    train: list[str]
    validation: list[str]
    test: list[str]


def split_runs(run_ids: list[str], seed: int = 42, validation: float = 0.2, test: float = 0.2) -> RunSplit:
    unique = sorted(set(run_ids))
    if len(unique) < 5:
        raise ValueError("Need at least five runs to split into train/validation/test")
    order = np.random.default_rng(seed).permutation(len(unique))
    shuffled = [unique[i] for i in order]
    n_test = max(1, round(len(unique) * test))
    n_val = max(1, round(len(unique) * validation))
    return RunSplit(
        train=sorted(shuffled[n_test + n_val:]),
        validation=sorted(shuffled[n_test:n_test + n_val]),
        test=sorted(shuffled[:n_test]),
    )


class ZScoreDetector:
    def __init__(self) -> None:
        self.mean_: np.ndarray | None = None
        self.std_: np.ndarray | None = None

    def fit(self, values: np.ndarray) -> "ZScoreDetector":
        transformed = feats.signed_log1p(np.asarray(values, dtype=float))
        self.mean_ = transformed.mean(axis=0)
        self.std_ = np.maximum(transformed.std(axis=0), 1e-6)
        return self

    def z_matrix(self, values: np.ndarray) -> np.ndarray:
        if self.mean_ is None or self.std_ is None:
            raise RuntimeError("ZScoreDetector must be fitted first")
        transformed = feats.signed_log1p(np.asarray(values, dtype=float))
        return np.abs((transformed - self.mean_) / self.std_)

    def score(self, values: np.ndarray) -> np.ndarray:
        return self.z_matrix(values).max(axis=1)

    def drivers(self, values: np.ndarray) -> np.ndarray:
        return self.z_matrix(values).argmax(axis=1)


class IsolationForestDetector:
    def __init__(self, seed: int = 42, n_estimators: int = 300) -> None:
        self.model = make_pipeline(
            FunctionTransformer(feats.signed_log1p),
            StandardScaler(),
            IsolationForest(n_estimators=n_estimators, contamination="auto", random_state=seed, n_jobs=-1),
        )

    def fit(self, values: np.ndarray) -> "IsolationForestDetector":
        self.model.fit(np.asarray(values, dtype=float))
        return self

    def score(self, values: np.ndarray) -> np.ndarray:
        return -self.model.score_samples(np.asarray(values, dtype=float))


def classification_metrics(labels: np.ndarray, flags: np.ndarray) -> dict[str, float]:
    precision, recall, f1, _ = precision_recall_fscore_support(
        labels, flags, average="binary", zero_division=0
    )
    return {
        "precision": float(precision),
        "recall": float(recall),
        "f1": float(f1),
        "flagged": int(np.sum(flags)),
        "positives": int(np.sum(labels)),
        "windows": int(len(labels)),
    }


def choose_threshold(scores: np.ndarray, labels: np.ndarray) -> float:
    candidates = np.unique(np.quantile(scores, np.linspace(0.50, 0.995, 200)))
    best_threshold, best_f1 = float(candidates[-1]), -1.0
    for threshold in candidates:
        f1 = classification_metrics(labels, scores > threshold)["f1"]
        if f1 > best_f1:
            best_threshold, best_f1 = float(threshold), f1
    return best_threshold


def run_level_metrics(frame: pd.DataFrame, flag_column: str) -> dict[str, float]:
    hits = frame.assign(_hit=frame[flag_column].astype(bool) & frame["is_anomaly"].astype(bool))
    per_run = hits.groupby("run_id").agg(
        disturbed=("is_anomaly", "max"),
        caught=("_hit", "max"),
        flagged=(flag_column, "max"),
    )
    disturbed = per_run[per_run["disturbed"] == 1]
    clean = per_run[per_run["disturbed"] == 0]
    clean_windows = frame[frame["run_id"].isin(clean.index)]
    return {
        "disturbed_runs": int(len(disturbed)),
        "detection_rate": float(disturbed["caught"].mean()) if len(disturbed) else float("nan"),
        "clean_runs": int(len(clean)),
        "clean_runs_with_false_alarm": float(clean["flagged"].mean()) if len(clean) else float("nan"),
        "clean_window_false_alarm_rate": float(clean_windows[flag_column].mean()) if len(clean_windows) else float("nan"),
    }


def recall_by_pattern(frame: pd.DataFrame, flag_column: str) -> dict[str, float]:
    positives = frame[frame["is_anomaly"] == 1]
    if positives.empty:
        return {}
    return {
        str(pattern): float(group[flag_column].mean())
        for pattern, group in positives.groupby("event_pattern", sort=True)
    }


def run_experiment(
    windows: pd.DataFrame,
    seed: int = 42,
    models_dir: Path | None = None,
) -> tuple[dict[str, Any], pd.DataFrame]:
    if "is_anomaly" not in windows.columns or "event_pattern" not in windows.columns:
        raise ValueError("Window table needs is_anomaly and event_pattern columns (see generate_dataset)")

    frame = feats.build_window_features(windows)
    columns = feats.feature_columns(frame)
    split = split_runs(frame["run_id"].tolist(), seed)
    frame["split"] = np.select(
        [frame["run_id"].isin(split.train), frame["run_id"].isin(split.validation)],
        ["train", "validation"],
        "test",
    )

    train_normal = frame[(frame["split"] == "train") & (frame["event_pattern"] == "none")]
    if len(train_normal) < 20:
        raise ValueError("Not enough windows from normal training runs to fit the detectors")
    values = frame[columns].to_numpy(dtype=float)
    normal_values = train_normal[columns].to_numpy(dtype=float)

    detectors = {
        "zscore": ZScoreDetector().fit(normal_values),
        "isolation_forest": IsolationForestDetector(seed).fit(normal_values),
    }

    validation = frame["split"] == "validation"
    test = frame["split"] == "test"
    labels = frame["is_anomaly"].to_numpy(dtype=int)
    results: dict[str, Any] = {
        "seed": seed,
        "primary": PRIMARY,
        "features": columns,
        "split_runs": {"train": len(split.train), "validation": len(split.validation), "test": len(split.test)},
        "train_normal_windows": int(len(train_normal)),
        "models": {},
    }

    for name, detector in detectors.items():
        scores = detector.score(values)
        frame[f"{name}_score"] = scores
        threshold = choose_threshold(scores[validation.to_numpy()], labels[validation.to_numpy()])
        frame[f"{name}_flag"] = scores > threshold
        test_frame = frame[test]
        results["models"][name] = {
            "threshold": threshold,
            "threshold_source": "max F1 on validation runs",
            "test": classification_metrics(labels[test.to_numpy()], frame.loc[test, f"{name}_flag"].to_numpy()),
            "test_runs": run_level_metrics(test_frame, f"{name}_flag"),
            "test_recall_by_pattern": recall_by_pattern(test_frame, f"{name}_flag"),
        }

    fixed = frame["zscore_score"] > FIXED_Z
    frame["zscore_fixed_flag"] = fixed
    results["models"]["zscore_fixed_3sigma"] = {
        "threshold": FIXED_Z,
        "threshold_source": "fixed |z| > 3 on any feature",
        "test": classification_metrics(labels[test.to_numpy()], fixed[test].to_numpy()),
        "test_runs": run_level_metrics(frame[test], "zscore_fixed_flag"),
        "test_recall_by_pattern": recall_by_pattern(frame[test], "zscore_fixed_flag"),
    }

    if models_dir is not None:
        models_dir.mkdir(parents=True, exist_ok=True)
        joblib.dump(
            {
                "primary": PRIMARY,
                "features": columns,
                "zscore": detectors["zscore"],
                "zscore_threshold": results["models"]["zscore"]["threshold"],
                "isolation_forest": detectors["isolation_forest"],
                "isolation_forest_threshold": results["models"]["isolation_forest"]["threshold"],
            },
            models_dir / BUNDLE_NAME,
        )

    return results, frame


def flag_windows(bundle: dict[str, Any], windows: pd.DataFrame) -> pd.DataFrame:
    frame = feats.build_window_features(windows)
    values = frame[bundle["features"]].to_numpy(dtype=float)
    for name in ("zscore", "isolation_forest"):
        scores = bundle[name].score(values)
        frame[f"{name}_score"] = scores
        frame[f"{name}_flag"] = scores > bundle[f"{name}_threshold"]
    names = np.asarray(bundle["features"])
    frame["zscore_driver"] = names[bundle["zscore"].drivers(values)]
    frame["anomaly_flag"] = frame[f"{bundle['primary']}_flag"]
    return frame


def _print_summary(results: dict[str, Any]) -> None:
    print(f"Runs: {results['split_runs']}  normal training windows: {results['train_normal_windows']}")
    print(f"{'model':<22}{'precision':>10}{'recall':>8}{'F1':>7}{'run detect':>12}{'clean FA':>10}")
    for name, model in results["models"].items():
        test, runs = model["test"], model["test_runs"]
        print(
            f"{name:<22}{test['precision']:>10.3f}{test['recall']:>8.3f}{test['f1']:>7.3f}"
            f"{runs['detection_rate']:>12.3f}{runs['clean_window_false_alarm_rate']:>10.3f}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", default=str(DEFAULT_WINDOWS), help="windows_all.csv from generate_dataset")
    parser.add_argument("--models", default=str(DEFAULT_MODELS))
    parser.add_argument("--plots", default=str(DEFAULT_PLOTS))
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--no-plots", action="store_true")
    args = parser.parse_args()

    windows = feats.load_windows(args.windows)
    results, frame = run_experiment(windows, args.seed, Path(args.models))
    plots_dir = Path(args.plots)
    plots_dir.mkdir(parents=True, exist_ok=True)
    (plots_dir / "anomaly_results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    frame.drop(columns=[c for c in frame.columns if c.endswith(("_delta1", "_delta3", "_vs_roll3", "_vs_ref"))]).to_csv(
        plots_dir / "anomaly_predictions.csv", index=False
    )
    if not args.no_plots:
        from ml import plots

        plots.plot_anomaly_timeline(frame, plots_dir / "anomaly_timeline.png", "zscore_flag", "z-score")
        plots.plot_anomaly_timeline(
            frame, plots_dir / "anomaly_timeline_isolation_forest.png", "isolation_forest_flag", "IsolationForest"
        )
        plots.plot_anomaly_comparison(results, plots_dir / "anomaly_f1_comparison.png")
        runs_path = Path(args.windows).with_name("runs_all.csv")
        if runs_path.is_file():
            plots.plot_algorithm_comparison(pd.read_csv(runs_path), plots_dir / "algorithm_comparison.png")
    _print_summary(results)


if __name__ == "__main__":
    main()
