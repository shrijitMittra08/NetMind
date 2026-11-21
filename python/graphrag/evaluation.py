"""Score question answering against ground truth computed from raw run files.

    python -m graphrag.evaluation [--cli PATH] [--json REPORT] [--verbose]

Generates fresh, seeded runs for the scenarios in eval_questions.json with the real
simulator, then scores:
  * "old"   - the current rule-based GraphRAG, asked exactly as the viewer asks it;
  * "tools" - the deterministic tools in graphrag.tools, called the way an agent would.

The oracle below reads the CSVs with the csv module and does not import the tools,
so the tools are not graded against themselves.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = Path(__file__).with_name("eval_questions.json")
TIE_TOLERANCE = 0.001
TIE_WORDS = ("tie", "tied", "same", "equal", "identical")
ALGORITHM_WORDS = {"bfs": "bfs", "dfs": "dfs", "dijkstra": "dijkstra", "bellman-ford": "bellman ford"}


def find_cli(explicit: str | None) -> Path:
    candidates = [explicit, os.environ.get("NETMIND_CLI")]
    for build in ("release", "debug"):
        for name in ("netmind_cli.exe", "netmind_cli"):
            candidates.append(str(PROJECT_ROOT / "build" / build / name))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise SystemExit("netmind_cli not found; build it or pass --cli")


def generate_runs(spec: dict[str, Any], cli: Path, out: Path) -> dict[str, dict[str, Any]]:
    runs: dict[str, dict[str, Any]] = {}
    for name, scenario in spec["scenarios"].items():
        topology = (PROJECT_ROOT / scenario["topology"]).resolve()
        runs[name] = {}
        for algorithm in scenario["algorithms"]:
            path = out / f"scenario_{name}_{algorithm}.json"
            path.write_text(json.dumps({
                "name": f"eval_{name}_{algorithm}",
                "topology": str(topology),
                "seed": scenario["seed"],
                "duration_s": scenario["duration_s"],
                "routing": {"algorithm": algorithm, "weight": scenario["weight"], "reconvergence_delay_ms": 0},
                "flows": scenario["flows"],
                "events": scenario["events"],
            }), encoding="utf-8")
            before = {p.stem for p in out.glob("run_*.json")}
            done = subprocess.run([str(cli), "--scenario", str(path), "--out", str(out), "--quiet"],
                                  capture_output=True, text=True, check=False)
            if done.returncode != 0:
                raise SystemExit(f"Simulator failed for {name}/{algorithm}:\n{done.stderr or done.stdout}")
            (run_id,) = {p.stem for p in out.glob("run_*.json")} - before
            runs[name][algorithm] = {"run_id": run_id, "topology": topology}
    return runs


def _rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        return list(csv.DictReader(stream))


def _reason(detail: str) -> str:
    match = re.search(r"reason=([^;]+)", detail)
    return match.group(1) if match else "unknown"


class Oracle:
    def __init__(self, data: Path, runs: dict[str, dict[str, Any]]):
        self.data = data
        self.runs = runs

    def _names(self, topology: Path) -> tuple[dict[str, str], dict[str, tuple[str, str]]]:
        doc = json.loads(topology.read_text(encoding="utf-8"))
        nodes = {str(n["id"]): n["name"] for n in doc["nodes"]}
        links = {str(i): (nodes[str(l["a"])], nodes[str(l["b"])]) for i, l in enumerate(doc["links"], start=1)}
        return nodes, links

    def best_algorithm(self, scenario: str, metric: str) -> list[str]:
        column = {"latency_ms": ("avg_latency_ms", min), "reliability_pct": ("reliability_pct", max),
                  "throughput_mbps": ("avg_throughput_mbps", max), "drops": ("total_drops", min)}
        values = {}
        for algorithm, info in self.runs[scenario].items():
            if metric == "avg_hops":
                flows = _rows(self.data / f"{info['run_id']}_flows.csv")
                delivered = sum(float(f["delivered"]) for f in flows)
                values[algorithm] = sum(float(f["avg_hops"]) * float(f["delivered"]) for f in flows) / delivered
                pick = min
            else:
                name, pick = column[metric]
                values[algorithm] = float(_rows(self.data / f"{info['run_id']}_network.csv")[0][name])
        best = pick(values.values())
        scale = max(abs(best), 1e-9)
        return [a for a, v in values.items() if abs(v - best) <= TIE_TOLERANCE * scale]

    def first_failure(self, scenario: str) -> tuple[list[str], float]:
        info = next(iter(self.runs[scenario].values()))
        nodes, links = self._names(info["topology"])
        for row in _rows(self.data / f"{info['run_id']}_trace.csv"):
            if row["event_type"] == "LINK_DOWN":
                return list(links[row["location"]]), float(row["time_s"])
            if row["event_type"] == "ROUTER_DOWN":
                return [nodes[row["location"]]], float(row["time_s"])
        raise ValueError(f"No failure in {scenario}")

    def flow_path(self, scenario: str, run: str, src: str, dst: str, at: float) -> list[str]:
        info = self.runs[scenario][run]
        nodes, _ = self._names(info["topology"])
        flow = next(f["flow_id"] for f in _rows(self.data / f"{info['run_id']}_flows.csv")
                    if nodes[f["src"]] == src and nodes[f["dst"]] == dst)
        hops: dict[str, list[tuple[float, str]]] = {}
        delivered = set()
        for row in _rows(self.data / f"{info['run_id']}_trace.csv"):
            if row["flow_id"] != flow:
                continue
            if row["event_type"] == "TX_START":
                hops.setdefault(row["packet_id"], []).append((float(row["time_s"]), row["location"]))
            elif row["event_type"] == "DELIVERED":
                delivered.add(row["packet_id"])
        candidates = sorted((min(t for t, _ in hops[p]), p) for p in delivered if p in hops)
        chosen = next((p for t, p in candidates if t >= at), candidates[-1][1])
        channels = [c for _, c in sorted(hops[chosen])]
        return [channels[0].split(">")[0]] + [c.split(">")[1] for c in channels]

    def drops(self, scenario: str, run: str, t_from: float | None, t_to: float | None) -> tuple[str, int]:
        counts: dict[str, int] = {}
        for row in _rows(self.data / f"{self.runs[scenario][run]['run_id']}_trace.csv"):
            t = float(row["time_s"])
            if row["event_type"] != "DROPPED" or (t_from is not None and t < t_from) or (t_to is not None and t > t_to):
                continue
            reason = _reason(row["detail"])
            counts[reason] = counts.get(reason, 0) + 1
        if not counts:
            return "none", 0
        reason = max(counts, key=lambda k: counts[k])
        return reason, sum(counts.values())

    def drop_total(self, scenario: str, run: str) -> int:
        return int(float(_rows(self.data / f"{self.runs[scenario][run]['run_id']}_network.csv")[0]["total_drops"]))

    def busiest_link(self, scenario: str, run: str) -> list[str]:
        rows = _rows(self.data / f"{self.runs[scenario][run]['run_id']}_links.csv")
        top = max(rows, key=lambda r: float(r["utilization_pct"]))
        return top["channel"].split(">")


def expected_for(oracle: Oracle, question: dict[str, Any]) -> Any:
    check, scenario = question["check"], question["scenario"]
    kind = check["kind"]
    if kind == "best_algorithm":
        return oracle.best_algorithm(scenario, check["metric"])
    if kind == "first_failure":
        return oracle.first_failure(scenario)
    if kind == "flow_path":
        return oracle.flow_path(scenario, check["run"], check["src"], check["dst"], check["at"])
    if kind == "drop_reason":
        reason, count = oracle.drops(scenario, check["run"], check["from"], check["to"])
        return reason, count, check["from"] is not None
    if kind == "drop_total":
        return oracle.drop_total(scenario, check["run"])
    if kind == "busiest_link":
        return oracle.busiest_link(scenario, check["run"])
    raise ValueError(kind)


def _norm(text: str) -> str:
    return re.sub(r"\s+", " ", re.sub(r"[–—→>_\-|,()]", " ", text.lower())).strip()


def _numbers(text: str) -> list[float]:
    text = re.sub(r"(?<=\d)[,   ](?=\d{3}(?!\d))", "", text)
    return [float(n) for n in re.findall(r"\d+(?:\.\d+)?", text)]


REASON_PHRASES = {
    "no_route": ("no route", "find a route", "no path", "no available route", "without a route"),
    "queue_full": ("queue full", "queue was full", "full queue", "queue overflow", "queues were full"),
    "link_down": ("link down", "link was down", "link went down", "link failure"),
    "node_down": ("node down", "router down", "router was down", "router went down", "node was down"),
    "random_loss": ("random loss", "random packet loss", "loss rate"),
}


def _node_sequence(text: str, names: set[str]) -> list[str]:
    lookup = {n.lower(): n for n in names}
    return [lookup[t] for t in re.findall(r"[a-z0-9]+", text.lower()) if t in lookup]


def _contains_run(sequence: list[str], wanted: list[str]) -> bool:
    return any(sequence[i:i + len(wanted)] == wanted for i in range(len(sequence) - len(wanted) + 1))


def score_text(kind: str, expected: Any, answer: str, all_algorithms: list[str], names: set[str]) -> bool:
    text = _norm(answer)
    if kind == "best_algorithm":
        if len(expected) > 1:
            return any(re.search(rf"\b{w}\b", text) for w in TIE_WORDS)
        mentioned = sorted(
            (text.find(ALGORITHM_WORDS[a]), a) for a in all_algorithms if ALGORITHM_WORDS[a] in text
        )
        return bool(mentioned) and mentioned[0][1] == expected[0]
    if kind == "first_failure":
        targets, time = expected
        for clause in re.split(r"[.;\n]", answer):
            if not re.search(r"\b(down|fail\w*|went|lost)\b", clause.lower()):
                continue
            sequence = _node_sequence(clause, names)
            named = _contains_run(sequence, targets) or _contains_run(sequence, targets[::-1])
            if named and any(abs(n - time) < 0.05 for n in _numbers(clause)):
                return True
        return False
    if kind in {"flow_path"}:
        return _contains_run(_node_sequence(answer, names), expected)
    if kind == "busiest_link":
        sequence = _node_sequence(answer, names)
        return _contains_run(sequence, expected) or _contains_run(sequence, expected[::-1])
    if kind == "drop_reason":
        reason, count, windowed = (*expected, False)[:3]
        phrases = (reason.replace("_", " "), *REASON_PHRASES.get(reason, ()))
        if not any(phrase in text for phrase in phrases):
            return False
        return windowed or count in [int(n) for n in _numbers(answer) if n == int(n)]
    if kind == "drop_total":
        return expected in [int(n) for n in _numbers(answer) if n == int(n)]
    raise ValueError(kind)


def _agent_with_rate_limit_waits(session: Any, question: str, llm: Any, attempts: int = 4) -> dict[str, Any]:
    import time

    from graphrag.agent import answer_with_agent

    for attempt in range(attempts):
        try:
            return answer_with_agent(session, question, llm)
        except Exception as error:  # noqa: BLE001
            text = str(error)
            if "429" not in text and "rate limit" not in text.lower() or attempt == attempts - 1:
                raise
            wait = re.search(r"try again in ([\d.]+)s", text)
            time.sleep(min(65.0, float(wait.group(1)) + 1.0) if wait else 30.0)
    raise RuntimeError("unreachable")


def score_tool(kind: str, expected: Any, result: dict[str, Any]) -> bool:
    if kind == "best_algorithm":
        return sorted(result["best"]) == sorted(expected) and result["tie"] == (len(expected) > 1)
    if kind == "first_failure":
        targets, time = expected
        first = next(e for e in result["events"] if e["event"] in {"LINK_DOWN", "ROUTER_DOWN"})
        return first["target"] == "–".join(targets) and abs(first["time_s"] - time) < 1e-6
    if kind == "flow_path":
        return result.get("path_at_time") == " → ".join(expected)
    if kind == "busiest_link":
        return result["busiest"]["channel"] == "→".join(expected)
    if kind == "drop_reason":
        reason, count = expected[:2]
        top = max(result["by_reason"], key=lambda k: result["by_reason"][k]) if result["by_reason"] else "none"
        return top == reason and result["total"] == count
    if kind == "drop_total":
        return result["drops"] == expected
    raise ValueError(kind)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", help="path to netmind_cli")
    parser.add_argument("--json", help="write the full report to this file")
    parser.add_argument("--verbose", action="store_true", help="print every answer")
    parser.add_argument("--engines", default="old,rules,agent",
                        help="comma list of old, rules, agent (agent is skipped when no LLM is configured)")
    args = parser.parse_args()

    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    cli = find_cli(args.cli)
    with tempfile.TemporaryDirectory(prefix="netmind_eval_") as temporary:
        data = Path(temporary)
        runs = generate_runs(spec, cli, data)

        os.environ["NETMIND_DATA_DIR"] = str(data)
        import netmind_api
        from graphrag import tools

        from graphrag.answer_rules import answer_with_rules
        from graphrag.session import QASession

        engines = [e.strip() for e in args.engines.split(",") if e.strip()]
        llm = None
        if "agent" in engines:
            from graphrag import llm as llm_config
            llm = llm_config.chat_model()
            if llm is None:
                print("agent skipped: no LLM configured "
                      "(set NETMIND_LLM_MODEL, NETMIND_LLM_BASE_URL, NETMIND_LLM_API_KEY)\n")
                engines.remove("agent")

        oracle = Oracle(data, runs)
        contexts = {
            (s, a): tools.load_run(data, info["run_id"], info["topology"])
            for s, by_algo in runs.items() for a, info in by_algo.items()
        }
        report = []
        for question in spec["questions"]:
            scenario, kind = question["scenario"], question["check"]["kind"]
            expected = expected_for(oracle, question)
            run_ids = [info["run_id"] for info in runs[scenario].values()]
            names = {n["name"] for n in json.loads(
                next(iter(runs[scenario].values()))["topology"].read_text(encoding="utf-8"))["nodes"]}

            session = QASession([contexts[(scenario, a)] for a in runs[scenario]])
            answers: dict[str, str] = {}
            for engine in engines:
                try:
                    if engine == "old":
                        result = netmind_api._answer_graphrag_legacy(run_ids, question["question"])
                    elif engine == "rules":
                        result = answer_with_rules(session, question["question"])
                    else:
                        result = _agent_with_rate_limit_waits(session, question["question"], llm)
                    answers[engine] = str(result.get("answer", ""))
                except Exception as error:  # noqa: BLE001
                    answers[engine] = f"ERROR: {error}"
            passed = {e: score_text(kind, expected, a, list(runs[scenario]), names) for e, a in answers.items()}

            call = question["tool"]
            function = getattr(tools, call["name"])
            if call["name"] == "compare_runs":
                result = function([contexts[(scenario, a)] for a in runs[scenario]], **call["args"])
            else:
                result = function(contexts[(scenario, call["run"])], **call["args"])
            tool_ok = score_tool(kind, expected, result)

            report.append({"id": question["id"], "question": question["question"], "kind": kind,
                           "expected": expected, "answers": answers, "passed": passed, "tool_ok": tool_ok})

    width = max(len(r["id"]) for r in report)
    print(f"{'question'.ljust(width)}  " + "  ".join(e.ljust(6) for e in engines) + "  tools   expected")
    for r in report:
        marks = "  ".join(("pass" if r["passed"][e] else "FAIL").ljust(6) for e in engines)
        print(f"{r['id'].ljust(width)}  {marks}  {'pass' if r['tool_ok'] else 'FAIL'}    {r['expected']}")
        for engine in engines:
            if args.verbose or (engine != "old" and not r["passed"][engine]):
                print(f"{'':{width}}    {engine}: {r['answers'][engine][:220]!r}")
    total = len(report)
    print("\n" + "   ".join(f"{e}: {sum(r['passed'][e] for r in report)}/{total}" for e in engines)
          + f"   tools: {sum(r['tool_ok'] for r in report)}/{total}")
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")
    return 0 if all(r["tool_ok"] for r in report) else 1


if __name__ == "__main__":
    sys.exit(main())
