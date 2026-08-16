// Muesli annotations are HTML comments of the form <!--m:{...json...}-->
// embedded in Markdown. In notes.md the payload is {"spans": [[start, end], ...]}
// (inclusive utterance-index ranges into transcript.jsonl). In raw-notes.md the
// payload is {"t": seconds} — the capture-relative time the paragraph was typed.

export interface Annotation {
  /** Character offset of the comment within the searched text. */
  offset: number;
  data: Record<string, unknown>;
}

const ANNOTATION_RE = /<!--m:(\{[\s\S]*?\})-->/g;

export function extractAnnotations(text: string): Annotation[] {
  const out: Annotation[] = [];
  for (const m of text.matchAll(ANNOTATION_RE)) {
    let data: unknown;
    try {
      data = JSON.parse(m[1]);
    } catch {
      throw new Error(`invalid annotation JSON at offset ${m.index}: ${m[1]}`);
    }
    if (typeof data !== "object" || data === null || Array.isArray(data)) {
      throw new Error(`annotation payload must be a JSON object at offset ${m.index}`);
    }
    out.push({ offset: m.index, data: data as Record<string, unknown> });
  }
  return out;
}

/** The span ranges of an annotation, or null if it carries none. */
export function spanRanges(a: Annotation): [number, number][] | null {
  const spans = a.data["spans"];
  if (spans === undefined) return null;
  if (
    !Array.isArray(spans) ||
    !spans.every(
      (r) =>
        Array.isArray(r) &&
        r.length === 2 &&
        r.every((n) => typeof n === "number" && Number.isInteger(n)),
    )
  ) {
    throw new Error(`malformed spans payload: ${JSON.stringify(spans)}`);
  }
  return spans as [number, number][];
}

/** The typing timestamp of an annotation, or null if it carries none. */
export function timestamp(a: Annotation): number | null {
  const t = a.data["t"];
  if (t === undefined) return null;
  if (typeof t !== "number") {
    throw new Error(`malformed timestamp payload: ${JSON.stringify(t)}`);
  }
  return t;
}

export function formatAnnotation(data: Record<string, unknown>): string {
  return `<!--m:${JSON.stringify(data)}-->`;
}
