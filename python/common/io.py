"""Schema-checked readers for exported NetMind run files."""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pandas as pd


CSV_SCHEMAS: dict[str, tuple[str, ...]] = {
    "trace": (
        "time_s",
        "event_type",
        "packet_id",
        "flow_id",
        "location",
        "detail",
    ),
    "flows": (
        "flow_id",
        "src",
        "dst",
        "sent",
        "delivered",
        "dropped",
        "loss_pct",
        "throughput_mbps",
        "goodput_mbps",
        "avg_latency_ms",
        "min",
        "max",
        "p95",
        "jitter_ms",
        "avg_hops",
    ),
    "links": (
        "channel",
        "utilization_pct",
        "avg_queue",
        "max_queue",
        "tx_pkts",
        "drops_queue",
        "drops_loss",
        "drops_down",
        "downtime_s",
    ),
    "windows": (
        "t_start",
        "sent",
        "delivered",
        "dropped",
        "loss_pct",
        "avg_latency_ms",
        "jitter_ms",
        "throughput_mbps",
        "max_queue",
        "max_util_pct",
        "active_failures",
    ),
    "network": (
        "reliability_pct",
        "avg_throughput_mbps",
        "avg_latency_ms",
        "total_drops",
        "recovery_time_s",
        "events",
    ),
}

INTEGER_COLUMNS: dict[str, tuple[str, ...]] = {
    "flows": ("flow_id", "src", "dst", "sent", "delivered", "dropped"),
    "links": (
        "max_queue",
        "tx_pkts",
        "drops_queue",
        "drops_loss",
        "drops_down",
    ),
    "windows": ("sent", "delivered", "dropped", "max_queue", "active_failures"),
    "network": ("total_drops", "events"),
}

FLOAT_COLUMNS: dict[str, tuple[str, ...]] = {
    "trace": ("time_s",),
    "flows": (
        "loss_pct",
        "throughput_mbps",
        "goodput_mbps",
        "avg_latency_ms",
        "min",
        "max",
        "p95",
        "jitter_ms",
        "avg_hops",
    ),
    "links": ("utilization_pct", "avg_queue", "downtime_s"),
    "windows": (
        "t_start",
        "loss_pct",
        "avg_latency_ms",
        "jitter_ms",
        "throughput_mbps",
        "max_util_pct",
    ),
    "network": (
        "reliability_pct",
        "avg_throughput_mbps",
        "avg_latency_ms",
        "recovery_time_s",
    ),
}


@dataclass(frozen=True)
class RunData:

    run_id: str
    manifest: dict[str, Any]
    trace: pd.DataFrame
    flows: pd.DataFrame
    links: pd.DataFrame
    windows: pd.DataFrame
    network: pd.DataFrame


def _read_csv(path: Path, kind: str) -> pd.DataFrame:
    if kind not in CSV_SCHEMAS:
        raise ValueError(f"Unknown Project901 CSV type: {kind}")

    if not path.is_file():
        raise FileNotFoundError(f"Run file not found: {path}")

    try:
        with path.open("r", encoding="utf-8-sig", newline="") as stream:
            reader = csv.reader(stream)
            header = next(reader, None)
    except OSError as error:
        raise OSError(f"Could not read {kind} CSV {path}: {error}") from error

    expected = list(CSV_SCHEMAS[kind])
    if header != expected:
        raise ValueError(
            f"Invalid {kind} CSV schema in {path}. "
            f"Expected columns {expected}; found {header}."
        )

                                                                           
                                                                                   
    read_options: dict[str, Any] = {"keep_default_na": False}
    if kind == "trace":
        read_options["dtype"] = {"packet_id": "string", "flow_id": "string"}

    try:
        frame = pd.read_csv(path, **read_options)
    except (OSError, pd.errors.ParserError, UnicodeError) as error:
        raise ValueError(f"Could not parse {kind} CSV {path}: {error}") from error

    if kind == "trace":
        for column in ("packet_id", "flow_id"):
            values = frame[column].replace("-", pd.NA)
            try:
                frame[column] = pd.to_numeric(
                    values,
                    errors="raise",
                ).astype("Int64")
            except (TypeError, ValueError) as error:
                raise ValueError(
                    f"Invalid integer value in {kind}.{column} in {path}"
                ) from error

    for column in INTEGER_COLUMNS.get(kind, ()):
        try:
            frame[column] = pd.to_numeric(
                frame[column],
                errors="raise",
            ).astype("int64")
        except (TypeError, ValueError) as error:
            raise ValueError(
                f"Invalid integer value in {kind}.{column} in {path}"
            ) from error

    for column in FLOAT_COLUMNS.get(kind, ()):
        try:
            frame[column] = pd.to_numeric(
                frame[column],
                errors="raise",
            ).astype("float64")
        except (TypeError, ValueError) as error:
            raise ValueError(
                f"Invalid numeric value in {kind}.{column} in {path}"
            ) from error

    return frame


