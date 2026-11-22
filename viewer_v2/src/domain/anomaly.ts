import type { AnomalyDriver, AnomalyReport, AnomalyResult, AnomalyWindow } from "../lib/types";

export const METRIC_NAMES: Record<string, string> = {
  avg_latency_ms: "latency",
  jitter_ms: "jitter",
  loss_pct: "packet loss",
  throughput_mbps: "throughput",
  max_queue: "queue length",
  max_util_pct: "link utilisation",
  no_delivery: "deliveries",
};

const CHANGES: Record<AnomalyDriver["change"], string> = {
  vs_normal: "far from this run's normal level",
  vs_recent: "changed sharply from the last few seconds",
  jump: "jumped from the previous second",
  jump3: "changed sharply over three seconds",
  level: "is outside the range seen in normal runs",
};

export function isReport(result: AnomalyResult | null | undefined): result is AnomalyReport {
  return result !== undefined && result !== null && !("error" in result);
}

export function describeDriver(driver: AnomalyDriver | null): string {
  if (!driver) return "";
  const metric = METRIC_NAMES[driver.metric] ?? driver.metric.replace(/_/g, " ");
  if (driver.metric === "no_delivery") return "nothing was delivered";
  return `${metric} ${CHANGES[driver.change] ?? "changed"}`;
}

export interface FlagSpan {
  start: number;
  end: number;
  windows: AnomalyWindow[];
  insideEvent: boolean;
}

export function flagSpans(windows: AnomalyWindow[], windowSize = 1): FlagSpan[] {
  const spans: FlagSpan[] = [];
  const sorted = [...windows].sort((a, b) => a.t - b.t);
  for (const window of sorted) {
    if (!window.flagged) continue;
    const last = spans[spans.length - 1];
    if (last && Math.abs(window.t - last.end) < 1e-9 && last.insideEvent === window.disturbed) {
      last.end = window.t + windowSize;
      last.windows.push(window);
    } else {
      spans.push({ start: window.t, end: window.t + windowSize, windows: [window], insideEvent: window.disturbed });
    }
  }
  return spans;
}

export function mainDriver(span: FlagSpan): AnomalyDriver | null {
  const counts = new Map<string, { driver: AnomalyDriver; count: number }>();
  for (const window of span.windows) {
    if (!window.driver) continue;
    const key = `${window.driver.metric}|${window.driver.change}`;
    const entry = counts.get(key) ?? { driver: window.driver, count: 0 };
    entry.count += 1;
    counts.set(key, entry);
  }
  let best: { driver: AnomalyDriver; count: number } | null = null;
  for (const entry of counts.values()) if (!best || entry.count > best.count) best = entry;
  return best?.driver ?? null;
}

export function describeSpan(span: FlagSpan): string {
  const first = span.windows.find((w) => w.driver)?.driver ?? null;
  const common = mainDriver(span);
  const start = describeDriver(first);
  if (!common || !first || (common.metric === first.metric && common.change === first.change)) return start;
  return `${start}, then ${describeDriver(common)}`;
}

export function verdict(report: AnomalyReport): { tone: "success" | "warning" | "danger" | "neutral"; text: string } {
  const { summary, disturbances } = report;
  if (disturbances.length === 0) {
    return summary.flagged === 0
      ? { tone: "success", text: "No events injected and nothing flagged." }
      : { tone: "warning", text: `No events injected, but ${summary.flagged} second(s) looked unusual.` };
  }
  if (summary.caught === 0) {
    return { tone: "danger", text: "The injected event was not detected; it may have had no visible effect." };
  }
  const delay = summary.first_flag_delay_s;
  const when = delay === null ? "" : delay < 0.5 ? " from its first second" : ` ${delay.toFixed(0)} s after it started`;
  return {
    tone: "success",
    text: `Detected${when}: ${summary.caught} of ${summary.disturbed} seconds inside the event window flagged.`,
  };
}
