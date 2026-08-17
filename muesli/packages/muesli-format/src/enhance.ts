// Enhancement pipeline assembly — the one AI call per meeting.
//
// This module owns everything *around* the model call: extracting the system
// prompt from prompts/enhance-v1.md, rendering the user message, choosing
// escalation, and turning the model's output into a valid notes.md +
// meeting.json enhancement record. The model itself is injected as a
// `NotesModel` function so clients, the CLI, and tests can supply the real
// API, a local model, or a mock.

import { type FMValue } from "./frontmatter.ts";
import { type Utterance } from "./transcript.ts";
import { type MeetingFolder, type Notes, type MeetingMeta } from "./folder.ts";

export interface EnhancementRequest {
  system: string;
  user: string;
}

export interface EnhancementResult {
  body: string;
  model: string;
  inputTokens?: number;
  outputTokens?: number;
}

/** A notes model: takes the assembled request, returns the Markdown body. */
export type NotesModel = (req: EnhancementRequest) => Promise<EnhancementResult>;

export const DEFAULT_MODEL = "claude-haiku-4-5";
export const ESCALATION_MODEL = "claude-sonnet-5";

/** The system prompt is the content between "## System prompt" and the next "## ". */
export function extractSystemPrompt(promptFile: string): string {
  const start = promptFile.indexOf("## System prompt");
  if (start === -1) {
    throw new Error("enhancement prompt file: '## System prompt' section not found");
  }
  const afterHeading = promptFile.indexOf("\n", start) + 1;
  const next = promptFile.indexOf("\n## ", afterHeading);
  return promptFile.slice(afterHeading, next === -1 ? undefined : next).trim();
}

export function extractPromptVersion(promptFile: string): string {
  const m = /^promptVersion:\s*(\S+)$/m.exec(promptFile);
  if (!m) throw new Error("enhancement prompt file: promptVersion not found in front matter");
  return m[1];
}

/** Render the transcript as numbered utterances the way the prompt expects. */
export function renderTranscriptForPrompt(
  utterances: Utterance[],
  speakerNames: Record<string, string> = {},
): string {
  return utterances
    .map((u) => {
      const who = u.speaker ? (speakerNames[u.speaker] ?? u.speaker) : "?";
      return `[${u.i}] ${who}: ${u.text}`;
    })
    .join("\n");
}

export function buildEnhancementRequest(input: {
  promptFile: string;
  template: string;
  glossary?: string;
  transcript: Utterance[];
  speakerNames?: Record<string, string>;
  rawNotesText: string;
}): EnhancementRequest {
  return {
    system: extractSystemPrompt(input.promptFile),
    user:
      `# Template\n${input.template}\n\n` +
      `# Glossary\n${input.glossary?.trim() || "(empty)"}\n\n` +
      `# Transcript\n${renderTranscriptForPrompt(input.transcript, input.speakerNames ?? {})}\n\n` +
      `# Raw notes\n${input.rawNotesText}`,
  };
}

/**
 * Escalation heuristic from prompts/enhance-v1.md: long transcripts and
 * many-speaker meetings go to the escalation model. (Template complexity and
 * the user's "Improve these notes" tap are client-side signals, passed via
 * `forceEscalation`.)
 */
export function chooseModel(
  transcript: Utterance[],
  opts: { forceEscalation?: boolean } = {},
): string {
  if (opts.forceEscalation) return ESCALATION_MODEL;
  const approxTokens = transcript.reduce((s, u) => s + u.text.length, 0) / 4;
  if (approxTokens > 40_000) return ESCALATION_MODEL;
  const speakers = new Set(transcript.map((u) => u.speaker).filter(Boolean));
  if (speakers.size > 4) return ESCALATION_MODEL;
  return DEFAULT_MODEL;
}

/** Build notes.md front matter from meeting metadata (storage-format.md). */
export function notesFrontMatterFromMeta(meta: MeetingMeta): Record<string, FMValue> {
  const attendees = Array.isArray(meta.attendees)
    ? (meta.attendees as { name?: string }[])
        .map((a) => a?.name)
        .filter((n): n is string => typeof n === "string")
    : [];
  const tags = Array.isArray(meta.tags) ? (meta.tags as string[]) : [];
  const startedAt = typeof meta.startedAt === "string" ? meta.startedAt : "";
  return {
    muesli: 1,
    id: String(meta.id ?? ""),
    title: String(meta.title ?? "Meeting"),
    date: startedAt.slice(0, 10),
    attendees,
    tags,
  };
}

/**
 * Run the enhancement for a captured meeting folder (must have a transcript
 * and raw notes) and return the folder with notes attached and the
 * `enhancement` record set. Pure with respect to disk — callers persist via
 * writeMeetingFolder.
 */
export async function enhanceFolder(
  folder: Omit<MeetingFolder, "dir">,
  input: {
    promptFile: string;
    template: string;
    templateId: string;
    glossary?: string;
    model: NotesModel;
  },
): Promise<Omit<MeetingFolder, "dir">> {
  if (!folder.transcript || folder.transcript.length === 0) {
    throw new Error("cannot enhance: folder has no transcript");
  }
  const speakerNames = (folder.meta.speakers ?? {}) as Record<string, string>;
  const request = buildEnhancementRequest({
    promptFile: input.promptFile,
    template: input.template,
    glossary: input.glossary,
    transcript: folder.transcript,
    speakerNames,
    rawNotesText: folder.rawNotes?.text ?? "(none)",
  });
  const result = await input.model(request);

  const notes: Notes = {
    frontMatter: notesFrontMatterFromMeta(folder.meta),
    body: result.body.endsWith("\n") ? result.body : result.body + "\n",
  };
  const enhancement: Record<string, unknown> = {
    model: result.model,
    template: input.templateId,
    promptVersion: extractPromptVersion(input.promptFile),
  };
  if (result.inputTokens !== undefined) enhancement.inputTokens = result.inputTokens;
  if (result.outputTokens !== undefined) enhancement.outputTokens = result.outputTokens;

  return {
    ...folder,
    notes,
    meta: { ...folder.meta, enhancement },
  };
}

/** The real thing: an Anthropic-API-backed NotesModel. */
export function anthropicNotesModel(opts: {
  apiKey: string;
  model: string;
  maxTokens?: number;
}): NotesModel {
  return async (req) => {
    const res = await fetch("https://api.anthropic.com/v1/messages", {
      method: "POST",
      headers: {
        "content-type": "application/json",
        "x-api-key": opts.apiKey,
        "anthropic-version": "2023-06-01",
      },
      body: JSON.stringify({
        model: opts.model,
        max_tokens: opts.maxTokens ?? 4096,
        // Stable prefix (prompt + template travels in `user`; the system
        // prompt alone is the cross-meeting stable part) gets a cache marker.
        system: [{ type: "text", text: req.system, cache_control: { type: "ephemeral" } }],
        messages: [{ role: "user", content: req.user }],
      }),
    });
    if (!res.ok) throw new Error(`enhancement API ${res.status}: ${await res.text()}`);
    const msg = (await res.json()) as {
      stop_reason: string;
      content: { type: string; text?: string }[];
      usage?: { input_tokens?: number; output_tokens?: number };
    };
    if (msg.stop_reason === "refusal") throw new Error("enhancement model refused the request");
    const body = msg.content
      .filter((b) => b.type === "text")
      .map((b) => b.text ?? "")
      .join("");
    return {
      body,
      model: opts.model,
      inputTokens: msg.usage?.input_tokens,
      outputTokens: msg.usage?.output_tokens,
    };
  };
}
