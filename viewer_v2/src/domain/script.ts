import { ALGORITHMS, WEIGHTS } from "../lib/types";
import type { Algorithm, EventDraft, NodeType, SimConfig, SimSettings, Topology, Weight } from "../lib/types";
import { makeEvent } from "./setup";
import { linkNamePairs } from "./topology";

export const SCRIPT_REFERENCE: [string, string][] = [
  ["set seed <n>", "Random seed (resets the run)"],
  ["set algo <bfs|dfs|dijkstra|bellman-ford>", "Routing algorithm"],
  ["set weight <hops|latency>", "Routing weight"],
  ["add flow <src> <dst> <mbps> <bytes> [start stop] [bursty]", "Traffic flow"],
  ["fail link <A> <B> at <t>", "Link failure"],
  ["restore link <A> <B> at <t>", "Link restore"],
  ["fail router <R> at <t>", "Router failure"],
  ["restore router <R> at <t>", "Router restore"],
  ["congest link <A> <B> factor <f> at <t> for <d>", "Temporary link degradation"],
  ["burst <src> <dst> <mbps> at <t> for <d>", "Temporary traffic burst"],
  ["route add <router> <dst> via <next>", "Static route"],
  ["show topology · show route <R> · show queue <A> <B> · show metrics", "Inspect the network"],
  ["ping <A> <B> [count] · traceroute <A> <B>", "Probe a path"],
  ["run <seconds>", "Advance the simulation"],
  ["step [n] · compare <A> <B> · reset", "Step, compare algorithms, reset"],
  ["export", "Write run files for analysis and replay"],
];

export interface ScriptNote {
  line: number;
  level: "error" | "warning" | "info";
  message: string;
}

export interface ParsedScript {
  settings: SimSettings;
  notes: ScriptNote[];
  hasRun: boolean;
  hasExport: boolean;
}

const TRANSIENT = new Set([
  "show topology", "show route", "show queue", "show metrics", "ping", "traceroute",
  "route add", "step", "compare", "export", "help", "exit",
]);

const MULTI_WORD = [
  "show topology", "show route", "show queue", "show metrics", "set algo", "set weight", "set seed",
  "route add", "add flow", "fail link", "fail router", "restore link", "restore router", "congest link",
];

const SCRIPT_BURST_ON_OFF_S = 0.5;
const DEFAULT_SCRIPT_FLOW_S = 10;
const BURST_PACKET_BYTES = 1000;

export function tokenize(line: string): string[] {
  const tokens: string[] = [];
  let current = "";
  let started = false;
  let quote: string | null = null;
  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i]!;
    if (quote) {
      if (ch === quote) {
        quote = null;
      } else if (ch === "\\" && (line[i + 1] === quote || line[i + 1] === "\\")) {
        current += line[i + 1];
        i += 1;
      } else {
        current += ch;
      }
    } else if (ch === '"' || ch === "'") {
      quote = ch;
      started = true;
    } else if (ch === "#" && !started) {
      break;
    } else if (/\s/.test(ch)) {
      if (started) tokens.push(current);
      current = "";
      started = false;
    } else {
      current += ch;
      started = true;
    }
  }
  if (quote) throw new Error("Unclosed quoted argument");
  if (started) tokens.push(current);
  return tokens;
}

function matchCommand(tokens: string[]): { name: string; args: string[] } {
  const lower = tokens.map((token) => token.toLowerCase());
  for (const name of MULTI_WORD) {
    const words = name.split(" ");
    if (words.every((word, i) => lower[i] === word)) return { name, args: tokens.slice(words.length) };
  }
  return { name: lower[0] ?? "", args: tokens.slice(1) };
}

const toNumber = (text: string | undefined) => (text !== undefined && text !== "" ? Number(text) : NaN);

interface ScriptState {
  settings: SimSettings;
  now: number;
  hasRun: boolean;
  hasExport: boolean;
}

