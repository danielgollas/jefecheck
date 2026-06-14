// Qt implementation of IGLViewport. Pure listener relay — paintGL,
// initializeGL, and resizeGL are forwarded to the registered
// IGLViewportListener (typically RenderBridge_Qt). The widget itself
// holds no rendering state.
#include "GlViewport_qt.h"
#include "SequenceLoadBridge_qt.h"

// FRAMING*_ID constants for layout shortcuts. UIConstants.h has no
// FLTK dependencies, so it's safe to pull into the Qt TU directly.
#include "../UIConstants.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPinchGesture>
#include <QUrl>
#include <QWheelEvent>

extern bool jefecheck_loadGladGL();

GlViewport_Qt::GlViewport_Qt(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setObjectName("viewport");
    setAccessibleName("Viewport");
    setAccessibleDescription("OpenGL plate viewport");
    // We do a full repaint each frame (clear + redraw every plate), so
    // we don't need Qt to preserve the previous frame's FBO contents.
    // NoPartialUpdate skips that copy in the FBO→window composite step;
    // measurable per-frame win on macOS where QOpenGLWidget already
    // pays an FBO blit that FLTK's native NSOpenGLView avoids.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    // Accept native pinch gestures — trackpad two-finger pinch maps to
    // zoom on the plate under the gesture center (Alt = gang). Without
    // grabGesture, QPinchGesture events never reach this widget.
    grabGesture(Qt::PinchGesture);
}

GlViewport_Qt::~GlViewport_Qt() = default;

void GlViewport_Qt::requestRedraw() {
    update();
}

void GlViewport_Qt::makeCurrent() {
    QOpenGLWidget::makeCurrent();
}

void GlViewport_Qt::swapBuffers() {
    // QOpenGLWidget swaps internally on paintGL completion.
}

int GlViewport_Qt::width() const {
    return QOpenGLWidget::width();
}

int GlViewport_Qt::height() const {
    return QOpenGLWidget::height();
}

float GlViewport_Qt::pixelsPerUnit() const {
    return static_cast<float>(devicePixelRatioF());
}

void GlViewport_Qt::setListener(jefe::ui::IGLViewportListener* listener) {
    listener_ = listener;
}

void GlViewport_Qt::setCursorVisible(bool visible) {
    setCursor(visible ? Qt::ArrowCursor : Qt::BlankCursor);
}

void GlViewport_Qt::setLoadWindowOpen(bool open) {
    if (loadWindowOpen_ == open) return;
    loadWindowOpen_ = open;

    // Drive every plate's showPreview deterministically from the flag.
    // Routed through the bridge so this TU doesn't have to pull
    // gfcplatemanager.h (glad transitivity vs QOpenGLWidget).
    jefe::qt::setAllPlatesShowPreview(open);
    update();
}

void GlViewport_Qt::initializeGL() {
    if (!gladLoaded_) {
        gladLoaded_ = jefecheck_loadGladGL();
    }
    if (listener_) listener_->onGLInit();
}

void GlViewport_Qt::resizeGL(int w, int h) {
    // Qt passes the size in logical pixels here. The QOpenGLWidget's
    // backing framebuffer is allocated at logical * devicePixelRatio
    // (2× on macOS Retina), so glViewport-bound rendering needs the
    // framebuffer dimensions — using the raw w/h would leave only the
    // bottom-left quarter of the FBO rendered.
    const float dpr = devicePixelRatioF();
    if (listener_) listener_->onResize(int(w * dpr), int(h * dpr));
}

