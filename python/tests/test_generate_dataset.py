import json

from ml.generate_dataset import (
    ALGORITHMS,
    RunTask,
    build_scenario,
    build_tasks,
    load_topology,
)


def write_topology(path):
    path.write_text(
        json.dumps(
            {
                "nodes": [
                    {"id": 1, "type": "HOST", "name": "PC1", "ip": "10.0.0.1"},
                    {"id": 2, "type": "ROUTER", "name": "R1", "ip": "10.0.0.2"},
                    {"id": 3, "type": "HOST", "name": "PC2", "ip": "10.0.0.3"},
                ],
                "links": [
                    {"a": 1, "b": 2, "bandwidthMbps": 100, "latencyMs": 1},
                    {"a": 2, "b": 3, "bandwidthMbps": 50, "latencyMs": 2},
                ],
            }
        ),
        encoding="utf-8",
    )


def make_task(topology, event_pattern="link_failure"):
    return RunTask(
        run_id="run_0001",
        topology=topology,
        algorithm="dijkstra",
        load_pct=80,
        event_pattern=event_pattern,
        seed=17,
        duration_s=10,
    )


def test_scenario_uses_topology_algorithm_load_seed_and_failure_events(tmp_path):
    topology_path = tmp_path / "topology.json"
    write_topology(topology_path)
    topology = load_topology(topology_path)

    scenario = build_scenario(make_task(topology))

    assert scenario["topology"] == str(topology_path.resolve())
    assert scenario["routing"]["algorithm"] == "dijkstra"
    assert scenario["seed"] == 17
    first = scenario["flows"][0]
    assert {first["src"], first["dst"]} == {"PC1", "PC2"}
    assert first["rate_mbps"] == 40
    failure = scenario["events"][0]
    assert failure["type"] == "LINK_FAILURE"
    assert {failure["a"], failure["b"]} in ({"PC1", "R1"}, {"R1", "PC2"})
    assert 0 < failure["time_s"] < scenario["duration_s"]
    metadata = scenario["dataset_metadata"]
    assert metadata["event_pattern"] == "link_failure"
    assert json.loads(metadata["scenario_json"])["flows"] == scenario["flows"]


def test_scenarios_are_reproducible(tmp_path):
    topology_path = tmp_path / "topology.json"
    write_topology(topology_path)
    topology = load_topology(topology_path)

    assert build_scenario(make_task(topology, "congestion")) == build_scenario(make_task(topology, "congestion"))


def test_burst_scenario_adds_a_new_flow_between_valid_hosts(tmp_path):
    topology_path = tmp_path / "topology.json"
    write_topology(topology_path)
    topology = load_topology(topology_path)

    scenario = build_scenario(make_task(topology, "burst"))

    event = scenario["events"][0]
    assert event["type"] == "TRAFFIC_BURST"
    assert event["flow_id"] not in {flow["id"] for flow in scenario["flows"]}
    assert {event["src"], event["dst"]} == {"PC1", "PC2"}
    assert event["time_s"] < scenario["duration_s"]


def test_task_sweep_includes_all_algorithms_and_is_reproducible(tmp_path):
    topology_path = tmp_path / "topology.json"
    write_topology(topology_path)
    topology = load_topology(topology_path)

    first = build_tasks([topology], [1], 10, 20, random_state=9)
    second = build_tasks([topology], [1], 10, 20, random_state=9)

    assert first == second
    assert len(first) == 20
    assert {task.algorithm for task in first} == set(ALGORITHMS)