from __future__ import annotations

import heapq
import json
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path
from typing import Any

ALGORITHMS = ("bfs", "dfs", "dijkstra", "bellman-ford")
WEIGHTS = ("latency", "hops")
FIXED_POINT_ROUNDS = 30


@dataclass(frozen=True)
class Link:
    index: int
    a: int
    b: int
    bandwidth_mbps: float
    latency_ms: float
    loss_rate: float
    queue_capacity: int


@dataclass(frozen=True)
class Topology:
    names: dict[int, str]
    ids: dict[str, int]
    kinds: dict[int, str]
    links: tuple[Link, ...]

    @property
    def hosts(self) -> list[int]:
        return sorted(node for node, kind in self.kinds.items() if kind == "HOST")

    def link_between(self, a: int, b: int) -> Link | None:
        for link in self.links:
            if {link.a, link.b} == {a, b}:
                return link
        return None


@lru_cache(maxsize=64)
def _load_topology_cached(path: str, stamp: int) -> Topology:
    document = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    return parse_topology(document)


def load_topology(path: str | Path) -> Topology:
    resolved = Path(path).expanduser()
    if not resolved.is_file():
        raise FileNotFoundError(f"Topology file not found: {resolved}")
    return _load_topology_cached(str(resolved.resolve()), resolved.stat().st_mtime_ns)


def parse_topology(document: dict[str, Any]) -> Topology:
    nodes, links = document.get("nodes"), document.get("links")
    if not isinstance(nodes, list) or not isinstance(links, list) or not links:
        raise ValueError("Topology must contain nodes and links")
    names = {int(node["id"]): str(node["name"]) for node in nodes}
    kinds = {int(node["id"]): str(node.get("type", "")).upper() for node in nodes}
    parsed = []
    for index, link in enumerate(links, start=1):
        a, b = int(link["a"]), int(link["b"])
        if a not in names or b not in names:
            raise ValueError(f"Link references an unknown node: {link!r}")
        bandwidth = float(link["bandwidthMbps"])
        if bandwidth <= 0:
            raise ValueError(f"Link bandwidth must be positive: {link!r}")
        parsed.append(Link(index, a, b, bandwidth, float(link.get("latencyMs", 0.0)),
                           float(link.get("lossRate", 0.0)), int(link.get("queueCapacity", 64))))
    return Topology(names, {name: node for node, name in names.items()}, kinds, tuple(parsed))


@dataclass(frozen=True)
class GraphState:
    down_links: frozenset[int] = frozenset()
    down_nodes: frozenset[int] = frozenset()


def _neighbours(topology: Topology, state: GraphState, node: int) -> list[tuple[int, Link]]:
    result = []
    for link in topology.links:
        if link.index in state.down_links or node not in (link.a, link.b):
            continue
        other = link.b if link.a == node else link.a
        if other not in state.down_nodes:
            result.append((other, link))
    return sorted(result, key=lambda item: item[0])


def _weight(link: Link, weight: str) -> float:
    return 1.0 if weight == "hops" else link.latency_ms


def _bfs_table(topology: Topology, state: GraphState, source: int) -> dict[int, int]:
    first: dict[int, int] = {}
    seen, queue = {source}, [source]
    while queue:
        current = queue.pop(0)
        for neighbour, _ in _neighbours(topology, state, current):
            if neighbour in seen:
                continue
            seen.add(neighbour)
            first[neighbour] = neighbour if current == source else first[current]
            queue.append(neighbour)
    return first


def _shortest_path_table(topology: Topology, state: GraphState, source: int, weight: str) -> dict[int, int]:
    distance: dict[int, float] = {source: 0.0}
    first: dict[int, int] = {}
    pending = [(0.0, source)]
    while pending:
        current_distance, current = heapq.heappop(pending)
        if distance.get(current) != current_distance:
            continue
        for neighbour, link in _neighbours(topology, state, current):
            if neighbour == source:
                continue
            candidate = current_distance + _weight(link, weight)
            candidate_first = neighbour if current == source else first[current]
            known = distance.get(neighbour)
            if known is None or candidate < known or (candidate == known and candidate_first < first.get(neighbour, candidate_first + 1)):
                distance[neighbour] = candidate
                first[neighbour] = candidate_first
                heapq.heappush(pending, (candidate, neighbour))
    return first


