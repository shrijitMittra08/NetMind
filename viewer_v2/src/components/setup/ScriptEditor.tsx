import { useRef } from "react";
import { BookOpen, Download, FileUp, Wand2, ArrowLeftRight, CircleAlert, TriangleAlert } from "lucide-react";
import { generateScript, SCRIPT_REFERENCE } from "../../domain/script";
import { saveTextFile } from "../../lib/saveFile";
import { useWorkbench } from "../../state/workbench";

export function ScriptEditor() {
  const { script, setScript, parsedScript, settings, setSettings, scriptResult, exportResults, setExportResults, startRun, problems, run, goTo } =
    useWorkbench();
  const gutter = useRef<HTMLDivElement>(null);
  const lines = Math.max(script.split("\n").length, 14);
  const flagged = new Map<number, "error" | "warning">();
  for (const note of parsedScript.notes) {
    if (note.level === "error") flagged.set(note.line, "error");
    else if (note.level === "warning" && !flagged.has(note.line)) flagged.set(note.line, "warning");
  }
  const problemNotes = parsedScript.notes.filter((note) => note.level !== "info");
  const infoNotes = parsedScript.notes.filter((note) => note.level === "info");
  const hasErrors = problemNotes.some((note) => note.level === "error");

  return (
    <div className="stack">
      <section className="card">
        <div className="card-head">
          <div>
            <h3>Script</h3>
            <p>One CLI command per line. The active topology is loaded for you.</p>
          </div>
          <div className="spacer" />
          <button type="button" className="btn sm" onClick={() => setScript(generateScript(settings))}>
            <Wand2 /> From interactive
          </button>
          <button
            type="button"
            className="btn sm"
            disabled={!script.trim() || hasErrors}
            title={hasErrors ? "Fix the errors first" : "Copy these settings into the interactive editor"}
            onClick={() => setSettings(parsedScript.settings)}
          >
            <ArrowLeftRight /> To interactive
          </button>
          <label className="btn sm">
            <FileUp /> Open
            <input
              type="file"
              accept=".txt,.nm,.cli,text/plain"
              className="sr-only"
              onChange={(event) => {
                void event.target.files?.[0]?.text().then(setScript);
                event.target.value = "";
              }}
            />
          </label>
          <button
            type="button"
            className="btn sm"
            disabled={!script.trim()}
            onClick={() => void saveTextFile(script, "netmind_script.txt")}
          >
            <Download /> Save
          </button>
        </div>
        <div className="editor">
          <div className="editor-gutter" ref={gutter} aria-hidden>
            {Array.from({ length: lines }, (_, i) => (
              <span key={i} className={flagged.get(i + 1)}>
                {i + 1}
              </span>
            ))}
          </div>
          <textarea
            className="editor-text"
            spellCheck={false}
            value={script}
            aria-label="Script"
            placeholder={"set seed 42\nset algo dijkstra\nadd flow PC1 PC2 5 1000 0 10\nfail link R1 R2 at 2\nrun 10\nshow metrics\nexport"}
            onChange={(event) => setScript(event.target.value)}
            onScroll={(event) => {
              if (gutter.current) gutter.current.scrollTop = event.currentTarget.scrollTop;
            }}
            onKeyDown={(event) => {
              if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
                event.preventDefault();
                if (problems.length === 0 && run.status !== "running") void startRun();
              } else if (event.key === "Tab") {
                event.preventDefault();
                const el = event.currentTarget;
                const { selectionStart: s, selectionEnd: e } = el;
                setScript(script.slice(0, s) + "  " + script.slice(e));
                requestAnimationFrame(() => el.setSelectionRange(s + 2, s + 2));
              }
            }}
          />
        </div>
        <div className="card-body stack editor-foot">
          {problemNotes.length > 0 && (
            <ul className="script-notes">
              {problemNotes.map((note, i) => (
                <li key={i} className={note.level}>
                  {note.level === "error" ? <CircleAlert /> : <TriangleAlert />}
                  <span className="mono">L{note.line}</span> {note.message}
                </li>
              ))}
            </ul>
          )}
          {infoNotes.length > 0 && (
            <details className="disclosure">
              <summary>{infoNotes.length} command(s) run but are not stored in a scenario</summary>
              <ul>
                {infoNotes.map((note, i) => (
                  <li key={i}>
                    <span className="mono">L{note.line}</span> {note.message}
                  </li>
                ))}
              </ul>
            </details>
          )}
          <label className="check">
            <input type="checkbox" checked={exportResults} onChange={(e) => setExportResults(e.target.checked)} />
            Add <code>export</code> automatically when the script runs a simulation, so it can be analysed and replayed
          </label>
          <details className="disclosure">
            <summary>
              <BookOpen /> Command reference · Ctrl+Enter runs the script
            </summary>
            <table className="data reference">
              <tbody>
                {SCRIPT_REFERENCE.map(([command, description]) => (
                  <tr key={command}>
                    <td>
                      <code>{command}</code>
                    </td>
                    <td className="muted">{description}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </details>
        </div>
      </section>

      {scriptResult && (
        <section className="card">
          <div className="card-head">
            <h3>Output</h3>
            <div className="spacer" />
            <span className={`badge ${scriptResult.ok ? "success" : "danger"}`}>
              {scriptResult.ok ? "All commands succeeded" : "Some commands failed"}
            </span>
            {scriptResult.runs.length > 0 && (
              <button type="button" className="btn sm primary" onClick={() => goTo("results")}>
                Open results ({scriptResult.runs.length}) →
              </button>
            )}
          </div>
          <div className="terminal" role="log" aria-label="Script output">
            {scriptResult.transcript.map((entry, i) => (
              <div key={i} className="term-entry">
                <div className="term-cmd">
                  <span className="term-prompt">netmind›</span> {entry.command}
                  {entry.line > 0 && <span className="term-line">L{entry.line}</span>}
                </div>
                {entry.output && <pre className={entry.ok ? "term-out" : "term-out err"}>{entry.output}</pre>}
              </div>
            ))}
          </div>
          {scriptResult.notes.length > 0 && (
            <div className="card-body stack">
              {scriptResult.notes.map((note, i) => (
                <p key={i} className="muted small">
                  {note}
                </p>
              ))}
            </div>
          )}
        </section>
      )}
    </div>
  );
}
