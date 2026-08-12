# Muesli — Product Spec

## One-sentence pitch

Type half-thoughts during your meeting; get complete, accurate notes the moment it ends — transcribed on your device, enhanced by AI for about a cent, saved as Markdown you own.

## Target user

People who sit in meetings and are expected to remember what happened: founders, managers, consultants, salespeople, researchers, students. They already half-take notes; Muesli makes those notes whole.

## Core loop

### 1. Start

- Open Muesli, tap **Record** (or auto-prompt from a calendar event: "Standup starts in 1 min — record?").
- A meeting gets a working folder immediately: title (from calendar or "Meeting — Aug 12, 10:00"), attendees (from calendar invite if available), start time.
- Recording indicator is always visible. One tap pauses/stops.

### 2. During the meeting

- **Live transcript** streams in a collapsible pane (on-device ASR). Confidence-styled: low-confidence words rendered dimmed.
- **Notes pane** is the main surface: a plain Markdown editor. The user types fragments. Timestamps are silently attached to each paragraph the user writes, so the AI can align notes to transcript regions.
- Optional quick actions: mark a moment ⭐ ("this matters"), tag `#decision`, `#todo`.
- Everything works offline. Nothing requires the network during capture.

### 3. End

- Tap **Stop**. Muesli immediately:
  1. Finalizes the transcript (local).
  2. Sends transcript + user notes + template to the notes model (one cloud call, or local model if configured).
  3. Streams the enhanced notes into the editor within seconds.
- The user reviews. Every AI-generated claim can be traced: tap a sentence → jump to the transcript region (and audio timestamp) that supports it.

### 4. After

- Notes live in the notes folder as Markdown (see `storage-format.md`). Editable forever, in Muesli or any text editor.
- **Ask this meeting**: on-demand chat over a single meeting's transcript (a deliberate extra cost the user invokes, not a background one).
- Full-text search across all meetings (local index).

## Templates

The enhancement call is template-driven. Built-ins:

- **Default** — Summary, Discussion, Decisions, Action items
- **1:1** — Updates, Feedback, Growth topics, Follow-ups
- **Sales call** — Prospect context, Pain points, Objections, Next steps
- **Interview** — Candidate background, Signals, Concerns, Verdict prompts
- **Lecture/Class** — Key concepts, Examples, Questions to review

Users can create templates: a template is just a Markdown file with section headings and per-section guidance (stored in the notes folder under `_templates/`, so they sync like everything else).

## Accuracy features (priority #1)

- **Notes are grounded**: the enhancement prompt instructs the model to use *only* the transcript and the user's notes; anything uncertain is marked or omitted. No invented attendees, dates, or figures.
- **Traceability**: enhanced-note sections carry hidden transcript-span references; tapping reveals the source.
- **User notes win**: where the user's note and the transcript conflict, the user's note is kept and the discrepancy flagged.
- **Names & jargon dictionary**: per-user glossary (auto-built from calendar attendees + corrections) is fed to both ASR biasing (where the engine supports it) and the enhancement prompt.
- **Speaker labels** (phase 2): on-device diarization; user can rename "Speaker 1" → "Dana" once and it applies across the meeting.

## Consent & privacy

- Persistent recording indicator; optional spoken/visible "this meeting is being recorded" reminder card the user can show.
- Audio never leaves the device unless the user enables cloud ASR fallback or cloud audio backup — both off by default.
- What *does* go to the cloud by default: the transcript text and the user's typed notes, for the single enhancement call. A visible setting explains exactly this. A fully-local mode (on-device enhancement model) is available with an accuracy caveat.
- Audio retention is user-controlled: keep forever / keep 30 days / delete after transcription (default: keep 30 days).

## Non-goals (v1)

- No meeting bot that joins calls (ever — it's against the product's character).
- No team/workspace features, sharing links, or comments (post-v1).
- No CRM integrations (post-v1; the file format makes them easy later).
- No real-time translation.
- No video capture.

## Success criteria

- A user's typed notes + Muesli output is judged more accurate and more complete than their typed notes alone, in >90% of meetings (self-reported + spot audits).
- Notes ready < 15 seconds after tapping Stop for a 60-minute meeting (on-device ASR keeps up in real time; enhancement call is the only post-meeting latency).
- Median cloud AI cost per meeting-hour < $0.03; p95 < $0.10.
- A user can point any Markdown app (Obsidian, iA Writer, VS Code) at their notes folder and everything is legible.
