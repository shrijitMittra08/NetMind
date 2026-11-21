import { layoutTopology } from "../../domain/replay";
import type { Topology } from "../../lib/types";

const COLOR = { HOST: "var(--host)", ROUTER: "var(--router)", SWITCH: "var(--switch)" } as const;

export function TopologyPreview({ topology, height = 150 }: { topology: Topology; height?: number }) {
  const width = 320;
  const pad = 18;
  const layout = layoutTopology(topology);
  const at = (id: number) => {
    const p = layout.get(id) ?? { x: 0.5, y: 0.5 };
    return { x: pad + p.x * (width - pad * 2), y: pad + p.y * (height - pad * 2) };
  };
  return (
    <svg className="topology-preview" viewBox={`0 0 ${width} ${height}`} role="img" aria-label="Topology preview">
      {topology.links.map((link, i) => {
        const a = at(link.a);
        const b = at(link.b);
        return <line key={i} x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="var(--link)" strokeWidth={1.6} />;
      })}
      {topology.nodes.map((node) => {
        const p = at(node.id);
        return (
          <g key={node.id}>
            <circle cx={p.x} cy={p.y} r={6} fill={COLOR[node.type]} />
            <text x={p.x} y={p.y + 16} textAnchor="middle" fontSize={9} fill="var(--muted)">
              {node.name}
            </text>
          </g>
        );
      })}
    </svg>
  );
}
