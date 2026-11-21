import { ALGORITHMS, WEIGHTS } from "../lib/types";
import type { Algorithm, EventDraft, SimConfig, SimSettings, Topology, Weight } from "../lib/types";
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
  "show topology",
  "show route",
  "show queue",
  "show metrics",
  "ping",
  "traceroute",
  "route add",
  "step",
  "compare",
  "export",
  "help",
  "exit",
]);

const MULTI_WORD = [
  "show topology",
  "show route",
  "show queue",
  "show metrics",
  "set algo",
  "set weight",
  "set seed",
  "route add",
  "add flow",
  "fail link",
  "fail router",
  "restore link",
  "restore router",
  "congest link",
];

export function tokenize(line: string): string[] {
  const tokens: string[] = [];
  let current = "";
  let started = false;
  let quote: string | null = null;
  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i]!;
    if (quote) {
      if (ch === quote) quote = null;
      else if (ch === "\\" && (line[i + 1] === quote || line[i + 1] === "\\")) {
        current += line[i + 1];
        i += 1;
      } else current += ch;
      continue;
    }
    if (ch === '"' || ch === "'") {
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

export function parseScript(text: string, topology: Topology | null, base: SimConfig): ParsedScript {
  const nodes = topology?.nodes ?? [];
  const typeOf = new Map(nodes.map((node) => [node.name, node.type]));
  const linkKeys = new Set(
    (topology ? linkNamePairs(topology) : []).flatMap(([a, b]) => [`${a}|${b}`, `${b}|${a}`]),
  );

  const settings: SimSettings = { config: { ...base, reconvergence_delay_ms: 0 }, flows: [], events: [] };
  const notes: ScriptNote[] = [];
  let now = 0;
  let hasRun = false;
  let hasExport = false;

  text.split(/\r?\n/).forEach((rawLine, index) => {
    const line = index + 1;
    const note = (level: ScriptNote["level"], message: string) => notes.push({ line, level, message });

    let tokens: string[];
    try {
      tokens = tokenize(rawLine);
    } catch (error) {
      note("error", (error as Error).message);
      return;
    }
    if (tokens.length === 0) return;

    const { name, args } = matchCommand(tokens);
    const usage = (counts: number[], text: string) => {
      if (counts.includes(args.length)) return true;
      note("error", `Usage: ${text}`);
      return false;
    };
    const numbers = (values: (string | undefined)[], what: string): number[] | null => {
      const parsed = values.map(toNumber);
      if (parsed.some(Number.isNaN)) {
        note("error", `${what} must be numeric.`);
        return null;
      }
      return parsed;
    };
    const host = (name: string) => {
      if (topology && typeOf.get(name) !== "HOST") {
        note("error", `“${name}” is not a host in this topology.`);
        return false;
      }
      return true;
    };
    const link = (a: string, b: string) => {
      if (topology && !linkKeys.has(`${a}|${b}`)) {
        note("error", `No link between ${a} and ${b}.`);
        return false;
      }
      return true;
    };
    const pushEvent = (fields: Partial<EventDraft> & Pick<EventDraft, "type">) =>
      settings.events.push({ ...makeEvent(null, fields.type), ...fields });
    const [a0 = "", a1 = "", a2 = "", a3 = "", a4 = "", a5 = "", a6 = "", a7 = ""] = args;

    switch (name) {
      case "load":
        note("info", "Ignored: the workbench topology is loaded automatically.");
        break;
      case "set seed": {
        if (!usage([1], "set seed <n>")) break;
        const values = numbers([a0], "Seed");
        if (!values) break;
        if (settings.flows.length || settings.events.length) {
          note("warning", "In the CLI, set seed resets the run, discarding flows and events added before it.");
        }
        settings.config.seed = values[0]!;
        settings.flows = [];
        settings.events = [];
        now = 0;
        break;
      }
      case "set algo":
        if (!usage([1], "set algo <bfs|dfs|dijkstra|bellman-ford>")) break;
        if ((ALGORITHMS as readonly string[]).includes(a0)) settings.config.algorithm = a0 as Algorithm;
        else note("error", `Unknown algorithm “${a0}”.`);
        break;
      case "set weight":
        if (!usage([1], "set weight <hops|latency>")) break;
        if ((WEIGHTS as readonly string[]).includes(a0)) settings.config.weight = a0 as Weight;
        else note("error", `Unknown weight “${a0}”.`);
        break;
      case "add flow": {
        if (!usage([4, 6, 7], "add flow <src> <dst> <mbps> <bytes> [start stop] [bursty]")) break;
        if (args.length === 7 && a6 !== "bursty") {
          note("error", "The optional traffic pattern must be “bursty”.");
          break;
        }
        const timed = args.length >= 6;
        const values = numbers([a2, a3, ...(timed ? [a4, a5] : [])], "Rate, size and times");
        if (!values || !host(a0) || !host(a1)) break;
        const start = timed ? values[2]! : now;
        const stop = timed ? values[3]! : start + 10;
        if (stop <= start || start < now) {
          note("error", "Flow times must satisfy now <= start < stop.");
          break;
        }
        settings.flows.push({
          src: a0,
          dst: a1,
          rate_mbps: values[0]!,
          packet_bytes: values[1]!,
          start_s: start,
          stop_s: stop,
          pattern: args.length === 7 ? "bursty" : "constant",
          on_duration_s: 0.5,
          off_duration_s: 0.5,
        });
        break;
      }
      case "fail link":
      case "restore link": {
        if (!usage([4], `${name} <A> <B> at <t>`)) break;
        if (a2 !== "at") {
          note("error", "Expected “at <time>”.");
          break;
        }
        const values = numbers([a3], "Event time");
        if (!values || !link(a0, a1)) break;
        pushEvent({ type: name === "fail link" ? "LINK_FAILURE" : "LINK_RESTORE", a: a0, b: a1, time_s: values[0]! });
        break;
      }
      case "fail router":
      case "restore router": {
        if (!usage([3], `${name} <R> at <t>`)) break;
        if (a1 !== "at") {
          note("error", "Expected “at <time>”.");
          break;
        }
        const values = numbers([a2], "Event time");
        if (!values) break;
        if (topology && typeOf.get(a0) !== "ROUTER") {
          note("error", `“${a0}” is not a router in this topology.`);
          break;
        }
        pushEvent({
          type: name === "fail router" ? "ROUTER_FAILURE" : "ROUTER_RESTORE",
          node: a0,
          time_s: values[0]!,
        });
        break;
      }
      case "congest link": {
        if (!usage([8], "congest link <A> <B> factor <f> at <t> for <d>")) break;
        if (a2 !== "factor" || a4 !== "at" || a6 !== "for") {
          note("error", "Usage: congest link <A> <B> factor <f> at <t> for <d>");
          break;
        }
        const values = numbers([a3, a5, a7], "Factor, time and duration");
        if (!values || !link(a0, a1)) break;
        pushEvent({
          type: "CONGESTION",
          a: a0,
          b: a1,
          bandwidth_multiplier: values[0]!,
          time_s: values[1]!,
          duration_s: values[2]!,
          extra_loss_rate: 0,
        });
        break;
      }
      case "burst": {
        if (!usage([7], "burst <src> <dst> <mbps> at <t> for <d>")) break;
        if (a3 !== "at" || a5 !== "for") {
          note("error", "Usage: burst <src> <dst> <mbps> at <t> for <d>");
          break;
        }
        const values = numbers([a2, a4, a6], "Rate, time and duration");
        if (!values || !host(a0) || !host(a1)) break;
        pushEvent({
          type: "TRAFFIC_BURST",
          src: a0,
          dst: a1,
          rate_mbps: values[0]!,
          time_s: values[1]!,
          duration_s: values[2]!,
          packet_bytes: 1000,
        });
        break;
      }
      case "run": {
        if (!usage([1], "run <seconds>")) break;
        const values = numbers([a0], "Duration");
        if (!values) break;
        now += values[0]!;
        hasRun = true;
        settings.config.duration_s = now;
        break;
      }
      case "reset":
        settings.flows = [];
        settings.events = [];
        now = 0;
        break;
      default:
        if (TRANSIENT.has(name)) {
          if (name === "export") hasExport = true;
          note("info", `${name} runs in the simulator but is not stored in a scenario.`);
        } else {
          note("error", `Unknown command: ${tokens[0]}`);
        }
    }
  });

  return { settings, notes, hasRun, hasExport };
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
    if (flow.pattern === "bursty" && (flow.on_duration_s !== 0.5 || flow.off_duration_s !== 0.5)) {
      lines.push(`# Flow ${i + 1}: scripts use fixed 0.5 s on / 0.5 s off bursts.`);
    }
    const suffix = flow.pattern === "bursty" ? " bursty" : "";
    lines.push(
      `add flow ${flow.src} ${flow.dst} ${flow.rate_mbps} ${flow.packet_bytes} ${flow.start_s} ${flow.stop_s}${suffix}`,
    );
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
        if (item.src && item.dst) lines.push(`burst ${item.src} ${item.dst} ${item.rate_mbps} at ${t} for ${item.duration_s}`);
        break;
    }
  }
  lines.push(`run ${config.duration_s}`, "show metrics", "export");
  return `${lines.join("\n")}\n`;
}
