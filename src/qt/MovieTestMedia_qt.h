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

}  // namespace jefe::qt::testmedia

#endif
