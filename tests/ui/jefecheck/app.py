"""JefeCheckApp helper — thin wrapper over Appium's mac2 driver.

Tests use this instead of touching webdriver directly so the locator
strategy and capability boilerplate live in one place.
"""
from __future__ import annotations

import os
import shutil
import tempfile
import time
from pathlib import Path
from typing import Optional

from appium import webdriver
from appium.options.mac import Mac2Options
from appium.webdriver.common.appiumby import AppiumBy
from appium.webdriver.webelement import WebElement

from . import locators

DEFAULT_APPIUM_URL = "http://127.0.0.1:4723"


class _SlowElement:
    """Proxy around a Mac2 WebElement that pauses after each interaction.

    Active only when --slow-mo > 0 is passed to pytest. Lets a human
    watching the screen catch each click/toggle before the next one
    happens — by default Mac2 fires AX press actions back-to-back in
    well under a second, faster than the eye can follow.

    The proxy delegates everything to the underlying element via
    __getattr__; only the interaction methods listed in `_SLOW_METHODS`
    sleep after they return. Read-only attribute access (`get_attribute`,
    `text`, etc.) goes through unchanged so assertion timing is untouched.
    """

    _SLOW_METHODS = ("click", "send_keys", "clear", "submit")

    def __init__(self, element: WebElement, slow_mo: float):
        # Underscore-prefixed so `__getattr__` still triggers for the
        # delegated members (Python skips __getattr__ for things found
        # on the proxy itself).
        self._element = element
        self._slow_mo = slow_mo

    def __getattr__(self, name: str):
        attr = getattr(self._element, name)
        if name in self._SLOW_METHODS and self._slow_mo > 0 and callable(attr):
            slow = self._slow_mo
            def wrapped(*args, **kwargs):
                result = attr(*args, **kwargs)
                time.sleep(slow)
                return result
            return wrapped
        return attr