void GlViewport_Qt::paintGL() {
    if (listener_) {
        listener_->onDraw();
    } else {
        // No listener attached — clear the framebuffer so we don't show
        // garbage from an uninitialized backing store.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void GlViewport_Qt::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
        // Hit-test which plate the click landed on so drag pan and
        // (in single mode) keyboard shortcuts target it. The current
        // viewport size matches what plateManager.draw saw last, so
        // the partitioning lines up.
        dragPlate_ = jefe::qt::plateAtViewportPos(
            int(lastMouseX_), int(lastMouseY_), width(), height());
        if (dragPlate_ >= 0) {
            // Sync the active-plate concept (used by plate-card highlight
            // and a couple of remaining keyboard paths) with the user's
            // most recent click.
            jefe::qt::setActivePlate(dragPlate_);
            emit plateStateChanged();
        }
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
}

void GlViewport_Qt::mouseReleaseEvent(QMouseEvent*) {
    const bool wasDragging = dragPlate_ >= 0;
    dragPlate_ = -1;
    if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
    // Final widget sync once the drag ends — this catches any motion
    // that arrived after the most recent throttled emit so the
    // spinboxes hold the exact final values.
    if (wasDragging) {
        emit plateStateChanged();
        dragEmittedAny_ = false;
    }
}

void GlViewport_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton && dragPlate_ >= 0) {
        const float dpr = devicePixelRatioF();
        const bool gang = (e->modifiers() & Qt::AltModifier) != 0;

        // ---- Color-correction key+drag dispatch (W/E/Q/D/S). ----
        //
        // FLTK GlViewport's left-drag path checks each letter key in
        // order and applies `adjustmentValue = (eventX - prevX) * 0.01`
        // additively (isDelta=1) to the matching plate field. Per-plate
        // calls target the active plate; Alt-modified calls hit every
        // plate via the *All variants.
        //
        // We branch BEFORE the pan path and early-return on a handled
        // color drag — pan and color-correct shouldn't compose in one
        // motion (matches FLTK's case-by-case if/else structure).
        const float dxLogical = float(e->position().x()) - lastMouseX_;
        const float adjust    = dxLogical * 0.01f;
        const int targetPlate = jefe::qt::getActivePlate();

        bool handledColor = false;
        auto applyColor = [&](auto perPlateFn, auto gangFn) {
            if (gang) gangFn(adjust);
            else      perPlateFn(targetPlate, adjust);
            handledColor = true;
        };

        if (heldDragModifierKeys_.contains(Qt::Key_W)) {
            applyColor(jefe::qt::adjustPlateGamma,
                       jefe::qt::adjustAllPlatesGamma);
        } else if (heldDragModifierKeys_.contains(Qt::Key_E)) {
            applyColor(jefe::qt::adjustPlateExposure,
                       jefe::qt::adjustAllPlatesExposure);
        } else if (heldDragModifierKeys_.contains(Qt::Key_Q)) {
            applyColor(jefe::qt::adjustPlateBrightness,
                       jefe::qt::adjustAllPlatesBrightness);
        } else if (heldDragModifierKeys_.contains(Qt::Key_D)) {
            applyColor(jefe::qt::adjustPlateContrast,
                       jefe::qt::adjustAllPlatesContrast);
        } else if (heldDragModifierKeys_.contains(Qt::Key_S)) {
            applyColor(jefe::qt::adjustPlateSaturation,
                       jefe::qt::adjustAllPlatesSaturation);
        }

        if (handledColor) {
            update();
            // Throttle + queued emit of plateColorChanged — same 60Hz
            // cap as the transform path so we don't spam the event
            // loop at the device's 100Hz+ mouse poll rate.
            constexpr qint64 kEmitIntervalMs = 16;
            if (!dragEmitTimer_.isValid()
                || dragEmitTimer_.elapsed() >= kEmitIntervalMs) {
                if (gang) {
                    for (int p = 0; p < 4; ++p) emit plateColorChanged(p);
                } else {
                    emit plateColorChanged(targetPlate);
                }
                dragEmitTimer_.restart();
                dragEmittedAny_ = true;
            }
            lastMouseX_ = e->position().x();
            lastMouseY_ = e->position().y();
            if (listener_) listener_->onEvent(jefe::ui::EventType::Drag);
            return;
        }

        // ---- Pan path (no color key held). ----
        //
        // FLTK's GlViewport pans by (prevX - eventX, prevY - eventY) so
        // dragging right shifts the plate left. Match that sign so the
        // mouse behaves the same in both backends. Multiply by dpr —
        // gfcPlate's pan is in world-space units which are now in
        // framebuffer pixels, but Qt mouse positions are logical, so
        // a 1-px drag would otherwise move the image only 1/dpr px on
        // Retina.
        const float dx = (lastMouseX_ - float(e->position().x())) * dpr;
        const float dy = (lastMouseY_ - float(e->position().y())) * dpr;
        // Alt-drag = gang-transform: pan every plate by the same delta.
        // Matches the FLTK convention in GlViewport.cpp where the
        // letterless Alt+drag branch routes to panAllPlates. (FLTK has
        // a separate Alt+drag = zoomPlate branch tied to a different
        // condition higher up the if-chain; preserving the pan-all
        // behavior the Qt UI already exposes — Daniel signed off on
        // that semantics for the Qt port.)
        if (gang) {
            jefe::qt::panAllPlates(dx, dy);
        } else {
            jefe::qt::panPlate(dragPlate_, dx, dy);
        }
        update();
        // Lightweight per-drag signal — only refreshes the four transform
        // spinboxes on the dragged plate's card. Skips the heavy
        // refreshAllCards (4 plates × ~15 fields) + FXParamPanel::refresh
        // cascade that plateStateChanged triggers, AND the QSignalBlocker
        // scope churn for every widget on every card.
        //
        // Throttled to ~60Hz so mouseMoveEvent stays fast even at the
        // device's 100Hz+ poll rate. mouseReleaseEvent emits the full
        // plateStateChanged so the inactive-plate cards and FX panel
        // get their one-shot post-drag sync.
        constexpr qint64 kEmitIntervalMs = 16;
        if (!dragEmitTimer_.isValid()
            || dragEmitTimer_.elapsed() >= kEmitIntervalMs) {
            if (gang) {
                // Every card got panned — refresh each. Queued + cached
                // so the cost is bounded; cards whose values didn't
                // actually change (none in practice during gang-pan)
                // would skip their setValue calls anyway.
                for (int p = 0; p < 4; ++p) emit plateTransformChanged(p);
            } else {
                emit plateTransformChanged(dragPlate_);
            }
            dragEmitTimer_.restart();
            dragEmittedAny_ = true;
        }
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
    }
    auto type = (e->buttons() == Qt::NoButton)
        ? jefe::ui::EventType::Move
        : jefe::ui::EventType::Drag;
    if (listener_) listener_->onEvent(type);
}

