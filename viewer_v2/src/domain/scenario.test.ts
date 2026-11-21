import { describe, expect, it } from "vitest";
import type { SimSettings, Topology } from "../lib/types";
import { buildScenarioDocument, parseScenarioJson } from "./scenario";
import { DEFAULT_CONFIG, buildPayload, findSetupProblems, makeEvent, makeFlow } from "./setup";

const topo: Topology = {
  nodes: [
    { id: 1, type: "HOST", name: "PC1", ip: "10.0.0.1" },
    { id: 2, type: "ROUTER", name: "R1", ip: "10.0.0.2" },
    { id: 3, type: "ROUTER", name: "R2", ip: "10.0.0.3" },
    { id: 4, type: "HOST", name: "PC2", ip: "10.0.0.4" },
  ],
  links: [
    { a: 1, b: 2, bandwidthMbps: 100, latencyMs: 1, lossRate: 0 },
    { a: 2, b: 3, bandwidthMbps: 50, latencyMs: 2, lossRate: 0 },
    { a: 3, b: 4, bandwidthMbps: 100, latencyMs: 1, lossRate: 0 },
  ],
};

const settings: SimSettings = {
  config: { duration_s: 12, seed: 7, algorithm: "bfs", weight: "hops", reconvergence_delay_ms: 200 },
  flows: [{ ...makeFlow(topo), rate_mbps: 8, stop_s: 12, pattern: "bursty", on_duration_s: 0.5, off_duration_s: 1.5 }],
  events: [
    { ...makeEvent(topo, "LINK_FAILURE"), a: "R1", b: "R2", time_s: 3 },
    { ...makeEvent(topo, "ROUTER_FAILURE"), node: "R2", time_s: 5 },
    { ...makeEvent(topo, "CONGESTION"), a: "R1", b: "R2", time_s: 6, duration_s: 2, bandwidth_multiplier: 0.4 },
    { ...makeEvent(topo, "TRAFFIC_BURST"), src: "PC2", dst: "PC1", time_s: 7, rate_mbps: 30 },
  ],
};

describe("scenario files", () => {
  it("round-trips a downloaded scenario to the same run request", () => {
    const document = buildScenarioDocument(topo, "line.json", settings, "demo");
    expect(document.routing).toEqual({ algorithm: "bfs", weight: "hops", reconvergence_delay_ms: 200 });

    const parsed = parseScenarioJson(JSON.stringify(document));
    expect(parsed.name).toBe("demo");
    expect(parsed.topology).toEqual(topo);
    expect(parsed.warnings).toEqual([]);
    expect(findSetupProblems(topo, parsed.settings)).toEqual([]);
    expect(buildPayload(topo, "line.json", parsed.settings)).toEqual(buildPayload(topo, "line.json", settings));
  });

  it("keeps a topology path for the API to resolve (the CLI form)", () => {
    const parsed = parseScenarioJson(
      JSON.stringify({
        name: "ring",
        topology: "../topologies/ring_backup.json",
        seed: 42,
        duration_s: 10,
        routing: { algorithm: "dijkstra", weight: "latency" },
        flows: [{ id: 1, src: "PC1", dst: "PC2", pattern: "constant", rate_mbps: 5, packet_bytes: 1000, start_s: 0, stop_s: 10 }],
        events: [{ type: "LINK_FAILURE", a: "R1", b: "R2", time_s: 3 }],
      }),
    );
    expect(parsed.topology).toBeNull();
    expect(parsed.topologyPath).toBe("../topologies/ring_backup.json");
    expect(parsed.settings.config).toEqual({ ...DEFAULT_CONFIG, duration_s: 10, seed: 42 });
    expect(parsed.settings.events[0]).toMatchObject({ type: "LINK_FAILURE", a: "R1", b: "R2", time_s: 3 });
  });

  it("explains files it cannot use", () => {
    expect(() => parseScenarioJson("{")).toThrow("not valid JSON");
    expect(() => parseScenarioJson(JSON.stringify(topo))).toThrow("topology file, not a scenario");
    expect(() => parseScenarioJson(JSON.stringify({ seed: 1 }))).toThrow("no topology");
  });

  it("warns about unsupported values and leaves missing names for setup validation", () => {
    const parsed = parseScenarioJson(
      JSON.stringify({
        topology: topo,
        routing: { algorithm: "ospf" },
        flows: [{ src: "PC1", dst: "PC2", pattern: "zigzag" }],
        events: [{ type: "METEOR", time_s: 1 }, { type: "LINK_FAILURE", time_s: 2 }],
      }),
      "odd.json",
    );
    expect(parsed.name).toBe("odd");
    expect(parsed.warnings).toEqual([
      'Unknown routing algorithm "ospf"; using dijkstra.',
      'Flow 1: unknown pattern "zigzag"; using constant.',
      'Event 1: unknown event type "METEOR" was skipped.',
    ]);
    expect(findSetupProblems(topo, parsed.settings)).toEqual(["Event 1: choose a link that exists in this topology."]);
  });
});
