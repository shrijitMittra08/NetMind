import type { AnomalyMetric, AnomalyReport, AnomalyWindow } from "../../lib/types";
import { describeDriver } from "../../domain/anomaly";
import { fmt } from "../../lib/format";

export const ANOMALY_METRICS: { value: AnomalyMetric; label: string; unit: string }[] = [
  { value: "avg_latency_ms", label: "Latency", unit: "ms" },
  { value: "loss_pct", label: "Packet loss", unit: "%" },
  { value: "throughput_mbps", label: "Throughput", unit: "Mbps" },
  { value: "max_util_pct", label: "Link utilisation", unit: "%" },
];

const W = 520;
const H = 200;
const LEFT = 42;
const RIGHT = 10;
const TOP = 22;
const BOTTOM = 44;
const TIME_TICK_STEPS = [1, 2, 5, 10, 15, 20, 30, 60, 120, 300];

function niceMax(value: number): number {
  if (value <= 0) return 1;
  const power = 10 ** Math.floor(Math.log10(value));
  const step = [1, 2, 2.5, 5].find((nice) => nice * power >= value) ?? 10;
  return step * power;
}

function tickLabel(value: number): string {
  if (Number.isInteger(value)) return fmt(value, 0);
  if (value >= 10) return fmt(value, 1);
  return fmt(value, 2);
}

export function windowAt(report: AnomalyReport, t: number | null): AnomalyWindow | null {
  if (t === null) return null;
  const step = report.window_s || 1;
  return report.windows.find((w) => t >= w.t && t < w.t + step) ?? null;
}

export function AnomalyChart({
  report,
  metric,
  showComparison,
  hoverT,
  onHover,
}: {
  report: AnomalyReport;
  metric: AnomalyMetric;
  showComparison: boolean;
  hoverT: number | null;
  onHover: (t: number | null) => void;
}) {
  const info = ANOMALY_METRICS.find((m) => m.value === metric)!;
  const windows = report.windows;
  if (windows.length === 0) return <p className="muted small">This run has no windows to show.</p>;

  const step = report.window_s || 1;
  const endT = Math.max(report.duration_s, windows[windows.length - 1]!.t + step);
  const maxV = niceMax(Math.max(...windows.map((w) => w[metric]), 1e-9));
  const x = (t: number) => LEFT + (t / endT) * (W - LEFT - RIGHT);
  const y = (v: number) => TOP + (1 - v / maxV) * (H - TOP - BOTTOM);
  const mid = (w: AnomalyWindow) => x(w.t + step / 2);
  const stripWidth = (w: AnomalyWindow) => Math.max(1, x(w.t + step) - x(w.t) - 1);

  const path = windows.map((w, i) => `${i ? "L" : "M"}${mid(w).toFixed(1)},${y(w[metric]).toFixed(1)}`).join("");
  const flagRow = H - BOTTOM + 12;
  const compareRow = H - BOTTOM + 21;
  const tickStep = TIME_TICK_STEPS.find((tick) => endT / tick <= 6) ?? 600;
  const ticks = Array.from({ length: Math.floor(endT / tickStep) + 1 }, (_, i) => i * tickStep);
  const flaggedWindows = windows.filter((w) => w.flagged);
  const hover = windowAt(report, hoverT);

  return (
    <figure className="anomaly-chart">
      <figcaption>
        {info.label} <span className="muted">({info.unit})</span>
      </figcaption>
      <svg
        viewBox={`0 0 ${W} ${H}`}
        role="img"
        aria-label={`${info.label} over time with anomaly flags`}
        onMouseLeave={() => onHover(null)}
        onMouseMove={(event) => {
          const rect = event.currentTarget.getBoundingClientRect();
          const px = ((event.clientX - rect.left) / rect.width) * W;
          const t = ((px - LEFT) / (W - LEFT - RIGHT)) * endT;
          onHover(t >= 0 && t < endT ? t : null);
        }}
      >
        {report.disturbances.map((d, i) => (
          <g key={i}>
            <rect x={x(d.start)} width={Math.max(1, x(d.end) - x(d.start))} y={TOP} height={H - TOP - BOTTOM} className="band" />
            <text x={x(d.start) + 2} y={TOP - 7} className="band-label">
              {d.label}
            </text>
          </g>
        ))}
        {[0, 0.5, 1].map((f) => (
          <g key={f}>
            <line x1={LEFT} x2={W - RIGHT} y1={y(maxV * f)} y2={y(maxV * f)} className={f === 0 ? "axis" : "grid"} />
            <text x={LEFT - 6} y={y(maxV * f) + 3} textAnchor="end" className="tick">
              {tickLabel(maxV * f)}
            </text>
          </g>
        ))}
        {ticks.map((t) => (
          <text key={t} x={x(t)} y={H - 3} textAnchor="middle" className="tick">
            {t.toFixed(0)} s
          </text>
        ))}
        <path d={path} className="line" />
        {flaggedWindows.map((w) => (
          <circle key={`f${w.t}`} cx={mid(w)} cy={y(w[metric])} r={3.4} className={`flag-dot${w.disturbed ? "" : " outside"}`} />
        ))}
        <text x={LEFT - 6} y={flagRow + 3} textAnchor="end" className="tick">
          z
        </text>
        {flaggedWindows.map((w) => (
          <rect key={`r${w.t}`} x={x(w.t) + 0.5} width={stripWidth(w)} y={flagRow - 3} height={6} className={`flag-strip${w.disturbed ? "" : " outside"}`} />
        ))}
        {showComparison && (
          <>
            <text x={LEFT - 6} y={compareRow + 3} textAnchor="end" className="tick">
              IF
            </text>
            {windows
              .filter((w) => w.if_flagged)
              .map((w) => (
                <rect key={`i${w.t}`} x={x(w.t) + 0.5} width={stripWidth(w)} y={compareRow - 3} height={6} className="compare-strip" />
              ))}
          </>
        )}
        {hover && <line x1={mid(hover)} x2={mid(hover)} y1={TOP} y2={H - BOTTOM} className="hover-line" />}
      </svg>
    </figure>
  );
}

export function AnomalyReadout({ report, hoverT }: { report: AnomalyReport; hoverT: number | null }) {
  const hover = windowAt(report, hoverT);
  const step = report.window_s || 1;
  if (!hover) return <p className="anomaly-readout small muted">Hover over any chart to inspect that second in all four.</p>;
  const metricsText = ANOMALY_METRICS.map((m) => ` · ${m.label.toLowerCase()} ${fmt(hover[m.value], 2)} ${m.unit}`).join("");
  return (
    <p className="anomaly-readout small">
      <strong>
        {hover.t.toFixed(0)}–{(hover.t + step).toFixed(0)} s
      </strong>
      {metricsText} · score{" "}
      {fmt(hover.score, 1)}
      {hover.flagged ? (
        <span className="flag-text"> · flagged: {describeDriver(hover.driver)}</span>
      ) : (
        <span className="muted"> · not flagged</span>
      )}
      {hover.disturbed && <span className="muted"> · inside the event window</span>}
    </p>
  );
}
