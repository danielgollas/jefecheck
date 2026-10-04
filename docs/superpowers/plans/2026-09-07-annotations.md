# Annotations (JEF-39) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw freehand / arrow / box / text notes on a frame, pinned to a frame range, saved beside the footage, synced live to a remote session, and optionally burned into renders.

**Architecture:** Complete the `gfcReview` → `gfcRevision` → `gfcNote` model that has existed in the tree with empty method bodies since 2006. Geometry is stored in normalised image space (0..1) so one representation serves both the on-screen overlay and the export composite. Notes draw AFTER the super-shader so colour correction can never alter a note's colour.

**Tech Stack:** C++20, Qt6 Widgets, OpenGL (ARB shader entry points), vendored `xmlParser`, existing RakNet-backed message bus behind `ITransport`.

**Spec:** `docs/superpowers/specs/2026-09-07-annotations-design.md` — read it first; it carries the reasoning this plan assumes.

**Branch:** `feature/annotations`, based on `qt-experimental` at `e821972`.

## Global Constraints

- Base commit is `e821972`. Verify with `git log --oneline -1` before starting. This branch does NOT contain `src/auth/`, `src/qt/RemotePanel_qt.*`, or the WebRTC transport — those live on `feature/remote-webrtc` and are not available here.
- Geometry is stored in **normalised image space**, x and y in `0.0..1.0`. Never screen pixels, never source-resolution pixels.
- Annotations composite **after** the super-shader. Never route note drawing through `startSuperShader()`.
- Before drawing note geometry, save and clear the active shader: `glGetHandleARB(GL_PROGRAM_OBJECT_ARB)`, then `glUseProgramObjectARB(0)`, restoring after. `GfcTextRenderer` does this already — match it.
- Use ARB shader entry points only (`glUseProgramObjectARB`, not `glUseProgram`). On macOS `GLhandleARB` is `void*`, not `GLuint`; mixing them breaks the build.
- Initialise every GL handle to `0` in constructors. An uninitialised handle passed to `glDeleteObjectARB` is a trace trap on macOS.
- Qt code reaches the rendering-chain managers ONLY through `jefe::qt::` accessors in `SequenceLoadBridge_qt`. See `developer_notes.md` §1 — glad and QOpenGLWidget cannot share a translation unit on macOS.
- **Do not edit `CMakeLists.txt`.** It globs `src/*.cpp` and `src/qt/*.cpp`, so new sources are collected automatically. After creating a new file you MUST re-run `cmake -B build` before building, because a glob is evaluated at configure time and an incremental `cmake --build build` will not see the new file.
- Build: `cmake -B build && cmake --build build`. Binary: `./build/jefecheck.app/Contents/MacOS/jefecheck`.
- Test footage: any `.exr` under `/Users/dgollas/projects/openexr-images`.
- No literal newlines inside a single bash command — join with `&&` or `;`.

## File Ownership Map

Tasks run in waves. Within a wave, file sets are disjoint and tasks may run in
parallel. **No task may edit a file owned by another task**, even to fix
something — report it instead.

| Task | Wave | Owns |
|---|---|---|
| 1 | 0 | `src/gfcNoteGeometry.h`, `src/gfcnote.{h,cpp}`, `src/gfcnotestroke.{h,cpp}`, `src/gfcnotearrow.{h,cpp}`, `src/gfcnotebox.{h,cpp}`, `src/gfcnotetext.{h,cpp}`, `src/gfcrevision.{h,cpp}`, `src/gfcreview.{h,cpp}` |
| 2 | 1 | `src/gfcNoteStore.{h,cpp}` |
| 3 | 1 | `src/gfcNoteOverlay.{h,cpp}` |
| 4 | 1 | `src/gfcNetworkStructures.h`, `src/gfcnetworkmanager.{h,cpp}`, `src/gfcnetworkclient.cpp`, `src/gfcnetworkserver.cpp` |
| 5 | 2 | `src/gfcPlate.{h,cpp}`, `src/gfcrenderparams.h`, `src/qt/RenderDialog_qt.cpp` |
| 6 | 2 | `src/qt/NotesPanel_qt.{h,cpp}`, `src/qt/SequenceLoadBridge_qt.{h,cpp}`, `src/qt/MainWindow_qt.{h,cpp}` |
| 7 | 2 | `src/main_qt.cpp` |

