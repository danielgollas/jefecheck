// Declaration for the jefe::wire self-test (JEF-23), implemented in
// gfcWireTest.cpp. Kept separate from gfcWire.h so pulling in the wire
// primitives never drags in the test's <cstdio>/<cmath> includes.
#pragma once

namespace jefe {
namespace wire {

// Runs the jefe::wire round-trip/bounds-safety self-test, prints
// "WIRE-TEST: pass=<N> fail=<M>" and returns 0 if fail==0, else 2.
int selfTest();

}  // namespace wire
}  // namespace jefe
