import { Plus, Trash2 } from "lucide-react";
import { EVENT_LABELS, LINK_EVENTS, ROUTER_EVENTS, makeEvent, makeFlow } from "../../domain/setup";
import { linkNamePairs } from "../../domain/topology";
import { algorithmLabel } from "../../lib/format";
import { ALGORITHMS, EVENT_TYPES, WEIGHTS } from "../../lib/types";
import type { EventDraft, FlowDraft, FlowPattern, NodeType, SimConfig, Topology } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import { NumberField, SelectField } from "../ui";

const FLOW_PATTERNS: FlowPattern[] = ["constant", "bursty", "poisson"];

type LinkOption = { value: string; label: string };

function nodeNames(topology: Topology | null, type: NodeType): string[] {
  return (topology?.nodes ?? []).filter((node) => node.type === type).map((node) => node.name);
}

function SectionHead({ title, subtitle, addLabel, onAdd }: { title: string; subtitle: string; addLabel: string; onAdd: () => void }) {
  return (
    <div className="card-head">
      <div>
        <h3>{title}</h3>
        <p>{subtitle}</p>
      </div>
      <div className="spacer" />
      <button type="button" className="btn sm" onClick={onAdd}>
        <Plus /> {addLabel}
      </button>
    </div>
  );
}

function RowEditorHead({ title, summary, onRemove }: { title: string; summary: string; onRemove: () => void }) {
  return (
    <div className="row-editor-head">
      <span className="row-index">{title}</span>
      <span className="muted small">{summary}</span>
      <div className="spacer" />
      <button type="button" className="btn ghost sm icon" aria-label={`Remove ${title.toLowerCase()}`} onClick={onRemove}>
        <Trash2 />
      </button>
    </div>
  );
}

interface FlowEditorProps {
  flow: FlowDraft;
  index: number;
  hosts: string[];
  onChange: (patch: Partial<FlowDraft>) => void;
  onRemove: () => void;
}

function FlowEditor({ flow, index, hosts, onChange, onRemove }: FlowEditorProps) {
  return (
    <article className="row-editor">
      <RowEditorHead title={`Flow ${index + 1}`} summary={`${flow.src || "?"} → ${flow.dst || "?"}`} onRemove={onRemove} />
      <div className="grid-4">
        <SelectField label="Source" value={flow.src} options={hosts} placeholder="Choose host" onChange={(src) => onChange({ src })} />
        <SelectField label="Destination" value={flow.dst} options={hosts} placeholder="Choose host" onChange={(dst) => onChange({ dst })} />
        <NumberField label="Rate (Mbps)" value={flow.rate_mbps} min={0} onChange={(v) => onChange({ rate_mbps: v })} />
        <NumberField label="Packet size (bytes)" value={flow.packet_bytes} min={1} step={1} onChange={(v) => onChange({ packet_bytes: v })} />
        <NumberField label="Start (s)" value={flow.start_s} min={0} onChange={(v) => onChange({ start_s: v })} />
        <NumberField label="Stop (s)" value={flow.stop_s} min={0} onChange={(v) => onChange({ stop_s: v })} />
        <SelectField<FlowPattern> label="Pattern" value={flow.pattern} options={FLOW_PATTERNS} onChange={(pattern) => onChange({ pattern })} />
        {flow.pattern === "bursty" && (
          <div className="grid-2 tight">
            <NumberField label="On (s)" value={flow.on_duration_s} min={0} onChange={(v) => onChange({ on_duration_s: v })} />
            <NumberField label="Off (s)" value={flow.off_duration_s} min={0} onChange={(v) => onChange({ off_duration_s: v })} />
          </div>
        )}
      </div>
    </article>
  );
}

interface EventEditorProps {
  event: EventDraft;
  index: number;
  hosts: string[];
  routers: string[];
  links: LinkOption[];
  onChange: (patch: Partial<EventDraft>) => void;
  onRemove: () => void;
}

