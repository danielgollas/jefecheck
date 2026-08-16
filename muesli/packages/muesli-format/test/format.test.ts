import { test } from "node:test";
import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import {
  readMeetingFolder,
  writeMeetingFolder,
  validateMeetingFolder,
  notesAnnotations,
  rawNotesTimestamps,
  folderName,
  sanitizeTitle,
  spanRanges,
  type MeetingFolder,
} from "../src/index.ts";

const fixturesDir = path.resolve(import.meta.dirname, "../../../fixtures");
const fixtureNames = ["2026-08-12 1000 Weekly Standup", "2026-01-05 0930 Quick Sync"];

interface ExpectedSummary {
  meta: { id: string; title: string; muesliVersion: number };
  transcript: { count: number; firstText: string; lastText: string };
  notes: {
    title: string;
    attendees: string[];
    tags: string[];
    spanAnnotations: [number, number][][];
  } | null;
  rawNotesTimestamps: number[] | null;
}

function summarize(folder: MeetingFolder): ExpectedSummary {
  const transcript = folder.transcript ?? [];
  return {
    meta: {
      id: folder.meta.id as string,
      title: folder.meta.title as string,
      muesliVersion: folder.meta.muesliVersion as number,
    },
    transcript: {
      count: transcript.length,
      firstText: transcript[0]?.text ?? "",
      lastText: transcript[transcript.length - 1]?.text ?? "",
    },
    notes: folder.notes
      ? {
          title: folder.notes.frontMatter.title as string,
          attendees: (folder.notes.frontMatter.attendees ?? []) as string[],
          tags: (folder.notes.frontMatter.tags ?? []) as string[],
          spanAnnotations: notesAnnotations(folder.notes)
            .map(spanRanges)
            .filter((r): r is [number, number][] => r !== null),
        }
      : null,
    rawNotesTimestamps: folder.rawNotes ? rawNotesTimestamps(folder.rawNotes) : null,
  };
}

for (const name of fixtureNames) {
  test(`golden fixture: ${name}`, () => {
    const folder = readMeetingFolder(path.join(fixturesDir, name));
    const expected = JSON.parse(
      fs.readFileSync(path.join(fixturesDir, "expected", `${name}.json`), "utf8"),
    ) as ExpectedSummary;
    assert.deepEqual(summarize(folder), expected);
  });

  test(`fixture validates cleanly: ${name}`, () => {
    assert.deepEqual(validateMeetingFolder(path.join(fixturesDir, name)), []);
  });
}

test("roundtrip: read → write → read is lossless", () => {
  const original = readMeetingFolder(path.join(fixturesDir, fixtureNames[0]));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-rt-"));
  try {
    writeMeetingFolder(tmp, original);
    const reread = readMeetingFolder(tmp);
    assert.deepEqual(reread.meta, original.meta);
    assert.deepEqual(reread.transcript, original.transcript);
    assert.deepEqual(reread.notes, original.notes);
    assert.deepEqual(reread.rawNotes, original.rawNotes);
    assert.deepEqual(validateMeetingFolder(tmp), []);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("unknown meta keys survive roundtrip", () => {
  const original = readMeetingFolder(path.join(fixturesDir, fixtureNames[1]));
  assert.deepEqual(original.meta["x-custom"], { importedFrom: "voice-memo", revision: 3 });
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-uk-"));
  try {
    writeMeetingFolder(tmp, original);
    const reread = readMeetingFolder(tmp);
    assert.deepEqual(reread.meta["x-custom"], { importedFrom: "voice-memo", revision: 3 });
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("folderName formats in the meeting's local time and sanitizes", () => {
  assert.equal(
    folderName("2026-08-12T10:00:03-07:00", "Weekly Standup"),
    "2026-08-12 1000 Weekly Standup",
  );
  assert.equal(
    folderName("2026-01-05T09:30:00+01:00", 'Q3: Plan/Review "Final"?'),
    "2026-01-05 0930 Q3 Plan Review Final",
  );
});

test("sanitizeTitle edge cases", () => {
  assert.equal(sanitizeTitle("///"), "Meeting");
  assert.equal(sanitizeTitle("  spaced   out  "), "spaced out");
  assert.equal(sanitizeTitle("x".repeat(200)).length, 80);
});

test("validation catches out-of-range spans", () => {
  const src = path.join(fixturesDir, fixtureNames[0]);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-bad-"));
  try {
    fs.cpSync(src, tmp, { recursive: true });
    const notesPath = path.join(tmp, "notes.md");
    const corrupted = fs
      .readFileSync(notesPath, "utf8")
      .replace('<!--m:{"spans":[[6,6]]}-->', '<!--m:{"spans":[[98,99]]}-->');
    fs.writeFileSync(notesPath, corrupted);
    const issues = validateMeetingFolder(tmp);
    assert.equal(issues.length, 1);
    assert.match(issues[0], /span \[98, 99\] out of range/);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("validation catches id mismatch", () => {
  const src = path.join(fixturesDir, fixtureNames[0]);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-id-"));
  try {
    fs.cpSync(src, tmp, { recursive: true });
    const metaPath = path.join(tmp, "meeting.json");
    const meta = JSON.parse(fs.readFileSync(metaPath, "utf8"));
    meta.id = "mtg_DIFFERENT";
    fs.writeFileSync(metaPath, JSON.stringify(meta, null, 2));
    const issues = validateMeetingFolder(tmp);
    assert.ok(issues.some((i) => i.includes("does not match")));
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});