def read_trace(path: str | Path) -> pd.DataFrame:
    return _read_csv(Path(path), "trace")


def read_flows(path: str | Path) -> pd.DataFrame:
    return _read_csv(Path(path), "flows")


def read_links(path: str | Path) -> pd.DataFrame:
    return _read_csv(Path(path), "links")


def read_windows(path: str | Path) -> pd.DataFrame:
    return _read_csv(Path(path), "windows")


def read_network(path: str | Path) -> pd.DataFrame:
    return _read_csv(Path(path), "network")


def read_manifest(path: str | Path) -> dict[str, Any]:
    manifest_path = Path(path)
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Run manifest not found: {manifest_path}")

    try:
        value = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(
            f"Could not read manifest {manifest_path}: {error}"
        ) from error

    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object in manifest: {manifest_path}")

    return value


def read_run(run_directory: str | Path, run_id: str) -> RunData:
    directory = Path(run_directory)
    return RunData(
        run_id=run_id,
        manifest=read_manifest(directory / f"{run_id}.json"),
        trace=read_trace(directory / f"{run_id}_trace.csv"),
        flows=read_flows(directory / f"{run_id}_flows.csv"),
        links=read_links(directory / f"{run_id}_links.csv"),
        windows=read_windows(directory / f"{run_id}_windows.csv"),
        network=read_network(directory / f"{run_id}_network.csv"),
    )


def read_runs(run_directory: str | Path) -> dict[str, RunData]:
    directory = Path(run_directory)
    if not directory.is_dir():
        raise FileNotFoundError(f"Run directory not found: {directory}")

    runs: dict[str, RunData] = {}
    for manifest_path in sorted(directory.glob("*.json")):
        run_id = manifest_path.stem
        required_files = (
            directory / f"{run_id}_trace.csv",
            directory / f"{run_id}_flows.csv",
            directory / f"{run_id}_links.csv",
            directory / f"{run_id}_windows.csv",
            directory / f"{run_id}_network.csv",
        )
        if all(path.is_file() for path in required_files):
            runs[run_id] = read_run(directory, run_id)

    return runs


def _manifest_fields(manifest: dict[str, Any]) -> dict[str, Any]:
    fields = {
        key: value
        for key, value in manifest.items()
        if value is None or isinstance(value, (str, int, float, bool))
    }

    metadata = manifest.get("dataset_metadata")
    if isinstance(metadata, dict):
        for key, value in metadata.items():
            if value is None or isinstance(value, (str, int, float, bool)):
                fields.setdefault(key, value)

    return fields


def combine_windows(runs: dict[str, RunData]) -> pd.DataFrame:
    frames: list[pd.DataFrame] = []

    for run_id, run in sorted(runs.items()):
        frame = run.windows.copy()
        frame.insert(0, "run_id", run_id)

        for key, value in _manifest_fields(run.manifest).items():
            if key not in frame.columns:
                frame[key] = value

        frames.append(frame)

    if not frames:
        return pd.DataFrame(columns=("run_id", *CSV_SCHEMAS["windows"]))

    return pd.concat(frames, ignore_index=True, sort=False)