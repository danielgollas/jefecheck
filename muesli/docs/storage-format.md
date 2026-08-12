# Muesli Storage Format v1

This is the interoperability contract between all Muesli platforms (and any third-party tool). **The folder is the database.** Any client that reads/writes this format correctly is a full citizen; the format is designed to remain human-legible with no Muesli software at all.

## Root layout

```
Muesli/                                  ← user-chosen location (iCloud Drive, Dropbox, local, …)
├── _templates/
│   ├── default.md
│   └── sales-call.md
├── _glossary.md                         ← names/jargon, one term per line, optional "= expansion"
├── 2026/
│   └── 08/
│       └── 2026-08-12 1000 Weekly Standup/
│           ├── meeting.json             ← metadata (see below)
│           ├── notes.md                 ← THE deliverable; user-editable
│           ├── raw-notes.md             ← what the user typed live (immutable after meeting)
│           ├── transcript.md            ← human-readable transcript (rendered)
│           ├── transcript.jsonl         ← machine transcript (source for rendering)
│           └── audio.m4a                ← optional per retention policy
└── .muesli/                             ← per-folder config (format version marker)
    └── format.json                      ← {"format": "muesli", "version": 1}
```

- Folder name: `YYYY-MM-DD HHMM <title>` (24h local time; title sanitized for filesystems, ≤ 80 chars).
- Year/month nesting keeps directories browsable at scale and plays well with sync clients.
- Everything except `notes.md` is written once and treated as immutable (edits to transcripts/speaker names rewrite the whole file atomically).

## `meeting.json`

```json
{
  "muesliVersion": 1,
  "id": "mtg_01J8Z0K3D9",
  "title": "Weekly Standup",
  "startedAt": "2026-08-12T10:00:03-07:00",
  "endedAt": "2026-08-12T10:31:40-07:00",
  "pauses": [{"t0": 601.2, "t1": 645.0}],
  "attendees": [{"name": "Dana Kim", "email": "dana@x.com", "source": "calendar"}],
  "calendarEventId": "…",
  "capture": {"platform": "ios", "source": "mic", "sampleRate": 16000},
  "asr": {"engine": "apple-speechanalyzer", "locale": "en-US", "meanConfidence": 0.93},
  "enhancement": {
    "model": "claude-haiku-4-5",
    "template": "default",
    "promptVersion": "2026-08-01",
    "inputTokens": 13400, "outputTokens": 1520,
    "costUSD": 0.021
  },
  "audioRetention": "30d",
  "tags": ["standup"]
}
```

Unknown keys must be preserved by all writers (forward compatibility).

## `notes.md`

Markdown with YAML front matter. Obsidian-compatible.

```markdown
---
muesli: 1
id: mtg_01J8Z0K3D9
title: Weekly Standup
date: 2026-08-12
attendees: [Dana Kim, Luis Ortega]
tags: [standup]
---

# Weekly Standup

## Summary
Shipping slipped to Friday due to the FreeType regression… <!--m:{"spans":[[12,58],[240,251]]}-->

## Decisions
- Ship v1.7.0 Friday, not Wednesday. <!--m:{"spans":[[240,251]]}-->

## Action items
- [ ] Dana: fix atlas invalidation bug <!--m:{"spans":[[102,110]]}-->
```

- **Traceability spans** are HTML comments (`<!--m:{…}-->`) referencing utterance index ranges in `transcript.jsonl`. Invisible in every Markdown renderer; Muesli renders them as tap-to-source. Editors that strip comments simply lose traceability, not content.
- Action items use standard Markdown task syntax so other tools can pick them up.

## `raw-notes.md`

The user's live notes, verbatim, with per-paragraph timestamps in the same comment convention (`<!--m:{"t":734.2}-->`). Never modified after the meeting ends — it's the audit trail of what the human actually observed.

## `transcript.jsonl`

One utterance per line, ordered by `t0`:

```json
{"i": 0, "t0": 0.4, "t1": 6.1, "speaker": "S1", "text": "Okay let's get started…", "conf": 0.95}
```

- `i` is the stable index that `notes.md` spans reference.
- `speaker` is `S1`, `S2`, … with a `speakers` map in `meeting.json` once the user names them.
- Re-transcription (e.g. cloud escalation) writes a new file atomically and bumps `asr` in `meeting.json`; spans in existing notes reference the transcript version noted in `enhancement`.

## Sync & concurrency rules

1. **Single-writer files**: everything except `notes.md` is written by the capturing device only.
2. `notes.md` conflicts resolve as sync-service conflict copies; Muesli surfaces them with a merge UI (line-level, aided by front-matter `id`).
3. Writers must write-temp-then-rename (atomic on APFS/ext4; best-effort on synced folders).
4. The search index, embeddings, and any cache live **outside** the notes folder and are always rebuildable from it.

## Versioning

- `muesliVersion`/`muesli: 1` markers in every artifact.
- Readers must accept unknown keys; writers must never remove keys they don't understand.
- Breaking changes bump the version and ship with a migrator that rewrites folders in place (files-first makes migration inspectable and reversible via the sync service's/OS's file history).

## Golden fixtures

`fixtures/` in the repo contains canonical meeting folders (tiny audio, real transcripts, expected parse results). Every platform implementation (Swift, TypeScript, Kotlin) must pass the same fixture suite — this is the mechanism that keeps five platforms honest about one format.
