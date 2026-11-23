import { useEffect, useState } from "react";
import { CircleAlert, Download, FileCode2, ListChecks, LoaderCircle, Play, Save, SlidersHorizontal } from "lucide-react";
import { buildScenarioDocument } from "../../domain/scenario";
import { saveTextFile } from "../../lib/saveFile";
import { api, ApiError } from "../../lib/api";
import { algorithmLabel } from "../../lib/format";
import { ALGORITHMS } from "../../lib/types";
import type { Algorithm } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import type { SetupMode } from "../../state/workbench";
import { EmptyState, Segmented, TIGHT_STACK_STYLE } from "../ui";
import { InteractiveSetup } from "./InteractiveSetup";
import { ScriptEditor } from "./ScriptEditor";

const ELAPSED_REFRESH_MS = 500;
const SCENARIO_NAME_PATTERN = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;
const HTTP_CONFLICT = 409;

type SaveState = { kind: "idle" } | { kind: "busy" } | { kind: "ok"; message: string } | { kind: "exists" } | { kind: "error"; message: string };

function Elapsed({ since }: { since: number }) {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), ELAPSED_REFRESH_MS);
    return () => window.clearInterval(timer);
  }, []);
  return <>{Math.max(0, Math.round((now - since) / 1000))} s</>;
}

function SaveScenario() {
  const { topology, topologyName, activeSettings, problems } = useWorkbench();
  const [name, setName] = useState(() => topologyName.replace(/\.json$/i, "").replace(/[^A-Za-z0-9_-]+/g, "_"));
  const [state, setState] = useState<SaveState>({ kind: "idle" });
  const trimmedName = name.trim();
  const nameOk = SCENARIO_NAME_PATTERN.test(trimmedName);
  const blocked = !topology || problems.length > 0;

  async function save(overwrite: boolean) {
    if (!topology) return;
    setState({ kind: "busy" });
    try {
      const saved = await api.saveScenario(topology, topologyName, activeSettings, trimmedName, overwrite);
      setState({ kind: "ok", message: `Saved ${saved.scenario_path}` });
    } catch (error) {
      if (error instanceof ApiError && error.status === HTTP_CONFLICT) setState({ kind: "exists" });
      else setState({ kind: "error", message: (error as Error).message });
    }
  }

  function download() {
    if (!topology) return;
    const document = buildScenarioDocument(topology, topologyName, activeSettings, trimmedName);
    void saveTextFile(JSON.stringify(document, null, 2) + "\n", `${trimmedName}.json`, "application/json");
  }

  return (
    <section className="card">
      <div className="card-head">
        <div>
          <h3>Save as scenario</h3>
          <p>Topology and settings, to config/scenarios or a file</p>
        </div>
      </div>
      <div className="card-body stack">
        <div className="row">
          <label className="field spacer">
            <span className="sr-only">Scenario name</span>
            <input
              value={name}
              placeholder="scenario_name"
              onChange={(e) => {
                setName(e.target.value);
                setState({ kind: "idle" });
              }}
            />
          </label>
          <button type="button" className="btn" disabled={blocked || !nameOk || state.kind === "busy"} onClick={() => void save(false)}>
            <Save /> Save
          </button>
          <button
            type="button"
            className="btn"
            title="Download one self-contained .json (topology included) that “Import scenario” can load"
            disabled={blocked || !nameOk}
            onClick={download}
          >
            <Download /> Download
          </button>
        </div>
        {trimmedName && !nameOk && <p className="muted small">Letters, numbers, “_” and “-” only, starting with a letter or number.</p>}
        {state.kind === "ok" && <div className="notice success small">{state.message}</div>}
        {state.kind === "error" && <div className="notice danger small">{state.message}</div>}
        {state.kind === "exists" && (
          <div className="notice warning small">
            “{trimmedName}” already exists.
            <button type="button" className="btn sm" onClick={() => void save(true)}>
              Overwrite
            </button>
          </div>
        )}
      </div>
    </section>
  );
}

function runButtonLabel(running: boolean, setupMode: SetupMode): string {
  if (running) return "Running…";
  if (setupMode === "script") return "Run script";
  return "Run simulation";
}

function RunPanel() {
  const { setupMode, algorithms, setAlgorithms, problems, run, startRun, activeSettings } = useWorkbench();
  const running = run.status === "running";

  function toggleAlgorithm(name: Algorithm) {
    if (algorithms.includes(name)) setAlgorithms(algorithms.filter((a) => a !== name));
    else setAlgorithms([...algorithms, name]);
  }

  return (
    <section className="card run-panel">
      <div className="card-head">
        <div>
          <h3>Run</h3>
          <p>
            {activeSettings.config.duration_s} s · {activeSettings.flows.length} flow(s) · {activeSettings.events.length} event(s)
          </p>
        </div>
      </div>
      <div className="card-body stack">
        {setupMode === "interactive" ? (
          <div className="stack" style={TIGHT_STACK_STYLE}>
            <span className="eyebrow">Algorithms to compare</span>
            <div className="algo-chips">
              {ALGORITHMS.map((name) => (
                <button key={name} type="button" className="chip" aria-pressed={algorithms.includes(name)} onClick={() => toggleAlgorithm(name)}>
                  {algorithmLabel(name)}
                </button>
              ))}
            </div>
          </div>
        ) : (
          <p className="muted small">
            The script decides the algorithm (<code>set algo</code>). Each <code>export</code> becomes a run you can analyse.
          </p>
        )}
        {problems.length > 0 ? (
          <div className="notice warning">
            <ListChecks />
            <ul>
              {problems.map((problem) => (
                <li key={problem}>{problem}</li>
              ))}
            </ul>
          </div>
        ) : (
          <span className="badge success">Ready to run</span>
        )}
        <button type="button" className="btn primary lg" disabled={running || problems.length > 0} onClick={() => void startRun()}>
          {running ? <LoaderCircle className="spin" /> : <Play />}
          {runButtonLabel(running, setupMode)}
        </button>
        {running && (
          <p className="muted small run-progress">
            {run.label} · <Elapsed since={run.startedAt} />
          </p>
        )}
        {run.status === "error" && (
          <div className="notice danger">
            <CircleAlert />
            {run.error}
          </div>
        )}
      </div>
    </section>
  );
}

export function SetupPage() {
  const { topology, setupMode, setSetupMode, goTo } = useWorkbench();

  if (!topology) {
    return (
      <section className="card">
        <EmptyState icon={<SlidersHorizontal />} title="No topology yet">
          <p>Build or import a topology, or import a whole scenario, before configuring a simulation.</p>
          <button type="button" className="btn primary" onClick={() => goTo("topology")}>
            Go to Topology
          </button>
        </EmptyState>
      </section>
    );
  }

  return (
    <div className="setup-page">
      <div className="stack">
        <Segmented
          label="Setup mode"
          value={setupMode}
          onChange={setSetupMode}
          options={[
            { value: "interactive", label: "Interactive", icon: <SlidersHorizontal /> },
            { value: "script", label: "Script (CLI)", icon: <FileCode2 /> },
          ]}
        />
        {setupMode === "interactive" ? <InteractiveSetup /> : <ScriptEditor />}
      </div>
      <div className="setup-side">
        <RunPanel />
        <SaveScenario />
      </div>
    </div>
  );
}
