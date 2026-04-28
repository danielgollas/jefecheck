# JefeCheck UI tests

Appium-driven UI tests for the Qt build. Phase B scaffold — only a
smoke test today; baseline tests land in Phase C.

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
