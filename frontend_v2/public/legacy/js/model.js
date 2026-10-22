window.NetMindViz = window.NetMindViz || {};

window.NetMindViz.buildReplayModel = function (
  topology, trace, windows, flows, links
) {
  const nodeByName = new Map(topology.nodes.map(node => [node.name, node]));
  const nodeById = new Map(topology.nodes.map(node => [String(node.id), node]));

                                                                        
                   
  const linkById = new Map(topology.links.map((link, index) => [
    String(index + 1),
    {
      ...link,
      id: index + 1,
      aNode: nodeById.get(String(link.a)),
      bNode: nodeById.get(String(link.b))
    }
  ]));

  const segments = [];
  const drops = [];
  const events = [];
  const journeys = new Map();
  const pendingByPacket = new Map();
  const downStarts = new Map();
  const downIntervals = [];

  function pendingFor(packetId) {
    if (!pendingByPacket.has(packetId)) {
      pendingByPacket.set(packetId, []);
    }
    return pendingByPacket.get(packetId);
  }

  function finishSegment(segment, endTime) {
    if (!Number.isFinite(endTime) || endTime < segment.tStart) return;

    segments.push({
      packetId: segment.packetId,
      flowId: segment.flowId,
      channel: segment.channel,
      from: segment.from,
      to: segment.to,
      tStart: segment.tStart,
      tEnd: endTime
    });
  }

  for (const row of trace) {
    const type = row.event_type;
    const packetId = row.packet_id;

    if (packetId && packetId !== "-") {
      if (!journeys.has(packetId)) journeys.set(packetId, []);
      journeys.get(packetId).push(row);
    }

    if (type === "TX_START" && packetId && packetId !== "-") {
      const [from, to] = row.location.split(">");
      if (from && to) {
        pendingFor(packetId).push({
          packetId,
          flowId: row.flow_id,
          channel: row.location,
          from,
          to,
          tStart: row.time,
          eta: Number(row.parsedDetail.eta)
        });
      }
    }

    if (type === "ARRIVE" && packetId && packetId !== "-") {
      const from = row.parsedDetail.from;
      const to = row.location;
      const pending = pendingFor(packetId);

                                                                             
      const index = pending.findIndex(segment =>
        segment.from === from &&
        segment.to === to &&
        segment.tStart <= row.time
      );

      if (index >= 0) {
        const [segment] = pending.splice(index, 1);
        finishSegment(segment, row.time);
      }
    }

    if (type === "DROPPED") {
      drops.push({
        time: row.time,
        location: row.location,
        reason: row.parsedDetail.reason || "unknown",
        packetId
      });

                                                                          
                                             
      if (packetId && packetId !== "-") {
        const pending = pendingFor(packetId);
        for (let index = pending.length - 1; index >= 0; index -= 1) {
          if (pending[index].tStart <= row.time) {
            const [segment] = pending.splice(index, 1);
            finishSegment(segment, row.time);
            break;
          }
        }
      }
    }

    if (type === "LINK_DOWN") {
      downStarts.set(row.location, row.time);
    } else if (type === "LINK_UP" && downStarts.has(row.location)) {
      downIntervals.push({
        linkId: row.location,
        start: downStarts.get(row.location),
        end: row.time
      });
      downStarts.delete(row.location);
    }

    if (
      type !== "ENQUEUE" &&
      type !== "TX_START" &&
      type !== "ARRIVE" &&
      type !== "FORWARD" &&
      type !== "SENT"
    ) {
      events.push(row);
    }
  }

  let endTime = 0;

for (const row of trace) {
  if (Number.isFinite(row.time)) {
    endTime = Math.max(endTime, row.time);
  }
}

for (const row of windows) {
  const startTime = Number(row.t_start);
  if (Number.isFinite(startTime)) {
    endTime = Math.max(endTime, startTime);
  }
}

                                                                         
                                               
  for (const pending of pendingByPacket.values()) {
    for (const segment of pending) {
      finishSegment(
        segment,
        Number.isFinite(segment.eta) && segment.eta >= segment.tStart
          ? segment.eta
          : segment.tStart
      );
    }
  }

  for (const [linkId, start] of downStarts) {
    downIntervals.push({ linkId, start, end: endTime });
  }

  const numericWindows = windows.map(row => {
    const copy = { ...row };
    for (const key of [
      "t_start",
      "avg_latency_ms",
      "throughput_mbps",
      "loss_pct",
      "max_util_pct",
      "max_queue",
      "sent",
      "delivered",
      "dropped"
    ]) {
      if (copy[key] !== undefined) {
        copy[key] = Number(copy[key]);
      }
    }
    return copy;
  });

  events.sort((a, b) => a.time - b.time || a.order - b.order);
  segments.sort((a, b) => a.tStart - b.tStart || a.tEnd - b.tEnd);

  return {
    topology,
    trace,
    windows: numericWindows,
    flows,
    links,
    nodeByName,
    nodeById,
    linkById,
    segments,
    drops,
    events,
    downIntervals,
    journeys,
    endTime
  };
};