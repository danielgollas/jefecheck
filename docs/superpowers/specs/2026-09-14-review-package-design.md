# Review package — design

**Date:** 2026-09-14
**Branch:** `feature/review-export` (from `feature/annotations`)
**Builds on:** `2026-09-07-annotations-design.md` (JEF-39 notes)
**Sibling:** `2026-09-14-review-summary-export-design.md` (shares the session
media-set enumeration)

## Goal

One file an artist can open in JefeCheck to see the review session's **full
latest state**: the session (tracks, playlist, plate transforms, colour
correction, LUTs, FX stacks) and every round of notes on every media — with the
media inside the file, or, for shared storage, referenced and found again even
if it has moved.

## Non-goals

Replay; compression; proxy media; packaging FX shaders (FX are referenced by
name, and a missing FX is reported on open); OS file association /
double-click; merging conflicting edits beyond the union rule below.

## Container: `.jcreview`

An **uncompressed POSIX ustar tar**, written and read in-house (no dependency).
Reasons: no 4 GB archive limit, streamable, and EXR/DPX media is already
compressed or incompressible. Constraints, enforced with clear errors:
entry names up to 255 bytes (ustar prefix + name), a single entry up to
8 GiB − 1 (11 octal digits).

```
manifest.json
session.jcs
notes/000.jnotes          one per media, in manifest order
media/000/<file names>    only when media is included; original file names kept
luts/<file>               LUT files the session uses that do not ship with the app
```

`manifest.json` is the first entry, so a reader can validate a package without
scanning it:

```json
{
  "format": "jefecheck-review-package",
  "version": 1,
  "created": "2026-09-14T20:10:00Z",
  "app": "1.7.0",
  "session": "session.jcs",
  "mediaIncluded": true,
  "media": [
    { "index": 0,
      "originalPath": "/shows/x/shot.####.exr",
      "packagedPath": "media/000/shot.####.exr",
      "fingerprint": "fp1:3b1f…",
      "frames": ["shot.0001.exr", "shot.0002.exr"],
      "notes": "notes/000.jnotes" }
  ],
  "luts": [ { "name": "show_look", "file": "luts/show_look.cube" } ]
}
```

`packagedPath` is empty when media is not included.

## Media fingerprint

Nothing in the tree computes `gfcReview::fingerprint` today; this defines it.
Filled in on export when a media's sidecar has none, and saved back to the
original sidecar (when writable) as well as the packaged copy. Format `fp1:`
followed by SHA-1 hex over, in order:

1. the ASCII tag `jefecheck-fp1`,
2. width, height and channel names of the first frame,
3. the frame count,
4. for up to three sample frames (first, middle, last): the first frame's data
   window decimated — every 16th row and every 16th column — channels 0–2 read
   as 32-bit float.

It ignores file names and locations and changes when pixels change. Cost:
three partial frame reads. Pixels are read with OIIO directly (no decode through
`gfcSequence`, no GL). SHA-1 comes from a shared `gfcSha1` unit, lifted out of
the file-local implementation `gfcNoteStore.cpp` already has for fallback
sidecar names; `gfcNoteStore` then uses the shared unit, with its self-test
proving the fallback names are unchanged.

## Export

- **File → Export Review Package…** — a small dialog: output path, an
  **Include media** checkbox (on by default) with the total media size shown
  beside it, Export/Cancel, and a progress bar. **CLI:**
  `--export-package <out> [--no-media]` prints
  `PACKAGE: wrote=<path> media=<M> included=<0|1> bytes=<B>` or
  `PACKAGE: FAIL <reason>`, exiting 0 or 2. `--open-package` below follows the
  same convention.
- Steps:
  1. Enumerate the session media set (shared with the summary).
  2. Save the current session to a temporary `.jcs`.
  3. For each media: ensure a fingerprint; copy its sidecar to `notes/NNN.jnotes`
     (a media without a sidecar gets an empty review so the index stays
     aligned); when including media, list its frame files.
  4. Rewrite the temporary session's media paths — track `filename` and
     playlist `fn` — to `media/NNN/<file name>` when included; leave them
     absolute otherwise.
  5. Collect every LUT name the session references — each plate's `lut`
     attribute and every FX widget of type `cube` or `lut`, in plates and in
     playlist items. Sessions store only the LUT's file name and resolve it by
     name on load, so for each name whose loaded LUT's source file
     (`CubeLUT::filename`) is outside the install LUT directory, add that file
     under `luts/`.
  6. Write `manifest.json`, `session.jcs`, notes, LUTs, then media, to
     `<out>.partial`, incrementally (one file per event-loop turn, like the
     render dialog) so the UI stays responsive and Cancel works; rename to
     `<out>` on success, delete the partial on cancel or error.

## Open

- **File → Open Review Package…** and **CLI** `--open-package <file>`, which
  prints `PACKAGE: opened=<file> media=<M> resolved=<K> missing=<J>`.
