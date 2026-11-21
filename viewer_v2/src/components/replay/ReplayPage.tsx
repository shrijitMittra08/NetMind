import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { ChevronLeft, ChevronRight, LoaderCircle, Pause, Play, PlayCircle, RotateCcw } from "lucide-react";
import { buildReplayModel, jumpTargets, stateAt } from "../../domain/replay";
import type { ReplayModel, TraceEvent } from "../../domain/replay";
import { api } from "../../lib/api";
import { algorithmLabel, fmt } from "../../lib/format";
import { useWorkbench } from "../../state/workbench";
import { EmptyState, Segmented, SelectField } from "../ui";
import { MetricChart, WINDOW_METRICS } from "./MetricChart";
import type { WindowMetric } from "./MetricChart";
import { ReplayStage } from "./ReplayStage";
import { Timeline } from "./Timeline";

const SPEEDS = [0.1, 0.25, 0.5, 1, 2, 5, 10];
const EPS = 1e-9;

function useReplayModel(runId: string | null) {
  const cache = useRef(new Map<string, ReplayModel>());
  const [state, setState] = useState<{ runId: string | null; model: ReplayModel | null; error: string }>({
    runId: null,
    model: null,
    error: "",
  });

  useEffect(() => {
    if (!runId) return;
    const cached = cache.current.get(runId);
    if (cached) {
      setState({ runId, model: cached, error: "" });
      return;
    }
    let alive = true;
    setState({ runId, model: null, error: "" });
    api
      .runFiles(runId)
      .then(({ files }) => {
        const model = buildReplayModel(files, runId);
        cache.current.set(runId, model);
        if (alive) setState({ runId, model, error: "" });
      })
      .catch((error: Error) => alive && setState({ runId, model: null, error: error.message }));
    return () => {
      alive = false;
    };
  }, [runId]);

  return state.runId === runId ? state : { runId, model: null, error: "" };
}

function describeEvent(event: TraceEvent, model: ReplayModel): string {
  const names = new Map(model.topology.nodes.map((n) => [String(n.id), n.name]));
  if (event.type.startsWith("LINK_")) {
    const link = model.topology.links[Number(event.location) - 1];
    return link ? `${names.get(String(link.a))}–${names.get(String(link.b))}` : event.location;
  }
  if (event.type.startsWith("ROUTER_")) return names.get(event.location) ?? event.location;
  return event.location;
}

function eventTone(type: string): string {
  if (/DOWN|DROPPED/.test(type)) return "danger";
  if (/DEGRADED/.test(type)) return "warning";
  if (/UP$/.test(type)) return "success";
  if (type === "ROUTE_UPDATE") return "accent";
  return "";
}

