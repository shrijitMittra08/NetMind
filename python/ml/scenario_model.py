from __future__ import annotations

import heapq
import json
from collections import deque
from dataclasses import dataclass, field
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable

ALGORITHMS = ("bfs", "dfs", "dijkstra", "bellman-ford")
FIXED_POINT_ROUNDS = 30
_OVERLOAD_MARGIN = 1.001
_TINY_INTERVAL_S = 1e-12

Channel = tuple[int, int]


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
    return parse_topology(json.loads(Path(path).read_text(encoding="utf-8-sig")))


def load_topology(path: str | Path) -> Topology:
    resolved = Path(path).expanduser()
    if not resolved.is_file():
        raise FileNotFoundError(f"Topology file not found: {resolved}")
    return _load_topology_cached(str(resolved.resolve()), resolved.stat().st_mtime_ns)


def parse_topology(document: dict[str, Any]) -> Topology:
    nodes = document.get("nodes")
    links = document.get("links")
    if not isinstance(nodes, list) or not isinstance(links, list) or not links:
        raise ValueError("Topology must contain nodes and links")

    names = {int(node["id"]): str(node["name"]) for node in nodes}
    kinds = {int(node["id"]): str(node.get("type", "")).upper() for node in nodes}
    parsed = []
    for index, link in enumerate(links, start=1):
        a = int(link["a"])
        b = int(link["b"])
        if a not in names or b not in names:
            raise ValueError(f"Link references an unknown node: {link!r}")
        bandwidth = float(link["bandwidthMbps"])
        if bandwidth <= 0:
            raise ValueError(f"Link bandwidth must be positive: {link!r}")
        parsed.append(Link(index=index, a=a, b=b, bandwidth_mbps=bandwidth,
                           latency_ms=float(link.get("latencyMs", 0.0)),
                           loss_rate=float(link.get("lossRate", 0.0)),
                           queue_capacity=int(link.get("queueCapacity", 64))))
    ids = {name: node for node, name in names.items()}
    return Topology(names, ids, kinds, tuple(parsed))


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


def _neighbour_ids(topology: Topology, state: GraphState, node: int) -> list[int]:
    return [neighbour for neighbour, _ in _neighbours(topology, state, node)]


def _bfs_table(topology: Topology, state: GraphState, source: int) -> dict[int, int]:
    first_hop: dict[int, int] = {}
    seen = {source}
    queue = deque([source])
    while queue:
        current = queue.popleft()
        for neighbour in _neighbour_ids(topology, state, current):
            if neighbour in seen:
                continue
            seen.add(neighbour)
            first_hop[neighbour] = neighbour if current == source else first_hop[current]
            queue.append(neighbour)
    return first_hop


def _shortest_path_table(topology: Topology, state: GraphState, source: int, weight: str) -> dict[int, int]:
    distance: dict[int, float] = {source: 0.0}
    first_hop: dict[int, int] = {}
    pending = [(0.0, source)]
    while pending:
        current_distance, current = heapq.heappop(pending)
        if distance.get(current) != current_distance:
            continue
        for neighbour, link in _neighbours(topology, state, current):
            if neighbour == source:
                continue
            candidate = current_distance + (1.0 if weight == "hops" else link.latency_ms)
            candidate_first = neighbour if current == source else first_hop[current]
            known = distance.get(neighbour)
            if known is None or candidate < known:
                better = True
            else:
                better = candidate == known and candidate_first < first_hop[neighbour]
            if better:
                distance[neighbour] = candidate
                first_hop[neighbour] = candidate_first
                heapq.heappush(pending, (candidate, neighbour))
    return first_hop


def _dfs_forest(topology: Topology, state: GraphState) -> dict[int, int]:
    parent: dict[int, int] = {}
    for start in sorted(topology.names):
        if start in state.down_nodes or start in parent:
            continue
        parent[start] = start
        stack = [[start, _neighbour_ids(topology, state, start), 0]]
        while stack:
            entry = stack[-1]
            node, neighbours, position = entry
            if position >= len(neighbours):
                stack.pop()
                continue
            entry[2] = position + 1
            neighbour = neighbours[position]
            if neighbour in parent:
                continue
            parent[neighbour] = node
            stack.append([neighbour, _neighbour_ids(topology, state, neighbour), 0])
    return parent


