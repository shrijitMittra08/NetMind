import { Plus, Trash2 } from "lucide-react";
import { EVENT_LABELS, LINK_EVENTS, ROUTER_EVENTS, makeEvent, makeFlow } from "../../domain/setup";
import { linkNamePairs } from "../../domain/topology";
import { algorithmLabel } from "../../lib/format";
import { ALGORITHMS, EVENT_TYPES, WEIGHTS } from "../../lib/types";
import type { EventDraft, FlowDraft, FlowPattern } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import { NumberField, SelectField } from "../ui";

export function InteractiveSetup() {
  const { topology, settings, setSettings } = useWorkbench();
  const nodes = topology?.nodes ?? [];
  const hosts = nodes.filter((n) => n.type === "HOST").map((n) => n.name);
  const routers = nodes.filter((n) => n.type === "ROUTER").map((n) => n.name);
  const links = topology ? linkNamePairs(topology).map(([a, b]) => ({ value: `${a}|${b}`, label: `${a} — ${b}` })) : [];
  const { config, flows, events } = settings;

  const setConfig = (patch: Partial<typeof config>) => setSettings((s) => ({ ...s, config: { ...s.config, ...patch } }));
  const setFlow = (index: number, patch: Partial<FlowDraft>) =>
    setSettings((s) => ({ ...s, flows: s.flows.map((f, i) => (i === index ? { ...f, ...patch } : f)) }));
  const setEvent = (index: number, patch: Partial<EventDraft>) =>
    setSettings((s) => ({ ...s, events: s.events.map((e, i) => (i === index ? { ...e, ...patch } : e)) }));

  return (
    <div className="stack">
      <section className="card">
        <div className="card-head">
          <div>
            <h3>General</h3>
            <p>Duration and randomness shared by every algorithm run</p>
          </div>
        </div>
        <div className="card-body grid-4">
          <NumberField label="Duration (s)" value={config.duration_s} min={0.1} step={0.5} onChange={(v) => setConfig({ duration_s: v })} />
          <NumberField label="Random seed" value={config.seed} step={1} onChange={(v) => setConfig({ seed: Math.round(v) })} />
          <SelectField
            label="Routing weight"
            value={config.weight}
            options={WEIGHTS}
            onChange={(weight) => setConfig({ weight })}
          />
          <SelectField
            label="Scenario algorithm"
            value={config.algorithm}
            options={ALGORITHMS.map((value) => ({ value, label: algorithmLabel(value) }))}
            onChange={(algorithm) => setConfig({ algorithm })}
          />
          <NumberField
            label="Reconvergence delay (ms)"
            value={config.reconvergence_delay_ms ?? 0}
            min={0}
            step={50}
            hint="Routers keep stale routes this long after a change"
            onChange={(v) => setConfig({ reconvergence_delay_ms: v })}
          />
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <div>
            <h3>Traffic flows</h3>
            <p>Packets sent between hosts</p>
          </div>
          <div className="spacer" />
          <button
            type="button"
            className="btn sm"
            onClick={() => setSettings((s) => ({ ...s, flows: [...s.flows, makeFlow(topology)] }))}
          >
            <Plus /> Add flow
          </button>
        </div>
        <div className="card-body stack">
          {flows.length === 0 && <p className="muted">No flows yet. A run needs at least one.</p>}
          {flows.map((flow, index) => (
            <article className="row-editor" key={index}>
              <div className="row-editor-head">
                <span className="row-index">Flow {index + 1}</span>
                <span className="muted small">
                  {flow.src || "?"} → {flow.dst || "?"}
                </span>
                <div className="spacer" />
                <button
                  type="button"
                  className="btn ghost sm icon"
                  aria-label={`Remove flow ${index + 1}`}
                  onClick={() => setSettings((s) => ({ ...s, flows: s.flows.filter((_, i) => i !== index) }))}
                >
                  <Trash2 />
                </button>
              </div>
              <div className="grid-4">
                <SelectField label="Source" value={flow.src} options={hosts} placeholder="Choose host" onChange={(src) => setFlow(index, { src })} />
                <SelectField label="Destination" value={flow.dst} options={hosts} placeholder="Choose host" onChange={(dst) => setFlow(index, { dst })} />
                <NumberField label="Rate (Mbps)" value={flow.rate_mbps} min={0} onChange={(v) => setFlow(index, { rate_mbps: v })} />
                <NumberField label="Packet size (bytes)" value={flow.packet_bytes} min={1} step={1} onChange={(v) => setFlow(index, { packet_bytes: v })} />
                <NumberField label="Start (s)" value={flow.start_s} min={0} onChange={(v) => setFlow(index, { start_s: v })} />
                <NumberField label="Stop (s)" value={flow.stop_s} min={0} onChange={(v) => setFlow(index, { stop_s: v })} />
                <SelectField<FlowPattern>
                  label="Pattern"
                  value={flow.pattern}
                  options={["constant", "bursty", "poisson"]}
                  onChange={(pattern) => setFlow(index, { pattern })}
                />
                {flow.pattern === "bursty" && (
                  <div className="grid-2 tight">
                    <NumberField label="On (s)" value={flow.on_duration_s} min={0} onChange={(v) => setFlow(index, { on_duration_s: v })} />
                    <NumberField label="Off (s)" value={flow.off_duration_s} min={0} onChange={(v) => setFlow(index, { off_duration_s: v })} />
                  </div>
                )}
              </div>
            </article>
          ))}
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <div>
            <h3>Scheduled events</h3>
            <p>Failures, congestion and bursts at set times</p>
          </div>
          <div className="spacer" />
          <button
            type="button"
            className="btn sm"
            onClick={() => setSettings((s) => ({ ...s, events: [...s.events, makeEvent(topology)] }))}
          >
            <Plus /> Add event
          </button>
        </div>
        <div className="card-body stack">
          {events.length === 0 && <p className="muted">No events. The network stays healthy for the whole run.</p>}
          {events.map((item, index) => (
            <article className="row-editor" key={index}>
              <div className="row-editor-head">
                <span className="row-index">Event {index + 1}</span>
                <span className="muted small">
                  {EVENT_LABELS[item.type]} at {item.time_s} s
                </span>
                <div className="spacer" />
                <button
                  type="button"
                  className="btn ghost sm icon"
                  aria-label={`Remove event ${index + 1}`}
                  onClick={() => setSettings((s) => ({ ...s, events: s.events.filter((_, i) => i !== index) }))}
                >
                  <Trash2 />
                </button>
              </div>
              <div className="grid-4">
                <SelectField
                  label="Type"
                  value={item.type}
                  options={EVENT_TYPES.map((value) => ({ value, label: EVENT_LABELS[value] }))}
                  onChange={(type) => setEvent(index, { type })}
                />
                <NumberField label="Time (s)" value={item.time_s} min={0} onChange={(v) => setEvent(index, { time_s: v })} />
                {LINK_EVENTS.includes(item.type) && (
                  <SelectField
                    label="Link"
                    value={item.a && item.b ? `${item.a}|${item.b}` : ""}
                    options={links}
                    placeholder="Choose link"
                    onChange={(value) => {
                      const [a = "", b = ""] = value.split("|");
                      setEvent(index, { a, b });
                    }}
                  />
                )}
                {ROUTER_EVENTS.includes(item.type) && (
                  <SelectField label="Router" value={item.node} options={routers} placeholder="Choose router" onChange={(node) => setEvent(index, { node })} />
                )}
                {item.type === "CONGESTION" && (
                  <>
                    <NumberField label="Bandwidth factor" value={item.bandwidth_multiplier} min={0} max={1} step={0.05} onChange={(v) => setEvent(index, { bandwidth_multiplier: v })} />
                    <NumberField label="Extra loss rate" value={item.extra_loss_rate} min={0} max={1} step={0.01} onChange={(v) => setEvent(index, { extra_loss_rate: v })} />
                    <NumberField label="Duration (s)" value={item.duration_s} min={0} onChange={(v) => setEvent(index, { duration_s: v })} />
                  </>
                )}
                {item.type === "TRAFFIC_BURST" && (
                  <>
                    <SelectField label="Source" value={item.src} options={hosts} placeholder="Choose host" onChange={(src) => setEvent(index, { src })} />
                    <SelectField label="Destination" value={item.dst} options={hosts} placeholder="Choose host" onChange={(dst) => setEvent(index, { dst })} />
                    <NumberField label="Rate (Mbps)" value={item.rate_mbps} min={0} onChange={(v) => setEvent(index, { rate_mbps: v })} />
                    <NumberField label="Packet size (bytes)" value={item.packet_bytes} min={1} step={1} onChange={(v) => setEvent(index, { packet_bytes: v })} />
                    <NumberField label="Duration (s)" value={item.duration_s} min={0} onChange={(v) => setEvent(index, { duration_s: v })} />
                  </>
                )}
              </div>
            </article>
          ))}
        </div>
      </section>
    </div>
  );
}
