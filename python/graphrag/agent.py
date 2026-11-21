from __future__ import annotations

import functools
import json
import re
from typing import Any, Callable

from langchain_core.messages import AIMessage, BaseMessage, HumanMessage, SystemMessage, ToolMessage
from langchain_core.tools import StructuredTool
from langgraph.graph import END, START, MessagesState, StateGraph
from langgraph.prebuilt import ToolNode, tools_condition

from graphrag import tools
from graphrag.answer_rules import answer_with_rules
from graphrag.session import ALGORITHM_LABELS, QASession, label

MAX_RETRIES = 1
VERIFIER = "verifier"
RECURSION_LIMIT = 16
_NUMBER = re.compile(r"(?<![\w.])-?\d+(?:,\d{3})*(?:\.\d+)?(?![\w])")
_DIGIT_GROUP = re.compile(r"(?<=\d)[   ](?=\d{3}(?!\d))")
_THINK = re.compile(r"<think>.*?</think>", re.DOTALL | re.IGNORECASE)


class AgentState(MessagesState):
    attempts: int
    verified: bool
    unverified: list[str]


def _dump(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, default=str)


def build_tools(session: QASession) -> list[StructuredTool]:

    def guarded(function: Callable[..., Any]) -> Callable[..., str]:
        @functools.wraps(function)
        def call(*args: Any, **kwargs: Any) -> str:
            try:
                return _dump(function(*args, **kwargs))
            except Exception as error:  # noqa: BLE001
                return _dump({"error": str(error)})

        return call

    def list_runs() -> Any:
        """List the runs being discussed: algorithm, run id, routing weight and seed."""
        return [{"algorithm": c.algorithm, "run_id": c.run_id, "weight": c.weight, "seed": c.seed} for c in session.contexts]

    def compare_algorithms(metric: str) -> Any:
        """Rank all runs on one metric and say which algorithm(s) were best, reporting ties.
        metric is one of: latency_ms, throughput_mbps, reliability_pct, drops, recovery_time_s, avg_hops."""
        return tools.compare_runs(session.contexts, metric)

    def run_summary(algorithm: str = "") -> Any:
        """End-of-run totals for one run (reliability, latency, throughput, drops, recovery time, per-flow stats).
        algorithm: bfs, dfs, dijkstra or bellman-ford; empty for the first run."""
        return tools.run_summary(session.run(algorithm))

    def busiest_links(algorithm: str = "") -> Any:
        """Link load for one run: busiest link directions by average utilisation, and queues that overflowed."""
        return tools.link_load(session.run(algorithm))

    def dropped_packets(algorithm: str = "", from_s: float | None = None, to_s: float | None = None) -> Any:
        """Dropped packets in one run, optionally between from_s and to_s seconds, grouped by reason and location."""
        return tools.drops(session.run(algorithm), from_s, to_s)

    def network_events(algorithm: str = "", from_s: float | None = None, to_s: float | None = None) -> Any:
        """Link and router failures, recoveries, degradations, route updates and flow start/stop, in time order."""
        result = tools.network_events(session.run(algorithm), from_s, to_s)
        result["events"] = result["events"][:40]
        return result

    def flow_path(src: str, dst: str, algorithm: str = "", at_s: float | None = None) -> Any:
        """The hop-by-hop path packets took from host src to host dst, the path at time at_s, and when it changed."""
        return tools.flow_path(session.run(algorithm), src, dst, at_s)

    def window_metrics(algorithm: str = "", from_s: float | None = None, to_s: float | None = None) -> Any:
        """Per-second metrics (latency, throughput, loss, utilisation) for one run; idle windows are flagged."""
        return tools.window_metrics(session.run(algorithm), from_s, to_s)

    def topology() -> Any:
        """The network's devices (hosts, routers, switches) and links with bandwidth and latency."""
        return tools.topology_info(session.run())

    functions = [list_runs, compare_algorithms, run_summary, busiest_links, dropped_packets,
                 network_events, flow_path, window_metrics, topology]
    return [StructuredTool.from_function(guarded(f), name=f.__name__, description=" ".join((f.__doc__ or "").split()))
            for f in functions]


def system_prompt(session: QASession) -> str:
    runs = ", ".join(f"{label(c.algorithm)} ({c.run_id})" for c in session.contexts)
    return (
        "You are NetMind's network analyst. You answer questions about network simulation runs for students.\n"
        f"Runs in this conversation: {runs}. They share the same topology, traffic and events; only the routing "
        f"algorithm differs. Devices: {', '.join(session.node_names)}.\n\n"
        "Rules:\n"
        "1. Get facts with the tools before answering. Never invent numbers, names or events.\n"
        "2. Every number you state must appear in a tool result. Do not calculate new numbers.\n"
        "3. If algorithms have equal values, say they tied; do not pick a winner.\n"
        "4. Name devices and links (for example R1–R2), never internal ids. Give times in seconds.\n"
        "5. Explain cause and effect when the data shows it, e.g. drops with reason no_route while a router was down.\n"
        "6. If the tools do not contain the answer, say so and say what is known.\n"
        "7. Reply in 2 to 5 plain sentences. No markdown, tables, lists or JSON."
    )