void GlViewport_Qt::wheelEvent(QWheelEvent* e) {
    // macOS trackpads report pixelDelta (smooth scroll); discrete-notch
    // mice report angleDelta (eighths of a degree, 120 per notch).
    // Convert both to FLTK-style "wheelDeltaY" units (~±1 per notch)
    // before scaling by zoomSpeed.
    float deltaY = 0.0f;
    if (!e->pixelDelta().isNull()) {
        deltaY = static_cast<float>(e->pixelDelta().y()) / 120.0f;
    } else if (!e->angleDelta().isNull()) {
        deltaY = static_cast<float>(e->angleDelta().y()) / 120.0f;
    }
    if (deltaY != 0.0f) {
        // Wheel zooms whichever plate the cursor is over, regardless of
        // active state. Click sets active separately.
        const int plate = jefe::qt::plateAtViewportPos(
            int(e->position().x()), int(e->position().y()), width(), height());
        // 0.1 matches FLTK's default zoomSpeed for the un-shifted wheel.
        jefe::qt::zoomPlate(plate, deltaY * 0.1f);
        update();
        // Targeted refresh — wheel only mutates the zoom field on one
        // plate, so the lightweight transform-only signal is enough.
        // Plays nice with rapid wheel events: no FX-panel refresh and
        // no other-card walk per scroll tick.
        if (plate >= 0) emit plateTransformChanged(plate);
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Wheel);
}

