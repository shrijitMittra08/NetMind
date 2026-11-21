import { useMemo, useRef, useState } from "react";
import type { PointerEvent as ReactPointerEvent } from "react";
import { CircleAlert, Download, Eraser, Link2, MousePointer2, Trash2, TriangleAlert, Upload, Waypoints } from "lucide-react";
import {
  CANVAS,
  NODE_KINDS,
  addLink,
  addNode,
  fromTopology,
  removeLink,
  removeNode,
  toTopology,
  validateTopology,
} from "../../domain/topology";
import type { BuilderLink, BuilderNode } from "../../domain/topology";
import { saveTextFile } from "../../lib/saveFile";
import type { NodeType } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";

const R = 24;
type Selection = { kind: "node"; id: number } | { kind: "link"; uid: number } | null;
type Tool = "select" | "connect";

function NodeGlyph({ node, selected, pending, invalid }: { node: BuilderNode; selected: boolean; pending: boolean; invalid: boolean }) {
  const cls = `b-node ${node.type.toLowerCase()}${selected ? " selected" : ""}${pending ? " pending" : ""}${invalid ? " invalid" : ""}`;
  return (
    <g className={cls} transform={`translate(${node.x} ${node.y})`}>
      <circle className="halo" r={R + 7} />
      {node.type === "SWITCH" ? (
        <rect className="body" x={-R} y={-R * 0.72} width={R * 2} height={R * 1.44} rx={9} />
      ) : (
        <circle className="body" r={R} />
      )}
      <text className="glyph" textAnchor="middle" dy="0.35em">
        {NODE_KINDS[node.type].prefix}
      </text>
      <text className="name" textAnchor="middle" y={R + 17}>
        {node.name}
      </text>
      <text className="ip" textAnchor="middle" y={R + 31}>
        {node.ip}
      </text>
    </g>
  );
}