Task 1 runs first, alone. Then 2, 3, 4 in parallel. Then 5, 6, 7 in parallel.

---

### Task 1: The note model

**Files:**
- Create: `src/gfcNoteGeometry.h`, `src/gfcnotestroke.{h,cpp}`, `src/gfcnotearrow.{h,cpp}`, `src/gfcnotebox.{h,cpp}`
- Modify: `src/gfcnote.{h,cpp}`, `src/gfcnotetext.{h,cpp}`, `src/gfcrevision.{h,cpp}`, `src/gfcreview.{h,cpp}`

**Interfaces:**
- Produces, relied on by Tasks 2, 3, 4, 6, 7:

```cpp
// src/gfcNoteGeometry.h
struct gfcNotePoint { float x = 0.0f; float y = 0.0f; };   // normalised 0..1

// src/gfcnote.h
enum gfcNoteType { GFCNOTE_STROKE = 0, GFCNOTE_ARROW, GFCNOTE_BOX, GFCNOTE_TEXT };

class gfcNote {
public:
    virtual ~gfcNote();
    virtual gfcNoteType noteType() const = 0;
    /** Every point this note is made of, in normalised image space, in draw order. */
    virtual std::vector<gfcNotePoint> points() const = 0;

    std::string id;         // UUID, assigned at construction
    std::string author;
    std::string name;
    int   quadID = 0;
    int   from = 0, to = 0;
    bool  always = false;
    float colorR = 1.0f, colorG = 0.2f, colorB = 0.2f;
    int   size = 3;
    /** True when this note should be drawn on the given frame. */
    bool visibleOnFrame(int frame) const;
};

class gfcNoteStroke : public gfcNote { public: std::vector<gfcNotePoint> pts; };
class gfcNoteArrow  : public gfcNote { public: gfcNotePoint tail, head; };
class gfcNoteBox    : public gfcNote { public: gfcNotePoint a, b; };
class gfcNoteText   : public gfcNote { public: gfcNotePoint anchor; std::string text; };

// src/gfcrevision.h
class gfcRevision {
public:
    std::string id, author;
    time_t created = 0, modified = 0;
    bool locked = false;
    std::vector<std::unique_ptr<gfcNote>> notes;
    /** Returns false and changes nothing when locked. */
    bool addNote(std::unique_ptr<gfcNote> n);
    bool removeNote(const std::string& noteId);
};

// src/gfcreview.h
class gfcReview {
public:
    std::string mediaPath, fingerprint;
    std::vector<gfcRevision> revisions;
    gfcRevision* openRevision();            // last unlocked revision, or nullptr
    gfcRevision& beginRevision(const std::string& author);
};
```

Keep every existing 2006 field. Do not rename `quadID`, `from`, `to`, `always`, `size`, or the `colorR/G/B` triple.

- [ ] **Step 1: Write the failing test**

Create `src/gfcnote.cpp`'s self-test entry, declared in `gfcnote.h` as
`int noteModelSelfTest();`. Follow the existing self-test style in the repo:
a `check(cond, msg)` helper incrementing pass/fail counters, printing
`NOTE-MODEL: pass=N fail=N` and returning non-zero on failure.

```cpp
check(!s.visibleOnFrame(5),  "a note outside its range is not visible");
s.from = 1; s.to = 10;
check(s.visibleOnFrame(5),   "a note inside its range is visible");
check(!s.visibleOnFrame(11), "the range is inclusive at the top");
s.always = true;
check(s.visibleOnFrame(999), "an always note ignores the range");

gfcRevision r;
auto n = std::make_unique<gfcNoteStroke>();
const std::string nid = n->id;
check(!nid.empty(),          "a note gets an id at construction");
check(r.addNote(std::move(n)), "an open revision accepts a note");
r.locked = true;
check(!r.addNote(std::make_unique<gfcNoteStroke>()), "a locked revision refuses a note");
check(!r.removeNote(nid),    "a locked revision refuses a removal");
```

