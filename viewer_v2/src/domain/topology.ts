import type { NodeType, Topology } from "../lib/types";

export const NODE_KINDS: Record<NodeType, { prefix: string; label: string }> = {
  HOST: { prefix: "PC", label: "Host" },
  ROUTER: { prefix: "R", label: "Router" },
  SWITCH: { prefix: "SW", label: "Switch" },
};

export const DEFAULT_LINK = { bandwidthMbps: 100, latencyMs: 2, lossRate: 0, queueCapacity: 64 };

export const CANVAS = { width: 960, height: 560 };

const AUTO_LAYOUT_RADIUS_X = 320;
const AUTO_LAYOUT_RADIUS_Y = 200;
const IP_PREFIX = "10.0.0.";
const LAST_HOST_ADDRESS = 254;

export interface BuilderNode {
  id: number;
  type: NodeType;
  name: string;
  ip: string;
  x: number;
  y: number;
}

export interface BuilderLink {
  uid: number;
  a: number;
  b: number;
  bandwidthMbps: number | string;
  latencyMs: number | string;
  lossRate: number | string;
  queueCapacity: number | string;
}

export interface BuilderModel { nodes: BuilderNode[]; links: BuilderLink[] }

export interface TopologyIssue {
  level: "error" | "warning";
  message: string;
  nodeId?: number;
  linkUid?: number;
}

export const emptyModel = (): BuilderModel => ({ nodes: [], links: [] });

export function nextNodeId(nodes: BuilderNode[]): number {
  return Math.max(0, ...nodes.map((node) => node.id)) + 1;
}

export function nextName(nodes: BuilderNode[], type: NodeType): string {
  const used = new Set(nodes.map((node) => node.name));
  const { prefix } = NODE_KINDS[type];
  for (let n = 1; ; n += 1) {
    if (!used.has(`${prefix}${n}`)) return `${prefix}${n}`;
  }
}

export function nextIp(nodes: BuilderNode[]): string {
  const used = new Set(nodes.map((node) => node.ip));
  for (let n = 1; n <= LAST_HOST_ADDRESS; n += 1) {
    if (!used.has(`${IP_PREFIX}${n}`)) return `${IP_PREFIX}${n}`;
  }
  return "";
}

export function isValidIPv4(text: string): boolean {
  const parts = text.split(".");
  return parts.length === 4 && parts.every((part) => /^\d{1,3}$/.test(part) && Number(part) <= 255);
}

export function addNode(model: BuilderModel, type: NodeType, x: number, y: number): BuilderModel {
  const node: BuilderNode = {
    id: nextNodeId(model.nodes),
    type,
    name: nextName(model.nodes, type),
    ip: nextIp(model.nodes),
    x: Math.round(x),
    y: Math.round(y),
  };
  return { ...model, nodes: [...model.nodes, node] };
}

export function linkExists(model: BuilderModel, a: number, b: number): boolean {
  return model.links.some((link) => (link.a === a && link.b === b) || (link.a === b && link.b === a));
}

export function addLink(model: BuilderModel, a: number, b: number): BuilderModel {
  if (a === b || linkExists(model, a, b)) return model;
  const uid = Math.max(0, ...model.links.map((link) => link.uid)) + 1;
  return { ...model, links: [...model.links, { uid, a, b, ...DEFAULT_LINK }] };
}

export function removeNode(model: BuilderModel, id: number): BuilderModel {
  return {
    nodes: model.nodes.filter((node) => node.id !== id),
    links: model.links.filter((link) => link.a !== id && link.b !== id),
  };
}

export function removeLink(model: BuilderModel, uid: number): BuilderModel {
  return { ...model, links: model.links.filter((link) => link.uid !== uid) };
}

function nameOf(model: BuilderModel, id: number): string {
  return model.nodes.find((node) => node.id === id)?.name || `#${id}`;
}

function isConnected(model: BuilderModel): boolean {
  const first = model.nodes[0];
  if (!first || model.nodes.length < 2) return true;
  const neighbours = new Map<number, number[]>(model.nodes.map((node) => [node.id, []]));
  for (const link of model.links) {
    neighbours.get(link.a)?.push(link.b);
    neighbours.get(link.b)?.push(link.a);
  }
  const seen = new Set([first.id]);
  const toVisit = [first.id];
  while (toVisit.length > 0) {
    const current = toVisit.pop()!;
    for (const next of neighbours.get(current) ?? []) {
      if (!seen.has(next)) {
        seen.add(next);
        toVisit.push(next);
      }
    }
  }
  return seen.size === model.nodes.length;
}

function addError(issues: TopologyIssue[], message: string, extra: Partial<TopologyIssue> = {}): void {
  issues.push({ level: "error", message, ...extra });
}

export function validateTopology(model: BuilderModel): TopologyIssue[] {
  const issues: TopologyIssue[] = [];
  if (model.nodes.length === 0) {
    addError(issues, "Add at least one node.");
    return issues;
  }
  checkNamesAndIps(model, issues);
  checkLinks(model, issues);
  checkHostLinkCounts(model, issues);
  if (model.nodes.filter((node) => node.type === "HOST").length < 2) {
    issues.push({ level: "warning", message: "Add at least two hosts to be able to define traffic flows." });
  }
  if (!isConnected(model)) {
    issues.push({ level: "warning", message: "The topology is not fully connected; some hosts may be unreachable." });
  }
  return issues;
}

