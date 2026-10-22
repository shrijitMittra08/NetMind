import { useEffect, useRef, useState } from "react";
import { askGraphRAG, fetchRunFiles, requestJson, saveScenario } from "./api.js";
import "./Workbench.css";

const algorithms = ["bfs", "dfs", "dijkstra", "bellman-ford"];
const weights = ["hops", "latency"];

const eventTypes = [
  "LINK_FAILURE",
  "LINK_RESTORE",
  "ROUTER_FAILURE",
  "ROUTER_RESTORE",
  "CONGESTION",
  "TRAFFIC_BURST",
];

const examples = [
  "Which algorithm performed best?",
  "Why were packets dropped around 5 seconds?",
  "Which link was most congested?",
  "What path did PC1 to PC2 use?",
];

const algorithmNames = {
  bfs: "BFS",
  dfs: "DFS",
  dijkstra: "Dijkstra",
  "bellman-ford": "Bellman-Ford",
};
const algorithmLabel = (name) => algorithmNames[name] ?? name;

function makeFlow() {
  return {
    src: "",
    dst: "",
    rate_mbps: 5,
    packet_bytes: 1000,
    start_s: 0,
    stop_s: 10,
    pattern: "constant",
    on_duration_s: 1,
    off_duration_s: 1,
  };
}

function makeEvent() {
  return {
    type: "LINK_FAILURE",
    a: "",
    b: "",
    node: "",
    time_s: 2,
    bandwidth_multiplier: 0.5,
    extra_loss_rate: 0.01,
    duration_s: 1,
    src: "",
    dst: "",
    rate_mbps: 10,
    packet_bytes: 1000,
  };
}

function TextField({ label, value, onChange, type = "text", step }) {
  return (
    <label className="gui-field">
      <span>{label}</span>
      <input
        type={type}
        step={step}
        value={value}
        onChange={(event) => onChange(event.target.value)}
      />
    </label>
  );
}

function SelectField({ label, value, options, onChange, emptyLabel }) {
  return (
    <label className="gui-field">
      <span>{label}</span>
      <select value={value} onChange={(event) => onChange(event.target.value)}>
        {emptyLabel && <option value="">{emptyLabel}</option>}
        {options.map((option) => {
          const item =
            typeof option === "string"
              ? { value: option, label: option }
              : option;
          return (
            <option key={item.value} value={item.value}>
              {item.label}
            </option>
          );
        })}
      </select>
    </label>
  );
}

