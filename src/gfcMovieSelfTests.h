#pragma once
// Headless suites behind --video-load-test. Each prints "<SUITE>: pass=N fail=N"
// and returns the failure count. No manager includes, so main_qt.cpp can use it.
#include <string>

int movieKindsSelfTest(const std::string& mediaDir);
int movieLibavSelfTest(const std::string& mediaDir);
