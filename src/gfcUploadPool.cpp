// Staging-buffer and texture pools for frame upload. See gfcUploadPool.h.

#include "gfcUploadPool.h"

#include <glad/glad.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace gfcUpload {

namespace {

enum class Level { Unknown, Off, Classic, Persistent };

// Staging and texture memory each stay under this. 512 MiB holds seven UHD
// half-float frames, more than a read-ahead queue keeps waiting at once.
constexpr size_t kStagingCapBytes = size_t(512) << 20;
constexpr size_t kTextureCapBytes = size_t(512) << 20;

bool envSet(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

}  // namespace

struct Staging {
    enum class State { Free, Held, InFlight };
    GLuint buffer = 0;
    size_t bytes = 0;
    void* data = nullptr;     // mapped address while Free/Held
    State state = State::Free;
    GLsync fence = nullptr;   // persistent level, while InFlight
};

namespace {

std::atomic<Level> g_level{Level::Unknown};
std::mutex g_mutex;                    // guards g_staging, g_wanted
std::vector<Staging*> g_staging;
std::map<size_t, int> g_wanted;        // bytes -> acquire() misses since service()
size_t g_stagingBytes = 0;

struct PooledTexture {
    GLuint id;
    GLenum target;
    GLint internalFormat;
    int width, height;
    size_t bytes;
};
std::vector<PooledTexture> g_textures;  // GL thread only
size_t g_textureBytes = 0;

std::atomic<long> g_stagedUploads{0}, g_stagingMisses{0}, g_texReused{0}, g_texCreated{0};

size_t bytesPerPixel(GLint internalFormat) {
    switch (internalFormat) {
    case GL_RGBA16:
    case GL_RGBA16F: return 8;
    case GL_RGBA4: return 2;
    default: return 4;
    }
}

Level detectLevel() {
    if (envSet("JEFECHECK_NO_PBO")) return Level::Off;
    const bool classicOk = glBindBuffer && glBufferData && glMapBuffer && glUnmapBuffer && glDeleteBuffers
                           && (GLAD_GL_VERSION_2_1 || GLAD_GL_ARB_pixel_buffer_object);
    const bool persistentOk = GLAD_GL_ARB_buffer_storage && glBufferStorage && glMapBufferRange
                              && glFenceSync && glClientWaitSync && glDeleteSync;
    if (persistentOk && !envSet("JEFECHECK_FORCE_CLASSIC_PBO")) return Level::Persistent;
    return classicOk ? Level::Classic : Level::Off;
}

// GL thread. Maps a classic-level buffer for the next writer; the
// glBufferData orphan gives the driver fresh storage, so a transfer still
// reading the old contents is never waited on or overwritten.
bool remapClassic(Staging* s) {
    glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)s->bytes, nullptr, GL_STREAM_DRAW);
    s->data = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    return s->data != nullptr;
}

// GL thread, g_mutex held.
Staging* createStaging(size_t bytes, Level level) {
    auto* s = new Staging;
    s->bytes = bytes;
    glGenBuffers(1, &s->buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s->buffer);
    bool ok;
    if (level == Level::Persistent) {
        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorage(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)bytes, nullptr, flags);
        s->data = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, (GLsizeiptr)bytes, flags);
        ok = s->data != nullptr;
    } else {
        ok = remapClassic(s);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (!ok) {
        glDeleteBuffers(1, &s->buffer);   // deleting also unmaps
        delete s;
        return nullptr;
    }
    return s;
}

// GL thread, g_mutex held. Only Free buffers are destroyed.
void destroyStaging(size_t index) {
    Staging* s = g_staging[index];
    glDeleteBuffers(1, &s->buffer);   // implicitly unmaps
    g_stagingBytes -= s->bytes;
    g_staging.erase(g_staging.begin() + index);
    delete s;
}

}  // namespace

Staging* acquire(size_t bytes, void** data) {
    const Level level = g_level.load();
    if (level == Level::Off || bytes == 0) return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Staging* s : g_staging) {
        if (s->state == Staging::State::Free && s->bytes == bytes && s->data) {
            s->state = Staging::State::Held;
            *data = s->data;
            return s;
        }
    }
    ++g_wanted[bytes];
    ++g_stagingMisses;
    return nullptr;
}

void abandon(Staging* s) {
    if (!s) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (s->state == Staging::State::Held) s->state = Staging::State::Free;
}

