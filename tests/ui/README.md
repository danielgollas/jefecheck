# JefeCheck UI tests

Appium-driven UI tests for the Qt build. Covers:

- **Behavioral tests** (`test_layouts.py`, `test_plate_ops.py`,
  `test_transport.py`, `test_lut.py`) — assert on AX attributes
  exposed by Qt's QAccessible bridge.
- **Visual regression tests** (`test_visual.py`) — pixel-diff the main
  window against committed baselines under `baselines/`.

## One-time setup (macOS only)

```bash
# Node 22+ (Appium 3 requires it). nvm makes this painless:
nvm install 22 && nvm use 22

# Appium server + mac2 driver
npm install -g appium
appium driver install mac2

# Python deps (the qt-uitests CMake target also handles this)
cd tests/ui
python3 -m venv .venv
source .venv/bin/activate
pip install -e .
```

The `mac2` driver builds WebDriverAgentMac on first run via Xcode —
that takes a minute and requires **Xcode** (not just the Command Line
Tools).

### Accessibility permission (required, one-time)

WebDriverAgent runs as an XCTest UI test, which needs **Accessibility**
permission to drive other apps' UI. After the first failed test run,
do this:

1. Open **System Settings → Privacy & Security → Accessibility**.
2. Find `WebDriverAgentRunner-Runner` (added by the failed attempt).
3. Toggle it ON.
4. If `Terminal.app` / `iTerm.app` / your shell's host is also in the
   list, toggle it ON too.

Symptom of missing permission: xcodebuild logs
`Failed to initialize for UI testing: Timed out while enabling
automation mode.`

## Running

```bash
# From the repo root
cmake --build build_qt
cmake --build build_qt --target qt-uitests
```

Or directly:

```bash
cd tests/ui
source .venv/bin/activate
JEFECHECK_BIN=$(pwd)/../../build_qt/jefecheck.app pytest
```

### Watching the suite drive the app (`--slow-mo`)

Mac2 fires AX press actions back-to-back in well under a second, so by
default the UI changes flicker by faster than the eye can follow. Pass
`--slow-mo SECONDS` to pause after every click / keystroke / shortcut:

```bash
pytest test_plate_ops.py::test_flip_button_toggles_when_clicked --slow-mo 1.0
```

Pure debugging aid — assertions never check timing, so a non-zero
slow-mo doesn't change pass/fail. Drag the JefeCheck window into a
visible spot before the test launches WDA and you'll see each toggle.

## Pre-push hook (release-tag gate)

The repo ships a pre-push hook that runs the full UI suite (behavioral +
visual) when you push a tag — typically a release tag like `v1.7.0`.
Branch pushes pass through without running the suite.

Wire it up once per clone:

```bash
git config core.hooksPath scripts/git-hooks
```

After that, `git push origin v1.7.0` builds the Qt app and runs
`cmake --build build_qt --target qt-uitests` before the push leaves your
machine. A failed test aborts the push. Make sure `nvm use 22` is active
in the shell you push from — Appium 3 needs Node 22+.

## Visual regression tests

`test_visual.py` boots the app with a committed test pattern preloaded
into plate 0 (via `--open-file`), captures the main window, and
asserts it matches a baseline PNG under `baselines/`.

When intentionally changing the UI (theme, layout, dock arrangement),
regenerate baselines:

```bash
JEFECHECK_BIN=$(pwd)/../../build_qt/jefecheck.app \
  pytest test_visual.py --update-baselines
```

A failed diff writes `<baseline>.actual.png` and `<baseline>.diff.png`
next to the baseline so the developer can eyeball the regression.

The fixture image (`fixtures/test_pattern_64.png`) is a 4-quadrant RGB
pattern (red / green / blue / yellow). Flip / flop / rotate regressions
swap the colored regions and produce huge diff ratios — well above the
0.5% jitter budget.

## Test isolation (module-scoped `app`)

The `app` fixture is **module-scoped** — one JefeCheck launch per
`test_*.py` file, shared across every test in that file. Pays the
~15s WDA + Mac2 cold start once per module instead of per test
(suite went from ~8 min to ~2 min for the current 22 tests).

This means tests in the same module **share state**. Three patterns
keep tests safe:

1. **Force the state you assert on.** Don't write
   `assert layout == "single"` after a launch — write
   `app.send_shortcut("cmd+1"); assert layout == "single"`.
   Tests in `test_layouts.py` follow this pattern.
2. **Add an autouse cleanup fixture in modules that mutate state.**
   `test_plate_ops.py` resets every plate's flip/flop/crop to off
   after each test, so a test that toggles flop on doesn't leak into
   the next test that asserts flop=off.
3. **First-test-runs-first for default-state assertions.** Pytest
   collects tests in file order. A `test_default_*` style assertion
   that depends on the just-launched state must be the first
   function in the file.

Read-only tests (smoke, transport, lut existence checks) don't need
cleanup — nothing they do mutates state.

## Architecture

- `conftest.py` — pytest fixtures. Auto-starts an Appium server on
  127.0.0.1:4723 if none is listening; reuses one if already running.
  The `app` fixture launches a fresh JefeCheck per test with a temp
  `--config-dir` so QSettings / session state never leak between tests.
- `jefecheck/app.py` — `JefeCheckApp` helper. `by_object_name(name)`
  matches Qt's full-chain AXIdentifier with a `ENDSWITH` predicate so
  tests are robust against widget reparenting.
- `jefecheck/locators.py` — symbolic constants for every widget's
  `objectName` leaf. Per-plate widgets use the `plate(idx, role)`
  helper.

## Adding a test

1. Pick the surface (e.g. timeline transport).
2. Use `app.by_object_name(locators.<NAME>)` to find widgets.
3. Assert on `widget.get_attribute("title")` (the AX-bridged
   `accessibleName`) or `widget.text` for current value.

When porting a new Qt window, set `objectName` + `accessibleName` on
every interactive widget, add the leaf names to `locators.py`, and
ship a test in this directory alongside the PR.
