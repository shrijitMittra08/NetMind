import { useState } from "react";
import type { ReactNode } from "react";
import { FileJson, Upload } from "lucide-react";
import { parseTopologyJson } from "../../domain/topology";
import type { Topology } from "../../lib/types";

interface JsonDropzoneProps {
  title: string;
  icon: ReactNode;
  disabled?: boolean;
  onFile: (file: File | undefined) => void;
}

export function JsonDropzone({ title, icon, disabled, onFile }: JsonDropzoneProps) {
  const [dragging, setDragging] = useState(false);
  return (
    <label
      className={`dropzone${dragging ? " dragging" : ""}`}
      onDragOver={(event) => {
        event.preventDefault();
        setDragging(true);
      }}
      onDragLeave={() => setDragging(false)}
      onDrop={(event) => {
        event.preventDefault();
        setDragging(false);
        onFile(event.dataTransfer.files[0]);
      }}
    >
      <span className="dropzone-icon">{icon}</span>
      <strong>{title}</strong>
      <span className="muted">or click to choose a file</span>
      <input
        type="file"
        accept=".json,application/json"
        className="sr-only"
        disabled={disabled}
        onChange={(event) => {
          onFile(event.target.files?.[0]);
          event.target.value = "";
        }}
      />
    </label>
  );
}

export function ImportPanel({ onLoaded }: { onLoaded: (topology: Topology, name: string) => void }) {
  const [error, setError] = useState("");
  const [loaded, setLoaded] = useState("");

  async function read(file: File | undefined) {
    if (!file) return;
    setError("");
    try {
      onLoaded(parseTopologyJson(await file.text()), file.name);
      setLoaded(file.name);
    } catch (problem) {
      setError(`${file.name}: ${(problem as Error).message}`);
    }
  }

  return (
    <div className="stack">
      <JsonDropzone title="Drop a topology JSON here" icon={<Upload />} onFile={(file) => void read(file)} />
      {error && <div className="notice danger">{error}</div>}
      {loaded && !error && (
        <div className="notice success">
          <FileJson />
          {loaded} is now the active topology. Continue to Simulation, or open it in the builder to edit.
        </div>
      )}
      <p className="muted small">
        Files follow docs/contracts/topology.md: <code>nodes</code> (id, type, name, ip, optional pos) and{" "}
        <code>links</code> (a, b, bandwidthMbps, latencyMs, lossRate). Sample files live in config/topologies. To load a topology together with its simulation settings, use “Import scenario”.
      </p>
    </div>
  );
}
