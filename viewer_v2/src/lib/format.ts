export const ALGORITHM_LABELS: Record<string, string> = {
  bfs: "BFS",
  dfs: "DFS",
  dijkstra: "Dijkstra",
  "bellman-ford": "Bellman-Ford",
};

export const algorithmLabel = (name: string) => ALGORITHM_LABELS[name] ?? name;

export function fmt(value: number | null | undefined, digits = 2): string {
  return value === null || value === undefined || !Number.isFinite(value) ? "—" : value.toFixed(digits);
}

export function fmtSigned(value: number | null | undefined, digits = 1, suffix = ""): string {
  if (value === null || value === undefined || !Number.isFinite(value)) return "—";
  return `${value > 0 ? "+" : ""}${value.toFixed(digits)}${suffix}`;
}

export const fmtTime = (seconds: number) => `${seconds.toFixed(3)} s`;
