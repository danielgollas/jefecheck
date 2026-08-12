# Muesli — Roadmap

Milestones ship in platform-priority order (iPhone → iPad → web → Android → Mac). Each milestone is releasable.

## M0 — Foundations (no UI)

- Storage format v1 finalized + golden fixtures (`fixtures/`)
- `MuesliCore` Swift package: format read/write, meeting folder lifecycle, FTS5 index
- Enhancement prompt v1 + template files + **accuracy eval harness**: ~20 recorded test meetings with reference notes; rubric scoring (grounding, completeness, action-item recall). This harness gates every future model/prompt change.
- Model provider abstraction with Haiku 4.5 default, Sonnet 5 escalation, prompt caching, structured outputs

**Exit criteria**: `muesli-cli record.m4a` → correct meeting folder with notes, on fixture audio; eval suite green.

## M1 — iPhone MVP (first ship)

- SwiftUI app: record screen (live transcript + notes editor), meeting list, meeting view with tap-to-source
- On-device ASR: SpeechAnalyzer (iOS 26+), whisper.cpp fallback (iOS 17+)
- One-call enhancement on Stop; offline queue (enhance when back online)
- Notes folder in iCloud Drive (default) or local; Files-app visible
- Calendar read integration (titles, attendees, "meeting starting" prompt)
- Audio retention settings; consent affordances

**Exit criteria**: daily-drivable for real meetings; product-spec latency and cost targets met.

## M2 — iPad + polish

- Adaptive layout (side-by-side transcript/notes), keyboard shortcuts, Apple Pencil scribble in notes
- Custom templates UI (`_templates/`), glossary UI
- Speaker diarization v1 + rename UI
- "Ask this meeting" chat (on-demand, transcript-scoped)

## M3 — Web

- `muesli-format` TypeScript package passing golden fixtures
- Read/search/edit web app over Drive/Dropbox APIs; optional E2E-encrypted sync service for iCloud-based users
- Chrome extension: tab-audio capture for browser meetings (client-side or paired-device transcription)

## M4 — Android

- Kotlin app: capture (mic + AudioPlaybackCapture where permitted), whisper.cpp ASR, format implementation passing fixtures
- Feature parity with M1 iPhone scope

## M5 — Mac

- SwiftUI multiplatform target reusing MuesliCore and the iOS UI where sensible
- **System-audio capture** (Core Audio taps): both sides of any call app, no bot
- Menu-bar quick record; auto-detect meeting apps in the foreground

## Post-v1 candidates

- Cross-meeting search Q&A (local embeddings, opt-in)
- Sharing (export bundle / publish single note)
- Team spaces
- CRM/task-manager integrations (files-first makes these thin)
- Live translation

## Standing rules

- **No model/prompt change ships without beating the eval suite.**
- **No feature adds a recurring per-meeting AI cost** beyond the one enhancement call without explicit product sign-off.
- **The format spec changes only with a version bump + migrator + fixture updates on all platforms.**
