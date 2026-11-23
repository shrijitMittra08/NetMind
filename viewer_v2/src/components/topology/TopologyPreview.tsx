import { layoutTopology } from "../../domain/replay";
import type { Topology } from "../../lib/types";

const COLOR = { HOST: "var(--host)", ROUTER: "var(--router)", SWITCH: "var(--switch)" } as const;
const PREVIEW_WIDTH = 320;
const PADDING = 18;

export function TopologyPreview({ topology, height = 150 }: { topology: Topology; height?: number }) {
  const layout = layoutTopology(topology);
  const toPixel = (id: number) => {
    const position = layout.get(id) ?? { x: 0.5, y: 0.5 };
    return {
      x: PADDING + position.x * (PREVIEW_WIDTH - PADDING * 2),
      y: PADDING + position.y * (height - PADDING * 2),
    };
  };

  return (
    <svg className="topology-preview" viewBox={`0 0 ${PREVIEW_WIDTH} ${height}`} role="img" aria-label="Topology preview">
      {topology.links.map((link, i) => {
        const a = toPixel(link.a);
        const b = toPixel(link.b);
        return <line key={i} x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="var(--link)" strokeWidth={1.6} />;
      })}
      {topology.nodes.map((node) => {
        const position = toPixel(node.id);
        return (
          <g key={node.id}>
            <circle cx={position.x} cy={position.y} r={6} fill={COLOR[node.type]} />
            <text x={position.x} y={position.y + 16} textAnchor="middle" fontSize={9} fill="var(--muted)">
              {node.name}
            </text>
          </g>
        );
      })}
    </svg>
  );
}