export function Builder() {
  const { builder: model, setBuilder: setModel, builderFileName, setBuilderFileName, topology, topologyName, applyTopology, goTo } =
    useWorkbench();
  const svgRef = useRef<SVGSVGElement>(null);
  const drag = useRef<{ id: number; dx: number; dy: number } | null>(null);
  const [tool, setTool] = useState<Tool>("select");
  const [selection, setSelection] = useState<Selection>(null);
  const [linkFrom, setLinkFrom] = useState<number | null>(null);
  const [ghost, setGhost] = useState<{ type: NodeType; x: number; y: number } | null>(null);
  const [hint, setHint] = useState("");
  const [applied, setApplied] = useState(false);

  const issues = useMemo(() => validateTopology(model), [model]);
  const errors = issues.filter((issue) => issue.level === "error");
  const nodeById = new Map(model.nodes.map((node) => [node.id, node]));
  const selectedNode = selection?.kind === "node" ? nodeById.get(selection.id) : undefined;
  const selectedLink = selection?.kind === "link" ? model.links.find((link) => link.uid === selection.uid) : undefined;
  const fileName = builderFileName.trim().replace(/\.json$/i, "").replace(/[^A-Za-z0-9_-]+/g, "_") || "topology";

  const edit: typeof setModel = (update) => {
    setApplied(false);
    setModel(update);
  };

  function toCanvas(clientX: number, clientY: number) {
    const svg = svgRef.current!;
    const point = svg.createSVGPoint();
    point.x = clientX;
    point.y = clientY;
    const mapped = point.matrixTransform(svg.getScreenCTM()!.inverse());
    return {
      x: Math.min(CANVAS.width - R, Math.max(R, mapped.x)),
      y: Math.min(CANVAS.height - R * 2, Math.max(R, mapped.y)),
    };
  }

  function place(type: NodeType, x: number, y: number) {
    const next = addNode(model, type, x, y);
    edit(next);
    setSelection({ kind: "node", id: next.nodes[next.nodes.length - 1]!.id });
  }

  function addAtFreeSpot(type: NodeType) {
    const i = model.nodes.length;
    place(type, 120 + (i % 7) * 120, 100 + Math.floor(i / 7) * 135);
  }

  function startPaletteDrag(event: ReactPointerEvent, type: NodeType) {
    if (event.button !== 0) return;
    event.preventDefault();
    const origin = { x: event.clientX, y: event.clientY };
    let moved = false;
    const move = (e: PointerEvent) => {
      if (!moved && Math.hypot(e.clientX - origin.x, e.clientY - origin.y) > 5) moved = true;
      if (moved) setGhost({ type, x: e.clientX, y: e.clientY });
    };
    const up = (e: PointerEvent) => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      setGhost(null);
      if (!moved) return addAtFreeSpot(type);
      const rect = svgRef.current!.getBoundingClientRect();
      if (e.clientX >= rect.left && e.clientX <= rect.right && e.clientY >= rect.top && e.clientY <= rect.bottom) {
        const { x, y } = toCanvas(e.clientX, e.clientY);
        place(type, x, y);
      }
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  }

  function onNodePointerDown(event: ReactPointerEvent<SVGGElement>, node: BuilderNode) {
    event.stopPropagation();
    if (tool === "connect") {
      if (linkFrom === null) {
        setLinkFrom(node.id);
        setHint(`Now click the node to connect ${node.name} to.`);
      } else if (linkFrom !== node.id) {
        const next = addLink(model, linkFrom, node.id);
        if (next === model) setHint("Those two nodes are already linked.");
        else {
          edit(next);
          setSelection({ kind: "link", uid: next.links[next.links.length - 1]!.uid });
          setHint("Linked. Click another node to start the next link.");
        }
        setLinkFrom(null);
      } else {
        setLinkFrom(null);
        setHint("");
      }
      return;
    }
    setSelection({ kind: "node", id: node.id });
    const start = toCanvas(event.clientX, event.clientY);
    drag.current = { id: node.id, dx: node.x - start.x, dy: node.y - start.y };
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function onNodePointerMove(event: ReactPointerEvent) {
    const current = drag.current;
    if (!current) return;
    const { x, y } = toCanvas(event.clientX, event.clientY);
    edit((m) => ({
      ...m,
      nodes: m.nodes.map((n) => (n.id === current.id ? { ...n, x: Math.round(x + current.dx), y: Math.round(y + current.dy) } : n)),
    }));
  }

  const updateNode = (id: number, patch: Partial<BuilderNode>) =>
    edit((m) => ({ ...m, nodes: m.nodes.map((n) => (n.id === id ? { ...n, ...patch } : n)) }));
  const updateLink = (uid: number, patch: Partial<BuilderLink>) =>
    edit((m) => ({ ...m, links: m.links.map((l) => (l.uid === uid ? { ...l, ...patch } : l)) }));

  function deleteSelection() {
    if (selectedNode) edit(removeNode(model, selectedNode.id));
    if (selectedLink) edit(removeLink(model, selectedLink.uid));
    setSelection(null);
  }

  function chooseTool(next: Tool) {
    setTool(next);
    setLinkFrom(null);
    setHint(next === "connect" ? "Click a node, then the node to connect it to." : "");
  }

  function applyToSimulation() {
    applyTopology(toTopology(model), `${fileName}.json`);
    setApplied(true);
  }

  return (
    <div className="builder">
      <div className="builder-toolbar">
        <div className="palette" aria-label="Network elements">
          {(Object.keys(NODE_KINDS) as NodeType[]).map((type) => (
            <button
              key={type}
              type="button"
              className={`palette-item ${type.toLowerCase()}`}
              onPointerDown={(event) => startPaletteDrag(event, type)}
              onKeyDown={(event) => {
                if (event.key === "Enter" || event.key === " ") {
                  event.preventDefault();
                  addAtFreeSpot(type);
                }
              }}
              title={`Drag onto the canvas, or click to add a ${NODE_KINDS[type].label.toLowerCase()}`}
            >
              <span className="palette-glyph">{NODE_KINDS[type].prefix}</span>
              {NODE_KINDS[type].label}
            </button>
          ))}
        </div>
        <div className="spacer" />
        <div className="segmented" role="group" aria-label="Canvas tool">
          <button type="button" aria-pressed={tool === "select"} onClick={() => chooseTool("select")}>
            <MousePointer2 /> Select
          </button>
          <button type="button" aria-pressed={tool === "connect"} onClick={() => chooseTool("connect")}>
            <Link2 /> Connect
          </button>
        </div>
      </div>

      <div className="builder-body">
        <div className="canvas-wrap">
          <svg
            ref={svgRef}
            className={`builder-canvas${tool === "connect" ? " connecting" : ""}`}
            viewBox={`0 0 ${CANVAS.width} ${CANVAS.height}`}
            role="application"
            aria-label="Topology canvas"
            tabIndex={0}
            onPointerDown={() => {
              setSelection(null);
              setLinkFrom(null);
            }}
            onKeyDown={(event) => {
              if ((event.key === "Delete" || event.key === "Backspace") && selection) {
                event.preventDefault();
                deleteSelection();
              } else if (event.key === "Escape") {
                setSelection(null);
                setLinkFrom(null);
              }
            }}
          >
            <defs>
              <pattern id="dots" width="24" height="24" patternUnits="userSpaceOnUse">
                <circle cx="2" cy="2" r="1.2" fill="var(--grid-dot)" />
              </pattern>
            </defs>
            <rect width={CANVAS.width} height={CANVAS.height} fill="url(#dots)" />

            {model.nodes.length === 0 && (
              <g className="canvas-empty">
                <text x={CANVAS.width / 2} y={CANVAS.height / 2 - 8} textAnchor="middle">
                  Drag a host, router or switch here
                </text>
                <text x={CANVAS.width / 2} y={CANVAS.height / 2 + 16} textAnchor="middle" className="sub">
                  then use Connect to link them
                </text>
              </g>
            )}

            {model.links.map((link) => {
              const a = nodeById.get(link.a);
              const b = nodeById.get(link.b);
              if (!a || !b) return null;
              const selected = selection?.kind === "link" && selection.uid === link.uid;
              const invalid = issues.some((issue) => issue.linkUid === link.uid);
              return (
                <g
                  key={link.uid}
                  className={`b-link${selected ? " selected" : ""}${invalid ? " invalid" : ""}`}
                  onPointerDown={(event) => {
                    event.stopPropagation();
                    setSelection({ kind: "link", uid: link.uid });
                  }}
                >
                  <line className="hit" x1={a.x} y1={a.y} x2={b.x} y2={b.y} />
                  <line className="wire" x1={a.x} y1={a.y} x2={b.x} y2={b.y} />
                  <text x={(a.x + b.x) / 2} y={(a.y + b.y) / 2 - 8} textAnchor="middle">
                    {link.bandwidthMbps} Mbps · {link.latencyMs} ms
                  </text>
                </g>
              );
            })}

            {model.nodes.map((node) => (
              <g
                key={node.id}
                onPointerDown={(event) => onNodePointerDown(event, node)}
                onPointerMove={onNodePointerMove}
                onPointerUp={() => (drag.current = null)}
                onPointerCancel={() => (drag.current = null)}
              >
                <NodeGlyph
                  node={node}
                  selected={selection?.kind === "node" && selection.id === node.id}
                  pending={linkFrom === node.id}
                  invalid={issues.some((issue) => issue.nodeId === node.id)}
                />
              </g>
            ))}
          </svg>
          <div className="canvas-hint" aria-live="polite">
            {hint || "Drag nodes to arrange them. Delete removes the selection, Esc clears it."}
          </div>
        </div>

        <aside className="inspector card">
          <div className="card-head">
            <h3>Properties</h3>
          </div>
          <div className="card-body stack">
            {!selectedNode && !selectedLink && (
              <p className="muted">Select a node or link on the canvas to edit it.</p>
            )}
            {selectedNode && (
              <>
                <span className={`badge kind-${selectedNode.type.toLowerCase()}`}>
                  {NODE_KINDS[selectedNode.type].label} · id {selectedNode.id}
                </span>
                <label className="field">
                  <span>Name</span>
                  <input value={selectedNode.name} onChange={(e) => updateNode(selectedNode.id, { name: e.target.value })} />
                </label>
                <label className="field">
                  <span>IPv4 address</span>
                  <input value={selectedNode.ip} onChange={(e) => updateNode(selectedNode.id, { ip: e.target.value })} />
                </label>
                <button type="button" className="btn danger sm" onClick={deleteSelection}>
                  <Trash2 /> Delete node
                </button>
              </>
            )}
            {selectedLink && (
              <>
                <span className="badge">
                  <Waypoints /> {nodeById.get(selectedLink.a)?.name} — {nodeById.get(selectedLink.b)?.name}
                </span>
                {(
                  [
                    ["bandwidthMbps", "Bandwidth (Mbps)", "any"],
                    ["latencyMs", "Latency (ms)", "any"],
                    ["lossRate", "Loss rate (0–1)", "0.001"],
                    ["queueCapacity", "Queue capacity (packets)", "1"],
                  ] as const
                ).map(([key, label, step]) => (
                  <label className="field" key={key}>
                    <span>{label}</span>
                    <input
                      type="number"
                      min={0}
                      step={step}
                      value={selectedLink[key]}
                      onChange={(e) => updateLink(selectedLink.uid, { [key]: e.target.value })}
                    />
                  </label>
                ))}
                <button type="button" className="btn danger sm" onClick={deleteSelection}>
                  <Trash2 /> Delete link
                </button>
              </>
            )}
          </div>
        </aside>
      </div>

      <div className={`validation ${errors.length ? "has-errors" : issues.length ? "has-warnings" : "ok"}`} aria-live="polite">
        {issues.length === 0 ? (
          <span className="badge success">Topology is valid</span>
        ) : (
          <ul>
            {issues.map((issue, index) => (
              <li key={index} className={issue.level}>
                {issue.level === "error" ? <CircleAlert /> : <TriangleAlert />}
                {issue.message}
              </li>
            ))}
          </ul>
        )}
      </div>

      <div className="builder-actions">
        <label className="field file-name">
          <span>File name</span>
          <input value={builderFileName} onChange={(e) => setBuilderFileName(e.target.value)} />
        </label>
        <button type="button" className="btn primary" disabled={errors.length > 0} onClick={applyToSimulation}>
          <Upload /> Use this topology
        </button>
        <button
          type="button"
          className="btn"
          disabled={errors.length > 0}
          onClick={() => void saveTextFile(JSON.stringify(toTopology(model), null, 2), `${fileName}.json`, "application/json")}
        >
          <Download /> Download .json
        </button>
        {topology && (
          <button
            type="button"
            className="btn ghost"
            onClick={() => {
              edit(fromTopology(topology));
              setBuilderFileName(topologyName.replace(/\.json$/i, "") || "topology");
              setSelection(null);
              setHint(`Loaded ${topologyName || "the active topology"} into the builder.`);
            }}
          >
            Edit active topology
          </button>
        )}
        <div className="spacer" />
        <button
          type="button"
          className="btn ghost danger"
          disabled={model.nodes.length === 0}
          onClick={() => {
            edit({ nodes: [], links: [] });
            setSelection(null);
          }}
        >
          <Eraser /> Clear
        </button>
      </div>
      {applied && (
        <div className="notice success">
          {fileName}.json is now the active topology.
          <button type="button" className="btn sm" onClick={() => goTo("setup")}>
            Continue to Simulation →
          </button>
        </div>
      )}

      {ghost && (
        <div className={`palette-ghost ${ghost.type.toLowerCase()}`} style={{ left: ghost.x, top: ghost.y }}>
          {NODE_KINDS[ghost.type].prefix}
        </div>
      )}
    </div>
  );
}
