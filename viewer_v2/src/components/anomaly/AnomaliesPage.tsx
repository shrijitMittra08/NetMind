import { useEffect, useRef, useState } from "react";
import { CircleAlert, CircleCheck, Info, LoaderCircle, TriangleAlert, X } from "lucide-react";
import { api } from "../../lib/api";
import { algorithmLabel, fmt } from "../../lib/format";
import type { AnomalyEvaluation, AnomalyReport, AnomalyResult, RunResult } from "../../lib/types";
import { describeSpan, flagSpans, isReport, verdict } from "../../domain/anomaly";
import type { FlagSpan } from "../../domain/anomaly";
import { useWorkbench } from "../../state/workbench";
import { Segmented } from "../ui";
import { ANOMALY_METRICS, AnomalyChart, AnomalyReadout } from "./AnomalyChart";

const DETECTOR_LABELS: Record<string, string> = {
  zscore: "z-score (used)",
  isolation_forest: "IsolationForest",
  zscore_fixed_3sigma: "z-score, fixed |z| > 3",
};

function pct(value: number | undefined): string {
  if (value === undefined || !Number.isFinite(value)) return "—";
  return `${(value * 100).toFixed(0)}%`;
}

function useRunAnomaly(run: RunResult | undefined): AnomalyResult | null {
  const [fetched, setFetched] = useState<Record<string, AnomalyResult>>({});
  const runId = run?.run_id;
  const attached = run?.anomaly;

  useEffect(() => {
    if (!runId || attached || fetched[runId]) return;
    let alive = true;
    api
      .anomaly(runId)
      .then((report) => {
        if (alive) setFetched((all) => ({ ...all, [runId]: report }));
      })
      .catch((error: Error) => {
        if (alive) setFetched((all) => ({ ...all, [runId]: { error: error.message } }));
      });
    return () => {
      alive = false;
    };
  }, [runId, attached, fetched]);

  if (!run) return null;
  return attached ?? fetched[run.run_id] ?? null;
}

function EvaluationInfo() {
  const [open, setOpen] = useState(false);
  const [state, setState] = useState<{ data: AnomalyEvaluation | null; error: string }>({ data: null, error: "" });
  const box = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open || state.data || state.error) return;
    let alive = true;
    api
      .anomalyEvaluation()
      .then((data) => {
        if (alive) setState({ data, error: "" });
      })
      .catch((error: Error) => {
        if (alive) setState({ data: null, error: error.message });
      });
    return () => {
      alive = false;
    };
  }, [open, state]);

  useEffect(() => {
    if (!open) return;
    const onClick = (event: MouseEvent) => {
      if (box.current && !box.current.contains(event.target as Node)) setOpen(false);
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
    };
    document.addEventListener("mousedown", onClick);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("mousedown", onClick);
      document.removeEventListener("keydown", onKey);
    };
  }, [open]);

  const { data, error } = state;
  return (
    <div className="info-anchor" ref={box}>
      <button
        type="button"
        className="info-button"
        aria-label="How the detector was evaluated"
        aria-expanded={open}
        title="How the detector was evaluated"
        onClick={() => setOpen((value) => !value)}
      >
        <Info />
      </button>
      {open && (
        <div className="info-popover" role="dialog" aria-label="How the detector was evaluated">
          <div className="row">
            <strong>How the detector was evaluated</strong>
            <div className="spacer" />
            <button type="button" className="info-close" aria-label="Close" onClick={() => setOpen(false)}>
              <X />
            </button>
          </div>
          {!data && !error && (
            <p className="muted small">
              <LoaderCircle className="spin inline" /> Loading…
            </p>
          )}
          {error && <p className="small danger-text">{error}</p>}
          {data && <EvaluationDetails data={data} />}
        </div>
      )}
    </div>
  );
}