def _dfs_forest(topology: Topology, state: GraphState) -> dict[int, int]:
    parent: dict[int, int] = {}
    for start in sorted(topology.names):
        if start in state.down_nodes or start in parent:
            continue
        parent[start] = start
        stack = [(start, [n for n, _ in _neighbours(topology, state, start)], 0)]
        while stack:
            node, neighbours, position = stack[-1]
            if position >= len(neighbours):
                stack.pop()
                continue
            stack[-1] = (node, neighbours, position + 1)
            neighbour = neighbours[position]
            if neighbour in parent:
                continue
            parent[neighbour] = node
            stack.append((neighbour, [n for n, _ in _neighbours(topology, state, neighbour)], 0))
    return parent


def _tree_path(parent: dict[int, int], source: int, destination: int) -> list[int] | None:
    if source not in parent or destination not in parent:
        return None
    ancestors = [source]
    while parent[ancestors[-1]] != ancestors[-1]:
        ancestors.append(parent[ancestors[-1]])
    index = {node: position for position, node in enumerate(ancestors)}
    tail = [destination]
    while tail[-1] not in index:
        up = parent[tail[-1]]
        if up == tail[-1]:
            return None
        tail.append(up)
    return ancestors[: index[tail[-1]] + 1] + list(reversed(tail[:-1]))


class Router:

    def __init__(self, topology: Topology, state: GraphState, algorithm: str, weight: str):
        algorithm = algorithm.strip().lower()
        if algorithm not in ALGORITHMS:
            raise ValueError(f"Unsupported routing algorithm: {algorithm!r}")
        self.topology, self.state, self.algorithm, self.weight = topology, state, algorithm, weight
        self._tables: dict[int, dict[int, int]] = {}
        self._forest = _dfs_forest(topology, state) if algorithm == "dfs" else None

    def _next_hop(self, node: int, destination: int) -> int | None:
        if self.topology.kinds.get(node) == "HOST":
            gateways = _neighbours(self.topology, self.state, node)
            return gateways[0][0] if gateways else None
        if self._forest is not None:
            path = _tree_path(self._forest, node, destination)
            return path[1] if path and len(path) > 1 else None
        if node not in self._tables:
            self._tables[node] = (
                _bfs_table(self.topology, self.state, node) if self.algorithm == "bfs"
                else _shortest_path_table(self.topology, self.state, node, self.weight)
            )
        return self._tables[node].get(destination)

    def path(self, source: int, destination: int) -> list[int] | None:
        if source in self.state.down_nodes:
            return None
        route = [source]
        while route[-1] != destination:
            hop = self._next_hop(route[-1], destination)
            if hop is None or hop in route or len(route) > len(self.topology.names):
                return None
            route.append(hop)
        return route


@dataclass
class Demand:
    src: int
    dst: int
    rate_mbps: float
    packet_bits: float
    start: float
    stop: float
    burst: bool = False
    duty: float = 1.0

    @property
    def mean_rate_mbps(self) -> float:
        return self.rate_mbps * self.duty


@dataclass
class Interval:
    start: float
    end: float
    physical: GraphState
    routing: GraphState
    capacity_factor: dict[int, float] = field(default_factory=dict)
    extra_loss: dict[int, float] = field(default_factory=dict)


def _duty(flow: dict[str, Any]) -> float:
    if str(flow.get("pattern", "constant")).lower() == "bursty":
        on, off = float(flow.get("on_duration_s", 1.0)), float(flow.get("off_duration_s", 0.0))
        return on / (on + off) if on + off > 0 else 1.0
    return 1.0