def _text(message: BaseMessage) -> str:
    content = message.content
    if isinstance(content, list):
        content = " ".join(part.get("text", "") if isinstance(part, dict) else str(part) for part in content)
    return _THINK.sub("", str(content)).strip()


def _ungroup(text: str) -> str:
    return _DIGIT_GROUP.sub("", text)


def _numbers(text: str) -> list[str]:
    return [n.replace(",", "") for n in _NUMBER.findall(_ungroup(text))]


def unverified_numbers(answer: str, evidence: str, names: list[str]) -> list[str]:
    cleaned = answer
    for name in sorted(names + list(ALGORITHM_LABELS.values()), key=len, reverse=True):
        cleaned = re.sub(rf"\b{re.escape(name)}\b", " ", cleaned, flags=re.IGNORECASE)
    known = [float(n) for n in _numbers(evidence)]
    missing = []
    for raw in _numbers(cleaned):
        value = float(raw)
        decimals = len(raw.split(".")[1]) if "." in raw else 0
        tolerance = 0.5 * 10 ** -decimals + 1e-9
        if decimals == 0 and abs(value) <= 10:
            continue
        if not any(abs(value - k) <= tolerance for k in known):
            missing.append(raw)
    return missing


def build_graph(llm: Any, session: QASession):
    tool_list = build_tools(session)
    model = llm.bind_tools(tool_list)
    prompt = SystemMessage(system_prompt(session))
    names = session.node_names

    def _is_verifier(message: HumanMessage) -> bool:
        return str(message.id or "").startswith(f"{VERIFIER}-")

    def agent(state: AgentState) -> dict[str, Any]:
        return {"messages": [model.invoke([prompt, *state["messages"]])]}

    def verify(state: AgentState) -> dict[str, Any]:
        messages = state["messages"]
        answer = _text(messages[-1])
        evidence = " ".join(
            _text(m) for m in messages
            if isinstance(m, ToolMessage) or (isinstance(m, HumanMessage) and not _is_verifier(m))
        )
        missing = unverified_numbers(answer, evidence, names)
        attempts = state.get("attempts", 0) + 1
        update: dict[str, Any] = {"attempts": attempts, "verified": not missing, "unverified": missing}
        if missing and attempts <= MAX_RETRIES:
            update["messages"] = [HumanMessage(
                id=f"{VERIFIER}-{attempts}",
                content=f"These numbers in your answer are not in any tool result: {', '.join(missing)}. "
                "Check with the tools and answer again using only values the tools returned.",
            )]
        return update

    def after_verify(state: AgentState) -> str:
        return END if state["verified"] or state["attempts"] > MAX_RETRIES else "agent"

    graph = StateGraph(AgentState)
    graph.add_node("agent", agent)
    graph.add_node("tools", ToolNode(tool_list))
    graph.add_node("verify", verify)
    graph.add_edge(START, "agent")
    graph.add_conditional_edges("agent", tools_condition, {"tools": "tools", END: "verify"})
    graph.add_edge("tools", "agent")
    graph.add_conditional_edges("verify", after_verify, {"agent": "agent", END: END})
    return graph.compile()


def answer_with_agent(
    session: QASession,
    question: str,
    llm: Any,
    history: list[dict[str, str]] | None = None,
) -> dict[str, Any]:
    messages: list[BaseMessage] = []
    for turn in (history or [])[-6:]:
        if turn.get("question") and turn.get("answer"):
            messages += [HumanMessage(turn["question"]), AIMessage(turn["answer"])]
    messages.append(HumanMessage(question))

    state = build_graph(llm, session).invoke(
        {"messages": messages, "attempts": 0, "verified": False, "unverified": []},
        config={"recursion_limit": RECURSION_LIMIT},
    )
    new_messages = state["messages"][len(messages):]
    calls = [
        {"name": call["name"], "args": call.get("args", {})}
        for message in new_messages if isinstance(message, AIMessage)
        for call in message.tool_calls
    ]
    if not state["verified"]:
        fallback = answer_with_rules(session, question)
        fallback["fallback_reason"] = (
            "The AI answer contained numbers that are not in the run data "
            f"({', '.join(state['unverified'])}), so the rule-based answer is shown instead."
        )
        return fallback
    return {
        "engine": "agent",
        "intent": "agent",
        "answer": _text(state["messages"][-1]),
        "tool_calls": calls,
        "verified": True,
        "attempts": state["attempts"],
    }
