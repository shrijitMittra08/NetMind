from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import pandas as pd

from common.io import read_run

PACKET_EVENTS = {"SENT", "ENQUEUE", "TX_START", "ARRIVE", "FORWARD", "DELIVERED", "DROPPED"}

TIE_TOLERANCE = 0.001

METRICS: dict[str, dict[str, str]] = {
    "latency_ms": {"column": "avg_latency_ms", "better": "low", "label": "average latency", "unit": "ms"},
    "throughput_mbps": {"column": "avg_throughput_mbps", "better": "high", "label": "average throughput", "unit": "Mbps"},
    "reliability_pct": {"column": "reliability_pct", "better": "high", "label": "reliability", "unit": "%"},
    "drops": {"column": "total_drops", "better": "low", "label": "dropped packets", "unit": "packets"},
    "recovery_time_s": {"column": "recovery_time_s", "better": "low", "label": "recovery time", "unit": "s"},
    "avg_hops": {"column": "", "better": "low", "label": "average path length", "unit": "hops"},
}


def _num(value: Any, digits: int = 4) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return round(number, digits) if math.isfinite(number) else None


def _int(value: Any) -> int:
    return int(value or 0)


@dataclass
class RunContext:
    run_id: str
    algorithm: str
    weight: str
    seed: Any
    topology: dict[str, Any]
    trace: pd.DataFrame
    flows: pd.DataFrame
    links: pd.DataFrame
    windows: pd.DataFrame
    network: dict[str, Any]
    node_names: dict[str, str] = field(default_factory=dict)
    link_names: dict[str, str] = field(default_factory=dict)

    def node(self, node_id: Any) -> str:
        return self.node_names.get(str(node_id), str(node_id))

    def link(self, link_id: Any) -> str:
        return self.link_names.get(str(link_id), f"link {link_id}")


def load_run(run_dir: str | Path, run_id: str, topology_path: str | Path) -> RunContext:
    run = read_run(run_dir, run_id)
    topology = json.loads(Path(topology_path).read_text(encoding="utf-8-sig"))
    node_names = {str(node["id"]): str(node["name"]) for node in topology.get("nodes", [])}
    link_names = {}
    for index, link in enumerate(topology.get("links", []), start=1):
        end_a = node_names.get(str(link["a"]), link["a"])
        end_b = node_names.get(str(link["b"]), link["b"])
        link_names[str(index)] = f"{end_a}–{end_b}"
    return RunContext(
        run_id=run_id,
        algorithm=str(run.manifest.get("algorithm", "unknown")),
        weight=str(run.manifest.get("weight", "unknown")),
        seed=run.manifest.get("seed"),
        topology=topology,
        trace=run.trace,
        flows=run.flows,
        links=run.links,
        windows=run.windows,
        network=run.network.iloc[0].to_dict() if not run.network.empty else {},
        node_names=node_names,
        link_names=link_names,
    )


def _flow_rows(ctx: RunContext) -> list[dict[str, Any]]:
    return [{
        "flow_id": int(record["flow_id"]),
        "src": ctx.node(record["src"]),
        "dst": ctx.node(record["dst"]),
        "sent": int(record["sent"]),
        "delivered": int(record["delivered"]),
        "dropped": int(record["dropped"]),
        "loss_pct": _num(record.get("loss_pct")),
        "avg_latency_ms": _num(record.get("avg_latency_ms")),
        "avg_hops": _num(record.get("avg_hops")),
    } for record in ctx.flows.to_dict(orient="records")]


def _avg_hops(ctx: RunContext) -> float | None:
    flows = ctx.flows
    if flows.empty or "avg_hops" not in flows or flows["delivered"].sum() <= 0:
        return None
    weights = flows["delivered"].astype(float)
    return _num((flows["avg_hops"].astype(float) * weights).sum() / weights.sum())


def run_summary(ctx: RunContext) -> dict[str, Any]:
    net = ctx.network
    return {
        "run_id": ctx.run_id,
        "algorithm": ctx.algorithm,
        "weight": ctx.weight,
        "seed": ctx.seed,
        "duration_s": _num(ctx.trace["time_s"].max()) if not ctx.trace.empty else None,
        "reliability_pct": _num(net.get("reliability_pct")),
        "throughput_mbps": _num(net.get("avg_throughput_mbps")),
        "latency_ms": _num(net.get("avg_latency_ms")),
        "drops": _int(net.get("total_drops", 0)),
        "recovery_time_s": _num(net.get("recovery_time_s")),
        "avg_hops": _avg_hops(ctx),
        "flows": _flow_rows(ctx),
    }


