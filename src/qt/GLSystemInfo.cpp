// Captures GL state for the "Host System Specs" dialog. Pure glad —
// no Qt headers in this TU (the macOS OpenGL framework headers
// QOpenGLWidget pulls in conflict with glad's symbol redefinitions).
#include "GLSystemInfo.h"

#include <glad/glad.h>

#include <cstring>

namespace jefe::qt {

namespace {

GLSystemInfo g_info;

const char* glsString(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "";
}

bool hasExtension(const char* name) {
    // GL_EXTENSIONS is a single space-separated string in the
    // compatibility profile glad targets here. Substring match isn't
    // safe ("GL_EXT_foo" matches "GL_EXT_foobar"), so walk tokens.
    const char* exts = glsString(GL_EXTENSIONS);
    if (!exts || !*exts) return false;
    const std::size_t nameLen = std::strlen(name);
    const char* p = exts;
    while (*p) {
        const char* sp = std::strchr(p, ' ');
        const std::size_t tokLen = sp ? std::size_t(sp - p) : std::strlen(p);
        if (tokLen == nameLen && std::strncmp(p, name, nameLen) == 0) {
            return true;
        }
        if (!sp) break;
        p = sp + 1;
    }
    return false;
}

}  // namespace

const GLSystemInfo& glSystemInfo() {
    return g_info;
}

void captureGLSystemInfo() {
    if (g_info.captured) return;

    g_info.version  = glsString(GL_VERSION);
    g_info.vendor   = glsString(GL_VENDOR);
    g_info.renderer = glsString(GL_RENDERER);

    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    g_info.maxTextureSize = int(v);

    GLint dims[2] = {0, 0};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dims);
    g_info.maxViewportX = int(dims[0]);
    g_info.maxViewportY = int(dims[1]);

    g_info.shaderObjects    = hasExtension("GL_ARB_shader_objects");
    g_info.glsl             = hasExtension("GL_ARB_fragment_shader") &&
                              hasExtension("GL_ARB_vertex_shader");
    g_info.fbo              = hasExtension("GL_EXT_framebuffer_object") ||
                              hasExtension("GL_ARB_framebuffer_object");
    g_info.textureFloat     = hasExtension("GL_ARB_texture_float");
    g_info.textureHalf      = hasExtension("GL_ARB_half_float_pixel");
    g_info.textureRectangle = hasExtension("GL_ARB_texture_rectangle") ||
                              hasExtension("GL_NV_texture_rectangle");
    g_info.pbo              = hasExtension("GL_ARB_pixel_buffer_object");
    g_info.s3tc             = hasExtension("GL_EXT_texture_compression_s3tc") &&
                              hasExtension("GL_ARB_texture_compression");

    g_info.captured = true;
}

}  // namespace jefe::qt
