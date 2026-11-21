import { useMemo, useState } from "react";
import { ArrowRight, FileStack, FileUp, Info, PencilRuler, X } from "lucide-react";
import { toTopology } from "../../domain/topology";
import { useWorkbench } from "../../state/workbench";
import { Segmented } from "../ui";
import { Builder } from "./Builder";
import { ImportPanel } from "./ImportPanel";
import { ScenarioImportPanel } from "./ScenarioImportPanel";
import { TopologyPreview } from "./TopologyPreview";

export function TopologyPage() {
  const { topology, topologyName, applyTopology, clearTopology, builder, goTo } = useWorkbench();
  const canvasDiffers = useMemo(() => {
    if (builder.nodes.length === 0 && !topology) return false;
    const canvas = toTopology(builder);
    const strip = (t: typeof canvas | null) =>
      JSON.stringify({
        nodes: (t?.nodes ?? []).map(({ id, type, name, ip }) => ({ id, type, name, ip })),
        links: (t?.links ?? []).map(({ a, b, bandwidthMbps, latencyMs, lossRate, queueCapacity }) => ({
          a, b, bandwidthMbps, latencyMs, lossRate, queueCapacity: queueCapacity ?? 64,
        })),
      });
    return strip(canvas) !== strip(topology);
  }, [builder, topology]);
  const [mode, setMode] = useState<"build" | "import" | "scenario">("build");
  const counts = topology
    ? (["HOST", "ROUTER", "SWITCH"] as const).map((type) => [type, topology.nodes.filter((n) => n.type === type).length] as const)
    : [];

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
                {counts
                  .filter(([, count]) => count > 0)
                  .map(([type, count]) => (
                    <div key={type}>
                      <span>{count}</span>
                      {type.toLowerCase()}s
                    </div>
                  ))}
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
