window.NetMindViz = window.NetMindViz || {};

window.NetMindViz.drawMetricChart = function (canvas, windows, key, currentTime) {
  const context = canvas.getContext("2d");
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(width * ratio));
  canvas.height = Math.max(1, Math.floor(height * ratio));
  context.scale(ratio, ratio);
  context.clearRect(0, 0, width, height);

  const points = windows
    .map(row => ({ time: row.t_start, value: row[key] }))
    .filter(point => Number.isFinite(point.time) && Number.isFinite(point.value));

  context.fillStyle = "#526677";
  context.font = "12px system-ui";
  if (!points.length) {
    context.fillText("No values for this metric in windows.csv.", 12, 24);
    return;
  }

  const left = 46, right = 12, top = 14, bottom = 32;
  const plotWidth = Math.max(1, width - left - right);
  const plotHeight = Math.max(1, height - top - bottom);
  const maxTime = Math.max(1e-9, ...points.map(point => point.time));
  const maxValue = Math.max(1e-9, ...points.map(point => point.value));

  context.strokeStyle = "#d8e0e6";
  context.beginPath();
  context.moveTo(left, top);
  context.lineTo(left, top + plotHeight);
  context.lineTo(left + plotWidth, top + plotHeight);
  context.stroke();

  context.fillText(maxValue.toFixed(2), 4, top + 5);
  context.fillText("0", 26, top + plotHeight);
  context.fillText("time (s)", left + plotWidth - 42, height - 7);

  context.strokeStyle = "#1684b5";
  context.lineWidth = 2;
  context.beginPath();
  points.forEach((point, index) => {
    const x = left + point.time / maxTime * plotWidth;
    const y = top + plotHeight - point.value / maxValue * plotHeight;
    if (index === 0) context.moveTo(x, y);
    else context.lineTo(x, y);
  });
  context.stroke();

  const markerX = left + Math.min(currentTime, maxTime) / maxTime * plotWidth;
  context.strokeStyle = "#e34b4b";
  context.setLineDash([4, 3]);
  context.beginPath();
  context.moveTo(markerX, top);
  context.lineTo(markerX, top + plotHeight);
  context.stroke();
  context.setLineDash([]);
};

window.NetMindViz.drawComparisonChart = function (canvas, rows, metric) {
  const context = canvas.getContext("2d");
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const ratio = window.devicePixelRatio || 1;

  canvas.width = Math.max(1, Math.floor(width * ratio));
  canvas.height = Math.max(1, Math.floor(height * ratio));
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);

  const points = rows
    .map(row => ({
      label: `${row.algorithm} (${row.runId})`,
      value: Number(row[metric])
    }))
    .filter(point => Number.isFinite(point.value));

  context.font = "12px system-ui";
  context.fillStyle = "#526677";

  if (!points.length) {
    context.fillText("No comparison data is loaded.", 12, 24);
    return;
  }

  const left = 58;
  const right = 18;
  const top = 24;
  const bottom = 64;
  const chartWidth = Math.max(1, width - left - right);
  const chartHeight = Math.max(1, height - top - bottom);
  const minValue = Math.min(0, ...points.map(point => point.value));
  const maxValue = Math.max(1e-9, ...points.map(point => point.value));
  const valueRange = Math.max(1e-9, maxValue - minValue);
  const slotWidth = chartWidth / points.length;
  const barWidth = Math.min(72, slotWidth * 0.58);
  const zeroY = top + ((maxValue - 0) / valueRange) * chartHeight;

  context.strokeStyle = "#d8e0e6";
  context.beginPath();
  context.moveTo(left, top);
  context.lineTo(left, top + chartHeight);
  context.lineTo(left + chartWidth, top + chartHeight);
  context.stroke();

  const colors = ["#1684b5", "#e7a329", "#57a773", "#9b6ac7"];

  points.forEach((point, index) => {
    const valueY =
      top + ((maxValue - point.value) / valueRange) * chartHeight;
    const x = left + index * slotWidth + (slotWidth - barWidth) / 2;
    const y = Math.min(zeroY, valueY);
    const barHeight = Math.max(1, Math.abs(zeroY - valueY));

    context.fillStyle = colors[index % colors.length];
    context.fillRect(x, y, barWidth, barHeight);

    context.fillStyle = "#17212b";
    context.textAlign = "center";
    context.fillText(
      point.value.toFixed(3),
      x + barWidth / 2,
      Math.max(top + 12, y - 6)
    );

    context.save();
    context.translate(x + barWidth / 2, top + chartHeight + 12);
    context.rotate(-Math.PI / 5);
    context.fillStyle = "#41576a";
    context.fillText(point.label, 0, 0);
    context.restore();
  });

  context.textAlign = "left";
};