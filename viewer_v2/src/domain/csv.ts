export type CsvRecord = Record<string, string>;

export function parseCsv(text: string): CsvRecord[] {
  const [header, ...body] = splitIntoRows(text);
  if (!header) return [];
  const names = header.map((name) => name.trim().replace(/^\uFEFF/, ""));
  return body.map((values) => {
    const record: CsvRecord = {};
    names.forEach((name, index) => {
      record[name] = (values[index] ?? "").trim();
    });
    return record;
  });
}

function splitIntoRows(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [];
  let field = "";
  let insideQuotes = false;

  const endRow = () => {
    row.push(field);
    field = "";
    if (row.some((value) => value.length > 0)) rows.push(row);
    row = [];
  };

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    if (insideQuotes) {
      if (ch === '"' && text[i + 1] === '"') {
        field += '"';
        i += 1;
      } else if (ch === '"') {
        insideQuotes = false;
      } else {
        field += ch;
      }
    } else if (ch === '"' && field.length === 0) {
      insideQuotes = true;
    } else if (ch === ",") {
      row.push(field);
      field = "";
    } else if (ch === "\n" || ch === "\r") {
      if (ch === "\r" && text[i + 1] === "\n") i += 1;
      endRow();
    } else {
      field += ch;
    }
  }
  if (insideQuotes) throw new Error("CSV file ends inside a quoted field.");
  if (field.length || row.length) endRow();
  return rows;
}

export function parseDetail(text: string): Record<string, string> {
  const result: Record<string, string> = {};
  for (const part of text.split(";")) {
    const separator = part.indexOf("=");
    if (separator > 0) result[part.slice(0, separator).trim()] = part.slice(separator + 1).trim();
  }
  return result;
}

export const num = (value: string | undefined): number => {
  if (value === undefined || value === "") return NaN;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : NaN;
};
