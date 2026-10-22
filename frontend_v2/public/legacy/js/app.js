window.NetMindViz = window.NetMindViz || {};

(() => {
  const $ = id => document.getElementById(id);
  let comparisonRows = [];
  const selectedFiles = new Map();
  let replayControlsCleanup = null;

  async function findTopologyFile(files) {
    for (const file of files) {
      if (!file.name.toLowerCase().endsWith(".json")) continue;

      try {
        const text = await file.text();
        const topology = window.NetMindViz.parseTopology(text);
        return { file, topology };
      } catch {
                                                                       
      }
    }

    return null;
  }

  function fileMap(files) {
    return new Map(files.map(file => [file.name.toLowerCase(), file]));
  }

  function findRunBundles(files) {
  const byName = fileMap(files);

  const manifests = files.filter(file => {
    const name = file.name.toLowerCase();
    return name.endsWith("_manifest.json") ||
      /^run_.+\.json$/i.test(file.name);
  });

  return manifests.map(manifestFile => {
    const name = manifestFile.name;
    const runId = name.toLowerCase().endsWith("_manifest.json")
      ? name.replace(/_manifest\.json$/i, "")
      : name.replace(/\.json$/i, "");

    return {
      runId,
      manifestFile,
      networkFile: byName.get(`${runId}_network.csv`.toLowerCase()),
      flowsFile: byName.get(`${runId}_flows.csv`.toLowerCase()),
      traceFile: byName.get(`${runId}_trace.csv`.toLowerCase()),
      windowsFile: byName.get(`${runId}_windows.csv`.toLowerCase()),
      linksFile: byName.get(`${runId}_links.csv`.toLowerCase())
    };
  }).filter(bundle => bundle.networkFile);
}

  async function readComparisonRow(bundle) {
    const [manifestText, networkText, flowsText] = await Promise.all([
      bundle.manifestFile.text(),
      bundle.networkFile.text(),
      bundle.flowsFile ? bundle.flowsFile.text() : Promise.resolve("")
    ]);

    const manifest = JSON.parse(manifestText);
    const networkRows = window.NetMindViz.parseCsv(networkText);
    const network = networkRows[0];

    if (!network) {
      throw new Error(`${bundle.runId}_network.csv has no data row.`);
    }

    const flows = flowsText ? window.NetMindViz.parseCsv(flowsText) : [];
    const hopValues = flows
      .map(flow => Number(flow.avg_hops))
      .filter(Number.isFinite);

    const averageHops = hopValues.length
      ? hopValues.reduce((sum, value) => sum + value, 0) / hopValues.length
      : NaN;

    return {
      runId: bundle.runId,
      algorithm: manifest.algorithm || "unknown",
      weight: manifest.weight || "unknown",
      seed: manifest.seed ?? "unknown",
      reliability_pct: Number(network.reliability_pct),
      avg_throughput_mbps: Number(network.avg_throughput_mbps),
      avg_latency_ms: Number(network.avg_latency_ms),
      total_drops: Number(network.total_drops),
      recovery_time_s: Number(network.recovery_time_s),
      avg_hops: averageHops
    };
  }

  function fillComparison(rows) {
    comparisonRows = rows;
    const body = $("comparisonRows");
    const metricSelect = $("comparisonMetric");

    body.replaceChildren();

    for (const item of rows) {
      const row = document.createElement("tr");
      const values = [
        item.runId,
        item.algorithm,
        item.weight,
        item.seed,
        formatMetric(item.avg_latency_ms),
        formatMetric(item.avg_throughput_mbps),
        formatMetric(item.reliability_pct),
        formatMetric(item.total_drops),
        formatMetric(item.recovery_time_s),
        formatMetric(item.avg_hops)
      ];

      for (const value of values) {
        const cell = document.createElement("td");
        cell.textContent = value;
        row.append(cell);
      }

      body.append(row);
    }

    metricSelect.disabled = rows.length === 0;

    $("comparisonStatus").textContent = rows.length
      ? `Comparing ${rows.length} exported run(s). For a fair comparison, ` +
        "use runs from the same scenario, seed, and routing weight."
      : "No standard exported run files were selected.";

    window.NetMindViz.drawComparisonChart(
      $("comparisonCanvas"),
      rows,
      metricSelect.value
    );
  }

  function formatMetric(value) {
    return Number.isFinite(Number(value))
      ? Number(value).toFixed(3)
      : "—";
  }

  function fillSummary(model, runId) {
    const hosts = model.topology.nodes.filter(
      node => node.type === "HOST"
    ).length;
    const routers = model.topology.nodes.filter(
      node => node.type === "ROUTER"
    ).length;

    $("summary").textContent =
      `Run ${runId}: ${model.topology.nodes.length} devices ` +
      `(${hosts} hosts, ${routers} routers); ` +
      `${model.topology.links.length} links; ` +
      `${model.trace.length} trace rows; ` +
      `${model.events.length} replay events; ` +
      `duration ${model.endTime.toFixed(3)} s.`;
  }

  function fillFlows(flows) {
    const body = $("flowRows");
    body.replaceChildren();

    for (const flow of flows) {
      const row = document.createElement("tr");

      for (const field of [
        "flow_id", "src", "dst", "sent", "delivered", "dropped", "loss_pct"
      ]) {
        const cell = document.createElement("td");
        cell.textContent = flow[field] || "—";
        row.append(cell);
      }

      body.append(row);
    }
  }

  async function loadFiles(files) {
    const topologyResult = await findTopologyFile(files);
    const bundles = findRunBundles(files);

    if (!topologyResult) {
      $("status").textContent = bundles.length
        ? "Run files remembered. Add the matching topology JSON file to finish loading."
        : "Add the topology JSON file and the exported run files you want to view.";
      return;
    }

    if (!bundles.length) {
      $("status").textContent =
        "Topology " + topologyResult.file.name +
        " remembered. Add a run manifest and its matching _network.csv; " +
        "add _trace.csv and _windows.csv for replay.";
      return;
    }

    const rows = await Promise.all(bundles.map(readComparisonRow));
    fillComparison(rows);

    const replayBundle = bundles.find(bundle =>
      bundle.traceFile && bundle.windowsFile
    );

    if (!replayBundle) {
      $("status").textContent =
        "Comparison loaded. Select a run's matching _trace.csv and " +
        "_windows.csv files as well to use packet replay.";
      return;
    }

    const [traceText, windowsText, flowsText, linksText] = await Promise.all([
      replayBundle.traceFile.text(),
      replayBundle.windowsFile.text(),
      replayBundle.flowsFile
        ? replayBundle.flowsFile.text()
        : Promise.resolve(""),
      replayBundle.linksFile
        ? replayBundle.linksFile.text()
        : Promise.resolve("")
    ]);

    const trace = window.NetMindViz.parseTrace(traceText);
    const windows = window.NetMindViz.parseCsv(windowsText);
    const flows = flowsText ? window.NetMindViz.parseCsv(flowsText) : [];
    const links = linksText ? window.NetMindViz.parseCsv(linksText) : [];

    const model = window.NetMindViz.buildReplayModel(
      topologyResult.topology,
      trace,
      windows,
      flows,
      links
    );

    fillSummary(model, replayBundle.runId);
    fillFlows(flows);

    $("status").textContent =
      `Loaded topology ${topologyResult.file.name}; replaying ` +
      `${replayBundle.runId} and comparing ${bundles.length} exported run(s).`;

    if (replayControlsCleanup) {
      replayControlsCleanup();
    }
    replayControlsCleanup = window.NetMindViz.createControls(model, {
      timeSlider: $("timeSlider"),
      timeLabel: $("timeLabel"),
      playButton: $("playButton"),
      speedSelect: $("speedSelect"),
      nextIncidentButton: $("nextIncidentButton"),
      metricSelect: $("metricSelect"),
      topologyCanvas: $("topologyCanvas"),
      metricCanvas: $("metricCanvas"),
      eventLog: $("eventLog"),
      selectionInfo: $("selectionInfo"),
      status: $("status")
    });
  }

  function renderSelectedFiles() {
    const list = $("selectedFileList");
    const files = Array.from(selectedFiles.values());
    list.replaceChildren();
    $("selectedFileCount").textContent = String(files.length);
    $("clearFilesButton").disabled = files.length === 0;

    if (!files.length) {
      const empty = document.createElement("li");
      empty.className = "empty-file-list";
      empty.textContent = "No files selected yet.";
      list.append(empty);
      return;
    }

    for (const file of files) {
      const item = document.createElement("li");
      item.textContent = file.name;
      item.title = file.name;
      list.append(item);
    }
  }

  async function addSelectedFiles(files) {
    for (const file of files) {
      selectedFiles.set(file.name.toLowerCase(), file);
    }
    renderSelectedFiles();

    try {
      await loadFiles(Array.from(selectedFiles.values()));
    } catch (error) {
      $("status").textContent = error.message || String(error);
    }
  }

  function clearSelectedFiles() {
    selectedFiles.clear();
    renderSelectedFiles();

    if (replayControlsCleanup) {
      replayControlsCleanup();
      replayControlsCleanup = null;
    }

    fillComparison([]);
    $("summary").textContent = "No run loaded.";
    $("flowRows").replaceChildren();
    $("eventLog").textContent = "Load a trace file to see events.";
    $("selectionInfo").textContent = "Click a device to inspect it.";
    $("timeLabel").textContent = "0.000 s";
    $("timeSlider").value = "0";
    $("timeSlider").max = "0";
    $("timeSlider").disabled = true;
    $("playButton").disabled = true;
    $("nextIncidentButton").disabled = true;

    for (const id of ["topologyCanvas", "metricCanvas"]) {
      const canvas = $(id);
      canvas.getContext("2d").clearRect(0, 0, canvas.width, canvas.height);
    }

    $("status").textContent =
      "Selected files cleared. Add a topology and run files to begin.";
  }

  $("comparisonMetric").addEventListener("change", () => {
    window.NetMindViz.drawComparisonChart(
      $("comparisonCanvas"),
      comparisonRows,
      $("comparisonMetric").value
    );
  });

  $("fileInput").addEventListener("change", async event => {
    const files = Array.from(event.target.files || []);
    event.target.value = "";
    if (files.length) {
      $("status").textContent = "Adding files to the current selection…";
      await addSelectedFiles(files);
    }
  });

  $("clearFilesButton").addEventListener("click", clearSelectedFiles);

  window.addEventListener("dragover", event => event.preventDefault());

  window.addEventListener("drop", async event => {
    event.preventDefault();

    try {
      $("status").textContent = "Reading dropped files…";
      await addSelectedFiles(Array.from(event.dataTransfer.files || []));
    } catch (error) {
      $("status").textContent = error.message || String(error);
    }
  });
})();
