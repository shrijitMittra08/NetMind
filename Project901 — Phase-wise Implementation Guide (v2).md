# Project901 — Phase-wise Implementation Guide (v2)

### Graph-Based Network Simulation, Routing & Analysis Engine (Packet Tracer-style)

This guide merges the **Project Blueprint** and the **Implementation Plan** into one build manual. The 5-layer architecture is unchanged. What is new: every phase is broken into **modules**, and each module says *what it contains*, *how to build it*, and *how to test it*. Section 1 lists the gaps and contradictions found in the two source documents, with a decision for each. Freeze these decisions in Week 1.

**Phase map**

| Phase | Name | Layer | Language | Weeks | Primary owner |
| --- | --- | --- | --- | --- | --- |
| 0 | Foundation & contracts | all | C++/docs | 1 | All |
| 1 | Network model | 0 | C++ | 1–2 | A |
| 2 | Graph engine & topology loader | 1 | C++ | 3–4 | A |
| 3 | Routing engine | 2 | C++ | 3–6 | B |
| 4 | Simulation engine | 2 | C++ | 2–7 | B |
| 5 | Metrics & trace logging | 3 | C++ | 5–8 | C |
| 5B | Packet Tracer-style CLI | 5 | C++ | 7–8 | A + B |
| 6 | ML module | 4 | Python | 9–10 | C |
| 7 | GraphRAG layer | 4 | Python | 9–10 | C (+A/B) |
| 8 | Visualization | 5 | JS/Python | 9–11 | A (+C) |
| 9 | Integration, evaluation, report | all | all | 11–12 | All |

---

## 1. Decisions to Freeze in Week 1 (gaps found in the source documents)

| # | Topic | What the sources say | Decision | Why |
| --- | --- | --- | --- | --- |
| D1 | Link direction | `Link` is one undirected edge; `linkBuffers` is keyed by link | Graph edge stays **undirected**. Dynamic state lives in **two directional `Channel`s per link** (A→B and B→A), each with its own FIFO and busy flag | A real cable is full-duplex. One shared buffer would let opposite-direction traffic fight for space and distort every metric |
| D2 | Layer dependencies | `SimulationEngine` "has a" `MetricsCollector` | Engine reports through a **`SimObserver` interface**. `MetricsCollector` and `TraceLogger` implement it | Layer 2 must not depend on Layer 3. Also gives the Observer pattern the blueprint lists as stretch |
| D3 | Ownership | Blueprint uses raw `Node*` in `Graph` | `Graph` owns `unique_ptr<Node>` / `unique_ptr<Link>`. Everyone else holds ids or non-owning raw pointers | Matches the RAII goal and removes lifetime bugs |
| D4 | How nodes act on packets | `Node::receivePacket(const Packet&)` | `receivePacket(Packet pkt, LinkId inLink, SimContext& ctx)` where `SimContext` is an **abstract facade** (`now()`, `transmit()`, `deliver()`, `drop()`, `rng()`) | A node cannot schedule or forward anything without engine access. An abstract facade lets Router/Host be unit-tested with a mock |
| D5 | Units | Mixed (ms, s, Mbps, bytes) | Sim clock = **seconds (double)**. Config uses ms / Mbps / bytes. Conversion only in `common/Units.h` | Most common source of metric bugs |
| D6 | Determinism | "Fixed seed" | One `std::mt19937_64` owned by the engine. Events ordered by `(time, sequence_number)`. **Never let `unordered_map` iteration order affect behavior or output**; sort keys first | Iteration order differs between compilers. Equal timestamps are common (e.g. two flows starting at t=0) |
| D7 | `Flow` | `Flow` holds `queue<Packet>` | `Flow` holds a **`TrafficPattern` generator** and creates each packet when its generate-event fires | A 60 s flow at 10k pps would pre-allocate 600k packets for nothing |
| D8 | Routing API | `computeRoute(g, src, dst)` only | Add `computeTable(g, src)` → `{dest → nextHop}` for all destinations. Keep `computeRoute` for `traceroute`/`compare` | Dijkstra/BFS/Bellman-Ford compute single-source trees anyway. Forwarding tables need next hops |
| D9 | Edge weights | Bellman-Ford "handles negative adjustments" | Weights are **always ≥ 0** (latency + optional congestion penalty). Show Bellman-Ford's negative-weight handling only in a unit test on a synthetic graph | No real latency is negative. Don't claim an advantage the simulation never uses |
| D10 | Failure recovery | "Ask the algorithm to recompute" | Recompute through `RoutingManager` with an optional **`reconvergenceDelay`** per algorithm (default 0; e.g. Bellman-Ford/RIP-like 3 s, Dijkstra/OSPF-like 0.2 s) | With instant recompute all algorithms recover identically and the failure comparison tells you nothing |
| D11 | Packet fields | id, src, dst, size, timestamps, path | Add `PacketType {DATA, ECHO_REQUEST, ECHO_REPLY}`, `ttl` (default 64) and `originTime` | Needed for `ping` RTT, loop protection during reroute, and `traceroute` |
| D12 | Trace format | `time_s, event_type, ...` with ambiguous names | Tightened spec in Section 4 (fixed event vocabulary, location naming, `k=v;k=v` detail) | The trace is the contract between C++, ML, GraphRAG and the viewer |
| D13 | Phase-I scope | Blueprint calls adaptive routing "future"; the Implementation Plan puts it in Phase 4 | Adaptive routing is **Tier 1** (the failure demo needs it). NL event creation and the ML expected-metric baseline stay **Tier 3** | Keeps Packet Tracer feel in the core, keeps risky AI parts optional |

---

## 2. Scope Tiers

| Tier | Contents | Rule |
| --- | --- | --- |
| **1 — Core (never cut)** | Phases 0–5B: model, graph, BFS/Dijkstra/Bellman-Ford, event simulation with queues/loss, link/router failure with reroute, congestion & burst events, metrics + trace export, CLI (`ping`, `traceroute`, `show route`, `compare`) | Graded on OOP + DS&A |
| **2 — Should** | Anomaly detection + forecasting, basic GraphRAG Q&A (templates/keyword), HTML replay viewer | Scale down before cutting Tier 1 |
| **3 — Could** | `Switch`, Poisson traffic, DFS routing baseline, reconvergence delay, TTL-based traceroute, ML expected-metric baseline, NL event creation, LangGraph workflow | Cut in the order listed in Section 12 |
| **4 — Future** | ARP, VLANs, spanning tree, longest-prefix-match trie, TCP congestion control, per-node CLI modes | Document as future work only |

---

## 3. Tech Stack, Conventions and Repository

| Concern | Choice |
| --- | --- |
| C++ | C++17, CMake ≥ 3.16, GCC/Clang with `-Wall -Wextra -Wpedantic`, optional `-fsanitize=address,undefined` build type `Sanitize` |
| Tests (C++) | Catch2 v3 (or GoogleTest) fetched with CMake `FetchContent` |
| JSON | `nlohmann/json` (header-only, `FetchContent`) |
| Style | `clang-format` (Google or LLVM base); `PascalCase.h/.cpp` file names for classes, `camelCase` methods, trailing `_` on private members, namespace `netmind` |
| Python | 3.10+, `venv`, `pandas numpy scikit-learn statsmodels matplotlib networkx pytest` (+ optional `langgraph`/LLM SDK) |
| Integration | **Files, not bindings.** C++ writes CSV/JSON; Python reads them. Python can launch the C++ CLI with `subprocess`. No pybind11 needed |
| CI | GitHub Actions: build, `ctest`, sanitizer build, `pytest` on every PR |
| Git | `main` is always green. Feature branches `a/graph-bfs`, `b/dijkstra`, … Small PRs, one reviewer from another member |

