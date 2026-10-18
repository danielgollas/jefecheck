// IOSurface stubs for platforms without it: every call reports "unavailable",
// so callers keep the glTexImage2D path. See gfcIOSurface.h.

#include "gfcIOSurface.h"

namespace gfcSurface {

bool available() { return false; }
void* create(int, int, Layout) { return nullptr; }
void* lockForWriting(void*, size_t*) { return nullptr; }
void unlock(void*) {}
void retain(void*) {}
void release(void*) {}
bool bindToTexture(void*, int, int, Layout) { return false; }
bool readBack(unsigned, int, int, float*) { return false; }
PoolStats poolStats() { return {0, 0, 0, 0}; }
void drainPool() {}
long boundCount() { return 0; }
int selfTest() { return 0; }

}  // namespace gfcSurface