interface LineContext {
  name: string;
  args: string[];
  firstToken: string;
  note: (level: ScriptNote["level"], message: string) => void;
  checkArgCount: (counts: number[], usageText: string) => boolean;
  parseNumbers: (values: (string | undefined)[], what: string) => number[] | null;
  checkHost: (name: string) => boolean;
  checkLink: (a: string, b: string) => boolean;
  checkRouter: (name: string) => boolean;
}

export function parseScript(text: string, topology: Topology | null, base: SimConfig): ParsedScript {
  const typeOf = new Map((topology?.nodes ?? []).map((node) => [node.name, node.type]));
  const linkKeys = new Set<string>();
  for (const [a, b] of topology ? linkNamePairs(topology) : []) {
    linkKeys.add(`${a}|${b}`);
    linkKeys.add(`${b}|${a}`);
  }
  const state: ScriptState = {
    settings: { config: { ...base, reconvergence_delay_ms: 0 }, flows: [], events: [] },
    now: 0,
    hasRun: false,
    hasExport: false,
  };
  const notes: ScriptNote[] = [];
  const isNodeType = (nodeName: string, type: NodeType) => !topology || typeOf.get(nodeName) === type;

  text.split(/\r?\n/).forEach((rawLine, index) => {
    const note = (level: ScriptNote["level"], message: string) => {
      notes.push({ line: index + 1, level, message });
    };
    const check = (ok: boolean, message: string) => {
      if (!ok) note("error", message);
      return ok;
    };

    let tokens: string[];
    try {
      tokens = tokenize(rawLine);
    } catch (error) {
      note("error", (error as Error).message);
      return;
    }
    if (tokens.length === 0) return;

    const { name, args } = matchCommand(tokens);
    runCommand(
      {
        name,
        args,
        firstToken: tokens[0]!,
        note,
        checkArgCount: (counts, usageText) => check(counts.includes(args.length), `Usage: ${usageText}`),
        parseNumbers: (values, what) => {
          const parsed = values.map(toNumber);
          return check(!parsed.some(Number.isNaN), `${what} must be numeric.`) ? parsed : null;
        },
        checkHost: (host) => check(isNodeType(host, "HOST"), `“${host}” is not a host in this topology.`),
        checkLink: (a, b) => check(!topology || linkKeys.has(`${a}|${b}`), `No link between ${a} and ${b}.`),
        checkRouter: (router) => check(isNodeType(router, "ROUTER"), `“${router}” is not a router in this topology.`),
      },
      state,
    );
  });

  return { settings: state.settings, notes, hasRun: state.hasRun, hasExport: state.hasExport };
}

type CommandHandler = (line: LineContext, state: ScriptState) => void;

const COMMANDS = new Map<string, CommandHandler>([
  ["load", (line) => line.note("info", "Ignored: the workbench topology is loaded automatically.")],
  ["set seed", setSeed],
  ["set algo", setAlgorithm],
  ["set weight", setWeight],
  ["add flow", addFlow],
  ["fail link", addLinkEvent],
  ["restore link", addLinkEvent],
  ["fail router", addRouterEvent],
  ["restore router", addRouterEvent],
  ["congest link", addCongestion],
  ["burst", addBurst],
  ["run", runFor],
  ["reset", (_line, state) => clearRun(state)],
]);

function runCommand(line: LineContext, state: ScriptState): void {
  const handler = COMMANDS.get(line.name);
  if (handler) {
    handler(line, state);
  } else if (TRANSIENT.has(line.name)) {
    if (line.name === "export") state.hasExport = true;
    line.note("info", `${line.name} runs in the simulator but is not stored in a scenario.`);
  } else {
    line.note("error", `Unknown command: ${line.firstToken}`);
  }
}

function clearRun(state: ScriptState): void {
  state.settings.flows = [];
  state.settings.events = [];
  state.now = 0;
}

function addEvent(state: ScriptState, fields: Partial<EventDraft> & Pick<EventDraft, "type">): void {
  state.settings.events.push({ ...makeEvent(null, fields.type), ...fields });
}

