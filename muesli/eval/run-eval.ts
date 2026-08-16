// Muesli notes eval runner.
//
// Usage:
//   ANTHROPIC_API_KEY=... node --experimental-strip-types run-eval.ts [--model claude-haiku-4-5]
//
// Each case under cases/<name>/ contains:
//   transcript.jsonl     required — meeting transcript (storage-format.md)
//   raw-notes.md         required — the participant's live notes
//   reference-notes.md   required — human-written reference for grading
//   glossary.md          optional — names/terms, one per line
//   template.txt         optional — template id (default: "default")
//
// The runner generates notes with the product model (default claude-haiku-4-5,
// per docs/architecture.md), then grades them with a stronger grader model
// against eval/rubric.md, and applies the ship gates from that file.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseTranscript } from "../packages/muesli-format/src/index.ts";

const API_URL = "https://api.anthropic.com/v1/messages";
const GRADER_MODEL = "claude-opus-5";
const DIMENSIONS = ["grounding", "completeness", "actionItems", "userNoteIntegration", "formatCompliance"] as const;
const WEIGHTS: Record<string, number> = {
  grounding: 2, completeness: 1, actionItems: 1, userNoteIntegration: 1, formatCompliance: 1,
};

const here = import.meta.dirname;
const apiKey = process.env.ANTHROPIC_API_KEY;
const productModel = process.argv.includes("--model")
  ? process.argv[process.argv.indexOf("--model") + 1]
  : "claude-haiku-4-5";

if (!apiKey) {
  console.error("ANTHROPIC_API_KEY is not set.");
  process.exit(2);
}

const systemPrompt = extractSystemPrompt(
  fs.readFileSync(path.join(here, "../prompts/enhance-v1.md"), "utf8"),
);
const rubric = fs.readFileSync(path.join(here, "rubric.md"), "utf8");

const casesDir = path.join(here, "cases");
const caseNames = fs.existsSync(casesDir)
  ? fs.readdirSync(casesDir).filter((n) => fs.statSync(path.join(casesDir, n)).isDirectory())
  : [];

if (caseNames.length === 0) {
  console.error("No cases found in eval/cases/ — see eval/README.md for how to add them.");
  process.exit(2);
}

interface Scores { grounding: number; completeness: number; actionItems: number; userNoteIntegration: number; formatCompliance: number }
const results: { name: string; scores: Scores }[] = [];

for (const name of caseNames) {
  const dir = path.join(casesDir, name);
  const transcript = parseTranscript(fs.readFileSync(path.join(dir, "transcript.jsonl"), "utf8"));
  const rawNotes = fs.readFileSync(path.join(dir, "raw-notes.md"), "utf8");
  const reference = fs.readFileSync(path.join(dir, "reference-notes.md"), "utf8");
  const glossary = readOptional(path.join(dir, "glossary.md")) ?? "(empty)";
  const templateId = (readOptional(path.join(dir, "template.txt")) ?? "default").trim();
  const template = fs.readFileSync(path.join(here, `../templates/${templateId}.md`), "utf8");

  const transcriptText = transcript
    .map((u) => `[${u.i}] ${u.speaker ?? "?"}: ${u.text}`)
    .join("\n");

  process.stderr.write(`${name}: generating (${productModel})…`);
  const notes = await generateNotes({ template, glossary, transcriptText, rawNotes });
  process.stderr.write(" grading…");
  const scores = await gradeNotes({ notes, transcriptText, rawNotes, reference });
  process.stderr.write(" done\n");
  results.push({ name, scores });
}

report(results);

// ---------------------------------------------------------------------------

async function generateNotes(input: {
  template: string; glossary: string; transcriptText: string; rawNotes: string;
}): Promise<string> {
  const body = {
    model: productModel,
    max_tokens: 4096,
    system: [
      { type: "text", text: systemPrompt, cache_control: { type: "ephemeral" } },
    ],
    messages: [{
      role: "user",
      content:
        `# Template\n${input.template}\n\n# Glossary\n${input.glossary}\n\n` +
        `# Transcript\n${input.transcriptText}\n\n# Raw notes\n${input.rawNotes}`,
    }],
  };
  const msg = await callApi(body);
  return msg.content.filter((b: any) => b.type === "text").map((b: any) => b.text).join("");
}

