export async function requestJson(url, options = {}) {
  const response = await fetch(url, {
    ...options,
    headers: {
      Accept: "application/json",
      ...(options.body ? { "Content-Type": "application/json" } : {}),
      ...options.headers,
    },
  });

  let payload;
  try {
    payload = await response.json();
  } catch {
    throw new Error("The NetMind service returned an unreadable response.");
  }

  if (!response.ok) {
    const error = new Error(payload.error || `Request failed (${response.status}).`);
    error.status = response.status;
    throw error;
  }

  return payload;
}

export function askGraphRAG(runIds, question) {
  return requestJson("/api/graphrag/query", {
    method: "POST",
    body: JSON.stringify({ run_ids: runIds, question }),
  });
}

export function fetchRunFiles(runId, { light = false } = {}) {
  return requestJson(
    `/api/gui/runs/${encodeURIComponent(runId)}/files${light ? "?light=1" : ""}`,
  );
}

export function saveScenario(payload) {
  return requestJson("/api/scenario/save", {
    method: "POST",
    body: JSON.stringify(payload),
  });
}