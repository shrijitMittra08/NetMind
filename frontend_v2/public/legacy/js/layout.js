window.NetMindViz = window.NetMindViz || {};

window.NetMindViz.computeLayout = function (topology, width, height) {
  const padding = 55;
  const nodes = topology.nodes;
  const positions = new Map();
  const withPosition = nodes.filter(node =>
    Array.isArray(node.pos) && node.pos.length >= 2 &&
    Number.isFinite(Number(node.pos[0])) &&
    Number.isFinite(Number(node.pos[1]))
  );

  if (withPosition.length === nodes.length && nodes.length) {
    const xs = nodes.map(node => Number(node.pos[0]));
    const ys = nodes.map(node => Number(node.pos[1]));
    const minX = Math.min(...xs), maxX = Math.max(...xs);
    const minY = Math.min(...ys), maxY = Math.max(...ys);
    nodes.forEach(node => {
      const x = Number(node.pos[0]), y = Number(node.pos[1]);
      positions.set(String(node.id), {
        x: padding + (maxX === minX ? .5 : (x - minX) / (maxX - minX)) *
          (width - padding * 2),
        y: padding + (maxY === minY ? .5 : (y - minY) / (maxY - minY)) *
          (height - padding * 2)
      });
    });
  } else {
    const centerX = width / 2, centerY = height / 2;
    const radius = Math.max(30, Math.min(width, height) / 2 - padding);
    nodes.forEach((node, index) => {
      const angle = -Math.PI / 2 + 2 * Math.PI * index / Math.max(1, nodes.length);
      positions.set(String(node.id), {
        x: centerX + Math.cos(angle) * radius,
        y: centerY + Math.sin(angle) * radius
      });
    });
  }

  return positions;
};