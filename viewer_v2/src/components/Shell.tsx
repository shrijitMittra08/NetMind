import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import { Activity, ChartColumnBig, CircleCheck, Moon, Network } from "lucide-react";
import { PlayCircle, SlidersHorizontal, Sparkles, Sun } from "lucide-react";
import { api } from "../lib/api";
import { load, save } from "../state/storage";
import { useWorkbench } from "../state/workbench";
import type { Section } from "../state/workbench";

type Theme = "light" | "dark";
type ApiStatus = "checking" | "online" | "offline";

const HEALTH_CHECK_INTERVAL_MS = 15000;

const API_STATUS_TEXT: Record<ApiStatus, string> = {
  checking: "Connecting…",
  online: "Service online",
  offline: "Service offline",
};

interface NavEntry {
  id: Section;
  label: string;
  icon: ReactNode;
  needsRuns?: boolean;
}

const NAV: NavEntry[] = [
  { id: "topology", label: "Topology", icon: <Network /> },
  { id: "setup", label: "Simulation", icon: <SlidersHorizontal /> },
  { id: "results", label: "Results", icon: <ChartColumnBig />, needsRuns: true },
  { id: "anomalies", label: "Anomalies", icon: <Activity />, needsRuns: true },
  { id: "replay", label: "Replay", icon: <PlayCircle />, needsRuns: true },
  { id: "insights", label: "Insights", icon: <Sparkles />, needsRuns: true },
];

const SUBTITLES: Record<Section, string> = {
  topology: "Build a network on the canvas or import a topology file.",
  setup: "Configure traffic and events, or script the run like the CLI.",
  results: "Compare routing algorithms against each other and the ML baseline.",
  anomalies: "See which seconds of the current run looked abnormal, and why.",
  replay: "Watch packets move through the network, step by step.",
  insights: "Ask about the runs in plain language; answers are built from the run data.",
};

function initialTheme(): Theme {
  const saved = load<string>("theme", "");
  if (saved === "light" || saved === "dark") return saved;
  return window.matchMedia?.("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

function useApiStatus(): ApiStatus {
  const [status, setStatus] = useState<ApiStatus>("checking");
  useEffect(() => {
    let alive = true;
    const update = (next: ApiStatus) => {
      if (alive) setStatus(next);
    };
    const check = () => api.health().then(() => update("online")).catch(() => update("offline"));
    check();
    const timer = window.setInterval(check, HEALTH_CHECK_INTERVAL_MS);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, []);
  return status;
}

export function Shell({ children }: { children: ReactNode }) {
  const { section, goTo, topology, runs, run } = useWorkbench();
  const [theme, setTheme] = useState<Theme>(initialTheme);
  const done: Partial<Record<Section, boolean>> = { topology: Boolean(topology), setup: runs.length > 0 };
  const dark = theme === "dark";

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    save("theme", theme);
  }, [theme]);
  const apiStatus = useApiStatus();

  return (
    <div className="shell">
      <aside className="sidebar">
        <div className="brand">
          <img src="/favicon.svg" alt="" width={30} height={30} />
          <div>
            <strong>NetMind</strong>
            <span>Simulation workbench</span>
          </div>
        </div>

        <nav aria-label="Workbench sections">
          {NAV.map((item, index) => {
            const disabled = Boolean(item.needsRuns) && runs.length === 0;
            return (
              <button
                key={item.id}
                type="button"
                className="nav-item"
                aria-label={item.label}
                aria-current={section === item.id ? "page" : undefined}
                disabled={disabled}
                title={disabled ? "Run a simulation first" : undefined}
                onClick={() => goTo(item.id)}
              >
                <span className="nav-step">{index + 1}</span>
                {item.icon}
                <span className="nav-label">{item.label}</span>
                {done[item.id] && <CircleCheck className="nav-done" aria-label="done" />}
                {item.id === "results" && runs.length > 0 && <span className="nav-count">{runs.length}</span>}
              </button>
            );
          })}
        </nav>

        <div className="sidebar-foot">
          {run.status === "running" && (
            <div className="sidebar-run">
              <span className="pulse" />
              {run.label}…
            </div>
          )}
          <div className={`api-status ${apiStatus}`}>
            <span className="dot" />
            {API_STATUS_TEXT[apiStatus]}
          </div>
          <button
            type="button"
            role="switch"
            aria-checked={dark}
            aria-label="Dark mode"
            className="theme-switch"
            onClick={() => setTheme(dark ? "light" : "dark")}
          >
            <Sun className="theme-icon sun" />
            <span className="theme-track">
              <span className="theme-thumb" />
            </span>
            <Moon className="theme-icon moon" />
            <span className="theme-label">{dark ? "Dark" : "Light"}</span>
          </button>
        </div>
      </aside>

      <main className="main">
        <header className="page-head">
          <div>
            <h1>{NAV.find((item) => item.id === section)?.label}</h1>
            <p>{SUBTITLES[section]}</p>
          </div>
        </header>
        {apiStatus === "offline" && (
          <div className="notice danger offline-banner">
            The NetMind service is not reachable. Start the API (python netmind_api.py) and this page will reconnect.
          </div>
        )}
        <div className="page">{children}</div>
      </main>
    </div>
  );
}
