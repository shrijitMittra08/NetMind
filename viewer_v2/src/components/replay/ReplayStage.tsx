import { useEffect, useMemo, useRef, useState } from "react";
import { layoutTopology, packetsAt, stateAt } from "../../domain/replay";
import type { ElementState, ReplayModel } from "../../domain/replay";

const NODE_R = 20;
const PAD = 56;
const BOTTOM_EXTRA = 10;
const DROP_FADE_S = 0.7;
const BADGE_TEXT = { HOST: "PC", ROUTER: "R", SWITCH: "SW" };

type PointOf = (id: number) => { x: number; y: number };

function readThemeColors(canvas: HTMLCanvasElement) {
  const style = getComputedStyle(canvas);
  const cssVar = (name: string) => style.getPropertyValue(name).trim() || "#888";
  return {
    link: cssVar("--link"),
    warn: cssVar("--warning"),
    danger: cssVar("--danger"),
    packet: cssVar("--packet"),
    text: cssVar("--text"),
    muted: cssVar("--muted"),
    surface: cssVar("--surface"),
    accent: cssVar("--accent"),
    HOST: cssVar("--host"),
    ROUTER: cssVar("--router"),
    SWITCH: cssVar("--switch"),
  };
}

interface Scene {
  ctx: CanvasRenderingContext2D;
  colors: ReturnType<typeof readThemeColors>;
  font: string;
  pointOf: PointOf;
  model: ReplayModel;
  time: number;
}

function linkColor(state: ElementState | undefined, usage: number, colors: Scene["colors"]): string {
  if (state === "down") return colors.danger;
  if (state === "degraded") return colors.warn;
  if (usage >= 80) return colors.danger;
  if (usage >= 50) return colors.warn;
  return colors.link;
}

function drawLinks({ ctx, colors, font, pointOf, model, time }: Scene) {
  const nameById = new Map(model.topology.nodes.map((n) => [n.id, n.name]));
  const linkStates = stateAt(model.linkIntervals, time);
  model.topology.links.forEach((link, index) => {
    const a = pointOf(link.a);
    const b = pointOf(link.b);
    const state = linkStates.get(String(index + 1));
    const usage = Math.max(
      model.utilization.get(`${nameById.get(link.a)}>${nameById.get(link.b)}`) ?? 0,
      model.utilization.get(`${nameById.get(link.b)}>${nameById.get(link.a)}`) ?? 0,
    );
    ctx.strokeStyle = linkColor(state, usage, colors);
    ctx.lineWidth = 2 + Math.min(4, usage / 25);
    ctx.setLineDash(state ? [8, 6] : []);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.font = `11px ${font}`;
    ctx.textAlign = "center";
    ctx.fillStyle = state ? linkColor(state, 0, colors) : colors.muted;
    const label = state ? state.toUpperCase() : `${link.bandwidthMbps} Mbps`;
    ctx.fillText(label, (a.x + b.x) / 2, (a.y + b.y) / 2 - 8);
  });
}

function drawPackets({ ctx, colors, pointOf, model, time }: Scene) {
  const idByName = new Map(model.topology.nodes.map((n) => [n.name, n.id]));
  for (const { seg, fraction, opacity } of packetsAt(model, time)) {
    const fromId = idByName.get(seg.from);
    const toId = idByName.get(seg.to);
    if (fromId === undefined || toId === undefined) continue;
    const from = pointOf(fromId);
    const to = pointOf(toId);
    const x = from.x + (to.x - from.x) * fraction;
    const y = from.y + (to.y - from.y) * fraction;
    ctx.globalAlpha = opacity * 0.25;
    ctx.fillStyle = colors.packet;
    ctx.beginPath();
    ctx.arc(x, y, 9, 0, Math.PI * 2);
    ctx.fill();
    ctx.globalAlpha = opacity;
    ctx.beginPath();
    ctx.arc(x, y, 4.5, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.globalAlpha = 1;
}

function drawNodes({ ctx, colors, font, pointOf, model, time }: Scene, selected: number | null) {
  const routerStates = stateAt(model.routerIntervals, time);
  for (const node of model.topology.nodes) {
    const p = pointOf(node.id);
    const down = routerStates.get(String(node.id)) === "down";
    const tint = down ? colors.muted : colors[node.type];

    if (node.id === selected) {
      ctx.fillStyle = colors.accent;
      ctx.globalAlpha = 0.18;
      ctx.beginPath();
      ctx.arc(p.x, p.y, NODE_R + 10, 0, Math.PI * 2);
      ctx.fill();
      ctx.globalAlpha = 1;
    }

    ctx.fillStyle = colors.surface;
    ctx.strokeStyle = down ? colors.danger : tint;
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
    ctx.fillText(BADGE_TEXT[node.type], p.x, p.y + 0.5);
    ctx.textBaseline = "alphabetic";
    ctx.fillStyle = colors.text;
    ctx.font = `600 12px ${font}`;
    ctx.fillText(node.name, p.x, p.y + NODE_R + 16);
    if (down) {
      ctx.fillStyle = colors.danger;
      ctx.font = `700 10px ${font}`;
      ctx.fillText("DOWN", p.x, p.y - NODE_R - 8);
    }
  }
}

function drawDrops({ ctx, colors, font, pointOf, model, time }: Scene) {
  const idByName = new Map(model.topology.nodes.map((n) => [n.name, n.id]));
  for (const drop of model.drops) {
    const age = time - drop.time;
    const id = idByName.get(drop.location);
    if (age < 0 || age >= DROP_FADE_S || id === undefined) continue;
    const p = pointOf(id);
    ctx.globalAlpha = 1 - age / DROP_FADE_S;
    ctx.fillStyle = colors.danger;
    ctx.font = `700 22px ${font}`;
    ctx.textAlign = "left";
    ctx.fillText("×", p.x + NODE_R - 2, p.y - NODE_R + 4);
  }
  ctx.globalAlpha = 1;
}

export function ReplayStage({
  model,
  time,
  selected,
  onSelect,
}: {
  model: ReplayModel;
  time: number;
  selected: number | null;
  onSelect: (nodeId: number | null) => void;
}) {
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

  const pointOf = useMemo<PointOf>(() => {
    const { width, height } = size;
    return (id: number) => {
      const p = layout.get(id) ?? { x: 0.5, y: 0.5 };
      return { x: PAD + p.x * Math.max(1, width - PAD * 2), y: PAD + p.y * Math.max(1, height - PAD * 2 - BOTTOM_EXTRA) };
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
    const scene = { ctx, colors: readThemeColors(canvas), font: getComputedStyle(canvas).fontFamily, pointOf, model, time };
    drawLinks(scene);
    drawPackets(scene);
    drawNodes(scene, selected);
    drawDrops(scene);
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