- [ ] **Step 2: Run it and watch it fail**

Task 7 adds the CLI flag. Until then, verify by building only:
`cmake --build build 2>&1 | grep -E 'error' | head`
Expected: compile errors naming the not-yet-written members.

- [ ] **Step 3: Implement the model**

Generate `id` with a 32-hex-character random string from `std::random_device`.
There is an existing example of exactly this in the codebase — search for
`makeSelfJoinNonce` on `feature/remote-webrtc` if you want a reference, but do
not depend on that branch; write it locally.

- [ ] **Step 4: Build clean**

Run: `cmake --build build 2>&1 | grep -E 'error' | head`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add src && git commit -m "JEF-39: complete the 2006 note/revision/review model"
```

---

### Task 2: Sidecar store

**Files:**
- Create: `src/gfcNoteStore.{h,cpp}`

**Interfaces:**
- Consumes: `gfcReview`, `gfcRevision`, `gfcNote` and subclasses from Task 1.
- Produces, relied on by Tasks 6 and 7:

```cpp
namespace gfcNoteStore {
    /** Collapse a frame path to its sequence pattern: .0101.exr -> .####.exr */
    std::string normalisePath(const std::string& anyFramePath);
    /** Where notes for this sequence live. Prefers the media directory; falls
        back to ~/.config/jefecheck/notes/<sha1>.jnotes when it is not writable. */
    std::string sidecarPathFor(const std::string& normalisedPath);
    bool save(const gfcReview& review);
    bool load(const std::string& normalisedPath, gfcReview& out);
}
```

XML via the vendored `xmlParser`, following `gfcsessionmanager.cpp:150-188`:
`XMLNode::createXMLTopNode`, `addChild`, `addAttribute`, `writeToFile`.

Format is given verbatim in the spec under "Storage" — match it exactly.

- [ ] **Step 1: Write the failing test**

Declare `int noteStoreSelfTest();` in the header, same self-test style as Task 1,
printing `NOTE-STORE: pass=N fail=N`.

```cpp
check(normalisePath("/j/sh010.0101.exr") == "/j/sh010.####.exr",
      "a frame number collapses to a pattern");
check(normalisePath("/j/sh010.####.exr") == "/j/sh010.####.exr",
      "an already-normalised path is unchanged");

// round trip
gfcReview w; w.mediaPath = "/tmp/jefe_notes_test/sh.####.exr";
gfcRevision& rev = w.beginRevision("tester");
auto s = std::make_unique<gfcNoteStroke>();
s->pts = { {0.1f, 0.2f}, {0.3f, 0.4f} };
s->from = 7; s->to = 9; s->quadID = 2;
rev.addNote(std::move(s));
check(save(w), "the review writes");
gfcReview back;
check(load(w.mediaPath, back), "the review reads back");
check(back.revisions.size() == 1, "one revision survives");
check(back.revisions[0].notes.size() == 1, "one note survives");
check(back.revisions[0].notes[0]->points().size() == 2, "both points survive");
check(back.revisions[0].notes[0]->quadID == 2, "quadID survives");

