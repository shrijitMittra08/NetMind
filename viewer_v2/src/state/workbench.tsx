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

export type Section = "topology" | "setup" | "results" | "replay" | "insights";
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
  run: RunState;
  startRun: () => Promise<void>;
}

const WorkbenchContext = createContext<Workbench | null>(null);

export function WorkbenchProvider({ children }: { children: ReactNode }) {
  const [section, setSection] = useState<Section>("topology");
  const [topology, setTopology] = useState<Topology | null>(() => load("topology", null));
  const [topologyName, setTopologyName] = useState<string>(() => load("topologyName", ""));
  const [builder, setBuilder] = useState<BuilderModel>(() => load("builder", emptyModel()));
  const [builderFileName, setBuilderFileName] = useState(() => load("builderFileName", "my_topology"));
  const [settings, setSettings] = useState<SimSettings>(() =>
    load("settings", { config: DEFAULT_CONFIG, flows: [makeFlow(null)], events: [] }),
  );
  const [setupMode, setSetupMode] = useState<SetupMode>(() => load("setupMode", "interactive"));
  const [algorithms, setAlgorithms] = useState<Algorithm[]>(() => load("algorithms", [...ALGORITHMS]));
  const [script, setScript] = useState<string>(() => load("script", ""));
  const [exportResults, setExportResults] = useState(true);
  const [scriptResult, setScriptResult] = useState<ScriptRunResult | null>(null);
  const [runs, setRuns] = useState<RunResult[]>([]);
  const [run, setRun] = useState<RunState>({ status: "idle", startedAt: 0, label: "", error: "" });

  useEffect(() => {
    save("topology", topology);
  }, [topology]);
  useEffect(() => {
    save("topologyName", topologyName);
  }, [topologyName]);
  useEffect(() => {
    save("builder", builder);
  }, [builder]);
  useEffect(() => {
    save("builderFileName", builderFileName);
  }, [builderFileName]);
  useEffect(() => {
    save("settings", settings);
  }, [settings]);
  useEffect(() => {
    save("setupMode", setupMode);
  }, [setupMode]);
  useEffect(() => {
    save("algorithms", algorithms);
  }, [algorithms]);
  useEffect(() => {
    save("script", script);
  }, [script]);

  const applyTopology = useCallback((next: Topology, name: string) => {
    setTopology(next);
    setTopologyName(name);
    setSettings((current) => repairForTopology(next, current));
    setScriptResult(null);
  }, []);

  const applyScenario = useCallback((next: Topology, name: string, nextSettings: SimSettings) => {
    setTopology(next);
    setTopologyName(name);
    setSettings(nextSettings);
    setSetupMode("interactive");
    setScriptResult(null);
  }, []);

  const clearTopology = useCallback(() => {
    setTopology(null);
    setTopologyName("");
    setScriptResult(null);
  }, []);

  const parsedScript = useMemo(
    () => parseScript(script, topology, settings.config),
    [script, topology, settings.config],
  );

  const activeSettings = setupMode === "script" ? parsedScript.settings : settings;

  const problems = useMemo(() => {
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
  }, [setupMode, topology, settings, algorithms, script, parsedScript]);

  const startRun = useCallback(async () => {
    if (!topology || problems.length > 0) return;
    const scripted = setupMode === "script";
    setRun({
      status: "running",
      startedAt: Date.now(),
      label: scripted ? "Running script" : `Simulating ${algorithms.length} algorithm${algorithms.length === 1 ? "" : "s"}`,
      error: "",
    });
    if (scripted) setScriptResult(null);
    try {
      if (scripted) {
        const result = await api.runScript(topology, script, exportResults, parsedScript.settings);
        setScriptResult(result);
        if (result.runs.length > 0) setRuns(result.runs);
      } else {
        const ordered = ALGORITHMS.filter((name) => algorithms.includes(name));
        const result = await api.runSimulation(topology, topologyName, settings, ordered);
        setRuns(result.runs);
        setSection("results");
      }
      setRun((current) => ({ ...current, status: "idle" }));
    } catch (error) {
      setRun((current) => ({ ...current, status: "error", error: (error as Error).message }));
    }
  }, [topology, topologyName, problems, setupMode, algorithms, script, exportResults, parsedScript, settings]);

  const value: Workbench = {
    section,
    goTo: setSection,
    topology,
    topologyName,
    applyTopology,
    applyScenario,
    clearTopology,
    builder,
    setBuilder,
    builderFileName,
    setBuilderFileName,
    settings,
    setSettings,
    setupMode,
    setSetupMode,
    algorithms,
    setAlgorithms,
    script,
    setScript,
    parsedScript,
    scriptResult,
    exportResults,
    setExportResults,
    activeSettings,
    problems,
    runs,
    run,
    startRun,
  };

  return <WorkbenchContext.Provider value={value}>{children}</WorkbenchContext.Provider>;
}

export function useWorkbench(): Workbench {
  const value = useContext(WorkbenchContext);
  if (!value) throw new Error("useWorkbench must be used inside WorkbenchProvider");
  return value;
}