**Repository layout** (the blueprint's layout plus the additions this plan needs)

```
Project901/
├── CMakeLists.txt
├── include/netmind/
│   ├── common/    Types.h  Units.h  Rng.h  Errors.h  MinHeap.h(opt)  UnionFind.h
│   ├── model/     NetworkEntity.h Node.h Router.h Host.h Switch.h Link.h Packet.h Flow.h TrafficPattern.h ForwardingTable.h
│   ├── graph/     Graph.h  TopologyLoader.h
│   ├── routing/   RoutingAlgorithm.h WeightPolicy.h BFSRouting.h DFSRouting.h DijkstraRouting.h BellmanFordRouting.h RoutingManager.h RoutingFactory.h
│   ├── simulation/ SimContext.h SimulationEngine.h EventQueue.h Channel.h NetworkEvent.h events/*.h SimObserver.h ScenarioManager.h EventFactory.h
│   ├── metrics/   MetricsCollector.h TraceLogger.h Exporters.h Report.h
│   └── cli/       CommandLineInterface.h Command.h commands/*.h
├── src/            (mirrors include/)
├── apps/           netmind_cli.cpp    (main for the CLI executable)
├── tests/          unit/ integration/ golden/
├── config/
│   ├── topologies/ small4.json medium10.json ring_backup.json dumbbell.json
│   └── scenarios/  normal.json link_failure.json congestion.json
├── python/
│   ├── ml/  graphrag/  common/ (readers for trace/metrics CSV)
│   └── tests/
├── viz/            index.html  js/{parser,layout,renderer,controls,charts}.js
├── data/           runs/  plots/  (git-ignored except samples)
└── docs/           contracts/ (trace, schema, scenario)  architecture.md  report/
```

---

## 4. Contracts (agree in Phase 0; changes need all three members' approval)

### 4.1 Topology file (`config/topologies/*.json`)

```json
{
  "nodes": [
    {"id": 1, "type": "HOST",   "name": "PC1", "ip": "10.0.0.1",   "pos": [80, 200]},
    {"id": 2, "type": "ROUTER", "name": "R1",  "ip": "10.0.0.254", "pos": [260, 200]}
  ],
  "links": [
    {"a": 1, "b": 2, "bandwidthMbps": 100, "latencyMs": 2, "lossRate": 0.0, "queueCapacity": 64}
  ]
}
```

`pos` is optional (viewer auto-layouts if missing). `queueCapacity` is in **packets** and defaults to 64. Names must be unique. IPs must be unique and valid dotted-quad.

### 4.2 Scenario file (`config/scenarios/*.json`)

```json
{
  "name": "link_failure_demo",
  "topology": "config/topologies/ring_backup.json",
  "seed": 42,
  "duration_s": 60,
  "routing": {"algorithm": "dijkstra", "weight": "latency", "reconvergence_delay_ms": 0},
  "flows": [
    {"id": 1, "src": "PC1", "dst": "PC2", "pattern": "constant",
     "rate_mbps": 5, "packet_bytes": 1000, "start_s": 0, "stop_s": 60}
  ],
  "events": [
    {"type": "LINK_FAILURE",  "a": "R1", "b": "R2", "time_s": 20},
    {"type": "LINK_RESTORE",  "a": "R1", "b": "R2", "time_s": 40}
  ]
}
```

Event `type` values: `LINK_FAILURE, LINK_RESTORE, ROUTER_FAILURE, ROUTER_RESTORE, CONGESTION, TRAFFIC_BURST`. The GraphRAG layer later produces exactly this `events[]` structure.

### 4.3 Trace file (`data/runs/run_<id>_trace.csv`)

Header: `time_s,event_type,packet_id,flow_id,location,detail`. `time_s` has 6 decimals. `-` means "not applicable". `detail` is `key=value` pairs separated by `;` (no commas, no spaces). Locations use **names**: a node (`R1`), a directional channel (`R1>R2`), or an undirected link (`R1-R2`, lower id first).

| event_type | location | detail keys | Emitted when |
| --- | --- | --- | --- |
| `FLOW_START` / `FLOW_STOP` | src host | `dst` | Flow begins/ends |
| `SENT` | src host | `dst;size;type` | Packet created |
| `ENQUEUE` | channel | `qlen` (after insert) | Packet accepted into a channel FIFO |
| `TX_START` | channel | `eta` (arrival time at far node) | First bit leaves |
| `ARRIVE` | node | `from` | Packet reaches a node |
| `FORWARD` | router | `next` | Router chooses next hop |
| `DELIVERED` | dst host | `latency_ms;hops` | Packet reaches destination |
| `DROPPED` | where it died | `reason` ∈ `queue_full, link_down, node_down, no_route, ttl_expired, random_loss` | Packet lost |
| `LINK_DOWN` / `LINK_UP` / `LINK_DEGRADED` | link | `-` or `bw_factor;loss` | Link state change |
| `NODE_DOWN` / `NODE_UP` | node | `-` | Router/host state change |
| `ROUTE_UPDATE` | router or `*` | `algo;changed` | Forwarding tables recomputed |

Mapping to the Implementation Plan's examples: `HOP` becomes `ARRIVE` + `FORWARD`, and `QUEUED` becomes `ENQUEUE`. Add a golden file `tests/golden/mini_trace.csv` in Phase 0 so Python/viewer work can start before the engine exists.

### 4.4 Output files per run (`<id>` = `run_001`)

| File | Content |
| --- | --- |
| `<id>_manifest.json` | seed, algorithm, weight policy, topology path, scenario copy, git commit, wall-clock time |
| `<id>_trace.csv` | Section 4.3 |
| `<id>_flows.csv` | `flow_id,src,dst,sent,delivered,dropped,loss_pct,throughput_mbps,goodput_mbps,avg_latency_ms,min,max,p95,jitter_ms,avg_hops` |
| `<id>_links.csv` | `channel,utilization_pct,avg_queue,max_queue,tx_pkts,drops_queue,drops_loss,drops_down,downtime_s` |
| `<id>_windows.csv` | 1-second bins: `t_start,sent,delivered,dropped,loss_pct,avg_latency_ms,jitter_ms,throughput_mbps,max_queue,max_util_pct,active_failures` |
| `<id>_network.csv` | one row: `reliability_pct,avg_throughput_mbps,avg_latency_ms,total_drops,recovery_time_s,events` |

---

## Phase 0 — Foundation & Contracts (Week 1, all members)

**Goal:** an empty-but-compiling project, frozen interfaces, and agreed file formats, so three people can work in parallel without blocking each other.

### 0.1 Repository & build system

- **Contains:** root `CMakeLists.txt`, a static library target `netmind_core` (all `src/`), executable `netmind_cli` (`apps/`), test executable `netmind_tests`, build types `Debug`, `Release`, `Sanitize`.
- **How:** use `file(GLOB_RECURSE)` or explicit lists; `target_include_directories(netmind_core PUBLIC include)`; `FetchContent_Declare` for Catch2 and nlohmann/json; `enable_testing()` + `catch_discover_tests`. Keep one `CMakePresets.json` so everyone builds the same way.
- **Test:** one dummy test (`REQUIRE(1+1==2)`) runs under `ctest`.

### 0.2 Coding conventions (`docs/conventions.md`)

- **Contains:** naming rules, `#pragma once`, include order, namespace `netmind`, no `using namespace std` in headers, smart pointers only (no owning raw pointers), `const`-correctness, `[[nodiscard]]` on queries, errors: **exceptions** (`ConfigError`, `TopologyError`) for bad input, `assert`/`NETMIND_CHECK` for invariants.
- **How:** add `.clang-format`, `.editorconfig`, a PR template with a checklist (tests, formatted, headers unchanged or approved).

### 0.3 Common types (`common/Types.h`, `Units.h`, `Rng.h`)

- **Contains:** id aliases (`NodeId`, `LinkId`, `FlowId`, `PacketId`), `SimTime = double` (seconds), enums with `toString()`/`fromString()`: `NodeType{ROUTER,HOST,SWITCH}`, `LinkState{UP,DOWN,DEGRADED}`, `PacketStatus{CREATED,IN_TRANSIT,DELIVERED,DROPPED}`, `PacketType{DATA,ECHO_REQUEST,ECHO_REPLY}`, `DropReason{...}` (Section 4.3 vocabulary), `Direction{AtoB,BtoA}`.
- **Units helpers:** `msToSec`, `txTimeSec(bytes, mbps) = bytes*8 / (mbps*1e6)`, `mbps(bits, seconds)`. Unit-test them (1000 B at 100 Mbps = 0.00008 s).
- **Rng:** thin wrapper around `std::mt19937_64` with `uniform01()`, `exponential(rate)`, constructed from the scenario seed.

### 0.4 Header freeze (the key deliverable)

Write every header listed in Section 3 with **declarations only**. Reviewed together. Core signatures to agree on:

```cpp
// model/Node.h
class Node : public NetworkEntity {
public:
    Node(NodeId id, std::string name, std::string ip, NodeType type);
    NodeId getId() const override;
    const std::string& name() const;   const std::string& ip() const;
    NodeType type() const;             bool isUp() const;   void setUp(bool);
    virtual void receivePacket(Packet pkt, LinkId inLink, SimContext& ctx) = 0;
};

// simulation/SimContext.h  (abstract facade; implemented by SimulationEngine, mocked in tests)
class SimContext {
public:
    virtual ~SimContext() = default;
    virtual SimTime now() const = 0;
    virtual void transmit(Packet pkt, NodeId from, NodeId to) = 0;  // enqueue on channel
    virtual void deliver(const Packet& pkt, NodeId at) = 0;
    virtual void drop(const Packet& pkt, NodeId at, DropReason why) = 0;
    virtual Rng& rng() = 0;
    virtual const Graph& graph() const = 0;
};

// routing/RoutingAlgorithm.h
class RoutingAlgorithm {
public:
    virtual ~RoutingAlgorithm() = default;
    virtual std::vector<NodeId> computeRoute(const Graph&, NodeId src, NodeId dst,
                                             const WeightPolicy&) const = 0;
    virtual std::unordered_map<NodeId, NodeId>
            computeTable(const Graph&, NodeId src, const WeightPolicy&) const = 0;
    virtual std::string name() const = 0;
};

// simulation/NetworkEvent.h
class NetworkEvent {
public:
    virtual ~NetworkEvent() = default;
    virtual void apply(SimulationEngine& eng) = 0;
    virtual std::string describe() const = 0;
};

// simulation/SimObserver.h  (all methods have empty default bodies)
class SimObserver {
public:
    virtual ~SimObserver() = default;
    virtual void onSent(SimTime, const Packet&) {}
    virtual void onEnqueue(SimTime, const Packet&, const ChannelId&, size_t qlen) {}
    virtual void onTxStart(SimTime, const Packet&, const ChannelId&, SimTime eta) {}
    virtual void onArrive(SimTime, const Packet&, NodeId, NodeId from) {}
    virtual void onForward(SimTime, const Packet&, NodeId at, NodeId next) {}
    virtual void onDelivered(SimTime, const Packet&) {}
    virtual void onDropped(SimTime, const Packet&, const std::string& where, DropReason) {}
    virtual void onNetworkEvent(SimTime, const std::string& type, const std::string& target,
                                const std::string& detail) {}
};
```

- **How:** compile every header alone (`#include` test) to catch missing includes; add stub `.cpp` files that compile but do nothing.
- **Rule:** after the freeze, signature changes need a PR titled `[HEADER CHANGE]` approved by all.

### 0.5 Contract documents and fixtures

- Put Section 4 into `docs/contracts/` and add `tests/golden/mini_trace.csv` (about 30 hand-written lines: two packets, one queue drop, one link down) plus a matching `mini_topology.json`.
- Member C builds Python readers and the viewer against this fixture while the engine is not yet written.

### 0.6 Tooling & CI

- GitHub Actions workflow: configure, build (Release), `ctest`, build with `Sanitize`, `pytest`.
- Python: `python/requirements.txt`, `pyproject.toml` (or `setup.cfg`), `pytest` with one dummy test.

**Done when:** `cmake --build` succeeds with stubs; the dummy C++ and Python tests pass in CI; headers are merged and tagged `headers-v1`; contract docs and golden files exist.

---

## Phase 1 — Network Model (Weeks 1–2, Member A)

**Goal:** all network entities exist as OOP classes with clear responsibilities. No graph, no simulation yet.

### 1.1 `NetworkEntity` and `Node` (abstract)

- **Contains:** `NetworkEntity` with `virtual ~NetworkEntity()` and `getId()`. `Node` adds `name`, `ip`, `type`, `up` flag, and the pure virtual `receivePacket`.
- **How:** make constructors validate input (non-empty name, valid IP via 1.8). `Node` is non-copyable (derived classes are stored in `unique_ptr`).
- **Concept shown:** abstraction, pure virtual, inheritance root.

### 1.2 `Host`

- **Contains:** `gatewayId` (the router it connects to, resolved after loading), flow bookkeeping, `originate(Packet, SimContext&)`.
- **Behavior:** `receivePacket`: if `pkt.dst == id`: `ECHO_REQUEST` → build `ECHO_REPLY` (swap src/dst, copy `originTime`) and `originate` it; otherwise `ctx.deliver()`. If `pkt.dst != id`, drop (`no_route`), because hosts do not forward.
- **Test with a mock `SimContext`:** assert that delivery and echo reply happen as expected.

### 1.3 `ForwardingTable` and `Router`

- **`ForwardingTable`:** wraps `unordered_map<NodeId, Entry{nextHop, cost, source}>`, where `source ∈ {DYNAMIC, STATIC}`. Methods: `lookup(dst)`, `install(dst, nextHop, cost)`, `clearDynamic()`, `addStatic(...)`, `toSortedVector()` (deterministic printing for `show route`). Static entries take precedence over dynamic ones.
- **`Router`:** owns a `ForwardingTable`. `receivePacket`: record arrival; if `ttl <= 1` → drop `ttl_expired`; decrement ttl; if `dst == id` consume; else `lookup(dst)`; missing → drop `no_route`; otherwise `ctx.transmit(pkt, id, nextHop)`.
- **Concept shown:** polymorphism (`Host` and `Router` react differently to the same call), encapsulation of the table.

### 1.4 `Switch` (Tier 3, \~30 lines)

- Floods every packet to all neighbors except the arriving link, with a duplicate guard (`unordered_set<PacketId>` of recently seen ids) to prevent loops. Skip when time is short.

### 1.5 `Link`

- **Contains (private):** `id`, `nodeA`, `nodeB`, `bandwidthMbps`, `latencyMs`, `lossRate`, `queueCapacity`, `state`, `epoch` (incremented on every DOWN transition; used to void in-flight packets), `degradeFactor`.
- **Methods:** `other(nodeId)`, `connects(a,b)`, `effectiveBandwidth()` (= bandwidth × factor when DEGRADED), `isUsable()` (state ≠ DOWN and both end nodes up), `getWeight(const WeightPolicy&)` (see 3.2), `setState()`.
- **How:** validate `bandwidth > 0`, `0 ≤ lossRate ≤ 1`, `latency ≥ 0`.
- **Concept shown:** encapsulation (raw fields hidden; routing sees only `getWeight`).

### 1.6 `Packet`

- **Contains:** `id, srcId, dstId, flowId, sizeBytes, type, ttl, timestampSent, originTime, timestampDelivered, status, pathTaken (vector<NodeId>), dropReason`.
- **Constructors (overloading):** fresh packet from a flow; `Packet::cloneForReply(const Packet&)`; `Packet::cloneForRetransmit(...)` (optional).
- **Operators:** `operator<<` for debug printing; `operator<` only if needed by containers.
- **Rule:** `pathTaken` is appended in `Router::receivePacket` and `Host::originate`, nowhere else.

### 1.7 `Flow` and `TrafficPattern`

- **`TrafficPattern` (abstract):** `virtual SimTime nextInterval(Rng&)`.
  - `ConstantRate(rateMbps, packetBytes)` → interval = `bytes*8 / (rate*1e6)`.
  - `BurstyPattern(onDuration, offDuration, onRate)` → ON/OFF cycle.
  - `PoissonPattern(meanRate)` (Tier 3) → `rng.exponential(1/meanInterval)`.
- **`Flow`:** `flowId, srcHostId, dstHostId, packetBytes, startTime, stopTime, unique_ptr<TrafficPattern>, nextPacketSeq`. `Packet makePacket(SimTime now)`.
- **Concept shown:** inheritance + runtime polymorphism for a second family (patterns), composition (`Flow` has-a pattern).

### 1.8 IP helpers (`common/Ip.h`)

- `parseIpv4(string) → uint32`, `formatIpv4`, `isValidIpv4`. Optional `sameSubnet(a, b, prefixLen)` with bit masking, which gives a small bit-manipulation demonstration.

### 1.9 Unit tests (Catch2)

- Constructor validation, getters, `Link::other`, weight values, `ForwardingTable` precedence and sorted print, each `TrafficPattern` interval (constant: 1000 B at 8 Mbps → 1 ms), Host echo behavior via mock context.

**Done when:** a test builds 2 hosts, 1 router and 2 links directly in code and prints them; all model tests pass; no model header includes anything from `graph/`, `routing/` or `simulation/` except the forward-declared `SimContext`.

---

## Phase 2 — Graph Engine & Topology Loader (Weeks 3–4, Member A)

**Goal:** the network exists as a graph you can query, traverse, validate and load from JSON.

### 2.1 `Graph` core

- **Contains:** `unordered_map<NodeId, unique_ptr<Node>> nodes`, `unordered_map<LinkId, unique_ptr<Link>> links`, `unordered_map<NodeId, vector<Link*>> adj`, `unordered_map<string, NodeId> nameIndex`.
- **API:** `addNode(unique_ptr<Node>)`, `addLink(unique_ptr<Link>)`, `removeLink(LinkId)`, `getNode(id)`, `findByName(name)`, `getNeighbors(id)` (all links), `getActiveNeighbors(id)` (usable links to up nodes), `linkBetween(a,b)`, `nodeIds()` (sorted), `numNodes/numLinks`.
- **How:** `addLink` rejects unknown endpoints, self-loops and duplicate parallel links (or allows them with distinct ids; pick one and document). `removeLink` erases from both adjacency vectors (swap-and-pop).
- **Concept shown:** adjacency list, hash maps (O(1) lookup), composition and ownership.

### 2.2 Traversals: BFS and DFS

- `bfsPath(src, dst, out)`: `std::queue`, `parent` map, stop on reaching `dst`, rebuild path backward and reverse. Considers **active** links only. Neighbor order is sorted by node id so the result is deterministic.
- `dfsPath(src, dst, out)`: iterative with an explicit `std::stack` (avoids recursion limits), `visited` set, path via parent map.
- Complexity noted in code comments: O(V+E).

### 2.3 Connectivity, components, cycles

- `UnionFind` class (`parent[]`, `rank[]`, path compression, union by rank). `connectedComponents()` returns groups of node ids. `isConnected()` is true for one component.
- `hasCycle()` via DFS (undirected: track parent) for topology validation. Rings are valid, so this is informational, not an error.
- `reachableFrom(src)` (BFS reachability), used after failures to report which hosts are cut off.
- **Concept shown:** Union-Find, DFS, BFS.

### 2.4 `TopologyLoader`

- **Contains:** `static unique_ptr<Graph> loadFromFile(path)` and `loadFromJson(const json&)`.
- **How:** parse with nlohmann/json → create nodes through a small **factory** (`NodeType` → `make_unique<Router|Host|Switch>`) → create links → resolve each `Host`'s gateway (its single neighbor router).
- **Validation (collect all errors into one `TopologyError` message):** duplicate ids/names/IPs; link to unknown node; invalid IP; bandwidth ≤ 0; loss outside \[0,1\]; Host with ≠ 1 link; graph not connected (warning, not error).
- **Concept shown:** factory pattern, exceptions, RAII.

### 2.5 Sample topologies (`config/topologies/`)

| File | Shape | Purpose | Expected check |
| --- | --- | --- | --- |
| `small4.json` | PC1–R1–R2–PC2 (2/10/2 ms) | Smoke tests, ping demo | BFS = PC1,R1,R2,PC2 |
| `ring_backup.json` | R1–R2–R3–R4 ring, PC1 on R1, PC2 on R3. R1–R2–R3 is short/high-bandwidth; R1–R4–R3 is longer | Failure and reroute | Before failure uses R2; after failing R1–R2 uses R4 |
| `diamond_weighted.json` | Two paths PC1→PC2: **fewest hops but slow** (2 hops, 40 ms each) vs **more hops but fast** (4 hops, 2 ms each) | BFS vs Dijkstra difference | BFS picks the 2-hop path, Dijkstra the 4-hop path |
| `dumbbell.json` | 2 hosts → R1 ═(bottleneck 50 Mbps)═ R2 → 2 hosts | Congestion test | Offered load > 50 Mbps causes drops |
| `medium10.json` | 10 nodes, mixed bandwidths, 2 cycles | Demo and algorithm comparison | Connected; unique shortest paths |

### 2.6 Tests

- Add/remove link updates both adjacency lists; BFS/DFS on each sample file; disconnected graph detection after `removeLink`; loader rejects each invalid file in `tests/golden/bad_*.json` with a clear message; `Graph` destruction has no leaks under the Sanitize build.

**Done when:** `small4.json` loads and BFS returns `PC1 → R1 → R2 → PC2`; the diamond file shows different BFS and DFS results; loader error tests pass.

---

## Phase 3 — Routing Engine (Weeks 3–6, Member B)

**Goal:** swappable routing strategies that fill each router's forwarding table. Phase 3 and Phase 4 run in parallel: routing tests use only `Graph`; simulation tests use a hand-built table.

### 3.1 `WeightPolicy` (strategy, separate from the algorithm)

- **Contains:** abstract `WeightPolicy { virtual double weight(const Link&) const = 0; }` with:
  - `HopCountWeight` → 1.
  - `LatencyWeight` → `latencyMs`.
  - `LatencyCongestionWeight(alpha)` → `latencyMs + alpha * (currentQueueDelayMs)`, where the queue length is read from a lightweight `LinkLoadProvider` interface (implemented by the engine, returns 0 in offline tests). Always ≥ 0 (decision D9).
- **Why separate:** "BFS = fewest hops, Dijkstra = least latency" is then a combination you can change, not hard-coded behavior, and the congestion-aware routing experiment needs no new algorithm.

### 3.2 `RoutingAlgorithm` interface (frozen in Phase 0)

- `computeRoute(g, src, dst, policy)` returns a node path, or empty if unreachable. `computeTable(g, src, policy)` returns `{dest → nextHop}` for every reachable destination. `name()` for logs and CSVs.
- **All algorithms must:** ignore DOWN links/nodes (use `getActiveNeighbors`), break ties by smaller node id, never mutate the graph.

### 3.3 `BFSRouting` (fewest hops; ignores weights)

- Reuse the Graph BFS but record the parent tree from `src`; next hop for destination *d* is found by walking parents from *d* back to the node whose parent is `src`. Compute all next hops in one pass: store `firstHop[v] = (parent[v]==src) ? v : firstHop[parent[v]]`. O(V+E).

### 3.4 `DijkstraRouting`

- `priority_queue<pair<double,NodeId>, vector<...>, greater<>>` with **lazy deletion** (skip popped entries whose distance is stale). `unordered_map<NodeId,double> dist`, `parent`. Relax with `policy.weight(link)`. Same `firstHop` trick for `computeTable`. O((V+E) log V).
- Optional: implement the shared `MinHeap<T>` template (Section 9 of the blueprint) and use it here and in `EventQueue` to demonstrate generic programming.
- **Test:** hand-computed distances on `diamond_weighted.json`; unreachable nodes absent from the table.

### 3.5 `BellmanFordRouting` ("RIP-like")

- **Centralized form (required):** edge list (each undirected link as two directed edges); initialize `dist[src]=0`; relax all edges up to V−1 passes with **early exit** when a pass changes nothing; one extra pass to detect negative cycles (throws; only reachable in tests with synthetic negative weights). O(VE).
- **Distance-vector form (Tier 3, makes the comparison interesting):** each router keeps a vector `{dest → (cost, nextHop)}`; run synchronous rounds in which every router shares its vector with neighbors until no change. Report `convergenceRounds`; `RoutingManager` can convert rounds × `updateInterval` into a reconvergence delay.

### 3.6 `DFSRouting` (Tier 3, baseline only)

- Returns *a* path, not the best. Use it in the `compare` table to show why DFS is unsuitable for routing (path length and latency much worse). Mark clearly in the report as a teaching baseline.

### 3.7 `RoutingManager`

- **Contains:** `unique_ptr<RoutingAlgorithm> algo`, `unique_ptr<WeightPolicy> policy`, `reconvergenceDelay`.
- **API:** `setAlgorithm(name)`, `setPolicy(name)`, `recomputeAll(Graph&)` (for every **up router**, compute table, call `router.table().clearDynamic()`, then `install`), `recomputeFor(routerId)`, `explainRoute(src, dst)` (node path with per-hop cost, used by `traceroute`/`show route`), `addStaticRoute(router, dst, nextHop)`.
- **Hosts:** a host's "table" is its default gateway. Only routers hold full tables.
- **How:** iterate routers in sorted-id order. Return a `RoutingChangeSummary` (how many entries changed), logged as `ROUTE_UPDATE`.

### 3.8 `RoutingFactory`

- `makeAlgorithm("bfs"|"dfs"|"dijkstra"|"bellman-ford")` and `makePolicy("hops"|"latency"|"latency+congestion")`. Used by the CLI (`set algo`) and `ScenarioManager`.

### 3.9 Tests and benchmarks

- Correctness: on 100 random connected graphs (seeded), Dijkstra distance == Bellman-Ford distance for all pairs; BFS hop count ≤ every other algorithm's hop count; table next hops lead to destination without loops (walk the table).
- Special cases: unreachable destination, DOWN link ignored, equal-cost paths (tie-break by id), single node.
- Benchmark harness (`tests/bench/`, `std::chrono`) over graph sizes 100–5000, results saved to `data/bench.csv` for the Phase 9 complexity table.

**Done when:** on `diamond_weighted.json` BFS and Dijkstra produce different, correct routes and a test proves it; all algorithms agree on distances on random graphs; `RoutingManager::recomputeAll` fills tables for `ring_backup.json` and routes change after `removeLink`.

---

## Phase 4 — Simulation Engine (Weeks 2–7, Member B; core skeleton can start in Week 2)

**Goal:** an event-driven simulator where packets travel hop by hop with queueing, transmission and propagation delay, random loss, failures and adaptive rerouting.

### 4.1 `EventQueue` and simulation clock

- **Contains:** `struct ScheduledEvent { SimTime time; uint64_t seq; unique_ptr<NetworkEvent> ev; }` with `operator>` comparing `(time, seq)`. `std::priority_queue` wrapped in `EventQueue { push, pop, empty, nextTime }`.
- **How:** `seq` is a global counter incremented per `schedule`. This gives deterministic FIFO ordering for equal times (D6). `unique_ptr` inside a priority queue needs a mutable access trick: store events in a `vector<unique_ptr>` pool and put indices in the heap, or use `std::shared_ptr` for simplicity.
- **API on `SimulationEngine`:** `schedule(unique_ptr<NetworkEvent>, SimTime at)`, `run(untilTime)`, `step()` (pop and execute exactly one event, returns its description; this is the Packet Tracer "simulation mode"), `now()`, `reset()`.

### 4.2 `SimContext` implementation inside the engine

- `SimulationEngine` implements `SimContext`: `transmit`, `deliver`, `drop`, `rng`, `graph`. It also holds `vector<SimObserver*>` with `addObserver()`. Every state change calls the matching observer hook (`notifySent`, `notifyDrop`, …). That is how `TraceLogger` and `MetricsCollector` see everything with zero engine-side knowledge of them.

### 4.3 `Channel` (per-direction link state)

- **Contains:** `ChannelId {linkId, dir}`, `deque<Packet> queue`, `bool busy`, `size_t capacity`, running stats (time-weighted queue length integral, `bitsTransmitted`, `maxQueue`, drop counts by reason).
- **Storage:** `unordered_map<ChannelId, Channel>` created lazily, or a vector indexed by `linkId*2 + dir`.
- **Behavior (as small methods the events call):**
  - `bool tryEnqueue(Packet&)` → false if `queue.size() >= capacity` (tail-drop = congestion model).
  - `startTransmission()` → `txTime = bytes*8 / (effectiveBandwidth*1e6)`; schedule `TransmitCompleteEvent` at `now + txTime`.
  - `onTransmitComplete()` → pop; apply random loss (`rng.uniform01() < lossRate`); else schedule `PacketArriveEvent` at `now + latency`; if queue non-empty, `startTransmission()` again.

### 4.4 Packet event handlers (the heart of the engine)

| Event class | Fires when | What it does |
| --- | --- | --- |
| `GeneratePacketEvent(flow)` | Flow's next-packet time | Create packet, `onSent`, hand to source `Host::originate`, schedule the next `GeneratePacketEvent` at `now + pattern.nextInterval()` (until `stopTime`) |
| `TransmitCompleteEvent(channel)` | Last bit leaves | As in 4.3 |
| `PacketArriveEvent(node, pkt, linkId, epoch)` | Packet reaches far end | If `link.epoch != epoch` or node down → drop (`link_down`/`node_down`); else `onArrive`, `node.receivePacket(...)` |

**Lifecycle of one packet (reference pseudo-code)**

```
Host::originate(pkt): pkt.path += self; ctx.transmit(pkt, self, gateway)
Engine::transmit(pkt, from, to):
    link = graph.linkBetween(from, to)
    if !link or !link.isUsable(): drop(LINK_DOWN)           // includes node down
    ch = channel(link, dirOf(from))
    if !ch.tryEnqueue(pkt): drop(QUEUE_FULL); return
    notifyEnqueue(qlen); if !ch.busy: ch.startTransmission()
TransmitComplete: pkt = pop; if loss: drop(RANDOM_LOSS)
    else schedule PacketArrive(to, pkt, now + latency, link.epoch)
Router::receivePacket: ttl check → table lookup → ctx.transmit(...)   // else drop no_route
Host::receivePacket(dst==self): deliver, or reply to ECHO_REQUEST
```

**Per-hop time = queue wait + `bytes*8/bandwidth` + `latency`.** Hand-check example: a 1000-byte packet on an idle 100 Mbps, 2 ms link takes 0.08 ms + 2 ms = 2.08 ms. Make this a unit test.

### 4.5 Traffic scheduling

- `Engine::addFlow(Flow)`: schedules `FlowStartEvent` at `startTime` (emits `FLOW_START`, first `GeneratePacketEvent`) and `FlowStopEvent`. Flow ids unique. Packet ids come from one engine counter.
- `TrafficBurstEvent(src, dst, packets or duration, rate)`: creates a temporary `Flow` with `BurstyPattern`.

### 4.6 `NetworkEvent` subclasses (`apply(engine)`; polymorphic dispatch)

| Class | Effect | Trace |
| --- | --- | --- |
| `LinkFailureEvent(linkId)` | `state=DOWN`, `epoch++`, **flush both channels** (every queued packet dropped `link_down`), request route recompute | `LINK_DOWN`, `DROPPED…` |
| `LinkRestoreEvent` | `state=UP`, request recompute | `LINK_UP` |
| `RouterFailureEvent(nodeId)` | `setUp(false)`, treat all attached links unusable, flush their channels, recompute | `NODE_DOWN` |
| `RouterRestoreEvent` | `setUp(true)`, recompute | `NODE_UP` |
| `CongestionEvent(linkId, bwFactor, extraLoss, duration)` | `state=DEGRADED`, scales effective bandwidth (e.g. 0.2) and/or raises loss; schedules automatic end | `LINK_DEGRADED`, then `LINK_UP` |
| `TrafficBurstEvent` | See 4.5 | `FLOW_START` |
| `RecomputeRoutesEvent` | Calls `RoutingManager::recomputeAll` | `ROUTE_UPDATE` |

### 4.7 Adaptive routing (Blueprint 12.1)

`Network Event → state update → route invalidation → RoutingManager → alternative path → simulation continues`.

- **How:** a failure event ends with `engine.requestRecompute()`. If `reconvergenceDelay == 0` call `recomputeAll` immediately. Otherwise schedule `RecomputeRoutesEvent` at `now + delay`. During the window routers still hold stale routes, so packets sent toward a dead link are dropped `link_down`. That window is the **convergence cost** you will measure.
- **Optional:** periodic recompute for the `LatencyCongestionWeight` policy (every N seconds) to demonstrate congestion-aware routing.
- **Loop protection:** TTL handles transient loops while tables are inconsistent.

### 4.8 `ScenarioManager` and `EventFactory`

- `ScenarioManager::load(scenario.json)`: load topology via `TopologyLoader`, create algorithm/policy via `RoutingFactory`, seed the engine, add flows and events, run initial `recomputeAll`, attach observers it is given.
- `EventFactory::create(json)` maps `"LINK_FAILURE"` etc. to subclasses, resolving names through `Graph::findByName`. Throws `ConfigError` for unknown target/time outside `[0, duration]`. This is the same validation that GraphRAG-generated events pass through later.
- `ScenarioManager::cloneWith(algorithmName)` rebuilds an identical scenario (same seed) with a different algorithm. `compare` depends on it.

### 4.9 Tests

- **Unit:** event ordering with equal timestamps; channel tail-drop at capacity; per-hop time formula; loss probability over 100k packets ≈ configured (±1 %); epoch invalidates in-flight packets.
- **Integration:** `small4` constant flow delivers every packet with latency ≈ sum of hop times; `ring_backup` link failure at t=5 s: packets before and after delivered, path changes, drops only in the reroute window; no backup path ⇒ drops until restore.
- **Determinism:** same scenario + seed ⇒ byte-identical trace file (compare hashes in CI).
- **Hand-checked congestion:** `dumbbell` with two 30 Mbps flows through a 50 Mbps bottleneck should deliver ≈ 50 Mbps total, loss ≈ (60−50)/60 ≈ 16.7 %.

**Done when:** a flow PC1→PC2 delivers packets with sensible delays; failing the middle link at 5 s makes packets reroute (or drop if no backup); `step()` advances exactly one event; the determinism test passes.

---

## Phase 5 — Metrics & Trace Logging (Weeks 5–8, Member C; starts early using fixtures)

**Goal:** every simulation run produces a trace plus metric files that Python, the viewer and the report all consume. Member C can start in Week 5 by feeding **fake observer events** (replaying `mini_trace.csv`) into the collector before the engine is finished.

### 5.1 `TraceLogger` (implements `SimObserver`)

- **Contains:** an `ofstream`, a buffered writer (flush every \~4096 lines and at destruction), `nameOf(NodeId)` and `channelName(ChannelId)` helpers.
- **How:** one method per observer hook writes exactly one line in the Section 4.3 format (`%.6f` for time). Writes events in the order received, which is time order. Include network events (`LINK_DOWN`, `ROUTE_UPDATE`, …).
- **Tests:** compare output against `tests/golden/mini_trace.csv`; every line has 6 columns; every `DROPPED` has a `reason` from the vocabulary.

### 5.2 `MetricsCollector` (implements `SimObserver`)

Keeps lightweight records during the run and computes aggregates at the end.

- **Per-packet table:** `unordered_map<PacketId, PacketRecord{flowId, sent, delivered, hops, status, reason}>`. Packets still in flight at the end are `UNFINISHED` and **excluded from loss %**.
- **Per-flow aggregator:** counts, bytes, latency vector, last-latency (for jitter). Delivered latencies kept in a `vector<double>` for min/max/avg/p95.
- **Per-channel aggregator:** bits transmitted, **time-weighted queue length** (maintain `lastChangeTime`, `currentQueue`; on each enqueue/dequeue add `currentQueue × Δt` to the integral), max queue, drops by reason, downtime accumulated between `LINK_DOWN` and `LINK_UP`.
- **Window aggregator:** a vector of 1-second bins (configurable) filled on each hook, so `windows.csv` exists for ML and the viewer.
- **Network aggregator:** reliability, totals, recovery time (5.4).

### 5.3 Metric definitions (put this table in the report too)

| Metric | Formula |
| --- | --- |
| One-way latency | `deliveredTime − sentTime` per packet; flow value = mean (also min/max/p95) |
| RTT (ping) | `reply.deliveredTime − request.originTime` |
| Jitter | Mean of \` |
| Packet loss % | `dropped / (delivered + dropped) × 100` (excludes unfinished) |
| Throughput | `deliveredBits / measurementWindow` (flow active period) |
| Goodput | Same, counting only unique DATA payload (≈ throughput here; keep for TCP future work) |
| Link utilization | `bitsTransmitted / (bandwidth × T) × 100` per channel |
| Avg queue length | `∫ q(t) dt / T` |
| Hop count | `len(pathTaken) − 1` of delivered packets |
| Reliability | `delivered / (delivered + dropped)` across the network |
| Recovery time | `first DELIVERED after LINK_DOWN on a flow that was interrupted − LINK_DOWN time` |

### 5.4 Convergence and failure metrics

- For each failure event, record: packets dropped in `[failure, failure + window]`, time until delivery resumes (recovery time), and latency before vs after (path change cost). These feed the algorithm comparison and GraphRAG explanations.

### 5.5 Reporting and sorting (DS&A)

- `Report` class: `topLinksByUtilization(k)`, `topLinksByDrops(k)`, `flowsByLatency()`. Implement with `std::partial_sort` and a custom comparator, or a small hand-written heap-based top-k to show a priority-queue application. Also provide a stable sort by `(metric desc, name asc)` for deterministic output.

### 5.6 Exporters

- `exportFlowsCsv`, `exportLinksCsv`, `exportWindowsCsv`, `exportNetworkCsv`, `exportManifest` (Section 4.4 schemas). One header row. Floats with 6 decimals. Wrap everything in `Exporters::exportAll(outDir, runId)`.
- Run ids auto-increment (`run_001`, …) by scanning `data/runs/`.

### 5.7 Validation

- Replay a hand-crafted event sequence (10 packets, known latencies 10,12,10,14…) and assert latency, jitter, loss% match hand calculations.
- Cross-check: sum of `windows.csv` deliveries equals sum of `flows.csv` deliveries; `SENT == DELIVERED + DROPPED + UNFINISHED`.
- Trace and metrics files open in Excel and `pd.read_csv` without errors.

**Done when:** one simulation run writes `data/runs/run_001_{manifest.json,trace.csv,flows.csv,links.csv,windows.csv,network.csv}`, and the cross-check tests pass.

---

## Phase 5B — Packet Tracer-style CLI (Weeks 7–8, Members A + B)

**Goal:** the full demo can be run from a text prompt without touching code. The CLI contains no simulation logic; it only calls existing classes.

### 5B.1 Command framework

- **Contains:** abstract `Command { name(), help(), execute(Session&, args) }`, a `CommandRegistry` (`unordered_map<string, unique_ptr<Command>>`), `Session` (current `Graph`, `ScenarioManager`/`Engine`, `RoutingManager`, observers, output dir), and `CommandLineInterface` (read line → tokenize → lookup by first word, optionally first two words such as `show route` → execute).
- **How:** whitespace tokenizer that supports quoted names; consistent error messages (`% Unknown command`, `% Node 'R9' not found`); `help` lists commands from the registry; `exit`.
- **Concept shown:** Command pattern, polymorphism, registry/factory.

### 5B.2 Commands

| Command | Behavior and output |
| --- | --- |
| `load <file>` | Loads topology or scenario; prints node/link counts |
| `show topology` | Table: node, type, IP, neighbors; link list with bandwidth/latency/loss/state |
| `show route <router>` | Forwarding table sorted by destination: `dest next-hop cost [S/D]` |
| `show queue <A>-<B>` | Current queue length, max, drops per direction |
| `show metrics [flow]` | Latest aggregates (calls `Report`) |
| \`set algo \<bfs | dfs |
| \`set weight \<hops | latency |
| `set seed <n>` | Re-seeds RNG (resets run) |
| `route add <router> <dst> via <next>` | Static route |
| `add flow <src> <dst> <mbps> <bytes> [start stop] [bursty]` | Registers a flow |
| `ping <A> <B> [count]` | Sends ECHO_REQUESTs, prints per-reply RTT, then `sent/received/loss, min/avg/max RTT` |
| `traceroute <A> <B>` | Hop list with cumulative latency (from `pathTaken`/`explainRoute`) |
| `fail link <A> <B> at <t>` / `fail router <R> at <t>` | Schedules failure |
| `restore link <A> <B> at <t>` / `restore router <R> at <t>` | Schedules recovery |
| `congest link <A> <B> factor <f> at <t> for <d>` | Schedules degradation |
| `burst <src> <dst> <mbps> at <t> for <d>` | Schedules burst |
| `run <seconds>` | Runs until that time |
| `step [n]` | Executes *n* events, printing each (`[t=5.002000] R1 forward pkt 9 → R2`) |
| `compare <A> <B>` | Runs the active scenario once per algorithm with identical seed and prints a table |
| `export [dir]` | Writes the run files |
| `reset` | Rebuilds engine, keeps topology |

### 5B.3 `ping` and `traceroute` implementation

- **Ping:** creates a one-shot `Flow` of N `ECHO_REQUEST`s at 1 s spacing from the source host; the destination Host auto-replies (Phase 1.2); RTT is read at reply delivery from `originTime`. Because the engine is event-driven, the command runs events until all replies arrive or a timeout of 2 s per packet passes (`Request timed out`).
- **Traceroute (Level 1, required):** send one probe, read `pathTaken` and per-hop arrival times from the trace, print `1 R1 2.08 ms` per hop.
- **Traceroute (Level 2, Tier 3):** TTL-based probes (ttl = 1, 2, 3…); a router that drops with `ttl_expired` sends back a reply identifying itself. This replicates the real tool.

### 5B.4 `compare`

- For each algorithm: `ScenarioManager::cloneWith(name)` → run → collect `FlowMetrics`/`NetworkMetrics` → table:

```
Algorithm      Hops  Avg latency  Loss %  Throughput  Recovery(s)  Route computes  Compute µs
bfs            2     84.1 ms      0.4     4.96 Mbps   0.0          1               12
dijkstra       4     9.3 ms       0.0     5.00 Mbps   0.0          1               31
bellman-ford   4     9.3 ms       0.0     5.00 Mbps   0.0          1               88
```

Wall-clock compute time is measured around `recomputeAll`.

### 5B.5 Non-interactive modes

- `netmind_cli --scenario s.json --out data/runs --quiet` (used by Python dataset generator), `netmind_cli --script demo.txt` (runs commands from a file; used for the demo), `--json-summary` prints a one-line JSON of key metrics to stdout.

**Done when:** you can run the whole demo (load → show route → ping → fail link → run → compare → export) from the prompt, and a `--script` file reproduces it. CLI tests feed scripted input and compare to expected output files.

---

## Phase 6 — ML Module (Python, Weeks 9–10, Member C)

**Goal:** turn simulation output into insight: detect anomalies, forecast metrics, and (Tier 3) compare actual metrics against a learned baseline.

### 6.1 Data layer (`python/common/io.py`)

- Readers for `trace`, `flows`, `links`, `windows`, `network`, `manifest` returning typed DataFrames; schema checks that fail loudly if columns change; helper to join all runs into one table with `run_id` and manifest fields.

### 6.2 Dataset generator (`python/ml/generate_dataset.py`)

- **What:** programmatically builds scenario JSONs and calls `netmind_cli --scenario … --quiet` through `subprocess` (parallelize with `concurrent.futures`).
- **Sweep grid:** topologies (`medium10`, `ring_backup`, `dumbbell`, plus randomly generated ones) × algorithms (3) × offered load (10–120 % of bottleneck) × event pattern (none, link failure at random time, router failure, congestion, burst) × seeds (5). Target ≈ 300–500 runs (minutes of compute).
- **Ground truth:** each run's manifest stores injected events and times, so a window can be labeled `anomalous` if it overlaps `[event, event + recovery]`.
- **Output:** `data/dataset/windows_all.csv`, `runs_all.csv`.

### 6.3 Feature engineering (`features.py`)

- Per-window features: mean/p95 latency, jitter, loss %, throughput, max queue, max utilization, plus lagged and rolling-mean deltas (window t vs t−1, t−3). Normalize with `StandardScaler` fitted on training data only.

### 6.4 Anomaly detection (`anomaly.py`) — Tier 2

- **Model:** `IsolationForest` trained on **normal runs** (no events); `contamination` tuned or replaced by a percentile threshold on the anomaly score.
- **Evaluation:** on held-out runs with injected events compute precision/recall/F1 of window labels; plot latency over time with flagged windows shaded and the true event time marked.
- **Compare:** a simple z-score baseline (|x − μ| > 3σ) so the ML claim is justified.

### 6.5 Forecasting (`forecast.py`) — Tier 2

- **Approach A:** lag-feature regression (Ridge or RandomForest) predicting next-window latency/throughput. **Approach B:** ARIMA (`statsmodels`) per run. Evaluate with MAE/RMSE on the last 20 % of each series, against a naive "last value" baseline.

### 6.6 Expected-metric baseline (`baseline.py`, Blueprint 12.3) — Tier 3

- **Inputs (per run/flow):** topology statistics (nodes, links, avg degree), path hop count, bottleneck bandwidth, sum of path latency, offered load / bottleneck, algorithm (one-hot), event flags.
- **Targets:** mean latency, throughput, loss %. **Model:** `RandomForestRegressor`. **Split by run/topology (`GroupKFold`)** to avoid leakage.
- **Deviation:** `(actual − predicted) / predicted`, flagged when beyond `k ×` the residual standard deviation. Label results **"expected/baseline"**, never "optimal".

### 6.7 Plots and artifacts

- Save to `data/plots/` (latency timeline with anomaly shading, predicted-vs-actual scatter, algorithm comparison bars, forecast vs truth). Save models with `joblib` under `data/models/`. A notebook `notebooks/ml_demo.ipynb` runs the story end to end.
- **Tests (`pytest`):** readers on golden files, feature shapes, anomaly detector flags the injected-failure window on a fixed seed.

**Done when:** the notebook flags the window of an injected failure as anomalous with F1 clearly above the z-score baseline, and plots are generated.

---

## Phase 7 — GraphRAG Layer (Python, Weeks 9–10, Member C with A/B support)

**Goal:** a knowledge graph built from topology, events and metrics that answers questions with **grounded** facts, and (Tier 3) turns natural language into structured events. Start deterministic, add an LLM only on top.

### 7.1 Knowledge graph builder (`graphrag/build_kg.py`)

- **Library:** `networkx.MultiDiGraph`.
- **Node types and attributes:** `Device(name,type,ip)`, `Link(name,bw,latency,loss)`, `Run(id,algo,seed)`, `Flow(id,src,dst)`, `Event(type,target,time)`, `Window(t_start,metrics…)`, `Anomaly(window,score)`.
- **Edges:** `CONNECTED_TO` (Device–Link–Device), `RAN_IN` (Flow/Event → Run), `TARGETS` (Event → Device/Link), `ROUTED_VIA` (Flow → Device, before and after each event, from `ROUTE_UPDATE`/`pathTaken`), `AFFECTED` (Event → Flow, with deltas), `FLAGGED` (Anomaly → Window).
- **Source:** manifest + trace + flows/links/windows files; build once per run and cache as `graph.pkl` or JSON.

### 7.2 Causal fact extractor (`graphrag/facts.py`)

- Deterministic rules that precompute explanation facts and attach them to events:
  - *Failure impact:* drops with `reason=link_down` within `[t, t+recovery]`; flows whose route changed; latency before vs after (`+8 ms`); recovery time.
  - *Congestion:* channels whose queue reached capacity and the time they did; flows crossing them; drop counts.
  - *Anomaly explanation:* nearest preceding event within the window.
- Every fact gets an id (`F12`) and text so answers can cite it.

### 7.3 Retrieval and answering (`graphrag/query.py`)

- **Step 1, intent routing:** rule-based (keywords + regex) for: *most congested link*, *why did packets drop at t*, *why did latency change at t*, *which algorithm performed best*, *what path does X use*. Unknown → fallback keyword search over facts.
- **Step 2, retrieval:** graph queries (e.g. nodes of type `Event` with `time` within ±2 s of *t*, neighbors via `TARGETS`, `AFFECTED`).
- **Step 3, answer:** fill a template from retrieved facts: *"Latency rose at t=20 s because link R1–R2 failed; PC1→PC2 rerouted via R4 (+8 ms); 14 packets were dropped during the 0.2 s reconvergence window."* Cite fact ids.
- **Step 4 (optional LLM):** pass only retrieved facts plus the question; instruct "answer only from these facts, say 'not enough data' otherwise". Keep the template path as fallback and for tests.
- **Tests:** at least 3 sample questions with expected answers on a fixed run (the final checklist requires this).

### 7.4 Natural-language event creation (Tier 3, Blueprint 5/12.2)

- **Flow (optionally as a LangGraph graph):** `parse` (LLM or regex → candidate type/target/time) → `resolve` (match `R3` or "the link between R2 and R4" to real names using the KG) → `validate` (target exists, time ≤ duration, type known) → `confirm` (show the structured spec to the user) → `emit` (append to scenario `events[]` or print a CLI command).
- **Output:** `{"type":"ROUTER_FAILURE","target":"R3","time_s":20}` and the equivalent `fail router R3 at 20`.
- **Rule:** GraphRAG never simulates anything. The C++ `EventFactory` validates and schedules, so a hallucinated target is rejected there as well.

**Done when:** "which link was most congested?" and "why did packets drop at 20 s?" return correct, fact-cited answers on a fixed run; (Tier 3) "fail R3 at 20 seconds" produces a valid event that the C++ engine accepts.

---

## Phase 8 — Visualization (Weeks 9–11, Member A leads, Member C supplies data panels)

**Goal:** a replay viewer that reads files only; **no C++ change**.

### 8.1 Option A: HTML + canvas replay (recommended)

Files in `viz/js/`:

| Module | Content | How |
| --- | --- | --- |
| `parser.js` | Parse `topology.json`, `trace.csv`, `windows.csv` | Use `FileReader` / drag-and-drop (avoids `file://` fetch limits), or serve with `python -m http.server`. Split trace lines, convert times to numbers, index by packet |
| `layout.js` | Node positions | Use `pos` from JSON; else circular layout, or a simple force-directed pass for 10–30 nodes |
| `model.js` | Replay data model | Build **packet journeys**: for each packet a list of segments `{channel, tStart (TX_START), tEnd (eta)}`; plus **intervals** for link down (`LINK_DOWN`→`LINK_UP`) and drop markers `{t, location, reason}` |
| `renderer.js` | Canvas drawing | Devices as icons/labels (router, host); links as lines. At playback time *t*: draw each packet dot at the interpolated position along its active segment; queued packets shown as a count badge at the sender; drops as a brief ✕ in red; DOWN links red; link color from windowed utilization (green → orange → red) |
| `controls.js` | Play/pause, speed (0.1×–10×), time slider, **step to next event**, jump to next failure/drop, click a node to show its forwarding table at that time from `ROUTE_UPDATE` | `requestAnimationFrame` loop advancing `t`; binary search over sorted segment lists to find active packets quickly |
| `charts.js` | Metrics panel | Latency/throughput/loss vs time from `windows.csv`, with failure times as vertical lines; table of `flows.csv` and top congested links. Use Chart.js from a pinned CDN or vendored file |
| `index.html` | Layout | Topology canvas left, metric panel right, event log under the slider |

- **Performance:** pre-sort journeys by start time; only draw journeys with `tStart ≤ t ≤ tEnd` (maintain an active window index); cap at \~2,000 simultaneous dots by sampling.

### 8.2 Option B: Python fallback

- `python/viz/draw_topology.py`: `networkx` + `matplotlib` draw of topology colored by utilization, failed links dashed red, path overlay for a chosen flow, with metric tables beside it. Static images only, no animation.

### 8.3 Optional dashboard additions

- Predicted-vs-actual panel (Phase 6.6) and a chat-style box that calls the Phase 7 query function (via a tiny Flask endpoint, only if time remains).

**Done when:** opening `viz/index.html`, loading a run and pressing Play shows packets flowing, a link turning red at the failure time, and traffic moving to the backup path.

---

## Phase 9 — Integration, Evaluation, Report (Weeks 11–12, all)

### 9.1 Three end-to-end demo scenarios (`config/scenarios/`)

| Scenario | Topology | Setup | What to show |
| --- | --- | --- | --- |
| **S1 Normal** | `medium10` | 3 constant flows (2–5 Mbps), 60 s, Dijkstra | Stable latency, 0 % loss, route table, ping/traceroute |
| **S2 Link failure** | `ring_backup` | 5 Mbps flow; fail R1–R2 at 20 s, restore at 40 s; `reconvergence_delay` 0.2 s (Dijkstra) vs 3 s (Bellman-Ford) | Latency step at 20 s, drops during reconvergence, return at 40 s, algorithm recovery difference, anomaly flagged by ML, GraphRAG explanation |
| **S3 Congestion** | `dumbbell` | Two 30 Mbps flows over a 50 Mbps bottleneck; burst at 30 s | Queue fills, tail drops ≈ 16.7 %, utilization ≈ 100 %, top congested link report |

### 9.2 Fair algorithm comparison

- Same topology, same seed, same events, one algorithm per run (use `compare`). Repeat over 5 seeds and report mean ± std. Include BFS vs Dijkstra on `diamond_weighted` (hops vs latency) and Dijkstra vs Bellman-Ford recovery time.

### 9.3 Complexity evaluation

- Use Phase 3.9 benchmark data: random connected graphs with 100, 500, 1k, 2k, 5k nodes (E ≈ 4V). Plot runtime vs V on log-log axes next to theory: BFS O(V+E), Dijkstra O((V+E) log V), Bellman-Ford O(VE) (with early-exit note). Also measure simulation throughput (events/second) and memory.

### 9.4 Quality gates

- All unit + integration tests pass in CI; ASan/UBSan clean; no compiler warnings; same-seed determinism hash test; Python tests pass; every CLI command exercised by a script test; headers match the documentation.

### 9.5 Documentation and demo

- **README:** build steps, quick start (`load`, `run`, `export`, open viewer), architecture diagram, scenario/trace schemas.
- **Report outline:** problem and motivation → architecture (5 layers) → OOP concept table → DS&A table → algorithms and complexity (with measurements) → simulation model (delay formula, queues, failures) → metrics definitions → results of S1–S3 → ML and GraphRAG → limitations (simplified protocols, no ARP/TCP) → future work (Switch/MAC learning, LPM trie, TCP, VLAN).
- **5-minute demo script:** (1) `load ring_backup`, `show topology` (30 s); (2) `ping`, `traceroute`, `show route R1` (45 s); (3) `fail link R1 R2 at 20`, `run 60` (30 s); (4) open viewer: watch reroute (60 s); (5) `compare` table (45 s); (6) notebook: anomaly flagged (45 s); (7) GraphRAG Q&A (45 s). Rehearse twice; keep exported run files as a fallback if live run fails.

**Done when:** the demo script runs start to finish on a clean checkout following only the README; report and slides are complete.

---

## 10. Team Plan and Weekly Timeline

| Week | A (model, graph, viz) | B (routing, simulation) | C (metrics, ML, GraphRAG) |
| --- | --- | --- | --- |
| 1 | **Phase 0 (all)** | Phase 0 | Phase 0, contracts, golden trace |
| 2 | Phase 1 (model) | EventQueue + engine skeleton (no Graph dependency) | Python readers, viewer prototype on golden trace |
| 3 | Phase 2: Graph, BFS/DFS, UnionFind | Routing interface, WeightPolicy, BFS/Dijkstra tests on a mock graph | Observer replay harness for metrics |
| 4 | Phase 2: loader, sample topologies | Dijkstra, Bellman-Ford; Channel + packet events | `TraceLogger` |
| 5 | Switch/Poisson, `Session`+command framework | Phase 3 finish; Phase 4: failures, adaptive routing | `MetricsCollector` core |
| 6 | CLI commands (`show`, `load`, `set`) | `ScenarioManager`, `EventFactory`, determinism tests | Windows, links, exporters |
| 7 | CLI `ping`, `traceroute`, `step` | Integration tests, `compare` support | Report class, validation; ML dataset generator |
| 8 | CLI `compare`, `export`, script mode | Benchmarks, bug fixing | Finish Phase 5; start anomaly/forecast |
| 9 | Viewer parser/layout/renderer | Support dataset generation, congestion-aware routing | Phase 6 (anomaly, forecast); KG builder |
| 10 | Viewer controls and charts | Distance-vector convergence; scenario files S1–S3 | Phase 7 (query, facts); baseline (opt.) |
| 11 | **Phase 9:** integration | integration, complexity plots | integration, plots, predicted-vs-actual |
| 12 | Report, README | Slides, demo rehearsal | Report ML/GraphRAG sections |

Weekly 30-minute sync: headers still match? trace spec unchanged? CI green? Anyone blocked? Anyone finished early picks the next unowned task from Phase 5B or 8.

---

## 11. Risk Register

| Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- |
| Interface drift between layers | High | High | Header freeze, `[HEADER CHANGE]` PR rule, weekly sync, mock-based tests |
| Event-ordering bugs make runs irreproducible | Medium | High | `(time, seq)` ordering, no unordered iteration in behavior, hash-compare traces in CI |
| Unit mistakes (ms vs s, Mbps vs bytes) | High | Medium | `Units.h` only, hand-checked delay tests |
| Phase 4 takes longer than planned | Medium | High | Start skeleton in Week 2; Poisson/Switch/convergence are Tier 3 |
| ML finds nothing meaningful | Medium | Medium | Inject known anomalies, use z-score baseline, evaluate with labels |
| LLM answers wrong or unavailable | Medium | Medium | Template path first; LLM only rephrases retrieved facts |
| Viewer too slow on big traces | Low | Low | Active-window index, dot cap, trace for demo runs short |
| Team member unavailable | Medium | High | Pair reviews on every PR so someone else can read each module |

---

## 12. Cut Order and Final Checklist

**Cut in this order if time runs out:** (1) NL event creation, (2) ML expected-metric baseline, (3) `Switch`, Poisson, DFS, distance-vector convergence, Level-2 traceroute, (4) fancy viewer → Matplotlib plots, (5) full GraphRAG → keyword search over facts. **Never cut:** Phases 0–5B, at least two routing algorithms compared, link-failure and congestion events, metrics export.

**Final checklist**

- [ ] Topology and scenario JSON load; invalid files give clear errors
- [ ] BFS, Dijkstra, Bellman-Ford produce correct, agreeing routes (tests prove it)
- [ ] Packets show queueing, transmission, propagation delay and drops (hand-check tests pass)
- [ ] Link and router failures reroute traffic through the routing manager
- [ ] `ping`, `traceroute`, `show route`, `compare`, `step` work in the CLI
- [ ] Trace and metric files export with the agreed schemas; same seed ⇒ identical trace
- [ ] Algorithm comparison table and complexity plots generated
- [ ] ML flags an injected failure as an anomaly (F1 beats z-score baseline)
- [ ] GraphRAG answers at least 3 sample questions with cited facts
- [ ] Viewer replays a run including a reroute
- [ ] README, report, slides and demo script finished; CI green on `main`