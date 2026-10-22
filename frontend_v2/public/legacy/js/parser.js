window.NetMindViz = window.NetMindViz || {};

window.NetMindViz.parseCsv = function (text) {
  const rows = [];
  let row = [];
  let field = "";
  let quoted = false;

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];

    if (quoted) {
      if (ch === '"' && text[i + 1] === '"') {
        field += '"';
        i += 1;
      } else if (ch === '"') {
        quoted = false;
      } else {
        field += ch;
      }
    } else if (ch === '"' && field.length === 0) {
      quoted = true;
    } else if (ch === ",") {
      row.push(field);
      field = "";
    } else if (ch === "\n" || ch === "\r") {
      if (ch === "\r" && text[i + 1] === "\n") i += 1;
      row.push(field);
      field = "";
      if (row.some(value => value.length > 0)) rows.push(row);
      row = [];
    } else {
      field += ch;
    }
  }

  if (quoted) throw new Error("CSV file ends inside a quoted field.");
  if (field.length || row.length) {
    row.push(field);
    if (row.some(value => value.length > 0)) rows.push(row);
  }
  if (!rows.length) return [];

  const headers = rows[0].map(value =>
  value.trim().replace(/^\uFEFF/, "")
);
  return rows.slice(1).map(values => {
    const record = {};
    headers.forEach((name, index) => {
      record[name] = (values[index] || "").trim();
    });
    return record;
  });
};

window.NetMindViz.parseDetail = function (text) {
  const result = {};
  for (const part of (text || "").split(";")) {
    const separator = part.indexOf("=");
    if (separator > 0) {
      result[part.slice(0, separator).trim()] =
        part.slice(separator + 1).trim();
    }
  }
  return result;
};

window.NetMindViz.readText = function (file) {
  return file.text();
};

window.NetMindViz.parseTopology = function (text) {
  const topology = JSON.parse(text);
  if (!Array.isArray(topology.nodes) || !Array.isArray(topology.links)) {
    throw new Error("Topology JSON must contain nodes and links arrays.");
  }

  const names = new Set();
  for (const node of topology.nodes) {
    if (node.id === undefined || !node.name || !node.type) {
      throw new Error("Each topology node needs id, name, and type.");
    }
    if (names.has(node.name)) throw new Error(`Duplicate node name: ${node.name}`);
    names.add(node.name);
  }

  return topology;
};

window.NetMindViz.parseTrace = function (text) {
  const rows = window.NetMindViz.parseCsv(text);
  const required = ["time_s", "event_type", "packet_id", "flow_id", "location", "detail"];
  if (!rows.length || required.some(key => !(key in rows[0]))) {
    throw new Error("Trace CSV does not have the expected NetMind columns.");
  }

  return rows.map((row, index) => ({
    ...row,
    time: Number(row.time_s),
    order: index,
    parsedDetail: window.NetMindViz.parseDetail(row.detail)
  })).filter(row => Number.isFinite(row.time));
};