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


ALGORITHMS = ("bfs", "dfs", "dijkstra", "bellman-ford")
LOAD_PERCENTAGES = (10, 50, 80, 120)
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


def _event_for(
    pattern: str,
    topology: TopologyInfo,
    duration_s: float,
    rate_mbps: float,
    packet_bytes: int,
) -> tuple[list[dict[str, Any]], float | None, float | None]:
    start = duration_s * 0.4
    end = duration_s * 0.65

    if pattern == "none":
        return [], None, None

    if pattern == "link_failure":
        link = min(topology.links, key=lambda item: float(item["bandwidthMbps"]))
        try:
            a = topology.id_to_name[int(link["a"])]
            b = topology.id_to_name[int(link["b"])]
        except KeyError as error:
            raise ValueError(f"Link endpoint is missing from topology: {link!r}") from error

        return [
            {"type": "LINK_FAILURE", "a": a, "b": b, "time_s": start},
            {"type": "LINK_RESTORE", "a": a, "b": b, "time_s": end},
        ], start, end

    if pattern == "router_failure":
        if not topology.routers:
            raise ValueError("Router failure requested for a topology without routers")
        router = topology.routers[0]
        return [
            {"type": "ROUTER_FAILURE", "node": router, "time_s": start},
            {"type": "ROUTER_RESTORE", "node": router, "time_s": end},
        ], start, end

    if pattern == "congestion":
        link = min(topology.links, key=lambda item: float(item["bandwidthMbps"]))
        a = topology.id_to_name[int(link["a"])]
        b = topology.id_to_name[int(link["b"])]
        congestion_duration = max(0.1, end - start)
        return [
            {
                "type": "CONGESTION",
                "a": a,
                "b": b,
                "time_s": start,
                "bandwidth_multiplier": 0.5,
                "extra_loss_rate": 0.02,
                "duration_s": congestion_duration,
            }
        ], start, start + congestion_duration

    if pattern == "burst":
        burst_duration = max(0.1, duration_s * 0.2)
        return [
            {
                "type": "TRAFFIC_BURST",
                "flow_id": 2,
                "src": topology.hosts[0],
                "dst": topology.hosts[1],
                "time_s": start,
                "duration_s": burst_duration,
                "rate_mbps": max(rate_mbps * 2.0, 0.1),
                "packet_bytes": packet_bytes,
            }
        ], start, start + burst_duration

    raise ValueError(f"Unsupported event pattern: {pattern}")


def build_scenario(task: RunTask, packet_bytes: int = 1000) -> dict[str, Any]:
    offered_rate = task.topology.minimum_bandwidth_mbps * task.load_pct / 100.0
    events, disturbance_start, disturbance_end = _event_for(
        task.event_pattern,
        task.topology,
        task.duration_s,
        offered_rate,
        packet_bytes,
    )

    return {
        "name": task.run_id,
        "topology": str(task.topology.path),
        "seed": task.seed,
        "duration_s": task.duration_s,
        "routing": {
            "algorithm": task.algorithm,
            "weight": "latency",
            "reconvergence_delay_ms": 0,
        },
        "flows": [
            {
                "id": 1,
                "src": task.topology.hosts[0],
                "dst": task.topology.hosts[1],
                "pattern": "constant",
                "rate_mbps": offered_rate,
                "packet_bytes": packet_bytes,
                "start_s": 0,
                "stop_s": task.duration_s,
            }
        ],
        "events": events,
        "dataset_metadata": {
            "run_id": task.run_id,
            "topology_path": str(task.topology.path),
            "algorithm": task.algorithm,
            "weight": "latency",
            "seed": task.seed,
            "load_pct_of_min_link_bandwidth": task.load_pct,
            "offered_rate_mbps": offered_rate,
            "event_pattern": task.event_pattern,
            "injected_events": events,
            "disturbance_start_s": disturbance_start,
            "disturbance_end_s": disturbance_end,
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
    packet_bytes: int,
) -> dict[str, Any]:
    scenarios_directory = dataset_directory / "scenarios"
    work_directory = dataset_directory / ".work"
    scenarios_directory.mkdir(parents=True, exist_ok=True)
    work_directory.mkdir(parents=True, exist_ok=True)
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

                                                                                  
        read_run(simulator_output, "run_001")

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

    retained_run = read_run(runs_directory, task.run_id)
    metadata = retained_run.manifest.get("dataset_metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}

    run_row = {
        "run_id": task.run_id,
        **retained_run.network.iloc[0].to_dict(),
        **metadata,
    }
    return run_row


def generate_dataset(
    cli_path: str | Path,
    topology_paths: list[str | Path],
    output_directory: str | Path,
    max_runs: int = 300,
    seeds: list[int] | None = None,
    workers: int = 1,
    duration_s: float = 10.0,
    packet_bytes: int = 1000,
    random_state: int = 42,
    runs_output_directory: str | Path | None = None,
) -> pd.DataFrame:
    executable = Path(cli_path).expanduser().resolve()
    if not executable.is_file():
        raise FileNotFoundError(f"Project901 CLI executable not found: {executable}")
    if workers <= 0:
        raise ValueError("workers must be greater than zero")
    if packet_bytes <= 0:
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

    with ThreadPoolExecutor(max_workers=workers) as executor:
        results = list(
            executor.map(
                lambda task: run_task(
                    task,
                    executable,
                    dataset_directory,
                    runs_directory,
                    packet_bytes,
                ),
                tasks,
            )
        )

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
    parser.add_argument("--packet-bytes", type=int, default=1000)
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
