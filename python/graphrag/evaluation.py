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

DESCRIPTION = (
    "Score question answering against ground truth computed from raw run files.\n\n"
    "    python -m graphrag.evaluation [--cli PATH] [--json REPORT] [--verbose]\n\n"
    "Generates fresh, seeded runs for the scenarios in eval_questions.json with the real\n"
    "simulator, then scores:\n"
    '  * "old"   - the current rule-based GraphRAG, asked exactly as the viewer asks it;\n'
    '  * "tools" - the deterministic tools in graphrag.tools, called the way an agent would.\n\n'
    "The oracle below reads the CSVs with the csv module and does not import the tools,\n"
    "so the tools are not graded against themselves.\n"
)

_NETWORK_METRICS = {
    "latency_ms": ("avg_latency_ms", min),
    "reliability_pct": ("reliability_pct", max),
    "throughput_mbps": ("avg_throughput_mbps", max),
    "drops": ("total_drops", min),
}

_PUNCTUATION_PATTERN = r"[–—→>_\-|,()]"
_DIGIT_GROUP_PATTERN = r"(?<=\d)[,   ](?=\d{3}(?!\d))"
_FAILURE_WORDS = r"\b(down|fail\w*|went|lost)\b"

REASON_PHRASES = {
    "no_route": ("no route", "find a route", "no path", "no available route", "without a route"),
    "queue_full": ("queue full", "queue was full", "full queue", "queue overflow", "queues were full"),
    "link_down": ("link down", "link was down", "link went down", "link failure"),
    "node_down": ("node down", "router down", "router was down", "router went down", "node was down"),
    "random_loss": ("random loss", "random packet loss", "loss rate"),
}


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


class Oracle:
    def __init__(self, data: Path, runs: dict[str, dict[str, Any]]):
        self.data = data
        self.runs = runs

    def _names(self, topology: Path) -> tuple[dict[str, str], dict[str, tuple[str, str]]]:
        doc = json.loads(topology.read_text(encoding="utf-8"))
        nodes = {str(node["id"]): node["name"] for node in doc["nodes"]}
        links = {}
        for index, link in enumerate(doc["links"], start=1):
            links[str(index)] = (nodes[str(link["a"])], nodes[str(link["b"])])
        return nodes, links

    def _file(self, scenario: str, run: str, suffix: str) -> Path:
        return self.data / f"{self.runs[scenario][run]['run_id']}_{suffix}.csv"

    def best_algorithm(self, scenario: str, metric: str) -> list[str]:
        values = {}
        for algorithm in self.runs[scenario]:
            if metric == "avg_hops":
                flows = _rows(self._file(scenario, algorithm, "flows"))
                delivered = sum(float(flow["delivered"]) for flow in flows)
                total_hops = sum(float(flow["avg_hops"]) * float(flow["delivered"]) for flow in flows)
                values[algorithm] = total_hops / delivered
                pick = min
            else:
                name, pick = _NETWORK_METRICS[metric]
                values[algorithm] = float(_rows(self._file(scenario, algorithm, "network"))[0][name])

        best = pick(values.values())
        allowed_gap = TIE_TOLERANCE * max(abs(best), 1e-9)
        return [algorithm for algorithm, value in values.items() if abs(value - best) <= allowed_gap]

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
        nodes, _ = self._names(self.runs[scenario][run]["topology"])
        flow_rows = _rows(self._file(scenario, run, "flows"))
        flow = next(row["flow_id"] for row in flow_rows if nodes[row["src"]] == src and nodes[row["dst"]] == dst)

        hops: dict[str, list[tuple[float, str]]] = {}
        delivered = set()
        for row in _rows(self._file(scenario, run, "trace")):
            if row["flow_id"] != flow:
                continue
            if row["event_type"] == "TX_START":
                hops.setdefault(row["packet_id"], []).append((float(row["time_s"]), row["location"]))
            elif row["event_type"] == "DELIVERED":
                delivered.add(row["packet_id"])

        candidates = []
        for packet in delivered:
            if packet in hops:
                candidates.append((min(time_s for time_s, _ in hops[packet]), packet))
        candidates.sort()
        chosen = candidates[-1][1]
        for send_time, packet in candidates:
            if send_time >= at:
                chosen = packet
                break

        channels = [channel for _, channel in sorted(hops[chosen])]
        return [channels[0].split(">")[0]] + [channel.split(">")[1] for channel in channels]

    def drops(self, scenario: str, run: str, t_from: float | None, t_to: float | None) -> tuple[str, int]:
        counts: dict[str, int] = {}
        for row in _rows(self._file(scenario, run, "trace")):
            time_s = float(row["time_s"])
            if row["event_type"] != "DROPPED":
                continue
            if t_from is not None and time_s < t_from:
                continue
            if t_to is not None and time_s > t_to:
                continue
            match = re.search(r"reason=([^;]+)", row["detail"])
            reason = match.group(1) if match else "unknown"
            counts[reason] = counts.get(reason, 0) + 1
        if not counts:
            return "none", 0
        return max(counts, key=lambda key: counts[key]), sum(counts.values())

    def drop_total(self, scenario: str, run: str) -> int:
        return int(float(_rows(self._file(scenario, run, "network"))[0]["total_drops"]))

    def busiest_link(self, scenario: str, run: str) -> list[str]:
        rows = _rows(self._file(scenario, run, "links"))
        return max(rows, key=lambda row: float(row["utilization_pct"]))["channel"].split(">")


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
    without_punctuation = re.sub(_PUNCTUATION_PATTERN, " ", text.lower())
    return re.sub(r"\s+", " ", without_punctuation).strip()