// read-only fallback
// chmod the directory to 0500, save again, assert the file landed under
// ~/.config/jefecheck/notes/ and that load() still finds it.
```

- [ ] **Step 2: Verify it fails** — `cmake --build build 2>&1 | grep error | head`

- [ ] **Step 3: Implement**

The read-only fallback is REQUIRED, not optional — render mounts are commonly
read-only and without it the feature does not work at most facilities. Test
writability by attempting the write and handling failure, not by inspecting
permission bits (an NFS mount can lie about those).

- [ ] **Step 4: Build clean** — no errors.

- [ ] **Step 5: Commit**

```bash
git add src && git commit -m "JEF-39: note sidecar store with read-only fallback"
```

---

### Task 3: Overlay renderer

**Files:**
- Create: `src/gfcNoteOverlay.{h,cpp}`

**Interfaces:**
- Consumes: `gfcNote` and subclasses from Task 1.
- Produces, relied on by Task 5:

```cpp
namespace gfcNoteOverlay {
    struct Rect { float x, y, w, h; };   // where 0..1 maps to, in current GL coords
    /** Draw every note in `notes` visible on `frame` whose quadID == quadID,
        mapping normalised coordinates onto `target`. Saves and restores the
        active shader program and all GL state it touches. */
    void draw(const std::vector<const gfcNote*>& notes,
              int frame, int quadID, const Rect& target);
}
```

Requirements:
- Save the active program with `glGetHandleARB(GL_PROGRAM_OBJECT_ARB)`, call
  `glUseProgramObjectARB(0)`, restore at the end. `GfcTextRenderer` does this;
  match it.
- Two-pass draw: a dark outline at `size + 2` first, then the note colour at
  `size`. Without this a red note is invisible on a red frame.
- Strokes and boxes are `GL_LINE_STRIP`. An arrow is the shaft plus two head
  segments computed from the tail→head direction.
- `GFCNOTE_TEXT` draws via `gfc_gl_draw` at the anchor, using the existing text
  renderer. Do NOT add a second text path.
- Restore blend, line width, and colour to what they were on entry.

- [ ] **Step 1: Write the failing test**

There is no headless GL context available in a unit test here, so this task's
test is a **geometry** test, not a pixel test. Declare
`int noteOverlaySelfTest();` printing `NOTE-OVERLAY: pass=N fail=N`, and expose
the mapping function so it can be tested without GL:

```cpp
// exposed for test: normalised -> target rect
gfcNotePoint mapped = gfcNoteOverlay::mapPoint({0.5f, 0.5f}, {0, 0, 100, 200});
check(mapped.x == 50.0f && mapped.y == 100.0f, "centre maps to the rect centre");
mapped = gfcNoteOverlay::mapPoint({0.0f, 0.0f}, {10, 20, 100, 200});
check(mapped.x == 10.0f && mapped.y == 20.0f, "origin maps to the rect origin");
```

Pixel proof lives in Task 5's render test, where a real GL context exists.

- [ ] **Step 2: Verify it fails** — `cmake --build build 2>&1 | grep error | head`
- [ ] **Step 3: Implement**
- [ ] **Step 4: Build clean** — no errors.
- [ ] **Step 5: Commit**

```bash
git add src && git commit -m "JEF-39: note overlay renderer"
```

---

### Task 4: Sync messages

**Files:**
- Modify: `src/gfcNetworkStructures.h`, `src/gfcnetworkmanager.{h,cpp}`, `src/gfcnetworkclient.cpp`, `src/gfcnetworkserver.cpp`

**Interfaces:**
- Consumes: `gfcNote` from Task 1.
- Produces, relied on by Task 6:

```cpp
// on gfcNetworkManager
void broadcastNoteAdd(const gfcNote& n);
void broadcastNoteRemove(const std::string& noteId);
void broadcastRevisionLock(const std::string& revisionId);
```

Follow the EXISTING paired convention in `gfcNetworkStructures.h:45-54`: every
message has a client→server form and a server→all broadcast form. Read how
`GFCNETID_POINTERINFOMESSAGE` / `GFCNETID_POINTERINFOBROADCASTMESSAGE` is
handled end to end and mirror it exactly.

Add to the end of `enum gfcNetPacketEnums`, never in the middle — the values are
wire-visible and renumbering breaks compatibility with any running peer:

```
GFCNETID_NOTEADDMESSAGE, GFCNETID_NOTEADDBROADCASTMESSAGE,
GFCNETID_NOTEREMOVEMESSAGE, GFCNETID_NOTEREMOVEBROADCASTMESSAGE,
GFCNETID_REVISIONLOCKMESSAGE, GFCNETID_REVISIONLOCKBROADCASTMESSAGE,
```

Requirements:
- A note is sent on mouse-UP, as one message. Never per motion sample.
- A joiner receives a snapshot of the open revision on join, the same way the
  peer list is delivered today.
- Removal is honoured only from the note's author or the host.

- [ ] **Step 1: Read the pointer-message path**

Run: `grep -rn 'GFCNETID_POINTERINFOMESSAGE\|GFCNETID_POINTERINFOBROADCASTMESSAGE' src | head -20`
Read every hit before writing anything.

- [ ] **Step 2: Add the enum values at the END of the enum**
- [ ] **Step 3: Implement serialise / deserialise / broadcast, mirroring the pointer path**
- [ ] **Step 4: Build clean** — `cmake --build build 2>&1 | grep error | head` gives no output.
- [ ] **Step 5: Verify the existing remote harness still passes**

Run: `./build/jefecheck.app/Contents/MacOS/jefecheck --remote-test`
Expected: same result as before your change. Capture the BEFORE output first.

- [ ] **Step 6: Commit**

```bash
git add src && git commit -m "JEF-39: note add/remove/lock sync messages"
```

---

### Task 5: Plate integration and burn-in

**Files:**
- Modify: `src/gfcPlate.{h,cpp}`, `src/gfcrenderparams.h`, `src/qt/RenderDialog_qt.cpp`

**Interfaces:**
- Consumes: `gfcNoteOverlay::draw` and `Rect` from Task 3; `gfcNote` from Task 1.

Two call sites:

1. **On screen** — inside `FXPASS_LAST` in `gfcPlate::draw3DrectWithFX`, next to
   where the existing text overlay is drawn. Uses the plate's transformed quad
   as the target rect so notes track pan and zoom.
2. **Export** — after the super-shader pass writes into the FBO and BEFORE
   read-back, using the FBO's ortho rect as the target. Gated on
   `renderParams.burnInNotes`.

Add to `gfcRenderParams`, beside the existing `bakeCropBars` at line 60:

```cpp
    bool burnInNotes;   // default false — see spec, a note baked into a
                        // delivery render is worse than one you must opt into