function setSeed(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([1], "set seed <n>")) return;
  const values = line.parseNumbers([line.args[0]], "Seed");
  if (!values) return;
  if (state.settings.flows.length || state.settings.events.length) {
    line.note("warning", "In the CLI, set seed resets the run, discarding flows and events added before it.");
  }
  state.settings.config.seed = values[0]!;
  clearRun(state);
}

function setAlgorithm(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([1], "set algo <bfs|dfs|dijkstra|bellman-ford>")) return;
  const algorithm = line.args[0]!;
  if ((ALGORITHMS as readonly string[]).includes(algorithm)) state.settings.config.algorithm = algorithm as Algorithm;
  else line.note("error", `Unknown algorithm “${algorithm}”.`);
}

function setWeight(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([1], "set weight <hops|latency>")) return;
  const weight = line.args[0]!;
  if ((WEIGHTS as readonly string[]).includes(weight)) state.settings.config.weight = weight as Weight;
  else line.note("error", `Unknown weight “${weight}”.`);
}

function addFlow(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([4, 6, 7], "add flow <src> <dst> <mbps> <bytes> [start stop] [bursty]")) return;
  const [src = "", dst = "", rate = "", bytes = "", startText = "", stopText = "", patternText = ""] = line.args;
  const isBursty = line.args.length === 7;
  if (isBursty && patternText !== "bursty") {
    line.note("error", "The optional traffic pattern must be “bursty”.");
    return;
  }
  const hasTimes = line.args.length >= 6;
  const values = line.parseNumbers(hasTimes ? [rate, bytes, startText, stopText] : [rate, bytes], "Rate, size and times");
  if (!values || !line.checkHost(src) || !line.checkHost(dst)) return;
  const start = hasTimes ? values[2]! : state.now;
  const stop = hasTimes ? values[3]! : start + DEFAULT_SCRIPT_FLOW_S;
  if (stop <= start || start < state.now) {
    line.note("error", "Flow times must satisfy now <= start < stop.");
    return;
  }
  state.settings.flows.push({
    src,
    dst,
    rate_mbps: values[0]!,
    packet_bytes: values[1]!,
    start_s: start,
    stop_s: stop,
    pattern: isBursty ? "bursty" : "constant",
    on_duration_s: SCRIPT_BURST_ON_OFF_S,
    off_duration_s: SCRIPT_BURST_ON_OFF_S,
  });
}

function addLinkEvent(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([4], `${line.name} <A> <B> at <t>`)) return;
  const [a = "", b = "", atWord = "", timeText = ""] = line.args;
  if (atWord !== "at") {
    line.note("error", "Expected “at <time>”.");
    return;
  }
  const values = line.parseNumbers([timeText], "Event time");
  if (!values || !line.checkLink(a, b)) return;
  const type = line.name === "fail link" ? "LINK_FAILURE" : "LINK_RESTORE";
  addEvent(state, { type, a, b, time_s: values[0]! });
}

function addRouterEvent(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([3], `${line.name} <R> at <t>`)) return;
  const [router = "", atWord = "", timeText = ""] = line.args;
  if (atWord !== "at") {
    line.note("error", "Expected “at <time>”.");
    return;
  }
  const values = line.parseNumbers([timeText], "Event time");
  if (!values || !line.checkRouter(router)) return;
  const type = line.name === "fail router" ? "ROUTER_FAILURE" : "ROUTER_RESTORE";
  addEvent(state, { type, node: router, time_s: values[0]! });
}

function addCongestion(line: LineContext, state: ScriptState): void {
  const usageText = "congest link <A> <B> factor <f> at <t> for <d>";
  if (!line.checkArgCount([8], usageText)) return;
  const [a = "", b = "", factorWord = "", factor = "", atWord = "", time = "", forWord = "", duration = ""] = line.args;
  if (factorWord !== "factor" || atWord !== "at" || forWord !== "for") {
    line.note("error", `Usage: ${usageText}`);
    return;
  }
  const values = line.parseNumbers([factor, time, duration], "Factor, time and duration");
  if (!values || !line.checkLink(a, b)) return;
  addEvent(state, {
    type: "CONGESTION",
    a,
    b,
    bandwidth_multiplier: values[0]!,
    time_s: values[1]!,
    duration_s: values[2]!,
    extra_loss_rate: 0,
  });
}

