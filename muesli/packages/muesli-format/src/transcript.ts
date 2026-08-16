export interface Utterance {
  /** Stable index; notes.md spans reference these. Strictly increasing. */
  i: number;
  t0: number;
  t1: number;
  speaker?: string;
  text: string;
  conf?: number;
}

export function parseTranscript(jsonl: string): Utterance[] {
  const utterances: Utterance[] = [];
  const lines = jsonl.split(/\r?\n/).filter((l) => l.trim() !== "");
  for (const [lineNo, line] of lines.entries()) {
    let obj: unknown;
    try {
      obj = JSON.parse(line);
    } catch {
      throw new Error(`transcript line ${lineNo + 1}: invalid JSON`);
    }
    const u = obj as Record<string, unknown>;
    if (
      typeof u.i !== "number" ||
      typeof u.t0 !== "number" ||
      typeof u.t1 !== "number" ||
      typeof u.text !== "string"
    ) {
      throw new Error(`transcript line ${lineNo + 1}: missing required fields`);
    }
    utterances.push(u as unknown as Utterance);
  }
  for (let n = 1; n < utterances.length; n++) {
    if (utterances[n].i <= utterances[n - 1].i) {
      throw new Error(`transcript: utterance index not strictly increasing at line ${n + 1}`);
    }
    if (utterances[n].t0 < utterances[n - 1].t0) {
      throw new Error(`transcript: utterances not ordered by t0 at line ${n + 1}`);
    }
  }
  return utterances;
}

export function serializeTranscript(utterances: Utterance[]): string {
  return (
    utterances
      .map((u) => {
        const ordered: Record<string, unknown> = { i: u.i, t0: u.t0, t1: u.t1 };
        if (u.speaker !== undefined) ordered.speaker = u.speaker;
        ordered.text = u.text;
        if (u.conf !== undefined) ordered.conf = u.conf;
        return JSON.stringify(ordered);
      })
      .join("\n") + "\n"
  );
}

/** Render the human-readable transcript.md from utterances + speaker names. */
export function renderTranscriptMarkdown(
  title: string,
  utterances: Utterance[],
  speakerNames: Record<string, string> = {},
): string {
  const lines = utterances.map((u) => {
    const mm = Math.floor(u.t0 / 60);
    const ss = Math.floor(u.t0 % 60);
    const stamp = `${String(mm).padStart(2, "0")}:${String(ss).padStart(2, "0")}`;
    const who = u.speaker ? (speakerNames[u.speaker] ?? u.speaker) + ":" : "";
    const label = who ? ` ${who}` : "";
    return `**[${stamp}]${label}** ${u.text}`;
  });
  return `# Transcript — ${title}\n\n${lines.join("\n\n")}\n`;
}
