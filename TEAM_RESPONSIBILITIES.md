# Project901 — Team Responsibilities

Roles follow the Implementation Guide (v2), Section 10:

| Member | Layer |
| --- | --- |
| **A** | Model, graph, CLI, viewer |
| **B** | Routing, simulation engine, scenarios |
| **C** | Metrics, ML, GraphRAG, contracts |

> **Source note:** the phase ownership in Part 1 comes directly from the guide.
> The guide only says that Phase 0 is done by "all" and that C owns the contracts and golden trace.
> The detailed Phase 0 split in Part 2 is a **suggestion** for the team to confirm, based on who uses each piece.

---

## Part 1 — Who owns each phase

| Phase | What it is | Owner | Notes |
| --- | --- | --- | --- |
| **0** | Foundation and contracts | **A, B, C** | Everyone. Detailed split in Part 2. |
| **1** | Network model (`Node`, `Host`, `Router`, `Link`, `Packet`, `Flow`) | **A** | Weeks 1–2 |
| **2** | Graph engine and topology loader (`Graph`, BFS/DFS, `UnionFind`, loader, sample topologies) | **A** | Weeks 3–4 |
| **3** | Routing engine (`WeightPolicy`, BFS, Dijkstra, Bellman-Ford, `RoutingManager`) | **B** | Weeks 3–6 |
| **4** | Simulation engine (`EventQueue`, `Channel`, packet events, failures, adaptive routing, `ScenarioManager`) | **B** | Weeks 2–7. Skeleton starts in week 2. |
| **5** | Metrics and trace logging (`TraceLogger`, `MetricsCollector`, exporters, report) | **C** | Weeks 5–8. Starts early using the golden trace. |
| **5B** | Packet Tracer-style CLI (`ping`, `traceroute`, `show route`, `compare`) | **A + B** | Weeks 7–8. A: framework and commands. B: `compare` support. |
| **6** | ML module (Python): dataset, features, anomaly detection, forecasting | **C** | Weeks 9–10. B helps with dataset generation. |
| **7** | GraphRAG layer (Python): knowledge graph, facts, query | **C** | Weeks 9–10, with A/B support |
| **8** | Visualization (HTML replay viewer) | **A leads**, **C** supplies data panels | Weeks 9–11 |
| **9** | Integration, evaluation, report | **A, B, C** | Weeks 11–12 |

### Timeline at a glance

| Week | A | B | C |
| --- | --- | --- | --- |
| 1 | Phase 0 | Phase 0 | Phase 0, contracts, golden trace |
| 2 | Phase 1 (model) | EventQueue and engine skeleton | Python readers, viewer prototype on golden trace |
| 3 | Graph, BFS/DFS, UnionFind | Routing interface, `WeightPolicy`, tests on a mock graph | Observer replay harness |
| 4 | Loader, sample topologies | Dijkstra, Bellman-Ford; `Channel` and packet events | `TraceLogger` |
| 5 | Switch/Poisson, CLI framework | Finish Phase 3; failures, adaptive routing | `MetricsCollector` core |
| 6 | CLI commands (`show`, `load`, `set`) | `ScenarioManager`, `EventFactory`, determinism tests | Windows, links, exporters |
| 7 | CLI `ping`, `traceroute`, `step` | Integration tests, `compare` support | Report class, validation; ML dataset generator |
| 8 | CLI `compare`, `export`, script mode | Benchmarks, bug fixing | Finish Phase 5; start anomaly/forecast |
| 9 | Viewer parser/layout/renderer | Dataset support, congestion-aware routing | Phase 6; KG builder |
| 10 | Viewer controls and charts | Distance-vector convergence; scenarios S1–S3 | Phase 7; baseline (optional) |
| 11 | Integration | Integration, complexity plots | Integration, plots, predicted-vs-actual |
| 12 | Report, README | Slides, demo rehearsal | Report ML/GraphRAG sections |

---

## Part 2 — Who owns each part of Phase 0 (suggested)

Rule of thumb: the person whose layer uses a thing writes it, and the people whose layers depend on it review it.

| Part | Item | Owner | Reviewers | Status |
| --- | --- | --- | --- | --- |
| **0.1 Build system** | Root `CMakeLists.txt`, `netmind_core` / `netmind_cli` / `netmind_tests` targets, Catch2 and JSON via FetchContent | **A** | B, C | Done |
| | `CMakePresets.json`, `Sanitize` build type | **B** | A | To do |
| **0.2 Conventions** | `docs/conventions.md` | **A** | B, C | Done |
| | `.clang-format`, `.editorconfig`, PR template (incl. `[HEADER CHANGE]` rule) | **A** | B, C | To do |
| **0.3 Common types** | `Types.h`, `Units.h`, `Errors.h`, with tests | **A** | B, C | Done |
| | `Rng.h` (the engine owns the RNG) | **B** | A | Done |
| | `Ip.h`, `UnionFind.h` (used by model and graph) | **A** | B | Done |
| | `MinHeap.h` (used by Dijkstra and the event queue) | **B** | A | Done |
| **0.4 Header freeze** | `model/` headers (`NetworkEntity`, `Node`, `Host`, `Router`, `Switch`, `Link`, `Packet`, `Flow`, `TrafficPattern`, `ForwardingTable`) | **A** | B, C | To do |
| | `graph/` headers (`Graph`, `TopologyLoader`) | **A** | B | To do |
| | `routing/` headers (`RoutingAlgorithm`, `WeightPolicy`, all algorithms, `RoutingManager`, `RoutingFactory`) | **B** | A | To do |
| | `simulation/` headers (`SimContext`, `SimulationEngine`, `EventQueue`, `Channel`, `NetworkEvent`, `events/*`, `ScenarioManager`, `EventFactory`) | **B** | A, C | To do |
| | `SimObserver.h` (the interface C implements) | **B** with **C** | A | To do |
| | `metrics/` headers (`TraceLogger`, `MetricsCollector`, `Exporters`, `Report`) | **C** | B | To do |
| | `cli/` headers (`Command`, `CommandRegistry`, `Session`, `CommandLineInterface`, commands) | **A** | B | To do |
| | Compile-each-header-alone test, stub `.cpp` files | **A** (shared) | all | To do |
| | **Review and tag `headers-v1`** | **A, B, C** together | all | To do |
| **0.5 Contracts and fixtures** | Topology contract (`topology.md`) | **A** | C | Done |
| | Scenario contract (`scenario.md`) | **B** | C | Done |
| | Trace and output-file contract (`trace.md`) | **C** | A, B | Done |
| | Golden trace (`mini_trace.csv`) and `mini_topology.json` | **C** | A, B | Done |
| **0.6 Tooling and CI** | GitHub Actions: build, `ctest`, sanitizer build | **B** | A | To do |
| | Python setup: `requirements.txt`, `pyproject.toml`, dummy pytest, `pytest` in CI | **C** | B | To do |

### Phase 0 is done when (from the guide)

- `cmake --build` succeeds with stubs
- the dummy C++ and Python tests pass in CI
- headers are merged and tagged `headers-v1`
- contract docs and golden files exist

### After the freeze

Any change to a frozen header's signature needs a pull request titled `[HEADER CHANGE]`, approved by **all three** members.
