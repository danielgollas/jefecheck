# Muesli

**A Granola-style AI note taker that respects your time, your wallet, and your files.**

Muesli listens to your meetings, transcribes them on your own hardware, and blends the sparse notes you type with the full transcript into polished, accurate meeting notes — stored as plain files you own, in a folder you choose.

## Priorities (in order)

Every design decision resolves conflicts by this ranking:

1. **Accurate** — Notes must be trustworthy. Never hallucinate attendees, decisions, or numbers. When in doubt, quote the transcript.
2. **Quick** — Live transcription during the meeting; finished notes within seconds of the meeting ending.
3. **Minimize AI cost** — Target well under $0.05 per meeting-hour of cloud AI spend. Local ASR is free; one LLM call per meeting.
4. **Local hardware where appropriate** — On-device speech recognition (Apple Neural Engine), on-device models for micro-tasks. Cloud only where it measurably improves accuracy or speed.
5. **Own-your-data storage** — Notes are Markdown files in a folder structure. Sync via iCloud Drive, Google Drive, Dropbox, or keep them purely local. No proprietary database as source of truth.

When priorities conflict, the higher one wins: e.g. if a local model produces worse notes than a cloud model, accuracy beats cost and we use the cloud model (but we pick the cheapest cloud model that meets the accuracy bar).

## Platforms (in order of delivery)

1. **iPhone** — primary capture device: in-person meetings, calls on speaker
2. **iPad** — same app, adaptive layout; great for typing notes during meetings
3. **Web** — view/search/edit notes anywhere; capture browser-tab meetings via extension
4. **Android** — Kotlin port of the capture + notes app
5. **Mac** — native app with system-audio capture (both sides of video calls, no bot)

iPhone, iPad, and Mac share a Swift codebase. The **storage format spec** (`docs/storage-format.md`) is the contract that keeps all five platforms interoperable.

## How it works (the Granola model)

1. **You take sparse notes** during the meeting — bullet points, fragments, half-thoughts.
2. **Muesli records and transcribes locally** the whole time (with visible recording indicator and consent affordances).
3. **When the meeting ends**, one AI call merges your notes with the transcript: your notes provide structure and emphasis; the transcript provides accuracy and completeness.
4. **The result is a Markdown file** in your notes folder — plus the raw transcript and your original notes, preserved separately.

No bot joins your call. No audio leaves your device by default.

## Documents

| Doc | Contents |
|---|---|
| [`docs/product-spec.md`](docs/product-spec.md) | What Muesli does: features, flows, non-goals |
| [`docs/architecture.md`](docs/architecture.md) | Capture, ASR, LLM pipeline, per-platform tech choices |
| [`docs/storage-format.md`](docs/storage-format.md) | The on-disk format — the interoperability contract |
| [`docs/roadmap.md`](docs/roadmap.md) | Milestones M0–M5 |

## Repository layout

```
muesli/
├── docs/                     Founding spec (product, architecture, format, roadmap)
├── fixtures/                 Golden meeting folders — the cross-platform contract
├── packages/
│   ├── muesli-format/        TypeScript reference implementation of the format + enhancement pipeline
│   ├── muesli-cli/           Dev CLI: new / enhance / validate meeting folders (everything downstream of ASR)
│   └── MuesliCore/           Swift package: format + enhancement pipeline (capture/ASR come with the apps)
├── prompts/                  Enhancement prompt (versioned data, shared by all platforms)
├── templates/                Built-in note templates
└── eval/                     Accuracy eval harness — gates all model/prompt changes
```

CI (`.github/workflows/muesli.yml`) runs both format implementations against the
same golden fixtures on every change under `muesli/`.

## Status

M0 (foundations) largely complete: storage format v1 implemented and tested in
TypeScript and Swift; the enhancement pipeline (prompt assembly, model
selection/escalation, notes generation, validation) implemented in both
languages with a dev CLI; eval harness in place with six bootstrap cases.
Remaining for M0: grow the eval set toward ~20 cases (real consented meetings
preferred) and run a first live eval. Then M1: the iPhone app, where capture
and on-device ASR live.
