import { test } from "node:test";
import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import { run } from "../src/main.ts";
import {
  readMeetingFolder,
  validateMeetingFolder,
  notesAnnotations,
  type NotesModel,
} from "../../muesli-format/src/index.ts";

const fixturesDir = path.resolve(import.meta.dirname, "../../../fixtures");
const standupFixture = path.join(fixturesDir, "2026-08-12 1000 Weekly Standup");

// A mock model that returns a template-shaped body with valid span citations.
const mockModel: NotesModel = async (req) => {
  assert.ok(req.system.includes("Ground every statement"), "system prompt is the real one");
  assert.ok(req.user.includes("# Transcript"), "user message carries the transcript");
  return {
    body:
      "## Summary\n\n" +
      'Release slipped to Friday over the FreeType regression. <!--m:{"spans":[[1,6]]}-->\n\n' +
      "## Action items\n\n" +
      '- [ ] Dana: fix the atlas invalidation bug. <!--m:{"spans":[[7,7]]}-->',
    model: "mock-model",
    inputTokens: 1000,
    outputTokens: 100,
  };
};

test("new → enhance (mock) → validate produces a clean folder", async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-cli-"));
  try {
    // Create a captured folder by copying the fixture's capture artifacts.
    const dir = path.join(tmp, "meeting");
    fs.mkdirSync(dir);
    for (const f of ["meeting.json", "transcript.jsonl", "raw-notes.md"]) {
      fs.copyFileSync(path.join(standupFixture, f), path.join(dir, f));
    }
    // Strip the fixture's pre-existing enhancement record.
    const metaPath = path.join(dir, "meeting.json");
    const meta = JSON.parse(fs.readFileSync(metaPath, "utf8"));
    delete meta.enhancement;
    fs.writeFileSync(metaPath, JSON.stringify(meta, null, 2));

    const code = await run(["enhance", dir, "--template", "default"], mockModel);
    assert.equal(code, 0);

    const folder = readMeetingFolder(dir);
    assert.ok(folder.notes, "notes.md written");
    assert.equal(folder.notes!.frontMatter.id, "mtg_01J8Z0K3D9");
    assert.equal(folder.notes!.frontMatter.title, "Weekly Standup");
    assert.deepEqual(folder.notes!.frontMatter.attendees, ["Dana Kim", "Luis Ortega"]);
    assert.equal(notesAnnotations(folder.notes!).length, 2);

    const enhancement = folder.meta.enhancement as Record<string, unknown>;
    assert.equal(enhancement.model, "mock-model");
    assert.equal(enhancement.template, "default");
    assert.equal(enhancement.promptVersion, "2026-08-16");
    assert.equal(enhancement.inputTokens, 1000);

    assert.deepEqual(validateMeetingFolder(dir), []);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("enhance rejects a folder with no transcript", async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-cli-nt-"));
  try {
    const code = await run(["new", tmp, "--title", "Empty", "--start", "2026-03-01T09:00:00Z"]);
    assert.equal(code, 0);
    const dir = path.join(tmp, "2026-03-01 0900 Empty");
    assert.ok(fs.existsSync(path.join(dir, "meeting.json")));
    assert.equal(await run(["enhance", dir], mockModel), 1);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("enhancement failing validation is reported (bad spans from model)", async () => {
  const badModel: NotesModel = async () => ({
    body: '## Summary\n\nStuff happened. <!--m:{"spans":[[50,60]]}-->',
    model: "mock-bad",
  });
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "muesli-cli-bad-"));
  try {
    const dir = path.join(tmp, "meeting");
    fs.mkdirSync(dir);
    for (const f of ["meeting.json", "transcript.jsonl", "raw-notes.md"]) {
      fs.copyFileSync(path.join(standupFixture, f), path.join(dir, f));
    }
    const code = await run(["enhance", dir], badModel);
    assert.equal(code, 1);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("validate command on a golden fixture", async () => {
  assert.equal(await run(["validate", standupFixture]), 0);
});
