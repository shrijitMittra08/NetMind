import { useEffect, useState } from "react";
import { ArrowRight, FileStack, FolderOpen, LoaderCircle, TriangleAlert, Upload } from "lucide-react";
import { api } from "../../lib/api";
import { parseScenarioJson, scenarioSettings } from "../../domain/scenario";
import type { ParsedScenario } from "../../domain/scenario";
import { findSetupProblems } from "../../domain/setup";
import type { Topology } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import { TIGHT_STACK_STYLE } from "../ui";
import { JsonDropzone } from "./ImportPanel";

interface Loaded {
  name: string;
  topologyName: string;
  flows: number;
  events: number;
  notes: string[];
}

interface TopologyPreset {
  topology: Topology;
  topologyName: string;
}

const DEMO_DESCRIPTIONS: Record<string, string> = {
  s1_normal: "S1 · medium10, three steady flows, no events",
  s2_link_failure_dijkstra: "S2 · ring, R1-R2 fails at 20 s, Dijkstra (0.2 s reconvergence)",
  s2_link_failure_bellman_ford: "S2 · ring, same failure, Bellman-Ford (3 s reconvergence)",
  s3_congestion: "S3 · dumbbell, 2 × 30 Mbps over a 50 Mbps bottleneck, burst at 30 s",
  s4_router_failure_congestion: "S4 · ring, R2 fails at 15 s, R1-R2 congested at 40 s",
};

async function resolveScenario(scenario: ParsedScenario, preset?: TopologyPreset) {
  if (scenario.topology) {
    return {
      topology: scenario.topology,
      topologyName: `${scenario.name}.json`,
      settings: scenario.settings,
      warnings: scenario.warnings,
    };
  }
  let known = preset;
  if (!known) {
    const resolved = await api.resolveScenarioTopology(scenario.document);
    known = { topology: resolved.topology, topologyName: resolved.topology_name };
  }
  const { settings, warnings } = scenarioSettings(scenario.document, known.topology);
  return { topology: known.topology, topologyName: known.topologyName, settings, warnings };
}

function useSavedScenarioNames(): string[] {
  const [saved, setSaved] = useState<string[]>([]);
  useEffect(() => {
    let alive = true;
    api
      .listScenarios()
      .then(({ scenarios }) => {
        if (alive) setSaved(scenarios.map((s) => s.name));
      })
      .catch(() => {
        if (alive) setSaved([]);
      });
    return () => {
      alive = false;
    };
  }, []);
  return saved;
}

function SavedScenarioList({ names, busy, onOpen }: { names: string[]; busy: boolean; onOpen: (name: string) => void }) {
  const demos = names.filter((name) => name in DEMO_DESCRIPTIONS);
  const others = names.filter((name) => !(name in DEMO_DESCRIPTIONS));
  return (
    <div className="stack" style={TIGHT_STACK_STYLE}>
      <strong className="small">
        <FolderOpen className="inline" /> Or open a scenario from config/scenarios
      </strong>
      <div className="scenario-list">
        {[...demos, ...others].map((name) => (
          <button key={name} type="button" className="scenario-pick" disabled={busy} onClick={() => onOpen(name)}>
            {name in DEMO_DESCRIPTIONS ? (
              <>
                <span className="badge accent">Demo</span>
                <span>{DEMO_DESCRIPTIONS[name]}</span>
              </>
            ) : (
              <span className="mono">{name}</span>
            )}
          </button>
        ))}
      </div>
    </div>
  );
}

export function ScenarioImportPanel() {
  const { applyScenario, goTo } = useWorkbench();
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [loaded, setLoaded] = useState<Loaded | null>(null);
  const saved = useSavedScenarioNames();

  async function importScenario(fileName: string, getText: () => Promise<string>, preset?: TopologyPreset) {
    setError("");
    setLoaded(null);
    setBusy(true);
    try {
      const scenario = parseScenarioJson(await getText(), fileName);
      const { topology, topologyName, settings, warnings } = await resolveScenario(scenario, preset);
      applyScenario(topology, topologyName, settings);
      setLoaded({
        name: scenario.name,
        topologyName,
        flows: settings.flows.length,
        events: settings.events.length,
        notes: [...warnings, ...findSetupProblems(topology, settings)],
      });
    } catch (problem) {
      setError(`${fileName}: ${(problem as Error).message}`);
    } finally {
      setBusy(false);
    }
  }

  async function read(file: File | undefined) {
    if (file) await importScenario(file.name, () => file.text());
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

  return (
    <div className="stack">
      <JsonDropzone
        title="Drop a scenario JSON here"
        icon={busy ? <LoaderCircle className="spin" /> : <Upload />}
        disabled={busy}
        onFile={(file) => void read(file)}
      />
      {saved.length > 0 && <SavedScenarioList names={saved} busy={busy} onOpen={(name) => void openSaved(name)} />}
      {error && <div className="notice danger">{error}</div>}
      {loaded && (
        <div className="notice success">
          <FileStack />
          <div className="stack" style={TIGHT_STACK_STYLE}>
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
      {loaded && loaded.notes.length > 0 && (
        <div className="notice warning">
          <TriangleAlert />
          <ul>
            {loaded.notes.map((line) => (
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
