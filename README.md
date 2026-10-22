# NetMind

NetMind is a C++17 discrete-event network simulator with a command-line interface, a React workbench, and Python analysis tools. It models a network from a topology or scenario file, simulates packet traffic and network events, and exports run data for comparison and review.

This README describes features present in the repository. It does not describe planned work.

## Implemented features

### C++ network simulator

- Models hosts, routers, switches, packets, flows, links, forwarding tables, and simulation events.
- Loads network topologies and scenarios from JSON files.
- Represents the topology as a graph and provides breadth-first search, depth-first search, connected-component, and reachability operations.
- Provides BFS, DFS, Dijkstra, and Bellman–Ford routing. The CLI supports hop-count and latency weights.
- Simulates packet transmission time, propagation delay, directional link queues, queue limits, packet loss, and link or router availability.
- Schedules link and router failures and restorations, temporary link congestion, and temporary burst traffic.
- Collects packet traces, flow and network metrics, and event information. Exports run manifests and CSV files for traces, flows, links, time windows, and network summaries.

### Command-line interface

The interactive CLI can load topologies or scenarios, configure routing and traffic, inspect routes and queues, run simulations, and export results. It includes:

`load`, `show topology`, `show route`, `show queue`, `show metrics`, `set algo`, `set weight`, `set seed`, `route add`, `add flow`, `ping`, `traceroute`, `fail link`, `restore link`, `fail router`, `restore router`, `congest link`, `burst`, `run`, `step`, `compare`, `export`, `reset`, `help`, and `exit`.

It can also execute a text command script or run a scenario file from the command line.

### Python API and React workbench

- A FastAPI service connects the GUI to the C++ simulator and exported run files.
- The React workbench configures and runs simulations, can save scenarios, shows expected-baseline results, loads runs into the replay viewer, and provides a GraphRAG question panel.
- The replay viewer reads exported run files and displays packet movement and run comparisons; it does not run simulations itself.

### ML expected baseline

The baseline module uses a Random Forest regressor to estimate latency, throughput, and packet loss from a generated run dataset. Evaluation groups runs by topology so a topology can be held out from model training. The GUI compares a completed run with these estimates.

The baseline is a reference based on the dataset, not an optimal result. It needs `data/dataset/runs_all.csv`; estimates may be less reliable for topologies unlike those represented in that dataset.

### GraphRAG

GraphRAG builds a NetworkX graph from topology, run, flow, event, trace, and window data. Its current query implementation uses supported question patterns and recorded facts to answer run-specific questions and return supporting evidence.

GraphRAG is currently fact-based. It does not use an LLM or retrieve information from external knowledge sources.

## Current scope limits

- ML provides expected-metric baseline comparisons. Anomaly detection and future-metric forecasting are not included in the current implementation.
- ML predictions do not choose routes or change the simulator's routing decisions.
- GraphRAG does not generate answers with Groq, LangChain, or LangGraph.
- Run data is stored as local JSON and CSV files; there is no database service.

## Repository layout

```text
apps/                 C++ CLI entry point
config/topologies/    Example topology JSON files
config/scenarios/     Example scenario JSON files
frontend_v2/          React and Vite workbench
include/netmind/      C++ public headers
python/               FastAPI service, ML baseline, GraphRAG, and data tools
src/                  C++ simulator implementation
tests/                C++ tests and fixtures
python/tests/          Python tests
data/                 Generated run files and training data (created locally)
```

## Build and test the C++ project

From the repository root, configure, build, and run the C++ tests:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
```

CMake downloads Catch2 and nlohmann/json when configuring, so the first configure needs network access. The CLI executable is created as `build/debug/netmind_cli` on Linux/macOS and `build/debug/netmind_cli.exe` on Windows.

Run Python tests from the repository root:

```sh
python -m pip install -r python/requirements.txt
cd python
python -m pytest -q
```

## Use the CLI

Start the interactive CLI from the repository root:

```sh
./build/debug/netmind_cli
```

On Windows PowerShell:

```powershell
.\build\debug\netmind_cli.exe
```

Example interactive session:

```text
load config/topologies/ring_backup.json
set seed 42
set algo dijkstra
set weight latency
add flow PC1 PC2 5 1000 0 10
run 10
show metrics
export
```

To run a saved scenario and export its output:

```sh
./build/debug/netmind_cli --scenario config/scenarios/ring_backup.json --out data
```

Use `netmind_cli --help` for the supported command-line options and `help` inside the interactive CLI for the command list. Exported files are written to `data/` by default; use `--out` or the `export` command to choose another output directory.

## Use the API and React workbench

Install the Python dependencies from the repository root:

```sh
python -m pip install -r python/requirements.txt
```

Start the API in one terminal:

```sh
python -m uvicorn netmind_api:app --app-dir python --host 127.0.0.1 --port 8765
```

The API defaults to `build/debug/netmind_cli.exe`. Set `NETMIND_CLI` to the actual executable path if you are on a platform that uses a different binary name or built it elsewhere.

Start the React workbench in another terminal:

```sh
cd frontend_v2
npm install
npm run dev
```

Vite proxies `/api` requests to the local API on port `8765`. The frontend also provides `npm run build` and `npm run lint` scripts.

## Generate the ML dataset

The baseline requires a generated dataset. Example for Windows PowerShell:

```powershell
python python\ml\generate_dataset.py `
  --cli build\debug\netmind_cli.exe `
  --topologies config\topologies\ring_backup.json `
  --output data\dataset `
  --max-runs 80 `
  --seeds 1 2 3 4 5 `
  --workers 1 `
  --duration 10
```

The generator writes aggregate run and time-window CSV files, along with per-run artifacts. Run `python python/ml/generate_dataset.py --help` to see all options.

## Data and tests

Generated simulation runs and datasets are local artifacts and are not included in the repository by default. Topology and scenario examples are kept under `config/`.

The C++ suite is built by CMake/CTest. The Python suite is run with pytest. The frontend can be checked with its lint and production-build scripts.
