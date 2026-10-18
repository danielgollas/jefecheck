#ifndef GFCIOSURFACE_H
#define GFCIOSURFACE_H

// Shared CPU/GPU frame memory on macOS (IOSurface). On Apple silicon the CPU
// and GPU share one pool of memory, so a frame decoded straight into an
// IOSurface can back a GL texture without the copy (and the main-thread time)
// that glTexImage2D costs. The decode thread fills the surface; the GL thread
// only binds it.
//
// This header stays free of GL and Apple headers: Apple's OpenGL headers and
// glad cannot share a translation unit, so callers pass plain GL enum values.
// Off macOS every function is a stub that reports "unavailable", and callers
// keep the glTexImage2D path.

#include <cstddef>
#include <cstdint>

namespace gfcSurface {

/** The surface layouts the loaders produce. */
enum class Layout {
    BGRA8,     // 8-bit BGRA      (GL_BGRA / GL_UNSIGNED_INT_8_8_8_8_REV)
    RGBA16,    // 16-bit RGBA     (GL_RGBA / GL_UNSIGNED_SHORT)
    RGBAHalf,  // half-float RGBA (GL_RGBA / GL_HALF_FLOAT)
};

/** True when IOSurface-backed textures can be used at all (macOS, not
    disabled with JEFECHECK_NO_IOSURFACE=1). */
bool available();

/** A new surface of @a width x @a height in @a layout, or nullptr. The caller
    owns one reference: hand it to bindToTexture and/or release(). */
void* create(int width, int height, Layout layout);

/** Locks @a surface for CPU writing and returns its base address; the row
    pitch (which may be wider than width * bytes-per-pixel) goes to
    @a bytesPerRow. Pair every successful lock with unlock(). */
void* lockForWriting(void* surface, size_t* bytesPerRow);
void unlock(void* surface);

/** Adds / drops one reference. Null is ignored. Dropping the last reference
    returns the surface to a pool (up to 512 MiB) that create() reuses for
    the same size and layout once the GPU is done with it. */
void retain(void* surface);
void release(void* surface);

struct PoolStats {
    long created;        // surfaces allocated so far
    long reused;         // create() calls answered from the pool
    size_t pooled;       // surfaces waiting in the pool
    size_t pooledBytes;
};
PoolStats poolStats();

/** Frees every pooled surface. */
void drainPool();

/** Makes the texture bound to GL_TEXTURE_RECTANGLE_ARB on the current CGL
    context use @a surface as its storage (CGLTexImageIOSurface2D). The
    texture does NOT keep the surface alive: hold a reference until after
    glDeleteTextures (selfTest checks this). Returns false (leaving the
    texture without storage) when the context refuses the layout. */
bool bindToTexture(void* surface, int width, int height, Layout layout);

/** Reads a GL_TEXTURE_RECTANGLE_ARB texture back as RGBA floats by drawing
    it into a float FBO: glGetTexImage returns nothing for IOSurface-backed
    textures. False off macOS. */
bool readBack(unsigned texture, int width, int height, float* rgba);

/** How many bindToTexture calls have succeeded (for the upload bench). */
long boundCount();

/** Round-trips known pixels through a surface-backed texture for every
    layout on the current GL context (drawn, not glGetTexImage), and reports
    whether binding took a reference on the surface. Prints one IOSURFACE-TEST line
    per layout; returns the number of layouts that work (0 off macOS). */
int selfTest();

}  // namespace gfcSurface

#endif