def compare_runs(contexts: list[RunContext], metric: str = "latency_ms") -> dict[str, Any]:
    if metric not in METRICS:
        raise ValueError(f"Unknown metric {metric!r}; choose one of {sorted(METRICS)}")
    spec = METRICS[metric]
    values = []
    for ctx in contexts:
        value = _avg_hops(ctx) if metric == "avg_hops" else _num(ctx.network.get(spec["column"]))
        values.append({"run_id": ctx.run_id, "algorithm": ctx.algorithm, "value": value})

    known = [item["value"] for item in values if item["value"] is not None]
    if not known:
        return {"metric": metric, "label": spec["label"], "unit": spec["unit"], "values": values, "best": [], "tie": False}

    best_value = min(known) if spec["better"] == "low" else max(known)
    allowed_gap = TIE_TOLERANCE * max(abs(best_value), 1e-9)
    best = [item["algorithm"] for item in values
            if item["value"] is not None and abs(item["value"] - best_value) <= allowed_gap]
    return {
        "metric": metric,
        "label": spec["label"],
        "unit": spec["unit"],
        "better": "lower" if spec["better"] == "low" else "higher",
        "values": values,
        "best": best,
        "tie": len(best) > 1,
        "all_equal": len(best) == len(known) and len(known) > 1,
    }


def _link_row(record: dict[str, Any]) -> dict[str, Any]:
    a, _, b = str(record["channel"]).partition(">")
    return {
        "channel": f"{a}→{b}",
        "link": "–".join(sorted([a, b])),
        "utilization_pct": _num(record.get("utilization_pct"), 3),
        "avg_queue": _num(record.get("avg_queue"), 3),
        "max_queue": _int(record.get("max_queue", 0)),
        "drops_queue_full": _int(record.get("drops_queue", 0)),
        "drops_random_loss": _int(record.get("drops_loss", 0)),
        "drops_link_down": _int(record.get("drops_down", 0)),
        "downtime_s": _num(record.get("downtime_s"), 3),
    }


def link_load(ctx: RunContext, top: int = 5) -> dict[str, Any]:
    rows = [_link_row(record) for record in ctx.links.to_dict(orient="records")]
    rows.sort(key=lambda row: (row["utilization_pct"] or 0, row["drops_queue_full"]), reverse=True)
    overflowing = [row for row in rows if row["drops_queue_full"] > 0]
    overflowing.sort(key=lambda row: row["drops_queue_full"], reverse=True)
    return {
        "run_id": ctx.run_id,
        "busiest": rows[0] if rows else None,
        "top": rows[:top],
        "queue_overflows": overflowing[:top],
        "note": "Utilization is averaged over the whole run for each direction of a link.",
    }


def _window(frame: pd.DataFrame, t_from: float | None, t_to: float | None, column: str = "time_s") -> pd.DataFrame:
    if t_from is not None:
        frame = frame[frame[column] >= t_from]
    if t_to is not None:
        frame = frame[frame[column] <= t_to]
    return frame


def _drop_reason(detail: Any) -> str:
    reason = "unknown"
    for part in str(detail or "").split(";"):
        key, _, value = part.partition("=")
        if key.strip() == "reason" and value:
            reason = value.strip()
    return reason


def drops(ctx: RunContext, t_from: float | None = None, t_to: float | None = None) -> dict[str, Any]:
    rows = _window(ctx.trace[ctx.trace["event_type"] == "DROPPED"], t_from, t_to)
    reasons = rows["detail"].map(_drop_reason)

    def location_name(location: str) -> str:
        if ">" in location:
            return location.replace(">", "→", 1)
        return ctx.node(location)

    locations = rows["location"].astype(str).map(location_name)
    grouped = (
        pd.DataFrame({"location": locations, "reason": reasons})
        .value_counts()
        .reset_index(name="count")
        .to_dict(orient="records")
    )
    return {
        "run_id": ctx.run_id,
        "from_s": t_from,
        "to_s": t_to,
        "total": int(len(rows)),
        "by_reason": {str(k): int(v) for k, v in reasons.value_counts().items()},
        "by_location": [{**item, "count": int(item["count"])} for item in grouped[:10]],
        "first_s": _num(rows["time_s"].min()) if len(rows) else None,
        "last_s": _num(rows["time_s"].max()) if len(rows) else None,
    }


