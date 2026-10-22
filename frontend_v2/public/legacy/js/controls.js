window.NetMindViz = window.NetMindViz || {};

window.NetMindViz.createControls = function (model, elements) {
  const controller = new AbortController();
  const listenerOptions = { signal: controller.signal };
  let time = 0;
  let playing = false;
  let selectedNode = "";
  let lastFrame = 0;
  let positions = new Map();

  const slider = elements.timeSlider;
  const epsilon = 1e-9;

  slider.min = "0";
  slider.max = String(model.endTime);
  slider.step = "any";
  slider.value = "0";
  slider.disabled = false;

  function update() {
    time = Number(slider.value);
    elements.timeLabel.textContent = `${time.toFixed(6)} s`;
    elements.status.textContent =
      `Replay time: ${time.toFixed(6)} s / ${model.endTime.toFixed(6)} s.`;

    positions = window.NetMindViz.drawTopology(
      elements.topologyCanvas,
      model,
      time,
      selectedNode
    );

    window.NetMindViz.drawMetricChart(
      elements.metricCanvas,
      model.windows,
      elements.metricSelect.value,
      time
    );

    updateEventLog();
  }

  function updateEventLog() {
    const visible = model.events.filter(row => row.time <= time + epsilon);
    const recent = visible.slice(-80).reverse();

    elements.eventLog.replaceChildren();

    for (const row of recent) {
      const line = document.createElement("div");
      line.className = "event-row";

      const jump = document.createElement("button");
      jump.textContent = `${row.time.toFixed(6)}s`;
      jump.setAttribute(
        "aria-label",
        `Jump to ${row.event_type} at ${row.time.toFixed(6)} seconds`
      );
      jump.addEventListener("click", () => {
        slider.value = String(row.time);
        update();
      });

      line.append(
        jump,
        document.createTextNode(
          `${row.event_type} ${row.location} ${row.detail}`
        )
      );

      elements.eventLog.append(line);
    }

    if (!visible.length) {
      elements.eventLog.textContent = "No events yet at this time.";
    }
  }

  function jumpToNextIncident() {
    const candidates = [
      ...model.drops.map(item => ({
        time: item.time,
        type: "DROPPED"
      })),
      ...model.events
        .filter(row =>
          /LINK_DOWN|ROUTER_DOWN|FAILURE|LINK_DEGRADED/i.test(row.event_type)
        )
        .map(row => ({
          time: row.time,
          type: row.event_type
        }))
    ]
      .filter(item => item.time > time + epsilon)
      .sort((a, b) => a.time - b.time);

    if (!candidates.length) {
      elements.status.textContent =
        "No later failure, degradation, or packet drop was recorded.";
      return;
    }

    slider.value = String(candidates[0].time);
    update();
  }

  function frame(timestamp) {
    if (!playing) return;

    if (lastFrame !== 0) {
      const elapsedSeconds = (timestamp - lastFrame) / 1000;
      const speed = Number(elements.speedSelect.value);
      const nextTime = Number(slider.value) + elapsedSeconds * speed;

      slider.value = String(Math.min(model.endTime, nextTime));
      update();

      if (Number(slider.value) >= model.endTime) {
        playing = false;
      }
    }

    lastFrame = timestamp;

    if (playing) {
      requestAnimationFrame(frame);
    } else {
      elements.playButton.textContent = "Play";
    }
  }

  elements.playButton.disabled = false;
  elements.nextIncidentButton.disabled = false;

  elements.playButton.addEventListener("click", () => {
    playing = !playing;
    elements.playButton.textContent = playing ? "Pause" : "Play";
    lastFrame = 0;

    if (playing) {
      requestAnimationFrame(frame);
    }
  }, listenerOptions);

  slider.addEventListener("input", update, listenerOptions);
  elements.metricSelect.addEventListener("change", update, listenerOptions);
  elements.nextIncidentButton.addEventListener(
    "click",
    jumpToNextIncident,
    listenerOptions
  );

  elements.topologyCanvas.addEventListener("click", event => {
    const rect = elements.topologyCanvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    const node = window.NetMindViz.nodeAtPoint(
      positions,
      model.topology,
      x,
      y
    );

    if (!node) return;

    selectedNode = node.name;

    const updates = model.events.filter(row =>
      row.event_type === "ROUTE_UPDATE" && row.time <= time + epsilon
    );
    const latestUpdate = updates.length
      ? updates[updates.length - 1]
      : null;

    elements.selectionInfo.textContent =
      `${node.name} (${node.type}, ${node.ip}). ` +
      (latestUpdate
        ? `Latest network route update: t=${latestUpdate.time.toFixed(6)} s ` +
          `(${latestUpdate.detail}). The trace does not contain historical ` +
          `per-router forwarding tables.`
        : "No route update has occurred yet.");

    update();
  }, listenerOptions);

  window.addEventListener("resize", update, listenerOptions);
  update();

  return () => {
    playing = false;
    controller.abort();
    elements.playButton.textContent = "Play";
    elements.playButton.disabled = true;
    elements.nextIncidentButton.disabled = true;
  };
};
