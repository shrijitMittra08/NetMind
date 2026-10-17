# NetMind Usage Guide

This guide describes how to build and use the project as it exists today. It covers the C++ simulator and command-line interface, scenario files, exported run data, the replay viewer, and the Python GraphRAG package.

NetMind is a discrete-event network simulator. It models hosts, routers, links, traffic flows, routing, failures, and metrics. The current Python package contains GraphRAG tooling. The previously developed Phase 6 machine-learning module was reverted, so ML dataset generation, anomaly detection, and forecasting are not currently available.

## 1. Requirements

- C++17 compiler supported by CMake (GCC or Clang recommended).
- CMake 3.25 or newer.
- Ninja, used by the supplied CMake presets.
- Python 3.10 or newer for Python tools and tests.
- Internet access for the first C++ configure if Catch2 and nlohmann/json are not already cached. CMake downloads these through FetchContent.

Run project commands from the repository root so paths such as `config/topologies/small4.json` resolve correctly.

## 2. Build and test

### Windows PowerShell

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The CLI executable is `build/debug/netmind_cli.exe` and the test executable is `build/debug/netmind_tests.exe`.

### Git Bash, Linux, or macOS

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The CLI executable is `build/debug/netmind_cli` on Unix-like systems.

Other available presets are `release` and `sanitize`. The sanitizer preset requires a compiler installation with AddressSanitizer and UndefinedBehaviorSanitizer runtimes available.

### Python package and tests

From the repository root:

```powershell
python -m pip install -r python/requirements.txt
python -m pip install -e python
python -m pytest -q python/tests
```

The Python package currently needs NetworkX for GraphRAG. `pytest` is needed to run its tests. The editable install lets Python import the package as `graphrag`.

## 3. Topology files

Example topologies are in `config/topologies/`:

| File | Example use |
|---|---|
| `small4.json` | Small four-node route and CLI demonstrations |
| `medium10.json` | A larger topology for routing experiments |
| `ring_backup.json` | Failure and alternate-route demonstrations |
| `dumbbell.json` | Shared-bottleneck and congestion experiments |
| `diamond_weighted.json` | Compare a short route with a differently weighted alternative |

A topology JSON has `nodes` and `links` arrays. Each node has a unique numeric `id`, `type` (`HOST`, `ROUTER`, or `SWITCH`), unique `name`, and unique valid IPv4 `ip`. Each link specifies endpoint IDs `a` and `b`, positive `bandwidthMbps`, non-negative `latencyMs`, and `lossRate` from 0 to 1. `queueCapacity` is optional and measured in packets; it defaults to 64. Hosts must each have exactly one link, connected to a router. See [`contracts/topology.md`](contracts/topology.md) for the full contract.

## 4. Use the interactive CLI

Start the CLI from the repository root:

```powershell
build\debug\netmind_cli.exe
```

At the `netmind>` prompt, load a topology and inspect it:

```text
load config/topologies/small4.json
show topology
help
```

`help` lists registered commands. Commands can be entered in a script file as well as interactively. Paths are interpreted relative to the process's current working directory.

### Available commands

| Command | Purpose |
|---|---|
| `load <file>` | Load a topology JSON or scenario JSON. |
| `show topology` | List nodes, neighbors, and link properties. |
| `show route <router>` | Display a router's forwarding table. |
| `show queue <A>-<B>` | Display queue and drop counters for both directions of a link. You may also use `show queue <A> <B>`. |
| `show metrics` | Display network and per-flow summary metrics. |
| `show metrics <flow-id>` | Display metrics for one flow. |
| `set algo <bfs\|dfs\|dijkstra\|bellman-ford>` | Select the routing algorithm. |
| `set weight <hops\|latency>` | Select hop-count or latency routing weights. |
| `set seed <n>` | Set the random seed and reset the run. |
| `route add <router> <destination> via <next-hop>` | Add a static route through a directly connected next hop. |
| `add flow <src-host> <dst-host> <Mbps> <bytes> [start stop] [bursty]` | Add constant-rate traffic; append `bursty` to use a bursty pattern. If times are omitted, it starts now and runs for 10 simulated seconds. |
| `ping <src-host> <dst-host> [count]` | Send simulated echo requests; count defaults to 4. |
| `traceroute <src-host> <dst-host>` | Show nodes reached by a simulated probe along its route. |
| `fail link <A> <B> at <time>` / `restore link <A> <B> at <time>` | Schedule a link outage or restoration at simulation time in seconds. |
| `fail router <name> at <time>` / `restore router <name> at <time>` | Schedule a router outage or restoration. |
| `congest link <A> <B> factor <f> at <time> for <duration>` | Temporarily reduce a link's bandwidth by the specified factor. For example, `factor 0.5` halves it. |
| `burst <src-host> <dst-host> <Mbps> at <time> for <duration>` | Schedule a temporary traffic burst using 1000-byte packets. |
| `run <seconds>` | Advance by the given number of simulated seconds from the current time. |
| `step [n]` | Process the next event, or the next `n` events. |
| `compare <src-host> <dst-host>` | Compare BFS, Dijkstra, and Bellman–Ford on a loaded scenario. This command requires a scenario, not a topology-only load. |
| `export [directory]` | Export trace, metrics, and manifest files. Defaults to `data/runs`. |
| `reset` | Rebuild the runtime while retaining the loaded topology or scenario. |
| `exit` | Leave the CLI. |

