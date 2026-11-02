// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Headless suites behind --cache-test. Each prints "<SUITE>: pass=N fail=N" and
// returns the failure count. No manager includes, so main_qt.cpp can use it.
#include <string>

int memoryMeasureSelfTest(const std::string& mediaDir);