```

Initialise it to `false` wherever `bakeCropBars` is initialised.

In `RenderDialog_qt.cpp`, add a checkbox beside the existing "Bake aspect / crop
bars" control, labelled **"Burn in notes"**, unchecked by default, writing
`params.burnInNotes`.

- [ ] **Step 1: Capture a baseline render**

```bash
EXR=/Users/dgollas/projects/openexr-images/TestImages/GammaChart.exr
./build/jefecheck.app/Contents/MacOS/jefecheck --open-file "$EXR" --render-test /tmp/jefe_notes_base
```
Keep the output — Task 7's test compares against it.

- [ ] **Step 2: Add `burnInNotes` and the dialog checkbox**
- [ ] **Step 3: Wire the on-screen call site in FXPASS_LAST**
- [ ] **Step 4: Wire the export call site before read-back**
- [ ] **Step 5: Verify nothing regressed**

```bash
EXR=/Users/dgollas/projects/openexr-images/TestImages/GammaChart.exr
B=./build/jefecheck.app/Contents/MacOS/jefecheck
$B --fx-test "$EXR" && $B --cc-test "$EXR"
```
Expected: `FX-TEST PASS` and `CC-TEST PASS`, with the SAME numeric values as
before your change. Capture them first. A changed number means you altered the
render path, which this task must not do when there are no notes.

- [ ] **Step 6: Commit**

```bash
git add src && git commit -m "JEF-39: draw notes on screen and burn them into renders"
```

---

### Task 6: Notes dock

**Files:**
- Create: `src/qt/NotesPanel_qt.{h,cpp}`
- Modify: `src/qt/SequenceLoadBridge_qt.{h,cpp}`, `src/qt/MainWindow_qt.{h,cpp}`

**Interfaces:**
- Consumes: Task 1's model, Task 2's store, Task 4's broadcast calls.
- Produces: TU-safe accessors in the `jefe::qt` namespace, for example
  `std::vector<jefe::qt::NoteRow> notesForActivePlate(int frame);`
  Follow the existing accessor style in `SequenceLoadBridge_qt.h` exactly.

**The dock MUST NOT include rendering-chain headers.** Per `developer_notes.md`
§1, only `SequenceLoadBridge_qt.cpp` includes those; glad and QOpenGLWidget
cannot share a translation unit on macOS. Everything the panel needs goes
through a `jefe::qt::` accessor.

Contents: a list of revisions with their notes, click-to-jump-to-frame, a tool
picker (freehand / arrow / box / text), colour, size, and a "Lock round" button.

Shortcuts: dock toggle on `F7`; bare `N` toggles note visibility. Both are free —
`F H L M O P R T V` are taken, verified.

Object names follow the dotted-leaf scheme in `tests/ui/jefecheck/locators.py`:
`remote.` style prefixes, e.g. `notes.revision.list`, `notes.tool.freehand`,
`notes.lock.button`.

- [ ] **Step 1: Read the accessor pattern**

Run: `grep -n 'namespace jefe' src/qt/SequenceLoadBridge_qt.h | head` and read
20 lines around two existing accessors before writing your own.

- [ ] **Step 2: Add the accessors to the bridge**
- [ ] **Step 3: Build the panel**
- [ ] **Step 4: Register the dock, `F7` and `N` in MainWindow**
- [ ] **Step 5: Verify it opens**

```bash
./build/jefecheck.app/Contents/MacOS/jefecheck --screenshot-remote /tmp/notes_dock.png 4000
```
Expected: `SCREENSHOT=… ok=1`.

- [ ] **Step 6: Commit**

```bash
git add src && git commit -m "JEF-39: notes dock"
```

---

### Task 7: Headless test entry points

**Files:**
- Modify: `src/main_qt.cpp`

**Interfaces:**
- Consumes: `noteModelSelfTest()`, `noteStoreSelfTest()`, `noteOverlaySelfTest()` from Tasks 1, 2, 3.

Add `--notes-test`, which runs all three self-tests and exits non-zero if any
fails. Follow the existing pattern for a headless flag in `main_qt.cpp` — find
one with `grep -n 'auth-test\|wire-test\|signal-test' src/main_qt.cpp` and mirror
its structure, including `std::_Exit` to avoid the known teardown trap.

- [ ] **Step 1: Read an existing self-test flag's wiring**
- [ ] **Step 2: Add `--notes-test`**
- [ ] **Step 3: Run it**

```bash
./build/jefecheck.app/Contents/MacOS/jefecheck --notes-test
```
Expected: `NOTE-MODEL: pass=N fail=0`, `NOTE-STORE: pass=N fail=0`,
`NOTE-OVERLAY: pass=N fail=0`, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src && git commit -m "JEF-39: --notes-test"
```

---

## Self-review

**Spec coverage:** model → T1; geometry in normalised space → T1 + T3; frame
ranges → T1; overlay after super-shader → T3 + T5; export composite and
`burnInNotes` default off → T5; sidecar format → T2; read-only fallback → T2;
sync messages → T4; locked semantics → T1 (`addNote`/`removeNote` refuse) + T4
(lock broadcast); UI, `F7`, `N` → T6; `--notes-test` → T7; render proof → T5
step 5.

**Known gap, accepted:** the spec's two-process sync test (host adds a note,
joiner asserts arrival) is NOT a task here. Task 4 verifies only that the
existing `--remote-test` does not regress. A full two-process note test needs
the harness work that lives on `feature/remote-webrtc` and is not available on
this base. Recorded rather than silently dropped.

**Type consistency:** `gfcNotePoint`, `gfcNoteType`, `points()`, `visibleOnFrame`,
`addNote`, `removeNote`, `openRevision`, `beginRevision`, `normalisePath`,
`sidecarPathFor`, `save`, `load`, `gfcNoteOverlay::draw`, `Rect`, `mapPoint`,
`burnInNotes` — each defined once in the task that owns it and referenced with
the same spelling everywhere else.
