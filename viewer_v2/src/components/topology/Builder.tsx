import { useMemo, useRef, useState } from "react";
import type { KeyboardEvent as ReactKeyboardEvent, PointerEvent as ReactPointerEvent } from "react";
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

const NODE_RADIUS = 24;
const DRAG_THRESHOLD_PX = 5;
const FREE_SPOT = { perRow: 7, left: 120, top: 100, columnGap: 120, rowGap: 135 };
const LINK_FIELDS = [
  ["bandwidthMbps", "Bandwidth (Mbps)", "any"],
  ["latencyMs", "Latency (ms)", "any"],
  ["lossRate", "Loss rate (0–1)", "0.001"],
  ["queueCapacity", "Queue capacity (packets)", "1"],
] as const;

type Selection = { kind: "node"; id: number } | { kind: "link"; uid: number } | null;
type Tool = "select" | "connect";

function toSafeFileName(text: string): string {
  return text.trim().replace(/\.json$/i, "").replace(/[^A-Za-z0-9_-]+/g, "_") || "topology";
}

function withStates(base: string, states: { selected: boolean; pending?: boolean; invalid: boolean }): string {
  let className = base;
  if (states.selected) className += " selected";
  if (states.pending) className += " pending";
  if (states.invalid) className += " invalid";
  return className;
}

function validationClassName(errorCount: number, issueCount: number): string {
  if (errorCount > 0) return "has-errors";
  if (issueCount > 0) return "has-warnings";
  return "ok";
}

function NodeGlyph({ node, selected, pending, invalid }: { node: BuilderNode; selected: boolean; pending: boolean; invalid: boolean }) {
  return (
    <g className={withStates(`b-node ${node.type.toLowerCase()}`, { selected, pending, invalid })} transform={`translate(${node.x} ${node.y})`}>
      <circle className="halo" r={NODE_RADIUS + 7} />
      {node.type === "SWITCH" ? (
        <rect className="body" x={-NODE_RADIUS} y={-NODE_RADIUS * 0.72} width={NODE_RADIUS * 2} height={NODE_RADIUS * 1.44} rx={9} />
      ) : (
        <circle className="body" r={NODE_RADIUS} />
      )}
      <text className="glyph" textAnchor="middle" dy="0.35em">
        {NODE_KINDS[node.type].prefix}
      </text>
      <text className="name" textAnchor="middle" y={NODE_RADIUS + 17}>
        {node.name}
      </text>
      <text className="ip" textAnchor="middle" y={NODE_RADIUS + 31}>
        {node.ip}
      </text>
    </g>
  );
}

interface LinkWireProps {
  link: BuilderLink;
  from: BuilderNode;
  to: BuilderNode;
  selected: boolean;
  invalid: boolean;
  onSelect: () => void;
}

function LinkWire({ link, from, to, selected, invalid, onSelect }: LinkWireProps) {
  return (
    <g
      className={withStates("b-link", { selected, invalid })}
      onPointerDown={(event) => {
        event.stopPropagation();
        onSelect();
      }}
    >
      <line className="hit" x1={from.x} y1={from.y} x2={to.x} y2={to.y} />
      <line className="wire" x1={from.x} y1={from.y} x2={to.x} y2={to.y} />
      <text x={(from.x + to.x) / 2} y={(from.y + to.y) / 2 - 8} textAnchor="middle">
        {link.bandwidthMbps} Mbps · {link.latencyMs} ms
      </text>
    </g>
  );
}

