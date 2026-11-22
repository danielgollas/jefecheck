// SPDX-License-Identifier: GPL-2.0-or-later
// Offscreen GL context for the headless --asset-test harness. Own TU: Qt's OpenGL
// headers cannot share a TU with glad.h (developer_notes §1).

#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>

// Defined in glad_loader.cpp (the sole glad TU). Resolves GLAD's function
// pointers against the current context.
extern bool jefecheck_loadGladGL();

namespace jefe::qt {

// Brings up an offscreen GL context matching the viewport's format and resolves GLAD.
// Intentionally leaked: the context must stay current for the whole test process.
bool setupOffscreenTestGL() {
    auto* ctx = new QOpenGLContext();
    ctx->setFormat(QSurfaceFormat::defaultFormat());
    if (!ctx->create()) return false;

    auto* surf = new QOffscreenSurface();
    surf->setFormat(ctx->format());
    surf->create();
    if (!surf->isValid()) return false;

    if (!ctx->makeCurrent(surf)) return false;
    return jefecheck_loadGladGL();
}

}  // namespace jefe::qt
