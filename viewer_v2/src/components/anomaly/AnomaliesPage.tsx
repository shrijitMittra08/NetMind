import { useEffect, useState } from "react";
import { Activity, CircleAlert, CircleCheck, ImageIcon, LoaderCircle, TriangleAlert } from "lucide-react";
import { api } from "../../lib/api";
import { algorithmLabel, fmt } from "../../lib/format";
import type { AnomalyEvaluation, AnomalyMetric, AnomalyResult, RunResult } from "../../lib/types";
import { describeSpan, flagSpans, isReport, verdict } from "../../domain/anomaly";
import { useWorkbench } from "../../state/workbench";
import { EmptyState, Segmented } from "../ui";
import { ANOMALY_METRICS, AnomalyChart } from "./AnomalyChart";

const DETECTOR_LABELS: Record<string, string> = {
  zscore: "z-score (used)",
  isolation_forest: "IsolationForest",
  zscore_fixed_3sigma: "z-score, fixed |z| > 3",
};

const PATTERN_LABELS: Record<string, string> = {
  burst: "Traffic burst",
  congestion: "Congestion",
  link_failure: "Link failure",
  router_failure: "Router failure",
};

const pct = (value: number | undefined) => (value === undefined || !Number.isFinite(value) ? "—" : `${(value * 100).toFixed(0)}%`);

function useRunAnomaly(run: RunResult | undefined): { result: AnomalyResult | null; loading: boolean } {
  const [fetched, setFetched] = useState<Record<string, AnomalyResult>>({});
  const runId = run?.run_id;
  const attached = run?.anomaly;

  useEffect(() => {
    if (!runId || attached || fetched[runId]) return;
    let alive = true;
    api
      .anomaly(runId)
      .then((report) => alive && setFetched((all) => ({ ...all, [runId]: report })))
      .catch((error: Error) => alive && setFetched((all) => ({ ...all, [runId]: { error: error.message } })));
    return () => {
      alive = false;
    };
  }, [runId, attached, fetched]);

  if (!run) return { result: null, loading: false };
  const result = attached ?? fetched[run.run_id] ?? null;
  return { result, loading: result === null };
}