def _numbers(text: str) -> list[float]:
    text = re.sub(_DIGIT_GROUP_PATTERN, "", text)
    return [float(number) for number in re.findall(r"\d+(?:\.\d+)?", text)]


def _whole_numbers(text: str) -> list[int]:
    return [int(number) for number in _numbers(text) if number == int(number)]


def _node_sequence(text: str, names: set[str]) -> list[str]:
    name_by_lowercase = {name.lower(): name for name in names}
    return [name_by_lowercase[word] for word in re.findall(r"[a-z0-9]+", text.lower()) if word in name_by_lowercase]


def _contains_run(sequence: list[str], wanted: list[str]) -> bool:
    return any(sequence[start:start + len(wanted)] == wanted for start in range(len(sequence) - len(wanted) + 1))


def _contains_either_direction(sequence: list[str], wanted: list[str]) -> bool:
    return _contains_run(sequence, wanted) or _contains_run(sequence, wanted[::-1])


def _scores_best_algorithm(expected: list[str], text: str, all_algorithms: list[str]) -> bool:
    if len(expected) > 1:
        return any(re.search(rf"\b{word}\b", text) for word in TIE_WORDS)
    mentioned = []
    for algorithm in all_algorithms:
        word = ALGORITHM_WORDS[algorithm]
        if word in text:
            mentioned.append((text.find(word), algorithm))
    mentioned.sort()
    return bool(mentioned) and mentioned[0][1] == expected[0]


def _scores_first_failure(expected: tuple[list[str], float], answer: str, names: set[str]) -> bool:
    targets, time = expected
    for clause in re.split(r"[.;\n]", answer):
        if not re.search(_FAILURE_WORDS, clause.lower()):
            continue
        named = _contains_either_direction(_node_sequence(clause, names), targets)
        if named and any(abs(number - time) < 0.05 for number in _numbers(clause)):
            return True
    return False


def _scores_drop_reason(expected: tuple, text: str, answer: str) -> bool:
    reason, count = expected[0], expected[1]
    windowed = expected[2] if len(expected) > 2 else False
    phrases = (reason.replace("_", " "), *REASON_PHRASES.get(reason, ()))
    if not any(phrase in text for phrase in phrases):
        return False
    return windowed or count in _whole_numbers(answer)


def score_text(kind: str, expected: Any, answer: str, all_algorithms: list[str], names: set[str]) -> bool:
    text = _norm(answer)
    if kind == "best_algorithm":
        return _scores_best_algorithm(expected, text, all_algorithms)
    if kind == "first_failure":
        return _scores_first_failure(expected, answer, names)
    if kind == "flow_path":
        return _contains_run(_node_sequence(answer, names), expected)
    if kind == "busiest_link":
        return _contains_either_direction(_node_sequence(answer, names), expected)
    if kind == "drop_reason":
        return _scores_drop_reason(expected, text, answer)
    if kind == "drop_total":
        return expected in _whole_numbers(answer)
    raise ValueError(kind)


def _agent_with_rate_limit_waits(session: Any, question: str, llm: Any, attempts: int = 4) -> dict[str, Any]:
    import time

    from graphrag.agent import answer_with_agent

    for attempt in range(attempts):
        try:
            return answer_with_agent(session, question, llm)
        except Exception as error:
            text = str(error)
            is_rate_limit = "429" in text or "rate limit" in text.lower()
            if not is_rate_limit or attempt == attempts - 1:
                raise
            wait = re.search(r"try again in ([\d.]+)s", text)
            time.sleep(min(65.0, float(wait.group(1)) + 1.0) if wait else 30.0)
    raise RuntimeError("unreachable")


