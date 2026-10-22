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
    assert scenario["flows"][0]["rate_mbps"] == 40
    assert scenario["events"] == [
        {"type": "LINK_FAILURE", "a": "R1", "b": "PC2", "time_s": 4.0},
        {"type": "LINK_RESTORE", "a": "R1", "b": "PC2", "time_s": 6.5},
    ]
    assert scenario["dataset_metadata"]["event_pattern"] == "link_failure"


def test_burst_scenario_targets_flow_one_and_uses_valid_hosts(tmp_path):
    topology_path = tmp_path / "topology.json"
    write_topology(topology_path)
    topology = load_topology(topology_path)

    scenario = build_scenario(make_task(topology, "burst"))

    event = scenario["events"][0]
    assert event["type"] == "TRAFFIC_BURST"
    assert event["flow_id"] != scenario["flows"][0]["id"]
    assert event["src"] == "PC1"
    assert event["dst"] == "PC2"
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