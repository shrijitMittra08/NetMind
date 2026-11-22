import { describe, expect, it } from "vitest";
import type { AnomalyReport, AnomalyWindow } from "../lib/types";
import { describeDriver, describeSpan, flagSpans, isReport, mainDriver, verdict } from "./anomaly";

function window(t: number, flagged: boolean, disturbed: boolean, metric = "loss_pct"): AnomalyWindow {
  return {
    t,
    avg_latency_ms: 5,
    loss_pct: flagged ? 50 : 0,
    throughput_mbps: 5,
    max_util_pct: 10,
    no_delivery: false,
    score: flagged ? 8 : 1,
    flagged,
    if_flagged: false,
    disturbed,
    driver: flagged ? { metric, change: "jump" } : null,
  };
}

function report(windows: AnomalyWindow[], disturbances = [{ start: 2, end: 5, label: "link failure R1-R2" }]): AnomalyReport {
  const disturbed = windows.filter((w) => w.disturbed);
  return {
    run_id: "run_001",
    detector: "z-score",
    comparison: "IsolationForest",
    threshold: 4,
    window_s: 1,
    duration_s: windows.length,
    disturbances,
    windows,
    summary: {
      windows: windows.length,
      flagged: windows.filter((w) => w.flagged).length,
      disturbed: disturbed.length,
      caught: disturbed.filter((w) => w.flagged).length,
      false_alarms: windows.filter((w) => w.flagged && !w.disturbed).length,
      if_flagged: 0,
      first_flag_delay_s: disturbed.some((w) => w.flagged) ? 0 : null,
    },
  };
}

describe("anomaly helpers", () => {
  it("merges consecutive flagged seconds and splits at the event boundary", () => {
    const windows = [
      window(0, false, false),
      window(2, true, true),
      window(3, true, true),
      window(4, true, true),
      window(5, true, false),
      window(7, true, false),
    ];
    const spans = flagSpans(windows);
    expect(spans.map((s) => [s.start, s.end, s.insideEvent])).toEqual([
      [2, 5, true],
      [5, 6, false],
      [7, 8, false],
    ]);
  });

  it("names the metric that drove the flag in plain words", () => {
    expect(describeDriver({ metric: "loss_pct", change: "jump" })).toBe("packet loss jumped from the previous second");
    expect(describeDriver({ metric: "avg_latency_ms", change: "vs_normal" })).toBe("latency far from this run's normal level");
    expect(describeDriver(null)).toBe("");
  });

  it("picks the most common driver within a span", () => {
    const span = flagSpans([window(1, true, true, "loss_pct"), window(2, true, true, "avg_latency_ms"), window(3, true, true, "avg_latency_ms")])[0]!;
    expect(mainDriver(span)?.metric).toBe("avg_latency_ms");
  });

  it("describes how a flagged period started and what dominated it", () => {
    const span = flagSpans([window(1, true, true, "loss_pct"), window(2, true, true, "avg_latency_ms"), window(3, true, true, "avg_latency_ms")])[0]!;
    expect(describeSpan(span)).toBe("packet loss jumped from the previous second, then latency jumped from the previous second");
    const single = flagSpans([window(1, true, true, "loss_pct")])[0]!;
    expect(describeSpan(single)).toBe("packet loss jumped from the previous second");
  });

  it("reports detected, missed and quiet runs", () => {
    expect(verdict(report([window(1, false, false), window(2, true, true), window(3, false, true)])).tone).toBe("success");
    expect(verdict(report([window(2, false, true), window(3, false, true)])).tone).toBe("danger");
    expect(verdict(report([window(1, false, false)], [])).tone).toBe("success");
    expect(verdict(report([window(1, true, false)], [])).tone).toBe("warning");
  });

  it("distinguishes reports from errors", () => {
    expect(isReport({ error: "not trained" })).toBe(false);
    expect(isReport(undefined)).toBe(false);
    expect(isReport(report([]))).toBe(true);
  });
});
