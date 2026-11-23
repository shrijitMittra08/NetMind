import { createContext, useCallback, useContext, useEffect, useMemo, useState } from "react";
import type { ReactNode } from "react";
import { api } from "../lib/api";
import { ALGORITHMS } from "../lib/types";
import type { Algorithm, RunResult, ScriptRunResult, SimSettings, Topology } from "../lib/types";
import { parseScript } from "../domain/script";
import type { ParsedScript } from "../domain/script";
import { DEFAULT_CONFIG, findSetupProblems, makeFlow, repairForTopology } from "../domain/setup";
import { emptyModel } from "../domain/topology";
import type { BuilderModel } from "../domain/topology";
import { load, save } from "./storage";

export type Section = "topology" | "setup" | "results" | "anomalies" | "replay" | "insights";
export type SetupMode = "interactive" | "script";

interface RunState {
  status: "idle" | "running" | "error";
  startedAt: number;
  label: string;
  error: string;
}

interface Workbench {
  section: Section;
  goTo: (section: Section) => void;

  topology: Topology | null;
  topologyName: string;
  applyTopology: (topology: Topology, name: string) => void;
  applyScenario: (topology: Topology, name: string, settings: SimSettings) => void;
  clearTopology: () => void;

  builder: BuilderModel;
  setBuilder: (update: BuilderModel | ((current: BuilderModel) => BuilderModel)) => void;
  builderFileName: string;
  setBuilderFileName: (name: string) => void;

  settings: SimSettings;
  setSettings: (update: SimSettings | ((current: SimSettings) => SimSettings)) => void;
  setupMode: SetupMode;
  setSetupMode: (mode: SetupMode) => void;
  algorithms: Algorithm[];
  setAlgorithms: (algorithms: Algorithm[]) => void;

  script: string;
  setScript: (script: string) => void;
  parsedScript: ParsedScript;
  scriptResult: ScriptRunResult | null;
  exportResults: boolean;
  setExportResults: (value: boolean) => void;

  activeSettings: SimSettings;
  problems: string[];

  runs: RunResult[];
  focusRunId: string | null;
  setFocusRunId: (runId: string) => void;
  run: RunState;
  startRun: () => Promise<void>;
}

const WorkbenchContext = createContext<Workbench | null>(null);

function usePersistedState<T>(key: string, fallback: () => T) {
  const [value, setValue] = useState<T>(() => load(key, fallback()));
  useEffect(() => {
    save(key, value);
  }, [key, value]);
  return [value, setValue] as const;
}

function findProblems(
  setupMode: SetupMode,
  topology: Topology | null,
  settings: SimSettings,
  algorithms: Algorithm[],
  script: string,
  parsedScript: ParsedScript,
): string[] {
  if (setupMode === "interactive") {
    const list = findSetupProblems(topology, settings);
    if (algorithms.length === 0) list.push("Choose at least one routing algorithm to run.");
    return list;
  }
  const list = topology ? [] : ["Load or build a topology first."];
  if (!script.trim()) list.push("Write a script to run.");
  for (const note of parsedScript.notes) {
    if (note.level === "error") list.push(`Line ${note.line}: ${note.message}`);
  }
  return list;
}

export function WorkbenchProvider({ children }: { children: ReactNode }) {
  const [section, setSection] = useState<Section>("topology");
  const [topology, setTopology] = usePersistedState<Topology | null>("topology", () => null);
  const [topologyName, setTopologyName] = usePersistedState("topologyName", () => "");
  const [builder, setBuilder] = usePersistedState("builder", emptyModel);
  const [builderFileName, setBuilderFileName] = usePersistedState("builderFileName", () => "my_topology");
  const [settings, setSettings] = usePersistedState<SimSettings>("settings", () => ({
    config: DEFAULT_CONFIG,
    flows: [makeFlow(null)],
    events: [],
  }));
  const [setupMode, setSetupMode] = usePersistedState<SetupMode>("setupMode", () => "interactive");
  const [algorithms, setAlgorithms] = usePersistedState<Algorithm[]>("algorithms", () => [...ALGORITHMS]);
  const [script, setScript] = usePersistedState("script", () => "");
  const [exportResults, setExportResults] = useState(true);
  const [scriptResult, setScriptResult] = useState<ScriptRunResult | null>(null);
  const [runs, setRuns] = useState<RunResult[]>([]);
  const [focusRunId, setFocusRunId] = useState<string | null>(null);
  const [run, setRun] = useState<RunState>({ status: "idle", startedAt: 0, label: "", error: "" });

  const applyTopology = useCallback((next: Topology, name: string) => {
    setTopology(next);
    setTopologyName(name);
    setSettings((current) => repairForTopology(next, current));
    setScriptResult(null);
  }, [setTopology, setTopologyName, setSettings]);

  const applyScenario = useCallback((next: Topology, name: string, nextSettings: SimSettings) => {
    setTopology(next);
    setTopologyName(name);
    setSettings(nextSettings);
    setSetupMode("interactive");
    setScriptResult(null);
  }, [setTopology, setTopologyName, setSettings, setSetupMode]);

  const clearTopology = useCallback(() => {
    setTopology(null);
    setTopologyName("");
    setScriptResult(null);
  }, [setTopology, setTopologyName]);

  const parsedScript = useMemo(
    () => parseScript(script, topology, settings.config),
    [script, topology, settings.config],
  );
  const activeSettings = setupMode === "script" ? parsedScript.settings : settings;
  const problems = useMemo(
    () => findProblems(setupMode, topology, settings, algorithms, script, parsedScript),
    [setupMode, topology, settings, algorithms, script, parsedScript],
  );

  const startRun = useCallback(async () => {
    if (!topology || problems.length > 0) return;
    const scripted = setupMode === "script";
    const plural = algorithms.length === 1 ? "" : "s";
    const label = scripted ? "Running script" : `Simulating ${algorithms.length} algorithm${plural}`;
    setRun({ status: "running", startedAt: Date.now(), label, error: "" });
    if (scripted) setScriptResult(null);
    try {
      if (scripted) {
        const result = await api.runScript(topology, script, exportResults, parsedScript.settings);
        setScriptResult(result);
        if (result.runs.length > 0) {
          setRuns(result.runs);
          setFocusRunId(result.runs[0]!.run_id);
        }
      } else {
        const ordered = ALGORITHMS.filter((name) => algorithms.includes(name));
        const result = await api.runSimulation(topology, topologyName, settings, ordered);
        setRuns(result.runs);
        setFocusRunId(result.runs[0]?.run_id ?? null);
        setSection("results");
      }
      setRun((current) => ({ ...current, status: "idle" }));
    } catch (error) {
      setRun((current) => ({ ...current, status: "error", error: (error as Error).message }));
    }
  }, [topology, topologyName, problems, setupMode, algorithms, script, exportResults, parsedScript, settings]);

  const value: Workbench = {
    section, goTo: setSection,
    topology, topologyName, applyTopology, applyScenario, clearTopology,
    builder, setBuilder, builderFileName, setBuilderFileName,
    settings, setSettings, setupMode, setSetupMode, algorithms, setAlgorithms,
    script, setScript, parsedScript, scriptResult, exportResults, setExportResults,
    activeSettings, problems,
    runs, focusRunId, setFocusRunId, run, startRun,
  };

  return <WorkbenchContext.Provider value={value}>{children}</WorkbenchContext.Provider>;
}

export function useWorkbench(): Workbench {
  const value = useContext(WorkbenchContext);
  if (!value) throw new Error("useWorkbench must be used inside WorkbenchProvider");
  return value;
}