- Steps:
  1. Read and validate `manifest.json` (format and version); refuse unknown
     versions with a message naming the version.
  2. Extract into `<AppDataLocation>/packages/<id>/`, where `<id>` is the SHA-1
     of the package's absolute path, size and modification time. A completed
     extraction writes a `.complete` marker and is reused next time.
  3. Load `luts/` files into the LUT manager before the session, so plate LUT
     names resolve.
  4. Resolve each media:
     - included → the extracted `media/NNN/…` path;
     - not included → the original path if it exists; otherwise the first
       sequence under the **Preferences → Search Paths** list
       (`sett.searchPaths`, descending into subdirectories when
       `sett.searchPathsRecursive`) whose fingerprint matches. The list is
       used whether or not "use search paths" is ticked — that checkbox
       governs the older automatic relink, not this explicit one. Candidates
       are filtered by frame count and first-frame resolution before any
       fingerprint is computed, and candidates whose file-name pattern matches
       are tried first. If nothing matches: interactively, a
       "Locate <display name>…" dialog (the chosen sequence must match the
       fingerprint, or the user confirms using it anyway); in CLI mode the
       media is counted as missing.
     - A media that stays missing keeps its original path in the session.
       The existing basename-only fallback (`findFileInSearchPaths`, applied
       when "use search paths" is on) may then still pick a file up at load
       time, exactly as it would for any session today.
  5. Place notes where `gfcNoteStore::sidecarPathFor(resolved path)` expects
     them. If a sidecar already exists there, **union-merge** by revision id
     and note id — package entries are added, nothing local is deleted, and an
     existing note or revision with the same id is kept as it is — then save.
     This is the same idempotent-by-id rule as live note sync.
  6. Rewrite the session's media paths to the resolved absolute paths, write it
     into the extraction directory, and open it through the existing
     open-session path (GL current, refresh after session load, notes refreshed
     for loaded media).
  7. The window title shows the package name. Missing media and missing FX are
     listed in one message after load.

Opening a package's own extraction again, adding a round and exporting a new
package works with no special handling: the extracted media and sidecars are
ordinary files.

## Components

| Unit | Responsibility | Depends on |
|------|----------------|------------|
| `src/gfcTarArchive.{h,cpp}` | ustar writer (streaming, incremental) and reader (list, extract), header checksum validation, self-test. | C++ standard library |
| `src/gfcSha1.{h,cpp}` | SHA-1 (incremental and one-shot, hex output), lifted from `gfcNoteStore.cpp`; self-test with the FIPS 180 vectors. `gfcNoteStore` switches to it. | C++ standard library |
| `src/gfcMediaFingerprint.{h,cpp}` | `fp1` fingerprint of a sequence from its frame paths, self-test. | OIIO, `gfcSha1` |
| `src/gfcSessionPaths.{h,cpp}` | List and rewrite media paths inside `.jcs` XML; self-test on a fixture. | xmlParser |
| `src/gfcNoteMerge.{h,cpp}` | Union-merge two reviews by revision and note id; self-test. | `gfcReview` |
| `src/qt/ReviewPackage_qt.{h,cpp}` | Manifest JSON (QtCore `QJsonDocument`), export and open orchestration steps, relink search. | QtCore, the units above, bridge |
| `src/qt/ReviewPackageDialog_qt.{h,cpp}` | Export dialog: path, Include media + size, progress, cancel. | QtWidgets |
| `src/qt/SequenceLoadBridge_qt.{h,cpp}` | Session media set (shared), frame file lists, LUT source paths, loading LUT files. | managers |
| `src/qt/MainWindow_qt.{h,cpp}` | Menu actions, save-temp-session and open-session plumbing, messages. | the above |
| `src/main_qt.cpp` | `--export-package`, `--open-package`; self-tests join `--notes-test`. | |

## Testing (TDD)

Self-tests join the `--notes-test` battery, each printing `NAME: pass=N fail=N`:

- **Tar** (`NOTE-TAR`): round-trip of empty, small and multi-megabyte files;
  a 200-byte name split into prefix + name; a 300-byte name refused; a corrupted
  header checksum refused; a truncated archive detected; entry order preserved.
- **Fingerprint** (`NOTE-FINGERPRINT`): same pixels under a different path and
  name → same value; one changed pixel inside a sampled row/column → different
  value; different frame count → different value; `fp1:` prefix.
- **Session paths** (`NOTE-SESSIONPATHS`): list track and playlist paths from a
  fixture `.jcs`; rewrite them; untouched attributes byte-identical.
- **Merge** (`NOTE-MERGE`): union of disjoint revisions; same note id kept
  once; local-only notes survive; merging twice is idempotent.
- **SHA-1** (`NOTE-SHA1`): FIPS 180 vectors (`""`, `"abc"`, the 56-byte
  message, one million `a`s fed incrementally); the notes-store self-test
  still passes unchanged after `gfcNoteStore` switches to the shared unit.

End-to-end flags on copies of openexr-images media in a temporary directory,
run with `--config-dir` pointing at a temporary settings directory; like every
test flag they exit 0 on pass and 2 on fail:

- `--package-test <workdir>`: export with media → open → the loaded tracks
  point inside the extraction directory, each media's bytes hash equal to the
  source, notes equal (by `gfcNoteStore::toJsonString`), plate
  exposure/gamma/LUT name equal to the exporting session's.
- `--relink-test <workdir>`: export without media → move the media into a
  directory written into the test's settings as the only search path
  (`Search/paths`, "use search paths" off to prove it is not required) → open
  → media resolved by fingerprint (`resolved=1 missing=0`) and its notes
  present.

## Error handling

- Unwritable output, a name over 255 bytes, or an entry over 8 GiB − 1 → export
  refuses before writing, naming the file.
- Disk full or I/O error mid-write → partial file removed, message names the
  file being written.
- Unreadable or truncated package, or an unknown format/version → nothing is
  extracted or loaded.
- A package entry whose name is absolute or contains `..` → rejected (no
  writing outside the extraction directory).
