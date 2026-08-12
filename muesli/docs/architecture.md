# Muesli — Architecture

The pipeline is: **Capture → Transcribe (local) → Enhance (one AI call) → Store (files)**. Each stage is chosen against the priority order: accurate → quick → cheap → local → files-first.

```
 mic / system audio / tab audio
        │
        ▼
 ┌──────────────┐   streaming    ┌──────────────────┐
 │   Capture    │ ─────────────► │  ASR (on-device)  │──► live transcript UI
 │ (per-platform)│               │ SpeechAnalyzer /  │
 └──────────────┘                │ whisper.cpp       │
        │                        └────────┬─────────┘
        │ audio.m4a                       │ transcript.jsonl
        ▼                                 ▼
 ┌────────────────────────────────────────────────────┐
 │  Meeting folder (source of truth, plain files)     │
 │  raw-notes.md · transcript.jsonl · audio.m4a       │
 └────────────────────────┬───────────────────────────┘
                          │ on Stop: one enhancement call
                          ▼
              ┌───────────────────────┐
              │  Notes model           │  default: claude-haiku-4-5
              │  (cloud, cheap tier;   │  escalate: claude-sonnet-5
              │   or local model)      │  local opt-in: on-device LLM
              └───────────┬───────────┘
                          ▼
                     notes.md  (+ traceability spans)
```

## 1. Capture (per platform)

| Platform | In-person / speakerphone | Other party of a call | Notes |
|---|---|---|---|
| iPhone | ✅ mic (`AVAudioEngine`) | via speaker + mic | iOS forbids third-party capture of other apps' audio; mic-on-table is the model (same as Granola iOS). Background recording via audio background mode. |
| iPad | ✅ mic | via speaker + mic | Identical code to iPhone. |
| Web | ✅ mic (`getUserMedia`) | ✅ browser-tab audio (`getDisplayMedia` tab capture, Chrome/Edge) | Tab capture covers Meet/Zoom-web/Teams-web. Safari lacks tab audio → mic only. |
| Android | ✅ mic | partial (`AudioPlaybackCapture`, app-permitting) | Many meeting apps opt out of playback capture; mic is the reliable path. |
| Mac | ✅ mic | ✅ system audio (Core Audio process taps, macOS 14.2+) | The best capture platform: both sides of any call, no bot. Ships last per platform priority, but the capture layer is designed for it from day one. |

Capture writes AAC (`audio.m4a`, 32 kbps mono is plenty for speech) and feeds 16 kHz PCM to the ASR stage. Pause/resume produces one logical timeline (gaps recorded in metadata).

## 2. Transcription — local first (priorities 3 & 4)

**Primary: Apple SpeechAnalyzer / SpeechTranscriber** (iOS 26+, iPadOS 26+, macOS 26+)
- On-device, free, fast (Neural Engine), streaming partial results, word timestamps, automatic language detection, custom-vocabulary biasing.
- This is the default engine on Apple platforms — zero marginal cost satisfies priority 3, on-device satisfies 4, and quality is strong enough for priority 1 in typical meeting audio.

**Fallback: whisper.cpp** (bundled, all platforms)
- For older OS versions, Android, and the web-adjacent desktop cases. Model tier by hardware: `large-v3-turbo` (Apple Silicon Macs, recent iPhones), `small`/`base` (older devices, with the cloud-escalation option surfaced).
- Android: whisper.cpp via JNI (or `whisper-android`); web: transcription happens client-side only on capable machines (WASM whisper for short clips), otherwise the web app is primarily a viewer/editor and defers transcription to a paired device or opt-in cloud ASR.

**Opt-in cloud ASR escalation** (off by default)
- When the local engine reports low confidence (noisy room, heavy accents, poor hardware), offer "Re-transcribe in the cloud for higher accuracy". Provider abstraction (`AsrProvider` interface) so we can use whichever hosted engine wins on price/accuracy at the time (~$0.10–0.30 per audio-hour). Accuracy (priority 1) is allowed to spend money (priority 3) — but only with user consent and only when the local result is measurably weak.

**Diarization** (phase 2): on-device speaker segmentation (e.g. FluidAudio/sherpa-onnx pipelines on Apple platforms). Speaker turns stored in `transcript.jsonl`; labels are user-editable.

Transcript format: JSON Lines, one utterance per line: `{t0, t1, speaker?, text, conf}`. A human-readable `transcript.md` is rendered from it for the folder.

## 3. Enhancement — one cheap LLM call (priorities 1 & 3)

**The whole AI bill is one call per meeting.** No background summarization, no per-utterance calls, no embeddings by default.

