import type { RunFile, Topology } from "../lib/types";
import { num, parseCsv, parseDetail } from "./csv";

export interface TraceEvent {
  time: number;
  type: string;
  packetId: string;
  flowId: string;
  location: string;
  detail: string;
  order: number;
}

export interface Segment {
  packetId: string;
  flowId: string;
  from: string;
  to: string;
  tStart: number;
  tEnd: number;
}

export interface Drop { time: number; location: string; reason: string }

export type ElementState = "down" | "degraded";

export interface StateInterval { id: string; state: ElementState; start: number; end: number }

export interface WindowRow {
  t_start: number;
  avg_latency_ms: number;
  throughput_mbps: number;
  loss_pct: number;
  max_util_pct: number;
  sent: number;
  delivered: number;
  dropped: number;
}

export interface FlowStat {
  flowId: string;
  src: string;
  dst: string;
  sent: number;
  delivered: number;
  dropped: number;
  lossPct: number;
  throughputMbps: number;
  avgLatencyMs: number;
  avgHops: number;
}

export interface Incident { time: number; kind: "failure" | "degraded" | "recovery" | "route"; label: string }

export interface ReplayModel {
  runId: string;
  algorithm: string;
  weight: string;
  seed: string;
  topology: Topology;
  traceRows: number;
  events: TraceEvent[];
  segments: Segment[];
  maxSegmentDuration: number;
  drops: Drop[];
  linkIntervals: StateInterval[];
  routerIntervals: StateInterval[];
  windows: WindowRow[];
  flows: FlowStat[];
  utilization: Map<string, number>;
  incidents: Incident[];
  endTime: number;
}

const PACKET_EVENTS = new Set(["ENQUEUE", "TX_START", "ARRIVE", "FORWARD", "SENT", "DELIVERED"]);

type PendingSegment = Segment & { eta: number };

function intervalsFrom(events: TraceEvent[], starts: Record<string, ElementState>, ends: Set<string>, endTime: number) {
  const open = new Map<string, { state: ElementState; start: number }>();
  const intervals: StateInterval[] = [];
  const close = (id: string, time: number) => {
    const current = open.get(id);
    if (current) {
      intervals.push({ id, state: current.state, start: current.start, end: time });
      open.delete(id);
    }
  };
  for (const event of events) {
    const state = starts[event.type];
    if (state) {
      close(event.location, event.time);
      open.set(event.location, { state, start: event.time });
    } else if (ends.has(event.type)) {
      close(event.location, event.time);
    }
  }
  for (const id of [...open.keys()]) close(id, endTime);
  return intervals;
}

function readTrace(traceText: string) {
  const events: TraceEvent[] = [];
  const segments: Segment[] = [];
  const drops: Drop[] = [];
  const pending = new Map<string, PendingSegment[]>();
  let endTime = 0;
  let traceRows = 0;

  parseCsv(traceText).forEach((row, order) => {
    const time = num(row.time_s);
    if (!Number.isFinite(time)) return;
    traceRows += 1;
    endTime = Math.max(endTime, time);
    const type = row.event_type ?? "";
    const packetId = row.packet_id ?? "-";
    const location = row.location ?? "";
    const detail = row.detail ?? "";
    const hasPacket = packetId !== "-" && packetId !== "";

    if (type === "TX_START" && hasPacket) {
      const [from, to] = location.split(">");
      if (from && to) {
        const list = pending.get(packetId) ?? [];
        const eta = num(parseDetail(detail).eta);
        list.push({ packetId, flowId: row.flow_id ?? "", from, to, tStart: time, tEnd: time, eta });
        pending.set(packetId, list);
      }
    } else if (type === "ARRIVE" && hasPacket) {
      const list = pending.get(packetId) ?? [];
      const { from } = parseDetail(detail);
      const index = list.findIndex((seg) => seg.from === from && seg.to === location && seg.tStart <= time);
      if (index >= 0) {
        const [seg] = list.splice(index, 1);
        segments.push({ ...seg!, tEnd: time });
      }
    } else if (type === "DROPPED") {
      drops.push({ time, location, reason: parseDetail(detail).reason ?? "unknown" });
      const list = hasPacket ? pending.get(packetId) : undefined;
      if (list && list.length > 0) {
        const seg = list.pop()!;
        if (seg.tStart <= time) segments.push({ ...seg, tEnd: time });
      }
    }

    if (!PACKET_EVENTS.has(type)) {
      events.push({ time, type, packetId, flowId: row.flow_id ?? "-", location, detail, order });
    }
  });

  for (const list of pending.values()) {
    for (const seg of list) {
      const etaIsUsable = Number.isFinite(seg.eta) && seg.eta >= seg.tStart;
      segments.push({ ...seg, tEnd: etaIsUsable ? seg.eta : seg.tStart });
    }
  }
  segments.sort((a, b) => a.tStart - b.tStart || a.tEnd - b.tEnd);
  events.sort((a, b) => a.time - b.time || a.order - b.order);
  return { events, segments, drops, traceRows, endTime };
}