void service() {
    Level level = g_level.load();
    if (level == Level::Unknown) {
        level = detectLevel();
        g_level.store(level);
        static const char* names[] = {"unknown", "off", "classic", "persistent"};
        std::printf("gfcUpload: staging buffers %s\n", names[static_cast<int>(level)]);
    }
    if (level == Level::Off) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    // Retire buffers whose transfer the GPU has finished (persistent level;
    // classic buffers are re-mapped as soon as they are uploaded).
    for (Staging* s : g_staging) {
        if (s->state != Staging::State::InFlight || !s->fence) continue;
        const GLenum r = glClientWaitSync(s->fence, 0, 0);
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
            glDeleteSync(s->fence);
            s->fence = nullptr;
            s->state = Staging::State::Free;
        }
    }
    // Grow for the sizes the decode thread missed, making room by dropping
    // free buffers of other sizes (a new sequence replaces the old one).
    for (auto& [bytes, count] : g_wanted) {
        if (bytes > kStagingCapBytes) continue;
        for (int n = 0; n < count; ++n) {
            for (size_t i = 0; i < g_staging.size() && g_stagingBytes + bytes > kStagingCapBytes;) {
                if (g_staging[i]->state == Staging::State::Free && g_staging[i]->bytes != bytes)
                    destroyStaging(i);
                else
                    ++i;
            }
            if (g_stagingBytes + bytes > kStagingCapBytes) break;
            Staging* s = createStaging(bytes, level);
            if (!s) break;
            g_staging.push_back(s);
            g_stagingBytes += bytes;
        }
    }
    g_wanted.clear();
}

bool upload(Staging* s, unsigned target, int internalFormat, int width, int height,
            unsigned format, unsigned type, bool allocate) {
    if (!s) return false;
    const Level level = g_level.load();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (s->state != Staging::State::Held) return false;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s->buffer);
    bool ok = true;
    if (level == Level::Classic) ok = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) == GL_TRUE;
    if (ok) {
        // With a buffer bound, the pixel pointer is an offset into it.
        if (allocate)
            glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, nullptr);
        else
            glTexSubImage2D(target, 0, 0, 0, width, height, format, type, nullptr);
    }
    if (level == Level::Persistent) {
        s->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        s->state = Staging::State::InFlight;
    } else {
        // Re-map for the next frame now; a failed map leaves it unusable
        // (acquire skips buffers without data) until the pool drops it.
        remapClassic(s);
        s->state = Staging::State::Free;
    }
    // Everything else uploads from client memory: never leave this bound.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (ok) ++g_stagedUploads;
    return ok;
}

bool poolable(unsigned target, int internalFormat) {
    if (target != GL_TEXTURE_RECTANGLE) return false;
    switch (internalFormat) {
    case GL_RGBA:
    case GL_RGBA8:
    case GL_RGBA4:
    case GL_RGBA16:
    case GL_RGBA16F: return true;
    default: return false;   // compressed formats can't be refilled from raw pixels
    }
}

unsigned takeTexture(unsigned target, int internalFormat, int width, int height) {
    for (size_t i = 0; i < g_textures.size(); ++i) {
        const PooledTexture& t = g_textures[i];
        if (t.target == target && t.internalFormat == internalFormat && t.width == width && t.height == height) {
            const GLuint id = t.id;
            g_textureBytes -= t.bytes;
            g_textures.erase(g_textures.begin() + i);
            glBindTexture(target, id);
            ++g_texReused;
            return id;
        }
    }
    return 0;
}

void giveTexture(unsigned texture, unsigned target, int internalFormat, int width, int height) {
    if (!texture) return;
    static const bool pooling = !envSet("JEFECHECK_NO_TEXTURE_POOL");
    const size_t bytes = (size_t)width * height * bytesPerPixel(internalFormat);
    if (!pooling || !poolable(target, internalFormat) || width <= 0 || height <= 0 || bytes > kTextureCapBytes) {
        glDeleteTextures(1, &texture);
        return;
    }
    for (const PooledTexture& t : g_textures)
        if (t.id == texture) return;   // already returned by a copy of the same frame
    while (g_textureBytes + bytes > kTextureCapBytes && !g_textures.empty()) {
        glDeleteTextures(1, &g_textures.front().id);
        g_textureBytes -= g_textures.front().bytes;
        g_textures.erase(g_textures.begin());
    }
    g_textures.push_back({texture, target, internalFormat, width, height, bytes});
    g_textureBytes += bytes;
}

void noteTextureCreated() { ++g_texCreated; }

Stats stats() {
    static const char* names[] = {"unknown", "off", "classic", "persistent"};
    std::lock_guard<std::mutex> lock(g_mutex);
    return {names[static_cast<int>(g_level.load())], g_stagedUploads.load(), g_stagingMisses.load(),
            g_texReused.load(), g_texCreated.load(), g_stagingBytes, g_textureBytes};
}

}  // namespace gfcUpload