class JefeCheckApp:
    """A live JefeCheck instance under Appium control."""

    def __init__(self, driver: webdriver.Remote, config_dir: Path,
                 slow_mo: float = 0.0, owns_config_dir: bool = True):
        self.driver = driver
        self.config_dir = config_dir
        # When the caller supplies their own config_dir (e.g. the
        # cross-launch persistence test), they own its lifecycle.
        # Otherwise quit() clears the temp dir we created.
        self.owns_config_dir = owns_config_dir
        # Inserted between every Mac2 interaction (click, key, shortcut)
        # so a human watching the screen can follow what each step does.
        # 0 disables the pause entirely; non-zero only slows the suite —
        # nothing in the assertions depends on timing.
        self.slow_mo = slow_mo

    @classmethod
    def launch(
        cls,
        binary: Path,
        appium_url: str = DEFAULT_APPIUM_URL,
        config_dir: Optional[Path] = None,
        slow_mo: float = 0.0,
        open_files: Optional[list[Path]] = None,
    ) -> "JefeCheckApp":
        """Start the app under Appium and return a wrapped client.

        `open_files`: paths to load into plates 0..3 in order. Used by
        visual-diff tests so the viewport has known content before the
        screenshot is taken (otherwise a fresh launch shows an empty
        black viewport and every test would hit the same baseline).
        """
        owns_config_dir = config_dir is None
        if config_dir is None:
            config_dir = Path(tempfile.mkdtemp(prefix="jefecheck-test-"))
        config_dir.mkdir(parents=True, exist_ok=True)

        args = ["--config-dir", str(config_dir)]
        if open_files:
            for path in open_files:
                args += ["--open-file", str(path)]

        opts = Mac2Options()
        opts.bundle_id = locators.BUNDLE_ID
        opts.app = str(binary)
        opts.arguments = args
        opts.environment = {"JEFECHECK_CONFIG_DIR": str(config_dir)}
        # mac2 spends most of its time on the initial bundle path resolve
        # and a (slow) UI tree warm-up; give it room without hiding bugs.
        opts.set_capability("appium:newCommandTimeout", 120)
        # WebDriverAgentMac needs to build with xcodebuild on first run.
        # Cold path is 60-90s; warm is sub-second.
        opts.set_capability("appium:webDriverAgentMacUrl", None)
        opts.set_capability("appium:wdaLaunchTimeout", 240000)
        opts.set_capability("appium:wdaConnectionTimeout", 240000)

        driver = webdriver.Remote(appium_url, options=opts)
        instance = cls(driver, config_dir, slow_mo=slow_mo,
                       owns_config_dir=owns_config_dir)
        # Synchronize on the main window being AX-visible before returning.
        # Without this the first synthesized keystroke after launch can
        # race the app's window-activation phase and silently no-op.
        instance.main_window()
        return instance

    def quit(self) -> None:
        try:
            self.driver.quit()
        finally:
            if self.owns_config_dir:
                shutil.rmtree(self.config_dir, ignore_errors=True)

    def _wrap(self, element: WebElement):
        """Wrap with a slow-mo proxy when --slow-mo is active."""
        if self.slow_mo > 0:
            return _SlowElement(element, self.slow_mo)
        return element

    def by_object_name(self, name: str, timeout: float = 5.0) -> WebElement:
        """Find a widget whose AXIdentifier ends with `name`.

        Qt's identifier is the full parent chain
        (e.g. `QApplication.MainWindow.dock.platemanager.....plate.0.gamma.spin`),
        so we use a predicate suffix-match for stability.
        """
        predicate = f"identifier ENDSWITH '{name}'"
        deadline = time.monotonic() + timeout
        last_err: Optional[Exception] = None
        while time.monotonic() < deadline:
            try:
                el = self.driver.find_element(AppiumBy.IOS_PREDICATE, predicate)
                return self._wrap(el)
            except Exception as e:  # noqa: BLE001
                last_err = e
                time.sleep(0.2)
        raise AssertionError(
            f"Widget not found: {name} (predicate: {predicate})"
        ) from last_err

    def by_object_name_optional(self, name: str) -> Optional[WebElement]:
        try:
            return self.by_object_name(name, timeout=0.5)
        except AssertionError:
            return None

    def main_window(self) -> WebElement:
        return self.driver.find_element(
            AppiumBy.IOS_PREDICATE,
            "elementType == 4 AND title == 'JefeCheck'")

    def wait_for_startup_ready(self, timeout: float = 30.0) -> str:
        """Block until the app's startup status label reaches a terminal state.

        The status walks through 'Loading LUTs…' → 'Loading FXs…' →
        'Ready (X FX, Y LUT)' (or 'Errors (...)') as the autoload
        finishes. Tests that read FX/LUT panel contents call this
        before asserting so they don't race the load. Returns the
        final label text so callers can assert on the FX/LUT counts
        without re-querying.

        Re-queries the element each iteration and tolerates
        StaleElementReference / NoSuchElement: the label's text
        updates rapidly (every ~16ms during FX autoload), and Mac2's
        AX cache invalidates the element reference whenever the text
        changes — a single resolve-then-read pattern races the
        update and throws. Treat any read failure as "not yet
        terminal" and try again.
        """
        from selenium.common.exceptions import (
            StaleElementReferenceException,
            NoSuchElementException,
        )
        deadline = time.monotonic() + timeout
        last_text = ""
        while time.monotonic() < deadline:
            try:
                label = self.by_object_name_optional(locators.STATUSBAR_STARTUP)
                if label is not None:
                    text = label.get_attribute("value") or label.text or ""
                    last_text = text
                    if text.startswith("Startup: Ready") or \
                            text.startswith("Startup: Errors"):
                        return text
            except (StaleElementReferenceException,
                    NoSuchElementException):
                pass
            time.sleep(0.2)
        raise AssertionError(
            f"Startup did not reach a terminal state within {timeout}s "
            f"(last seen: {last_text!r})"
        )

    # XCUIKeyModifierFlags — see Apple docs.
    MOD_SHIFT   = 1 << 17
    MOD_CONTROL = 1 << 18
    MOD_OPTION  = 1 << 19
    MOD_COMMAND = 1 << 20

    def send_keys(self, key: str, modifier_flags: int = 0) -> None:
        """Send a single keystroke at the application level.

        `key` is a literal character (e.g. '1', 'f') or an XCUIKeyboardKey
        token (e.g. '' for Return).
        """
        self.driver.execute_script(
            "macos: keys",
            {"keys": [{"key": key, "modifierFlags": modifier_flags}]})
        if self.slow_mo > 0:
            time.sleep(self.slow_mo)

    def send_shortcut(self, combo: str) -> None:
        """Send a Cmd/Ctrl/Shift/Opt-modified shortcut to JefeCheck.

        Format: 'cmd+1', 'shift+f', 'cmd+,', etc. Only one main key.

        Routes through System Events / AppleScript instead of XCTest's
        UIKeyEvent synthesis. Mac2's `macos: keys` doesn't reliably
        deliver Cmd+modifier shortcuts to Qt's QShortcut on macOS
        (synthesized events don't propagate through Qt's window event
        filter chain), but System Events keystroke does — it's the same
        mechanism used by GUI scripting.
        """
        parts = [p.strip().lower() for p in combo.split("+")]
        modifiers: list[str] = []
        key: Optional[str] = None
        for part in parts:
            if part in ("cmd", "command", "meta"):
                modifiers.append("command down")
            elif part in ("ctrl", "control"):
                modifiers.append("control down")
            elif part == "shift":
                modifiers.append("shift down")
            elif part in ("opt", "option", "alt"):
                modifiers.append("option down")
            else:
                key = part
        if key is None:
            raise ValueError(f"send_shortcut: no main key in {combo!r}")
        using_clause = ""
        if modifiers:
            using_clause = " using {" + ", ".join(modifiers) + "}"
        # Force the JefeCheck process frontmost via System Events (more
        # immediate than `tell application to activate`, which returns
        # before the activation actually completes), wait long enough
        # for the AppKit window-server handoff to settle, then deliver
        # the keystroke. The settling time matters: the first
        # synthesized keystroke after a fresh launch races the focus
        # transition and silently no-ops if delivered too soon.
        script = (
            'tell application "System Events"\n'
            '  set frontmost of (first process whose '
            f'    bundle identifier is "{locators.BUNDLE_ID}") to true\n'
            '  delay 0.4\n'
            f'  keystroke "{key}"{using_clause}\n'
            'end tell'
        )
        self.driver.execute_script("macos: appleScript", {"script": script})
        if self.slow_mo > 0:
            time.sleep(self.slow_mo)

    def window_screenshot(self) -> bytes:
        """Capture the JefeCheck main window as PNG bytes.

        Routes through the AX element's screenshot endpoint rather than
        the whole-display capture so the result is independent of the
        host's display arrangement (CI runners and dev machines have
        different resolutions, menubar heights, dock positions). The
        captured frame includes window chrome — the title bar and dock
        separators are stable enough that the chrome doesn't dominate
        the diff.
        """
        return self.main_window().screenshot_as_png
