import { useState } from "react";
import { Activity, ChartColumnBig, CircleAlert, PlayCircle, Sparkles, TriangleAlert, CircleCheck } from "lucide-react";
import { isReport } from "../../domain/anomaly";
import { algorithmLabel, fmt, fmtSigned } from "../../lib/format";
import type { BaselineMetric, RunResult, RunSummary } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import { EmptyState, Segmented } from "../ui";

type MetricKey = "latency_ms" | "throughput_mbps" | "reliability_pct" | "drops" | "recovery_time_s" | "avg_hops";

const METRICS: { key: MetricKey; label: string; unit: string; better: "low" | "high"; digits: number }[] = [
  { key: "latency_ms", label: "Latency", unit: "ms", better: "low", digits: 2 },
  { key: "throughput_mbps", label: "Throughput", unit: "Mbps", better: "high", digits: 2 },
  { key: "reliability_pct", label: "Reliability", unit: "%", better: "high", digits: 2 },
  { key: "drops", label: "Drops", unit: "pkts", better: "low", digits: 0 },
  { key: "recovery_time_s", label: "Recovery", unit: "s", better: "low", digits: 3 },
  { key: "avg_hops", label: "Avg hops", unit: "", better: "low", digits: 2 },
];

const SERIES = ["var(--chart-1)", "var(--chart-2)", "var(--chart-3)", "var(--chart-4)"];

const value = (summary: RunSummary, key: MetricKey): number | null => {
  const v = summary[key];
  return typeof v === "number" && Number.isFinite(v) ? v : null;
};

function bestIndex(runs: RunResult[], key: MetricKey, better: "low" | "high"): number | null {
  const values = runs.map((run) => value(run.summary, key));
  const finite = values.filter((v): v is number => v !== null);
  if (finite.length < 2 || finite.every((v) => v === finite[0])) return null;
  const target = better === "low" ? Math.min(...finite) : Math.max(...finite);
  return values.indexOf(target);
}

function ComparisonChart({ runs, metric }: { runs: RunResult[]; metric: (typeof METRICS)[number] }) {
  const values = runs.map((run) => value(run.summary, metric.key) ?? 0);
  const max = Math.max(...values, 1e-9);
  const best = bestIndex(runs, metric.key, metric.better);
  return (
    <div className="bars" role="img" aria-label={`${metric.label} by algorithm`}>
      {runs.map((run, i) => (
        <div key={run.run_id} className="bar-row">
          <span className="bar-label">{algorithmLabel(run.algorithm)}</span>
          <div className="bar-track">
            <div className="bar-fill" style={{ width: `${(values[i]! / max) * 100}%`, background: SERIES[i % SERIES.length] }} />
          </div>
          <span className={`bar-value${best === i ? " best" : ""}`}>
            {fmt(value(run.summary, metric.key), metric.digits)}
            {metric.unit && <small> {metric.unit}</small>}
          </span>
        </div>
      ))}
      {best === null && runs.length > 1 && (
        <p className="muted small">All algorithms tie on this metric for this scenario.</p>
      )}
    </div>
  );
}

const BASELINE_LABELS: Record<string, { label: string; unit: string }> = {
  avg_latency_ms: { label: "Avg latency", unit: "ms" },
  throughput_mbps: { label: "Throughput", unit: "Mbps" },
  loss_pct: { label: "Packet loss", unit: "%" },
};

function BaselineGauge({ metric }: { metric: BaselineMetric }) {
  const info = BASELINE_LABELS[metric.name] ?? { label: metric.name, unit: "" };
  const scale = Math.max(metric.actual ?? 0, metric.expected ?? 0, 1e-9);
  const deviation =
    metric.name === "loss_pct"
      ? fmtSigned(metric.delta, 2, " pp")
      : metric.deviation_pct !== null
        ? fmtSigned(metric.deviation_pct, 1, "%")
        : fmtSigned(metric.delta, 3);
  return (
    <div className={`gauge${metric.flagged ? " flagged" : ""}`}>
      <div className="row">
        <span className="gauge-title">{info.label}</span>
        <div className="spacer" />
        <span className={`badge ${metric.flagged ? "warning" : "success"}`}>
          {metric.flagged ? <TriangleAlert /> : <CircleCheck />}
          {deviation}
        </span>
      </div>
      <div className="gauge-bars">
        <div className="gauge-line">
          <span>Actual</span>
          <div className="gauge-track">
            <div className="gauge-fill actual" style={{ width: `${((metric.actual ?? 0) / scale) * 100}%` }} />
          </div>
          <strong>{fmt(metric.actual, 3)}</strong>
        </div>
        <div className="gauge-line">
          <span>Expected</span>
          <div className="gauge-track">
            <div className="gauge-fill expected" style={{ width: `${((metric.expected ?? 0) / scale) * 100}%` }} />
          </div>
          <strong>{fmt(metric.expected, 3)}</strong>
        </div>
      </div>
      <span className="muted small">
        {info.unit} · {metric.flagged ? "large deviation from the model's expectation" : "within the expected range"}
      </span>
    </div>
  );
}

