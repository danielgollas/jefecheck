#pragma once
// SMPTE timecode: parse, format and frame-count math, including drop-frame.
// Pure functions; no libav or Qt.
#include <cstdint>
#include <string>

struct gfcTimecode { int hh = 0, mm = 0, ss = 0, ff = 0; bool drop = false; };

// Accepts HH:MM:SS:FF (non-drop) and HH:MM:SS;FF (drop). Rejects anything else.
bool gfcParseTimecode(const std::string& s, gfcTimecode& out);
// Counting rate: ceil(num/den). 24000/1001 -> 24, 30000/1001 -> 30.
int gfcNominalFps(int num, int den);
// Frames since 00:00:00:00. Drop-frame (tc.drop) removes 2*(nominal/30) numbers
// at the start of every minute except each tenth minute.
int64_t gfcTimecodeToFrames(const gfcTimecode& tc, int num, int den);
gfcTimecode gfcFramesToTimecode(int64_t frames, int num, int den, bool drop);
// "HH:MM:SS:FF", or "HH:MM:SS;FF" when drop.
std::string gfcFormatTimecode(const gfcTimecode& tc);

int timecodeSelfTest(const std::string& mediaDir);