def _demands(topology: Topology, scenario: dict[str, Any], duration: float) -> list[Demand]:
    demands = []
    for flow in scenario.get("flows", []):
        src, dst = topology.ids.get(str(flow["src"])), topology.ids.get(str(flow["dst"]))
        if src is None or dst is None:
            raise ValueError(f"Flow refers to an unknown host: {flow!r}")
        demands.append(Demand(src, dst, float(flow.get("rate_mbps", 0.0)), 8.0 * float(flow.get("packet_bytes", 1000)),
                              float(flow.get("start_s", 0.0)), float(flow.get("stop_s", duration)), duty=_duty(flow)))
    for event in scenario.get("events", []):
        if str(event.get("type")) == "TRAFFIC_BURST":
            start = float(event["time_s"])
            demands.append(Demand(topology.ids[str(event["src"])], topology.ids[str(event["dst"])],
                                  float(event["rate_mbps"]), 8.0 * float(event.get("packet_bytes", 1000)),
                                  start, start + float(event["duration_s"]), burst=True))
    return demands


def _state_changes(topology: Topology, scenario: dict[str, Any]) -> list[tuple[float, str, Any]]:
    changes = []
    for event in scenario.get("events", []):
        kind, time = str(event.get("type")), float(event.get("time_s", 0.0))
        if kind in ("LINK_FAILURE", "LINK_RESTORE", "CONGESTION"):
            link = topology.link_between(topology.ids[str(event["a"])], topology.ids[str(event["b"])])
            if link is None:
                raise ValueError(f"Event refers to an unknown link: {event!r}")
            if kind == "CONGESTION":
                end = time + float(event.get("duration_s", 0.0))
                changes.append((time, "congest", (link.index, float(event.get("bandwidth_multiplier", 1.0)),
                                                  float(event.get("extra_loss_rate", 0.0)))))
                changes.append((end, "uncongest", link.index))
            else:
                changes.append((time, "link_down" if kind == "LINK_FAILURE" else "link_up", link.index))
        elif kind in ("ROUTER_FAILURE", "ROUTER_RESTORE"):
            node = topology.ids[str(event["node"])]
            changes.append((time, "node_down" if kind == "ROUTER_FAILURE" else "node_up", node))
    return sorted(changes, key=lambda change: change[0])


def _intervals(topology: Topology, scenario: dict[str, Any], duration: float, delay_s: float) -> list[Interval]:
    changes = _state_changes(topology, scenario)
    demands = _demands(topology, scenario, duration)
    cuts = {0.0, duration}
    for time, _, _ in changes:
        cuts.update({time, time + delay_s})
    for demand in demands:
        cuts.update({demand.start, demand.stop})
    cuts = sorted(cut for cut in cuts if 0.0 <= cut <= duration)

    def state_at(time: float) -> tuple[GraphState, dict[int, float], dict[int, float]]:
        down_links: set[int] = set()
        down_nodes: set[int] = set()
        factor: dict[int, float] = {}
        extra: dict[int, float] = {}
        for change_time, kind, value in changes:
            if change_time > time:
                break
            if kind == "link_down":
                down_links.add(value)
            elif kind == "link_up":
                down_links.discard(value)
            elif kind == "node_down":
                down_nodes.add(value)
            elif kind == "node_up":
                down_nodes.discard(value)
            elif kind == "congest":
                factor[value[0]], extra[value[0]] = value[1], value[2]
            elif kind == "uncongest":
                factor.pop(value, None)
                extra.pop(value, None)
        return GraphState(frozenset(down_links), frozenset(down_nodes)), factor, extra

    intervals = []
    for start, end in zip(cuts, cuts[1:]):
        if end - start <= 1e-12:
            continue
        physical, factor, extra = state_at(start)
        routing, _, _ = state_at(start - delay_s) if delay_s > 0 else (physical, None, None)
        intervals.append(Interval(start, end, physical, routing, factor, extra))
    return intervals


