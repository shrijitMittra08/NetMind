import type { WindowRow } from "../../domain/replay";

export type WindowMetric = "avg_latency_ms" | "throughput_mbps" | "loss_pct" | "max_util_pct";

export const WINDOW_METRICS: { value: WindowMetric; label: string }[] = [
  { value: "avg_latency_ms", label: "Latency (ms)" },
  { value: "throughput_mbps", label: "Throughput (Mbps)" },
  { value: "loss_pct", label: "Loss (%)" },
  { value: "max_util_pct", label: "Max utilisation (%)" },
];

const WIDTH = 320;
const HEIGHT = 150;
const MARGIN_LEFT = 36;
const MARGIN_RIGHT = 8;
const MARGIN_TOP = 10;
const MARGIN_BOTTOM = 22;
const PLOT_WIDTH = WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
const TINY = 1e-9;

export function MetricChart({
  windows,
  metric,
  time,
  endTime,
  onSeek,
  flags = [],
  bands = [],
}: {
  windows: WindowRow[];
  metric: WindowMetric;
  time: number;
  endTime: number;
  onSeek: (time: number) => void;
  flags?: number[];
  bands?: { start: number; end: number }[];
}) {
  const points = windows.filter((row) => Number.isFinite(row.t_start) && Number.isFinite(row[metric]));
  if (points.length === 0) return <p className="muted small">No values for this metric.</p>;

  const maxT = Math.max(endTime, TINY);
  const maxV = Math.max(...points.map((p) => p[metric]), TINY);
  const x = (t: number) => MARGIN_LEFT + (t / maxT) * PLOT_WIDTH;
  const y = (v: number) => MARGIN_TOP + (1 - v / maxV) * (HEIGHT - MARGIN_TOP - MARGIN_BOTTOM);
  const path = points.map((p, i) => `${i ? "L" : "M"}${x(p.t_start).toFixed(1)},${y(p[metric]).toFixed(1)}`).join("");
  const area = `${path}L${x(points[points.length - 1]!.t_start)},${y(0)}L${x(points[0]!.t_start)},${y(0)}Z`;
  const marker = x(Math.min(time, maxT));

  return (
    <svg
      className="metric-chart"
      viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
      role="img"
      aria-label="Metric over time"
      onClick={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        const px = ((event.clientX - rect.left) / rect.width) * WIDTH;
        onSeek(Math.min(maxT, Math.max(0, ((px - MARGIN_LEFT) / PLOT_WIDTH) * maxT)));
      }}
    >
      {bands.map((band, i) => (
        <rect
          key={`b${i}`}
          x={x(band.start)}
          width={Math.max(1, x(Math.min(band.end, maxT)) - x(band.start))}
          y={MARGIN_TOP}
          height={y(0) - MARGIN_TOP}
          className="event-band"
        />
      ))}
      <line x1={MARGIN_LEFT} x2={WIDTH - MARGIN_RIGHT} y1={y(0)} y2={y(0)} className="axis" />
      <line x1={MARGIN_LEFT} x2={WIDTH - MARGIN_RIGHT} y1={y(maxV)} y2={y(maxV)} className="grid" />
      <text x={MARGIN_LEFT - 6} y={y(maxV) + 4} textAnchor="end" className="tick">
        {maxV.toFixed(maxV >= 10 ? 0 : 2)}
      </text>
      <text x={MARGIN_LEFT - 6} y={y(0) + 4} textAnchor="end" className="tick">
        0
      </text>
      <text x={WIDTH - MARGIN_RIGHT} y={HEIGHT - 4} textAnchor="end" className="tick">
        {maxT.toFixed(1)} s
      </text>
      <path d={area} className="area" />
      <path d={path} className="line" />
      {flags.map((t) => (
        <rect key={`f${t}`} x={x(t) + 0.3} width={Math.max(1, x(Math.min(t + 1, maxT)) - x(t) - 0.6)} y={y(0) + 2} height={4} className="anomaly-mark">
          <title>{`Anomaly flagged at ${t.toFixed(0)} s`}</title>
        </rect>
      ))}
      <line x1={marker} x2={marker} y1={MARGIN_TOP} y2={y(0)} className="playhead" />
    </svg>
  );
}