function RunConfiguration({
  topology,
  config,
  setConfig,
  flows,
  setFlows,
  events,
  setEvents,
  onRun,
  running,
  saveName,
  setSaveName,
  onSave,
  saveStatus,
}) {
  const nodes = topology?.nodes ?? [];
  const hosts = nodes.filter((node) => node.type === "HOST");
  const routers = nodes.filter((node) => node.type === "ROUTER");
  const linkOptions = (topology?.links ?? []).map((link) => {
    const a = nodes.find((node) => node.id === link.a)?.name ?? String(link.a);
    const b = nodes.find((node) => node.id === link.b)?.name ?? String(link.b);
    return { value: `${a}|${b}`, label: `${a} — ${b}` };
  });
  const nameOk = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(saveName.trim());
  const flowsOk =
    flows.length > 0 &&
    flows.every((flow) => flow.src && flow.dst && flow.src !== flow.dst);

  function updateFlow(index, key, value) {
    setFlows((current) =>
      current.map((flow, i) =>
        i === index ? { ...flow, [key]: value } : flow,
      ),
    );
  }

  function updateEvent(index, key, value) {
    setEvents((current) =>
      current.map((item, i) =>
        i === index ? { ...item, [key]: value } : item,
      ),
    );
  }

  return (
    <section className="gui-card">
      <div className="gui-section-title">
        <div>
          <h2>Simulation setup</h2>
        </div>
      </div>

      <div className="gui-grid">
        <TextField
          label="Simulation duration (seconds)"
          type="number"
          step="0.1"
          value={config.duration_s}
          onChange={(value) =>
            setConfig({ ...config, duration_s: Number(value) })
          }
        />
        <TextField
          label="Random seed"
          type="number"
          step="1"
          value={config.seed}
          onChange={(value) => setConfig({ ...config, seed: Number(value) })}
        />
        <SelectField
          label="Routing algorithm"
          value={config.algorithm}
          options={algorithms}
          onChange={(value) => setConfig({ ...config, algorithm: value })}
        />
        <SelectField
          label="Routing weight"
          value={config.weight}
          options={weights}
          onChange={(value) => setConfig({ ...config, weight: value })}
        />
      </div>

      <div className="gui-subsection">
        <div className="gui-subsection-heading">
          <div>
            <h3>Traffic flows</h3>
          </div>
          <button
            className="secondary-button"
            type="button"
            onClick={() => setFlows((current) => [...current, makeFlow()])}
          >
            Add flow
          </button>
        </div>

        {flows.map((flow, index) => (
          <article className="editor-card" key={index}>
            <div className="editor-heading">
              <strong>Flow {index + 1}</strong>
              {flows.length > 1 && (
                <button
                  className="text-button"
                  type="button"
                  onClick={() =>
                    setFlows((current) =>
                      current.filter((_, i) => i !== index),
                    )
                  }
                >
                  Remove
                </button>
              )}
            </div>
            <div className="gui-grid">
              <SelectField
                label="Source host"
                value={flow.src}
                options={hosts.map((node) => node.name)}
                emptyLabel="Choose host"
                onChange={(value) => updateFlow(index, "src", value)}
              />
              <SelectField
                label="Destination host"
                value={flow.dst}
                options={hosts.map((node) => node.name)}
                emptyLabel="Choose host"
                onChange={(value) => updateFlow(index, "dst", value)}
              />
              <TextField
                label="Rate (Mbps)"
                type="number"
                step="0.1"
                value={flow.rate_mbps}
                onChange={(value) =>
                  updateFlow(index, "rate_mbps", Number(value))
                }
              />
              <TextField
                label="Packet size (bytes)"
                type="number"
                step="1"
                value={flow.packet_bytes}
                onChange={(value) =>
                  updateFlow(index, "packet_bytes", Number(value))
                }
              />
              <TextField
                label="Start time (seconds)"
                type="number"
                step="0.1"
                value={flow.start_s}
                onChange={(value) =>
                  updateFlow(index, "start_s", Number(value))
                }
              />
              <TextField
                label="Stop time (seconds)"
                type="number"
                step="0.1"
                value={flow.stop_s}
                onChange={(value) =>
                  updateFlow(index, "stop_s", Number(value))
                }
              />
              <SelectField
                label="Traffic pattern"
                value={flow.pattern}
                options={["constant", "bursty", "poisson"]}
                onChange={(value) => updateFlow(index, "pattern", value)}
              />
              {flow.pattern === "bursty" && (
                <>
                  <TextField
                    label="On duration (seconds)"
                    type="number"
                    step="0.1"
                    value={flow.on_duration_s}
                    onChange={(value) =>
                      updateFlow(index, "on_duration_s", Number(value))
                    }
                  />
                  <TextField
                    label="Off duration (seconds)"
                    type="number"
                    step="0.1"
                    value={flow.off_duration_s}
                    onChange={(value) =>
                      updateFlow(index, "off_duration_s", Number(value))
                    }
                  />
                </>
              )}
            </div>
          </article>
        ))}
      </div>

      <div className="gui-subsection">
        <div className="gui-subsection-heading">
          <div>
            <h3>Scheduled events</h3>
          </div>
          <button
            className="secondary-button"
            type="button"
            onClick={() => setEvents((current) => [...current, makeEvent()])}
          >
            Add event
          </button>
        </div>

        {events.map((item, index) => {
          const isLinkEvent = [
            "LINK_FAILURE",
            "LINK_RESTORE",
            "CONGESTION",
          ].includes(item.type);
          const isRouterEvent = [
            "ROUTER_FAILURE",
            "ROUTER_RESTORE",
          ].includes(item.type);

          return (
            <article className="editor-card" key={index}>
              <div className="editor-heading">
                <strong>Event {index + 1}</strong>
                <button
                  className="text-button"
                  type="button"
                  onClick={() =>
                    setEvents((current) =>
                      current.filter((_, i) => i !== index),
                    )
                  }
                >
                  Remove
                </button>
              </div>
              <div className="gui-grid">
                <SelectField
                  label="Event type"
                  value={item.type}
                  options={eventTypes}
                  onChange={(value) => updateEvent(index, "type", value)}
                />
                <TextField
                  label="Event time (seconds)"
                  type="number"
                  step="0.1"
                  value={item.time_s}
                  onChange={(value) =>
                    updateEvent(index, "time_s", Number(value))
                  }
                />

                {isLinkEvent && (
                  <SelectField
                    label="Link"
                    value={
                      item.a && item.b ? `${item.a}|${item.b}` : ""
                    }
                    options={linkOptions}
                    emptyLabel="Choose link"
                    onChange={(value) => {
                      const [a, b] = value.split("|");
                      updateEvent(index, "a", a);
                      updateEvent(index, "b", b);
                    }}
                  />
                )}

                {isRouterEvent && (
                  <SelectField
                    label="Router"
                    value={item.node}
                    options={routers.map((node) => node.name)}
                    emptyLabel="Choose router"
                    onChange={(value) =>
                      updateEvent(index, "node", value)
                    }
                  />
                )}

                {item.type === "CONGESTION" && (
                  <>
                    <TextField
                      label="Bandwidth multiplier (0–1)"
                      type="number"
                      step="0.05"
                      value={item.bandwidth_multiplier}
                      onChange={(value) =>
                        updateEvent(
                          index,
                          "bandwidth_multiplier",
                          Number(value),
                        )
                      }
                    />
                    <TextField
                      label="Extra loss rate (0–1)"
                      type="number"
                      step="0.01"
                      value={item.extra_loss_rate}
                      onChange={(value) =>
                        updateEvent(index, "extra_loss_rate", Number(value))
                      }
                    />
                    <TextField
                      label="Duration (seconds)"
                      type="number"
                      step="0.1"
                      value={item.duration_s}
                      onChange={(value) =>
                        updateEvent(index, "duration_s", Number(value))
                      }
                    />
                  </>
                )}

                {item.type === "TRAFFIC_BURST" && (
                  <>
                    <SelectField
                      label="Source host"
                      value={item.src}
                      options={hosts.map((node) => node.name)}
                      emptyLabel="Choose host"
                      onChange={(value) =>
                        updateEvent(index, "src", value)
                      }
                    />
                    <SelectField
                      label="Destination host"
                      value={item.dst}
                      options={hosts.map((node) => node.name)}
                      emptyLabel="Choose host"
                      onChange={(value) =>
                        updateEvent(index, "dst", value)
                      }
                    />
                    <TextField
                      label="Rate (Mbps)"
                      type="number"
                      step="0.1"
                      value={item.rate_mbps}
                      onChange={(value) =>
                        updateEvent(index, "rate_mbps", Number(value))
                      }
                    />
                    <TextField
                      label="Packet size (bytes)"
                      type="number"
                      step="1"
                      value={item.packet_bytes}
                      onChange={(value) =>
                        updateEvent(index, "packet_bytes", Number(value))
                      }
                    />
                    <TextField
                      label="Duration (seconds)"
                      type="number"
                      step="0.1"
                      value={item.duration_s}
                      onChange={(value) =>
                        updateEvent(index, "duration_s", Number(value))
                      }
                    />
                  </>
                )}
              </div>
            </article>
          );
        })}
      </div>

      <button
        className="primary-button"
        type="button"
        disabled={running || !topology}
        onClick={onRun}
      >
        {running ? "Running simulation…" : "Run simulation"}
      </button>
            <div className="gui-subsection">
        <div className="gui-subsection-heading">
          <div>
            <h3>Save as scenario</h3>
            
          </div>
        </div>

        <div className="save-row">
          <TextField
            label="Scenario name"
            value={saveName}
            onChange={setSaveName}
          />
          <button
            className="secondary-button"
            type="button"
            disabled={
              !topology || !nameOk || !flowsOk || saveStatus.kind === "busy"
            }
            onClick={() => onSave(false)}
          >
            {saveStatus.kind === "busy" ? "Saving…" : "Save scenario"}
          </button>
        </div>

        {saveName.trim() && !nameOk && (
          <p className="gui-muted">
            Use letters, numbers, “_” or “-”, starting with a letter or number
            (up to 64 characters).
          </p>
        )}
        {!flowsOk && (
          <p className="gui-muted">
            Choose a different source and destination host for every flow to
            enable saving.
          </p>
        )}

        {saveStatus.kind === "ok" && (
          <p className="gui-success">{saveStatus.message}</p>
        )}
        {saveStatus.kind === "error" && (
          <div className="gui-error">{saveStatus.message}</div>
        )}
        {saveStatus.kind === "exists" && (
          <div className="gui-warning">
            A scenario named “{saveName.trim()}” already exists.{" "}
            <button
              className="text-button"
              type="button"
              onClick={() => onSave(true)}
            >
              Overwrite it
            </button>
          </div>
        )}
      </div>
    </section>
  );
}

