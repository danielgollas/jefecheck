// IOSurface-backed textures on macOS. See gfcIOSurface.h.
//
// This file must not include glad: it uses Apple's OpenGL headers for
// CGLTexImageIOSurface2D, and the two cannot share a translation unit.

#include "gfcIOSurface.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include <atomic>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gfcSurface {

namespace {

struct Combo {
    GLint internalFormat;
    GLenum format;
    GLenum type;
};

struct LayoutDesc {
    OSType pixelFormat;
    int bytesPerElement;
    Combo combos[2];
};

// Per layout: the IOSurface pixel format and the GL format triples to try,
// in order. Apple's drivers accept different internal formats on different
// GPUs, so the first combo that binds is remembered in g_chosen.
const LayoutDesc kLayouts[3] = {
    // BGRA8
    {'BGRA', 4,
     {{GL_RGBA, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV},
      {GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV}}},
    // RGBA16
    {'l64r', 8,
     {{GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT},
      {GL_RGBA, GL_RGBA, GL_UNSIGNED_SHORT}}},
    // RGBAHalf
    {'RGhA', 8,
     {{GL_RGBA16F_ARB, GL_RGBA, GL_HALF_FLOAT_ARB},
      {GL_RGBA, GL_RGBA, GL_HALF_FLOAT_ARB}}},
};

// -1: not tried yet; 0/1: the combo that worked; -2: none works, so the
// caller should keep using glTexImage2D for this layout.
std::atomic<int> g_chosen[3] = {{-1}, {-1}, {-1}};
std::atomic<long> g_bound{0};

const LayoutDesc& desc(Layout layout) { return kLayouts[static_cast<int>(layout)]; }

bool tryBind(IOSurfaceRef s, int w, int h, const Combo& c) {
    CGLContextObj ctx = CGLGetCurrentContext();
    if (!ctx) return false;
    CGLError err = CGLTexImageIOSurface2D(ctx, GL_TEXTURE_RECTANGLE_ARB, c.internalFormat,
                                          w, h, c.format, c.type, s, 0);
    return err == kCGLNoError;
}

// Draws the rectangle texture @a tex (w x h) 1:1 into a float FBO and reads
// it back: what a shader sampling the texture sees. glGetTexImage is not
// enough on its own, since Apple's drivers may answer it from a different copy.
void drawAndRead(GLuint tex, int w, int h, float* out) {
    GLint oldFbo = 0, oldProgram = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &oldFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLuint target = 0, fbo = 0;
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, target, 0);
    glUseProgram(0);
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glClampColorARB(GL_CLAMP_VERTEX_COLOR_ARB, GL_FALSE);
    glClampColorARB(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_FALSE);
    glClampColorARB(GL_CLAMP_READ_COLOR_ARB, GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, tex);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f((float)w, 0); glVertex2f((float)w, 0);
    glTexCoord2f((float)w, (float)h); glVertex2f((float)w, (float)h);
    glTexCoord2f(0, (float)h); glVertex2f(0, (float)h);
    glEnd();
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)oldFbo);
    glUseProgram((GLuint)oldProgram);
    glDeleteFramebuffersEXT(1, &fbo);
    glDeleteTextures(1, &target);
}

// Released surfaces wait here for a frame of the same size and layout, so
// reloading a sequence or advancing the playlist reuses memory that is
// already mapped instead of allocating (and page-faulting) fresh surfaces.
struct Pooled {
    IOSurfaceRef surface;
    int width, height;
    OSType pixelFormat;
    size_t bytes;
};
constexpr size_t kPoolCapBytes = size_t(512) << 20;
std::mutex g_poolMutex;
std::vector<Pooled> g_pool;
size_t g_poolBytes = 0;
std::atomic<long> g_created{0}, g_reused{0};

bool poolEnabled() {
    static const bool on = [] {
        const char* v = std::getenv("JEFECHECK_NO_IOSURFACE_POOL");
        return !(v && *v && std::strcmp(v, "0") != 0);
    }();
    return on;
}

IOSurfaceRef takeFromPool(int w, int h, OSType pf) {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    for (size_t i = 0; i < g_pool.size(); ++i) {
        const Pooled& p = g_pool[i];
        // A deleted texture's draws may still be in flight: skip surfaces
        // the GPU is still using rather than overwrite a frame mid-draw.
        if (p.width == w && p.height == h && p.pixelFormat == pf && !IOSurfaceIsInUse(p.surface)) {
            IOSurfaceRef s = p.surface;
            g_poolBytes -= p.bytes;
            g_pool.erase(g_pool.begin() + i);
            return s;
        }
    }
    return nullptr;
}

}  // namespace