def _tree_path(parent: dict[int, int], source: int, destination: int) -> list[int] | None:
    if source not in parent or destination not in parent:
        return None
    ancestors = [source]
    while parent[ancestors[-1]] != ancestors[-1]:
        ancestors.append(parent[ancestors[-1]])
    position_of = {node: position for position, node in enumerate(ancestors)}

    tail = [destination]
    while tail[-1] not in position_of:
        up = parent[tail[-1]]
        if up == tail[-1]:
            return None
        tail.append(up)
    return ancestors[: position_of[tail[-1]] + 1] + list(reversed(tail[:-1]))


class Router:
    def __init__(self, topology: Topology, state: GraphState, algorithm: str, weight: str):
        algorithm = algorithm.strip().lower()
        if algorithm not in ALGORITHMS:
            raise ValueError(f"Unsupported routing algorithm: {algorithm!r}")
        self.topology = topology
        self.state = state
        self.algorithm = algorithm
        self.weight = weight
        self._tables: dict[int, dict[int, int]] = {}
        self._forest = _dfs_forest(topology, state) if algorithm == "dfs" else None

    def _next_hop(self, node: int, destination: int) -> int | None:
        if self.topology.kinds.get(node) == "HOST":
            gateways = _neighbour_ids(self.topology, self.state, node)
            return gateways[0] if gateways else None
        if self._forest is not None:
            path = _tree_path(self._forest, node, destination)
            return path[1] if path and len(path) > 1 else None
        if node not in self._tables:
            if self.algorithm == "bfs":
                self._tables[node] = _bfs_table(self.topology, self.state, node)
            else:
                self._tables[node] = _shortest_path_table(self.topology, self.state, node, self.weight)
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
    if str(flow.get("pattern", "constant")).lower() != "bursty":
        return 1.0
    on = float(flow.get("on_duration_s", 1.0))
    off = float(flow.get("off_duration_s", 0.0))
    return on / (on + off) if on + off > 0 else 1.0


def _demands(topology: Topology, scenario: dict[str, Any], duration: float) -> list[Demand]:
    demands = []
    for flow in scenario.get("flows", []):
        src = topology.ids.get(str(flow["src"]))
        dst = topology.ids.get(str(flow["dst"]))
        if src is None or dst is None:
            raise ValueError(f"Flow refers to an unknown host: {flow!r}")
        demands.append(Demand(src=src, dst=dst, rate_mbps=float(flow.get("rate_mbps", 0.0)),
                              packet_bits=8.0 * float(flow.get("packet_bytes", 1000)),
                              start=float(flow.get("start_s", 0.0)), stop=float(flow.get("stop_s", duration)),
                              duty=_duty(flow)))
    for event in scenario.get("events", []):
        if str(event.get("type")) != "TRAFFIC_BURST":
            continue
        start = float(event["time_s"])
        demands.append(Demand(src=topology.ids[str(event["src"])], dst=topology.ids[str(event["dst"])],
                              rate_mbps=float(event["rate_mbps"]),
                              packet_bits=8.0 * float(event.get("packet_bytes", 1000)),
                              start=start, stop=start + float(event["duration_s"]), burst=True))
    return demands


def _state_changes(topology: Topology, scenario: dict[str, Any]) -> list[tuple[float, str, Any]]:
    changes = []
    for event in scenario.get("events", []):
        kind = str(event.get("type"))
        time = float(event.get("time_s", 0.0))
        if kind in ("LINK_FAILURE", "LINK_RESTORE", "CONGESTION"):
            link = topology.link_between(topology.ids[str(event["a"])], topology.ids[str(event["b"])])
            if link is None:
                raise ValueError(f"Event refers to an unknown link: {event!r}")
            if kind == "CONGESTION":
                end = time + float(event.get("duration_s", 0.0))
                multiplier = float(event.get("bandwidth_multiplier", 1.0))
                extra_loss = float(event.get("extra_loss_rate", 0.0))
                changes.append((time, "congest", (link.index, multiplier, extra_loss)))
                changes.append((end, "uncongest", link.index))
            elif kind == "LINK_FAILURE":
                changes.append((time, "link_down", link.index))
            else:
                changes.append((time, "link_up", link.index))
        elif kind == "ROUTER_FAILURE":
            changes.append((time, "node_down", topology.ids[str(event["node"])]))
        elif kind == "ROUTER_RESTORE":
            changes.append((time, "node_up", topology.ids[str(event["node"])]))
    return sorted(changes, key=lambda change: change[0])