**Inputs**: system prompt (static, cached) + template (cached) + user glossary + transcript + user's timestamped raw notes.
**Output**: structured notes per the template, with transcript-span references for traceability, plus a title and extracted action items.

### Model selection (cost-tiered, accuracy-gated)

| Tier | Model | When | Est. cost, 1-hr meeting¹ |
|---|---|---|---|
| Default | `claude-haiku-4-5` ($1/$5 per MTok) | Meetings ≤ ~45 min transcript, standard templates | ~$0.02 |
| Escalation | `claude-sonnet-5` ($3/$15; intro $2/$10) | Long/multi-speaker meetings, complex templates, or user hits "Improve these notes" | ~$0.06 |
| Local (opt-in) | On-device model (Apple Foundation Models / small local LLM) | Fully-local mode; short meetings | $0 |

¹ ~1 hr ≈ 9k words ≈ 13k input tokens + ~1.5k output tokens.

- **Prompt caching**: system prompt + template are a stable prefix with `cache_control` — repeat meetings pay ~0.1× on that prefix. The transcript (volatile) comes last.
- **Structured outputs** (`output_config.format`) guarantee parseable results — no retry loops burning tokens.
- **Batch API** (50% off) for non-interactive work: re-processing old meetings after a template change, backfilling imported recordings.
- **On-device micro-tasks are free**: meeting title suggestion, live action-item detection, and glossary extraction run on Apple Foundation Models (iOS 26+) where available — the cloud call is reserved for the one job that needs real quality.
- The provider layer is abstracted (`NotesModel` interface) so the cheapest-model-meeting-the-accuracy-bar can be re-selected as pricing moves; accuracy evals (a fixed set of recorded test meetings + rubric) gate any model downgrade.

### Accuracy guardrails in the prompt

- Grounding: "Use only the transcript and the user's notes. If something is unclear in both, omit it or mark it ⚠︎ uncertain. Never invent names, numbers, or commitments."
- User-note precedence with discrepancy flags.
- Output spans: each section lists supporting transcript utterance ranges (drives tap-to-source in the UI).

## 4. Storage & sync (priority 5)

**Files are the database.** Full spec in `storage-format.md`. Summary:

- One folder per meeting: `Muesli/2026/08/2026-08-12 1000 Weekly Standup/` containing `notes.md`, `raw-notes.md`, `transcript.md`, `transcript.jsonl`, `meeting.json`, `audio.m4a`.
- The Muesli root folder lives wherever the user says: **iCloud Drive (default on Apple platforms)**, a Google Drive/Dropbox-synced folder, or a purely local directory. Muesli is a client over files, so any folder-sync service works without Muesli knowing about it.
- A local **SQLite FTS5 index** (in app support, not in the notes folder) powers instant search. It is derived and rebuildable — deleting it loses nothing.
- Conflict policy: last-writer-wins per file with conflict copies (`notes (conflict 2026-08-12).md`), same as file-sync tools; `notes.md` is the only file two devices plausibly edit.
- **Web access**: the web app reads the folder via cloud-storage APIs (Drive/Dropbox OAuth) or, for iCloud users, via a thin optional sync service that speaks the same on-disk format (end-to-end encrypted blobs; the service never needs plaintext for sync — search on web uses a client-side index). The sync service is an *optional accessory*, never the source of truth.

## 5. Shared code strategy

- **`MuesliCore` (Swift package)**: models, storage format read/write, capture/ASR/enhancement orchestration, prompt assembly, FTS indexing. Used by iPhone, iPad, and Mac targets (one SwiftUI multiplatform app).
- **`muesli-format` (TypeScript package)**: storage-format reader/writer for the web app; shares golden-file tests with MuesliCore.
- **Android**: Kotlin implementation of the format + capture; evaluate Kotlin Multiplatform for the core once the Swift core stabilizes — the *format spec + golden test fixtures* are the real cross-platform contract, deliberately, so ports stay honest.
- The enhancement prompt + templates are data (versioned files in the repo), not code, so all platforms produce identical notes quality.

## Key risks

| Risk | Mitigation |
|---|---|
| iOS can't capture the other side of calls | Product framing: mic-on-table / speakerphone (Granola iOS proves this works); Mac milestone adds full system-audio capture |
| Local ASR quality on low-end hardware | Confidence-triggered cloud escalation (opt-in); model-tier ladder |
| iCloud Drive sync latency/opacity | Format tolerates delayed sync (append-only artifacts, single-writer files); conflict copies |
| LLM hallucination in notes | Grounded prompt, span traceability, user-note precedence, eval suite gating model changes |
| Cost creep from chat-with-meeting | On-demand only, transcript-scoped context, cached prefix, Haiku default |
