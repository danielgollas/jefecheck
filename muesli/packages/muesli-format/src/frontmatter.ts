// Muesli front matter is a deliberate YAML *subset* so every platform can parse
// it without a YAML dependency: flat `key: value` pairs where value is a scalar
// (string or number) or an inline list `[a, b, c]` of scalars. Conforming
// writers emit only this subset (storage-format.md).

export type FMScalar = string | number;
export type FMValue = FMScalar | FMScalar[];

const FM_OPEN = "---\n";
const FM_CLOSE = "\n---\n";

export function parseFrontMatter(text: string): {
  data: Record<string, FMValue>;
  body: string;
} {
  if (!text.startsWith(FM_OPEN)) {
    throw new Error("missing front matter open marker");
  }
  const end = text.indexOf(FM_CLOSE, FM_OPEN.length);
  if (end === -1) {
    throw new Error("missing front matter close marker");
  }
  const raw = text.slice(FM_OPEN.length, end);
  const body = text.slice(end + FM_CLOSE.length);
  const data: Record<string, FMValue> = {};
  for (const line of raw.split("\n")) {
    if (line.trim() === "") continue;
    const m = /^([A-Za-z_][A-Za-z0-9_-]*):\s*(.*)$/.exec(line);
    if (!m) throw new Error(`unparseable front matter line: ${JSON.stringify(line)}`);
    data[m[1]] = parseValue(m[2].trim());
  }
  return { data, body };
}

function parseValue(s: string): FMValue {
  if (s.startsWith("[") && s.endsWith("]")) {
    const inner = s.slice(1, -1).trim();
    if (inner === "") return [];
    return splitTopLevel(inner).map((item) => parseScalar(item.trim()));
  }
  return parseScalar(s);
}

// Split an inline list on commas, respecting quoted items.
function splitTopLevel(s: string): string[] {
  const parts: string[] = [];
  let current = "";
  let quote: string | null = null;
  for (const ch of s) {
    if (quote) {
      current += ch;
      if (ch === quote) quote = null;
    } else if (ch === '"' || ch === "'") {
      current += ch;
      quote = ch;
    } else if (ch === ",") {
      parts.push(current);
      current = "";
    } else {
      current += ch;
    }
  }
  parts.push(current);
  return parts;
}

function parseScalar(s: string): FMScalar {
  if (
    (s.startsWith('"') && s.endsWith('"') && s.length >= 2) ||
    (s.startsWith("'") && s.endsWith("'") && s.length >= 2)
  ) {
    return s.slice(1, -1);
  }
  if (/^-?\d+(\.\d+)?$/.test(s)) return Number(s);
  return s;
}

export function serializeFrontMatter(
  data: Record<string, FMValue>,
  body: string,
): string {
  const lines = Object.entries(data).map(([k, v]) => `${k}: ${serializeValue(v)}`);
  return `---\n${lines.join("\n")}\n---\n${body}`;
}

function serializeValue(v: FMValue): string {
  if (Array.isArray(v)) return `[${v.map(serializeScalar).join(", ")}]`;
  return serializeScalar(v);
}

function serializeScalar(v: FMScalar): string {
  if (typeof v === "number") return String(v);
  const needsQuoting =
    v === "" ||
    /[:#,\[\]"']/.test(v) ||
    v !== v.trim() ||
    /^-?\d+(\.\d+)?$/.test(v);
  return needsQuoting ? JSON.stringify(v) : v;
}
