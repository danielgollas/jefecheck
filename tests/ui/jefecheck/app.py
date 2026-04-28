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


class JefeCheckApp:
    """A live JefeCheck instance under Appium control."""

    def __init__(self, driver: webdriver.Remote, config_dir: Path):
        self.driver = driver
        self.config_dir = config_dir

    @classmethod
    def launch(
        cls,
        binary: Path,
        appium_url: str = DEFAULT_APPIUM_URL,
        config_dir: Optional[Path] = None,
    ) -> "JefeCheckApp":
        """Start the app under Appium and return a wrapped client."""
        if config_dir is None:
            config_dir = Path(tempfile.mkdtemp(prefix="jefecheck-test-"))
        config_dir.mkdir(parents=True, exist_ok=True)

        opts = Mac2Options()
        opts.bundle_id = locators.BUNDLE_ID
        opts.app = str(binary)
        opts.arguments = ["--config-dir", str(config_dir)]
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
        return cls(driver, config_dir)

    def quit(self) -> None:
        try:
            self.driver.quit()
        finally:
            shutil.rmtree(self.config_dir, ignore_errors=True)

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
                return self.driver.find_element(AppiumBy.IOS_PREDICATE, predicate)
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