export function ResultsPage() {
  const { runs, goTo, scriptResult, setupMode, setFocusRunId } = useWorkbench();
  const [metricKey, setMetricKey] = useState<MetricKey>("latency_ms");
  const metric = METRICS.find((m) => m.key === metricKey)!;

  if (runs.length === 0) {
    return (
      <section className="card">
        <EmptyState icon={<ChartColumnBig />} title="No results yet">
          <p>Run a simulation to compare algorithms here.</p>
        </EmptyState>
      </section>
    );
  }

  const fromScript = setupMode === "script" && scriptResult !== null;
  return (
    <div className="stack" style={{ "--gap": "20px" } as React.CSSProperties}>
      <div className="kpis">
        {runs.map((run, i) => (
          <article key={run.run_id} className="kpi card">
            <div className="row">
              <span className="kpi-dot" style={{ background: SERIES[i % SERIES.length] }} />
              <strong>{algorithmLabel(run.algorithm)}</strong>
              <div className="spacer" />
              <span className="badge mono">{run.run_id}</span>
            </div>
            <div className="kpi-grid">
              <div>
                <span>{fmt(run.summary.reliability_pct, 2)}%</span>reliability
              </div>
              <div>
                <span>{fmt(run.summary.latency_ms, 2)}</span>ms latency
              </div>
              <div>
                <span>{fmt(run.summary.throughput_mbps, 2)}</span>Mbps
              </div>
              <div>
                <span>{run.summary.drops}</span>drops
              </div>
            </div>
            {isReport(run.anomaly) && (
              <button
                type="button"
                className="kpi-anomaly"
                onClick={() => {
                  setFocusRunId(run.run_id);
                  goTo("anomalies");
                }}
              >
                <Activity />
                {run.anomaly.summary.flagged === 0
                  ? "No anomalous seconds"
                  : `${run.anomaly.summary.flagged} anomalous second${run.anomaly.summary.flagged === 1 ? "" : "s"}`}
              </button>
            )}
          </article>
        ))}
      </div>

      <section className="card">
        <div className="card-head">
          <div>
            <h3>Algorithm comparison</h3>
            <p>
              Same topology, traffic, events and seed{fromScript ? " (from the script)" : ""}. Best value highlighted.
            </p>
          </div>
          <div className="spacer" />
          <Segmented label="Metric" value={metricKey} onChange={setMetricKey} options={METRICS.map((m) => ({ value: m.key, label: m.label }))} />
        </div>
        <div className="card-body stack" style={{ "--gap": "22px" } as React.CSSProperties}>
          <ComparisonChart runs={runs} metric={metric} />
          <div className="table-wrap">
            <table className="data">
              <thead>
                <tr>
                  <th>Algorithm</th>
                  <th>Run</th>
                  {METRICS.map((m) => (
                    <th key={m.key} className="num">
                      {m.label}
                      {m.unit && ` (${m.unit})`}
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {runs.map((run, i) => (
                  <tr key={run.run_id}>
                    <td>
                      <span className="kpi-dot" style={{ background: SERIES[i % SERIES.length] }} /> {algorithmLabel(run.algorithm)}
                    </td>
                    <td className="mono muted">{run.run_id}</td>
                    {METRICS.map((m) => (
                      <td key={m.key} className={`num${bestIndex(runs, m.key, m.better) === i ? " best" : ""}`}>
                        {fmt(value(run.summary, m.key), m.digits)}
                      </td>
                    ))}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <div>
            <h3>Expected baseline</h3>
            <p>A model of this scenario's routes, capacities and events estimates what each metric should be; past runs set how large a deviation must be to flag.</p>
          </div>
        </div>
        <div className="card-body stack" style={{ "--gap": "18px" } as React.CSSProperties}>
          {runs.map((run, i) => (
            <div key={run.run_id} className="baseline-run">
              <div className="row">
                <span className="kpi-dot" style={{ background: SERIES[i % SERIES.length] }} />
                <strong>{algorithmLabel(run.algorithm)}</strong>
                <span className="muted mono small">{run.run_id}</span>
              </div>
              {"error" in run.baseline ? (
                <div className="notice danger">
                  <CircleAlert />
                  {run.baseline.error}
                </div>
              ) : (
                <div className="grid-3">
                  {run.baseline.metrics.map((m) => (
                    <BaselineGauge key={m.name} metric={m} />
                  ))}
                </div>
              )}
            </div>
          ))}
        </div>
      </section>

      <div className="row">
        <button type="button" className="btn" onClick={() => goTo("anomalies")}>
          <Activity /> See anomalies
        </button>
        <button type="button" className="btn" onClick={() => goTo("replay")}>
          <PlayCircle /> Open replay
        </button>
        <button type="button" className="btn" onClick={() => goTo("insights")}>
          <Sparkles /> Ask about these runs
        </button>
      </div>
    </div>
  );
}