def score_tool(kind: str, expected: Any, result: dict[str, Any]) -> bool:
    if kind == "best_algorithm":
        return sorted(result["best"]) == sorted(expected) and result["tie"] == (len(expected) > 1)
    if kind == "first_failure":
        targets, time = expected
        first = next(event for event in result["events"] if event["event"] in {"LINK_DOWN", "ROUTER_DOWN"})
        return first["target"] == "–".join(targets) and abs(first["time_s"] - time) < 1e-6
    if kind == "flow_path":
        return result.get("path_at_time") == " → ".join(expected)
    if kind == "busiest_link":
        return result["busiest"]["channel"] == "→".join(expected)
    if kind == "drop_reason":
        reason, count = expected[:2]
        by_reason = result["by_reason"]
        top = max(by_reason, key=lambda key: by_reason[key]) if by_reason else "none"
        return top == reason and result["total"] == count
    if kind == "drop_total":
        return result["drops"] == expected
    raise ValueError(kind)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=DESCRIPTION, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", help="path to netmind_cli")
    parser.add_argument("--json", help="write the full report to this file")
    parser.add_argument("--verbose", action="store_true", help="print every answer")
    parser.add_argument("--engines", default="old,rules,agent",
                        help="comma list of old, rules, agent (agent is skipped when no LLM is configured)")
    return parser.parse_args()


def _ask_engines(engines: list[str], session: Any, run_ids: list[str], question: str, llm: Any) -> dict[str, str]:
    import netmind_api
    from graphrag.answer_rules import answer_with_rules

    answers: dict[str, str] = {}
    for engine in engines:
        try:
            if engine == "old":
                result = netmind_api._answer_graphrag_legacy(run_ids, question)
            elif engine == "rules":
                result = answer_with_rules(session, question)
            else:
                result = _agent_with_rate_limit_waits(session, question, llm)
            answers[engine] = str(result.get("answer", ""))
        except Exception as error:
            answers[engine] = f"ERROR: {error}"
    return answers


def _print_report(report: list[dict[str, Any]], engines: list[str], verbose: bool) -> None:
    width = max(len(row["id"]) for row in report)
    print(f"{'question'.ljust(width)}  " + "  ".join(engine.ljust(6) for engine in engines) + "  tools   expected")
    for row in report:
        marks = "  ".join(("pass" if row["passed"][engine] else "FAIL").ljust(6) for engine in engines)
        print(f"{row['id'].ljust(width)}  {marks}  {'pass' if row['tool_ok'] else 'FAIL'}    {row['expected']}")
        for engine in engines:
            if verbose or (engine != "old" and not row["passed"][engine]):
                print(f"{'':{width}}    {engine}: {row['answers'][engine][:220]!r}")

    total = len(report)
    engine_totals = [f"{engine}: {sum(row['passed'][engine] for row in report)}/{total}" for engine in engines]
    print("\n" + "   ".join(engine_totals) + f"   tools: {sum(row['tool_ok'] for row in report)}/{total}")


def main() -> int:
    args = _parse_args()
    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    cli = find_cli(args.cli)
    with tempfile.TemporaryDirectory(prefix="netmind_eval_") as temporary:
        data = Path(temporary)
        runs = generate_runs(spec, cli, data)

        os.environ["NETMIND_DATA_DIR"] = str(data)
        from graphrag import tools
        from graphrag.session import QASession

        engines = [engine.strip() for engine in args.engines.split(",") if engine.strip()]
        llm = None
        if "agent" in engines:
            from graphrag import llm as llm_config
            llm = llm_config.chat_model()
            if llm is None:
                print("agent skipped: no LLM configured "
                      "(set NETMIND_LLM_MODEL, NETMIND_LLM_BASE_URL, NETMIND_LLM_API_KEY)\n")
                engines.remove("agent")

        oracle = Oracle(data, runs)
        contexts = {}
        for scenario, by_algorithm in runs.items():
            for algorithm, info in by_algorithm.items():
                contexts[(scenario, algorithm)] = tools.load_run(data, info["run_id"], info["topology"])

        report = []
        for question in spec["questions"]:
            scenario, kind = question["scenario"], question["check"]["kind"]
            expected = expected_for(oracle, question)
            run_ids = [info["run_id"] for info in runs[scenario].values()]
            first_topology = next(iter(runs[scenario].values()))["topology"]
            names = {node["name"] for node in json.loads(first_topology.read_text(encoding="utf-8"))["nodes"]}
            scenario_contexts = [contexts[(scenario, algorithm)] for algorithm in runs[scenario]]

            answers = _ask_engines(engines, QASession(scenario_contexts), run_ids, question["question"], llm)
            passed = {engine: score_text(kind, expected, answer, list(runs[scenario]), names)
                      for engine, answer in answers.items()}

            call = question["tool"]
            function = getattr(tools, call["name"])
            if call["name"] == "compare_runs":
                result = function(scenario_contexts, **call["args"])
            else:
                result = function(contexts[(scenario, call["run"])], **call["args"])

            report.append({"id": question["id"], "question": question["question"], "kind": kind,
                           "expected": expected, "answers": answers, "passed": passed,
                           "tool_ok": score_tool(kind, expected, result)})

    _print_report(report, engines, args.verbose)
    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")
    return 0 if all(row["tool_ok"] for row in report) else 1


if __name__ == "__main__":
    sys.exit(main())