export function Builder() {
  const { builder: model, setBuilder: setModel, builderFileName, setBuilderFileName, topology, topologyName, applyTopology, goTo } =
    useWorkbench();
  const svgRef = useRef<SVGSVGElement>(null);
  const nodeDrag = useRef<{ id: number; dx: number; dy: number } | null>(null);
  const [tool, setTool] = useState<Tool>("select");
  const [selection, setSelection] = useState<Selection>(null);
  const [linkFrom, setLinkFrom] = useState<number | null>(null);
  const [ghost, setGhost] = useState<{ type: NodeType; x: number; y: number } | null>(null);
  const [hint, setHint] = useState("");
  const [applied, setApplied] = useState(false);

  const issues = useMemo(() => validateTopology(model), [model]);
  const errorCount = issues.filter((issue) => issue.level === "error").length;
  const nodeById = new Map(model.nodes.map((node) => [node.id, node]));
  const selectedNode = selection?.kind === "node" ? nodeById.get(selection.id) : undefined;
  const selectedLink = selection?.kind === "link" ? model.links.find((link) => link.uid === selection.uid) : undefined;
  const fileName = toSafeFileName(builderFileName);

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
      x: Math.min(CANVAS.width - NODE_RADIUS, Math.max(NODE_RADIUS, mapped.x)),
      y: Math.min(CANVAS.height - NODE_RADIUS * 2, Math.max(NODE_RADIUS, mapped.y)),
    };
  }

  function place(type: NodeType, x: number, y: number) {
    const next = addNode(model, type, x, y);
    edit(next);
    setSelection({ kind: "node", id: next.nodes[next.nodes.length - 1]!.id });
  }

  function addAtFreeSpot(type: NodeType) {
    const count = model.nodes.length;
    const column = count % FREE_SPOT.perRow;
    const row = Math.floor(count / FREE_SPOT.perRow);
    place(type, FREE_SPOT.left + column * FREE_SPOT.columnGap, FREE_SPOT.top + row * FREE_SPOT.rowGap);
  }

  function startPaletteDrag(event: ReactPointerEvent, type: NodeType) {
    if (event.button !== 0) return;
    event.preventDefault();
    const origin = { x: event.clientX, y: event.clientY };
    let moved = false;

    const handleMove = (e: PointerEvent) => {
      if (!moved && Math.hypot(e.clientX - origin.x, e.clientY - origin.y) > DRAG_THRESHOLD_PX) moved = true;
      if (moved) setGhost({ type, x: e.clientX, y: e.clientY });
    };

    const handleUp = (e: PointerEvent) => {
      window.removeEventListener("pointermove", handleMove);
      window.removeEventListener("pointerup", handleUp);
      setGhost(null);
      if (!moved) {
        addAtFreeSpot(type);
        return;
      }
      const rect = svgRef.current!.getBoundingClientRect();
      if (e.clientX >= rect.left && e.clientX <= rect.right && e.clientY >= rect.top && e.clientY <= rect.bottom) {
        const { x, y } = toCanvas(e.clientX, e.clientY);
        place(type, x, y);
      }
    };

    window.addEventListener("pointermove", handleMove);
    window.addEventListener("pointerup", handleUp);
  }

  function handleConnectClick(node: BuilderNode) {
    if (linkFrom === null) {
      setLinkFrom(node.id);
      setHint(`Now click the node to connect ${node.name} to.`);
      return;
    }
    if (linkFrom === node.id) {
      setLinkFrom(null);
      setHint("");
      return;
    }
    const next = addLink(model, linkFrom, node.id);
    if (next === model) {
      setHint("Those two nodes are already linked.");
    } else {
      edit(next);
      setSelection({ kind: "link", uid: next.links[next.links.length - 1]!.uid });
      setHint("Linked. Click another node to start the next link.");
    }
    setLinkFrom(null);
  }

  function onNodePointerDown(event: ReactPointerEvent<SVGGElement>, node: BuilderNode) {
    event.stopPropagation();
    if (tool === "connect") {
      handleConnectClick(node);
      return;
    }
    setSelection({ kind: "node", id: node.id });
    const start = toCanvas(event.clientX, event.clientY);
    nodeDrag.current = { id: node.id, dx: node.x - start.x, dy: node.y - start.y };
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function onNodePointerMove(event: ReactPointerEvent) {
    const current = nodeDrag.current;
    if (!current) return;
    const { x, y } = toCanvas(event.clientX, event.clientY);
    const newX = Math.round(x + current.dx);
    const newY = Math.round(y + current.dy);
    edit((m) => ({ ...m, nodes: m.nodes.map((n) => (n.id === current.id ? { ...n, x: newX, y: newY } : n)) }));
  }

  function endNodeDrag() {
    nodeDrag.current = null;
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

  function clearSelection() {
    setSelection(null);
    setLinkFrom(null);
  }

  function onCanvasKeyDown(event: ReactKeyboardEvent<SVGSVGElement>) {
    if ((event.key === "Delete" || event.key === "Backspace") && selection) {
      event.preventDefault();
      deleteSelection();
    } else if (event.key === "Escape") {
      clearSelection();
    }
  }

  function chooseTool(next: Tool) {
    setTool(next);
    setLinkFrom(null);
    setHint(next === "connect" ? "Click a node, then the node to connect it to." : "");
  }

  function loadActiveTopology() {
    if (!topology) return;
    edit(fromTopology(topology));
    setBuilderFileName(topologyName.replace(/\.json$/i, "") || "topology");
    setSelection(null);
    setHint(`Loaded ${topologyName || "the active topology"} into the builder.`);
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
            onPointerDown={clearSelection}
            onKeyDown={onCanvasKeyDown}
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
              const from = nodeById.get(link.a);
              const to = nodeById.get(link.b);
              if (!from || !to) return null;
              return (
                <LinkWire
                  key={link.uid}
                  link={link}
                  from={from}
                  to={to}
                  selected={selection?.kind === "link" && selection.uid === link.uid}
                  invalid={issues.some((issue) => issue.linkUid === link.uid)}
                  onSelect={() => setSelection({ kind: "link", uid: link.uid })}
                />
              );
            })}
            {model.nodes.map((node) => (
              <g
                key={node.id}
                onPointerDown={(event) => onNodePointerDown(event, node)}
                onPointerMove={onNodePointerMove}
                onPointerUp={endNodeDrag}
                onPointerCancel={endNodeDrag}
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
            {!selectedNode && !selectedLink && <p className="muted">Select a node or link on the canvas to edit it.</p>}
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
                {LINK_FIELDS.map(([key, label, step]) => (
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

      <div className={`validation ${validationClassName(errorCount, issues.length)}`} aria-live="polite">
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
        <button
          type="button"
          className="btn primary"
          disabled={errorCount > 0}
          onClick={() => {
            applyTopology(toTopology(model), `${fileName}.json`);
            setApplied(true);
          }}
        >
          <Upload /> Use this topology
        </button>
        <button
          type="button"
          className="btn"
          disabled={errorCount > 0}
          onClick={() => void saveTextFile(JSON.stringify(toTopology(model), null, 2), `${fileName}.json`, "application/json")}
        >
          <Download /> Download .json
        </button>
        {topology && (
          <button type="button" className="btn ghost" onClick={loadActiveTopology}>
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
