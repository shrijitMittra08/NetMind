import { useEffect, useRef, useState } from "react";
import { ArrowUp, Bot, LoaderCircle, Sparkles, TriangleAlert, User, Wrench } from "lucide-react";
import { api } from "../../lib/api";
import { algorithmLabel } from "../../lib/format";
import type { GraphRagAnswer, GraphRagStatus } from "../../lib/types";
import { useWorkbench } from "../../state/workbench";
import { EmptyState } from "../ui";

const EXAMPLES = [
  "Which algorithm performed best?",
  "Why were packets dropped around 5 seconds?",
  "Which link was most congested?",
  "What path did PC1 to PC2 use?",
];

interface Exchange {
  question: string;
  answer?: GraphRagAnswer;
  error?: string;
}

export function InsightsPage() {
  const { runs, topology } = useWorkbench();
  const [question, setQuestion] = useState("");
  const [thread, setThread] = useState<Exchange[]>([]);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState<GraphRagStatus | null>(null);
  const end = useRef<HTMLDivElement>(null);

  useEffect(() => {
    api.graphragStatus().then(setStatus).catch(() => setStatus(null));
  }, []);
  const runKey = runs.map((r) => r.run_id).join(",");

  const [threadKey, setThreadKey] = useState(runKey);
  if (threadKey !== runKey) {
    setThreadKey(runKey);
    setThread([]);
  }

  useEffect(() => {
    end.current?.scrollIntoView({ behavior: "smooth", block: "end" });
  }, [thread]);

  const hosts = topology?.nodes.filter((n) => n.type === "HOST").map((n) => n.name) ?? [];
  const examples = EXAMPLES.map((q) => (hosts.length >= 2 ? q.replace("PC1 to PC2", `${hosts[0]} to ${hosts[1]}`) : q));
  const label = (text: string) =>
    text.replace(/run_[A-Za-z0-9_-]+/g, (id) => {
      const run = runs.find((r) => r.run_id === id);
      return run ? algorithmLabel(run.algorithm) : id;
    });

  async function ask(text: string) {
    const q = text.trim();
    if (!q || busy) return;
    setQuestion("");
    setBusy(true);
    const history = thread.filter((x) => x.answer).map((x) => ({ question: x.question, answer: x.answer!.answer }));
    setThread((t) => [...t, { question: q }]);
    try {
      const answer = await api.askGraphRag(
        runs.map((r) => r.run_id),
        q,
        history,
      );
      setThread((t) => t.map((x, i) => (i === t.length - 1 ? { ...x, answer } : x)));
    } catch (error) {
      setThread((t) => t.map((x, i) => (i === t.length - 1 ? { ...x, error: (error as Error).message } : x)));
    } finally {
      setBusy(false);
    }
  }

  if (runs.length === 0) {
    return (
      <section className="card">
        <EmptyState icon={<Sparkles />} title="No runs to ask about" />
      </section>
    );
  }

  return (
    <section className="card insights">
      <div className="card-head">
        <div>
          <h3>Ask about {runs.length === 1 ? "this run" : `these ${runs.length} runs`}</h3>
          <p>Answers use only facts computed from the run data; every number is checked against it.</p>
        </div>
        <div className="spacer" />
        {status && (
          <span
            className={`badge ${status.engine === "agent" ? "accent" : ""}`}
            title={status.engine === "agent" ? `Model hosted at ${status.provider}` : "No AI model configured; rule-based answers"}
          >
            <Bot />
            {status.engine === "agent" ? `AI agent · ${status.model}` : "Offline answers"}
          </span>
        )}
      </div>
      <div className="thread">
        {thread.length === 0 && (
          <div className="thread-empty">
            <Sparkles />
            <p>Try one of these, or ask your own question.</p>
          </div>
        )}
        {thread.map((x, i) => (
          <div key={i} className="exchange">
            <div className="msg user">
              <span className="avatar">
                <User />
              </span>
              <p>{x.question}</p>
            </div>
            <div className="msg bot">
              <span className="avatar">
                <Sparkles />
              </span>
              <div className="stack" style={{ "--gap": "8px" } as React.CSSProperties}>
                {!x.answer && !x.error && (
                  <p className="muted">
                    <LoaderCircle className="spin inline" />{" "}
                    {status?.engine === "agent" ? "Thinking with the run data…" : "Looking at the run data…"}
                  </p>
                )}
                {x.error && <div className="notice danger">{x.error}</div>}
                {x.answer && (
                  <>
                    <span className={`badge ${x.answer.engine === "agent" ? "accent" : ""}`}>
                      {x.answer.engine === "agent" ? "AI agent" : "Rule-based"}
                    </span>
                    <p className="answer">{label(x.answer.answer)}</p>
                    {x.answer.fallback_reason && (
                      <p className="notice warning small">
                        <TriangleAlert />
                        {x.answer.fallback_reason}
                      </p>
                    )}
                    {(x.answer.facts?.length ?? 0) > 0 && (
                      <details className="disclosure">
                        <summary>
                          <Wrench /> {x.answer.facts!.length} data lookup(s) used
                        </summary>
                        <ul className="facts">
                          {x.answer.facts!.map((fact, j) => (
                            <li key={fact.id ?? j}>
                              {fact.id && <code>{fact.id}</code>} {label(fact.text)}
                            </li>
                          ))}
                        </ul>
                      </details>
                    )}
                  </>
                )}
              </div>
            </div>
          </div>
        ))}
        <div ref={end} />
      </div>
      <div className="composer">
        <div className="suggestions">
          {examples.map((q) => (
            <button key={q} type="button" className="chip" disabled={busy} onClick={() => void ask(q)}>
              {q}
            </button>
          ))}
        </div>
        <form
          className="composer-input"
          onSubmit={(event) => {
            event.preventDefault();
            void ask(question);
          }}
        >
          <input value={question} placeholder="Ask a question about the runs…" aria-label="Question" onChange={(e) => setQuestion(e.target.value)} />
          <button type="submit" className="btn primary icon" aria-label="Ask" disabled={busy || !question.trim()}>
            {busy ? <LoaderCircle className="spin" /> : <ArrowUp />}
          </button>
        </form>
      </div>
    </section>
  );
}
