# Review summary export — design

**Date:** 2026-09-14
**Branch:** `feature/review-export` (from `feature/annotations`)
**Builds on:** `2026-09-07-annotations-design.md` (JEF-39 notes), JEF-41 note stamping
**Sibling:** `2026-09-14-review-package-design.md`

## Goal

Anyone — including people without JefeCheck — can see a review session's
**latest state**: every round of notes on every piece of media in the session.
The summary is a PDF with thumbnails that look the way the reviewers saw the
frame, plus plain-text and CSV forms for reading and for other tools.

## Non-goals

Replaying the session over time; editing notes from the summary; thumbnails of
the untouched source; packaging media (that is the review package).

## User-facing behaviour

- **File → Export Review Summary…** opens a save dialog with three filters:
  PDF (`*.pdf`), Text (`*.txt`), CSV (`*.csv`). The chosen file's extension
  selects the format. On success the status bar reports
  `Summary written: <file> — <M> media, <R> rounds, <N> notes`; on failure a
  message box says what failed and nothing partial is left at the path.
- **CLI:** `--export-summary <out>` runs once the session/media given on the
  command line has loaded, prints
  `SUMMARY: wrote=<path> media=<M> rounds=<R> notes=<N> thumbs=<T> thumbfail=<F>`
  (or `SUMMARY: FAIL <reason>`), and quits with exit status 0 on success or 2
  on failure — the convention every test flag in `main_qt.cpp` uses.

## What the summary covers

**The session's media set**: the unique normalised media paths (as produced by
`gfcNoteStore::normalisePath`) of

1. tracks A–D, in track order, then
2. every playlist item's tracks, in playlist order.

A media path already seen is not repeated. For each media, its review is loaded
from its sidecar with `gfcNoteStore::load`. Media without a sidecar, or whose
review has no notes, still appears (as "No notes") in TXT and PDF, and
contributes no CSV rows.

The media-set enumeration is shared with the review package.

## Document model (pure C++, no Qt, no GL)

A summary is built once, then handed to a writer:

```
SummaryNote  { id, type, author, from, to, always, r, g, b, size, text }
SummaryFrame { frame;            // kAllFrames (-1) for notes with always=true
               notes[];          // stored order
               thumbnailPath; }  // PDF only; empty when unavailable
SummaryRound { id, author, created, modified, locked, frames[] }
SummaryMedia { mediaPath, displayName, rounds[] }
SummaryDoc   { title, exportedAt, appVersion, media[] }
```

Grouping rules — the chosen layout is **media → round → frame**:

- Rounds appear in the review's stored revision order.
- Within a round, a note belongs to the frame entry `always ? kAllFrames : from`.
- Frame entries are ordered with `kAllFrames` first, then ascending frame.
- Notes inside a frame entry keep their stored order.
- A round with no notes is kept and rendered as "No notes".
- `type` is the sidecar's type name: `stroke`, `arrow`, `box`, `text`; `text`
  is empty for non-text notes.
- Times are written as ISO-8601 UTC (`2026-09-13T22:44:23Z`).
- `displayName` is the media file name with the frame-number pattern kept
  (e.g. `shot.####.exr`), for headings.

### Text writer

```
Review summary — <title>
Exported 2026-09-14T20:10:00Z by JefeCheck 1.7.0
2 media · 3 rounds · 7 notes

== Blobbies.exr ==
Round 1 — Supervisor — created 2026-09-13T22:44:23Z — locked
  Frame 1
    - stroke  Supervisor  frames 1–1  #ff4033
    - text    Supervisor  frames 1–1  #59d9ff  "too warm here"
Round 2 — Artist — created 2026-09-14T09:12:00Z — open
  No notes
```

### CSV writer

UTF-8, RFC 4180 quoting (fields containing `,` `"` CR or LF are quoted, `"`
doubled), header row, one row per note:

```
media,round_id,round_author,round_created,round_modified,round_locked,note_id,type,author,from,to,all_frames,color,size,text
```

`round_locked` and `all_frames` are `true`/`false`; `color` is `#rrggbb`.

## Thumbnails (PDF only)