void GlViewport_Qt::keyPressEvent(QKeyEvent* e) {
    // Track color-correction modifier keys (W/E/Q/D/S) — combined with
    // a left-drag in mouseMoveEvent they trigger the matching field
    // adjustment. Don't gate on autoRepeat: holding the key while
    // dragging is the normal use, and autoRepeat false on first press
    // is enough since we only need set membership.
    switch (e->key()) {
        case Qt::Key_W:
        case Qt::Key_E:
        case Qt::Key_Q:
        case Qt::Key_D:
        case Qt::Key_S:
            heldDragModifierKeys_.insert(e->key());
            break;
        default:
            break;
    }

    // Plate-control shortcuts. Any unhandled key falls through to the
    // listener and Qt's default propagation so menu mnemonics, dialog
    // accelerators, etc. still work.
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    const bool ctrl  = e->modifiers().testFlag(Qt::ControlModifier);
    const bool alt   = e->modifiers().testFlag(Qt::AltModifier);

    auto handled = [&]() {
        update();
        emit plateStateChanged();
        if (listener_) listener_->onEvent(jefe::ui::EventType::KeyDown);
    };

    switch (e->key()) {
        // Layout cycling: Ctrl+1..4 → single / horiz-split / vert-split / quad.
        case Qt::Key_1:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGSINGLE_ID);     handled(); return; }
            break;
        case Qt::Key_2:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGDOUBLE_ID);     handled(); return; }
            break;
        case Qt::Key_3:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGDOUBLEVERT_ID); handled(); return; }
            break;
        case Qt::Key_4:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGQUAD_ID);       handled(); return; }
            break;

        // F / Shift+F (fit), H / Shift+H (flop), V / Shift+V (flip),
        // T / Alt+T (text mode) all live as ApplicationShortcut bindings
        // in MainWindow_Qt — they need to fire regardless of whether the
        // viewport, a dock widget, a spinbox, or the menu bar has focus.
        // Keeping a duplicate handler here would either steal focus-
        // dependent keystrokes from the focused input widget OR fire
        // both the QShortcut and the viewport handler.

        // Track cycling on active plate (matches FLTK's Up/Down handler).
        case Qt::Key_Up:
            if (!ctrl && !alt) {
                jefe::qt::cycleTrackOnActivePlate(-1);
                handled();
                return;
            }
            break;
        case Qt::Key_Down:
            if (!ctrl && !alt) {
                jefe::qt::cycleTrackOnActivePlate(+1);
                handled();
                return;
            }
            break;

        // Playback. Space pauses; arrows step a frame. Frame stepping
        // is a no-op until the Qt build runs the playback loop, but
        // wiring it here keeps the surface symmetric with FLTK.
        case Qt::Key_Space:
            if (!ctrl && !alt) {
                jefe::qt::pausePlayback();
                handled();
                return;
            }
            break;
        case Qt::Key_Left:
            if (!ctrl && !alt) {
                jefe::qt::stepFrame(-1);
                handled();
                return;
            }
            break;
        case Qt::Key_Right:
            if (!ctrl && !alt) {
                jefe::qt::stepFrame(+1);
                handled();
                return;
            }
            break;

        default:
            break;
    }

    // Unhandled — pass through to listener / parent.
    if (listener_) listener_->onEvent(jefe::ui::EventType::KeyDown);
    QOpenGLWidget::keyPressEvent(e);
}

void GlViewport_Qt::keyReleaseEvent(QKeyEvent* e) {
    // Pair with keyPressEvent's color-correction modifier tracking. Qt
    // does fire autoRepeat release/press pairs during a held key — we
    // accept the brief drop in set membership; it just means the next
    // mouseMoveEvent in that millisecond window won't apply an
    // adjustment, which is invisible in practice at 60Hz emit gating.
    switch (e->key()) {
        case Qt::Key_W:
        case Qt::Key_E:
        case Qt::Key_Q:
        case Qt::Key_D:
        case Qt::Key_S:
            heldDragModifierKeys_.remove(e->key());
            break;
        default:
            break;
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::KeyUp);
}

void GlViewport_Qt::enterEvent(QEnterEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::Enter);
}

void GlViewport_Qt::leaveEvent(QEvent*) {
    // Cursor left the viewport — drop any held drag-modifier keys so a
    // subsequent re-enter without a fresh key press doesn't apply
    // ghost color-correction adjustments. Qt stops delivering key
    // events to a widget that loses focus, so without this clear the
    // set could stay populated indefinitely.
    heldDragModifierKeys_.clear();
    if (listener_) listener_->onEvent(jefe::ui::EventType::Leave);
}

