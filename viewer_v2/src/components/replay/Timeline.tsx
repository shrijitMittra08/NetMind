import { useMemo } from "react";
import { dropHistogram } from "../../domain/replay";
import type { ReplayModel } from "../../domain/replay";

const BUCKETS = 120;

export function Timeline({ model, time, onSeek }: { model: ReplayModel; time: number; onSeek: (time: number) => void }) {
  const histogram = useMemo(() => dropHistogram(model, BUCKETS), [model]);
  const peak = Math.max(...histogram, 1);
  const end = Math.max(model.endTime, 1e-9);

  return (
    <div className="timeline">
      <div className="timeline-strip" aria-hidden>
        {histogram.map((count, i) => (
          <span key={i} style={{ height: `${count ? 18 + (count / peak) * 82 : 0}%` }} />
        ))}
        {model.incidents.map((incident, i) => (
          <i
            key={i}
            className={`marker ${incident.kind}`}
            style={{ left: `${(incident.time / end) * 100}%` }}
            title={`${incident.time.toFixed(3)} s · ${incident.label}`}
          />
        ))}
        <b className="progress" style={{ width: `${(time / end) * 100}%` }} />
      </div>
      <input
        type="range"
        min={0}
        max={end}
        step="any"
        value={time}
        aria-label="Replay time"
        onChange={(event) => onSeek(Number(event.target.value))}
      />
    </div>
  );
}
