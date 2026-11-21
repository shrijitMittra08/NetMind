import type { NodeType, Topology } from "../lib/types";

export const NODE_KINDS: Record<NodeType, { prefix: string; label: string }> = {
  HOST: { prefix: "PC", label: "Host" },
  ROUTER: { prefix: "R", label: "Router" },
  SWITCH: { prefix: "SW", label: "Switch" },
};

export const DEFAULT_LINK = {
  bandwidthMbps: 100,
  latencyMs: 2,
  lossRate: 0,
  queueCapacity: 64,
};

export const CANVAS = { width: 960, height: 560 };

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

export interface BuilderModel {
  nodes: BuilderNode[];
  links: BuilderLink[];
}

export interface TopologyIssue {
  level: "error" | "warning";
  message: string;
  nodeId?: number;
  linkUid?: number;
}

export const emptyModel = (): BuilderModel => ({ nodes: [], links: [] });

export function nextNodeId(nodes: BuilderNode[]): number {
  return nodes.reduce((max, node) => Math.max(max, node.id), 0) + 1;
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
  for (let n = 1; n <= 254; n += 1) {
    if (!used.has(`10.0.0.${n}`)) return `10.0.0.${n}`;
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
  const uid = model.links.reduce((max, link) => Math.max(max, link.uid), 0) + 1;
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
  const adjacent = new Map<number, number[]>(model.nodes.map((node) => [node.id, []]));
  for (const link of model.links) {
    adjacent.get(link.a)?.push(link.b);
    adjacent.get(link.b)?.push(link.a);
  }
  const seen = new Set([first.id]);
  const queue = [first.id];
  while (queue.length) {
    for (const next of adjacent.get(queue.pop()!) ?? []) {
      if (!seen.has(next)) {
        seen.add(next);
        queue.push(next);
      }
    }
  }
  return seen.size === model.nodes.length;
}

export function validateTopology(model: BuilderModel): TopologyIssue[] {
  const issues: TopologyIssue[] = [];
  const error = (message: string, extra: Partial<TopologyIssue> = {}) =>
    issues.push({ level: "error", message, ...extra });
  const warn = (message: string) => issues.push({ level: "warning", message });

  if (model.nodes.length === 0) {
    error("Add at least one node.");
    return issues;
  }

  const names = new Set<string>();
  const ips = new Set<string>();
  for (const node of model.nodes) {
    const name = node.name.trim();
    const label = name || `Node ${node.id}`;
    if (!name) error(`Node ${node.id} needs a name.`, { nodeId: node.id });
    else if (names.has(name)) error(`Name “${name}” is used by more than one node.`, { nodeId: node.id });
    names.add(name);

    if (!isValidIPv4(node.ip.trim())) {
      error(`${label}: “${node.ip}” is not a valid IPv4 address.`, { nodeId: node.id });
    } else if (ips.has(node.ip.trim())) {
      error(`IP address ${node.ip} is used by more than one node.`, { nodeId: node.id });
    }
    ips.add(node.ip.trim());
  }

  const degree = new Map<number, number>(model.nodes.map((node) => [node.id, 0]));
  for (const link of model.links) {
    degree.set(link.a, (degree.get(link.a) ?? 0) + 1);
    degree.set(link.b, (degree.get(link.b) ?? 0) + 1);

    const label = `${nameOf(model, link.a)} — ${nameOf(model, link.b)}`;
    const at = { linkUid: link.uid };
    if (!(Number(link.bandwidthMbps) > 0)) error(`Link ${label}: bandwidth must be greater than 0.`, at);
    if (!(Number(link.latencyMs) >= 0)) error(`Link ${label}: latency cannot be negative.`, at);
    const loss = Number(link.lossRate);
    if (!(loss >= 0 && loss <= 1)) error(`Link ${label}: loss rate must be between 0 and 1.`, at);
    const queue = Number(link.queueCapacity);
    if (!Number.isInteger(queue) || queue < 0) {
      error(`Link ${label}: queue capacity must be a non-negative whole number.`, at);
    }
  }

  for (const node of model.nodes) {
    const count = degree.get(node.id) ?? 0;
    if (node.type === "HOST" && count !== 1) {
      error(`Host ${node.name || node.id} must have exactly one link (it has ${count}).`, {
        nodeId: node.id,
      });
    }
  }

  if (model.nodes.filter((node) => node.type === "HOST").length < 2) {
    warn("Add at least two hosts to be able to define traffic flows.");
  }
  if (!isConnected(model)) warn("The topology is not fully connected; some hosts may be unreachable.");
  return issues;
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
      x: hasPos ? Number(pos[0]) : Math.round(CANVAS.width / 2 + Math.cos(angle) * 320),
      y: hasPos ? Number(pos[1]) : Math.round(CANVAS.height / 2 + Math.sin(angle) * 200),
    };
  });
  const links = topology.links.map(
    (link, index): BuilderLink => ({
      uid: index + 1,
      a: link.a,
      b: link.b,
      bandwidthMbps: link.bandwidthMbps ?? DEFAULT_LINK.bandwidthMbps,
      latencyMs: link.latencyMs ?? DEFAULT_LINK.latencyMs,
      lossRate: link.lossRate ?? DEFAULT_LINK.lossRate,
      queueCapacity: link.queueCapacity ?? DEFAULT_LINK.queueCapacity,
    }),
  );
  return { nodes, links };
}

export function parseTopologyJson(text: string): Topology {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error("The file is not valid JSON.");
  }
  const candidate = parsed as Partial<Topology> | null;
  if (!candidate || !Array.isArray(candidate.nodes) || !Array.isArray(candidate.links)) {
    throw new Error("This JSON does not contain topology nodes and links.");
  }
  return candidate as Topology;
}

export function linkNamePairs(topology: Topology): [string, string][] {
  const names = new Map(topology.nodes.map((node) => [node.id, node.name]));
  return topology.links.map((link) => [names.get(link.a) ?? `#${link.a}`, names.get(link.b) ?? `#${link.b}`]);
}
