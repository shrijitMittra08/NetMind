import { describe, expect, it } from "vitest";
import type { Topology } from "../lib/types";
import { generateScript, parseScript, tokenize } from "./script";
import { DEFAULT_CONFIG } from "./setup";

const topo: Topology = {
  nodes: [
    { id: 1, type: "HOST", name: "PC1", ip: "10.0.0.1" },
    { id: 2, type: "ROUTER", name: "R1", ip: "10.0.0.2" },
    { id: 3, type: "HOST", name: "PC2", ip: "10.0.0.3" },
  ],
  links: [
    { a: 1, b: 2, bandwidthMbps: 100, latencyMs: 1, lossRate: 0 },
    { a: 2, b: 3, bandwidthMbps: 100, latencyMs: 1, lossRate: 0 },
  ],
};

describe("script tokenizer", () => {
  it("handles quotes and comments like the CLI", () => {
    expect(tokenize('load "my file.json"  # comment')).toEqual(["load", "my file.json"]);
    expect(tokenize("# only a comment")).toEqual([]);
    expect(() => tokenize('load "open')).toThrow("Unclosed quoted argument");
  });
});

describe("parseScript", () => {
  it("turns commands into settings and notes", () => {
    const parsed = parseScript(
      [
        "load x.json",
        "set seed 9",
        "set algo bfs",
        "set weight hops",
        "add flow PC1 PC2 8 500 1 7 bursty",
        "fail router R1 at 3",
        "congest link R1 PC2 factor 0.5 at 2 for 1",
        "ping PC1 PC2",
        "run 8",
        "export",
      ].join("\n"),
      topo,
      DEFAULT_CONFIG,
    );
    expect(parsed.settings.config).toEqual({ duration_s: 8, seed: 9, algorithm: "bfs", weight: "hops", reconvergence_delay_ms: 0 });
    expect(parsed.settings.flows).toEqual([
      expect.objectContaining({ src: "PC1", dst: "PC2", rate_mbps: 8, start_s: 1, stop_s: 7, pattern: "bursty" }),
    ]);
    expect(parsed.settings.events.map((e) => e.type)).toEqual(["ROUTER_FAILURE", "CONGESTION"]);
    expect(parsed.notes.every((n) => n.level === "info")).toBe(true);
    expect(parsed.hasRun && parsed.hasExport).toBe(true);
  });

  it("reports unknown commands and names not in the topology", () => {
    const parsed = parseScript("bogus\nadd flow PC1 Nobody 5 1000\nfail link PC1 PC2 at 1", topo, DEFAULT_CONFIG);
    expect(parsed.notes.map((n) => `${n.line}:${n.level}`)).toEqual(["1:error", "2:error", "3:error"]);
  });

  it("round-trips generated scripts", () => {
    const first = parseScript(
      "set seed 3\nadd flow PC1 PC2 5 1000 0 10\nfail link PC1 R1 at 2\nrun 10",
      topo,
      DEFAULT_CONFIG,
    );
    const again = parseScript(generateScript(first.settings), topo, DEFAULT_CONFIG);
    expect(again.settings).toEqual(first.settings);
    expect(again.notes.filter((n) => n.level !== "info")).toEqual([]);
  });
});
