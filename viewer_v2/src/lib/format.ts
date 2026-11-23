export const ALGORITHM_LABELS: Record<string, string> = {
  bfs: "BFS",
  dfs: "DFS",
  dijkstra: "Dijkstra",
  "bellman-ford": "Bellman-Ford",
};

export const algorithmLabel = (name: string) => ALGORITHM_LABELS[name] ?? name;

function isMissing(value: number | null | undefined): value is null | undefined {
  return value === null || value === undefined || !Number.isFinite(value);
}

export function fmt(value: number | null | undefined, digits = 2): string {
  return isMissing(value) ? "—" : value.toFixed(digits);
}

export function fmtSigned(value: number | null | undefined, digits = 1, suffix = ""): string {
  if (isMissing(value)) return "—";
  const sign = value > 0 ? "+" : "";
  return `${sign}${value.toFixed(digits)}${suffix}`;
}

export const fmtTime = (seconds: number) => `${seconds.toFixed(3)} s`;
