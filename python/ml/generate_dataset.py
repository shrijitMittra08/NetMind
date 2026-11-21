"""Generate reproducible NetMind simulation runs for ML experiments."""

from __future__ import annotations

import argparse
import itertools
import json
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any

                                                                       
_PYTHON_ROOT = Path(__file__).resolve().parents[1]
if str(_PYTHON_ROOT) not in sys.path:
    sys.path.insert(0, str(_PYTHON_ROOT))

import pandas as pd

from common.io import RunData, read_run
from ml import scenario_model


ALGORITHMS = ("bfs", "dfs", "dijkstra", "bellman-ford")
LOAD_PERCENTAGES = (10, 30, 50, 80, 100, 120)
EVENT_PATTERNS = ("none", "link_failure", "router_failure", "congestion", "burst")
RUN_FILE_SUFFIXES = (
    ".json",
    "_trace.csv",
    "_flows.csv",
    "_links.csv",
    "_windows.csv",
    "_network.csv",
)


@dataclass(frozen=True)
class TopologyInfo:
    path: Path
    nodes: tuple[dict[str, Any], ...]
    links: tuple[dict[str, Any], ...]
    hosts: tuple[str, ...]
    routers: tuple[str, ...]
    id_to_name: dict[int, str]
    minimum_bandwidth_mbps: float


@dataclass(frozen=True)
class RunTask:
    run_id: str
    topology: TopologyInfo
    algorithm: str
    load_pct: int
    event_pattern: str
    seed: int
    duration_s: float


