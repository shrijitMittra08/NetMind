import { useState } from "react";
import { FileJson, Upload } from "lucide-react";
import { parseTopologyJson } from "../../domain/topology";
import type { Topology } from "../../lib/types";

export function ImportPanel({ onLoaded }: { onLoaded: (topology: Topology, name: string) => void }) {
  const [error, setError] = useState("");
  const [dragging, setDragging] = useState(false);
  const [loaded, setLoaded] = useState("");

  async function read(file: File | undefined) {
    if (!file) return;
    setError("");
    try {
      const topology = parseTopologyJson(await file.text());
      onLoaded(topology, file.name);
      setLoaded(file.name);
    } catch (problem) {
      setError(`${file.name}: ${(problem as Error).message}`);
    }
  }

  return (
    <div className="stack">
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
          void read(event.dataTransfer.files[0]);
        }}
      >
        <span className="dropzone-icon">
          <Upload />
        </span>
        <strong>Drop a topology JSON here</strong>
        <span className="muted">or click to choose a file</span>
        <input
          type="file"
          accept=".json,application/json"
          className="sr-only"
          onChange={(event) => {
            void read(event.target.files?.[0]);
            event.target.value = "";
          }}
        />
      </label>
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
