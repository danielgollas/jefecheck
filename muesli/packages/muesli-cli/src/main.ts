// muesli-cli — drive the Muesli pipeline from a terminal (M0 development tool).
//
// Commands:
//   muesli-cli new <dir> --title "..." [--start 2026-08-12T10:00:03-07:00]
//       Create an empty captured-meeting folder (meeting.json + empty raw notes).
//   muesli-cli enhance <meeting-folder> [--template default] [--model ...] [--dry-run]
//       Run the enhancement call on a captured folder (transcript + raw notes
//       present) and write notes.md + the meeting.json enhancement record.
//       --dry-run prints the assembled request instead of calling the API.
//   muesli-cli validate <meeting-folder>
//       Check a folder against the storage format; exit 1 on issues.
//
// ASR is intentionally absent here: transcription is on-device work
// (SpeechAnalyzer / whisper.cpp) that lands with the platform apps. This CLI
// exercises everything downstream of the transcript.

import * as fs from "node:fs";
import * as path from "node:path";
import {
  readMeetingFolder,
  writeMeetingFolder,
  validateMeetingFolder,
  folderName,
  enhanceFolder,
  chooseModel,
  anthropicNotesModel,
  buildEnhancementRequest,
  type NotesModel,
} from "../../muesli-format/src/index.ts";

const muesliRoot = path.resolve(import.meta.dirname, "../../..");

export async function run(argv: string[], modelOverride?: NotesModel): Promise<number> {
  const [command, target, ...rest] = argv;
  const flags = parseFlags(rest);

  switch (command) {
    case "new":
      return cmdNew(target, flags);
    case "enhance":
      return await cmdEnhance(target, flags, modelOverride);
    case "validate":
      return cmdValidate(target);
    default:
      console.error(
        "usage: muesli-cli <new|enhance|validate> <dir> [--title t] [--start iso] [--template id] [--model id] [--dry-run]",
      );
      return 2;
  }
}

function cmdNew(parentDir: string, flags: Record<string, string | boolean>): number {
  const title = typeof flags.title === "string" ? flags.title : "Meeting";
  const start =
    typeof flags.start === "string" ? flags.start : new Date().toISOString().slice(0, 19) + "Z";
  const dir = path.join(parentDir, folderName(start, title));
  writeMeetingFolder(dir, {
    meta: {
      muesliVersion: 1,
      id: `mtg_${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`,
      title,
      startedAt: start,
      pauses: [],
      attendees: [],
      capture: { platform: "cli", source: "import" },
      tags: [],
    },
    rawNotes: { text: "" },
  });
  console.log(dir);
  return 0;
}

async function cmdEnhance(
  dir: string,
  flags: Record<string, string | boolean>,
  modelOverride?: NotesModel,
): Promise<number> {
  const folder = readMeetingFolder(dir);
  if (!folder.transcript || folder.transcript.length === 0) {
    console.error("enhance: folder has no transcript.jsonl");
    return 1;
  }

  const templateId = typeof flags.template === "string" ? flags.template : "default";
  const promptFile = fs.readFileSync(path.join(muesliRoot, "prompts/enhance-v1.md"), "utf8");
  const template = fs.readFileSync(path.join(muesliRoot, `templates/${templateId}.md`), "utf8");
  const glossaryPath = path.join(path.dirname(dir), "_glossary.md");
  const glossary = fs.existsSync(glossaryPath) ? fs.readFileSync(glossaryPath, "utf8") : undefined;

  if (flags["dry-run"]) {
    const request = buildEnhancementRequest({
      promptFile,
      template,
      glossary,
      transcript: folder.transcript,
      speakerNames: (folder.meta.speakers ?? {}) as Record<string, string>,
      rawNotesText: folder.rawNotes?.text ?? "(none)",
    });
    console.log(`--- system ---\n${request.system}\n--- user ---\n${request.user}`);
    return 0;
  }

  let model = modelOverride;
  if (!model) {
    const apiKey = process.env.ANTHROPIC_API_KEY;
    if (!apiKey) {
      console.error("enhance: ANTHROPIC_API_KEY is not set (or pass --dry-run)");
      return 2;
    }
    const modelId =
      typeof flags.model === "string" ? flags.model : chooseModel(folder.transcript);
    model = anthropicNotesModel({ apiKey, model: modelId });
  }

  const enhanced = await enhanceFolder(folder, {
    promptFile,
    template,
    templateId,
    glossary,
    model,
  });
  writeMeetingFolder(dir, enhanced);

  const issues = validateMeetingFolder(dir);
  if (issues.length > 0) {
    console.error(`enhance: output failed validation:\n  ${issues.join("\n  ")}`);
    return 1;
  }
  console.log(path.join(dir, "notes.md"));
  return 0;
}

function cmdValidate(dir: string): number {
  const issues = validateMeetingFolder(dir);
  if (issues.length === 0) {
    console.log("ok");
    return 0;
  }
  for (const issue of issues) console.error(issue);
  return 1;
}

function parseFlags(args: string[]): Record<string, string | boolean> {
  const flags: Record<string, string | boolean> = {};
  for (let n = 0; n < args.length; n++) {
    if (!args[n].startsWith("--")) continue;
    const name = args[n].slice(2);
    if (n + 1 < args.length && !args[n + 1].startsWith("--")) {
      flags[name] = args[n + 1];
      n++;
    } else {
      flags[name] = true;
    }
  }
  return flags;
}

// Only run as a script when invoked directly (not when imported by tests).
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
  process.exit(await run(process.argv.slice(2)));
}
