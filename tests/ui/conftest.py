"""Pytest fixtures — Appium server lifecycle + per-test JefeCheck launch.

The harness expects two things on PATH:
  - `appium` (npm i -g appium && appium driver install mac2)
  - JEFECHECK_BIN env var pointing at JefeCheck.app (set by the
    `qt-uitests` CMake target; may also be set manually for ad-hoc runs)
"""
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
    """Start Appium in the background unless one is already listening.

    Reusing an external server is the common dev workflow (faster
    iteration). CI starts its own; we exit cleanly either way.
    """
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
            # AppleScript is gated as an "insecure" feature; UI tests need
            # it because Mac2's macos:keys can't deliver Cmd+modifier
            # shortcuts to Qt's QShortcut on macOS, but System Events
            # keystroke (via AppleScript) does.
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
    # --slow-mo SECONDS: pause after every Mac2 click / keystroke so a
    # human watching the screen can see what each step does. Defaults to
    # 0 (CI / dev runs untouched). Pure debugging aid — nothing in the
    # suite asserts on timing, so a non-zero value never changes pass/fail.
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


@pytest.fixture(scope="session")
def slow_mo(request) -> float:
    return float(request.config.getoption("--slow-mo"))


@pytest.fixture
def app(appium_server, jefecheck_binary, tmp_path, slow_mo):
    """Per-test launch: fresh app, isolated config dir, torn down after."""
    instance = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=tmp_path / "jefecheck-config",
        slow_mo=slow_mo,
    )
    yield instance
    instance.quit()
