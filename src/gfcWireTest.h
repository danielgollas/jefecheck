// Declaration for the jefe::wire self-test (gfcWireTest.cpp), kept apart from
// gfcWire.h so the wire primitives don't pull in <cstdio>/<cmath>.
#pragma once

namespace jefe {
namespace wire {

// Runs the jefe::wire round-trip/bounds-safety self-test, prints
// "WIRE-TEST: pass=<N> fail=<M>" and returns 0 if fail==0, else 2.
int selfTest();

}  // namespace wire
}  // namespace jefe
