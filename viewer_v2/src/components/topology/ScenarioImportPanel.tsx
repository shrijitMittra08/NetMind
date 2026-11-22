import { useEffect, useState } from "react";
import { ArrowRight, FileStack, FolderOpen, LoaderCircle, TriangleAlert, Upload } from "lucide-react";
import { api } from "../../lib/api";
import { parseScenarioJson, scenarioSettings } from "../../domain/scenario";
import { findSetupProblems } from "../../domain/setup";
import type { Topology } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";

interface Loaded {
  file: string;
  name: string;
  topologyName: string;
  flows: number;
  events: number;
  warnings: string[];
  problems: string[];
}

const DEMO_DESCRIPTIONS: Record<string, string> = {
  s1_normal: "S1 · medium10, three steady flows, no events",
  s2_link_failure_dijkstra: "S2 · ring, R1-R2 fails at 20 s, Dijkstra (0.2 s reconvergence)",
  s2_link_failure_bellman_ford: "S2 · ring, same failure, Bellman-Ford (3 s reconvergence)",
  s3_congestion: "S3 · dumbbell, 2 × 30 Mbps over a 50 Mbps bottleneck, burst at 30 s",
  s4_router_failure_congestion: "S4 · ring, R2 fails at 15 s, R1-R2 congested at 40 s",
};

export function ScenarioImportPanel() {
  const { applyScenario, goTo } = useWorkbench();
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [dragging, setDragging] = useState(false);
  const [loaded, setLoaded] = useState<Loaded | null>(null);
  const [saved, setSaved] = useState<string[]>([]);

  useEffect(() => {
    let alive = true;
    api
      .listScenarios()
      .then(({ scenarios }) => alive && setSaved(scenarios.map((s) => s.name)))
      .catch(() => alive && setSaved([]));
    return () => {
      alive = false;
    };
  }, []);

  async function importScenario(fileName: string, getText: () => Promise<string>, preset?: { topology: Topology; topologyName: string }) {
    setError("");
    setLoaded(null);
    setBusy(true);
    try {
      const scenario = parseScenarioJson(await getText(), fileName);
      let { topology, settings, warnings } = scenario;
      let topologyName = `${scenario.name}.json`;
      if (!topology) {
        if (preset) {
          topology = preset.topology;
          topologyName = preset.topologyName;
        } else {
          const resolved = await api.resolveScenarioTopology(scenario.document);
          topology = resolved.topology;
          topologyName = resolved.topology_name;
        }
        ({ settings, warnings } = scenarioSettings(scenario.document, topology));
      }
      applyScenario(topology, topologyName, settings);
      setLoaded({
        file: fileName,
        name: scenario.name,
        topologyName,
        flows: settings.flows.length,
        events: settings.events.length,
        warnings,
        problems: findSetupProblems(topology, settings),
      });
    } catch (problem) {
      setError(`${fileName}: ${(problem as Error).message}`);
    } finally {
      setBusy(false);
    }
  }

  async function read(file: File | undefined) {
    if (!file) return;
    await importScenario(file.name, () => file.text());
  }

  async function openSaved(name: string) {
    setBusy(true);
    try {
      const result = await api.loadScenario(name);
      await importScenario(`${name}.json`, async () => JSON.stringify(result.scenario), {
        topology: result.topology,
        topologyName: result.topology_name,
      });
    } catch (problem) {
      setError(`${name}: ${(problem as Error).message}`);
      setBusy(false);
    }
  }

  const demos = saved.filter((name) => name in DEMO_DESCRIPTIONS);
  const others = saved.filter((name) => !(name in DEMO_DESCRIPTIONS));

  return (
    <div className="stack">
      <label
        className={`dropzone${dragging ? " dragging" : ""}`}
        onDragOver={(event) => {
          event.preventDefault();
          setDragging(true);
        }}
        onDragLeave={() => setDragging(false)}
        onDrop={(event) => {
          event.preventDefault();
          setDragging(false);
          void read(event.dataTransfer.files[0]);
        }}
      >
        <span className="dropzone-icon">{busy ? <LoaderCircle className="spin" /> : <Upload />}</span>
        <strong>Drop a scenario JSON here</strong>
        <span className="muted">or click to choose a file</span>
        <input
          type="file"
          accept=".json,application/json"
          className="sr-only"
          disabled={busy}
          onChange={(event) => {
            void read(event.target.files?.[0]);
            event.target.value = "";
          }}
        />
      </label>
      {saved.length > 0 && (
        <div className="stack" style={{ "--gap": "8px" } as React.CSSProperties}>
          <strong className="small">
            <FolderOpen className="inline" /> Or open a scenario from config/scenarios
          </strong>
          <div className="scenario-list">
            {demos.map((name) => (
              <button key={name} type="button" className="scenario-pick" disabled={busy} onClick={() => void openSaved(name)}>
                <span className="badge accent">Demo</span>
                <span>{DEMO_DESCRIPTIONS[name]}</span>
              </button>
            ))}
            {others.map((name) => (
              <button key={name} type="button" className="scenario-pick" disabled={busy} onClick={() => void openSaved(name)}>
                <span className="mono">{name}</span>
              </button>
            ))}
          </div>
        </div>
      )}
      {error && <div className="notice danger">{error}</div>}
      {loaded && (
        <div className="notice success">
          <FileStack />
          <div className="stack" style={{ "--gap": "8px" } as React.CSSProperties}>
            <span>
              Loaded scenario <strong>{loaded.name}</strong>: topology {loaded.topologyName}, {loaded.flows} flow(s) and{" "}
              {loaded.events} event(s). The simulation settings now hold this scenario.
            </span>
            <button type="button" className="btn primary sm" onClick={() => goTo("setup")}>
              Review simulation settings <ArrowRight />
            </button>
          </div>
        </div>
      )}
      {loaded && (loaded.warnings.length > 0 || loaded.problems.length > 0) && (
        <div className="notice warning">
          <TriangleAlert />
          <ul>
            {[...loaded.warnings, ...loaded.problems].map((line) => (
              <li key={line}>{line}</li>
            ))}
          </ul>
        </div>
      )}
      <p className="muted small">
        A scenario is a topology plus simulation settings (docs/contracts/scenario.md). Its <code>topology</code> is either
        embedded, as in files from “Download scenario”, or a path such as <code>config/topologies/ring_backup.json</code>.
        Sample files live in config/scenarios.
      </p>
    </div>
  );
}