Times in interactive commands are simulation seconds. Commands report errors with a leading `%`. For example, load `ring_backup.json` and try:

```text
load config/topologies/ring_backup.json
show topology
show route R1
ping PC1 PC2 2
traceroute PC1 PC2
add flow PC1 PC2 5 1000 0 10
fail link R1 R2 at 4
restore link R1 R2 at 7
run 10
show metrics
export data/runs
```

The `traceroute` command reports the nodes the probe reaches. It is a simulator route probe, not a full TTL-based implementation of operating-system traceroute.

## 5. Run a command script

The repository includes [`viz/replay_demo.txt`](../viz/replay_demo.txt), a CLI command script. From the repository root, run:

```powershell
build\debug\netmind_cli.exe --script viz/replay_demo.txt
```

The script loads the ring topology, adds traffic, schedules a link failure and restoration, runs the simulation, and exports the run to `data/runs`. You can choose another export directory:

```powershell
build\debug\netmind_cli.exe --script viz/replay_demo.txt --out data/my_runs
```

The script itself contains an `export "data/runs"` command, which overrides `--out` when it executes. Edit the script's export line if you want it to write elsewhere.

Other executable options:

```text
netmind_cli --help
netmind_cli --scenario FILE [--out DIR] [--quiet] [--json-summary]
netmind_cli --script FILE [--out DIR] [--quiet] [--json-summary]
```

`--scenario` and `--script` cannot be combined. `--quiet` suppresses normal output. `--json-summary` prints a JSON summary after the run. For scenario mode, NetMind runs to the scenario's full duration and exports automatically. In script mode, include an `export` command in the script.

## 6. Create and run a scenario

Scenario files specify a topology, seed, duration, routing configuration, flows, and scheduled events. The scenario loader resolves a relative topology path from the scenario file's directory. The sample repository may not have a `config/scenarios/` directory yet, so create it first:

```powershell
New-Item -ItemType Directory -Force config/scenarios
```

In Git Bash, use `mkdir -p config/scenarios`. Then create `config/scenarios/ring_failure.json` with:

```json
{
  "name": "ring_failure_demo",
  "topology": "../topologies/ring_backup.json",
  "seed": 42,
  "duration_s": 10,
  "routing": {
    "algorithm": "dijkstra",
    "weight": "latency",
    "reconvergence_delay_ms": 0
  },
  "flows": [
    {
      "id": 1,
      "src": "PC1",
      "dst": "PC2",
      "pattern": "constant",
      "rate_mbps": 5,
      "packet_bytes": 1000,
      "start_s": 0,
      "stop_s": 10
    }
  ],
  "events": [
    {"type": "LINK_FAILURE", "a": "R1", "b": "R2", "time_s": 4},
    {"type": "LINK_RESTORE", "a": "R1", "b": "R2", "time_s": 7}
  ]
}
```

Run it from the repository root:

```powershell
build\debug\netmind_cli.exe --scenario config/scenarios/ring_failure.json --out data/runs --json-summary
```

The scenario's required top-level fields are `name`, `topology`, `seed`, `duration_s`, `routing`, `flows`, and `events`. A flow pattern can be `constant`, `bursty`, or `poisson`. A bursty flow also needs `on_duration_s` and `off_duration_s`. Event types currently supported are `LINK_FAILURE`, `LINK_RESTORE`, `ROUTER_FAILURE`, `ROUTER_RESTORE`, `CONGESTION`, and `TRAFFIC_BURST`. Link events use endpoint names `a` and `b`; router events use `node`. A `CONGESTION` event uses link endpoints plus `bandwidth_multiplier`, `extra_loss_rate`, and `duration_s`. A `TRAFFIC_BURST` event uses `flow_id`, host names `src` and `dst`, `rate_mbps`, `packet_bytes`, and `duration_s`. All events require `time_s`. See [`contracts/scenario.md`](contracts/scenario.md) for the field contract.

## 7. Understand exported run files

By default, `export` creates a new `run_NNN` prefix under `data/runs` and writes:

