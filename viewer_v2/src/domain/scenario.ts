import { ALGORITHMS, EVENT_TYPES, WEIGHTS } from "../lib/types";
import type { Algorithm, EventDraft, EventType, FlowDraft, FlowPattern } from "../lib/types";
import type { SimConfig, SimSettings, Topology, Weight } from "../lib/types";
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

const FLOW_PATTERNS: string[] = ["constant", "bursty", "poisson"];

const isObject = (value: unknown): value is Json => typeof value === "object" && value !== null && !Array.isArray(value);

function readNumber(source: Json, key: string, fallback: number, warnings: string[], label: string): number {
  const value = source[key];
  if (value === undefined) return fallback;
  const parsed = Number(value);
  if (Number.isFinite(parsed)) return parsed;
  warnings.push(`${label}: "${key}" is not a number; using ${fallback}.`);
  return fallback;
}

function readText(source: Json, key: string, fallback = ""): string {
  const value = source[key];
  return typeof value === "string" ? value : fallback;
}

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

  const name = readText(parsed, "name") || fileName.replace(/\.json$/i, "");
  const { settings, warnings } = scenarioSettings(parsed, topology);
  return { name, topology, topologyPath, document: parsed, settings, warnings };
}

export function scenarioSettings(document: Json, topology: Topology | null): { settings: SimSettings; warnings: string[] } {
  const warnings: string[] = [];
  const config = readConfig(document, warnings);
  if (!Array.isArray(document.flows)) warnings.push("The scenario has no flows array; add flows before running.");
  const flows = readFlows(document, topology, config.duration_s, warnings);
  const events = readEvents(document, topology, warnings);
  return { settings: { config, flows, events }, warnings };
}

function readConfig(document: Json, warnings: string[]): SimConfig {
  const routing = isObject(document.routing) ? document.routing : document;
  const algorithmText = readText(routing, "algorithm", DEFAULT_CONFIG.algorithm);
  const weightText = readText(routing, "weight", DEFAULT_CONFIG.weight);
  const isAlgorithm = (ALGORITHMS as readonly string[]).includes(algorithmText);
  const isWeight = (WEIGHTS as readonly string[]).includes(weightText);
  const algorithm = isAlgorithm ? (algorithmText as Algorithm) : DEFAULT_CONFIG.algorithm;
  const weight = isWeight ? (weightText as Weight) : DEFAULT_CONFIG.weight;
  if (algorithm !== algorithmText) warnings.push(`Unknown routing algorithm "${algorithmText}"; using ${algorithm}.`);
  if (weight !== weightText) warnings.push(`Unknown routing weight "${weightText}"; using ${weight}.`);
  return {
    duration_s: readNumber(document, "duration_s", DEFAULT_CONFIG.duration_s, warnings, "Scenario"),
    seed: Math.round(readNumber(document, "seed", DEFAULT_CONFIG.seed, warnings, "Scenario")),
    algorithm,
    weight,
    reconvergence_delay_ms: readNumber(routing, "reconvergence_delay_ms", 0, warnings, "Routing"),
  };
}

function readFlows(document: Json, topology: Topology | null, durationS: number, warnings: string[]): FlowDraft[] {
  const rawFlows = Array.isArray(document.flows) ? document.flows : [];
  return rawFlows.filter(isObject).map((flow, index) => {
    const label = `Flow ${index + 1}`;
    const defaults = makeFlow(topology);
    const patternText = readText(flow, "pattern", "constant");
    const pattern = FLOW_PATTERNS.includes(patternText) ? (patternText as FlowPattern) : "constant";
    if (pattern !== patternText) warnings.push(`${label}: unknown pattern "${patternText}"; using constant.`);
    return {
      src: readText(flow, "src"),
      dst: readText(flow, "dst"),
      rate_mbps: readNumber(flow, "rate_mbps", defaults.rate_mbps, warnings, label),
      packet_bytes: readNumber(flow, "packet_bytes", defaults.packet_bytes, warnings, label),
      start_s: readNumber(flow, "start_s", 0, warnings, label),
      stop_s: readNumber(flow, "stop_s", durationS, warnings, label),
      pattern,
      on_duration_s: readNumber(flow, "on_duration_s", defaults.on_duration_s, warnings, label),
      off_duration_s: readNumber(flow, "off_duration_s", defaults.off_duration_s, warnings, label),
    };
  });
}

function readEvents(document: Json, topology: Topology | null, warnings: string[]): EventDraft[] {
  const rawEvents = Array.isArray(document.events) ? document.events : [];
  const events: EventDraft[] = [];
  rawEvents.filter(isObject).forEach((item, index) => {
    const label = `Event ${index + 1}`;
    const type = readText(item, "type");
    if (!(EVENT_TYPES as readonly string[]).includes(type)) {
      warnings.push(`${label}: unknown event type "${type}" was skipped.`);
      return;
    }
    const defaults = makeEvent(topology, type as EventType);
    events.push({
      ...defaults,
      time_s: readNumber(item, "time_s", defaults.time_s, warnings, label),
      a: readText(item, "a"),
      b: readText(item, "b"),
      node: readText(item, "node"),
      src: readText(item, "src"),
      dst: readText(item, "dst"),
      bandwidth_multiplier: readNumber(item, "bandwidth_multiplier", defaults.bandwidth_multiplier, warnings, label),
      extra_loss_rate: readNumber(item, "extra_loss_rate", defaults.extra_loss_rate, warnings, label),
      duration_s: readNumber(item, "duration_s", defaults.duration_s, warnings, label),
      rate_mbps: readNumber(item, "rate_mbps", defaults.rate_mbps, warnings, label),
      packet_bytes: readNumber(item, "packet_bytes", defaults.packet_bytes, warnings, label),
    });
  });
  return events;
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
