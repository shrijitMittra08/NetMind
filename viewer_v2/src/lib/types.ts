export type NodeType = "HOST" | "ROUTER" | "SWITCH";

export interface TopologyNode {
  id: number;
  type: NodeType;
  name: string;
  ip: string;
  pos?: [number, number];
}

export interface TopologyLink {
  a: number;
  b: number;
  bandwidthMbps: number;
  latencyMs: number;
  lossRate: number;
  queueCapacity?: number;
}

export interface Topology {
  nodes: TopologyNode[];
  links: TopologyLink[];
}

export const ALGORITHMS = ["bfs", "dfs", "dijkstra", "bellman-ford"] as const;
export type Algorithm = (typeof ALGORITHMS)[number];

export const WEIGHTS = ["hops", "latency"] as const;
export type Weight = (typeof WEIGHTS)[number];

export interface SimConfig {
  duration_s: number;
  seed: number;
  algorithm: Algorithm;
  weight: Weight;
  reconvergence_delay_ms?: number;
}

export type FlowPattern = "constant" | "bursty" | "poisson";

export interface FlowDraft {
  src: string;
  dst: string;
  rate_mbps: number;
  packet_bytes: number;
  start_s: number;
  stop_s: number;
  pattern: FlowPattern;
  on_duration_s: number;
  off_duration_s: number;
}

export const EVENT_TYPES = [
  "LINK_FAILURE",
  "LINK_RESTORE",
  "ROUTER_FAILURE",
  "ROUTER_RESTORE",
  "CONGESTION",
  "TRAFFIC_BURST",
] as const;
export type EventType = (typeof EVENT_TYPES)[number];

export interface EventDraft {
  type: EventType;
  time_s: number;
  a: string;
  b: string;
  node: string;
  bandwidth_multiplier: number;
  extra_loss_rate: number;
  duration_s: number;
  src: string;
  dst: string;
  rate_mbps: number;
  packet_bytes: number;
}

export interface SimSettings {
  config: SimConfig;
  flows: FlowDraft[];
  events: EventDraft[];
}

export interface RunSummary {
  simulation_time_s: number | null;
  reliability_pct: number | null;
  throughput_mbps: number | null;
  latency_ms: number | null;
  loss_pct: number | null;
  drops: number;
  events: number;
  recovery_time_s: number | null;
  avg_hops: number | null;
}

export type BaselineMetricName = "avg_latency_ms" | "throughput_mbps" | "loss_pct";

export interface BaselineMetric {
  name: BaselineMetricName;
  actual: number | null;
  expected: number | null;
  delta: number | null;
  deviation_pct: number | null;
  flagged: boolean;
}

export type Baseline = { metrics: BaselineMetric[] } | { error: string };

export interface RunResult {
  run_id: string;
  algorithm: string;
  summary: RunSummary;
  baseline: Baseline;
}

export interface TranscriptEntry {
  line: number;
  command: string;
  output: string;
  ok: boolean;
}

export interface ScriptRunResult {
  ok: boolean;
  transcript: TranscriptEntry[];
  notes: string[];
  runs: RunResult[];
}

export interface GraphRagFact {
  id?: string;
  text: string;
}

export interface GraphRagAnswer {
  intent: string;
  answer: string;
  facts?: GraphRagFact[];
  engine?: "agent" | "rules";
  verified?: boolean;
  fallback_reason?: string;
  tool_calls?: { name: string; args: Record<string, unknown> }[];
}

export interface GraphRagStatus {
  engine: "agent" | "rules";
  model: string | null;
  provider: string | null;
}

export interface ChatTurn {
  question: string;
  answer: string;
}

export interface RunFile {
  name: string;
  type: string;
  content: string;
}

export interface SavedScenario {
  scenario_path: string;
  topology_path: string;
}