function readWindows(windowsText: string): WindowRow[] {
  return parseCsv(windowsText).map((row) => ({
    t_start: num(row.t_start),
    avg_latency_ms: num(row.avg_latency_ms),
    throughput_mbps: num(row.throughput_mbps),
    loss_pct: num(row.loss_pct),
    max_util_pct: num(row.max_util_pct),
    sent: num(row.sent),
    delivered: num(row.delivered),
    dropped: num(row.dropped),
  }));
}

function readFlows(flowsText: string, nodeNameById: Map<string, string>): FlowStat[] {
  const nameOf = (id: string | undefined) => nodeNameById.get(id ?? "") ?? id ?? "";
  return parseCsv(flowsText).map((row) => ({
    flowId: row.flow_id ?? "",
    src: nameOf(row.src),
    dst: nameOf(row.dst),
    sent: num(row.sent),
    delivered: num(row.delivered),
    dropped: num(row.dropped),
    lossPct: num(row.loss_pct),
    throughputMbps: num(row.throughput_mbps),
    avgLatencyMs: num(row.avg_latency_ms),
    avgHops: num(row.avg_hops),
  }));
}

function readUtilization(linksText: string): Map<string, number> {
  const utilization = new Map<string, number>();
  for (const row of parseCsv(linksText)) {
    const value = num(row.utilization_pct);
    if (row.channel && Number.isFinite(value)) utilization.set(row.channel, value);
  }
  return utilization;
}

const INCIDENT_KINDS = new Map<string, [Incident["kind"], string]>([
  ["LINK_DOWN", ["failure", "down"]],
  ["LINK_DEGRADED", ["degraded", "degraded"]],
  ["LINK_UP", ["recovery", "up"]],
  ["ROUTER_DOWN", ["failure", "down"]],
  ["ROUTER_UP", ["recovery", "up"]],
]);

function buildIncidents(events: TraceEvent[], topology: Topology, nodeNameById: Map<string, string>): Incident[] {
  const linkLabel = (id: string) => {
    const link = topology.links[Number(id) - 1];
    if (!link) return `link ${id}`;
    return `${nodeNameById.get(String(link.a))}–${nodeNameById.get(String(link.b))}`;
  };
  const incidents: Incident[] = [];
  for (const event of events) {
    const incident = INCIDENT_KINDS.get(event.type);
    if (!incident) continue;
    const [kind, change] = incident;
    const routerName = nodeNameById.get(event.location) ?? event.location;
    const name = event.type.startsWith("LINK_") ? linkLabel(event.location) : routerName;
    incidents.push({ time: event.time, kind, label: `${name} ${change}` });
  }
  return incidents;
}

export function buildReplayModel(files: RunFile[], runId: string): ReplayModel {
  const byName = new Map(files.map((file) => [file.name, file.content]));
  const need = (name: string) => {
    const content = byName.get(name);
    if (content === undefined) throw new Error(`Run file ${name} is missing.`);
    return content;
  };

  const topology = JSON.parse(need(`topology_${runId}.json`)) as Topology;
  const manifest = JSON.parse(need(`${runId}.json`)) as Record<string, unknown>;
  const nodeNameById = new Map(topology.nodes.map((node) => [String(node.id), node.name]));
  const trace = readTrace(need(`${runId}_trace.csv`));
  const windows = readWindows(need(`${runId}_windows.csv`));

  let endTime = trace.endTime;
  for (const row of windows) {
    if (Number.isFinite(row.t_start)) endTime = Math.max(endTime, row.t_start);
  }

  const flows = readFlows(byName.get(`${runId}_flows.csv`) ?? "", nodeNameById);
  const utilization = readUtilization(byName.get(`${runId}_links.csv`) ?? "");

  const linkStarts: Record<string, ElementState> = { LINK_DOWN: "down", LINK_DEGRADED: "degraded" };
  const linkIntervals = intervalsFrom(trace.events, linkStarts, new Set(["LINK_UP"]), endTime);
  const routerIntervals = intervalsFrom(trace.events, { ROUTER_DOWN: "down" }, new Set(["ROUTER_UP"]), endTime);
  const incidents = buildIncidents(trace.events, topology, nodeNameById);

  let maxSegmentDuration = 0;
  for (const seg of trace.segments) maxSegmentDuration = Math.max(maxSegmentDuration, seg.tEnd - seg.tStart);

  return {
    runId,
    algorithm: String(manifest.algorithm ?? "unknown"),
    weight: String(manifest.weight ?? "unknown"),
    seed: String(manifest.seed ?? "unknown"),
    topology,
    traceRows: trace.traceRows,
    events: trace.events,
    segments: trace.segments,
    maxSegmentDuration,
    drops: trace.drops,
    linkIntervals, routerIntervals, windows, flows, utilization, incidents, endTime,
  };
}

