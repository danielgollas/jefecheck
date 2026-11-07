"""Pytest fixtures: Appium server lifecycle + per-test JefeCheck launch.
Needs `appium` on PATH and JEFECHECK_BIN pointing at JefeCheck.app (set by the `qt-uitests` CMake target)."""
from __future__ import annotations

import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from jefecheck import JefeCheckApp
from jefecheck.visual import fixtures_dir, make_test_pattern

APPIUM_HOST = "127.0.0.1"
APPIUM_PORT = 4723
APPIUM_URL = f"http://{APPIUM_HOST}:{APPIUM_PORT}"


def _port_open(host: str, port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.2)
        try:
            s.connect((host, port))
            return True
        except OSError:
            return False


def _wait_for_appium(timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _port_open(APPIUM_HOST, APPIUM_PORT):
            return
        time.sleep(0.5)
    raise RuntimeError(f"Appium server did not start on {APPIUM_URL}")


@pytest.fixture(scope="session")
def appium_server():
    """Start Appium in the background unless one is already listening
    (reusing an external server is the common dev workflow)."""
    if _port_open(APPIUM_HOST, APPIUM_PORT):
        yield APPIUM_URL
        return

    appium_bin = shutil.which("appium")
    if not appium_bin:
        pytest.skip(
            "Appium not found on PATH. Install with `npm i -g appium && "
            "appium driver install mac2`."
        )

    log_path = Path(__file__).parent / "appium-server.log"
    log_file = log_path.open("w", buffering=1)
    proc = subprocess.Popen(
        [
            appium_bin, "--address", APPIUM_HOST, "--port", str(APPIUM_PORT),
            # AppleScript is gated as "insecure"; needed because Mac2's macos:keys
            # can't deliver Cmd+modifier shortcuts to Qt's QShortcut on macOS.
            "--allow-insecure", "mac2:apple_script",
        ],
        stdout=log_file, stderr=subprocess.STDOUT,
    )
    try:
        _wait_for_appium()
        yield APPIUM_URL
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        log_file.close()


@pytest.fixture(scope="session")
def jefecheck_binary() -> Path:
    """Resolve the JefeCheck.app path. Set JEFECHECK_BIN to override."""
    env = os.environ.get("JEFECHECK_BIN")
    if env:
        p = Path(env)
        if p.suffix == ".app" and p.exists():
            return p

    # Walk up looking for a build dir with a .app inside.
    here = Path(__file__).resolve().parent
    for parent in [here, *here.parents]:
        for candidate in parent.glob("build*/jefecheck.app"):
            if candidate.exists():
                return candidate
    pytest.skip(
        "Could not find jefecheck.app. Build with USE_QT=ON or set "
        "JEFECHECK_BIN=/path/to/JefeCheck.app."
    )


def pytest_addoption(parser):
    # --slow-mo SECONDS: pause after every Mac2 click / keystroke so a human can
    # watch; nothing asserts on timing.
    parser.addoption(
        "--slow-mo",
        action="store",
        type=float,
        default=0.0,
        metavar="SECONDS",
        help="Pause this many seconds after every UI interaction (click, "
             "send_keys, send_shortcut). Useful for watching the suite "
             "drive the app. Default: 0.",
    )
    # --update-baselines: regenerate visual-diff baselines after an intentional UI change.
    parser.addoption(
        "--update-baselines",
        action="store_true",
        default=False,
        help="Overwrite baseline screenshots in tests/ui/baselines/ "
             "instead of comparing. Use after intentional UI changes.",
    )


@pytest.fixture(scope="session")
def slow_mo(request) -> float:
    return float(request.config.getoption("--slow-mo"))


@pytest.fixture(scope="module")
def app(appium_server, jefecheck_binary, tmp_path_factory, slow_mo):
    """Module-scoped launch: one JefeCheck per test file (pays the ~15s WDA cold start once).
    Tests share state, so a mutating test must restore it; QSettings live in a per-module temp dir."""
    config_dir = tmp_path_factory.mktemp("jefecheck-config")
    instance = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=config_dir,
        slow_mo=slow_mo,
    )
    yield instance
    instance.quit()


# Visual regression -----------------------------------------------

@pytest.fixture(scope="session")
def update_baselines(request) -> bool:
    return bool(request.config.getoption("--update-baselines"))


@pytest.fixture(scope="session")
def test_pattern_image() -> Path:
    """4-quadrant RGB test pattern (tests/ui/fixtures/). The filename has no trailing digits:
    findSequenceFiles would read them as a frame number and 'Loaded:' would show `foo_##.png`."""
    return make_test_pattern(fixtures_dir() / "test_pattern.png")


@pytest.fixture
def visual_app(appium_server, jefecheck_binary, tmp_path,
               test_pattern_image):
    """Launches JefeCheck with the test pattern preloaded into plate 0, so the
    viewport has deterministic content for baselines."""
    instance = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=tmp_path / "jefecheck-config",
        open_files=[test_pattern_image],
    )
    yield instance
    instance.quit()


@pytest.fixture(scope="session")
def multiview_sequence() -> Path:
    """First frame of the committed multi-view EXR sequence (4 frames, 64x64, default RGB + `right` view).
    Frame 0001 lets findSequenceFiles find the rest; the layer-switch test needs getNumPreviewFrames() > 1."""
    seq_dir = fixtures_dir() / "multiview_seq"
    first_frame = seq_dir / "multiview.0001.exr"
    if not first_frame.exists():
        pytest.skip(f"Multi-view EXR sequence missing at {seq_dir}")
    return first_frame


@pytest.fixture
def multiview_app(appium_server, jefecheck_binary, tmp_path,
                  multiview_sequence):
    """Launches JefeCheck with the multi-view EXR sequence preloaded (plate 0's
    track has more than one named layer and more than one frame)."""
    instance = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=tmp_path / "jefecheck-config",
        open_files=[multiview_sequence],
    )
    yield instance
    instance.quit()