function RunDetection() {
  const { runs } = useWorkbench();
  const [runId, setRunId] = useState(runs[0]?.run_id ?? "");
  const [metric, setMetric] = useState<AnomalyMetric>("loss_pct");
  const [showComparison, setShowComparison] = useState(false);
  const run = runs.find((r) => r.run_id === runId) ?? runs[0];
  const { result, loading } = useRunAnomaly(run);

  if (!run) {
    return (
      <section className="card">
        <EmptyState icon={<Activity />} title="No runs yet">
          <p>Run a simulation to see which seconds the detector flags. The evaluation below works without a run.</p>
        </EmptyState>
      </section>
    );
  }

  return (
    <section className="card">
      <div className="card-head">
        <div>
          <h3>Detected anomalies</h3>
          <p>Each second of the run is scored by the z-score detector; a second is flagged when one metric moves far outside what normal runs show.</p>
        </div>
        <div className="spacer" />
        {runs.length > 1 && (
          <Segmented
            label="Run"
            value={run.run_id}
            onChange={setRunId}
            options={runs.map((r) => ({ value: r.run_id, label: algorithmLabel(r.algorithm) }))}
          />
        )}
      </div>
      <div className="card-body stack" style={{ "--gap": "16px" } as React.CSSProperties}>
        {loading && (
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
        {isReport(result) && <RunReport report={result} metric={metric} setMetric={setMetric} showComparison={showComparison} setShowComparison={setShowComparison} />}
      </div>
    </section>
  );
}

function RunReport({
  report,
  metric,
  setMetric,
  showComparison,
  setShowComparison,
}: {
  report: Extract<AnomalyResult, { windows: unknown }>;
  metric: AnomalyMetric;
  setMetric: (metric: AnomalyMetric) => void;
  showComparison: boolean;
  setShowComparison: (value: boolean) => void;
}) {
  const v = verdict(report);
  const spans = flagSpans(report.windows, report.window_s);
  const s = report.summary;
  return (
    <>
      <div className={`notice ${v.tone === "neutral" ? "" : v.tone}`}>
        {v.tone === "success" ? <CircleCheck /> : <TriangleAlert />}
        {v.text}
      </div>
      <div className="anomaly-kpis">
        <div>
          <span>{s.flagged}</span>seconds flagged of {s.windows}
        </div>
        <div>
          <span>
            {s.caught}/{s.disturbed}
          </span>
          event seconds flagged
        </div>
        <div>
          <span>{s.false_alarms}</span>flagged outside events
        </div>
        <div>
          <span>{s.first_flag_delay_s === null ? "—" : `${fmt(s.first_flag_delay_s, 0)} s`}</span>delay to first flag
        </div>
      </div>
      <div className="row wrap">
        <Segmented label="Metric" value={metric} onChange={setMetric} options={ANOMALY_METRICS.map((m) => ({ value: m.value, label: m.label }))} />
        <div className="spacer" />
        <label className="check">
          <input type="checkbox" checked={showComparison} onChange={(e) => setShowComparison(e.target.checked)} />
          Compare with IsolationForest ({s.if_flagged} flagged)
        </label>
      </div>
      <AnomalyChart report={report} metric={metric} showComparison={showComparison} />
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
      {spans.length > 0 && (
        <details className="disclosure" open={spans.length <= 6}>
          <summary>What was flagged ({spans.length} period{spans.length === 1 ? "" : "s"})</summary>
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
        </details>
      )}
      {report.disturbances.length > 0 && s.false_alarms > 0 && (
        <p className="muted small">
          Flags just after an event often mark real after-effects, such as routes reconverging after a restore, that fall outside the labelled window.
        </p>
      )}
    </>
  );
}

function Evaluation() {
  const [state, setState] = useState<{ data: AnomalyEvaluation | null; error: string }>({ data: null, error: "" });

  useEffect(() => {
    let alive = true;
    api
      .anomalyEvaluation()
      .then((data) => alive && setState({ data, error: "" }))
      .catch((error: Error) => alive && setState({ data: null, error: error.message }));
    return () => {
      alive = false;
    };
  }, []);

  const { data, error } = state;
  const models = data ? Object.entries(data.results.models) : [];
  const patterns = data ? Object.keys(data.results.models[data.results.primary]?.test_recall_by_pattern ?? {}) : [];

  return (
    <section className="card">
      <div className="card-head">
        <div>
          <h3>How the detector was evaluated</h3>
          <p>
            Trained on normal simulated runs only, thresholds tuned on validation runs, scored on held-out runs that the detectors never saw.
          </p>
        </div>
      </div>
      <div className="card-body stack" style={{ "--gap": "18px" } as React.CSSProperties}>
        {!data && !error && (
          <p className="muted">
            <LoaderCircle className="spin inline" /> Loading the evaluation…
          </p>
        )}
        {error && (
          <div className="notice danger">
            <CircleAlert />
            {error}
          </div>
        )}
        {data && (
          <>
            <p className="muted small">
              {data.results.split_runs.train} training, {data.results.split_runs.validation} validation and {data.results.split_runs.test} test
              runs; {data.results.train_normal_windows} normal seconds used for fitting.
            </p>
            <div className="table-wrap">
              <table className="data">
                <thead>
                  <tr>
                    <th>Detector</th>
                    <th className="num">Precision</th>
                    <th className="num">Recall</th>
                    <th className="num">F1</th>
                    <th className="num">Disturbed runs caught</th>
                    <th className="num">False alarms on clean runs</th>
                  </tr>
                </thead>
                <tbody>
                  {models.map(([name, m]) => (
                    <tr key={name}>
                      <td>{DETECTOR_LABELS[name] ?? name}</td>
                      <td className="num">{fmt(m.test.precision, 2)}</td>
                      <td className="num">{fmt(m.test.recall, 2)}</td>
                      <td className={`num${name === data.results.primary ? " best" : ""}`}>{fmt(m.test.f1, 2)}</td>
                      <td className="num">{pct(m.test_runs.detection_rate)}</td>
                      <td className="num">{pct(m.test_runs.clean_window_false_alarm_rate)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            {patterns.length > 0 && (
              <div className="table-wrap">
                <table className="data">
                  <thead>
                    <tr>
                      <th>Recall by event type</th>
                      {models.map(([name]) => (
                        <th key={name} className="num">
                          {DETECTOR_LABELS[name] ?? name}
                        </th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {patterns.map((pattern) => (
                      <tr key={pattern}>
                        <td>{PATTERN_LABELS[pattern] ?? pattern}</td>
                        {models.map(([name, m]) => (
                          <td key={name} className="num">
                            {pct(m.test_recall_by_pattern[pattern])}
                          </td>
                        ))}
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
            <p className="muted small">
              Recall is capped below 100%: some injected events, such as a failure on a link no flow uses, change nothing measurable.
            </p>
            {data.plots.length > 0 ? (
              <div className="plot-gallery">
                {data.plots.map((plot) => (
                  <figure key={plot.name} className="plot">
                    <a href={plot.url} target="_blank" rel="noreferrer" title="Open full size">
                      <img src={plot.url} alt={plot.title} loading="lazy" />
                    </a>
                    <figcaption>{plot.title}</figcaption>
                  </figure>
                ))}
              </div>
            ) : (
              <div className="notice">
                <ImageIcon />
                No plots yet. Run python -m ml.anomaly from the python folder to generate them.
              </div>
            )}
          </>
        )}
      </div>
    </section>
  );
}

export function AnomaliesPage() {
  return (
    <div className="stack" style={{ "--gap": "20px" } as React.CSSProperties}>
      <RunDetection />
      <Evaluation />
    </div>
  );
}