def _state_at(changes: list[tuple[float, str, Any]],
              time: float) -> tuple[GraphState, dict[int, float], dict[int, float]]:
    down_links: set[int] = set()
    down_nodes: set[int] = set()
    capacity_factor: dict[int, float] = {}
    extra_loss: dict[int, float] = {}
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
            link_index, multiplier, loss = value
            capacity_factor[link_index] = multiplier
            extra_loss[link_index] = loss
        elif kind == "uncongest":
            capacity_factor.pop(value, None)
            extra_loss.pop(value, None)
    return GraphState(frozenset(down_links), frozenset(down_nodes)), capacity_factor, extra_loss


def _intervals(changes: list[tuple[float, str, Any]], demands: list[Demand], duration: float,
               delay_s: float) -> list[Interval]:
    cuts = {0.0, duration}
    for time, _, _ in changes:
        cuts.update({time, time + delay_s})
    for demand in demands:
        cuts.update({demand.start, demand.stop})
    cuts = sorted(cut for cut in cuts if 0.0 <= cut <= duration)

    intervals = []
    for start, end in zip(cuts, cuts[1:]):
        if end - start <= _TINY_INTERVAL_S:
            continue
        physical, capacity_factor, extra_loss = _state_at(changes, start)
        routing = _state_at(changes, start - delay_s)[0] if delay_s > 0 else physical
        intervals.append(Interval(start, end, physical, routing, capacity_factor, extra_loss))
    return intervals


@dataclass
class Estimate:
    latency_ms: float
    throughput_mbps: float
    loss_pct: float
    features: dict[str, float]


@dataclass
class _Totals:
    sent_packets: float = 0.0
    delivered_packets: float = 0.0
    delivered_bits: float = 0.0
    latency_packet_ms: float = 0.0
    offered_bits: float = 0.0
    burst_bits: float = 0.0
    rerouted_bits: float = 0.0
    unreachable_bits: float = 0.0
    congested_path_bits: float = 0.0
    stretch_bits_ms: float = 0.0
    max_utilisation: float = 0.0
    saturated_time: float = 0.0
    disturbed_time: float = 0.0


def _capacity(link: Link, interval: Interval) -> float:
    return link.bandwidth_mbps * interval.capacity_factor.get(link.index, 1.0)


def _survival(link: Link, interval: Interval) -> float:
    return (1.0 - link.loss_rate) * (1.0 - interval.extra_loss.get(link.index, 0.0))


def _path_latency_ms(topology: Topology, path: list[int]) -> float:
    return sum(topology.link_between(a, b).latency_ms for a, b in zip(path, path[1:]))


def _route_for(topology: Topology, router: Router, interval: Interval,
               demand: Demand) -> tuple[list[int] | None, list[tuple[Link, Channel]] | None]:
    path = router.path(demand.src, demand.dst)
    if path is None:
        return None, None
    if any(node in interval.physical.down_nodes for node in path):
        return path, None
    route = []
    for a, b in zip(path, path[1:]):
        link = topology.link_between(a, b)
        if link is None or link.index in interval.physical.down_links:
            return path, None
        route.append((link, (link.index, 0 if a == link.a else 1)))
    return path, route


def _count_offered_traffic(topology: Topology, interval: Interval, demand: Demand, path: list[int] | None,
                           route: list[tuple[Link, Channel]] | None, baseline_path: list[int] | None,
                           totals: _Totals) -> None:
    usable = route is not None
    bits = demand.mean_rate_mbps * 1e6 * (interval.end - interval.start)
    totals.offered_bits += bits
    if demand.burst:
        totals.burst_bits += bits
    if path is None:
        totals.unreachable_bits += bits
    if baseline_path is not None and (path != baseline_path or not usable):
        totals.rerouted_bits += bits
    if usable and baseline_path is not None:
        stretch = _path_latency_ms(topology, path) - _path_latency_ms(topology, baseline_path)
        totals.stretch_bits_ms += bits * stretch
    if usable and any(link.index in interval.capacity_factor for link, _ in route):
        totals.congested_path_bits += bits


