"""Local HTTP API used by the NetMind React frontend."""

from __future__ import annotations

import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import tempfile
import threading
import uuid
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

import pandas as pd

from common.io import read_run
from graphrag.build_kg import build_run_graph
from graphrag.facts import extract_facts
from graphrag.query import answer_question
from ml.baseline import evaluate_expected_baseline

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = Path(os.environ.get("NETMIND_DATA_DIR", PROJECT_ROOT / "data")).resolve()
SCENARIOS_DIR = PROJECT_ROOT / "config" / "scenarios"
TOPOLOGIES_DIR = PROJECT_ROOT / "config" / "topologies"

RUN_ID_PATTERN = re.compile(r"^run_[A-Za-z0-9_-]+$")
NAME_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")

_GUI_ALGORITHMS = ("bfs", "dfs", "dijkstra", "bellman-ford")
_GUI_WEIGHTS = {"hops", "latency", "latency+congestion"}

_GRAPH_CACHE: dict[str, tuple[Any, list[dict[str, Any]]]] = {}
_BASELINE_CACHE: dict[str, dict[str, Any]] = {}
_RUN_LOCK = threading.Lock()                                                              

def _num(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _absolute_project_path(value: Any) -> str:
    text = str(value).strip()
    if not text:
        return text
    path = Path(text.replace("\\", "/")).expanduser()
    return str(path if path.is_absolute() else (PROJECT_ROOT / path).resolve())


def _run_ids() -> list[dict[str, str]]:
    runs: list[dict[str, str]] = []

    for manifest_path in sorted(DATA_DIR.glob("run_*.json")):
        run_id = manifest_path.stem
        if not RUN_ID_PATTERN.fullmatch(run_id):
            continue

        required_files = [
            DATA_DIR / f"{run_id}_trace.csv",
            DATA_DIR / f"{run_id}_flows.csv",
            DATA_DIR / f"{run_id}_links.csv",
            DATA_DIR / f"{run_id}_windows.csv",
            DATA_DIR / f"{run_id}_network.csv",
        ]
        if not all(path.is_file() for path in required_files):
            continue

        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue

        metadata = manifest.get("dataset_metadata", {})
        if not isinstance(metadata, dict):
            metadata = {}

        runs.append({
            "run_id": run_id,
            "algorithm": str(
                metadata.get("algorithm", manifest.get("algorithm", "unknown"))
            ),
            "topology": str(
                metadata.get("topology_path", manifest.get("topology", "unknown"))
            ),
        })

    return runs


def _topology_path_for(manifest: dict[str, Any]) -> Path:
    metadata = manifest.get("dataset_metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}

    candidates: list[Path] = []
    for value in (
        metadata.get("topology_path"),
        metadata.get("topology_file"),
        manifest.get("topology"),
    ):
        if not value:
            continue
        text = str(value)
        if text.lower() in {"loaded topology", "unknown"}:
            continue

        candidate = Path(text).expanduser()
        candidates.append(candidate)
        if not candidate.is_absolute():
            candidates.append(PROJECT_ROOT / candidate)
            candidates.append(DATA_DIR / candidate.name)

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    original_name = Path(
        str(metadata.get("topology_path", manifest.get("topology", "")))
    ).name
    if original_name:
        stem = Path(original_name).stem
        possible = DATA_DIR / f"topology_{stem}.json"
        if possible.is_file():
            return possible.resolve()

    raise FileNotFoundError(
        "Could not locate this run's topology. Check the manifest's "
        "'topology_path' or place the topology JSON in data/."
    )


def _algorithm_of(run_id: str) -> str:
    try:
        manifest = json.loads(
            (DATA_DIR / f"{run_id}.json").read_text(encoding="utf-8-sig")
        )
    except (OSError, json.JSONDecodeError):
        return run_id
    metadata = manifest.get("dataset_metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}
    return str(metadata.get("algorithm", manifest.get("algorithm", run_id)))


                                                                            
                                      
                                                                            

def _valid_topology(document: Any) -> bool:
    return (
        isinstance(document, dict)
        and isinstance(document.get("nodes"), list)
        and isinstance(document.get("links"), list)
    )


def _resolve_scenario_topology(scenario: dict[str, Any]) -> dict[str, Any]:
    reference = scenario.get("topology")
    if not isinstance(reference, str) or not reference.strip():
        raise ValueError("The scenario has no 'topology' path.")

    ref_path = Path(reference.replace("\\", "/"))
    if ref_path.is_absolute():
        candidates = [ref_path]
    else:
        candidates = [
            PROJECT_ROOT / ref_path,
            SCENARIOS_DIR / ref_path,
            TOPOLOGIES_DIR / ref_path.name,
        ]

    root = PROJECT_ROOT.resolve()
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if (
            resolved.is_file()
            and resolved.suffix.lower() == ".json"
            and resolved.is_relative_to(root)
        ):
            topology = json.loads(resolved.read_text(encoding="utf-8-sig"))
            if _valid_topology(topology):
                return {"topology": topology, "topology_name": resolved.name}

    raise FileNotFoundError(
        f"Topology '{reference}' was not found inside the project."
    )


def _list_scenarios() -> list[dict[str, str]]:
    found: list[dict[str, str]] = []
    if not SCENARIOS_DIR.is_dir():
        return found
    for path in sorted(SCENARIOS_DIR.glob("*.json")):
        if not NAME_PATTERN.fullmatch(path.stem):
            continue
        try:
            document = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        if isinstance(document, dict) and "topology" in document:
            found.append({"name": path.stem})
    return found


def _load_saved_scenario(name: str) -> dict[str, Any]:
    if not NAME_PATTERN.fullmatch(name):
        raise ValueError("Invalid scenario name")
    path = SCENARIOS_DIR / f"{name}.json"
    if not path.is_file():
        raise FileNotFoundError(f"Scenario '{name}' was not found")
    scenario = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(scenario, dict):
        raise ValueError("The scenario file is not a JSON object")
    return {"scenario": scenario, **_resolve_scenario_topology(scenario)}


def _validated_run_fields(body: dict[str, Any]) -> dict[str, Any]:
    topology = body.get("topology")
    if not _valid_topology(topology):
        raise ValueError("Topology JSON must contain nodes and links")

    duration = float(body.get("duration_s", 0))
    seed = int(body.get("seed", 0))
    algorithm = str(body.get("algorithm", "dijkstra"))
    weight = str(body.get("weight", ""))
    flows = body.get("flows", [])
    events = body.get("events", [])
    reconvergence_ms = float(body.get("reconvergence_delay_ms", 0) or 0)

    if duration <= 0:
        raise ValueError("Duration must be greater than zero")
    if algorithm not in _GUI_ALGORITHMS:
        raise ValueError("Unsupported routing algorithm")
    if weight not in _GUI_WEIGHTS:
        raise ValueError("Unsupported routing weight")
    if reconvergence_ms < 0:
        raise ValueError("Reconvergence delay cannot be negative")
    if not isinstance(flows, list) or not isinstance(events, list):
        raise ValueError("Flows and events must be arrays")
    if not flows:
        raise ValueError("At least one flow is required")

    return {
        "topology": topology,
        "duration": duration,
        "seed": seed,
        "algorithm": algorithm,
        "weight": weight,
        "reconvergence_ms": reconvergence_ms,
        "flows": flows,
        "events": events,
    }


def _store_topology(topology: dict[str, Any], suggested_name: str) -> Path:
    TOPOLOGIES_DIR.mkdir(parents=True, exist_ok=True)
    canonical = json.dumps(topology, sort_keys=True)

    for existing in sorted(TOPOLOGIES_DIR.glob("*.json")):
        try:
            other = json.loads(existing.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        if json.dumps(other, sort_keys=True) == canonical:
            return existing

    stem = re.sub(r"[^A-Za-z0-9_-]+", "_", Path(suggested_name or "topology").stem)
    stem = stem.strip("_") or "topology"
    target = TOPOLOGIES_DIR / f"{stem}.json"
    if target.exists():                                                          
        digest = hashlib.sha1(canonical.encode("utf-8")).hexdigest()[:8]
        target = TOPOLOGIES_DIR / f"{stem}_{digest}.json"
    target.write_text(json.dumps(topology, indent=2) + "\n", encoding="utf-8")
    return target


def _save_scenario(body: dict[str, Any]) -> dict[str, Any]:
    fields = _validated_run_fields(body)

    name = str(body.get("name", "")).strip()
    if not NAME_PATTERN.fullmatch(name):
        raise ValueError(
            "Scenario name must start with a letter or number and use only "
            "letters, numbers, '_' or '-' (max 64 characters)."
        )

    SCENARIOS_DIR.mkdir(parents=True, exist_ok=True)
    scenario_path = SCENARIOS_DIR / f"{name}.json"
    if scenario_path.exists() and not bool(body.get("overwrite", False)):
        raise FileExistsError(f"A scenario named '{name}' already exists.")

    topology_file = _store_topology(
        fields["topology"], str(body.get("topology_name", ""))
    )
    scenario = {
        "name": name,
        "topology": f"../topologies/{topology_file.name}",
        "seed": fields["seed"],
        "duration_s": fields["duration"],
        "routing": {
            "algorithm": fields["algorithm"],
            "weight": fields["weight"],
            "reconvergence_delay_ms": fields["reconvergence_ms"],
        },
        "flows": fields["flows"],
        "events": fields["events"],
    }
    scenario_path.write_text(json.dumps(scenario, indent=2) + "\n", encoding="utf-8")

    return {
        "name": name,
        "scenario_path": f"config/scenarios/{name}.json",
        "topology_path": f"config/topologies/{topology_file.name}",
    }


                                                                            
          
                                                                            

def _graph_for_run(run_id: str) -> tuple[Any, list[dict[str, Any]]]:
    if not RUN_ID_PATTERN.fullmatch(run_id):
        raise ValueError("Invalid run ID")

    cached = _GRAPH_CACHE.get(run_id)
    if cached is not None:
        return cached

    run = read_run(DATA_DIR, run_id)
    topology_path = _topology_path_for(run.manifest)

                                                                        
                                                                            
    with tempfile.TemporaryDirectory(prefix="netmind_graphrag_") as temporary:
        staging = Path(temporary)
        shutil.copy2(DATA_DIR / f"{run_id}.json", staging / f"{run_id}_manifest.json")
        for suffix in ("_trace.csv", "_flows.csv", "_links.csv", "_windows.csv"):
            shutil.copy2(DATA_DIR / f"{run_id}{suffix}", staging / f"{run_id}{suffix}")
        graph = build_run_graph(staging, run_id, topology_path=topology_path)

    facts = extract_facts(graph, run.trace.to_dict(orient="records"))
    _GRAPH_CACHE[run_id] = (graph, facts)
    return _GRAPH_CACHE[run_id]


def _answer_graphrag(run_ids: list[str], question: str) -> dict[str, Any]:
    ids = list(dict.fromkeys(run_ids))
    if not ids:
        raise ValueError("Select at least one run")

    pairs = [_graph_for_run(run_id) for run_id in ids]
    graphs = [graph for graph, _ in pairs]
    facts_by_run = {run_id: facts for run_id, (_, facts) in zip(ids, pairs)}

    result = answer_question(graphs, facts_by_run, question)

                                                                              
                                                        
    if (
        len(ids) > 1
        and result.get("intent") == "flow_path"
        and str(result.get("answer", "")).startswith("Please ask about a flow path")
    ):
        lines = []
        for run_id, (graph, facts) in zip(ids, pairs):
            single = answer_question(graph, {run_id: facts}, question)
            lines.append(f"{_algorithm_of(run_id)}: {single['answer']}")
        return {
            "intent": "flow_path",
            "answer": "\n".join(lines),
            "citations": [],
            "facts": [],
        }

    return result


                                                                            
             
                                                                            

_RELATIVE_FLOOR = {"avg_latency_ms": 0.1, "throughput_mbps": 0.01}

def _baseline_metric_rows(row: Any) -> list[dict[str, Any]]:
    metrics = []
    for name in ("avg_latency_ms", "throughput_mbps", "loss_pct"):
        actual = _num(row[f"{name}_actual"])
        expected = _num(row[f"{name}_expected"])
        delta = actual - expected if actual is not None and expected is not None else None

                                                                                  
                                                                                
        relative = None
        floor = _RELATIVE_FLOOR.get(name)
        if (
            floor is not None
            and delta is not None
            and expected is not None
            and abs(expected) >= floor
        ):
            relative = 100.0 * delta / abs(expected)

        metrics.append({
            "name": name,
            "actual": actual,
            "expected": expected,
            "delta": delta,
            "deviation_pct": relative,
            "flagged": bool(row[f"{name}_deviation_flag"]),
        })
    return metrics

def _analyze_expected_baseline_uncached(run_id: str) -> dict[str, Any]:
    if not RUN_ID_PATTERN.fullmatch(run_id):
        raise ValueError("Invalid run ID")

    runs_path = DATA_DIR / "dataset" / "runs_all.csv"
    if not runs_path.is_file():
        raise FileNotFoundError(
            f"Run dataset not found: {runs_path}. Generate the ML dataset first."
        )

    runs = pd.read_csv(runs_path, keep_default_na=False)
    if "topology_path" in runs.columns:
        runs["topology_path"] = runs["topology_path"].map(_absolute_project_path)
    if "run_id" not in runs.columns:
        raise ValueError("The run dataset has no run_id column")

    runs["run_id"] = runs["run_id"].astype(str)
    if run_id not in set(runs["run_id"]):
        raise FileNotFoundError(f"{run_id} is not present in {runs_path.name}")

    _, validation = evaluate_expected_baseline(runs, n_estimators=100, random_state=42)
    selected = validation.loc[validation["run_id"].astype(str) == run_id]
    if selected.empty:
        raise ValueError(f"No baseline validation result was produced for {run_id}")

    return {
        "run_id": run_id,
        "label": "expected/baseline",
        "metrics": _baseline_metric_rows(selected.iloc[0]),
    }


def _analyze_expected_baseline(run_id: str) -> dict[str, Any]:
    if run_id not in _BASELINE_CACHE:
        _BASELINE_CACHE[run_id] = _analyze_expected_baseline_uncached(run_id)
    return _BASELINE_CACHE[run_id]


def _baseline_for_new_run(run_row: dict[str, Any]) -> dict[str, Any]:
    """Compare a just-completed GUI run against topology-held-out estimates."""
    runs_path = DATA_DIR / "dataset" / "runs_all.csv"
    if not runs_path.is_file():
        return {
            "error": (
                "The baseline dataset is missing. Generate "
                "data/dataset/runs_all.csv first."
            )
        }

    try:
        runs = pd.read_csv(runs_path, keep_default_na=False)
        if "topology_path" in runs.columns:
            runs["topology_path"] = runs["topology_path"].map(_absolute_project_path)

                                                                        
        if "loss_pct" in runs.columns:
            loss = pd.to_numeric(runs["loss_pct"], errors="coerce")
        else:
            loss = pd.Series(float("nan"), index=runs.index)
        if "reliability_pct" in runs.columns:
            reliability = pd.to_numeric(runs["reliability_pct"], errors="coerce")
            loss = loss.fillna(100.0 - reliability)
        runs["loss_pct"] = loss

        runs = runs.loc[runs["run_id"].astype(str) != str(run_row["run_id"])]
        runs = pd.concat([runs, pd.DataFrame([run_row])], ignore_index=True)

        _, validation = evaluate_expected_baseline(runs, n_estimators=100, random_state=42)
        selected = validation.loc[
            validation["run_id"].astype(str) == str(run_row["run_id"])
        ]
        if selected.empty:
            return {"error": "No baseline estimate was produced for this run."}

        return {"metrics": _baseline_metric_rows(selected.iloc[0])}
    except Exception as error:
        return {"error": f"Baseline unavailable: {error}"}


                                                                            
                     
                                                                            

def _cli_path() -> Path:
    configured = os.environ.get("NETMIND_CLI")
    cli = (
        Path(configured).expanduser().resolve()
        if configured
        else PROJECT_ROOT / "build" / "debug" / "netmind_cli.exe"
    )
    if not cli.is_file():
        raise FileNotFoundError(
            f"NetMind CLI executable not found: {cli}. "
            "Set NETMIND_CLI to its location."
        )
    return cli


def _run_cli(scenario_path: Path) -> str:
    cli = _cli_path()
    with _RUN_LOCK:
        before = {path.stem for path in DATA_DIR.glob("run_*.json")}
        completed = subprocess.run(
            [str(cli), "--scenario", str(scenario_path), "--out", str(DATA_DIR), "--quiet"],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            timeout=600,
            check=False,
        )
        if completed.returncode != 0:
            raise RuntimeError(
                "The simulator could not complete the run.\n"
                f"{completed.stderr.strip() or completed.stdout.strip()}"
            )
        new_ids = sorted(
            path.stem for path in DATA_DIR.glob("run_*.json") if path.stem not in before
        )
    if len(new_ids) != 1:
        raise RuntimeError("Could not identify the newly exported run")
    return new_ids[0]


def _event_pattern(events: list[dict[str, Any]]) -> str:
    types = {str(event.get("type", "")) for event in events}
    if "TRAFFIC_BURST" in types:
        return "burst"
    if "CONGESTION" in types:
        return "congestion"
    if types & {"ROUTER_FAILURE", "ROUTER_RESTORE"}:
        return "router_failure"
    if types & {"LINK_FAILURE", "LINK_RESTORE"}:
        return "link_failure"
    return "none"


def _finish_run(
    run_id: str,
    fields: dict[str, Any],
    algorithm: str,
    topology_path: Path,
) -> dict[str, Any]:
    manifest_path = DATA_DIR / f"{run_id}.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    metadata = manifest.get("dataset_metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}

    pattern = _event_pattern(fields["events"])
    offered = sum(float(flow.get("rate_mbps", 0)) for flow in fields["flows"])
    metadata.update({
        "topology_path": str(topology_path.resolve()),
        "topology_file": topology_path.name,
        "algorithm": algorithm,
        "weight": fields["weight"],
        "seed": fields["seed"],
        "event_pattern": pattern,
        "offered_rate_mbps": offered,
    })
    manifest["dataset_metadata"] = metadata
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    network = pd.read_csv(DATA_DIR / f"{run_id}_network.csv").iloc[0].to_dict()
    run_row = {
        "run_id": run_id,
        "topology_path": str(topology_path.resolve()),
        "algorithm": algorithm,
        "weight": fields["weight"],
        "seed": fields["seed"],
        "event_pattern": pattern,
        "offered_rate_mbps": offered,
        **network,
    }
    reliability = _num(run_row.get("reliability_pct"))
    run_row["loss_pct"] = (100.0 - reliability) if reliability is not None else float("nan")

    avg_hops = None
    try:
        flows_df = pd.read_csv(DATA_DIR / f"{run_id}_flows.csv")
        weights = flows_df["delivered"].astype(float)
        if weights.sum() > 0:
            avg_hops = float(
                (flows_df["avg_hops"].astype(float) * weights).sum() / weights.sum()
            )
    except Exception:
        avg_hops = None

    return {
        "run_id": run_id,
        "algorithm": algorithm,
        "summary": {
            "simulation_time_s": fields["duration"],
            "reliability_pct": reliability,
            "throughput_mbps": _num(run_row.get("avg_throughput_mbps")),
            "latency_ms": _num(run_row.get("avg_latency_ms")),
            "loss_pct": _num(run_row["loss_pct"]),
            "drops": int(_num(run_row.get("total_drops")) or 0),
            "events": int(_num(run_row.get("events")) or 0),
            "recovery_time_s": _num(run_row.get("recovery_time_s")),
            "avg_hops": avg_hops,
        },
        "baseline": _baseline_for_new_run(run_row),
    }


def _run_gui_simulation(body: dict[str, Any]) -> dict[str, Any]:
    fields = _validated_run_fields(body)

    requested = body.get("algorithms") or list(_GUI_ALGORITHMS)
    if not isinstance(requested, list) or not requested:
        raise ValueError("algorithms must be a non-empty list")
    algorithms = list(dict.fromkeys(str(item) for item in requested))
    if any(item not in _GUI_ALGORITHMS for item in algorithms):
        raise ValueError("Unsupported routing algorithm")

    topologies_dir = DATA_DIR / "gui_topologies"
    scenarios_dir = DATA_DIR / "gui_scenarios"
    topologies_dir.mkdir(parents=True, exist_ok=True)
    scenarios_dir.mkdir(parents=True, exist_ok=True)

    token = uuid.uuid4().hex[:12]
    topology_path = topologies_dir / f"topology_{token}.json"
    topology_path.write_text(json.dumps(fields["topology"], indent=2), encoding="utf-8")

    runs = []
    for algorithm in algorithms:
        scenario = {
            "name": f"gui_run_{token}_{algorithm}",
            "topology": str(topology_path.resolve()),
            "seed": fields["seed"],
            "duration_s": fields["duration"],
            "routing": {
                "algorithm": algorithm,
                "weight": fields["weight"],
                "reconvergence_delay_ms": fields["reconvergence_ms"],
            },
            "flows": fields["flows"],
            "events": fields["events"],
        }
        scenario_path = scenarios_dir / f"scenario_{token}_{algorithm}.json"
        scenario_path.write_text(json.dumps(scenario, indent=2), encoding="utf-8")

        run_id = _run_cli(scenario_path)
        runs.append(_finish_run(run_id, fields, algorithm, topology_path))

    return {"runs": runs}
                                                                           
class GraphQueryRequest(BaseModel):
    question: str
    run_ids: list[str] | None = None
    run_id: str | None = None


class BaselineRequest(BaseModel):
    run_id: str


class ScenarioResolveRequest(BaseModel):
    scenario: dict[str, Any]


class SimulationRequest(BaseModel):
    topology: dict[str, Any]
    duration_s: float
    seed: int = 0
    algorithm: str = "dijkstra"
    weight: str
    reconvergence_delay_ms: float = 0
    flows: list[dict[str, Any]]
    events: list[dict[str, Any]] = Field(default_factory=list)
    algorithms: list[str] | None = None


class ScenarioSaveRequest(SimulationRequest):
    name: str
    topology_name: str = ""
    overwrite: bool = False


app = FastAPI(
    title="NetMind API",
    version="1.0.0",
    description="Local API for NetMind simulation, replay, baseline analysis, and GraphRAG.",
)


@app.exception_handler(FileExistsError)
async def file_exists_error_handler(
    request: Request,
    error: FileExistsError,
) -> JSONResponse:
    return JSONResponse(
        status_code=409,
        content={"error": str(error), "exists": True},
    )


@app.exception_handler(ValueError)
async def value_error_handler(
    request: Request,
    error: ValueError,
) -> JSONResponse:
    return JSONResponse(status_code=400, content={"error": str(error)})


@app.exception_handler(FileNotFoundError)
async def file_not_found_error_handler(
    request: Request,
    error: FileNotFoundError,
) -> JSONResponse:
    return JSONResponse(status_code=400, content={"error": str(error)})


@app.exception_handler(RequestValidationError)
async def validation_error_handler(
    request: Request,
    error: RequestValidationError,
) -> JSONResponse:
    return JSONResponse(
        status_code=422,
        content={"error": "Invalid request", "details": error.errors()},
    )


@app.exception_handler(Exception)
async def unexpected_error_handler(
    request: Request,
    error: Exception,
) -> JSONResponse:
    return JSONResponse(
        status_code=500,
        content={"error": f"{type(error).__name__}: {error}"},
    )


@app.get("/api/health")
def health() -> dict[str, Any]:
    return {"ok": True, "data_directory": str(DATA_DIR)}


@app.get("/api/runs")
def list_runs() -> dict[str, Any]:
    return {"runs": _run_ids()}


@app.get("/api/scenarios")
def list_scenarios() -> dict[str, Any]:
    return {"scenarios": _list_scenarios()}


@app.get("/api/scenarios/{name}")
def load_scenario(name: str) -> dict[str, Any]:
    return _load_saved_scenario(name)


@app.get("/api/gui/runs/{run_id}/files")
def get_run_files(
    run_id: str,
    light: bool = False,
) -> dict[str, Any]:
    if not RUN_ID_PATTERN.fullmatch(run_id):
        raise ValueError("Invalid run ID")

    if light:
        required = [
            DATA_DIR / f"{run_id}.json",
            DATA_DIR / f"{run_id}_flows.csv",
            DATA_DIR / f"{run_id}_network.csv",
        ]
    else:
        required = [
            DATA_DIR / f"{run_id}.json",
            DATA_DIR / f"{run_id}_trace.csv",
            DATA_DIR / f"{run_id}_flows.csv",
            DATA_DIR / f"{run_id}_links.csv",
            DATA_DIR / f"{run_id}_windows.csv",
            DATA_DIR / f"{run_id}_network.csv",
        ]

    if not all(file.is_file() for file in required):
        return JSONResponse(
            status_code=404,
            content={"error": "Run files are incomplete"},
        )

    manifest = json.loads(required[0].read_text(encoding="utf-8-sig"))
    topology_file = _topology_path_for(manifest)

    files = [{
        "name": f"topology_{run_id}.json",
        "type": "application/json",
        "content": topology_file.read_text(encoding="utf-8-sig"),
    }]

    for file in required:
        files.append({
            "name": file.name,
            "type": "application/json" if file.suffix == ".json" else "text/csv",
            "content": file.read_text(encoding="utf-8-sig"),
        })

    return {"files": files}


@app.post("/api/graphrag/query")
def query_graphrag(body: GraphQueryRequest) -> dict[str, Any]:
    question = body.question.strip()
    if not question:
        raise ValueError("Enter a question")

    run_ids = body.run_ids
    if run_ids is None and body.run_id:
        run_ids = [body.run_id]

    if not run_ids:
        raise ValueError("Select a run before asking a question")

    return _answer_graphrag(
        [run_id.strip() for run_id in run_ids],
        question,
    )


@app.post("/api/ml/baseline")
def analyze_baseline(body: BaselineRequest) -> dict[str, Any]:
    run_id = body.run_id.strip()
    if not run_id:
        raise ValueError("Select a run before comparing the baseline")

    return _analyze_expected_baseline(run_id)


@app.post("/api/gui/run")
def run_simulation(body: SimulationRequest) -> dict[str, Any]:
    return _run_gui_simulation(body.model_dump())


@app.post("/api/scenario/resolve")
def resolve_scenario(body: ScenarioResolveRequest) -> dict[str, Any]:
    return _resolve_scenario_topology(body.scenario)


@app.post("/api/scenario/save")
def save_scenario(body: ScenarioSaveRequest) -> dict[str, Any]:
    return _save_scenario(body.model_dump())


def main() -> None:
    import uvicorn

    host = "127.0.0.1"
    port = 8765
    print(f"NetMind API listening at http://{host}:{port}")
    print(f"Reading exported runs from: {DATA_DIR}")
    uvicorn.run(app, host=host, port=port)


if __name__ == "__main__":
    main()