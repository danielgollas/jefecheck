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
// CACHE-BUDGET: the live loaders under an injected byte budget (one and two
// tracks, retargets, a lowered budget) and a reloaded frame's thumbnail.
int cacheBudgetSelfTest(const std::string& mediaDir);
// CACHE-SETTLE: a full budget, a loop that fits and a clip that fits load
// nothing more and evict nothing.
int cacheSettleSelfTest(const std::string& mediaDir);
// CACHE-LEAKS: load / retarget / clear cycles return textures, surfaces,
// cache and pool bytes to the baseline; loader errors and announcements.
int cacheLeaksSelfTest(const std::string& mediaDir);
// CACHE-HOLD: hold-edge frames outside the window stay resident.
int cacheHoldSelfTest(const std::string& mediaDir);
