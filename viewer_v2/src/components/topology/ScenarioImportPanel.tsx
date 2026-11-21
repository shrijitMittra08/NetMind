import { useState } from "react";
import { ArrowRight, FileStack, LoaderCircle, TriangleAlert, Upload } from "lucide-react";
import { api } from "../../lib/api";
import { parseScenarioJson, scenarioSettings } from "../../domain/scenario";
import { findSetupProblems } from "../../domain/setup";
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

export function ScenarioImportPanel() {
  const { applyScenario, goTo } = useWorkbench();
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [dragging, setDragging] = useState(false);
  const [loaded, setLoaded] = useState<Loaded | null>(null);

  async function read(file: File | undefined) {
    if (!file) return;
    setError("");
    setLoaded(null);
    setBusy(true);
    try {
      const scenario = parseScenarioJson(await file.text(), file.name);
      let { topology, settings, warnings } = scenario;
      let topologyName = `${scenario.name}.json`;
      if (!topology) {
        const resolved = await api.resolveScenarioTopology(scenario.document);
        topology = resolved.topology;
        topologyName = resolved.topology_name;
        ({ settings, warnings } = scenarioSettings(scenario.document, topology));
      }
      applyScenario(topology, topologyName, settings);
      setLoaded({
        file: file.name,
        name: scenario.name,
        topologyName,
        flows: settings.flows.length,
        events: settings.events.length,
        warnings,
        problems: findSetupProblems(topology, settings),
      });
    } catch (problem) {
      setError(`${file.name}: ${(problem as Error).message}`);
    } finally {
      setBusy(false);
    }
  }

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