function EventEditor({ event, index, hosts, routers, links, onChange, onRemove }: EventEditorProps) {
  return (
    <article className="row-editor">
      <RowEditorHead title={`Event ${index + 1}`} summary={`${EVENT_LABELS[event.type]} at ${event.time_s} s`} onRemove={onRemove} />
      <div className="grid-4">
        <SelectField
          label="Type"
          value={event.type}
          options={EVENT_TYPES.map((value) => ({ value, label: EVENT_LABELS[value] }))}
          onChange={(type) => onChange({ type })}
        />
        <NumberField label="Time (s)" value={event.time_s} min={0} onChange={(v) => onChange({ time_s: v })} />
        {LINK_EVENTS.includes(event.type) && (
          <SelectField
            label="Link"
            value={event.a && event.b ? `${event.a}|${event.b}` : ""}
            options={links}
            placeholder="Choose link"
            onChange={(value) => {
              const [a = "", b = ""] = value.split("|");
              onChange({ a, b });
            }}
          />
        )}
        {ROUTER_EVENTS.includes(event.type) && (
          <SelectField label="Router" value={event.node} options={routers} placeholder="Choose router" onChange={(node) => onChange({ node })} />
        )}
        {event.type === "CONGESTION" && (
          <>
            <NumberField label="Bandwidth factor" value={event.bandwidth_multiplier} min={0} max={1} step={0.05} onChange={(v) => onChange({ bandwidth_multiplier: v })} />
            <NumberField label="Extra loss rate" value={event.extra_loss_rate} min={0} max={1} step={0.01} onChange={(v) => onChange({ extra_loss_rate: v })} />
            <NumberField label="Duration (s)" value={event.duration_s} min={0} onChange={(v) => onChange({ duration_s: v })} />
          </>
        )}
        {event.type === "TRAFFIC_BURST" && (
          <>
            <SelectField label="Source" value={event.src} options={hosts} placeholder="Choose host" onChange={(src) => onChange({ src })} />
            <SelectField label="Destination" value={event.dst} options={hosts} placeholder="Choose host" onChange={(dst) => onChange({ dst })} />
            <NumberField label="Rate (Mbps)" value={event.rate_mbps} min={0} onChange={(v) => onChange({ rate_mbps: v })} />
            <NumberField label="Packet size (bytes)" value={event.packet_bytes} min={1} step={1} onChange={(v) => onChange({ packet_bytes: v })} />
            <NumberField label="Duration (s)" value={event.duration_s} min={0} onChange={(v) => onChange({ duration_s: v })} />
          </>
        )}
      </div>
    </article>
  );
}

export function InteractiveSetup() {
  const { topology, settings, setSettings } = useWorkbench();
  const hosts = nodeNames(topology, "HOST");
  const routers = nodeNames(topology, "ROUTER");
  const links: LinkOption[] = topology ? linkNamePairs(topology).map(([a, b]) => ({ value: `${a}|${b}`, label: `${a} — ${b}` })) : [];
  const { config, flows, events } = settings;

  const setConfig = (patch: Partial<SimConfig>) => setSettings((s) => ({ ...s, config: { ...s.config, ...patch } }));
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
          <SelectField label="Routing weight" value={config.weight} options={WEIGHTS} onChange={(weight) => setConfig({ weight })} />
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
        <SectionHead
          title="Traffic flows"
          subtitle="Packets sent between hosts"
          addLabel="Add flow"
          onAdd={() => setSettings((s) => ({ ...s, flows: [...s.flows, makeFlow(topology)] }))}
        />
        <div className="card-body stack">
          {flows.length === 0 && <p className="muted">No flows yet. A run needs at least one.</p>}
          {flows.map((flow, index) => (
            <FlowEditor
              key={index}
              flow={flow}
              index={index}
              hosts={hosts}
              onChange={(patch) => setFlow(index, patch)}
              onRemove={() => setSettings((s) => ({ ...s, flows: s.flows.filter((_, i) => i !== index) }))}
            />
          ))}
        </div>
      </section>

      <section className="card">
        <SectionHead
          title="Scheduled events"
          subtitle="Failures, congestion and bursts at set times"
          addLabel="Add event"
          onAdd={() => setSettings((s) => ({ ...s, events: [...s.events, makeEvent(topology)] }))}
        />
        <div className="card-body stack">
          {events.length === 0 && <p className="muted">No events. The network stays healthy for the whole run.</p>}
          {events.map((item, index) => (
            <EventEditor
              key={index}
              event={item}
              index={index}
              hosts={hosts}
              routers={routers}
              links={links}
              onChange={(patch) => setEvent(index, patch)}
              onRemove={() => setSettings((s) => ({ ...s, events: s.events.filter((_, i) => i !== index) }))}
            />
          ))}
        </div>
      </section>
    </div>
  );
}