export function ReplayPage() {
  const { runs } = useWorkbench();
  const [runId, setRunId] = useState<string | null>(runs[0]?.run_id ?? null);
  const activeRunId = runs.some((r) => r.run_id === runId) ? runId : (runs[0]?.run_id ?? null);
  const { model, error } = useReplayModel(activeRunId);

  const [time, setTime] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [selected, setSelected] = useState<number | null>(null);
  const [metric, setMetric] = useState<WindowMetric>("avg_latency_ms");
  const [loadedFor, setLoadedFor] = useState<ReplayModel | null>(null);

  if (model && model !== loadedFor) {
    setLoadedFor(model);
    setTime(0);
    setPlaying(false);
  }

  const endTime = model?.endTime ?? 0;
  const seek = useCallback((t: number) => setTime(Math.min(endTime, Math.max(0, t))), [endTime]);

  useEffect(() => {
    if (!playing) return;
    let frame = 0;
    let last = performance.now();
    const tick = (now: number) => {
      const elapsed = (now - last) / 1000;
      last = now;
      setTime((t) => {
        const next = Math.min(endTime, t + elapsed * speed);
        if (next >= endTime) setPlaying(false);
        return next;
      });
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [playing, speed, endTime]);

  const targets = useMemo(() => (model ? jumpTargets(model) : []), [model]);
  const next = targets.find((t) => t > time + EPS);
  const previous = [...targets].reverse().find((t) => t < time - EPS);

  const recentEvents = useMemo(() => {
    if (!model) return [];
    let lo = 0;
    let hi = model.events.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (model.events[mid]!.time <= time + EPS) lo = mid + 1;
      else hi = mid;
    }
    return model.events.slice(Math.max(0, lo - 60), lo).reverse();
  }, [model, time]);

  if (runs.length === 0) {
    return (
      <section className="card">
        <EmptyState icon={<PlayCircle />} title="Nothing to replay yet">
          <p>Run a simulation, then replay it here.</p>
        </EmptyState>
      </section>
    );
  }

  const run = runs.find((r) => r.run_id === activeRunId);
  const node = model?.topology.nodes.find((n) => n.id === selected);

  return (
    <div className="replay-page">
      <div className="row wrap replay-head">
        {runs.length > 1 && (
          <Segmented
            label="Run to replay"
            value={activeRunId ?? ""}
            onChange={(id) => {
              setRunId(id);
              setSelected(null);
            }}
            options={runs.map((r) => ({ value: r.run_id, label: algorithmLabel(r.algorithm) }))}
          />
        )}
        {model && (
          <span className="muted small">
            {run?.run_id} · {model.weight} weight · seed {model.seed} · {model.traceRows.toLocaleString()} trace rows ·{" "}
            {model.drops.length.toLocaleString()} drops
          </span>
        )}
      </div>

      {error && <div className="notice danger">{error}</div>}
      {!model && !error && (
        <section className="card">
          <EmptyState icon={<LoaderCircle className="spin" />} title="Loading run files…" />
        </section>
      )}

      {model && (
        <>
          <div className="replay-grid">
            <section className="card stage">
              <div className="stage-canvas-wrap">
                <ReplayStage model={model} time={time} selected={selected} onSelect={setSelected} />
                <div className="legend">
                  <span>
                    <i className="dot packet" /> packet
                  </span>
                  <span>
                    <i className="dash down" /> down
                  </span>
                  <span>
                    <i className="dash degraded" /> degraded
                  </span>
                  <span>
                    <i className="x">×</i> drop
                  </span>
                </div>
                <div className="clock mono">
                  {time.toFixed(3)}
                  <small> / {endTime.toFixed(3)} s</small>
                </div>
              </div>
              <div className="transport">
                <button type="button" className="btn icon" aria-label="Restart" onClick={() => seek(0)}>
                  <RotateCcw />
                </button>
                <button
                  type="button"
                  className="btn icon"
                  aria-label="Previous incident"
                  disabled={previous === undefined}
                  onClick={() => previous !== undefined && seek(previous)}
                >
                  <ChevronLeft />
                </button>
                <button
                  type="button"
                  className="btn primary icon play"
                  aria-label={playing ? "Pause" : "Play"}
                  onClick={() => {
                    if (!playing && time >= endTime) setTime(0);
                    setPlaying(!playing);
                  }}
                >
                  {playing ? <Pause /> : <Play />}
                </button>
                <button
                  type="button"
                  className="btn icon"
                  aria-label="Next incident"
                  title="Next failure, recovery or drop burst"
                  disabled={next === undefined}
                  onClick={() => next !== undefined && seek(next)}
                >
                  <ChevronRight />
                </button>
                <Timeline model={model} time={time} onSeek={seek} />
                <select className="speed" aria-label="Playback speed" value={speed} onChange={(e) => setSpeed(Number(e.target.value))}>
                  {SPEEDS.map((s) => (
                    <option key={s} value={s}>
                      {s}×
                    </option>
                  ))}
                </select>
              </div>
            </section>

            <div className="replay-side">
              <section className="card">
                <div className="card-head">
                  <h3>{node ? node.name : "Inspector"}</h3>
                  {node && <span className={`badge kind-${node.type.toLowerCase()}`}>{node.type.toLowerCase()}</span>}
                </div>
                <div className="card-body">
                  {node ? <Inspector model={model} nodeId={node.id} time={time} /> : <p className="muted">Click a device on the map.</p>}
                </div>
              </section>

              <section className="card">
                <div className="card-head">
                  <h3>Window metrics</h3>
                  <div className="spacer" />
                </div>
                <div className="card-body stack" style={{ "--gap": "10px" } as React.CSSProperties}>
                  <SelectField label="Metric" value={metric} options={WINDOW_METRICS} onChange={setMetric} />
                  <MetricChart windows={model.windows} metric={metric} time={time} endTime={endTime} onSeek={seek} />
                </div>
              </section>
            </div>
          </div>

          <div className="replay-bottom">
            <section className="card">
              <div className="card-head">
                <h3>Events</h3>
                <p>Network events up to the playhead, newest first</p>
              </div>
              <div className="event-log">
                {recentEvents.length === 0 && <p className="muted card-body">No events yet at this time.</p>}
                {recentEvents.map((event) => (
                  <button key={event.order} type="button" className="event-row" onClick={() => seek(event.time)}>
                    <span className="mono muted">{event.time.toFixed(3)}</span>
                    <span className={`badge ${eventTone(event.type)}`}>{event.type}</span>
                    <span className="event-where">{describeEvent(event, model)}</span>
                    <span className="muted small event-detail">{event.detail}</span>
                  </button>
                ))}
              </div>
            </section>

            <section className="card">
              <div className="card-head">
                <h3>Flows</h3>
                <p>End-of-run totals</p>
              </div>
              <div className="table-wrap">
                <table className="data">
                  <thead>
                    <tr>
                      <th>Flow</th>
                      <th>Path</th>
                      <th className="num">Sent</th>
                      <th className="num">Delivered</th>
                      <th className="num">Dropped</th>
                      <th className="num">Loss %</th>
                      <th className="num">Latency ms</th>
                    </tr>
                  </thead>
                  <tbody>
                    {model.flows.map((flow) => (
                      <tr key={flow.flowId}>
                        <td>#{flow.flowId}</td>
                        <td>
                          {flow.src} → {flow.dst}
                        </td>
                        <td className="num">{fmt(flow.sent, 0)}</td>
                        <td className="num">{fmt(flow.delivered, 0)}</td>
                        <td className="num">{fmt(flow.dropped, 0)}</td>
                        <td className="num">{fmt(flow.lossPct, 2)}</td>
                        <td className="num">{fmt(flow.avgLatencyMs, 2)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </section>
          </div>
        </>
      )}
    </div>
  );
}

function Inspector({ model, nodeId, time }: { model: ReplayModel; nodeId: number; time: number }) {
  const node = model.topology.nodes.find((n) => n.id === nodeId)!;
  const names = new Map(model.topology.nodes.map((n) => [n.id, n.name]));
  const linkState = stateAt(model.linkIntervals, time);
  const down = stateAt(model.routerIntervals, time).get(String(nodeId)) === "down";
  const links = model.topology.links
    .map((link, index) => ({ link, index }))
    .filter(({ link }) => link.a === nodeId || link.b === nodeId);
  const routeUpdate = [...model.events].reverse().find((e) => e.type === "ROUTE_UPDATE" && e.time <= time + EPS);
  const flows = model.flows.filter((f) => f.src === node.name || f.dst === node.name);

  return (
    <div className="stack inspector-body" style={{ "--gap": "12px" } as React.CSSProperties}>
      <dl className="kv">
        <dt>IP</dt>
        <dd className="mono">{node.ip}</dd>
        <dt>State</dt>
        <dd>{down ? <span className="badge danger">down</span> : <span className="badge success">up</span>}</dd>
      </dl>
      <div>
        <span className="eyebrow">Links</span>
        <ul className="link-list">
          {links.map(({ link, index }) => {
            const peer = names.get(link.a === nodeId ? link.b : link.a) ?? "?";
            const out = model.utilization.get(`${node.name}>${peer}`);
            const state = linkState.get(String(index + 1));
            return (
              <li key={index}>
                <span>{peer}</span>
                <span className="muted small">
                  {link.bandwidthMbps} Mbps · {out === undefined ? "idle" : `out ${fmt(out, 1)}% util`}
                </span>
                {state && <span className={`badge ${state === "down" ? "danger" : "warning"}`}>{state}</span>}
              </li>
            );
          })}
        </ul>
      </div>
      {flows.length > 0 && (
        <div>
          <span className="eyebrow">Flows</span>
          <ul className="link-list">
            {flows.map((f) => (
              <li key={f.flowId}>
                <span>
                  {f.src} → {f.dst}
                </span>
                <span className="muted small">{fmt(f.lossPct, 2)}% loss</span>
              </li>
            ))}
          </ul>
        </div>
      )}
      <p className="muted small">
        {routeUpdate
          ? `Latest route update at ${routeUpdate.time.toFixed(3)} s (${routeUpdate.detail}). The trace has no per-router forwarding tables.`
          : "No route update yet at this time."}
      </p>
    </div>
  );
}