function EvaluationDetails({ data }: { data: AnomalyEvaluation }) {
  return (
    <>
      <p className="small">
        Fitted on normal simulated runs only, thresholds tuned on {data.results.split_runs.validation} validation runs, and
        scored on {data.results.split_runs.test} held-out runs it never saw.
      </p>
      <table className="data compact">
        <thead>
          <tr>
            <th>Detector</th>
            <th className="num">Precision</th>
            <th className="num">Recall</th>
            <th className="num">F1</th>
            <th className="num">False alarms</th>
          </tr>
        </thead>
        <tbody>
          {Object.entries(data.results.models).map(([name, m]) => (
            <tr key={name}>
              <td>{DETECTOR_LABELS[name] ?? name}</td>
              <td className="num">{fmt(m.test.precision, 2)}</td>
              <td className="num">{fmt(m.test.recall, 2)}</td>
              <td className={`num${name === data.results.primary ? " best" : ""}`}>{fmt(m.test.f1, 2)}</td>
              <td className="num">{pct(m.test_runs.clean_window_false_alarm_rate)}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <p className="muted small">
        False alarms are the share of seconds flagged in runs with no injected event. Recall stays below 100% because some
        events change nothing measurable.
      </p>
    </>
  );
}

function AnomalyKpis({ summary }: { summary: AnomalyReport["summary"] }) {
  return (
    <div className="anomaly-kpis">
      <div>
        <span>{summary.flagged}</span>seconds flagged of {summary.windows}
      </div>
      <div>
        <span>
          {summary.caught}/{summary.disturbed}
        </span>
        event seconds flagged
      </div>
      <div>
        <span>{summary.false_alarms}</span>flagged outside events
      </div>
      <div>
        <span>{summary.first_flag_delay_s === null ? "—" : `${fmt(summary.first_flag_delay_s, 0)} s`}</span>delay to first flag
      </div>
    </div>
  );
}

function FlaggedSpans({ spans }: { spans: FlagSpan[] }) {
  if (spans.length === 0) return <p className="muted small">No second of this run was flagged.</p>;
  return (
    <div>
      <strong className="small">What was flagged</strong>
      <ul className="facts">
        {spans.map((span) => (
          <li key={span.start}>
            <code>
              {fmt(span.start, 0)}–{fmt(span.end, 0)} s
            </code>{" "}
            {describeSpan(span)}
            {!span.insideEvent && <span className="muted"> (outside the injected event window)</span>}
          </li>
        ))}
      </ul>
    </div>
  );
}

function RunReport({ report }: { report: AnomalyReport }) {
  const [showComparison, setShowComparison] = useState(false);
  const [hoverT, setHoverT] = useState<number | null>(null);
  const result = verdict(report);
  const summary = report.summary;

  return (
    <>
      <div className={`notice ${result.tone === "neutral" ? "" : result.tone}`}>
        {result.tone === "success" ? <CircleCheck /> : <TriangleAlert />}
        {result.text}
      </div>
      <AnomalyKpis summary={summary} />
      <div className="row wrap">
        <div className="anomaly-legend">
          <span>
            <i className="swatch band" /> injected event window
          </span>
          <span>
            <i className="swatch flag" /> flagged inside the event
          </span>
          <span>
            <i className="swatch outside" /> flagged outside it
          </span>
          {showComparison && (
            <span>
              <i className="swatch compare" /> IsolationForest flag
            </span>
          )}
        </div>
        <div className="spacer" />
        <label className="check">
          <input type="checkbox" checked={showComparison} onChange={(e) => setShowComparison(e.target.checked)} />
          Compare with IsolationForest ({summary.if_flagged} flagged)
        </label>
      </div>
      <div className="anomaly-grid">
        {ANOMALY_METRICS.map((m) => (
          <AnomalyChart
            key={m.value}
            report={report}
            metric={m.value}
            showComparison={showComparison}
            hoverT={hoverT}
            onHover={setHoverT}
          />
        ))}
      </div>
      <AnomalyReadout report={report} hoverT={hoverT} />
      <FlaggedSpans spans={flagSpans(report.windows, report.window_s)} />
      {report.disturbances.length > 0 && summary.false_alarms > 0 && (
        <p className="muted small">
          Flags just after an event often mark real after-effects, such as routes reconverging after a restore, that fall outside
          the labelled window.
        </p>
      )}
    </>
  );
}

export function AnomaliesPage() {
  const { runs, focusRunId, setFocusRunId } = useWorkbench();
  const run = runs.find((r) => r.run_id === focusRunId) ?? runs[0];
  const result = useRunAnomaly(run);
  if (!run) return null;
  return (
    <section className="card">
      <div className="card-head">
        <div>
          <div className="row">
            <h3>
              {algorithmLabel(run.algorithm)} run <span className="muted mono small">{run.run_id}</span>
            </h3>
            <EvaluationInfo />
          </div>
          <p>Each second of this run is scored by the z-score detector; a second is flagged when one metric moves far outside what normal runs show.</p>
        </div>
        <div className="spacer" />
        {runs.length > 1 && (
          <Segmented
            label="Algorithm"
            value={run.run_id}
            onChange={setFocusRunId}
            options={runs.map((r) => ({ value: r.run_id, label: algorithmLabel(r.algorithm) }))}
          />
        )}
      </div>
      <div className="card-body stack" style={{ "--gap": "16px" } as React.CSSProperties}>
        {result === null && (
          <p className="muted">
            <LoaderCircle className="spin inline" /> Scoring the run…
          </p>
        )}
        {result && !isReport(result) && (
          <div className="notice danger">
            <CircleAlert />
            {result.error}
          </div>
        )}
        {isReport(result) && <RunReport key={result.run_id} report={result} />}
      </div>
    </section>
  );
}