function addBurst(line: LineContext, state: ScriptState): void {
  const usageText = "burst <src> <dst> <mbps> at <t> for <d>";
  if (!line.checkArgCount([7], usageText)) return;
  const [src = "", dst = "", rate = "", atWord = "", time = "", forWord = "", duration = ""] = line.args;
  if (atWord !== "at" || forWord !== "for") {
    line.note("error", `Usage: ${usageText}`);
    return;
  }
  const values = line.parseNumbers([rate, time, duration], "Rate, time and duration");
  if (!values || !line.checkHost(src) || !line.checkHost(dst)) return;
  addEvent(state, {
    type: "TRAFFIC_BURST",
    src,
    dst,
    rate_mbps: values[0]!,
    time_s: values[1]!,
    duration_s: values[2]!,
    packet_bytes: BURST_PACKET_BYTES,
  });
}

function runFor(line: LineContext, state: ScriptState): void {
  if (!line.checkArgCount([1], "run <seconds>")) return;
  const values = line.parseNumbers([line.args[0]], "Duration");
  if (!values) return;
  state.now += values[0]!;
  state.hasRun = true;
  state.settings.config.duration_s = state.now;
}

export function generateScript(settings: SimSettings): string {
  const { config, flows, events } = settings;
  const lines = [
    "# NetMind script, generated from the interactive settings.",
    "# Lines starting with # are comments.",
    `set seed ${config.seed}`,
    `set algo ${config.algorithm}`,
    `set weight ${config.weight}`,
  ];
  flows.forEach((flow, i) => {
    if (!flow.src || !flow.dst) return;
    if (flow.pattern === "poisson") {
      lines.push(`# Flow ${i + 1}: Poisson traffic is not available in scripts, skipped.`);
      return;
    }
    const isBursty = flow.pattern === "bursty";
    if (isBursty && (flow.on_duration_s !== SCRIPT_BURST_ON_OFF_S || flow.off_duration_s !== SCRIPT_BURST_ON_OFF_S)) {
      lines.push(`# Flow ${i + 1}: scripts use fixed 0.5 s on / 0.5 s off bursts.`);
    }
    const times = `${flow.start_s} ${flow.stop_s}${isBursty ? " bursty" : ""}`;
    lines.push(`add flow ${flow.src} ${flow.dst} ${flow.rate_mbps} ${flow.packet_bytes} ${times}`);
  });
  for (const item of events) {
    const t = item.time_s;
    switch (item.type) {
      case "LINK_FAILURE":
        if (item.a && item.b) lines.push(`fail link ${item.a} ${item.b} at ${t}`);
        break;
      case "LINK_RESTORE":
        if (item.a && item.b) lines.push(`restore link ${item.a} ${item.b} at ${t}`);
        break;
      case "ROUTER_FAILURE":
        if (item.node) lines.push(`fail router ${item.node} at ${t}`);
        break;
      case "ROUTER_RESTORE":
        if (item.node) lines.push(`restore router ${item.node} at ${t}`);
        break;
      case "CONGESTION":
        if (!item.a || !item.b) break;
        if (item.extra_loss_rate > 0) {
          lines.push("# Congestion extra loss is not available in scripts, only the bandwidth factor.");
        }
        lines.push(`congest link ${item.a} ${item.b} factor ${item.bandwidth_multiplier} at ${t} for ${item.duration_s}`);
        break;
      case "TRAFFIC_BURST":
        if (!item.src || !item.dst) break;
        lines.push(`burst ${item.src} ${item.dst} ${item.rate_mbps} at ${t} for ${item.duration_s}`);
        break;
    }
  }
  lines.push(`run ${config.duration_s}`, "show metrics", "export");
  return `${lines.join("\n")}\n`;
}