def network_events(ctx: RunContext, t_from: float | None = None, t_to: float | None = None) -> dict[str, Any]:
    rows = _window(ctx.trace[~ctx.trace["event_type"].isin(PACKET_EVENTS)], t_from, t_to)
    events = []
    for record in rows.to_dict(orient="records"):
        kind = str(record["event_type"])
        target = str(record["location"])
        if kind.startswith("LINK_"):
            target = ctx.link(target)
        elif kind.startswith("ROUTER_"):
            target = ctx.node(target)
        events.append({"time_s": _num(record["time_s"]), "event": kind, "target": target, "detail": str(record["detail"])})
    return {"run_id": ctx.run_id, "from_s": t_from, "to_s": t_to, "events": events[:200], "count": len(events)}


def _path_text(channels: pd.Series) -> str:
    devices = [str(channels.iloc[0]).split(">")[0]]
    devices += [str(channel).split(">")[1] for channel in channels]
    return " → ".join(devices)


def _delivered_paths(ctx: RunContext, flow_id: int) -> pd.DataFrame:
    flow_rows = ctx.trace[ctx.trace["flow_id"] == flow_id]
    delivered = set(flow_rows.loc[flow_rows["event_type"] == "DELIVERED", "packet_id"].dropna().astype(int))
    hops = flow_rows[(flow_rows["event_type"] == "TX_START") & flow_rows["packet_id"].isin(delivered)]
    if hops.empty:
        return pd.DataFrame(columns=["packet_id", "sent_s", "path"])
    grouped = hops.sort_values(["packet_id", "time_s"]).groupby("packet_id")
    paths = grouped["location"].agg(_path_text)
    return pd.DataFrame({"sent_s": grouped["time_s"].min(), "path": paths}).reset_index().sort_values("sent_s")


def flow_path(ctx: RunContext, src: str, dst: str, at_time: float | None = None) -> dict[str, Any]:
    flows = _flow_rows(ctx)
    match = next((flow for flow in flows if flow["src"] == src and flow["dst"] == dst), None)
    if match is None:
        return {"run_id": ctx.run_id, "error": f"No flow from {src} to {dst} in this run.",
                "flows": [f"{f['src']}→{f['dst']}" for f in flows]}

    paths = _delivered_paths(ctx, match["flow_id"])
    if paths.empty:
        return {"run_id": ctx.run_id, "flow": f"{src}→{dst}", "error": "No packet of this flow was delivered."}

    periods = []
    for record in paths.to_dict(orient="records"):
        if periods and periods[-1]["path"] == record["path"]:
            periods[-1]["until_s"] = _num(record["sent_s"])
            periods[-1]["packets"] += 1
        else:
            periods.append({"path": record["path"], "from_s": _num(record["sent_s"]),
                            "until_s": _num(record["sent_s"]), "packets": 1})

    current = None
    if at_time is not None:
        later = paths[paths["sent_s"] >= at_time]
        current = (later.iloc[0] if not later.empty else paths.iloc[-1])["path"]
    return {
        "run_id": ctx.run_id,
        "algorithm": ctx.algorithm,
        "flow": f"{src}→{dst}",
        "path_at_time": current,
        "at_time_s": at_time,
        "path_periods": periods[:20],
        "path_changes": max(0, len(periods) - 1),
    }


def window_metrics(ctx: RunContext, t_from: float | None = None, t_to: float | None = None) -> dict[str, Any]:
    rows = []
    for record in _window(ctx.windows, t_from, t_to, "t_start").to_dict(orient="records"):
        delivered = _int(record.get("delivered", 0))
        rows.append({
            "t_start_s": _num(record["t_start"]),
            "delivered": delivered,
            "dropped": _int(record.get("dropped", 0)),
            "loss_pct": _num(record.get("loss_pct")),
            "avg_latency_ms": _num(record.get("avg_latency_ms")) if delivered > 0 else None,
            "throughput_mbps": _num(record.get("throughput_mbps")),
            "max_util_pct": _num(record.get("max_util_pct")),
            "idle": _int(record.get("sent", 0)) == 0,
        })
    return {"run_id": ctx.run_id, "windows": rows}


def topology_info(ctx: RunContext) -> dict[str, Any]:
    nodes = [{"name": str(node["name"]), "type": str(node["type"]), "ip": str(node.get("ip", ""))}
             for node in ctx.topology.get("nodes", [])]
    links = [{
        "link": ctx.link(index),
        "bandwidth_mbps": link.get("bandwidthMbps"),
        "latency_ms": link.get("latencyMs"),
        "loss_rate": link.get("lossRate"),
    } for index, link in enumerate(ctx.topology.get("links", []), start=1)]
    return {"run_id": ctx.run_id, "nodes": nodes, "links": links}