async function gradeNotes(input: {
  notes: string; transcriptText: string; rawNotes: string; reference: string;
}): Promise<Scores> {
  const schema = {
    type: "object",
    properties: Object.fromEntries(
      DIMENSIONS.map((d) => [d, { type: "integer", enum: [1, 2, 3, 4, 5] }]),
    ),
    required: [...DIMENSIONS],
    additionalProperties: false,
  };
  const body = {
    model: GRADER_MODEL,
    max_tokens: 8192,
    output_config: { format: { type: "json_schema", schema } },
    messages: [{
      role: "user",
      content:
        `Score the generated meeting notes against this rubric. Be strict; grounding failures are the most serious.\n\n` +
        `# Rubric\n${input.rubric ?? rubric}\n\n# Transcript\n${input.transcriptText}\n\n` +
        `# Participant's raw notes\n${input.rawNotes}\n\n# Reference notes (human-written)\n${input.reference}\n\n` +
        `# Generated notes (to score)\n${input.notes}`,
    }],
  };
  const msg = await callApi(body);
  const text = msg.content.find((b: any) => b.type === "text")?.text ?? "{}";
  return JSON.parse(text) as Scores;
}

async function callApi(body: unknown): Promise<any> {
  const res = await fetch(API_URL, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-api-key": apiKey!,
      "anthropic-version": "2023-06-01",
    },
    body: JSON.stringify(body),
  });
  if (!res.ok) throw new Error(`API ${res.status}: ${await res.text()}`);
  const msg = await res.json();
  if (msg.stop_reason === "refusal") throw new Error("model refused the request");
  return msg;
}

function report(all: { name: string; scores: Scores }[]): void {
  const mean = (d: keyof Scores) => all.reduce((s, r) => s + r.scores[d], 0) / all.length;
  const weightedMean =
    all.reduce(
      (s, r) =>
        s +
        DIMENSIONS.reduce((a, d) => a + r.scores[d] * WEIGHTS[d], 0) /
          DIMENSIONS.reduce((a, d) => a + WEIGHTS[d], 0),
      0,
    ) / all.length;

  console.log(`\nModel: ${productModel}   Grader: ${GRADER_MODEL}   Cases: ${all.length}\n`);
  console.log("case".padEnd(30) + DIMENSIONS.map((d) => d.slice(0, 6).padStart(8)).join(""));
  for (const r of all) {
    console.log(
      r.name.padEnd(30) + DIMENSIONS.map((d) => String(r.scores[d]).padStart(8)).join(""),
    );
  }
  console.log("\nmeans: " + DIMENSIONS.map((d) => `${d}=${mean(d).toFixed(2)}`).join("  "));
  console.log(`weighted mean: ${weightedMean.toFixed(2)}`);

  // Gates from rubric.md
  const failures: string[] = [];
  if (mean("grounding") < 4.5) failures.push("grounding mean < 4.5");
  if (all.some((r) => r.scores.grounding < 4)) failures.push("a case scored grounding < 4");
  if (weightedMean < 4.0) failures.push("weighted mean < 4.0");
  for (const d of DIMENSIONS) {
    if (mean(d) < 3.5) failures.push(`${d} mean < 3.5`);
  }

  if (failures.length > 0) {
    console.error(`\nGATE FAILED: ${failures.join("; ")}`);
    process.exit(1);
  }
  console.log("\nAll gates passed.");
}

function extractSystemPrompt(promptFile: string): string {
  // The system prompt is the content between "## System prompt" and the next "## ".
  const start = promptFile.indexOf("## System prompt");
  if (start === -1) throw new Error("prompts/enhance-v1.md: '## System prompt' section not found");
  const afterHeading = promptFile.indexOf("\n", start) + 1;
  const next = promptFile.indexOf("\n## ", afterHeading);
  return promptFile.slice(afterHeading, next === -1 ? undefined : next).trim();
}

function readOptional(p: string): string | null {
  return fs.existsSync(p) ? fs.readFileSync(p, "utf8") : null;
}
