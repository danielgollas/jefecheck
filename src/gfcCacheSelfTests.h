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
// CACHE-RETARGET: every seek moves the window: random jumps arrive, a seek
// storm keeps the loader, clip and crash session, and a play-through has every
// frame resident in turn.
int cacheRetargetSelfTest(const std::string& mediaDir);
// CACHE-DIRECTION: reverse once, a bounce that fits, and a loop shrunk then
// widened.
int cacheDirectionSelfTest(const std::string& mediaDir);
// CACHE-TRACKS: three tracks with offsets hold the same timeline frames.
int cacheTracksSelfTest(const std::string& mediaDir);
// CACHE-PREFS: memory limit and lag policy persist; keepTime() follows sound.
int cachePrefsSelfTest(const std::string& mediaDir);
// CACHE-DISPLAY: what a plate shows for an unloaded frame (Keep time holds the
// last drawn frame, a jump shows nothing and is pending), forced loads, the
// on-screen pin and the held frame of hold mode 1.
int cacheDisplaySelfTest(const std::string& mediaDir);
// CACHE-RENDER: rendering 30 frames far from a settled window (budget 10
// frames, previews off) through the real plate path writes 30 non-black
// pictures and leaves the cache, the on-screen pin and the playhead as they were.
int cacheRenderSelfTest(const std::string& mediaDir);
// AUDIO-HOLES: the hole-tolerant clip, the audio-only reader, and movie sound
// decoded on its own thread: complete under a small picture budget, a jump
// filled first then backfilled, a load from frame N lined up with the source.
int audioHolesSelfTest(const std::string& mediaDir);
// CACHE-KEEPTIME: Keep time with a loader at half the play rate: the sound
// leads, frames are skipped, time is kept; skipped frames load afterwards; the
// pre-play buffer holds play (silent) until ~0.25 s ahead is in.
int cacheKeepTimeSelfTest(const std::string& mediaDir);
// CACHE-EVERYFRAME: Show every frame with the same loader: playback slows to
// it one frame at a time, the sound never leads, every frame shown is resident.
int cacheEveryFrameSelfTest(const std::string& mediaDir);
// CACHE-SHUTDOWN: the app's quit path joins every loader and audio thread
// and wakes the coordinator's waiters; nothing loads after it. Runs last: the
// tracks stay shut down.
int cacheShutdownSelfTest(const std::string& mediaDir);