Each frame entry gets one thumbnail, **rendered through the plate pipeline**
(super-shader colour correction and the plate's LUT), with **only that round's
notes** burned in:

1. Before anything else, save the current session to a temporary `.jcs`
   (`jefe::qt::saveSession`).
2. For each media: if it is on a track, use the plate showing that track. If it
   is only a playlist media, load its playlist item (`jefe::qt::loadPlaylistItem`,
   the Playlist dock's path — which also applies the item's saved FX stacks and
   program state) and use the plate showing its track. The plate's colour
   correction and LUT are whatever that plate has — the look the reviewers had.
   Loading is asynchronous: frame file names are known immediately, pixels
   arrive over later ticks.
3. For each round and frame entry: set the plate's borrowed note list to that
   round's notes only (a new bridge call; the normal sync publishes every
   round), make sure the track's frame list exists (starting the track's load
   if it has none — renders force-decode the frame they draw, via
   `gfcSequence::getFrame(frame, forceLoad)`, but only once the track's async
   loader has recorded that frame's load parameters, so before rendering the
   export waits (bounded, 5 s per track, pumping the event loop and uploading
   pending textures) until the track reports loaded frames; a track that does
   not get there counts its entries in `thumbfail`), and render that one frame
   with
   `jefe::qt::triggerSyncRender` — `quadrant` = the plate, `from = to` = the
   frame (`kAllFrames` renders the media's first frame), `burnInNotes = true`,
   PNG, `outWidth` 960 and `outHeight` from the plate aspect — into a temporary
   directory. The file name follows `CreateRenderFilename`
   (`path + prefix + padded frame + postfix + ".png"`). There is no in-memory
   render API; the PDF writer loads the PNG.
4. After the last render — and on every failure or cancel path — republish the
   plate note lists from the review store (`jefe::qt::syncPlateNotes`) and
   reopen the temporary session through the same path as File → Open Session
   (GL current, `loadSession`, `startLoadingAllTracks`, refresh after load), so
   tracks, playlist selection, colour correction and the current frame are as
   the user left them.

The GL context is made current for the renders (the MainWindow's viewport, as
the Render dialog and `runHeadlessRenderTest` do). A thumbnail that cannot be produced (undecodable frame,
render error, timeout) leaves `thumbnailPath` empty; the PDF draws a
"thumbnail unavailable" box and the run reports the count in `thumbfail`. The
summary is still written.

## PDF layout

`QPdfWriter` (QtGui — no new dependency), A4 portrait, 300 dpi, 15 mm margins.

- **Header block** on page 1: title, export time and app version, totals.
- **Media heading** per media (starts a new page except the first).
- **Round header line**: `Round N — <author> — <created> — locked|open`.
- **Frame entry**: thumbnail on the left (80 mm wide), note list on the right —
  a colour swatch, type, author, frame range, and quoted text for text notes.
  An entry that does not fit the remaining page moves to the next page;
  entries are never split.
- **Footer**: `<title> — page X of Y`.

## Components

| Unit | Responsibility | Depends on |
|------|----------------|------------|
| `src/gfcReviewSummary.{h,cpp}` | Document model, grouping, TXT and CSV writers, self-test. | `gfcReview`/`gfcRevision`/`gfcNote` only |
| `src/qt/ReviewSummaryPdf_qt.{h,cpp}` | Lays out a `SummaryDoc` (with thumbnail paths) into a PDF. | QtGui only — no glad, no managers |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}` | Session media set (tracks via `getTrackParams`, playlist via `getPlaylistItemDetail`); load reviews for it; which plate shows a track; set a plate's notes to one round; whether a track's frame is decoded. Rendering reuses `triggerSyncRender`. | managers (the only TU allowed to) |
| `src/qt/MainWindow_qt.{h,cpp}` | Menu action, save dialog, orchestration (temp session, thumbnails, restore), status/errors. | bridge, PDF writer |
| `src/main_qt.cpp` | `--export-summary`; summary self-test joins the `--notes-test` battery. | MainWindow, model |

## Testing (TDD)

- **Model self-test** (`NOTE-SUMMARY: pass=N fail=N`, run by `--notes-test`):
  grouping by round and frame, `kAllFrames` ordering, empty round, stored
  order preserved, ISO times, TXT output against a golden string, CSV quoting
  of commas / quotes / newlines, CSV row count equals note count.
- **PDF writer test** (in the same battery, no GL): a two-media document with
  one real and one missing thumbnail produces a file that starts with `%PDF-`,
  ends with `%%EOF`, and whose page count — the number of `/Type /Page`
  dictionaries, excluding `/Type /Pages`, which QPdfWriter leaves uncompressed —
  equals the page count the layout reports. Runs in the `--notes-test`
  battery, which already has a `QApplication`.
- **End-to-end** (`--export-summary` on the two-player demo media and its
  4-note sidecar): the printed counts are `media=1 rounds=1 notes=4 thumbs=1
  thumbfail=0`; the TXT output matches the model test's expectations; and the
  thumbnail rendered with notes differs from the same frame rendered with the
  plate's notes cleared (mean absolute difference > 0), proving burn-in
  reached the thumbnail — the same shape as `--cc-test`.
- **State restore**: after the export the track's filename and the current
  frame equal their values before it (the session reload restores colour
  correction with the rest of the session file).

## Error handling

- No media in the session → the action reports "Nothing to summarise".
- Unwritable output path → error before any rendering starts.
- Sidecar that fails to parse → that media is listed with "Notes unreadable"
  and the run continues.
- Output is written to `<out>.partial` and renamed on success.