bool available() {
    static const bool on = [] {
        const char* v = std::getenv("JEFECHECK_NO_IOSURFACE");
        return !(v && *v && std::strcmp(v, "0") != 0);
    }();
    return on;
}

void* create(int width, int height, Layout layout) {
    if (!available() || width <= 0 || height <= 0) return nullptr;
    if (g_chosen[static_cast<int>(layout)].load() == -2) return nullptr;
    const LayoutDesc& d = desc(layout);
    if (IOSurfaceRef pooled = takeFromPool(width, height, d.pixelFormat)) {
        ++g_reused;
        return pooled;
    }

    int w = width, h = height, bpe = d.bytesPerElement;
    OSType pf = d.pixelFormat;
    CFNumberRef nw = CFNumberCreate(nullptr, kCFNumberIntType, &w);
    CFNumberRef nh = CFNumberCreate(nullptr, kCFNumberIntType, &h);
    CFNumberRef nb = CFNumberCreate(nullptr, kCFNumberIntType, &bpe);
    CFNumberRef np = CFNumberCreate(nullptr, kCFNumberSInt32Type, &pf);
    const void* keys[] = {kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement,
                          kIOSurfacePixelFormat};
    const void* vals[] = {nw, nh, nb, np};
    CFDictionaryRef props = CFDictionaryCreate(nullptr, keys, vals, 4,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef s = IOSurfaceCreate(props);
    CFRelease(props);
    CFRelease(nw);
    CFRelease(nh);
    CFRelease(nb);
    CFRelease(np);
    if (s) ++g_created;
    return s;
}

void* lockForWriting(void* surface, size_t* bytesPerRow) {
    if (!surface) return nullptr;
    IOSurfaceRef s = static_cast<IOSurfaceRef>(surface);
    if (IOSurfaceLock(s, 0, nullptr) != kIOReturnSuccess) return nullptr;
    if (bytesPerRow) *bytesPerRow = IOSurfaceGetBytesPerRow(s);
    return IOSurfaceGetBaseAddress(s);
}

void unlock(void* surface) {
    if (surface) IOSurfaceUnlock(static_cast<IOSurfaceRef>(surface), 0, nullptr);
}

void retain(void* surface) {
    if (surface) CFRetain(static_cast<CFTypeRef>(surface));
}

void release(void* surface) {
    if (!surface) return;
    IOSurfaceRef s = static_cast<IOSurfaceRef>(surface);
    {
        std::lock_guard<std::mutex> lock(g_poolMutex);
        // Ours is the last reference (nobody else can retain it now), so it
        // can go back to the pool; oldest surfaces make room when it is full.
        if (poolEnabled() && CFGetRetainCount(s) == 1) {
            const size_t bytes = IOSurfaceGetAllocSize(s);
            if (bytes <= kPoolCapBytes) {
                while (g_poolBytes + bytes > kPoolCapBytes && !g_pool.empty()) {
                    g_poolBytes -= g_pool.front().bytes;
                    CFRelease(g_pool.front().surface);
                    g_pool.erase(g_pool.begin());
                }
                g_pool.push_back({s, (int)IOSurfaceGetWidth(s), (int)IOSurfaceGetHeight(s),
                                  IOSurfaceGetPixelFormat(s), bytes});
                g_poolBytes += bytes;
                return;
            }
        }
    }
    CFRelease(s);
}

PoolStats poolStats() {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    return {g_created.load(), g_reused.load(), g_pool.size(), g_poolBytes};
}

void drainPool() {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    for (const Pooled& p : g_pool) CFRelease(p.surface);
    g_pool.clear();
    g_poolBytes = 0;
}

bool bindToTexture(void* surface, int width, int height, Layout layout) {
    if (!surface) return false;
    IOSurfaceRef s = static_cast<IOSurfaceRef>(surface);
    const int li = static_cast<int>(layout);
    const LayoutDesc& d = desc(layout);
    int chosen = g_chosen[li].load();
    if (chosen == -2) return false;
    if (chosen >= 0) {
        if (!tryBind(s, width, height, d.combos[chosen])) return false;
        ++g_bound;
        return true;
    }
    for (int i = 0; i < 2; ++i) {
        if (tryBind(s, width, height, d.combos[i])) {
            g_chosen[li].store(i);
            ++g_bound;
            return true;
        }
    }
    g_chosen[li].store(-2);
    return false;
}

long boundCount() { return g_bound.load(); }

bool readBack(unsigned texture, int width, int height, float* rgba) {
    if (!CGLGetCurrentContext() || width <= 0 || height <= 0) return false;
    drawAndRead(texture, width, height, rgba);
    return true;
}

int selfTest() {
    static const char* kNames[3] = {"bgra8", "rgba16", "half"};
    // One pixel per layout, written into every texel of a small surface,
    // and the RGBA floats the texture must read back.
    const unsigned char bgra8[4] = {51, 102, 204, 255};          // B G R A
    const unsigned short rgba16[4] = {0xC000, 0x4000, 0x1000, 0xFFFF};
    const unsigned short half[4] = {0x4400, 0x3800, 0x3400, 0x3C00};  // 4, .5, .25, 1
    const float expect[3][4] = {
        {204 / 255.f, 102 / 255.f, 51 / 255.f, 1.f},
        {0xC000 / 65535.f, 0x4000 / 65535.f, 0x1000 / 65535.f, 1.f},
        {4.f, .5f, .25f, 1.f},
    };
    const void* pixel[3] = {bgra8, rgba16, half};
    const int W = 7, H = 3;   // odd width: exercises row padding

    if (!available() || !CGLGetCurrentContext()) {
        std::printf("IOSURFACE-TEST unavailable\n");
        return 0;
    }
    int working = 0;
    for (int li = 0; li < 3; ++li) {
        const Layout layout = static_cast<Layout>(li);
        const int bpe = desc(layout).bytesPerElement;
        void* surf = create(W, H, layout);
        if (!surf) {
            std::printf("IOSURFACE-TEST %s create=fail\n", kNames[li]);
            continue;
        }
        size_t pitch = 0;
        unsigned char* base = static_cast<unsigned char*>(lockForWriting(surf, &pitch));
        for (int y = 0; base && y < H; ++y)
            for (int x = 0; x < W; ++x)
                std::memcpy(base + pitch * y + (size_t)x * bpe, pixel[li], bpe);
        if (base) unlock(surf);

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, tex);
        const CFIndex before = CFGetRetainCount(static_cast<CFTypeRef>(surf));
        const bool ok = bindToTexture(surf, W, H, layout);
        const CFIndex afterBind = CFGetRetainCount(static_cast<CFTypeRef>(surf));
        float got[W * H * 4] = {}, drawn[W * H * 4] = {};
        if (ok) {
            glGetTexImage(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, GL_FLOAT, got);
            drawAndRead(tex, W, H, drawn);
        }
        float maxErr = 0.f, readErr = 0.f;
        for (int i = 0; i < W * H; ++i)
            for (int c = 0; c < 4; ++c) {
                maxErr = std::fmax(maxErr, std::fabs(drawn[i * 4 + c] - expect[li][c]));
                readErr = std::fmax(readErr, std::fabs(got[i * 4 + c] - expect[li][c]));
            }
        std::printf("IOSURFACE-TEST %s drawn=(%.3f %.3f %.3f %.3f) getTexImageErr=%.4f\n", kNames[li],
                    drawn[0], drawn[1], drawn[2], drawn[3], readErr);
        // Hold an extra reference so the count stays readable after the
        // texture lets go of its own.
        CFRetain(static_cast<CFTypeRef>(surf));
        release(surf);   // the caller's reference, as the loader does
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
        glDeleteTextures(1, &tex);
        glFinish();
        const CFIndex afterDelete = CFGetRetainCount(static_cast<CFTypeRef>(surf));
        CFRelease(static_cast<CFTypeRef>(surf));

        const bool pixelsOk = ok && maxErr < 0.003f;
        const bool retained = afterBind > before;
        std::printf("IOSURFACE-TEST %s bind=%s combo=%d pitch=%zu maxErr=%.4f retain(before/bound/deleted)=%ld/%ld/%ld%s %s\n",
                    kNames[li], ok ? "ok" : "refused", g_chosen[li].load(), pitch, maxErr,
                    (long)before, (long)afterBind, (long)afterDelete,
                    retained ? "" : " (texture holds no reference)",
                    pixelsOk ? "PASS" : "FAIL");
        if (pixelsOk) ++working;
    }
    return working;
}

}  // namespace gfcSurface
