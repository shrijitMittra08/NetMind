import json

import pytest
from fastapi.testclient import TestClient

import netmind_api
from ml import anomaly
from test_ml_timeseries import synthetic_windows

EVENTS = [
    {"type": "LINK_FAILURE", "a": "R1", "b": "R2", "time_s": 10},
    {"type": "LINK_RESTORE", "a": "R1", "b": "R2", "time_s": 18},
]
FAKE_PNG = bytes([0x89]) + b"PNG\r\n\x1a\nfake"


@pytest.fixture
def data_dir(tmp_path, monkeypatch):
    monkeypatch.setattr(netmind_api, "DATA_DIR", tmp_path)
    monkeypatch.setattr(netmind_api, "_ANOMALY_MODEL", {})
    monkeypatch.setattr(netmind_api, "ML_ASSETS_DIR", None)
    return tmp_path


def train(directory):
    windows = synthetic_windows(seed=7)
    results, _ = anomaly.run_experiment(windows, seed=7, models_dir=directory / "models")
    return windows, results


def write_run(data_dir, windows, events=EVENTS, run_id="run_001"):
    run = windows[windows["run_id"] == "run_001"].drop(
        columns=["run_id", "algorithm", "event_pattern", "is_anomaly"]
    )
    run.to_csv(data_dir / f"{run_id}_windows.csv", index=False)
    scenario = {"duration_s": 30.0, "events": events, "flows": [], "routing": {}}
    manifest = {"simulation_time_s": 30.0, "dataset_metadata": {"scenario_json": json.dumps(scenario)}}
    (data_dir / f"{run_id}.json").write_text(json.dumps(manifest), encoding="utf-8")


def test_run_report_marks_event_window_and_explains_flags(data_dir):
    windows, _ = train(data_dir)
    write_run(data_dir, windows)

    report = netmind_api._anomaly_for_run("run_001")

    assert report["detector"] == "z-score"
    assert report["disturbances"] == [{"start": 10.0, "end": 18.0, "label": "link failure R1-R2"}]
    rows = report["windows"]
    assert len(rows) == 30
    assert [row["t"] for row in rows if row["disturbed"]] == [float(t) for t in range(10, 18)]
    summary = report["summary"]
    assert summary["caught"] == sum(row["flagged"] and row["disturbed"] for row in rows)
    assert summary["false_alarms"] == sum(row["flagged"] and not row["disturbed"] for row in rows)
    assert summary["caught"] > 0
    assert all((row["driver"] is not None) == row["flagged"] for row in rows)
    assert summary["first_flag_delay_s"] == 0.0


def test_runs_without_events_have_no_bands(data_dir):
    windows, _ = train(data_dir)
    write_run(data_dir, windows, events=[])
    report = netmind_api._anomaly_for_run("run_001")
    assert report["disturbances"] == []
    assert report["summary"]["disturbed"] == 0
    assert report["summary"]["first_flag_delay_s"] is None


def test_missing_model_is_reported_not_raised(data_dir):
    windows = synthetic_windows(seed=7)
    write_run(data_dir, windows)
    result = netmind_api._anomaly_or_error("run_001")
    assert "not been trained" in result["error"]


def test_endpoints_validate_run_ids_and_plot_names(data_dir):
    windows, results = train(data_dir)
    write_run(data_dir, windows)
    plots = data_dir / "plots"
    plots.mkdir()
    (plots / "anomaly_results.json").write_text(json.dumps(results), encoding="utf-8")
    (plots / "anomaly_timeline.png").write_bytes(FAKE_PNG)
    client = TestClient(netmind_api.app)

    assert client.post("/api/ml/anomaly", json={"run_id": "run_001"}).json()["run_id"] == "run_001"
    assert client.post("/api/ml/anomaly", json={"run_id": "../secret"}).status_code == 400

    evaluation = client.get("/api/ml/anomaly/evaluation").json()
    assert "features" not in evaluation["results"]
    assert evaluation["results"]["primary"] == "zscore"
    assert [plot["name"] for plot in evaluation["plots"]] == ["anomaly_timeline.png"]

    image = client.get("/api/ml/plots/anomaly_timeline.png")
    assert image.status_code == 200 and image.headers["content-type"] == "image/png"
    assert client.get("/api/ml/plots/anomaly_results.json").status_code == 400
    missing = client.get("/api/ml/plots/s2_anomaly_flags.png")
    assert missing.status_code == 400 and "has not been generated" in missing.json()["error"]


def test_bundled_assets_are_used_when_the_data_volume_lacks_them(tmp_path, monkeypatch):
    data = tmp_path / "data"
    assets = tmp_path / "assets"
    data.mkdir()
    monkeypatch.setattr(netmind_api, "DATA_DIR", data)
    monkeypatch.setattr(netmind_api, "ML_ASSETS_DIR", assets)
    monkeypatch.setattr(netmind_api, "_ANOMALY_MODEL", {})
    windows, results = train(assets)
    write_run(data, windows)
    (assets / "plots").mkdir()
    (assets / "plots" / "anomaly_results.json").write_text(json.dumps(results), encoding="utf-8")
    (assets / "plots" / "anomaly_f1_comparison.png").write_bytes(FAKE_PNG)

    assert netmind_api._anomaly_for_run("run_001")["summary"]["windows"] == 30
    evaluation = netmind_api._anomaly_evaluation()
    assert [plot["name"] for plot in evaluation["plots"]] == ["anomaly_f1_comparison.png"]

    (data / "plots").mkdir()
    (data / "plots" / "anomaly_f1_comparison.png").write_bytes(b"local")
    assert netmind_api._ml_file("plots", "anomaly_f1_comparison.png") == data / "plots" / "anomaly_f1_comparison.png"