const baselineLabels = {
  avg_latency_ms: "avg latency (ms)",
  throughput_mbps: "throughput (Mbps)",
  loss_pct: "packet loss (%)",
};

function fmt(value, digits = 3) {
  return value === null || value === undefined
    ? "—"
    : Number(value).toFixed(digits);
}

function deviationText(metric) {
  const sign = (v) => (v > 0 ? "+" : "");
  if (metric.name === "loss_pct") {
                                                                          
    return metric.delta === null || metric.delta === undefined
      ? "—"
      : `${sign(metric.delta)}${fmt(metric.delta, 2)} pp`;
  }
  if (metric.deviation_pct !== null && metric.deviation_pct !== undefined) {
    return `${sign(metric.deviation_pct)}${fmt(metric.deviation_pct, 1)}%`;
  }
  return metric.delta === null || metric.delta === undefined
    ? "—"
    : `${sign(metric.delta)}${fmt(metric.delta, 3)}`;
}

function Baseline({ runs }) {
  return (
    <section className="gui-card" id="baseline">
      <h2>Expected baseline</h2>
      

      {runs.map((run) => (
        <div className="baseline-run" key={run.run_id}>
          <h3 className="baseline-run-title">
            {algorithmLabel(run.algorithm)} <span>{run.run_id}</span>
          </h3>
          <p className="gui-muted">
            Reliability {fmt(run.summary?.reliability_pct, 2)}% · throughput{" "}
            {fmt(run.summary?.throughput_mbps, 2)} Mbps · average latency{" "}
            {fmt(run.summary?.latency_ms, 2)} ms · {run.summary?.drops ?? "—"} dropped
            packets
          </p>

          {run.baseline?.error ? (
            <div className="gui-error">{run.baseline.error}</div>
          ) : (
            <div className="baseline-grid">
              {(run.baseline?.metrics ?? []).map((metric) => (
                <article className="baseline-card" key={metric.name}>
                  <h3>{baselineLabels[metric.name] ?? metric.name}</h3>
                  <dl>
                    <div>
                      <dt>Actual</dt>
                      <dd>{fmt(metric.actual)}</dd>
                    </div>
                    <div>
                      <dt>Expected</dt>
                      <dd>{fmt(metric.expected)}</dd>
                    </div>
                    <div>
                      <dt>Deviation</dt>
                      <dd>{deviationText(metric)}</dd>
                    </div>
                  </dl>
                  <span
                    className={
                      metric.flagged ? "baseline-flag flagged" : "baseline-flag"
                    }
                  >
                    {metric.flagged ? "Large deviation" : "Within baseline range"}
                  </span>
                </article>
              ))}
            </div>
          )}
        </div>
      ))}
    </section>
  );
}