def _push_flow_rates(active: list[Demand], routes: dict[int, list[tuple[Link, Channel]] | None],
                     links_by_index: dict[int, Link],
                     interval: Interval) -> tuple[dict[Channel, float], dict[Channel, float]]:
    passing: dict[Channel, float] = {}
    for _ in range(FIXED_POINT_ROUNDS):
        arrivals: dict[Channel, float] = {}
        for demand in active:
            rate = demand.rate_mbps
            for link, channel in routes[id(demand)] or []:
                arrivals[channel] = arrivals.get(channel, 0.0) + rate
                rate *= passing.get(channel, 1.0) * _survival(link, interval)

        updated: dict[Channel, float] = {}
        for channel, load in arrivals.items():
            if load > 0:
                updated[channel] = min(1.0, _capacity(links_by_index[channel[0]], interval) / load)
            else:
                updated[channel] = 1.0
        if updated == passing:
            break
        passing = updated
    return arrivals, passing


def _count_utilisation(arrivals: dict[Channel, float], links_by_index: dict[int, Link], interval: Interval,
                       totals: _Totals) -> None:
    for channel, load in arrivals.items():
        utilisation = load / _capacity(links_by_index[channel[0]], interval)
        totals.max_utilisation = max(totals.max_utilisation, utilisation)
        if utilisation > _OVERLOAD_MARGIN:
            totals.saturated_time += interval.end - interval.start


def _count_delivery(demand: Demand, route: list[tuple[Link, Channel]] | None, arrivals: dict[Channel, float],
                    passing: dict[Channel, float], interval: Interval, totals: _Totals) -> None:
    packets = demand.mean_rate_mbps * 1e6 * (interval.end - interval.start) / demand.packet_bits
    totals.sent_packets += packets
    if not route:
        return
    fraction = 1.0
    latency = 0.0
    for link, channel in route:
        capacity = _capacity(link, interval)
        transmit_ms = demand.packet_bits / (capacity * 1e3)
        latency += link.latency_ms + transmit_ms
        if arrivals.get(channel, 0.0) > capacity * _OVERLOAD_MARGIN:
            latency += link.queue_capacity * transmit_ms
        fraction *= passing.get(channel, 1.0) * _survival(link, interval)
    delivered = packets * fraction
    totals.delivered_packets += delivered
    totals.delivered_bits += delivered * demand.packet_bits
    totals.latency_packet_ms += delivered * latency


def _add_interval(topology: Topology, links_by_index: dict[int, Link], interval: Interval, demands: list[Demand],
                  router_for: Callable[[GraphState], Router], baseline_paths: dict[int, list[int] | None],
                  totals: _Totals) -> None:
    physical = interval.physical
    if physical.down_links or physical.down_nodes or interval.capacity_factor:
        totals.disturbed_time += interval.end - interval.start
    active = [d for d in demands if d.start <= interval.start < d.stop and d.rate_mbps > 0]
    if not active:
        return
    router = router_for(interval.routing)

    routes: dict[int, list[tuple[Link, Channel]] | None] = {}
    for demand in active:
        path, route = _route_for(topology, router, interval, demand)
        routes[id(demand)] = route
        _count_offered_traffic(topology, interval, demand, path, route, baseline_paths[id(demand)], totals)
    arrivals, passing = _push_flow_rates(active, routes, links_by_index, interval)
    _count_utilisation(arrivals, links_by_index, interval, totals)
    for demand in active:
        _count_delivery(demand, routes[id(demand)], arrivals, passing, interval, totals)


def _share(bits: float, offered_bits: float) -> float:
    return bits / offered_bits if offered_bits > 0 else 0.0


