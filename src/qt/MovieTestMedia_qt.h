// SPDX-License-Identifier: GPL-2.0-or-later
// Test media for the --video-load-test suites, generated at test time with the
// ffmpeg CLI (never committed). QtCore only - no glad, no managers
// (developer_notes.md section 1).
#ifndef JEFECHECK_QT_MOVIE_TEST_MEDIA_H
#define JEFECHECK_QT_MOVIE_TEST_MEDIA_H

#include <QString>

namespace jefe::qt::testmedia {

// Writes the fixed set of test clips into dir (created if missing). Returns
// false with *err set when ffmpeg is not available or any clip fails.
bool generate(const QString& dir, QString* err);

// Writes the --cache-test clips into dir: cache_beep_24.mp4, cache_silent_24.mp4,
// cache_seq.%04d.png (120 frames) and cache_big_24.mp4 (1080p, 48 frames).
bool generateCacheMedia(const QString& dir, QString* err);

}  // namespace jefe::qt::testmedia

#endif
