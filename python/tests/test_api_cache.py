import json
import os

import netmind_api
from test_tools import TOPOLOGY, make_run


def write_run(directory, latency, topology_path):
    make_run(directory, "run_001", "bfs", latency)
    manifest = directory / "run_001.json"
    manifest.write_text(
        json.dumps({"algorithm": "bfs", "weight": "latency", "seed": 1, "topology": str(topology_path)}),
        encoding="utf-8",
    )
    return manifest


def test_reused_run_id_is_reloaded_not_served_from_cache(tmp_path, monkeypatch):
    monkeypatch.setattr(netmind_api, "DATA_DIR", tmp_path)
    monkeypatch.setattr(netmind_api, "_QA_CACHE", {})
    topology = tmp_path / "topology.json"
    topology.write_text(json.dumps(TOPOLOGY), encoding="utf-8")

    write_run(tmp_path, 5.0, topology)
    first = netmind_api._qa_session(["run_001"]).contexts[0]
    assert first.network["avg_latency_ms"] == 5.0

    manifest = write_run(tmp_path, 9.0, topology)
    stat = manifest.stat()
    os.utime(manifest, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000_000))
    second = netmind_api._qa_session(["run_001"]).contexts[0]
    assert second.network["avg_latency_ms"] == 9.0


def test_script_runs_finish_without_a_reconvergence_setting(tmp_path, monkeypatch):
    monkeypatch.setattr(netmind_api, "DATA_DIR", tmp_path)
    seen = {}
    monkeypatch.setattr(netmind_api, "_baseline_for_new_run", lambda row: seen.setdefault("row", row) and {"metrics": []})
    topology = tmp_path / "topology.json"
    topology.write_text(json.dumps(TOPOLOGY), encoding="utf-8")
    make_run(tmp_path, "run_001", "bfs", 5.0)
    flows = [{"src": "PC1", "dst": "PC2", "rate_mbps": 1, "packet_bytes": 1000}]
    fields = {"events": [], "flows": flows, "weight": "latency", "seed": 1, "duration": 10}

    result = netmind_api._finish_run("run_001", fields, "bfs", topology)

    assert result["run_id"] == "run_001"
    assert json.loads(seen["row"]["scenario_json"])["routing"]["reconvergence_delay_ms"] == 0
    no_flows = netmind_api._finish_run("run_001", {**fields, "flows": []}, "bfs", topology)
    assert "error" in no_flows["baseline"]
