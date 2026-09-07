# Annotations — Design

**Status:** approved 2026-09-07
**Branch:** `feature/annotations` (from `qt-experimental`)
**Ticket:** JEF-39

## Goal

Let a reviewer draw on the frame — freehand, arrow, box, text — with the markup
pinned to a frame range, saved beside the footage, synced live to everyone in a
remote session, and optionally burned into renders.

This is the first of three features that share one geometry core. Spline masks
for FX and on-screen handles for spatial FX parameters come later and reuse what
is built here. They are **not** in this scope.

## Why this shape

JefeCheck has had a sketch of this since 2006: `gfcReview`, `gfcRevision`,
`gfcNote` and `gfcNoteText` exist as headers with empty method bodies. They are
compiled today — the build globs `src/*.cpp` — they simply do nothing and
nothing references them. The hierarchy is right and two of its decisions are sharper than
a fresh design would have been:

- `gfcRevision::locked` — a round of notes can be finalised, which is a real
  review-workflow concept, not a storage detail.
- `gfcNote::quadID` — a note belongs to ONE plate, so in a 2×2 comparison the
  markup sticks to the version it is about.

This design completes that model rather than replacing it.

## Model

```
gfcReview                one per piece of footage
  mediaPath              normalised sequence pattern — the identity
  fingerprint            hash of sampled frames, for re-link when the path misses
  revisions[]

gfcRevision              one round of notes, in time
  author, created, modified, locked
  notes[]                std::vector<std::unique_ptr<gfcNote>>

gfcNote (abstract)       existing fields kept unchanged:
  name, quadID, from, to, always, colorR/G/B, size
  noteType()             REPLACES the stored `type` field — a virtual, so the
                         declared type can never disagree with the actual
                         subclass. The XML `type=` attribute is written by
                         mapping noteType() to a string, not from a field.
  id                     NEW — UUID
  author                 NEW — who drew this note
  geometry               NEW — see below
```

### Ownership

`gfcRevision::notes` becomes `std::vector<std::unique_ptr<gfcNote>>`, replacing
the raw `std::vector<gfcNote*>` and the `//TODO: Delete each note` in
`gfcrevision.cpp`.

### Why `id` and `author` are new

`id` is a UUID assigned at creation. Sync messages reference it, which makes
add/remove idempotent and independent of arrival order.

`author` moves DOWN to the note. `gfcRevision` already carries an author, but in
a live review several people draw into the same round, and attribution at
revision level loses who said what.

### Geometry

Stored in **normalised image space**, x and y in 0..1 across the source image —
never screen pixels. This is what makes a note stick to the image through pan,
zoom, flip, flop, aspect changes, a different display resolution, and export at
a different size. It also means the on-screen and render-time draws are two
mappings of the same rect rather than two coordinate systems.

Four concrete subclasses:

| Class | Geometry | Notes |
|---|---|---|
| `gfcNoteStroke` | `std::vector<gfcNotePoint>` | freehand |
| `gfcNoteArrow` | tail, head | |
| `gfcNoteBox` | two opposite corners | |
| `gfcNoteText` | anchor + `std::string` | existing subclass, filled in |

`gfcNotePoint` is `{ float x, y; }` in `src/gfcNoteGeometry.h`.

## Frame ranges

- `always == true` → visible on every frame of the shot.
- otherwise visible for `from..to` inclusive.
- A note drawn while paused defaults to `from = to = currentFrame`, editable
  afterwards.
- Notes appear and disappear at their boundaries during scrubbing.

Timeline markers showing where notes sit are an obvious follow-on and are NOT in
scope.

## Rendering — on screen

Notes draw inside `FXPASS_LAST` in `gfcPlate::draw3DrectWithFX`, at the point
where the composited FBO texture is mapped onto the transformed quad and the
existing text overlay is drawn. That location provides the plate's screen
transform, so notes track the image when panned or zoomed.

**Annotations must never pass through the super-shader.** If markup went through
colour correction, a note's red would shift when exposure is pulled and the
markup would start lying about itself. Drawing after `FXPASS_LAST` gives this
for free.

Two implementation requirements:

1. **Disable the active shader program before drawing** —
   `glGetHandleARB(GL_PROGRAM_OBJECT_ARB)` then `glUseProgramObjectARB(0)`,
   restoring afterwards. This is what `GfcTextRenderer` already does before its
   own quads; not doing it draws the notes through whatever shader was last
   bound.
2. **Two-pass draw for legibility** — a darker outline slightly thicker, then
   the colour on top. Same approach as the text shadow. Without it a red note is
   invisible on a red frame.

Strokes are `GL_LINE_STRIP` with width. Consistent-width antialiasing needs
triangle-strip expansion; that is a known quality limit, recorded here and not
addressed in this scope.

## Rendering — export

After the super-shader pass writes into the FBO and **before** read-back, draw
the notes into the same FBO using its ortho projection.