@dataclass
class Estimate:
    latency_ms: float
    throughput_mbps: float
    loss_pct: float
    features: dict[str, float]


def _channel(link: Link, a: int, b: int) -> tuple[int, int]:
    return (link.index, 0 if a == link.a else 1)


def estimate(topology: Topology, scenario: dict[str, Any]) -> Estimate:
    duration = float(scenario.get("duration_s", 10.0))
    routing = scenario.get("routing", {}) or {}
    algorithm = str(routing.get("algorithm", scenario.get("algorithm", "dijkstra"))).lower()
    weight = str(routing.get("weight", "latency")).lower()
    delay_s = float(routing.get("reconvergence_delay_ms", 0.0) or 0.0) / 1000.0
    demands = _demands(topology, scenario, duration)
    intervals = _intervals(topology, scenario, duration, delay_s)
    routers: dict[GraphState, Router] = {}

    def router_for(state: GraphState) -> Router:
        if state not in routers:
            routers[state] = Router(topology, state, algorithm, weight)
        return routers[state]

    baseline_router = router_for(GraphState())
    baseline_paths = {id(d): baseline_router.path(d.src, d.dst) for d in demands}

    sent_packets = delivered_packets = delivered_bits = latency_packet_ms = 0.0
    offered_bits = burst_bits = 0.0
    hit_bits = unreachable_bits = congested_path_bits = 0.0
    stretch_bits_ms = 0.0
    max_utilisation = 0.0
    saturated_time = 0.0
    disturbed_time = 0.0

    for interval in intervals:
        length = interval.end - interval.start
        active = [d for d in demands if d.start <= interval.start < d.stop and d.rate_mbps > 0]
        if interval.physical.down_links or interval.physical.down_nodes or interval.capacity_factor:
            disturbed_time += length
        if not active:
            continue
        router = router_for(interval.routing)
        links_by_index = {link.index: link for link in topology.links}

        hops: dict[int, list[tuple[Link, tuple[int, int]]] | None] = {}
        for demand in active:
            path = router.path(demand.src, demand.dst)
            usable = path is not None and not any(n in interval.physical.down_nodes for n in path)
            route = []
            if usable:
                for a, b in zip(path, path[1:]):
                    link = topology.link_between(a, b)
                    if link is None or link.index in interval.physical.down_links:
                        usable = False
                        break
                    route.append((link, _channel(link, a, b)))
            hops[id(demand)] = route if usable else None

            bits = demand.mean_rate_mbps * 1e6 * length
            offered_bits += bits
            if demand.burst:
                burst_bits += bits
            base = baseline_paths[id(demand)]
            if path is None:
                unreachable_bits += bits
            if base is not None and (path != base or not usable):
                hit_bits += bits
            if usable and base is not None:
                stretch = sum(l.latency_ms for l, _ in route) - sum(
                    topology.link_between(a, b).latency_ms for a, b in zip(base, base[1:]))
                stretch_bits_ms += bits * stretch
            if usable and any(l.index in interval.capacity_factor for l, _ in route):
                congested_path_bits += bits

        def capacity(link: Link) -> float:
            return link.bandwidth_mbps * interval.capacity_factor.get(link.index, 1.0)

        def survive(link: Link) -> float:
            return (1.0 - link.loss_rate) * (1.0 - interval.extra_loss.get(link.index, 0.0))

        passing: dict[tuple[int, int], float] = {}
        for _ in range(FIXED_POINT_ROUNDS):
            arrivals: dict[tuple[int, int], float] = {}
            for demand in active:
                route = hops[id(demand)]
                if not route:
                    continue
                rate = demand.rate_mbps
                for link, channel in route:
                    arrivals[channel] = arrivals.get(channel, 0.0) + rate
                    rate *= passing.get(channel, 1.0) * survive(link)
            updated = {
                channel: min(1.0, capacity(links_by_index[channel[0]]) / load) if load > 0 else 1.0
                for channel, load in arrivals.items()
            }
            if updated == passing:
                break
            passing = updated

        for channel, load in arrivals.items():
            utilisation = load / capacity(links_by_index[channel[0]])
            max_utilisation = max(max_utilisation, utilisation)
            if utilisation > 1.001:
                saturated_time += length

        for demand in active:
            packets = demand.mean_rate_mbps * 1e6 * length / demand.packet_bits
            sent_packets += packets
            route = hops[id(demand)]
            if not route:
                continue
            fraction, latency = 1.0, 0.0
            for link, channel in route:
                cap = capacity(link)
                transmit_ms = demand.packet_bits / (cap * 1e3)
                latency += link.latency_ms + transmit_ms
                if arrivals.get(channel, 0.0) > cap * 1.001:
                    latency += link.queue_capacity * transmit_ms
                fraction *= passing.get(channel, 1.0) * survive(link)
            delivered = packets * fraction
            delivered_packets += delivered
            delivered_bits += delivered * demand.packet_bits
            latency_packet_ms += delivered * latency

    loss = 100.0 * (sent_packets - delivered_packets) / sent_packets if sent_packets > 0 else 0.0
    throughput = delivered_bits / (duration * 1e6) if duration > 0 else 0.0
    latency = latency_packet_ms / delivered_packets if delivered_packets > 0 else 0.0

    reachable = [(d, p) for d, p in ((d, baseline_paths[id(d)]) for d in demands if not d.burst) if p]
    weights = [d.mean_rate_mbps for d, _ in reachable] or [1.0]
    paths = [p for _, p in reachable]
    hops_mean = sum(w * (len(p) - 1) for w, p in zip(weights, paths)) / sum(weights) if paths else 0.0
    path_latency = sum(
        w * sum(topology.link_between(a, b).latency_ms for a, b in zip(p, p[1:])) for w, p in zip(weights, paths)
    ) / sum(weights) if paths else 0.0
    bottleneck = min(
        (min(topology.link_between(a, b).bandwidth_mbps for a, b in zip(p, p[1:])) for p in paths), default=0.0
    )
    node_count, link_count = len(topology.names), len(topology.links)
    share = (lambda bits: bits / offered_bits) if offered_bits > 0 else (lambda bits: 0.0)

    return Estimate(latency, throughput, loss, {
        "node_count": float(node_count),
        "link_count": float(link_count),
        "average_degree": 2.0 * link_count / node_count if node_count else 0.0,
        "flow_count": float(sum(1 for d in demands if not d.burst)),
        "offered_rate_mbps": sum(d.mean_rate_mbps for d in demands if not d.burst),
        "bursty_flow_share": (sum(1 for d in demands if not d.burst and d.duty < 1.0)
                              / max(1, sum(1 for d in demands if not d.burst))),
        "duration_s": duration,
        "mean_path_hops": hops_mean,
        "mean_path_latency_ms": path_latency,
        "min_path_bottleneck_mbps": bottleneck,
        "max_channel_utilisation": max_utilisation,
        "saturated_time_share": saturated_time / duration if duration > 0 else 0.0,
        "disturbed_time_share": disturbed_time / duration if duration > 0 else 0.0,
        "rerouted_traffic_share": share(hit_bits),
        "unreachable_traffic_share": share(unreachable_bits),
        "congested_path_share": share(congested_path_bits),
        "burst_traffic_share": share(burst_bits),
        "reroute_stretch_ms": stretch_bits_ms / offered_bits if offered_bits > 0 else 0.0,
        "reconvergence_delay_ms": delay_s * 1000.0,
        "weight_hops": float(weight == "hops"),
        "weight_adaptive": float(weight == "latency+congestion"),
        **{f"algorithm_{name}": float(algorithm == name) for name in ALGORITHMS},
    })
