import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import {
  Activity,
  ChartColumnBig,
  CircleCheck,
  Moon,
  Network,
  PlayCircle,
  SlidersHorizontal,
  Sparkles,
  Sun,
} from "lucide-react";
import { api } from "../lib/api";
import { load, save } from "../state/storage";
import { useWorkbench } from "../state/workbench";
import type { Section } from "../state/workbench";

type Theme = "light" | "dark";

function useTheme(): [Theme, (theme: Theme) => void] {
  const [theme, setTheme] = useState<Theme>(() => {
    const saved = load<string>("theme", "");
    if (saved === "light" || saved === "dark") return saved;
    return window.matchMedia?.("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  });
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    save("theme", theme);
  }, [theme]);
  return [theme, setTheme];
}

function ThemeSwitch({ theme, onChange }: { theme: Theme; onChange: (theme: Theme) => void }) {
  const dark = theme === "dark";
  return (
    <button
      type="button"
      role="switch"
      aria-checked={dark}
      aria-label="Dark mode"
      className="theme-switch"
      onClick={() => onChange(dark ? "light" : "dark")}
    >
      <Sun className="theme-icon sun" />
      <span className="theme-track">
        <span className="theme-thumb" />
      </span>
      <Moon className="theme-icon moon" />
      <span className="theme-label">{dark ? "Dark" : "Light"}</span>
    </button>
  );
}

function useApiStatus(): "checking" | "online" | "offline" {
  const [status, setStatus] = useState<"checking" | "online" | "offline">("checking");
  useEffect(() => {
    let alive = true;
    const check = () =>
      api
        .health()
        .then(() => alive && setStatus("online"))
        .catch(() => alive && setStatus("offline"));
    check();
    const timer = window.setInterval(check, 15000);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, []);
  return status;
}

const NAV: { id: Section; label: string; icon: ReactNode; needsRuns?: boolean }[] = [
  { id: "topology", label: "Topology", icon: <Network /> },
  { id: "setup", label: "Simulation", icon: <SlidersHorizontal /> },
  { id: "results", label: "Results", icon: <ChartColumnBig />, needsRuns: true },
  { id: "anomalies", label: "Anomalies", icon: <Activity />, needsRuns: true },
  { id: "replay", label: "Replay", icon: <PlayCircle />, needsRuns: true },
  { id: "insights", label: "Insights", icon: <Sparkles />, needsRuns: true },
];

const SECTION_TITLES: Record<Section, { title: string; subtitle: string }> = {
  topology: { title: "Topology", subtitle: "Build a network on the canvas or import a topology file." },
  setup: { title: "Simulation", subtitle: "Configure traffic and events, or script the run like the CLI." },
  results: { title: "Results", subtitle: "Compare routing algorithms against each other and the ML baseline." },
  anomalies: { title: "Anomalies", subtitle: "See which seconds of the current run looked abnormal, and why." },
  replay: { title: "Replay", subtitle: "Watch packets move through the network, step by step." },
  insights: { title: "Insights", subtitle: "Ask about the runs in plain language; answers are built from the run data." },
};

export function Shell({ children }: { children: ReactNode }) {
  const { section, goTo, topology, runs, run } = useWorkbench();
  const [theme, setTheme] = useTheme();
  const apiStatus = useApiStatus();
  const done: Partial<Record<Section, boolean>> = { topology: Boolean(topology), setup: runs.length > 0 };
  const heading = SECTION_TITLES[section];

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
            const disabled = item.needsRuns && runs.length === 0;
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
                {item.needsRuns && runs.length > 0 && item.id === "results" && (
                  <span className="nav-count">{runs.length}</span>
                )}
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
            {apiStatus === "online" ? "Service online" : apiStatus === "offline" ? "Service offline" : "Connecting…"}
          </div>
          <ThemeSwitch theme={theme} onChange={setTheme} />
        </div>
      </aside>

      <main className="main">
        <header className="page-head">
          <div>
            <h1>{heading.title}</h1>
            <p>{heading.subtitle}</p>
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