void GlViewport_Qt::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& u : e->mimeData()->urls()) {
            if (u.isLocalFile()) {
                e->acceptProposedAction();
                return;
            }
        }
    }
    e->ignore();
}

void GlViewport_Qt::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}

void GlViewport_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }

    QString path;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) { path = u.toLocalFile(); break; }
    }
    if (path.isEmpty()) {
        e->ignore();
        return;
    }

    // Modal-open branch: forward the path to the active plate's strip
    // and skip the fast load. Scale modifiers don't apply — the load
    // window owns the load configuration.
    if (loadWindowOpen_) {
        const int plateIdx = 0;  // hardcoded today; future PR uses plate-under-cursor
        emit fileDroppedWhileLoadWindowOpen(plateIdx, path);
        e->acceptProposedAction();
        return;
    }

    // Modal-closed branch: existing scale-modifier fast path.
    // Scale modifier mapping mirrors the spec:
    //   plain     -> 1.0
    //   Shift     -> 0.5
    //   Shift+Cmd -> 0.25
    // Any other modifier combo (Cmd-only, Alt-only, etc.) keeps the
    // default 1.0 — Cmd-only is reserved for future "load into a
    // specific plate" gestures, so no surprise behavior for users
    // who hit it accidentally.
    const auto mods = e->keyboardModifiers();
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    // Qt::ControlModifier == Cmd on macOS, Ctrl on Linux/Windows.
    // The "Shift+Ctrl = 25%" gesture works fine on either since neither
    // platform binds it to a drag-drop convention.
    const bool cmd = mods.testFlag(Qt::ControlModifier);
    float scale = 1.0f;
    if (shift && cmd) {
        scale = 0.25f;
    } else if (shift) {
        scale = 0.5f;
    }

    emit fileDroppedWithScale(path, scale);
    emit fileDropped(path);  // legacy, see header comment
    e->acceptProposedAction();
}

bool GlViewport_Qt::event(QEvent* e) {
    if (e->type() == QEvent::Gesture) {
        auto* ge = static_cast<QGestureEvent*>(e);
        if (auto* gesture = ge->gesture(Qt::PinchGesture)) {
            handlePinchGesture(static_cast<QPinchGesture*>(gesture));
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

void GlViewport_Qt::handlePinchGesture(QPinchGesture* g) {
    // On gesture start, hit-test the plate under the gesture center.
    // centerPoint() is in global screen coords on QPinchGesture.
    if (g->state() == Qt::GestureStarted) {
        const QPoint local = mapFromGlobal(g->centerPoint().toPoint());
        pinchPlate_ = jefe::qt::plateAtViewportPos(
            local.x(), local.y(), width(), height());
    }

    if (g->changeFlags() & QPinchGesture::ScaleFactorChanged && pinchPlate_ >= 0) {
        // scaleFactor() is the incremental change since the last event
        // (1.0 = no change, > 1 zooms in). Convert to additive delta —
        // gfcPlateManager::zoomPlate / zoomAllPlates take a signed delta.
        const float delta = static_cast<float>(g->scaleFactor() - 1.0);
        if (delta != 0.0f) {
            const bool gang =
                QApplication::keyboardModifiers() & Qt::AltModifier;
            if (gang) {
                jefe::qt::zoomAllPlates(delta);
            } else {
                jefe::qt::zoomPlate(pinchPlate_, delta);
            }
            update();
            // Targeted refresh of just the zoom spinbox(es). Same queued,
            // cache-gated path used by wheel zoom and drag pan — pinch
            // rate is trackpad-driven (~60Hz) so the existing 16ms throttle
            // is unnecessary here.
            if (gang) {
                for (int p = 0; p < 4; ++p) emit plateTransformChanged(p);
            } else {
                emit plateTransformChanged(pinchPlate_);
            }
        }
    }

    if (g->state() == Qt::GestureFinished ||
        g->state() == Qt::GestureCanceled) {
        // Full sync of every card + FX panel at the end of the gesture
        // (matches the post-drag plateStateChanged in mouseReleaseEvent).
        emit plateStateChanged();
        pinchPlate_ = -1;
    }
}
