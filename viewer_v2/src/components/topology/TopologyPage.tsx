import { useMemo, useState } from "react";
import { ArrowRight, FileStack, FileUp, Info, PencilRuler, X } from "lucide-react";
import { toTopology } from "../../domain/topology";
import type { NodeType, Topology } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import { Segmented } from "../ui";
import { Builder } from "./Builder";
import { ImportPanel } from "./ImportPanel";
import { ScenarioImportPanel } from "./ScenarioImportPanel";
import { TopologyPreview } from "./TopologyPreview";

type TopologyMode = "build" | "import" | "scenario";

const NODE_TYPES: readonly NodeType[] = ["HOST", "ROUTER", "SWITCH"];
const DEFAULT_QUEUE_CAPACITY = 64;

function comparableJson(topology: Topology | null): string {
  const nodes = (topology?.nodes ?? []).map(({ id, type, name, ip }) => ({ id, type, name, ip }));
  const links = (topology?.links ?? []).map(({ a, b, bandwidthMbps, latencyMs, lossRate, queueCapacity }) => ({
    a,
    b,
    bandwidthMbps,
    latencyMs,
    lossRate,
    queueCapacity: queueCapacity ?? DEFAULT_QUEUE_CAPACITY,
  }));
  return JSON.stringify({ nodes, links });
}

export function TopologyPage() {
  const { topology, topologyName, applyTopology, clearTopology, builder, goTo } = useWorkbench();
  const canvasDiffers = useMemo(() => {
    if (builder.nodes.length === 0 && !topology) return false;
    return comparableJson(toTopology(builder)) !== comparableJson(topology);
  }, [builder, topology]);
  const [mode, setMode] = useState<TopologyMode>("build");

  return (
    <div className="topology-page">
      <section className="card">
        <div className="card-head">
          <Segmented
            label="Topology source"
            value={mode}
            onChange={setMode}
            options={[
              { value: "build", label: "Build", icon: <PencilRuler /> },
              { value: "import", label: "Import topology", icon: <FileUp /> },
              { value: "scenario", label: "Import scenario", icon: <FileStack /> },
            ]}
          />
        </div>
        <div className="card-body">
          {mode === "build" && canvasDiffers && (
            <div className="notice small" style={{ marginBottom: 12 }}>
              <Info />
              {topology
                ? "The canvas differs from the active topology. Simulations keep using the active topology until you click “Use this topology”."
                : "Click “Use this topology” to simulate what is on the canvas."}
            </div>
          )}
          {mode === "build" && <Builder />}
          {mode === "import" && <ImportPanel onLoaded={applyTopology} />}
          {mode === "scenario" && <ScenarioImportPanel />}
        </div>
      </section>

      <aside className="card active-topology">
        <div className="card-head">
          <div>
            <h3>Active topology</h3>
            <p>Used by the simulation</p>
          </div>
        </div>
        <div className="card-body stack">
          {topology ? (
            <>
              <div className="row wrap">
                <strong className="mono">{topologyName || "untitled.json"}</strong>
                <div className="spacer" />
                <button
                  type="button"
                  className="btn ghost danger sm"
                  title="Stop using this topology. The canvas, simulation settings and finished runs are kept."
                  onClick={clearTopology}
                >
                  <X /> Remove
                </button>
              </div>
              <TopologyPreview topology={topology} />
              <div className="stat-row">
                <div>
                  <span>{topology.nodes.length}</span>nodes
                </div>
                <div>
                  <span>{topology.links.length}</span>links
                </div>
                {NODE_TYPES.map((type) => {
                  const count = topology.nodes.filter((node) => node.type === type).length;
                  if (count === 0) return null;
                  return (
                    <div key={type}>
                      <span>{count}</span>
                      {type.toLowerCase()}s
                    </div>
                  );
                })}
              </div>
              <button type="button" className="btn primary" onClick={() => goTo("setup")}>
                Configure simulation <ArrowRight />
              </button>
            </>
          ) : (
            <p className="muted">No topology yet. Build one on the canvas, or import a topology or scenario file.</p>
          )}
        </div>
      </aside>
    </div>
  );
}
