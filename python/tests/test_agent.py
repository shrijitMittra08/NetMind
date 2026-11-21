import json
from typing import Any

import pytest
from langchain_core.language_models.chat_models import BaseChatModel
from langchain_core.messages import AIMessage
from langchain_core.outputs import ChatGeneration, ChatResult

from graphrag import tools
from graphrag.agent import answer_with_agent
from graphrag.session import QASession
from test_tools import TOPOLOGY, make_run


class ScriptedModel(BaseChatModel):

    replies: list[AIMessage]
    seen: list[Any] = []

    @property
    def _llm_type(self) -> str:
        return "scripted"

    def bind_tools(self, tool_list, **kwargs):
        return self

    def _generate(self, messages, stop=None, run_manager=None, **kwargs):
        self.seen.append(list(messages))
        return ChatResult(generations=[ChatGeneration(message=self.replies.pop(0))])


def call(name, args, call_id="c1"):
    return AIMessage(content="", tool_calls=[{"name": name, "args": args, "id": call_id}])


@pytest.fixture
def session(tmp_path):
    topology = tmp_path / "topology.json"
    topology.write_text(json.dumps(TOPOLOGY), encoding="utf-8")
    contexts = []
    for run_id, algorithm, latency in [("run_001", "bfs", 5.0), ("run_002", "dfs", 7.0)]:
        make_run(tmp_path, run_id, algorithm, latency)
        contexts.append(tools.load_run(tmp_path, run_id, topology))
    return QASession(contexts)


def test_agent_answers_from_tool_results(session):
    model = ScriptedModel(replies=[
        call("compare_algorithms", {"metric": "latency_ms"}),
        AIMessage(content="BFS was faster, with 5.0 ms average latency against 7.0 ms for DFS."),
    ])
    result = answer_with_agent(session, "Which algorithm was fastest?", model)
    assert result["engine"] == "agent" and result["verified"] is True
    assert result["tool_calls"] == [{"name": "compare_algorithms", "args": {"metric": "latency_ms"}}]
    tool_result = model.seen[1][-1].content
    assert '"best": ["bfs"]' in tool_result


def test_invented_number_is_sent_back_once_then_accepted_when_fixed(session):
    model = ScriptedModel(replies=[
        call("run_summary", {"algorithm": "bfs"}),
        AIMessage(content="BFS dropped 42 packets."),
        AIMessage(content="BFS dropped 2 packets and delivered 60% of them."),
    ])
    result = answer_with_agent(session, "How did BFS do?", model)
    assert result["engine"] == "agent" and result["attempts"] == 2
    feedback = model.seen[2][-1]
    assert feedback.id.startswith("verifier-") and "42" in feedback.content
    from langchain_openai.chat_models.base import _convert_message_to_dict
    assert all("name" not in _convert_message_to_dict(m) for sent in model.seen for m in sent)


def test_repeated_invention_falls_back_to_rules(session):
    model = ScriptedModel(replies=[
        call("run_summary", {"algorithm": "bfs"}),
        AIMessage(content="BFS dropped 42 packets."),
        AIMessage(content="Sorry, BFS dropped 42 packets."),
    ])
    result = answer_with_agent(session, "How many packets were dropped in total?", model)
    assert result["engine"] == "rules"
    assert "42" in result["fallback_reason"]
    assert "2 packets were dropped" in result["answer"]


def test_reasoning_blocks_are_removed_and_history_is_passed(session):
    model = ScriptedModel(replies=[
        call("network_events", {"algorithm": "bfs"}),
        AIMessage(content="<think>link 2 is R1–R2</think>The R1–R2 link went down at 1 s and recovered at 3 s."),
    ])
    history = [{"question": "Which run is this?", "answer": "BFS and DFS."}]
    result = answer_with_agent(session, "What failed?", model, history=history)
    assert result["answer"] == "The R1–R2 link went down at 1 s and recovered at 3 s."
    first_call = model.seen[0]
    assert [m.content for m in first_call[1:3]] == ["Which run is this?", "BFS and DFS."]


def test_tool_errors_reach_the_model_instead_of_crashing(session):
    model = ScriptedModel(replies=[
        call("flow_path", {"src": "PC9", "dst": "PC2"}),
        AIMessage(content="There is no flow from PC9 to PC2 in these runs."),
    ])
    result = answer_with_agent(session, "Path from PC9?", model)
    assert result["engine"] == "agent"
    assert "No flow from PC9 to PC2" in model.seen[1][-1].content


def test_numbers_grouped_with_unicode_spaces_are_checked_whole():
    from graphrag.agent import unverified_numbers

    evidence = "total_drops 2004"
    assert unverified_numbers("2 004 packets were dropped.", evidence, []) == []
    assert unverified_numbers("2 005 packets were dropped.", evidence, []) == ["2005"]
