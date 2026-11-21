import { useEffect, useMemo, useRef, useState } from "react";
import { layoutTopology, packetsAt, stateAt } from "../../domain/replay";
import type { ReplayModel } from "../../domain/replay";

const NODE_R = 20;
const PAD = 56;

interface Props {
  model: ReplayModel;
  time: number;
  selected: number | null;
  onSelect: (nodeId: number | null) => void;
}

function cssVar(el: Element, name: string): string {
  return getComputedStyle(el).getPropertyValue(name).trim() || "#888";
}

export function ReplayStage({ model, time, selected, onSelect }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState({ width: 0, height: 0 });
  const layout = useMemo(() => layoutTopology(model.topology), [model]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const observer = new ResizeObserver(([entry]) => {
      if (entry) setSize({ width: entry.contentRect.width, height: entry.contentRect.height });
    });
    observer.observe(canvas);
    return () => observer.disconnect();
  }, []);

  const pointOf = useMemo(() => {
    const { width, height } = size;
    return (id: number) => {
      const p = layout.get(id) ?? { x: 0.5, y: 0.5 };
      return { x: PAD + p.x * Math.max(1, width - PAD * 2), y: PAD + p.y * Math.max(1, height - PAD * 2 - 10) };
    };
  }, [layout, size]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || size.width === 0) return;
    const ctx = canvas.getContext("2d")!;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.floor(size.width * ratio);
    canvas.height = Math.floor(size.height * ratio);
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    ctx.clearRect(0, 0, size.width, size.height);

    const color = {
      link: cssVar(canvas, "--link"),
      ok: cssVar(canvas, "--success"),
      warn: cssVar(canvas, "--warning"),
      danger: cssVar(canvas, "--danger"),
      packet: cssVar(canvas, "--packet"),
      text: cssVar(canvas, "--text"),
      muted: cssVar(canvas, "--muted"),
      surface: cssVar(canvas, "--surface"),
      accent: cssVar(canvas, "--accent"),
      HOST: cssVar(canvas, "--host"),
      ROUTER: cssVar(canvas, "--router"),
      SWITCH: cssVar(canvas, "--switch"),
    };
    const names = new Map(model.topology.nodes.map((n) => [n.id, n.name]));
    const idByName = new Map(model.topology.nodes.map((n) => [n.name, n.id]));
    const linkState = stateAt(model.linkIntervals, time);
    const routerState = stateAt(model.routerIntervals, time);
    const font = getComputedStyle(canvas).fontFamily;

    model.topology.links.forEach((link, index) => {
      const a = pointOf(link.a);
      const b = pointOf(link.b);
      const state = linkState.get(String(index + 1));
      const usage = Math.max(
        model.utilization.get(`${names.get(link.a)}>${names.get(link.b)}`) ?? 0,
        model.utilization.get(`${names.get(link.b)}>${names.get(link.a)}`) ?? 0,
      );
      ctx.strokeStyle = state === "down" ? color.danger : state === "degraded" ? color.warn : usage >= 80 ? color.danger : usage >= 50 ? color.warn : color.link;
      ctx.lineWidth = 2 + Math.min(4, usage / 25);
      ctx.setLineDash(state ? [8, 6] : []);
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
      ctx.setLineDash([]);

      ctx.font = `11px ${font}`;
      ctx.textAlign = "center";
      ctx.fillStyle = color.muted;
      const label = state === "down" ? "DOWN" : state === "degraded" ? "DEGRADED" : `${link.bandwidthMbps} Mbps`;
      if (state) ctx.fillStyle = state === "down" ? color.danger : color.warn;
      ctx.fillText(label, (a.x + b.x) / 2, (a.y + b.y) / 2 - 8);
    });

    for (const { seg, fraction, opacity } of packetsAt(model, time)) {
      const fromId = idByName.get(seg.from);
      const toId = idByName.get(seg.to);
      if (fromId === undefined || toId === undefined) continue;
      const from = pointOf(fromId);
      const to = pointOf(toId);
      const x = from.x + (to.x - from.x) * fraction;
      const y = from.y + (to.y - from.y) * fraction;
      ctx.globalAlpha = opacity * 0.25;
      ctx.fillStyle = color.packet;
      ctx.beginPath();
      ctx.arc(x, y, 9, 0, Math.PI * 2);
      ctx.fill();
      ctx.globalAlpha = opacity;
      ctx.beginPath();
      ctx.arc(x, y, 4.5, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1;

    for (const node of model.topology.nodes) {
      const p = pointOf(node.id);
      const down = routerState.get(String(node.id)) === "down";
      const tint = down ? color.muted : color[node.type];
      if (node.id === selected) {
        ctx.fillStyle = color.accent;
        ctx.globalAlpha = 0.18;
        ctx.beginPath();
        ctx.arc(p.x, p.y, NODE_R + 10, 0, Math.PI * 2);
        ctx.fill();
        ctx.globalAlpha = 1;
      }
      ctx.fillStyle = color.surface;
      ctx.strokeStyle = down ? color.danger : tint;
      ctx.lineWidth = node.id === selected ? 3.5 : 2.5;
      ctx.beginPath();
      if (node.type === "SWITCH") ctx.roundRect(p.x - NODE_R, p.y - NODE_R * 0.72, NODE_R * 2, NODE_R * 1.44, 8);
      else ctx.arc(p.x, p.y, NODE_R, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();

      ctx.fillStyle = tint;
      ctx.font = `700 11px ${font}`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(node.type === "HOST" ? "PC" : node.type === "ROUTER" ? "R" : "SW", p.x, p.y + 0.5);
      ctx.textBaseline = "alphabetic";
      ctx.fillStyle = color.text;
      ctx.font = `600 12px ${font}`;
      ctx.fillText(node.name, p.x, p.y + NODE_R + 16);
      if (down) {
        ctx.fillStyle = color.danger;
        ctx.font = `700 10px ${font}`;
        ctx.fillText("DOWN", p.x, p.y - NODE_R - 8);
      }
    }

    for (const drop of model.drops) {
      const age = time - drop.time;
      if (age < 0 || age >= 0.7) continue;
      const id = idByName.get(drop.location);
      if (id === undefined) continue;
      const p = pointOf(id);
      ctx.globalAlpha = 1 - age / 0.7;
      ctx.fillStyle = color.danger;
      ctx.font = `700 22px ${font}`;
      ctx.textAlign = "left";
      ctx.fillText("×", p.x + NODE_R - 2, p.y - NODE_R + 4);
    }
    ctx.globalAlpha = 1;
  }, [model, time, selected, size, pointOf]);

  return (
    <canvas
      ref={canvasRef}
      className="stage-canvas"
      aria-label="Animated network replay; click a device to inspect it"
      onClick={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        const x = event.clientX - rect.left;
        const y = event.clientY - rect.top;
        const hit = model.topology.nodes.find((node) => {
          const p = pointOf(node.id);
          return Math.hypot(x - p.x, y - p.y) <= NODE_R + 6;
        });
        onSelect(hit ? hit.id : null);
      }}
    />
  );
}
