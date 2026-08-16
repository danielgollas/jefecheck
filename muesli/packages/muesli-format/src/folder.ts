import * as fs from "node:fs";
import * as path from "node:path";
import { parseFrontMatter, serializeFrontMatter, type FMValue } from "./frontmatter.ts";
import { extractAnnotations, spanRanges, timestamp, type Annotation } from "./spans.ts";
import {
  parseTranscript,
  serializeTranscript,
  renderTranscriptMarkdown,
  type Utterance,
} from "./transcript.ts";

export const FORMAT_VERSION = 1;

/**
 * Meeting metadata is kept as the full parsed object — never a typed subset —
 * so unknown keys written by newer clients survive read-modify-write cycles
 * (forward-compatibility rule in storage-format.md).
 */
export type MeetingMeta = Record<string, unknown>;

export interface Notes {
  frontMatter: Record<string, FMValue>;
  body: string;
}

export interface RawNotes {
  text: string;
}

export interface MeetingFolder {
  dir: string;
  meta: MeetingMeta;
  transcript?: Utterance[];
  notes?: Notes;
  rawNotes?: RawNotes;
}

const FILES = {
  meta: "meeting.json",
  transcript: "transcript.jsonl",
  transcriptMd: "transcript.md",
  notes: "notes.md",
  rawNotes: "raw-notes.md",
} as const;

export function readMeetingFolder(dir: string): MeetingFolder {
  const metaPath = path.join(dir, FILES.meta);
  const meta = JSON.parse(fs.readFileSync(metaPath, "utf8")) as MeetingMeta;

  const folder: MeetingFolder = { dir, meta };

  const transcriptPath = path.join(dir, FILES.transcript);
  if (fs.existsSync(transcriptPath)) {
    folder.transcript = parseTranscript(fs.readFileSync(transcriptPath, "utf8"));
  }

  const notesPath = path.join(dir, FILES.notes);
  if (fs.existsSync(notesPath)) {
    const { data, body } = parseFrontMatter(fs.readFileSync(notesPath, "utf8"));
    folder.notes = { frontMatter: data, body };
  }

  const rawNotesPath = path.join(dir, FILES.rawNotes);
  if (fs.existsSync(rawNotesPath)) {
    folder.rawNotes = { text: fs.readFileSync(rawNotesPath, "utf8") };
  }

  return folder;
}

export function writeMeetingFolder(dir: string, folder: Omit<MeetingFolder, "dir">): void {
  fs.mkdirSync(dir, { recursive: true });
  atomicWrite(path.join(dir, FILES.meta), JSON.stringify(folder.meta, null, 2) + "\n");
  if (folder.transcript) {
    atomicWrite(path.join(dir, FILES.transcript), serializeTranscript(folder.transcript));
    const title = typeof folder.meta.title === "string" ? folder.meta.title : "Meeting";
    const speakers = (folder.meta.speakers ?? {}) as Record<string, string>;
    atomicWrite(
      path.join(dir, FILES.transcriptMd),
      renderTranscriptMarkdown(title, folder.transcript, speakers),
    );
  }
  if (folder.notes) {
    atomicWrite(
      path.join(dir, FILES.notes),
      serializeFrontMatter(folder.notes.frontMatter, folder.notes.body),
    );
  }
  if (folder.rawNotes) {
    atomicWrite(path.join(dir, FILES.rawNotes), folder.rawNotes.text);
  }
}

// Write-temp-then-rename, per storage-format.md sync rules.
function atomicWrite(filePath: string, content: string): void {
  const tmp = filePath + ".muesli-tmp";
  fs.writeFileSync(tmp, content, "utf8");
  fs.renameSync(tmp, filePath);
}

export function notesAnnotations(notes: Notes): Annotation[] {
  return extractAnnotations(notes.body);
}

export function rawNotesTimestamps(rawNotes: RawNotes): number[] {
  return extractAnnotations(rawNotes.text)
    .map(timestamp)
    .filter((t): t is number => t !== null);
}

/**
 * Folder name: `YYYY-MM-DD HHMM <title>` in the meeting's local time.
 * Date parts are taken from the ISO string directly (which carries the meeting's
 * own UTC offset) so the name never depends on the machine's timezone.
 */
export function folderName(startedAtISO: string, title: string): string {
  const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})/.exec(startedAtISO);
  if (!m) throw new Error(`unparseable startedAt: ${startedAtISO}`);
  const clean = sanitizeTitle(title);
  return `${m[1]}-${m[2]}-${m[3]} ${m[4]}${m[5]} ${clean}`;
}

export function sanitizeTitle(title: string): string {
  const cleaned = title
    // eslint-disable-next-line no-control-regex
    .replace(/[/\\:*?"<>|\x00-\x1f]/g, " ")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 80)
    .trim();
  return cleaned === "" ? "Meeting" : cleaned;
}

/** Validate a meeting folder; returns a list of human-readable issues (empty = valid). */
export function validateMeetingFolder(dir: string): string[] {
  const issues: string[] = [];
  let folder: MeetingFolder;
  try {
    folder = readMeetingFolder(dir);
  } catch (e) {
    return [`unreadable folder: ${(e as Error).message}`];
  }

  if (folder.meta.muesliVersion !== FORMAT_VERSION) {
    issues.push(`meeting.json: muesliVersion must be ${FORMAT_VERSION}`);
  }
  if (typeof folder.meta.id !== "string" || folder.meta.id === "") {
    issues.push("meeting.json: missing id");
  }
  if (typeof folder.meta.title !== "string") {
    issues.push("meeting.json: missing title");
  }

  if (folder.notes) {
    const fm = folder.notes.frontMatter;
    if (fm.muesli !== FORMAT_VERSION) {
      issues.push(`notes.md: front matter 'muesli' must be ${FORMAT_VERSION}`);
    }
    if (fm.id !== folder.meta.id) {
      issues.push("notes.md: front matter id does not match meeting.json id");
    }
    const count = folder.transcript?.length ?? 0;
    try {
      for (const a of notesAnnotations(folder.notes)) {
        const ranges = spanRanges(a);
        if (!ranges) continue;
        for (const [start, end] of ranges) {
          if (start < 0 || end < start || end >= count) {
            issues.push(
              `notes.md: span [${start}, ${end}] out of range (transcript has ${count} utterances)`,
            );
          }
        }
      }
    } catch (e) {
      issues.push(`notes.md: ${(e as Error).message}`);
    }
  }

  if (folder.rawNotes) {
    try {
      for (const t of rawNotesTimestamps(folder.rawNotes)) {
        if (t < 0) issues.push(`raw-notes.md: negative timestamp ${t}`);
      }
    } catch (e) {
      issues.push(`raw-notes.md: ${(e as Error).message}`);
    }
  }

  return issues;
}
