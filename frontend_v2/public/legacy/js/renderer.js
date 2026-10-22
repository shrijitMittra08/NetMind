window.NetMindViz = window.NetMindViz || {};

window.NetMindViz.drawTopology = function (canvas, model, time, selectedNode) {
                                                                          
                                                                           
  const packetLingerSeconds = 0.02;

  const context = canvas.getContext("2d");
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const ratio = window.devicePixelRatio || 1;

  canvas.width = Math.max(1, Math.floor(width * ratio));
  canvas.height = Math.max(1, Math.floor(height * ratio));
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);

  const positions = window.NetMindViz.computeLayout(
    model.topology, width, height
  );

  const utilization = new Map();
  for (const row of model.links) {
    const value = Number(row.utilization_pct);
    if (Number.isFinite(value)) {
      utilization.set(row.channel, value);
    }
  }

  const downLinkIds = new Set(
    model.downIntervals
      .filter(interval => interval.start <= time && time <= interval.end)
      .map(interval => interval.linkId)
  );

                                                           
  model.topology.links.forEach((link, index) => {
    const nodeA = model.nodeById.get(String(link.a));
    const nodeB = model.nodeById.get(String(link.b));
    if (!nodeA || !nodeB) return;

    const pointA = positions.get(String(nodeA.id));
    const pointB = positions.get(String(nodeB.id));
    if (!pointA || !pointB) return;

    const isDown = downLinkIds.has(String(index + 1));
    const usage = Math.max(
      utilization.get(`${nodeA.name}>${nodeB.name}`) || 0,
      utilization.get(`${nodeB.name}>${nodeA.name}`) || 0
    );

    context.strokeStyle = isDown
      ? "#d52d2d"
      : usage >= 80
        ? "#e24b24"
        : usage >= 50
          ? "#e7a329"
          : "#57a773";
    context.lineWidth = 2 + Math.min(4, usage / 25);
    context.setLineDash(isDown ? [7, 5] : []);
    context.beginPath();
    context.moveTo(pointA.x, pointA.y);
    context.lineTo(pointB.x, pointB.y);
    context.stroke();
    context.setLineDash([]);

    context.fillStyle = "#526677";
    context.font = "11px system-ui";
    context.textAlign = "center";
    context.fillText(
      `${link.bandwidthMbps} Mbps`,
      (pointA.x + pointB.x) / 2,
      (pointA.y + pointB.y) / 2 - 5
    );
  });

                                                                          
                                                                    
  const visiblePackets = new Map();

  for (const segment of model.segments) {
    if (time < segment.tStart || time > segment.tEnd + packetLingerSeconds) {
      continue;
    }

    const existing = visiblePackets.get(segment.packetId);
    if (!existing || segment.tStart >= existing.segment.tStart) {
      visiblePackets.set(segment.packetId, { segment });
    }
  }

  for (const { segment } of visiblePackets.values()) {
    const fromNode = model.nodeByName.get(segment.from);
    const toNode = model.nodeByName.get(segment.to);
    if (!fromNode || !toNode) continue;

    const from = positions.get(String(fromNode.id));
    const to = positions.get(String(toNode.id));
    if (!from || !to) continue;

    const duration = segment.tEnd - segment.tStart;
    const fraction = duration <= 0
      ? 1
      : Math.max(0, Math.min(1, (time - segment.tStart) / duration));

    const lingerTime = Math.max(0, time - segment.tEnd);
    const opacity = Math.max(
      0.25,
      1 - lingerTime / packetLingerSeconds
    );

    const x = from.x + (to.x - from.x) * fraction;
    const y = from.y + (to.y - from.y) * fraction;

    context.globalAlpha = opacity;
    context.fillStyle = "#073bce";
    context.beginPath();
    context.arc(x, y, 5, 0, Math.PI * 2);
    context.fill();
    context.globalAlpha = 1;
  }

                                                
  for (const drop of model.drops) {
    if (time < drop.time || time - drop.time >= 0.7) continue;

    const node = model.nodeByName.get(drop.location);
    if (!node) continue;

    const point = positions.get(String(node.id));
    if (!point) continue;

    context.fillStyle = "#c21d32";
    context.font = "bold 24px system-ui";
    context.textAlign = "left";
    context.fillText("×", point.x + 9, point.y - 9);
  }

                                  
  for (const node of model.topology.nodes) {
    const point = positions.get(String(node.id));
    if (!point) continue;

    context.fillStyle = node.type === "HOST" ? "#e8f3fa" : "#fff2d7";
    context.strokeStyle = node.name === selectedNode ? "#073bce" : "#34495e";
    context.lineWidth = node.name === selectedNode ? 3 : 1.5;
    context.beginPath();
    context.arc(point.x, point.y, 21, 0, Math.PI * 2);
    context.fill();
    context.stroke();

    context.fillStyle = "#17212b";
    context.font = "12px system-ui";
    context.textAlign = "center";
    context.fillText(node.name, point.x, point.y + 38);
  }

  return positions;
};

window.NetMindViz.nodeAtPoint = function (positions, topology, x, y) {
  for (const node of topology.nodes) {
    const point = positions.get(String(node.id));
    if (point && Math.hypot(x - point.x, y - point.y) <= 24) {
      return node;
    }
  }
  return null;
};