def _path_summary(topology: Topology, demands: list[Demand],
                  baseline_paths: dict[int, list[int] | None]) -> tuple[float, float, float]:
    weights: list[float] = []
    paths: list[list[int]] = []
    for demand in demands:
        path = baseline_paths[id(demand)]
        if not demand.burst and path:
            weights.append(demand.mean_rate_mbps)
            paths.append(path)
    if not paths:
        return 0.0, 0.0, 0.0
    pairs = list(zip(weights, paths))
    mean_hops = sum(w * (len(p) - 1) for w, p in pairs) / sum(weights)
    mean_latency = sum(w * _path_latency_ms(topology, p) for w, p in pairs) / sum(weights)
    bottleneck = min(min(topology.link_between(a, b).bandwidth_mbps for a, b in zip(p, p[1:])) for p in paths)
    return mean_hops, mean_latency, bottleneck


def estimate(topology: Topology, scenario: dict[str, Any]) -> Estimate:
    duration = float(scenario.get("duration_s", 10.0))
    routing = scenario.get("routing", {}) or {}
    algorithm = str(routing.get("algorithm", scenario.get("algorithm", "dijkstra"))).lower()
    weight = str(routing.get("weight", "latency")).lower()
    delay_s = float(routing.get("reconvergence_delay_ms", 0.0) or 0.0) / 1000.0
    demands = _demands(topology, scenario, duration)
    intervals = _intervals(_state_changes(topology, scenario), demands, duration, delay_s)

    routers: dict[GraphState, Router] = {}

    def router_for(state: GraphState) -> Router:
        if state not in routers:
            routers[state] = Router(topology, state, algorithm, weight)
        return routers[state]

    baseline_router = router_for(GraphState())
    baseline_paths = {id(d): baseline_router.path(d.src, d.dst) for d in demands}
    links_by_index = {link.index: link for link in topology.links}
    totals = _Totals()
    for interval in intervals:
        _add_interval(topology, links_by_index, interval, demands, router_for, baseline_paths, totals)

    loss = 0.0
    if totals.sent_packets > 0:
        loss = 100.0 * (totals.sent_packets - totals.delivered_packets) / totals.sent_packets
    throughput = totals.delivered_bits / (duration * 1e6) if duration > 0 else 0.0
    latency = totals.latency_packet_ms / totals.delivered_packets if totals.delivered_packets > 0 else 0.0
    features = _features(topology, demands, baseline_paths, totals, duration, algorithm, weight, delay_s)
    return Estimate(latency, throughput, loss, features)


def _features(topology: Topology, demands: list[Demand], baseline_paths: dict[int, list[int] | None],
              totals: _Totals, duration: float, algorithm: str, weight: str, delay_s: float) -> dict[str, float]:
    mean_hops, mean_latency, bottleneck = _path_summary(topology, demands, baseline_paths)
    flows = [d for d in demands if not d.burst]
    node_count = len(topology.names)
    link_count = len(topology.links)
    offered_bits = totals.offered_bits
    features = {
        "node_count": float(node_count),
        "link_count": float(link_count),
        "average_degree": 2.0 * link_count / node_count if node_count else 0.0,
        "flow_count": float(len(flows)),
        "offered_rate_mbps": sum(d.mean_rate_mbps for d in flows),
        "bursty_flow_share": sum(1 for d in flows if d.duty < 1.0) / max(1, len(flows)),
        "duration_s": duration,
        "mean_path_hops": mean_hops,
        "mean_path_latency_ms": mean_latency,
        "min_path_bottleneck_mbps": bottleneck,
        "max_channel_utilisation": totals.max_utilisation,
        "saturated_time_share": totals.saturated_time / duration if duration > 0 else 0.0,
        "disturbed_time_share": totals.disturbed_time / duration if duration > 0 else 0.0,
        "rerouted_traffic_share": _share(totals.rerouted_bits, offered_bits),
        "unreachable_traffic_share": _share(totals.unreachable_bits, offered_bits),
        "congested_path_share": _share(totals.congested_path_bits, offered_bits),
        "burst_traffic_share": _share(totals.burst_bits, offered_bits),
        "reroute_stretch_ms": _share(totals.stretch_bits_ms, offered_bits),
        "reconvergence_delay_ms": delay_s * 1000.0,
        "weight_hops": float(weight == "hops"),
        "weight_adaptive": float(weight == "latency+congestion"),
    }
    for name in ALGORITHMS:
        features[f"algorithm_{name}"] = float(algorithm == name)
    return features