export const PACKET_LINGER_S = 0.02;

const MIN_PACKET_OPACITY = 0.25;

const DROP_JUMP_GAP_S = 0.25;

export function packetsAt(model: ReplayModel, time: number): { seg: Segment; fraction: number; opacity: number }[] {
  const { segments } = model;
  const earliest = time - model.maxSegmentDuration - PACKET_LINGER_S;
  let low = 0;
  let high = segments.length;
  while (low < high) {
    const middle = (low + high) >> 1;
    if (segments[middle]!.tStart < earliest) low = middle + 1;
    else high = middle;
  }
  const latest = new Map<string, Segment>();
  for (let i = low; i < segments.length; i += 1) {
    const seg = segments[i]!;
    if (seg.tStart > time) break;
    if (time > seg.tEnd + PACKET_LINGER_S) continue;
    const existing = latest.get(seg.packetId);
    if (!existing || seg.tStart >= existing.tStart) latest.set(seg.packetId, seg);
  }
  const visible: { seg: Segment; fraction: number; opacity: number }[] = [];
  for (const seg of latest.values()) {
    const duration = seg.tEnd - seg.tStart;
    const fraction = duration <= 0 ? 1 : Math.min(1, Math.max(0, (time - seg.tStart) / duration));
    const linger = Math.max(0, time - seg.tEnd);
    visible.push({ seg, fraction, opacity: Math.max(MIN_PACKET_OPACITY, 1 - linger / PACKET_LINGER_S) });
  }
  return visible;
}

export function stateAt(intervals: StateInterval[], time: number): Map<string, ElementState> {
  const states = new Map<string, ElementState>();
  for (const interval of intervals) {
    if (interval.start <= time && time < interval.end) states.set(interval.id, interval.state);
  }
  return states;
}

export function jumpTargets(model: ReplayModel): number[] {
  const times = model.incidents.map((incident) => incident.time);
  let lastDrop = -Infinity;
  for (const drop of model.drops) {
    if (drop.time - lastDrop > DROP_JUMP_GAP_S) times.push(drop.time);
    lastDrop = drop.time;
  }
  return [...new Set(times)].sort((a, b) => a - b);
}

export function dropHistogram(model: ReplayModel, buckets: number): number[] {
  const counts = new Array<number>(buckets).fill(0);
  if (model.endTime <= 0) return counts;
  for (const drop of model.drops) {
    const index = Math.min(buckets - 1, Math.floor((drop.time / model.endTime) * buckets));
    counts[index] = (counts[index] ?? 0) + 1;
  }
  return counts;
}

export function layoutTopology(topology: Topology): Map<number, { x: number; y: number }> {
  const positions = new Map<number, { x: number; y: number }>();
  const { nodes } = topology;
  const allPlaced = nodes.length > 0 && nodes.every((node) => Array.isArray(node.pos) && node.pos.every(Number.isFinite));
  if (allPlaced) {
    const xs = nodes.map((node) => node.pos![0]);
    const ys = nodes.map((node) => node.pos![1]);
    const minX = Math.min(...xs);
    const maxX = Math.max(...xs);
    const minY = Math.min(...ys);
    const maxY = Math.max(...ys);
    for (const node of nodes) {
      positions.set(node.id, {
        x: maxX === minX ? 0.5 : (node.pos![0] - minX) / (maxX - minX),
        y: maxY === minY ? 0.5 : (node.pos![1] - minY) / (maxY - minY),
      });
    }
  } else {
    nodes.forEach((node, index) => {
      const angle = -Math.PI / 2 + (2 * Math.PI * index) / Math.max(1, nodes.length);
      positions.set(node.id, { x: 0.5 + Math.cos(angle) / 2, y: 0.5 + Math.sin(angle) / 2 });
    });
  }
  return positions;
}
