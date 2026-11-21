import { describe, expect, it } from "vitest";
import type { RunFile } from "../lib/types";
import { parseCsv } from "./csv";
import { buildReplayModel, jumpTargets, packetsAt, stateAt } from "./replay";

const topology = {
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

const trace = [
  "time_s,event_type,packet_id,flow_id,location,detail",
  "0.0,FLOW_START,-,1,PC1,dst=PC2",
  "0.0,TX_START,1,1,PC1>R1,eta=0.002",
  "0.002,ARRIVE,1,1,R1,from=PC1",
  "0.002,TX_START,1,1,R1>PC2,eta=0.004",
  "0.003,DROPPED,1,1,R1,reason=random_loss",
  "1.0,LINK_DOWN,-,-,2,state=down",
  "1.5,ROUTER_DOWN,-,-,2,state=down",
  "2.0,LINK_UP,-,-,2,state=up",
].join("\n");

const file = (name: string, content: string): RunFile => ({ name, type: "text/plain", content });

const files: RunFile[] = [
  file("topology_run_001.json", JSON.stringify(topology)),
  file("run_001.json", JSON.stringify({ algorithm: "bfs", weight: "hops", seed: 1 })),
  file("run_001_trace.csv", trace),
  file("run_001_windows.csv", "t_start,avg_latency_ms,throughput_mbps,loss_pct,max_util_pct\n0,1,2,3,4\n3,1,2,3,4\n"),
  file("run_001_flows.csv", "flow_id,src,dst,sent,delivered,dropped,loss_pct\n1,1,3,1,0,1,100\n"),
  file("run_001_links.csv", "channel,utilization_pct\nPC1>R1,12.5\n"),
];

describe("csv", () => {
  it("parses quoted fields and BOM headers", () => {
    expect(parseCsv('﻿a,b\n"x,1","say ""hi"""\n')).toEqual([{ a: "x,1", b: 'say "hi"' }]);
  });
});

describe("replay model", () => {
  const model = buildReplayModel(files, "run_001");

  it("reads metadata, flows by host name and utilisation", () => {
    expect(model.algorithm).toBe("bfs");
    expect(model.flows[0]).toMatchObject({ src: "PC1", dst: "PC2", dropped: 1 });
    expect(model.utilization.get("PC1>R1")).toBe(12.5);
    expect(model.endTime).toBe(3);
  });

  it("tracks packet hops, ending a hop where the packet was dropped", () => {
    expect(model.segments.map((s) => [s.from, s.to, s.tStart, s.tEnd])).toEqual([
      ["PC1", "R1", 0, 0.002],
      ["R1", "PC2", 0.002, 0.003],
    ]);
    const [packet] = packetsAt(model, 0.001);
    expect(packet?.seg.to).toBe("R1");
    expect(packet?.fraction).toBeCloseTo(0.5);
    expect(packetsAt(model, 0.5)).toEqual([]);
  });

  it("derives link and router state over time", () => {
    expect(stateAt(model.linkIntervals, 1.2).get("2")).toBe("down");
    expect(stateAt(model.linkIntervals, 2.5).has("2")).toBe(false);
    expect(stateAt(model.routerIntervals, 2.9).get("2")).toBe("down");
    expect(model.incidents.map((i) => i.label)).toEqual(["R1–PC2 down", "R1 down", "R1–PC2 up"]);
  });

  it("offers jump targets for incidents and drop bursts", () => {
    expect(jumpTargets(model)).toEqual([0.003, 1, 1.5, 2]);
  });
});
