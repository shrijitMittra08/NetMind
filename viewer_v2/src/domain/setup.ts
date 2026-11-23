import type { EventDraft, EventType, FlowDraft, SimConfig, SimSettings, Topology } from "../lib/types";
import { linkNamePairs } from "./topology";

export const DEFAULT_CONFIG: SimConfig = {
  duration_s: 10,
  seed: 42,
  algorithm: "dijkstra",
  weight: "latency",
  reconvergence_delay_ms: 0,
};

export const LINK_EVENTS: EventType[] = ["LINK_FAILURE", "LINK_RESTORE", "CONGESTION"];
export const ROUTER_EVENTS: EventType[] = ["ROUTER_FAILURE", "ROUTER_RESTORE"];

export const EVENT_LABELS: Record<EventType, string> = {
  LINK_FAILURE: "Link failure",
  LINK_RESTORE: "Link restore",
  ROUTER_FAILURE: "Router failure",
  ROUTER_RESTORE: "Router restore",
  CONGESTION: "Congestion",
  TRAFFIC_BURST: "Traffic burst",
};

function hostNames(topology: Topology | null): string[] {
  return (topology?.nodes ?? []).filter((node) => node.type === "HOST").map((node) => node.name);
}

export function makeFlow(topology: Topology | null): FlowDraft {
  const hosts = hostNames(topology);
  return {
    src: hosts[0] ?? "",
    dst: hosts[1] ?? "",
    rate_mbps: 5,
    packet_bytes: 1000,
    start_s: 0,
    stop_s: 10,
    pattern: "constant",
    on_duration_s: 1,
    off_duration_s: 1,
  };
}

export function makeEvent(topology: Topology | null, type: EventType = "LINK_FAILURE"): EventDraft {
  const hosts = hostNames(topology);
  const [a, b] = (topology ? linkNamePairs(topology)[0] : undefined) ?? ["", ""];
  const firstRouter = topology?.nodes.find((node) => node.type === "ROUTER");
  return {
    type,
    time_s: 2,
    a,
    b,
    node: firstRouter?.name ?? "",
    bandwidth_multiplier: 0.5,
    extra_loss_rate: 0.01,
    duration_s: 1,
    src: hosts[0] ?? "",
    dst: hosts[1] ?? "",
    rate_mbps: 10,
    packet_bytes: 1000,
  };
}

export function findSetupProblems(topology: Topology | null, settings: SimSettings): string[] {
  const nodes = topology?.nodes ?? [];
  const isHost = (name: string) => nodes.some((node) => node.name === name && node.type === "HOST");
  const isRouter = (name: string) => nodes.some((node) => node.name === name && node.type === "ROUTER");
  const links = topology ? linkNamePairs(topology) : [];
  const hasLink = (a: string, b: string) => links.some(([x, y]) => (x === a && y === b) || (x === b && y === a));
  const problems: string[] = [];
  const checkHosts = (label: string, src: string, dst: string) => {
    if (!isHost(src)) problems.push(`${label}: choose a source host.`);
    if (!isHost(dst)) problems.push(`${label}: choose a destination host.`);
    if (src && src === dst) problems.push(`${label}: source and destination must differ.`);
  };

  if (!topology) problems.push("Load or build a topology first.");
  if (!(settings.config.duration_s > 0)) problems.push("The simulation duration must be greater than 0.");
  if (!((settings.config.reconvergence_delay_ms ?? 0) >= 0)) problems.push("The reconvergence delay cannot be negative.");
  if (settings.flows.length === 0) problems.push("Add at least one traffic flow.");

  settings.flows.forEach((flow, i) => {
    const label = `Flow ${i + 1}`;
    checkHosts(label, flow.src, flow.dst);
    if (!(flow.rate_mbps > 0)) problems.push(`${label}: rate must be greater than 0.`);
    if (!(flow.packet_bytes > 0)) problems.push(`${label}: packet size must be greater than 0.`);
    if (!(flow.stop_s > flow.start_s)) problems.push(`${label}: stop time must be after start time.`);
  });

  settings.events.forEach((item, i) => {
    const label = `Event ${i + 1}`;
    if (LINK_EVENTS.includes(item.type)) {
      if (!hasLink(item.a, item.b)) problems.push(`${label}: choose a link that exists in this topology.`);
    } else if (ROUTER_EVENTS.includes(item.type)) {
      if (!isRouter(item.node)) problems.push(`${label}: choose a router.`);
    } else {
      checkHosts(label, item.src, item.dst);
    }
    if (!(item.time_s >= 0)) problems.push(`${label}: time cannot be negative.`);
  });
  return problems;
}

export function repairForTopology(topology: Topology, settings: SimSettings): SimSettings {
  const hosts = hostNames(topology);
  const known = new Set(topology.nodes.map((node) => node.name));
  const keep = (name: string) => (known.has(name) ? name : "");
  let flows = settings.flows;
  if (hosts.length >= 2) {
    flows = flows.map((flow) => ({
      ...flow,
      src: hosts.includes(flow.src) ? flow.src : hosts[0]!,
      dst: hosts.includes(flow.dst) ? flow.dst : hosts[1]!,
    }));
  }
  return {
    config: settings.config,
    flows,
    events: settings.events.map((item) => ({
      ...item,
      a: keep(item.a),
      b: keep(item.b),
      node: keep(item.node),
      src: keep(item.src),
      dst: keep(item.dst),
    })),
  };
}

export function buildPayload(topology: Topology, topologyName: string, settings: SimSettings) {
  const flows = settings.flows.map((flow, index) => ({
    id: index + 1,
    src: flow.src,
    dst: flow.dst,
    pattern: flow.pattern,
    rate_mbps: flow.rate_mbps,
    packet_bytes: flow.packet_bytes,
    start_s: flow.start_s,
    stop_s: flow.stop_s,
    ...(flow.pattern === "bursty" ? { on_duration_s: flow.on_duration_s, off_duration_s: flow.off_duration_s } : {}),
  }));

  let nextBurstFlowId = flows.length + 1;
  const events = settings.events.map((item) => {
    const time_s = item.time_s;
    switch (item.type) {
      case "LINK_FAILURE":
      case "LINK_RESTORE":
        return { type: item.type, a: item.a, b: item.b, time_s };
      case "ROUTER_FAILURE":
      case "ROUTER_RESTORE":
        return { type: item.type, node: item.node, time_s };
      case "CONGESTION":
        return {
          type: item.type,
          a: item.a,
          b: item.b,
          time_s,
          bandwidth_multiplier: item.bandwidth_multiplier,
          extra_loss_rate: item.extra_loss_rate,
          duration_s: item.duration_s,
        };
      case "TRAFFIC_BURST":
        return {
          type: item.type,
          flow_id: nextBurstFlowId++,
          src: item.src,
          dst: item.dst,
          time_s,
          duration_s: item.duration_s,
          rate_mbps: item.rate_mbps,
          packet_bytes: item.packet_bytes,
        };
    }
  });

  return {
    topology,
    topology_name: topologyName || "uploaded-topology.json",
    duration_s: settings.config.duration_s,
    seed: settings.config.seed,
    algorithm: settings.config.algorithm,
    weight: settings.config.weight,
    reconvergence_delay_ms: settings.config.reconvergence_delay_ms ?? 0,
    flows,
    events,
  };
}
