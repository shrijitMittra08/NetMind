import networkx as nx
import pytest

from graphrag.facts import extract_facts


def add_event(graph, key, event_type, time_s, target):
    graph.add_node(
        key,
        node_type="Event",
        type=event_type,
        time=time_s,
        target=target,
    )


def test_failure_fact_cites_only_matching_drops_and_compares_packet_paths():
    graph = nx.MultiDiGraph()
    graph.add_node("device:r1", node_type="Device", name="R1")
    graph.add_node("device:r2", node_type="Device", name="R2")
    graph.add_node("device:r3", node_type="Device", name="R3")
    graph.add_node(
        "flow:run_001:1",
        node_type="Flow",
        id="1",
        src_name="PC1",
        dst_name="PC2",
    )
    graph.add_node("run:run_001", node_type="Run", id="run_001")
    graph.add_node(
        "window:before",
        node_type="Window",
        t_start=1.0,
        avg_latency_ms=5.0,
    )
    graph.add_node(
        "window:after",
        node_type="Window",
        t_start=4.0,
        avg_latency_ms=13.0,
    )
    add_event(graph, "event:down", "LINK_DOWN", 2.0, "R1-R2")
    add_event(graph, "event:up", "LINK_UP", 4.0, "R1-R2")

    flow = "flow:run_001:1"

                                         
    graph.add_edge(
        flow,
        "device:r1",
        relation="ROUTED_VIA",
        time_s=1.0,
        packet_id="10",
    )

                                                                       
    graph.add_edge(
        flow,
        "device:r3",
        relation="ROUTED_VIA",
        time_s=4.1,
        packet_id="11",
    )

    trace_rows = [
        {
            "time_s": "2.2",
            "event_type": "DROPPED",
            "flow_id": "1",
            "location": "R1>R2",
            "detail": "reason=link_down",
        },
        {
            "time_s": "2.3",
            "event_type": "DROPPED",
            "flow_id": "1",
            "location": "X>Y",
            "detail": "reason=link_down",
        },
        {
            "time_s": "4.2",
            "event_type": "DROPPED",
            "flow_id": "1",
            "location": "R1-R2",
            "detail": "reason=link_down",
        },
    ]

    facts = extract_facts(graph, trace_rows)
    failure_fact = next(
        fact for fact in facts if fact["type"] == "failure_impact"
    )

    assert failure_fact["evidence"]["drop_count"] == 1
    assert failure_fact["evidence"]["drop_times_s"] == [2.2]
    assert failure_fact["evidence"]["changed_flows"] == [{
        "name": "PC1->PC2",
        "before": ["R1"],
        "after": ["R3"],
    }]
    assert failure_fact["evidence"]["latency_before_ms"] == 5.0
    assert failure_fact["evidence"]["latency_after_ms"] == 13.0
    assert "8.000 ms" in failure_fact["text"]
    assert "1 matching link_down drop(s)" in failure_fact["text"]


def test_congestion_fact_limits_drops_to_target_and_congestion_interval():
    graph = nx.MultiDiGraph()
    graph.add_node("device:a", node_type="Device", name="A")
    graph.add_node("device:b", node_type="Device", name="B")
    graph.add_node(
        "link:0",
        node_type="Link",
        name="A-B",
        queue_capacity=4,
        max_queue=4,
    )
    graph.add_node(
        "flow:run_001:1",
        node_type="Flow",
        id="1",
        src_name="PC1",
        dst_name="PC2",
    )
    graph.add_node(
        "event:congestion",
        node_type="Event",
        type="CONGESTION",
        time=1.0,
        target="A-B",
    )
    add_event(graph, "event:congestion_end", "CONGESTION_END", 1.5, "A-B")

    graph.add_edge("device:a", "link:0", relation="CONNECTED_TO")
    graph.add_edge("link:0", "device:a", relation="CONNECTED_TO")
    graph.add_edge("device:b", "link:0", relation="CONNECTED_TO")
    graph.add_edge("link:0", "device:b", relation="CONNECTED_TO")
    graph.add_edge("event:congestion", "link:0", relation="TARGETS")

    trace_rows = [
        {
            "time_s": "1.1",
            "event_type": "ENQUEUE",
            "flow_id": "1",
            "location": "A>B",
            "detail": "qlen=4",
        },
        {
            "time_s": "1.2",
            "event_type": "DROPPED",
            "flow_id": "1",
            "location": "A>B",
            "detail": "reason=queue_full",
        },
        {
            "time_s": "1.3",
            "event_type": "DROPPED",
            "flow_id": "1",
            "location": "X>Y",
            "detail": "reason=queue_full",
        },
        {
            "time_s": "1.6",
            "event_type": "DROPPED",
            "flow_id": "1",
            "location": "A>B",
            "detail": "reason=queue_full",
        },
    ]

    facts = extract_facts(graph, trace_rows)
    congestion_fact = next(
        fact for fact in facts if fact["type"] == "congestion"
    )

    assert congestion_fact["evidence"]["max_queue"] == 4
    assert congestion_fact["evidence"]["queue_full_drop_count"] == 1
    assert congestion_fact["evidence"]["crossing_flows"] == ["PC1->PC2"]
    assert congestion_fact["evidence"]["end_s"] == 1.5
    assert "against capacity 4" in congestion_fact["text"]


def test_anomaly_fact_cites_nearest_preceding_event():
    graph = nx.MultiDiGraph()
    graph.add_node(
        "window:run_001:0",
        node_type="Window",
        t_start=5.0,
        avg_latency_ms=12.0,
    )
    graph.add_node(
        "anomaly:run_001:0",
        node_type="Anomaly",
        window="window:run_001:0",
        score=0.9,
    )
    add_event(graph, "event:old", "LINK_DOWN", 2.0, "A-B")
    add_event(graph, "event:near", "ROUTER_DOWN", 4.5, "R1")
    graph.add_edge(
        "anomaly:run_001:0",
        "window:run_001:0",
        relation="FLAGGED",
    )

    facts = extract_facts(graph, [], anomaly_lookback_seconds=1.0)
    anomaly_facts = [
        fact for fact in facts if fact["type"] == "anomaly_context"
    ]

    assert len(anomaly_facts) == 1
    assert anomaly_facts[0]["evidence"]["nearest_event"] == "event:near"
    assert "ROUTER_DOWN on R1" in anomaly_facts[0]["text"]


def test_negative_anomaly_lookback_is_rejected():
    with pytest.raises(ValueError, match="finite and non-negative"):
        extract_facts(nx.MultiDiGraph(), [], anomaly_lookback_seconds=-1)