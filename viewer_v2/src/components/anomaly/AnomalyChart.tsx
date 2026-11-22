import { useState } from "react";
import type { AnomalyMetric, AnomalyReport, AnomalyWindow } from "../../lib/types";
import { describeDriver } from "../../domain/anomaly";
import { fmt } from "../../lib/format";

export const ANOMALY_METRICS: { value: AnomalyMetric; label: string; unit: string }[] = [
  { value: "avg_latency_ms", label: "Latency", unit: "ms" },
  { value: "loss_pct", label: "Loss", unit: "%" },
  { value: "throughput_mbps", label: "Throughput", unit: "Mbps" },
  { value: "max_util_pct", label: "Utilisation", unit: "%" },
];

const W = 720;
const H = 240;
const LEFT = 46;
const RIGHT = 12;
const TOP = 24;
const BOTTOM = 48;

function niceMax(value: number): number {
  if (value <= 0) return 1;
  const power = 10 ** Math.floor(Math.log10(value));
  const step = [1, 2, 2.5, 5, 10].find((s) => s * power >= value) ?? 10;
  return step * power;
}

export function AnomalyChart({
  report,
  metric,
  showComparison,
}: {
  report: AnomalyReport;
  metric: AnomalyMetric;
  showComparison: boolean;
}) {
  const [hover, setHover] = useState<AnomalyWindow | null>(null);
  const info = ANOMALY_METRICS.find((m) => m.value === metric)!;
  const windows = report.windows;
  if (windows.length === 0) return <p className="muted small">This run has no windows to show.</p>;

  const step = report.window_s || 1;
  const endT = Math.max(report.duration_s, windows[windows.length - 1]!.t + step);
  const maxV = niceMax(Math.max(...windows.map((w) => w[metric]), 1e-9));
  const x = (t: number) => LEFT + (t / endT) * (W - LEFT - RIGHT);
  const y = (v: number) => TOP + (1 - v / maxV) * (H - TOP - BOTTOM);
  const mid = (w: AnomalyWindow) => x(w.t + step / 2);
  const path = windows.map((w, i) => `${i ? "L" : "M"}${mid(w).toFixed(1)},${y(w[metric]).toFixed(1)}`).join("");
  const flagRow = H - BOTTOM + 14;
  const compareRow = H - BOTTOM + 24;
  const tickStep = [1, 2, 5, 10, 15, 20, 30, 60, 120, 300].find((s) => endT / s <= 8) ?? 600;
  const ticks = Array.from({ length: Math.floor(endT / tickStep) + 1 }, (_, i) => i * tickStep);

  return (
    <div className="anomaly-chart">
      <svg
        viewBox={`0 0 ${W} ${H}`}
        role="img"
        aria-label={`${info.label} over time with anomaly flags`}
        onMouseLeave={() => setHover(null)}
        onMouseMove={(event) => {
          const rect = event.currentTarget.getBoundingClientRect();
          const px = ((event.clientX - rect.left) / rect.width) * W;
          const t = ((px - LEFT) / (W - LEFT - RIGHT)) * endT;
          const found = windows.find((w) => t >= w.t && t < w.t + step) ?? null;
          setHover(found);
        }}
      >
        {report.disturbances.map((d, i) => (
          <g key={i}>
            <rect x={x(d.start)} width={Math.max(1, x(d.end) - x(d.start))} y={TOP} height={H - TOP - BOTTOM} className="band" />
            <text x={x(d.start) + 2} y={TOP - 8} className="band-label">
              {d.label}
            </text>
          </g>
        ))}
        {[0, 0.5, 1].map((f) => (
          <g key={f}>
            <line x1={LEFT} x2={W - RIGHT} y1={y(maxV * f)} y2={y(maxV * f)} className={f === 0 ? "axis" : "grid"} />
            <text x={LEFT - 6} y={y(maxV * f) + 3} textAnchor="end" className="tick">
              {fmt(maxV * f, Number.isInteger(maxV * f) ? 0 : maxV * f >= 10 ? 1 : 2)}
            </text>
          </g>
        ))}
        {ticks.map((t) => (
          <text key={t} x={x(t)} y={H - 4} textAnchor="middle" className="tick">
            {t.toFixed(0)} s
          </text>
        ))}
        <path d={path} className="line" />
        {windows
          .filter((w) => w.flagged)
          .map((w) => (
            <circle key={`f${w.t}`} cx={mid(w)} cy={y(w[metric])} r={3.6} className={`flag-dot${w.disturbed ? "" : " outside"}`} />
          ))}
        <text x={LEFT - 6} y={flagRow + 3} textAnchor="end" className="tick">
          z
        </text>
        {windows
          .filter((w) => w.flagged)
          .map((w) => (
            <rect key={`r${w.t}`} x={x(w.t) + 0.5} width={Math.max(1, x(w.t + step) - x(w.t) - 1)} y={flagRow - 3} height={6} className={`flag-strip${w.disturbed ? "" : " outside"}`} />
          ))}
        {showComparison && (
          <>
            <text x={LEFT - 6} y={compareRow + 3} textAnchor="end" className="tick">
              IF
            </text>
            {windows
              .filter((w) => w.if_flagged)
              .map((w) => (
                <rect key={`i${w.t}`} x={x(w.t) + 0.5} width={Math.max(1, x(w.t + step) - x(w.t) - 1)} y={compareRow - 3} height={6} className="compare-strip" />
              ))}
          </>
        )}
        {hover && <line x1={mid(hover)} x2={mid(hover)} y1={TOP} y2={H - BOTTOM} className="hover-line" />}
      </svg>
      <p className="anomaly-readout small">
        {hover ? (
          <>
            <strong>
              {hover.t.toFixed(0)}–{(hover.t + step).toFixed(0)} s
            </strong>{" "}
            · {info.label} {fmt(hover[metric], 2)} {info.unit}
            {metric !== "loss_pct" && ` · loss ${fmt(hover.loss_pct, 1)}%`} · score {fmt(hover.score, 1)}
            {hover.flagged ? (
              <span className="flag-text"> · flagged: {describeDriver(hover.driver)}</span>
            ) : (
              <span className="muted"> · not flagged</span>
            )}
            {hover.disturbed && <span className="muted"> · inside the event window</span>}
          </>
        ) : (
          <span className="muted">Hover over the chart to inspect a second.</span>
        )}
      </p>
    </div>
  );
}
