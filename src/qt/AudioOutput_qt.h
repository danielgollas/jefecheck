#pragma once
// The app's audio device: a gfcAudioOutput backed by Qt Multimedia's
// QAudioSink (pull mode). No Qt Multimedia types here, so any TU may include
// this header; Qt Multimedia itself stays inside AudioOutput_qt.cpp
// (developer_notes §1).
#include <string>

class gfcAudioOutput;

namespace jefe::qt::audio {

// A new, closed output. open(engine) starts it on the default output device
// without blocking the caller: the sink runs on its own thread and isOpen()
// turns true once the device has started. Follows default-device changes.
gfcAudioOutput* createOutput();
// Closes (waits for the audio thread to stop) and deletes. Null is fine.
void destroyOutput(gfcAudioOutput* out);

// Headless check of a real device (--video-load-test): opens the default
// output, checks its clock runs at wall-clock speed, leads a quiet tone, and
// measures step-slice latency. Prints "AUDIO-DEVICE: pass=N fail=N" (skips
// with pass=0 when there is no output device). Returns the failure count.
int deviceSelfTest();

}  // namespace jefe::qt::audio
