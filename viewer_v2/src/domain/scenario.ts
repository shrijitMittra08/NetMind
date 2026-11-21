import { ALGORITHMS, EVENT_TYPES, WEIGHTS } from "../lib/types";
import type { Algorithm, EventDraft, EventType, FlowDraft, FlowPattern, SimSettings, Topology, Weight } from "../lib/types";
import { DEFAULT_CONFIG, buildPayload, makeEvent, makeFlow } from "./setup";
import { parseTopologyJson } from "./topology";

export interface ParsedScenario {
  name: string;
  topology: Topology | null;
  topologyPath: string;
  document: Record<string, unknown>;
  settings: SimSettings;
  warnings: string[];
}

type Json = Record<string, unknown>;

const isObject = (value: unknown): value is Json => typeof value === "object" && value !== null && !Array.isArray(value);

function number(source: Json, key: string, fallback: number, warnings: string[], label: string): number {
  const value = source[key];
  if (value === undefined) return fallback;
  const parsed = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(parsed)) {
    warnings.push(`${label}: "${key}" is not a number; using ${fallback}.`);
    return fallback;
  }
  return parsed;
}

const text = (source: Json, key: string, fallback = "") =>
  typeof source[key] === "string" ? (source[key] as string) : fallback;

export function parseScenarioJson(raw: string, fileName = "scenario.json"): ParsedScenario {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    throw new Error("The file is not valid JSON.");
  }
  if (!isObject(parsed)) throw new Error("A scenario file must be a JSON object.");
  if (Array.isArray(parsed.nodes) && Array.isArray(parsed.links) && !("topology" in parsed)) {
    throw new Error("This is a topology file, not a scenario. Import it under “Import topology”.");
  }

  let topology: Topology | null = null;
  let topologyPath = "";
  if (isObject(parsed.topology)) {
    topology = parseTopologyJson(JSON.stringify(parsed.topology));
  } else if (typeof parsed.topology === "string" && parsed.topology.trim()) {
    topologyPath = parsed.topology.trim();
  } else {
    throw new Error("The scenario has no topology: expected a topology object or a path to one.");
  }

  const name = text(parsed, "name") || fileName.replace(/\.json$/i, "");
  const { settings, warnings } = scenarioSettings(parsed, topology);
  return { name, topology, topologyPath, document: parsed, settings, warnings };
}

export function scenarioSettings(document: Json, topology: Topology | null): { settings: SimSettings; warnings: string[] } {
  const warnings: string[] = [];
  const routing = isObject(document.routing) ? document.routing : document;

  const algorithmText = text(routing, "algorithm", DEFAULT_CONFIG.algorithm);
  const weightText = text(routing, "weight", DEFAULT_CONFIG.weight);
  const algorithm = (ALGORITHMS as readonly string[]).includes(algorithmText) ? (algorithmText as Algorithm) : DEFAULT_CONFIG.algorithm;
  const weight = (WEIGHTS as readonly string[]).includes(weightText) ? (weightText as Weight) : DEFAULT_CONFIG.weight;
  if (algorithm !== algorithmText) warnings.push(`Unknown routing algorithm "${algorithmText}"; using ${algorithm}.`);
  if (weight !== weightText) warnings.push(`Unknown routing weight "${weightText}"; using ${weight}.`);

  const config = {
    duration_s: number(document, "duration_s", DEFAULT_CONFIG.duration_s, warnings, "Scenario"),
    seed: Math.round(number(document, "seed", DEFAULT_CONFIG.seed, warnings, "Scenario")),
    algorithm,
    weight,
    reconvergence_delay_ms: number(routing, "reconvergence_delay_ms", 0, warnings, "Routing"),
  };

  const rawFlows = Array.isArray(document.flows) ? document.flows : [];
  if (!Array.isArray(document.flows)) warnings.push("The scenario has no flows array; add flows before running.");
  const flows: FlowDraft[] = rawFlows.filter(isObject).map((flow, index) => {
    const label = `Flow ${index + 1}`;
    const base = makeFlow(topology);
    const patternText = text(flow, "pattern", "constant");
    const pattern: FlowPattern = ["constant", "bursty", "poisson"].includes(patternText) ? (patternText as FlowPattern) : "constant";
    if (pattern !== patternText) warnings.push(`${label}: unknown pattern "${patternText}"; using constant.`);
    return {
      src: text(flow, "src"),
      dst: text(flow, "dst"),
      rate_mbps: number(flow, "rate_mbps", base.rate_mbps, warnings, label),
      packet_bytes: number(flow, "packet_bytes", base.packet_bytes, warnings, label),
      start_s: number(flow, "start_s", 0, warnings, label),
      stop_s: number(flow, "stop_s", config.duration_s, warnings, label),
      pattern,
      on_duration_s: number(flow, "on_duration_s", base.on_duration_s, warnings, label),
      off_duration_s: number(flow, "off_duration_s", base.off_duration_s, warnings, label),
    };
  });

  const rawEvents = Array.isArray(document.events) ? document.events : [];
  const events: EventDraft[] = [];
  rawEvents.filter(isObject).forEach((item, index) => {
    const label = `Event ${index + 1}`;
    const type = text(item, "type");
    if (!(EVENT_TYPES as readonly string[]).includes(type)) {
      warnings.push(`${label}: unknown event type "${type}" was skipped.`);
      return;
    }
    const base = { ...makeEvent(topology, type as EventType), a: "", b: "", node: "", src: "", dst: "" };
    events.push({
      ...base,
      time_s: number(item, "time_s", base.time_s, warnings, label),
      a: text(item, "a"),
      b: text(item, "b"),
      node: text(item, "node"),
      src: text(item, "src"),
      dst: text(item, "dst"),
      bandwidth_multiplier: number(item, "bandwidth_multiplier", base.bandwidth_multiplier, warnings, label),
      extra_loss_rate: number(item, "extra_loss_rate", base.extra_loss_rate, warnings, label),
      duration_s: number(item, "duration_s", base.duration_s, warnings, label),
      rate_mbps: number(item, "rate_mbps", base.rate_mbps, warnings, label),
      packet_bytes: number(item, "packet_bytes", base.packet_bytes, warnings, label),
    });
  });

  return { settings: { config, flows, events }, warnings };
}

export function buildScenarioDocument(topology: Topology, topologyName: string, settings: SimSettings, name: string) {
  const payload = buildPayload(topology, topologyName, settings);
  return {
    name,
    topology,
    seed: payload.seed,
    duration_s: payload.duration_s,
    routing: {
      algorithm: payload.algorithm,
      weight: payload.weight,
      reconvergence_delay_ms: payload.reconvergence_delay_ms,
    },
    flows: payload.flows,
    events: payload.events,
  };
}
