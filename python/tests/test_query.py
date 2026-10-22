import networkx as nx

from graphrag.query import answer_question


def make_run(run_id, algorithm, latency, throughput=10.0, loss=0.0):
    graph = nx.MultiDiGraph()
    graph.graph["run_id"] = run_id
    graph.add_node(
        f"run:{run_id}",
        node_type="Run",
        id=run_id,
        algo=algorithm,
    )
    graph.add_node(
        f"window:{run_id}:0",
        node_type="Window",
        t_start=1.0,
        avg_latency_ms=latency,
        throughput_mbps=throughput,
        loss_pct=loss,
    )
    return graph


def test_answers_drop_question_with_nearby_fact_citation():
    graph = make_run("run_001", "dijkstra", 8.0)
    facts = {
        "run_001": [
            {
                "id": "F001",
                "type": "failure_impact",
                "time": 20.0,
                "text": "LINK_DOWN on R1-R2; 14 matching link_down drops recorded.",
                "evidence": {
                    "start_s": 20.0,
                    "drop_count": 14,
                    "latency_before_ms": None,
                    "latency_after_ms": None,
                },
            },
            {
                "id": "F002",
                "type": "failure_impact",
                "time": 40.0,
                "text": "LINK_DOWN on R3-R4; 2 matching link_down drops recorded.",
                "evidence": {
                    "start_s": 40.0,
                    "drop_count": 2,
                    "latency_before_ms": None,
                    "latency_after_ms": None,
                },
            },
        ]
    }

    result = answer_question(
        graph,
        facts,
        "Why did packets drop at 20 seconds?",
    )

    assert result["intent"] == "drops_at_time"
    assert "14 matching link_down drops" in result["answer"]
    assert result["citations"] == ["[run_001:F001]"]
    assert "[run_001:F002]" not in result["answer"]


def test_identifies_link_with_highest_queue_saturation():
    graph = make_run("run_001", "dijkstra", 8.0)
    graph.add_node("link:0", node_type="Link", name="R1-R2")
    graph.add_node("link:1", node_type="Link", name="R2-R3")

    facts = {
        "run_001": [
            {
                "id": "F001",
                "type": "congestion",
                "time": 10.0,
                "text": "R1-R2 reached 50% queue capacity.",
                "evidence": {
                    "link": "link:0",
                    "max_queue": 5,
                    "queue_capacity": 10,
                    "queue_full_drop_count": 0,
                },
            },
            {
                "id": "F002",
                "type": "congestion",
                "time": 12.0,
                "text": "R2-R3 reached full queue capacity.",
                "evidence": {
                    "link": "link:1",
                    "max_queue": 10,
                    "queue_capacity": 10,
                    "queue_full_drop_count": 3,
                },
            },
        ]
    }

    result = answer_question(
        graph,
        facts,
        "Which link was most congested?",
    )

    assert result["intent"] == "most_congested_link"
    assert "R2-R3" in result["answer"]
    assert "100.0% of capacity" in result["answer"]
    assert result["citations"] == ["[run_001:F002]"]


def test_compares_algorithms_by_mean_window_latency():
    graphs = [
        make_run("run_bfs", "bfs", latency=5.0),
        make_run("run_dijkstra", "dijkstra", latency=8.0),
    ]

    result = answer_question(
        graphs,
        {},
        "Which routing algorithm performed best?",
    )

    assert result["intent"] == "best_algorithm"
    assert "bfs had the lowest mean window latency" in result["answer"]
    assert "lowest average latency" in result["answer"]


def test_returns_observed_flow_path():
    graph = nx.MultiDiGraph()
    graph.graph["run_id"] = "run_001"
    graph.add_node(
        "flow:run_001:1",
        node_type="Flow",
        id="1",
        src="1",
        dst="3",
        src_name="PC1",
        dst_name="PC2",
    )
    graph.add_node("device:r1", node_type="Device", name="R1")
    graph.add_node("device:r2", node_type="Device", name="R2")
    graph.add_edge(
        "flow:run_001:1",
        "device:r1",
        relation="ROUTED_VIA",
        time_s=1.0,
        packet_id="100",
    )
    graph.add_edge(
        "flow:run_001:1",
        "device:r2",
        relation="ROUTED_VIA",
        time_s=2.0,
        packet_id="100",
    )

    result = answer_question(
        graph,
        {},
        "What path does PC1 to PC2 use?",
    )

    assert result["intent"] == "flow_path"
    assert "PC1 -> R1 -> R2 -> PC2" in result["answer"]


def test_unknown_question_searches_stored_facts():
    graph = make_run("run_001", "dijkstra", 8.0)
    facts = {
        "run_001": [{
            "id": "F004",
            "type": "failure_impact",
            "time": 20.0,
            "text": "The router failure caused a temporary route change.",
            "evidence": {},
        }]
    }

    result = answer_question(
        graph,
        facts,
        "Tell me about the router failure",
    )

    assert result["intent"] == "fact_search"
    assert "temporary route change" in result["answer"]
    assert result["citations"] == ["[run_001:F004]"]