| File | Contents |
|---|---|
| `<run>_manifest.json` | Seed, routing algorithm, weight policy, and simulation end time. |
| `<run>_trace.csv` | Timestamped packet, forwarding, queue, delivery, drop, and network events. |
| `<run>_flows.csv` | Per-flow sent/delivered/dropped counts and latency, throughput, loss, jitter, and hop summaries. |
| `<run>_links.csv` | Per-direction link utilization, queue, transmission, drop, and downtime metrics. |
| `<run>_windows.csv` | Time-window metrics used by the viewer. |
| `<run>_network.csv` | One-row network-level summary. |

The trace uses the columns `time_s,event_type,packet_id,flow_id,location,detail`. The exporter also keeps a live trace at `data/runs/netmind_live_trace.csv` while a CLI session is running; `export` copies that trace into the selected run's files.

The current CLI-generated manifest uses the generic topology label `loaded topology`; it does not store a resolvable topology path. When using GraphRAG on these exports, supply the topology file explicitly.

## 8. Replay a run in the browser viewer

1. Generate or choose an exported run.
2. Open `viz/index.html` in a browser. If your browser restricts local files, start a static server from the repository root:

   ```powershell
   python -m http.server 8000 --directory viz
   ```

   Then open `http://localhost:8000`.

3. Click **Load files** and select the topology JSON, that run's `_trace.csv` and `_windows.csv`, and optionally its `_flows.csv` and `_links.csv`.
4. Use the time slider or **Play** to replay. Choose a speed, use **Next event** or **Next failure/drop**, and click a device to inspect it. The metric selector changes the plotted window metric.

Select files from the same run. The viewer reads files locally in the browser; it does not run simulations or modify the input files. It shows recorded events and metrics, not historical per-router forwarding tables.

## 9. Use the GraphRAG package

GraphRAG builds a NetworkX graph from one exported run, extracts evidence-backed facts, and answers a small set of question types using deterministic templates and retrieval. It does not call an LLM.

Install the package as described in Section 2. Then, from the repository root, create a Python script or start a Python session with the following example. It uses the included `run_001` exports and their matching topology copy:

```python
import csv
from pathlib import Path

from graphrag.build_kg import build_run_graph
from graphrag.facts import extract_facts
from graphrag.query import answer_question

runs = Path("data/runs")
run_id = "run_001"
topology = runs / "ring_backup.json"

graph = build_run_graph(
    runs_directory=runs,
    run_id=run_id,
    topology_path=topology,
    cache_path=None,  # Optional path to save a pickle graph cache.
)

with (runs / f"{run_id}_trace.csv").open(
    encoding="utf-8-sig", newline=""
) as stream:
    trace_rows = list(csv.DictReader(stream))

facts = extract_facts(graph, trace_rows)
result = answer_question(
    graph,
    {run_id: facts},
    "What route did PC1 to PC2 take?",
)

print(result["answer"])
print("Citations:", result["citations"])
```

For a different run, update `run_id` and set `topology` to the exact topology used in that run. `build_run_graph` requires the run manifest, trace, flows, links, and windows files. `extract_facts` derives facts from failure, congestion, and explicitly labeled anomaly data. `answer_question` supports path, algorithm comparison, congestion, and time-specific drop/latency questions, plus a fact-search fallback. Answers return citations so you can inspect the supporting facts.

Run the Python tests with:

```powershell
python -m pytest -q python/tests
```

## 10. Current scope and limits

- The main simulator is C++17 and runs through the CLI; the viewer is a static browser app.
- Routing algorithms available in the CLI are BFS, DFS, Dijkstra, and Bellman–Ford. `compare` currently compares BFS, Dijkstra, and Bellman–Ford.
- Routing weights exposed by the CLI are hop count and latency.
- GraphRAG is a Python library, not a separate CLI application. It uses NetworkX and deterministic templates/retrieval; no LLM connection is configured.
- Phase 6 ML tools were reverted. ML dataset generation, anomaly model training, forecasting, and their notebooks are not part of the current checkout.
- The viewer visualizes recorded trace events and exported window metrics; it cannot alter or rerun a simulation.

## 11. Troubleshooting

- **`cmake` cannot find Ninja or a compiler:** install them and ensure they are on `PATH`, then rerun `cmake --preset debug`.
- **First configure fails fetching dependencies:** check network access, then rerun the configure command.
- **CLI says it cannot open a topology/scenario:** run the CLI from the repository root or pass a path that is valid from your current directory.
- **`compare` says a scenario is required:** load a scenario JSON rather than a topology-only JSON.
- **Viewer shows no events or metrics:** confirm you selected the topology, trace, and windows files from the same run. Flow and link CSVs are optional.
- **GraphRAG cannot locate the topology:** pass `topology_path` explicitly; current exported manifests may only say `loaded topology`.
- **Python reports `No module named 'graphrag'`:** install the package with `python -m pip install -e python` from the repository root, or run Python from the `python/` directory.
- **Generated runs do not appear in Git status:** `data/runs/` outputs are ignored by `.gitignore` except for its `.gitkeep` placeholder. Exported run files can be used locally without being tracked by Git.