New field `gfcRenderParams::burnInNotes`, surfaced in the Render dialog beside
the existing "Bake aspect / crop bars" toggle.

**Default off**, matching `bakeCropBars`. Burn-in is the point of this approach,
but a note accidentally baked into a delivery render is far worse than one the
user has to tick a box to get.

## Storage

Sidecar file `<sequence_dir>/<basename>.jnotes`, XML via the vendored
`xmlParser`, keyed by the normalised sequence path with the fingerprint stored
alongside so a moved sequence can be re-linked instead of orphaned.

```xml
<jefecheckNotes version="1">
  <media path="/jobs/ep01/sh010/v003/sh010_v003.####.exr" fingerprint="a1b2c3…"/>
  <revision author="daniel" created="1788800000" modified="1788800120" locked="1">
    <note id="7f3a…" type="stroke" author="daniel" quad="0"
          from="101" to="101" always="0" r="1" g="0.2" b="0.2" size="3">
      <p x="0.412" y="0.331"/>
      <p x="0.418" y="0.340"/>
    </note>
  </revision>
</jefecheckNotes>
```

### Read-only media directories — REQUIRED

Render mounts are commonly read-only. When the media directory cannot be
written, the sidecar goes to `~/.config/jefecheck/notes/<sha1-of-path>.jnotes`.
Reads check the media directory first, then the fallback location.

Without this the feature does not work at most facilities. It is not optional.

### When writes happen

On revision lock, on session save, on quit, and on a dirty-flag timer, so a
crash does not cost a round. This mirrors the existing session crash recovery.

## Sync

Three messages over the existing remote bus, alongside chat and the remote
pointer:

| Message | Payload | When |
|---|---|---|
| `note-add` | full note | on mouse-up — NOT per motion sample |
| `note-remove` | note id | |
| `revision-lock` | revision id | host only |

A joiner arriving mid-session receives a snapshot of the open revision.

Anyone in the session may draw. Removal is limited to the note's author or the
host.

## Locked revisions

Locking is a host action. A locked revision refuses adds, removes and edits.

Locked rounds remain **visible** — seeing Monday's notes while giving
Wednesday's is the reason the model has rounds at all.

The host may unlock, which stamps `modified`.

## UI

A **Notes dock**, toggled by `F7`, listing revisions and their notes with
click-to-jump-to-frame, a tool picker (freehand / arrow / box / text), colour,
size, and a "Lock round" button.

Bare **`N`** toggles note visibility. Verified free: `F H L M O P R T V` are
taken.

Per `developer_notes.md` §1, the dock reaches the managers only through
`jefe::qt::` accessors and must not include the rendering-chain headers
directly.

## Testing

| Test | Proves |
|---|---|
| `--notes-test` | Build a review, add one of each note type, write the sidecar, reload, assert round-trip equality. No GL needed for the model half. |
| Render proof | Render a frame with `burnInNotes` off and on; assert the pixels differ. Structurally identical to `--cc-test`, which exists because a render once silently dropped colour correction. |
| Sync | Extend the two-process `--remote-test`: host adds a note, joiner asserts arrival. |
| Read-only fallback | Point the store at an unwritable directory; assert the note lands in the fallback path and reads back. |

## Files

**New**

- `src/gfcNoteGeometry.h` — `gfcNotePoint`
- `src/gfcnotestroke.{h,cpp}`, `src/gfcnotearrow.{h,cpp}`, `src/gfcnotebox.{h,cpp}`
- `src/gfcNoteStore.{h,cpp}` — path normalisation, fingerprint, sidecar read/write, fallback location
- `src/gfcNoteOverlay.{h,cpp}` — GL drawing for both screen and FBO
- `src/qt/NotesPanel_qt.{h,cpp}` — the dock

**Filled in (currently empty bodies, not in the build)**

- `src/gfcnote.{h,cpp}`, `src/gfcnotetext.{h,cpp}`, `src/gfcrevision.{h,cpp}`, `src/gfcreview.{h,cpp}`

**Modified**

- `CMakeLists.txt` — NOT modified. It uses `file(GLOB src/*.cpp)` and `file(GLOB src/qt/*.cpp)`, so every new source is picked up automatically (the 2006 note files are already compiled this way today). The only requirement is re-running `cmake -B build` after adding a file, since a glob is evaluated at configure time.
- `src/gfcPlate.cpp` — overlay call in `FXPASS_LAST`, and the export composite
- `src/gfcrenderparams.h` — `burnInNotes`
- `src/qt/RenderDialog_qt.cpp` — the toggle
- `src/qt/SequenceLoadBridge_qt.{h,cpp}` — TU-safe accessors for the dock
- `src/qt/MainWindow_qt.cpp` — dock, `F7`, `N`
- `src/main_qt.cpp` — `--notes-test`

## Explicitly out of scope

Spline masks, feathering and FX binding; keyframed or animated notes; on-screen
handles for FX parameters; replies or threads on a note; coordinator-side note
storage; timeline markers for note positions.
