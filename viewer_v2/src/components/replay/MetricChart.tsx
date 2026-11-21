import type { WindowRow } from "../../domain/replay";

export type WindowMetric = "avg_latency_ms" | "throughput_mbps" | "loss_pct" | "max_util_pct";

export const WINDOW_METRICS: { value: WindowMetric; label: string }[] = [
  { value: "avg_latency_ms", label: "Latency (ms)" },
  { value: "throughput_mbps", label: "Throughput (Mbps)" },
  { value: "loss_pct", label: "Loss (%)" },
  { value: "max_util_pct", label: "Max utilisation (%)" },
];

export function MetricChart({
  windows,
  metric,
  time,
  endTime,
  onSeek,
}: {
  windows: WindowRow[];
  metric: WindowMetric;
  time: number;
  endTime: number;
  onSeek: (time: number) => void;
}) {
  const W = 320;
  const H = 150;
  const left = 36;
  const right = 8;
  const top = 10;
  const bottom = 22;
  const points = windows.filter((row) => Number.isFinite(row.t_start) && Number.isFinite(row[metric]));
  if (points.length === 0) return <p className="muted small">No values for this metric.</p>;

  const maxT = Math.max(endTime, 1e-9);
  const maxV = Math.max(...points.map((p) => p[metric]), 1e-9);
  const x = (t: number) => left + (t / maxT) * (W - left - right);
  const y = (v: number) => top + (1 - v / maxV) * (H - top - bottom);
  const path = points.map((p, i) => `${i ? "L" : "M"}${x(p.t_start).toFixed(1)},${y(p[metric]).toFixed(1)}`).join("");
  const area = `${path}L${x(points[points.length - 1]!.t_start)},${y(0)}L${x(points[0]!.t_start)},${y(0)}Z`;
  const marker = x(Math.min(time, maxT));

  return (
    <svg
      className="metric-chart"
      viewBox={`0 0 ${W} ${H}`}
      role="img"
      aria-label="Metric over time"
      onClick={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        const px = ((event.clientX - rect.left) / rect.width) * W;
        onSeek(Math.min(maxT, Math.max(0, ((px - left) / (W - left - right)) * maxT)));
      }}
    >
      <line x1={left} x2={W - right} y1={y(0)} y2={y(0)} className="axis" />
      <line x1={left} x2={W - right} y1={y(maxV)} y2={y(maxV)} className="grid" />
      <text x={left - 6} y={y(maxV) + 4} textAnchor="end" className="tick">
        {maxV.toFixed(maxV >= 10 ? 0 : 2)}
      </text>
      <text x={left - 6} y={y(0) + 4} textAnchor="end" className="tick">
        0
      </text>
      <text x={W - right} y={H - 4} textAnchor="end" className="tick">
        {maxT.toFixed(1)} s
      </text>
      <path d={area} className="area" />
      <path d={path} className="line" />
      <line x1={marker} x2={marker} y1={top} y2={y(0)} className="playhead" />
    </svg>
  );
}
