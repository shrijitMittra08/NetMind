import type {
  AnomalyEvaluation,
  AnomalyReport,
  ChatTurn,
  GraphRagAnswer,
  GraphRagStatus,
  RunFile,
  RunResult,
  SavedScenario,
  ScriptRunResult,
  SimSettings,
  Topology,
} from "./types";
import { buildPayload } from "../domain/setup";

export class ApiError extends Error {
  readonly status: number;

  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

async function requestJson<T>(url: string, init: RequestInit = {}): Promise<T> {
  let response: Response;
  try {
    response = await fetch(url, {
      ...init,
      headers: {
        Accept: "application/json",
        ...(init.body ? { "Content-Type": "application/json" } : {}),
        ...init.headers,
      },
    });
  } catch {
    throw new ApiError("Cannot reach the NetMind service. Is the API running?", 0);
  }
  let payload: unknown;
  try {
    payload = await response.json();
  } catch {
    throw new ApiError("The NetMind service returned an unreadable response.", response.status);
  }
  if (response.ok) return payload as T;
  if (typeof payload === "object" && payload && "error" in payload) {
    throw new ApiError(String(payload.error), response.status);
  }
  throw new ApiError(`Request failed (${response.status}).`, response.status);
}

const post = <T>(url: string, body: unknown) =>
  requestJson<T>(url, { method: "POST", body: JSON.stringify(body) });

export const api = {
  health: () => requestJson<{ ok: boolean; data_directory: string }>("/api/health"),

  runSimulation: (topology: Topology, topologyName: string, settings: SimSettings, algorithms: string[]) =>
    post<{ runs: RunResult[] }>("/api/gui/run", { ...buildPayload(topology, topologyName, settings), algorithms }),

  runScript: (topology: Topology, script: string, exportResults: boolean, settings: SimSettings) =>
    post<ScriptRunResult>("/api/script/run", {
      topology,
      script,
      export_results: exportResults,
      settings: buildPayload(topology, "", settings),
    }),

  saveScenario: (topology: Topology, topologyName: string, settings: SimSettings, name: string, overwrite: boolean) =>
    post<SavedScenario>("/api/scenario/save", { ...buildPayload(topology, topologyName, settings), name, overwrite }),

  listScenarios: () => requestJson<{ scenarios: { name: string }[] }>("/api/scenarios"),

  loadScenario: (name: string) =>
    requestJson<{ scenario: Record<string, unknown>; topology: Topology; topology_name: string }>(
      `/api/scenarios/${encodeURIComponent(name)}`,
    ),

  resolveScenarioTopology: (scenario: Record<string, unknown>) =>
    post<{ topology: Topology; topology_name: string }>("/api/scenario/resolve", { scenario }),

  runFiles: (runId: string) => requestJson<{ files: RunFile[] }>(`/api/gui/runs/${encodeURIComponent(runId)}/files`),

  askGraphRag: (runIds: string[], question: string, history: ChatTurn[] = []) =>
    post<GraphRagAnswer>("/api/graphrag/query", { run_ids: runIds, question, history }),

  graphragStatus: () => requestJson<GraphRagStatus>("/api/graphrag/status"),

  anomaly: (runId: string) => post<AnomalyReport>("/api/ml/anomaly", { run_id: runId }),

  anomalyEvaluation: () => requestJson<AnomalyEvaluation>("/api/ml/anomaly/evaluation"),
};