function GraphRAG({ runs }) {
  const [question, setQuestion] = useState("");
  const [result, setResult] = useState(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const runLabel = (runId) => {
    const run = runs.find((item) => item.run_id === runId);
    return run ? algorithmLabel(run.algorithm) : runId;
  };

  async function ask(event) {
    event.preventDefault();
    setBusy(true);
    setError("");
    setResult(null);
    try {
      setResult(
        await askGraphRAG(
          runs.map((run) => run.run_id),
          question,
        ),
      );
    } catch (requestError) {
      setError(requestError.message);
    } finally {
      setBusy(false);
    }
  }

  return (
    <section className="gui-card" id="graphrag">
      <form className="query-form" onSubmit={ask}>
        <label className="gui-field">
          <span>Question about the four algorithm runs</span>
          <textarea
            rows={3}
            value={question}
            onChange={(event) => setQuestion(event.target.value)}
            placeholder="Which algorithm performed best?"
          />
        </label>
        <div className="example-questions">
          {examples.map((text) => (
            <button
              type="button"
              className="text-button"
              key={text}
              onClick={() => setQuestion(text)}
            >
              {text}
            </button>
          ))}
        </div>
        <button className="primary-button" disabled={busy || !question.trim()}>
          {busy ? "Searching run facts…" : "Ask GraphRAG"}
        </button>
      </form>

      {error && <div className="gui-error">{error}</div>}
      {result && (
        <div className="answer-box">
          <p className="gui-eyebrow">Answer · {result.intent}</p>
          <p style={{ whiteSpace: "pre-line" }}>
            {result.answer.replace(/run_[A-Za-z0-9_-]+/g, runLabel)}
          </p>
          {result.facts?.length > 0 && (
            <>
              <h3>Supporting facts</h3>
              {result.facts.map((fact, index) => (
                <article className="fact-card" key={fact.id ?? index}>
                  <strong>{fact.id}</strong>
                  <p>{fact.text}</p>
                </article>
              ))}
            </>
          )}
        </div>
      )}
    </section>
  );
}


const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

export default function Workbench() {
  const [topologyFile, setTopologyFile] = useState(null);
  const [topology, setTopology] = useState(null);
  const [topologyError, setTopologyError] = useState("");
  const [config, setConfig] = useState({
    duration_s: 10,
    seed: 42,
    algorithm: "dijkstra",
    weight: "latency",
  });
  const [flows, setFlows] = useState([makeFlow()]);
  const [events, setEvents] = useState([]);
  const [running, setRunning] = useState(false);
  const [runResult, setRunResult] = useState(null);
  const [runError, setRunError] = useState("");
  const [replayAlgorithm, setReplayAlgorithm] = useState("dijkstra");
  const [replayStatus, setReplayStatus] = useState("");
  const [viewerReady, setViewerReady] = useState(false);
  const viewerRef = useRef(null);
  const [saveName, setSaveName] = useState("");
  const [saveStatus, setSaveStatus] = useState({ kind: "idle" });

  const runs = runResult?.runs ?? [];
  const replayRun =
    runs.find((run) => run.algorithm === replayAlgorithm) ?? runs[0] ?? null;

  async function loadTopology(file) {
    setTopologyError("");
    setRunResult(null);
    setTopologyFile(file);
    try {
      const parsed = JSON.parse(await file.text());
      if (!Array.isArray(parsed.nodes) || !Array.isArray(parsed.links)) {
        throw new Error("This JSON does not contain topology nodes and links.");
      }
      setTopology(parsed);
      setSaveStatus({ kind: "idle" });
      setSaveName(
        (current) =>
          current || file.name.replace(/\.json$/i, "").replace(/[^A-Za-z0-9_-]+/g, "_"),
      );
      const hosts = parsed.nodes.filter((node) => node.type === "HOST");
      if (hosts.length >= 2) {
        setFlows((current) =>
          current.map((flow) => ({
            ...flow,
            src: flow.src || hosts[0].name,
            dst: flow.dst || hosts[1].name,
          })),
        );
      }
    } catch (error) {
      setTopology(null);
      setTopologyError(error.message);
    }
  }

                                                                                 
  function buildPayload() {
    const normalizedFlows = flows.map((flow, index) => ({
      id: index + 1,
      src: flow.src,
      dst: flow.dst,
      pattern: flow.pattern,
      rate_mbps: Number(flow.rate_mbps),
      packet_bytes: Number(flow.packet_bytes),
      start_s: Number(flow.start_s),
      stop_s: Number(flow.stop_s),
      ...(flow.pattern === "bursty"
        ? {
            on_duration_s: Number(flow.on_duration_s),
            off_duration_s: Number(flow.off_duration_s),
          }
        : {}),
    }));

    let nextBurstFlowId = normalizedFlows.length + 1;
    const normalizedEvents = events.map((item) => {
      const time_s = Number(item.time_s);
      if (item.type === "LINK_FAILURE" || item.type === "LINK_RESTORE") {
        return { type: item.type, a: item.a, b: item.b, time_s };
      }
      if (item.type === "ROUTER_FAILURE" || item.type === "ROUTER_RESTORE") {
        return { type: item.type, node: item.node, time_s };
      }
      if (item.type === "CONGESTION") {
        return {
          type: item.type,
          a: item.a,
          b: item.b,
          time_s,
          bandwidth_multiplier: Number(item.bandwidth_multiplier),
          extra_loss_rate: Number(item.extra_loss_rate),
          duration_s: Number(item.duration_s),
        };
      }
      return {
        type: "TRAFFIC_BURST",
        flow_id: nextBurstFlowId++,
        src: item.src,
        dst: item.dst,
        time_s,
        duration_s: Number(item.duration_s),
        rate_mbps: Number(item.rate_mbps),
        packet_bytes: Number(item.packet_bytes),
      };
    });

    return {
      topology: topology,
      topology_name: topologyFile?.name ?? "uploaded-topology.json",
      duration_s: Number(config.duration_s),
      seed: Number(config.seed),
      algorithm: config.algorithm,
      weight: config.weight,
      flows: normalizedFlows,
      events: normalizedEvents,
    };
  }

  async function startRun() {
    setRunning(true);
    setRunError("");
    setRunResult(null);
    setViewerReady(false);
    setReplayStatus("");

    try {
                                                                                
      const result = await requestJson("/api/gui/run", {
        method: "POST",
        body: JSON.stringify({ ...buildPayload(), algorithms }),
      });

      setRunResult(result);
      setReplayAlgorithm(
        result.runs.some((run) => run.algorithm === config.algorithm)
          ? config.algorithm
          : result.runs[0]?.algorithm ?? "dijkstra",
      );
      setTimeout(
        () => document.getElementById("baseline")?.scrollIntoView({ behavior: "smooth" }),
        80,
      );
    } catch (error) {
      setRunError(error.message);
    } finally {
      setRunning(false);
    }
  }

  async function saveAsScenario(overwrite = false) {
    setSaveStatus({ kind: "busy" });
    try {
      const saved = await saveScenario({
        ...buildPayload(),
        name: saveName.trim(),
        overwrite,
      });
      setSaveStatus({
        kind: "ok",
        message: `Saved ${saved.scenario_path} (topology: ${saved.topology_path}).`,
      });
    } catch (error) {
      setSaveStatus(
        error.status === 409
          ? { kind: "exists" }
          : { kind: "error", message: error.message },
      );
    }
  }

  function changeReplayAlgorithm(value) {
    setViewerReady(false);                                               
    setReplayAlgorithm(value);
  }

                                                                               
  useEffect(() => {
    if (!viewerReady || !replayRun || !viewerRef.current) return;

    let cancelled = false;

    async function loadReplay() {
      try {
        setReplayStatus("Loading run files…");

                                                                               
                                                                                
        const others = runs.filter((run) => run.run_id !== replayRun.run_id);
        const [full, ...light] = await Promise.all([
          fetchRunFiles(replayRun.run_id),
          ...others.map((run) => fetchRunFiles(run.run_id, { light: true })),
        ]);
        if (cancelled) return;

        const frame = viewerRef.current;
        const win = frame?.contentWindow;
        const doc = frame?.contentDocument;
        const input = doc?.getElementById("fileInput");
        const play = doc?.getElementById("playButton");
        if (!win || !input || !play) {
          throw new Error("Replay viewer did not finish loading.");
        }

                                                                   
        

        const transfer = new win.DataTransfer();
        const payloads = [full, ...light];
        payloads.forEach((payload, index) => {
          for (const item of payload.files) {
                                                                        
            if (index > 0 && item.name.startsWith("topology_")) continue;
            transfer.items.add(
              new win.File([item.content], item.name, { type: item.type }),
            );
          }
        });

        input.files = transfer.files;
        input.dispatchEvent(new win.Event("change", { bubbles: true }));

                                                                               
        const started = Date.now();
        while (!cancelled && play.disabled && Date.now() - started < 30000) {
          await wait(100);
        }
        if (cancelled) return;
        if (play.disabled) {
          throw new Error("The replay viewer could not load this run.");
        }
        if (play.textContent.trim() === "Play") play.click();

        setReplayStatus(
          `Replaying ${algorithmLabel(replayRun.algorithm)} · the comparison below ` +
            `covers ${runs.length} algorithms.`,
        );
      } catch (error) {
        if (!cancelled) {
          setReplayStatus("");
          setRunError(error.message);
        }
      }
    }

    loadReplay();
    return () => {
      cancelled = true;
    };
                                                           
  }, [viewerReady, runResult, replayAlgorithm]);

  return (
    <main className="workbench">
      <header className="workbench-header">
        <div>
          <p className="gui-eyebrow">NetMind · Simulation workbench</p>
        </div>
      </header>

      <section className="gui-card">
        <h2>Load topology</h2>
        <label className="file-picker">
          <span>{topologyFile?.name ?? "Choose topology JSON"}</span>
          <input
            type="file"
            accept=".json,application/json"
            onChange={(event) => {
              const file = event.target.files?.[0];
              if (file) loadTopology(file);
            }}
          />
        </label>

        {topologyError && <div className="gui-error">{topologyError}</div>}

        {topology && (
          <div className="topology-summary">
            <strong>{topologyFile?.name}</strong>
            <span>{topology.nodes.length} nodes</span>
            <span>{topology.links.length} links</span>
            <div className="topology-name-list">
              {topology.nodes.map((node) => (
                <span key={node.id}>
                  {node.name} · {node.type}
                </span>
              ))}
            </div>
          </div>
        )}
      </section>

      {topology && (
        <RunConfiguration
          topology={topology}
          config={config}
          setConfig={setConfig}
          flows={flows}
          setFlows={setFlows}
          events={events}
          setEvents={setEvents}
          onRun={startRun}
          running={running}
          saveName={saveName}
          setSaveName={setSaveName}
          onSave={saveAsScenario}
          saveStatus={saveStatus}
        />
      )}

      {runError && <div className="gui-error">{runError}</div>}

      {runs.length > 0 && (
        <>
          <Baseline runs={runs} />

          <section className="gui-card" id="replay">
            <h2>Replay viewer</h2>
            

            <div className="gui-grid replay-picker">
              <SelectField
                label="Algorithm shown in the replay"
                value={replayRun?.algorithm ?? ""}
                options={runs.map((run) => ({
                  value: run.algorithm,
                  label: algorithmLabel(run.algorithm),
                }))}
                onChange={changeReplayAlgorithm}
              />
            </div>

            <iframe
              key={`${runs.map((run) => run.run_id).join("-")}:${replayRun?.algorithm}`}
              ref={viewerRef}
              title="NetMind replay viewer"
              src="/legacy/index.html"
              onLoad={() => setViewerReady(true)}
              className="replay-frame"
            />
          </section>

          <GraphRAG runs={runs} />
        </>
      )}
    </main>
  );
}