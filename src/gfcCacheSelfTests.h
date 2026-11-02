// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Headless suites behind --cache-test. Each prints "<SUITE>: pass=N fail=N" and
// returns the failure count. No manager includes, so main_qt.cpp can use it.
#include <string>

int memoryMeasureSelfTest(const std::string& mediaDir);
// FRAME-STATE: gfcFrameTable transitions, a sequence's resident set after a
// load and an eviction, hold-edge pins, and the bridge's timeline segments.
int frameStateSelfTest(const std::string& mediaDir);
// CACHE-POLICY: gfcFrameCache window, unload order, loop/bounce/reverse walks,
// minimum ahead, pins, offsets, mixed sizes and pool trim. No GL.
int cachePolicySelfTest();
