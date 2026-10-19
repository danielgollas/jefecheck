#ifndef GFCUPLOADPOOL_H
#define GFCUPLOADPOOL_H

// Frame upload without the GL thread copying pixels, for GPUs that cannot
// share an IOSurface (Windows, Linux, and macOS with IOSurface off).
//
// Staging buffers: a pool of pixel-unpack buffers (PBOs) that stay mapped.
// The decode thread writes a finished frame straight into one; the GL
// thread then starts the transfer with glTexSubImage2D from the buffer,
// which returns without copying on the CPU. Two levels, picked at runtime:
//   - persistent (ARB_buffer_storage + sync): mapped once for life; a fence
//     says when the GPU has finished reading a buffer so it can be reused.
//   - classic (GL 2.1 PBOs): the GL thread maps a buffer ahead of time,
//     unmaps it to upload, then orphans and re-maps it straight away.
//
// Texture pool: freed frame textures wait for a frame of the same size and
// format and are refilled with glTexSubImage2D instead of reallocated.
//
// The header stays GL-free so the decode-thread code needs no GL types.
// Every function marked "GL thread" needs the viewport context current.

#include <cstddef>

namespace gfcUpload {

struct Staging;   // one pooled buffer

/** Decode thread: a mapped buffer of exactly @a bytes, or nullptr when none
    is free (the pool then grows on the GL thread's next service()). Writes
    go to @a *data. Hand the result to upload() or give it back with
    abandon(). */
Staging* acquire(size_t bytes, void** data);

/** Any thread: returns a buffer that was acquired but never uploaded.
    Ignored for one upload() already consumed. Null is ignored. */
void abandon(Staging* staging);

/** GL thread: retires buffers the GPU has finished with and creates the
    buffers acquire() asked for. Cheap; called before every upload. */
void service();

/** GL thread: fills the texture bound to @a target (w x h) from @a staging.
    @a allocate: true for a texture with no storage yet (glTexImage2D),
    false to refill (glTexSubImage2D). Consumes the buffer either way;
    returns false (texture untouched) if the buffer could not be used. */
bool upload(Staging* staging, unsigned target, int internalFormat, int width, int height,
            unsigned format, unsigned type, bool allocate);

/** GL thread: a pooled texture with this target, format and size, already
    bound and with its parameters set, or 0 when none is waiting. */
unsigned takeTexture(unsigned target, int internalFormat, int width, int height);

/** GL thread: offers a frame's texture back to the pool, or deletes it when
    pooling is off, the format is not poolable, or the pool is full. */
void giveTexture(unsigned texture, unsigned target, int internalFormat, int width, int height);

/** True when a texture of this format can go through the pools (not
    compressed formats). */
bool poolable(unsigned target, int internalFormat);

struct Stats {
    const char* level;     // "persistent", "classic", "off" or "unknown"
    long stagedUploads;    // uploads that came from a staging buffer
    long stagingMisses;    // acquire() calls that found no free buffer
    long texturesReused;   // takeTexture() hits
    long texturesCreated;  // giveTexture() never saw these: fresh allocations
    size_t stagingBytes;   // memory held by staging buffers
    size_t pooledTextureBytes;
};
Stats stats();

/** Counts a freshly allocated texture (for stats only). */
void noteTextureCreated();

}  // namespace gfcUpload

#endif