def load_topology(path: str | Path) -> TopologyInfo:
    topology_path = Path(path).expanduser().resolve()
    if not topology_path.is_file():
        raise FileNotFoundError(f"Topology file not found: {topology_path}")

    try:
        document = json.loads(topology_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Could not read topology {topology_path}: {error}") from error

    if not isinstance(document, dict):
        raise ValueError(f"Expected a topology JSON object: {topology_path}")

    nodes = document.get("nodes")
    links = document.get("links")
    if not isinstance(nodes, list) or not isinstance(links, list) or not links:
        raise ValueError(
            f"Topology must contain nodes and at least one link: {topology_path}"
        )

    id_to_name: dict[int, str] = {}
    hosts: list[str] = []
    routers: list[str] = []

    for node in nodes:
        if not isinstance(node, dict):
            raise ValueError(f"Invalid node entry in {topology_path}: {node!r}")
        try:
            node_id = int(node["id"])
            name = str(node["name"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"Invalid node id/name in {topology_path}") from error

        id_to_name[node_id] = name
        node_type = str(node.get("type", "")).upper()
        if node_type == "HOST":
            hosts.append(name)
        elif node_type == "ROUTER":
            routers.append(name)

    bandwidths: list[float] = []
    for link in links:
        try:
            bandwidth = float(link["bandwidthMbps"])
            int(link["a"])
            int(link["b"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"Invalid link in {topology_path}: {link!r}") from error

        if bandwidth <= 0:
            raise ValueError(f"Link bandwidth must be positive in {topology_path}")
        bandwidths.append(bandwidth)

    if len(hosts) < 2:
        raise ValueError(f"Topology needs at least two hosts: {topology_path}")

    return TopologyInfo(
        path=topology_path,
        nodes=tuple(nodes),
        links=tuple(links),
        hosts=tuple(hosts),
        routers=tuple(routers),
        id_to_name=id_to_name,
        minimum_bandwidth_mbps=min(bandwidths),
    )


def _node_name(topology: TopologyInfo, node_id: int) -> str:
    try:
        return topology.id_to_name[int(node_id)]
    except KeyError as error:
        raise ValueError(f"Node {node_id} is missing from topology {topology.path}") from error


def _path_links(model: scenario_model.Topology, path: list[int] | None) -> list[scenario_model.Link]:
    if not path:
        return []
    return [model.link_between(a, b) for a, b in zip(path, path[1:])]


def _pick(rng: random.Random, preferred: list[Any], fallback: list[Any], preferred_share: float) -> Any:
    pool = preferred if preferred and rng.random() < preferred_share else fallback
    return rng.choice(pool)


def build_scenario(task: RunTask, packet_bytes: int | None = None) -> dict[str, Any]:
    rng = random.Random(f"{task.run_id}|{task.topology.path.name}|{task.algorithm}|{task.seed}")
    model = scenario_model.parse_topology({"nodes": list(task.topology.nodes), "links": list(task.topology.links)})
    weight = "hops" if rng.random() < 0.25 else "latency"
    router = scenario_model.Router(model, scenario_model.GraphState(), task.algorithm, weight)
    duration = task.duration_s

    hosts = model.hosts
    pairs = [(s, d) for s in hosts for d in hosts if s != d and router.path(s, d)]
    if not pairs:
        raise ValueError(f"No routable host pair in {task.topology.path}")
    rng.shuffle(pairs)
    flow_count = min(len(pairs), rng.choice((1, 1, 2, 2, 3, 4)))
    chosen = pairs[:flow_count]

    flows, paths = [], []
    for index, (src, dst) in enumerate(chosen, start=1):
        path = router.path(src, dst)
        paths.append(path)
        bottleneck = min(link.bandwidth_mbps for link in _path_links(model, path))
        share = task.load_pct / 100.0 if index == 1 else rng.uniform(0.05, 0.6)
        flow: dict[str, Any] = {
            "id": index,
            "src": model.names[src],
            "dst": model.names[dst],
            "pattern": "constant",
            "rate_mbps": round(bottleneck * share, 4),
            "packet_bytes": packet_bytes or rng.choice((500, 1000, 1500)),
            "start_s": 0.0 if index == 1 else round(rng.uniform(0, 0.3 * duration), 3),
            "stop_s": duration if index == 1 else round(rng.uniform(0.7 * duration, duration), 3),
        }
        kind = rng.random()
        if kind < 0.15:
            flow["pattern"] = "poisson"
        elif kind < 0.30:
            flow["pattern"] = "bursty"
            flow["on_duration_s"] = round(rng.uniform(0.5, 1.5), 3)
            flow["off_duration_s"] = round(rng.uniform(0.2, 1.0), 3)
        flows.append(flow)

    used_links = list({link.index: link for path in paths for link in _path_links(model, path)}.values())
    core_links = [l for l in model.links if model.kinds[l.a] != "HOST" and model.kinds[l.b] != "HOST"]
    used_core = [l for l in used_links if l in core_links]
    routers = [n for n, kind in model.kinds.items() if kind == "ROUTER"]
    used_routers = sorted({n for path in paths for n in path if model.kinds[n] == "ROUTER"})

    start = round(rng.uniform(0.2, 0.5) * duration, 3)
    end = round(min(duration * 0.95, start + rng.uniform(0.15, 0.4) * duration), 3)
    events: list[dict[str, Any]] = []
    target: str | None = None
    pattern = task.event_pattern
    if pattern == "link_failure":
        link = _pick(rng, used_core or used_links, core_links or list(model.links), 0.6)
        a, b = model.names[link.a], model.names[link.b]
        target = f"{a}-{b}"
        events.append({"type": "LINK_FAILURE", "a": a, "b": b, "time_s": start})
        if rng.random() < 0.9:
            events.append({"type": "LINK_RESTORE", "a": a, "b": b, "time_s": end})
    elif pattern == "router_failure":
        if not routers:
            raise ValueError("Router failure requested for a topology without routers")
        node = model.names[_pick(rng, used_routers, routers, 0.6)]
        target = node
        events.append({"type": "ROUTER_FAILURE", "node": node, "time_s": start})
        if rng.random() < 0.9:
            events.append({"type": "ROUTER_RESTORE", "node": node, "time_s": end})
    elif pattern == "congestion":
        link = _pick(rng, used_links, list(model.links), 0.7)
        a, b = model.names[link.a], model.names[link.b]
        target = f"{a}-{b}"
        events.append({
            "type": "CONGESTION", "a": a, "b": b, "time_s": start,
            "bandwidth_multiplier": round(rng.uniform(0.2, 0.8), 3),
            "extra_loss_rate": round(rng.uniform(0.0, 0.05), 4),
            "duration_s": round(end - start, 3),
        })
    elif pattern == "burst":
        src, dst = chosen[0] if rng.random() < 0.5 else rng.choice(pairs)
        bottleneck = min(link.bandwidth_mbps for link in _path_links(model, router.path(src, dst)))
        target = f"{model.names[src]}->{model.names[dst]}"
        events.append({
            "type": "TRAFFIC_BURST", "flow_id": len(flows) + 1,
            "src": model.names[src], "dst": model.names[dst], "time_s": start,
            "duration_s": round(end - start, 3), "rate_mbps": round(bottleneck * rng.uniform(0.5, 2.0), 4),
            "packet_bytes": packet_bytes or 1000,
        })
    elif pattern != "none":
        raise ValueError(f"Unsupported event pattern: {pattern}")

    reconvergence = 0 if rng.random() < 0.7 else rng.choice((50, 200, 500))
    routing = {"algorithm": task.algorithm, "weight": weight, "reconvergence_delay_ms": reconvergence}
    offered = sum(flow["rate_mbps"] for flow in flows)
    return {
        "name": task.run_id,
        "topology": str(task.topology.path),
        "seed": task.seed,
        "duration_s": duration,
        "routing": routing,
        "flows": flows,
        "events": events,
        "dataset_metadata": {
            "run_id": task.run_id,
            "topology_path": str(task.topology.path),
            "algorithm": task.algorithm,
            "weight": weight,
            "seed": task.seed,
            "load_pct_of_path_bottleneck": task.load_pct,
            "offered_rate_mbps": offered,
            "flow_count": len(flows),
            "event_pattern": pattern,
            "event_target": target,
            "injected_events": events,
            "disturbance_start_s": start if events else None,
            "disturbance_end_s": end if events else None,
            "scenario_json": json.dumps({"duration_s": duration, "routing": routing, "flows": flows, "events": events}),
        },
    }


def build_tasks(
    topologies: list[TopologyInfo],
    seeds: list[int],
    duration_s: float,
    max_runs: int,
    random_state: int,
    run_id_start: int = 1,
) -> list[RunTask]:
    if not topologies:
        raise ValueError("Provide at least one topology")
    if not seeds:
        raise ValueError("Provide at least one seed")
    if duration_s <= 0:
        raise ValueError("Duration must be greater than zero")
    if max_runs <= 0:
        raise ValueError("max_runs must be greater than zero")
    if run_id_start <= 0:
        raise ValueError("run_id_start must be positive")

    combinations = [
        (topology, algorithm, load_pct, event_pattern, seed)
        for topology, algorithm, load_pct, event_pattern, seed in itertools.product(
            topologies,
            ALGORITHMS,
            LOAD_PERCENTAGES,
            EVENT_PATTERNS,
            seeds,
        )
        if event_pattern != "router_failure" or topology.routers
    ]
    random.Random(random_state).shuffle(combinations)
    selected = combinations[:max_runs]

    return [
        RunTask(
            run_id=f"run_{run_id_start + index:03d}",
            topology=topology,
            algorithm=algorithm,
            load_pct=load_pct,
            event_pattern=event_pattern,
            seed=seed,
            duration_s=duration_s,
        )
        for index, (topology, algorithm, load_pct, event_pattern, seed)
        in enumerate(selected)
    ]


def next_run_number(*directories: Path) -> int:
    pattern = re.compile(r"^run_(\d+)(?:_|\.|$)")
    highest = 0

    for directory in directories:
        if not directory.is_dir():
            continue
        for path in directory.iterdir():
            match = pattern.match(path.name)
            if match:
                highest = max(highest, int(match.group(1)))

    return highest + 1


def run_task(
    task: RunTask,
    cli_path: Path,
    dataset_directory: Path,
    runs_directory: Path,
    packet_bytes: int | None,
    keep_runs: bool = True,
) -> dict[str, Any]:
    scenarios_directory = dataset_directory / "scenarios"
    work_directory = dataset_directory / ".work"
    scenarios_directory.mkdir(parents=True, exist_ok=True)
    work_directory.mkdir(parents=True, exist_ok=True)
    if keep_runs:
        runs_directory.mkdir(parents=True, exist_ok=True)

    scenario = build_scenario(task, packet_bytes)
    scenario_path = scenarios_directory / f"{task.run_id}_scenario.json"
    scenario_path.write_text(json.dumps(scenario, indent=2), encoding="utf-8")

    with tempfile.TemporaryDirectory(
        prefix=f"{task.run_id}_",
        dir=work_directory,
    ) as temporary:
        simulator_output = Path(temporary) / "simulator_output"
        command = [
            str(cli_path),
            "--scenario",
            str(scenario_path),
            "--out",
            str(simulator_output),
            "--quiet",
        ]
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"Simulator failed for {task.run_id} with exit code "
                f"{result.returncode}.\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )

        generated_manifest = simulator_output / "run_001.json"
        if not generated_manifest.is_file():
            raise RuntimeError(
                f"Simulator did not export expected files for {task.run_id}"
            )

        manifest = json.loads(generated_manifest.read_text(encoding="utf-8"))
        manifest["dataset_metadata"] = scenario["dataset_metadata"]
        generated_manifest.write_text(
            json.dumps(manifest, indent=2),
            encoding="utf-8",
        )

                                                                                  
        produced = read_run(simulator_output, "run_001")
        run_row = {"run_id": task.run_id, **produced.network.iloc[0].to_dict(), **scenario["dataset_metadata"]}
        if not keep_runs:
            return run_row

        for suffix in RUN_FILE_SUFFIXES:
            source = simulator_output / f"run_001{suffix}"
            target = runs_directory / f"{task.run_id}{suffix}"
            if not source.is_file():
                raise RuntimeError(
                    f"Simulator did not create required file: {source.name}"
                )
            if target.exists():
                raise FileExistsError(
                    f"Refusing to overwrite existing run file: {target}"
                )
            shutil.copy2(source, target)

    return run_row


def generate_dataset(
    cli_path: str | Path,
    topology_paths: list[str | Path],
    output_directory: str | Path,
    max_runs: int = 300,
    seeds: list[int] | None = None,
    workers: int = 1,
    duration_s: float = 10.0,
    packet_bytes: int | None = None,
    random_state: int = 42,
    runs_output_directory: str | Path | None = None,
    keep_runs: bool = True,
) -> pd.DataFrame:
    executable = Path(cli_path).expanduser().resolve()
    if not executable.is_file():
        raise FileNotFoundError(f"Project901 CLI executable not found: {executable}")
    if workers <= 0:
        raise ValueError("workers must be greater than zero")
    if packet_bytes is not None and packet_bytes <= 0:
        raise ValueError("packet_bytes must be greater than zero")
    dataset_directory = Path(output_directory).expanduser().resolve()
    dataset_directory.mkdir(parents=True, exist_ok=True)

    if runs_output_directory is None:
        runs_directory = dataset_directory.parent
    else:
        runs_directory = Path(runs_output_directory).expanduser().resolve()
    runs_directory.mkdir(parents=True, exist_ok=True)

    topologies = [load_topology(path) for path in topology_paths]
    starting_number = next_run_number(
        runs_directory,
        dataset_directory / "scenarios",
    )
    tasks = build_tasks(
        topologies,
        seeds if seeds is not None else [1, 2, 3, 4, 5],
        duration_s,
        max_runs,
        random_state,
        run_id_start=starting_number,
    )

    finished = 0

    def run_one(task: RunTask) -> dict[str, Any]:
        nonlocal finished
        row = run_task(task, executable, dataset_directory, runs_directory, packet_bytes, keep_runs)
        finished += 1
        if finished % 25 == 0 or finished == len(tasks):
            print(f"{finished}/{len(tasks)} runs done", flush=True)
        return row

    with ThreadPoolExecutor(max_workers=workers) as executor:
        results = list(executor.map(run_one, tasks))

    runs_frame = pd.DataFrame(results)

    runs_frame.to_csv(dataset_directory / "runs_all.csv", index=False)
    return runs_frame


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, help="Path to netmind_cli executable")
    parser.add_argument(
        "--topologies",
        required=True,
        nargs="+",
        help="Topology JSON files to include",
    )
    parser.add_argument("--output", required=True, help="Dataset CSV/scenario directory")
    parser.add_argument(
        "--runs-output",
        help="Directory for individual run exports; defaults to the parent of --output",
    )
    parser.add_argument("--max-runs", type=int, default=300)
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3, 4, 5])
    parser.add_argument("--workers", type=int, default=1)
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--packet-bytes", type=int, default=None,
                        help="Fixed packet size for every flow (default: 500/1000/1500 at random)")
    parser.add_argument("--discard-runs", action="store_true",
                        help="Keep only the dataset rows, not each run's trace files")
    parser.add_argument("--random-state", type=int, default=42)
    args = parser.parse_args()

    runs = generate_dataset(
        cli_path=args.cli,
        topology_paths=args.topologies,
        output_directory=args.output,
        max_runs=args.max_runs,
        seeds=args.seeds,
        workers=args.workers,
        duration_s=args.duration,
        packet_bytes=args.packet_bytes,
        random_state=args.random_state,
        runs_output_directory=args.runs_output,
        keep_runs=not args.discard_runs,
    )
    run_directory = (
        Path(args.runs_output).resolve()
        if args.runs_output
        else Path(args.output).resolve().parent
    )
    print(
        f"Generated {len(runs)} runs.\n"
        f"Run files: {run_directory}\n"
        f"Dataset files: {Path(args.output).resolve()}"
    )


if __name__ == "__main__":
    main()
