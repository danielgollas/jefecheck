// Snapshot of the GL state that drives the "Host System Specs" dialog.
//
// Populated once during the first onGLInit() — Qt header-free so the
// .cpp can include glad/glad.h without colliding with QOpenGL headers
// (the same constraint that motivates glad_loader.cpp). Consumers read
// from glSystemInfo(); writers should never poke this struct after
// captureGLSystemInfo() has run.
#ifndef JEFECHECK_QT_GLSYSTEMINFO_H
#define JEFECHECK_QT_GLSYSTEMINFO_H

#include <string>

namespace jefe::qt {

struct GLSystemInfo {
    std::string version;
    std::string vendor;
    std::string renderer;
    int maxTextureSize  = 0;
    int maxViewportX    = 0;
    int maxViewportY    = 0;
    // Capability flags — match the FLTK MinSpecs window's check rows.
    bool textureRectangle = false;
    bool shaderObjects    = false;
    bool glsl             = false;
    bool fbo              = false;
    bool textureFloat     = false;
    bool textureHalf      = false;
    bool pbo              = false;
    bool s3tc             = false;
    bool captured         = false;
};

const GLSystemInfo& glSystemInfo();

// Idempotent: subsequent calls are no-ops once `captured` is true. Must
// run with a current GL context (RenderBridge_Qt::onGLInit is the
// designated caller).
void captureGLSystemInfo();

}  // namespace jefe::qt

#endif