function checkNamesAndIps(model: BuilderModel, issues: TopologyIssue[]): void {
  const names = new Set<string>();
  const ips = new Set<string>();
  for (const node of model.nodes) {
    const name = node.name.trim();
    const label = name || `Node ${node.id}`;
    const ip = node.ip.trim();
    const at = { nodeId: node.id };
    if (!name) addError(issues, `Node ${node.id} needs a name.`, at);
    else if (names.has(name)) addError(issues, `Name “${name}” is used by more than one node.`, at);
    if (!isValidIPv4(ip)) addError(issues, `${label}: “${node.ip}” is not a valid IPv4 address.`, at);
    else if (ips.has(ip)) addError(issues, `IP address ${node.ip} is used by more than one node.`, at);
    names.add(name);
    ips.add(ip);
  }
}

function checkLinks(model: BuilderModel, issues: TopologyIssue[]): void {
  for (const link of model.links) {
    const label = `${nameOf(model, link.a)} — ${nameOf(model, link.b)}`;
    const at = { linkUid: link.uid };
    const loss = Number(link.lossRate);
    const queue = Number(link.queueCapacity);
    if (!(Number(link.bandwidthMbps) > 0)) addError(issues, `Link ${label}: bandwidth must be greater than 0.`, at);
    if (!(Number(link.latencyMs) >= 0)) addError(issues, `Link ${label}: latency cannot be negative.`, at);
    if (!(loss >= 0 && loss <= 1)) addError(issues, `Link ${label}: loss rate must be between 0 and 1.`, at);
    if (!Number.isInteger(queue) || queue < 0) {
      addError(issues, `Link ${label}: queue capacity must be a non-negative whole number.`, at);
    }
  }
}

function checkHostLinkCounts(model: BuilderModel, issues: TopologyIssue[]): void {
  const linkCount = new Map<number, number>();
  for (const link of model.links) {
    linkCount.set(link.a, (linkCount.get(link.a) ?? 0) + 1);
    linkCount.set(link.b, (linkCount.get(link.b) ?? 0) + 1);
  }
  for (const node of model.nodes) {
    const count = linkCount.get(node.id) ?? 0;
    if (node.type === "HOST" && count !== 1) {
      const message = `Host ${node.name || node.id} must have exactly one link (it has ${count}).`;
      addError(issues, message, { nodeId: node.id });
    }
  }
}

export function toTopology(model: BuilderModel): Topology {
  return {
    nodes: model.nodes.map((node) => ({
      id: node.id,
      type: node.type,
      name: node.name.trim(),
      ip: node.ip.trim(),
      pos: [node.x, node.y],
    })),
    links: model.links.map((link) => ({
      a: link.a,
      b: link.b,
      bandwidthMbps: Number(link.bandwidthMbps),
      latencyMs: Number(link.latencyMs),
      lossRate: Number(link.lossRate),
      queueCapacity: Number(link.queueCapacity),
    })),
  };
}

export function fromTopology(topology: Topology): BuilderModel {
  const count = topology.nodes.length;
  const nodes = topology.nodes.map((node, index): BuilderNode => {
    const pos = node.pos;
    const hasPos = Array.isArray(pos) && Number.isFinite(Number(pos[0])) && Number.isFinite(Number(pos[1]));
    const angle = -Math.PI / 2 + (2 * Math.PI * index) / Math.max(1, count);
    return {
      id: node.id,
      type: node.type,
      name: String(node.name ?? ""),
      ip: String(node.ip ?? ""),
      x: hasPos ? Number(pos[0]) : Math.round(CANVAS.width / 2 + Math.cos(angle) * AUTO_LAYOUT_RADIUS_X),
      y: hasPos ? Number(pos[1]) : Math.round(CANVAS.height / 2 + Math.sin(angle) * AUTO_LAYOUT_RADIUS_Y),
    };
  });
  const links = topology.links.map((link, index): BuilderLink => ({
    uid: index + 1,
    a: link.a,
    b: link.b,
    bandwidthMbps: link.bandwidthMbps ?? DEFAULT_LINK.bandwidthMbps,
    latencyMs: link.latencyMs ?? DEFAULT_LINK.latencyMs,
    lossRate: link.lossRate ?? DEFAULT_LINK.lossRate,
    queueCapacity: link.queueCapacity ?? DEFAULT_LINK.queueCapacity,
  }));
  return { nodes, links };
}

export function parseTopologyJson(text: string): Topology {
  let parsed: Partial<Topology> | null;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error("The file is not valid JSON.");
  }
  if (!parsed || !Array.isArray(parsed.nodes) || !Array.isArray(parsed.links)) {
    throw new Error("This JSON does not contain topology nodes and links.");
  }
  return parsed as Topology;
}

export function linkNamePairs(topology: Topology): [string, string][] {
  const names = new Map(topology.nodes.map((node) => [node.id, node.name]));
  return topology.links.map((link) => [names.get(link.a) ?? `#${link.a}`, names.get(link.b) ?? `#${link.b}`]);
}
