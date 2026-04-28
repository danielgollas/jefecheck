# Qt UI Testing Plan (Appium + Accessibility)

## Why now

The Qt port has reached the point where manual smoke testing is no longer
sufficient. Each new PR (timeline, FX stack, render window, etc.) silently
risks regressing earlier work — keyboard shortcuts, plate-card binding, LUT
loading, viewport hit-testing — and there are too many surfaces for the
author to re-verify by hand on every change.

Investing in automated UI testing **before** the remaining windows ship gives
us:

- A baseline that locks in current behavior (PRs 12–21).
- A regression net for every subsequent port PR.
- A forcing function to give every interactive widget a stable, accessible
  identity — which also benefits real users of assistive tech.

The cost is real: ~1–2 weeks of plumbing, plus ongoing test maintenance.
The alternative (continuing to port without tests) is worse — bugs found
weeks later in code the author has paged out.

## Stack

| Layer            | Choice                              | Rationale                                                                                |
| ---------------- | ----------------------------------- | ---------------------------------------------------------------------------------------- |
| Driver           | Appium 2 + `appium-mac2-driver`     | Maintained, uses XCUITest (Apple's accessibility API). Standard for native macOS apps.   |
| Test client      | Python 3.12 + `Appium-Python-Client` + pytest | Larger Appium ecosystem in Python; pytest fixtures map cleanly to per-test app launch.  |
| Element identity | `accessibleName` + `objectName`     | `accessibleName` → `AXTitle/AXLabel`; `objectName` is a stable widget locator in Qt.     |
| Assertions       | `expect` + screenshot diffing later | Start with state assertions (combo selection, label text); add pixel diff once stable.   |
| CI               | macOS GitHub runner (manual gate)   | Mac2 needs a real display; runs on `macos-14`. Initially label-triggered, not on every push. |

**Not** chosen and why:

- **Squish / Froglogic:** commercial, $$, overkill for a one-window app.
- **pytest-qt / Qt Test:** in-process, can't validate accessibility tree
  the way assistive tech sees it. Useful later for unit tests of pure
  widgets, not as the primary harness.
- **Node + WebdriverIO:** considered for tooling consistency with the
  marketing site, but the Python Appium ecosystem is larger and the
  docs/examples are better.

## Phase A — Accessibility plumbing (1 PR, ~2 days)

Goal: every interactive widget has a stable `objectName` and a human-readable
`accessibleName`. Appium can launch the app and find every widget.

### A1. Enable accessibility at app startup

In `src/main.cpp` (Qt branch) or `iapplication_qt.cpp`:

```cpp
QApplication app(argc, argv);
// Mac2 driver reads NSAccessibility; Qt auto-bridges QAccessible →
// NSAccessibility on macOS, but we make it explicit.
qputenv("QT_ACCESSIBILITY", "1");
```

Verify with `ax-inspector` (open `Accessibility Inspector.app` from
`/Applications/Xcode.app/Contents/Applications/`) that the running app
exposes a usable AX tree.

### A2. Naming convention

For each interactive widget:

```cpp
slider->setObjectName("plate.gamma.slider");
slider->setAccessibleName("Gamma");
slider->setAccessibleDescription("Plate gamma correction");
```

- **`objectName`** — namespaced, dotted, lowercase, no spaces. The locator.
- **`accessibleName`** — user-visible label (matches the on-screen label).
- **`accessibleDescription`** — optional, only when the name is ambiguous.

Per-plate widgets get the plate index baked in:
`"plate.0.lut.combo"`, `"plate.1.lut.combo"`, etc.

### A3. Naming pass — surface inventory

Files to touch (and roughly what each owns):

- `MainWindow_qt.cpp` — menu bar actions, dock toggles. Already has dock
  `objectName`s; add accessible names + name the menu actions
  (`menu.file.open`, `menu.view.layout.2x2`, etc.).
- `PlateCard_qt.cpp` — per-plate track box, gamma/exposure/BCS sliders,
  flip/flop checkboxes, LUT combo. ~14 widgets × up to 4 plates.
- `PlateManager_qt.cpp` — plate-card grid wrapper, layout buttons.
- `TimelinePanel_qt.cpp` — play/pause, scrub slider, frame counter, FPS
  spinner, in/out spinners, loop mode.
- `FXLutPanel_qt.cpp` — LUT list, filter, refresh, load buttons.
- `PreferencesWindow_qt.cpp` — sidebar list + per-section widgets (~30).
- `GlViewport_Qt` — set `accessibleName("viewport")` and
  `accessibleDescription` describing the active layout. The GL surface
  itself is opaque to AX; we identify it by name and interact via
  Mac2's coordinate-based actions.

Total: ~73 interactive widgets today. A morning's work.

**Verification:** Accessibility Inspector shows every widget with a
non-empty `AXTitle` and the dotted `objectName` is queryable via
`-ios predicate string:identifier == 'plate.0.gamma.slider'`.

## Phase B — Test harness (1 PR, ~3 days)

### B1. Directory layout

```
tests/
  ui/
    pyproject.toml        # appium-python-client, pytest, pytest-xdist
    conftest.py           # session/function fixtures: appium server, app launch
    jefecheck/
      __init__.py
      app.py              # JefeCheckApp helper (launch, quit, locators)
      locators.py         # OBJ.PLATE_LUT_COMBO(0) etc.
      shortcuts.py        # platform-correct key combos
    fixtures/
      tiny.exr            # 64×64 single-frame EXR
      seq/0001.png ..0004.png   # 4-frame numbered sequence
      sample.cube         # tiny LUT
    test_smoke.py
    test_layouts.py
    test_plate_ops.py
    test_lut.py
    test_transport.py
    test_preferences.py
  README.md               # how to run locally
```

### B2. App launch isolation

Tests must not clobber the user's preferences. The app reads
`QSettings` (org/app pair) and a session XML. Strategy:

1. Add `--config-dir <path>` CLI flag to `main.cpp`. When set:
   - `QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path)`
   - Override session save path to `<path>/session.xml`
   - Override `sett.lutPath` to `<path>/FX/` if the dir exists, otherwise
     fall back to install LUTs.
2. Each test creates a temp dir via `os.mkdtempSync()`, points
   `--config-dir` at it, copies in fixture LUTs, launches the app.
3. After the test: kill the app, `rm -rf` the temp dir.

This also removes a class of flakes (test 2 sees test 1's leftover state).

### B3. Helper API

```python
# jefecheck/app.py
class JefeCheckApp:
    @classmethod
    def launch(cls, *, open_file=None, config_dir=None) -> "JefeCheckApp": ...

    def by_object_name(self, name: str) -> WebElement: ...
    def send_shortcut(self, combo: str) -> None: ...   # 'Cmd+1'
    def screenshot_plate(self, idx: int) -> bytes: ...  # for visual diff
    def quit(self) -> None: ...
```

Locators are object-name based, exposed via XCUITest predicates:

```python
lut_combo = app.by_object_name("plate.0.lut.combo")
lut_combo.click()
app.by_object_name("plate.0.lut.option.gamma2.4").click()
```

A pytest fixture wraps launch/quit so each test gets a fresh app:

```python
# conftest.py
@pytest.fixture
def app(tmp_path):
    instance = JefeCheckApp.launch(config_dir=tmp_path)
    yield instance
    instance.quit()
```

### B4. CMake integration

Add a `qt-uitests` target that:

1. Depends on `jefecheck` (Qt build).
2. Runs `pytest` in `tests/ui/` via the project's venv.
3. Sets `JEFECHECK_BIN=$<TARGET_FILE:jefecheck>` so the harness finds the
   binary.

Local invocation: `cmake --build build_qt --target qt-uitests`.

### B5. CI integration

`.github/workflows/uitests.yml` — manual `workflow_dispatch` initially,
graduate to `pull_request` once the suite is stable. macOS-14 runner,
`brew install node` (Appium server), `npm install -g appium`, `appium
driver install mac2`, `python -m pip install -e tests/ui`, then run
the target.

Skip on Linux/Windows for now — Mac2 driver is macOS-only. We'll add
WinAppDriver / AT-SPI based equivalents in a later phase if needed.

## Phase C — Baseline tests (1–2 PRs, ~3 days)

One spec per major surface, each test under 10 seconds. Bias toward
**state** assertions over **pixel** assertions — pixel diffs come later.

### C1. `test_smoke.py`

- App launches, main window visible, title contains "JefeCheck".
- File menu present, expected items.
- All 4 docks present (PlateManager, Timeline, FX, LUT).

### C2. `test_layouts.py`

- `Cmd+1` → 1×1 layout, viewport accessible name reflects 1×1.
- `Cmd+2`/`Cmd+3`/`Cmd+4` → 2×1 / 1×2 / 2×2.
- Plate manager grid size matches.

### C3. `test_plate_ops.py`

Drop fixture image into plate 0:

- Flip toggle changes plate.0 GUI flip state.
- Flop toggle changes plate.0 GUI flop state.
- `F` shortcut flips active plate, `Shift+F` flips all.
- Fit / Fill / Original framing modes set the framing mode.

### C4. `test_lut.py`

- LUT panel auto-populates on launch (≥1 LUT visible).
- Load fixture `.cube`; appears in panel and in plate-card combos.
- Selecting LUT in plate.0 combo changes plate.0 GUI LUT index.
- "No LUT" choice clears it.

### C5. `test_transport.py`

Load fixture sequence:

- Play button toggles isPlaying.
- Scrub slider seeks; current-frame label updates.
- In/Out spinners set the loop range.
- FPS spinner changes target FPS.

### C6. `test_preferences.py`

- `Cmd+,` opens preferences.
- Sidebar navigation switches sections.
- Changing a setting + closing the dialog persists it (re-launch with
  same `--config-dir`, value still set).

**Verification:** `cmake --build build_qt --target qt-uitests` is green
locally on a clean checkout. Suite runs in under 90 seconds.

## Phase D — Pixel diff for the GL viewport

The state-only assertions in Phase C don't catch a class of bugs that
are exactly the ones we keep hitting (Retina viewport sized to quarter-
window, flip/flop wired but not refreshed, LUT applied but not sampled).
We need eyes on actual pixels.

### D1. Capture path — shipped

The harness uses **Appium's window-element screenshot** rather than a
custom debug bridge or `--screenshot` CLI mode. Mac2 exposes
`element.screenshot_as_png` on the QMainWindow AX node, which returns
the entire window (chrome + viewport) as PNG bytes. The viewport itself
is opaque to NSAccessibility — `QOpenGLWidget` doesn't expose an AX node
— but the parent window does, and that's enough.

Trade-off accepted: we compare the full window, not just the viewport.
Window chrome is bit-stable between runs, so the diff still attributes
regressions to the viewport. If we ever need viewport-only comparisons
(e.g. to ignore a status-bar drift), wire `grabFramebuffer()` through a
debug bridge then.

### D2. Image loading at startup — shipped

`--open-file <path>` (repeatable, fills plates 0..3) loads a fixture
image deterministically before the screenshot. Without it the viewport
would be empty for every test and the baseline wouldn't exercise the
render path. Used by the `visual_app` fixture.

### D3. Diffing — shipped

[`pixelmatch-py`](https://pypi.org/project/pixelmatch/) compares the
captured PNG against a committed baseline with a per-pixel YIQ
threshold (0.1) and a 0.5% diff-ratio budget. The budget absorbs
font-hinting jitter around the layout-status label and any Qt
focus-redraw artifacts; the test pattern's primary colors swing the
diff well above budget if rendering actually breaks.

### D4. Baselines — shipped

`tests/ui/baselines/<assertion>.png`. Updates require explicit opt-in:
`pytest --update-baselines`. A failed diff also writes
`<baseline>.actual.png` and `<baseline>.diff.png` next to the baseline
so the developer can eyeball the regression without re-running.

Per-OS subdirs deferred until Linux/Windows tests come online.

### D5. Initial coverage — partial

Shipped (PR-26):

- Single-plate layout, test pattern loaded into plate 0.
- Quad layout (Cmd+4), test pattern in plate 0, others empty.

Follow-up (PR-26b, after FX/LUT panels port):

- Plate 0 with `gamma2.4` LUT applied (verifies LUT actually samples).
- Plate 0 flipped + flopped (verifies the toggle reaches the shader).
- 2×2 layout with four distinct fixtures loaded.
- Retina viewport at 1×1 fills the entire viewport rect.

**Verification budget:** intentionally regress a shipped fix once the
follow-up assertions are in (the two-test floor isn't a robust enough
canary on its own).

## Phase E — Per-PR test gate (ongoing)

Once Phase C is green, every new Qt UI PR ships with at least one test
exercising the new surface. The PR template gets:

```
- [ ] Adds or extends a test in `tests/ui/`
- [ ] Runs `cmake --build build_qt --target qt-uitests` clean locally
```

Specifically for the remaining ports:

- **PR-19+ FX stack panel** — open panel, add an FX, select preset,
  remove it.
- **Render window** — open dialog, set output range, cancel without
  rendering.
- **Remote/networking window** — open, validate fields exist (no live
  connect in tests).
- **PR-LAST Load window** — open, navigate, cancel without loading.

## Out of scope (for now)

- **Linux / Windows UI tests.** Single-platform first. Add AT-SPI
  (Linux) and WinAppDriver (Windows) once the test API and locator
  conventions are stable.
- **Network/remote tests with real connections.** Mocked or fake
  bridge; no live RakNet sessions in CI.
- **Performance tests.** Benchmarks belong in their own harness.

## Risk register

| Risk                                                  | Mitigation                                                                |
| ----------------------------------------------------- | ------------------------------------------------------------------------- |
| Mac2 driver flake (XCUITest is famously slow to start) | Pre-warm Appium server once per spec file; use `wdio` retry on startup.   |
| GL viewport content untestable via AX                 | Coordinate-based interaction + (later) screenshot diff for visual proof.  |
| Per-plate widgets indexed by ID — IDs change          | `objectName` includes the plate index (`plate.0.*`); never relies on tab order. |
| Suite slows the dev loop                              | Two suites: `qt-uitests-fast` (smoke + layouts, ~15 s) and full.          |
| Tests touch user QSettings                            | `--config-dir` flag isolates everything to a temp dir.                    |

## Sequencing against in-flight work

1. **PR-21** input/render fixes — shipped.
2. **PR-23 Phase A** accessibility names + `--config-dir` flag — shipped.
3. **PR-24** macOS .app bundle — shipped.
4. **PR-25 Phase B** Appium harness scaffolding — shipped.
5. **PR-26 Phase C** baseline behavioral tests — shipped (22 passing).
6. **PR-27 Phase D** pixel-diff harness — shipped (2 baseline assertions;
   3 follow-ups gated on FX/LUT panel port).
7. Resume Qt window porting (FX, render, remote, load) — each PR ships
   with at least one behavioral test, plus a visual assertion when the
   port introduces new on-screen rendering.

Ongoing tax once Phases A–D are in: ~30–60 minutes per port PR.
