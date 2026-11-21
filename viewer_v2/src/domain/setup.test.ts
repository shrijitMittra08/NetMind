import { describe, expect, it } from "vitest";
import type { SimSettings, Topology } from "../lib/types";
import { DEFAULT_CONFIG, buildPayload, findSetupProblems, makeEvent, makeFlow, repairForTopology } from "./setup";

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

describe("simulation setup", () => {
  it("creates rows that are valid for the topology as added", () => {
    const settings: SimSettings = { config: DEFAULT_CONFIG, flows: [makeFlow(topo)], events: [makeEvent(topo)] };
    expect(settings.flows[0]).toMatchObject({ src: "PC1", dst: "PC2" });
    expect(settings.events[0]).toMatchObject({ a: "PC1", b: "R1", node: "R1" });
    expect(findSetupProblems(topo, settings)).toEqual([]);
  });

  it("reports empty or stale names instead of letting the simulator fail", () => {
    const settings: SimSettings = {
      config: DEFAULT_CONFIG,
      flows: [{ ...makeFlow(topo), src: "" }],
      events: [{ ...makeEvent(topo), a: "Ghost", b: "R1" }],
    };
    expect(findSetupProblems(topo, settings)).toEqual([
      "Flow 1: choose a source host.",
      "Event 1: choose a link that exists in this topology.",
    ]);
  });

  it("repairs flows and blanks stale event references after a topology change", () => {
    const renamed: Topology = {
      ...topo,
      nodes: topo.nodes.map((n) => (n.id === 1 ? { ...n, name: "Alice" } : n)),
    };
    const settings: SimSettings = { config: DEFAULT_CONFIG, flows: [makeFlow(topo)], events: [makeEvent(topo)] };
    const repaired = repairForTopology(renamed, settings);
    expect(repaired.flows[0]).toMatchObject({ src: "Alice", dst: "PC2" });
    expect(repaired.events[0]).toMatchObject({ a: "", b: "R1" });
  });

  it("builds the API payload, numbering burst flows after the regular flows", () => {
    const settings: SimSettings = {
      config: DEFAULT_CONFIG,
      flows: [makeFlow(topo)],
      events: [makeEvent(topo, "TRAFFIC_BURST"), makeEvent(topo, "ROUTER_FAILURE")],
    };
    const payload = buildPayload(topo, "t.json", settings);
    expect(payload.flows[0]).toMatchObject({ id: 1, src: "PC1", pattern: "constant" });
    expect(payload.flows[0]).not.toHaveProperty("on_duration_s");
    expect(payload.events).toEqual([
      {
        type: "TRAFFIC_BURST",
        flow_id: 2,
        src: "PC1",
        dst: "PC2",
        time_s: 2,
        duration_s: 1,
        rate_mbps: 10,
        packet_bytes: 1000,
      },
      { type: "ROUTER_FAILURE", node: "R1", time_s: 2 },
    ]);
  });
});
