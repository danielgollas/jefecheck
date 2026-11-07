// Qt implementation of IGLViewport: a pure listener relay. paintGL,
// initializeGL and resizeGL are forwarded to the registered IGLViewportListener.
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
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
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
    // Full repaint each frame, so skip the FBO->window copy of the previous frame.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

    // Accept native pinch gestures (trackpad pinch zooms the plate under the
    // gesture center, Alt = gang); QPinchGesture events need grabGesture.
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

    // Drive every plate's showPreview from the flag, via the bridge so this TU
    // doesn't pull in gfcplatemanager.h (glad vs QOpenGLWidget).
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
    // Qt passes logical pixels; the backing framebuffer is logical * devicePixelRatio
    // (2x on Retina), so glViewport-bound rendering needs framebuffer dimensions.
    const float dpr = devicePixelRatioF();
    if (listener_) listener_->onResize(int(w * dpr), int(h * dpr));
}

void GlViewport_Qt::paintGL() {
    if (listener_) {
        // QOpenGLWidget renders into its own FBO, not 0, but gfcPlate rebinds 0 to "return to screen" (black viewport);
        // publish Qt's real default FBO every paint (it can change on resize).
        jefe::qt::setScreenFBO(defaultFramebufferObject());
        listener_->onDraw();
        // Chat + remote-pointer overlay (ported from the FLTK GlViewport).
        // Drawn last so it composites over the plates.
        const float dpr = devicePixelRatioF();
        jefe::qt::drawNetworkOverlay(int(width() * dpr), int(height() * dpr));
    } else {
        // No listener attached — clear the framebuffer so we don't show
        // garbage from an uninitialized backing store.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void GlViewport_Qt::mousePressEvent(QMouseEvent* e) {
    // Reclaim keyboard focus on any click so viewport shortcuts work after a text field had it
    // (the override doesn't get Qt's implicit click-focus).
    setFocus(Qt::MouseFocusReason);
    if (e->button() == Qt::LeftButton) {
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();

        // Give in-viewport pick overlays (the histogram sub-window) first claim on the click;
        // doPicking needs the GL context current.
        const float dpr = devicePixelRatioF();
        const int xFb = int(lastMouseX_ * dpr);
        const int yFb = int((float(height()) - lastMouseY_) * dpr);
        const bool ctrl  = e->modifiers().testFlag(Qt::ControlModifier);
        const bool alt   = e->modifiers().testFlag(Qt::AltModifier);
        const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);

        // With a note tool armed a left-drag draws. Checked FIRST: a pick pass would consume the click,
        // and panning would move the image out from under the stroke.
        if (jefe::qt::noteDrawingArmed()) {
            const int plate = jefe::qt::plateAtViewportPos(
                int(lastMouseX_), int(lastMouseY_), width(), height());
            makeCurrent();
            const bool began = jefe::qt::noteDrawBegin(xFb, yFb, plate);
            doneCurrent();
            if (began) {
                noteDragActive_ = true;
                dragPlate_ = -1;          // do not also pan
                update();
                if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
                return;
            }
            // Missed the image, or the round is locked. Fall through to the
            // normal handling rather than swallowing the click silently.
        }

        makeCurrent();
        const bool picked = jefe::qt::viewportPickDown(xFb, yFb, ctrl, alt, shift) != 0;
        doneCurrent();
        // doPicking renders the selection buffer into the widget FBO, so always
        // repaint to restore the real frame, even when nothing was picked.
        update();
        if (picked) {
            pickDragActive_ = true;
            dragPlate_ = -1;     // don't also pan the plate under the cursor
            if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
            return;
        }

        // Hit-test which plate the click landed on so drag pan and (in single
        // mode) keyboard shortcuts target it.
        dragPlate_ = jefe::qt::plateAtViewportPos(
            int(lastMouseX_), int(lastMouseY_), width(), height());
        if (dragPlate_ >= 0) {
            // Sync the active-plate concept (plate-card highlight, some keyboard paths) with the last click.
            jefe::qt::setActivePlate(dragPlate_);
            emit plateStateChanged();
        }
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
}

void GlViewport_Qt::mouseReleaseEvent(QMouseEvent* e) {
    if (noteDragActive_) {
        noteDragActive_ = false;
        if (jefe::qt::noteDrawIsText()) {
            // A text note needs its words before it exists. Ask on release; Cancel
            // or an empty answer means "never mind", not an empty note.
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Text Note"), tr("Note:"), QLineEdit::Normal,
                QString(), &ok);
            setFocus(Qt::OtherFocusReason);   // the dialog took keyboard focus
            if (ok && !text.trimmed().isEmpty()) {
                jefe::qt::noteDrawSetText(text.trimmed().toStdString());
                jefe::qt::noteDrawEnd();
            } else {
                jefe::qt::noteDrawCancel();
            }
        } else {
            jefe::qt::noteDrawEnd();   // adds, broadcasts and saves the sidecar
        }
        update();
        emit plateStateChanged();  // the dock's list has a new row
        if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
        return;
    }

    if (pickDragActive_) {
        const float dpr = devicePixelRatioF();
        const int xFb = int(float(e->position().x()) * dpr);
        const int yFb = int((float(height()) - float(e->position().y())) * dpr);
        makeCurrent();
        jefe::qt::viewportPickUp(xFb, yFb,
                                 e->modifiers().testFlag(Qt::ControlModifier),
                                 e->modifiers().testFlag(Qt::AltModifier),
                                 e->modifiers().testFlag(Qt::ShiftModifier));
        doneCurrent();
        pickDragActive_ = false;
        update();
        if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
        return;
    }
    const bool wasDragging = dragPlate_ >= 0;
    dragPlate_ = -1;
    if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
    // Final widget sync once the drag ends, catching motion after the last throttled emit.
    if (wasDragging) {
        emit plateStateChanged();
        dragEmittedAny_ = false;
    }
}

void GlViewport_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (noteDragActive_) {
        const float dpr = devicePixelRatioF();
        const int xFb = int(float(e->position().x()) * dpr);
        const int yFb = int((float(height()) - float(e->position().y())) * dpr);
        makeCurrent();
        jefe::qt::noteDrawAppend(xFb, yFb);
        doneCurrent();
        update();   // live feedback: the line follows the cursor as it is drawn
        return;
    }

    // Pick-overlay drag (histogram sub-window): latched on press; runs the pick
    // dispatch with the GL context current and consumes the motion.
    if (pickDragActive_ && (e->buttons() & Qt::LeftButton)) {
        const float dpr = devicePixelRatioF();
        const int xFb  = int(float(e->position().x()) * dpr);
        const int yFb  = int((float(height()) - float(e->position().y())) * dpr);
        // gfcGLSubWindow::move applies (prevX-curX, prevY-curY) — same sign
        // convention as the plate pan path below.
        const int dxFb = int((lastMouseX_ - float(e->position().x())) * dpr);
        const int dyFb = int((lastMouseY_ - float(e->position().y())) * dpr);
        const bool ctrl  = e->modifiers().testFlag(Qt::ControlModifier);
        const bool alt   = e->modifiers().testFlag(Qt::AltModifier);
        const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
        makeCurrent();
        jefe::qt::viewportPickDrag(xFb, yFb, dxFb, dyFb, ctrl, alt, shift);
        doneCurrent();
        update();
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
        if (listener_) listener_->onEvent(jefe::ui::EventType::Drag);
        return;
    }

    // Remote pointer/trail gesture: SHIFT + left-drag over a plate (avoids macOS right-click ambiguity); the bridge converts to image space, ~60Hz.
    // Pointer-only: return so it doesn't also pan.
    if ((e->buttons() & Qt::LeftButton) &&
        e->modifiers().testFlag(Qt::ShiftModifier) && dragPlate_ >= 0) {
        const float dpr = devicePixelRatioF();
        const int xFb = int(float(e->position().x()) * dpr);
        const int yFb = int((float(height()) - float(e->position().y())) * dpr);
        // getCursorPositionIn2DSpace does gluUnProject against the plate's live GL
        // matrices, so the context MUST be current or glGet returns stale matrices.
        makeCurrent();
        jefe::qt::sendRemotePointer(xFb, yFb, dragPlate_);
        doneCurrent();
        update();
        return;
    }

    if (e->buttons() & Qt::LeftButton && dragPlate_ >= 0) {
        const float dpr = devicePixelRatioF();
        const bool gang = (e->modifiers() & Qt::AltModifier) != 0;

        // ---- Color-correction key+drag dispatch (W/E/Q/D/S) ----
        // Left-drag adds (eventX - prevX) * 0.01 to the matching field (Alt = every plate); returns early, pan and CC don't compose.
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
            // Throttle + queued emit of plateColorChanged, same 60Hz cap as the transform path.
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

        // ---- Pan path (no color key held) ----
        // Pans by (prevX - eventX, prevY - eventY) like FLTK; multiply by dpr: pan is in framebuffer pixels, Qt mouse positions are logical.
        const float dx = (lastMouseX_ - float(e->position().x())) * dpr;
        const float dy = (lastMouseY_ - float(e->position().y())) * dpr;
        // Alt-drag = gang-transform: pan every plate by the same delta.
        if (gang) {
            jefe::qt::panAllPlates(dx, dy);
        } else {
            jefe::qt::panPlate(dragPlate_, dx, dy);
        }
        update();
        // Lightweight per-drag signal: refreshes only the dragged plate's transform spinboxes, skipping the heavy plateStateChanged cascade.
        // Throttled to ~60Hz; mouseReleaseEvent emits the full plateStateChanged.
        constexpr qint64 kEmitIntervalMs = 16;
        if (!dragEmitTimer_.isValid()
            || dragEmitTimer_.elapsed() >= kEmitIntervalMs) {
            if (gang) {
                // Every card got panned; refresh each (queued and cache-gated).
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
    // macOS trackpads report pixelDelta; mice report angleDelta (eighths of a
    // degree, 120 per notch). Convert both to ~±1 per notch before zoomSpeed.
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
        // Targeted refresh: wheel only mutates the zoom field on one plate.
        if (plate >= 0) emit plateTransformChanged(plate);
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Wheel);
}

void GlViewport_Qt::keyPressEvent(QKeyEvent* e) {
    // Escape puts the pencil down. Checked before the chat handler claims
    // Escape, and only when a tool is armed, so chat keeps its cancel key.
    if (e->key() == Qt::Key_Escape && jefe::qt::noteDrawingArmed() &&
        !jefe::qt::remoteChatModeActive()) {
        jefe::qt::setNoteDrawingArmed(false);
        update();
        emit plateStateChanged();
        return;
    }

    // Remote chat entry: when in chat mode, keystrokes build the message.
    if (jefe::qt::remoteChatModeActive()) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            jefe::qt::remoteChatSubmit();
        } else if (e->key() == Qt::Key_Escape) {
            jefe::qt::remoteChatCancel();
        } else if (e->key() == Qt::Key_Backspace) {
            jefe::qt::remoteChatBackspace();
        } else if (!e->text().isEmpty() && e->text().at(0).isPrint()) {
            jefe::qt::remoteChatAppend(e->text().toStdString());
        }
        update();     // repaint the overlay with the new text
        return;       // consume — don't fall through to plate shortcuts
    }
    // Enter chat mode with Return when connected and not already typing.
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        jefe::qt::isRemoteConnected()) {
        jefe::qt::remoteChatBegin();
        update();
        return;
    }

    // Track CC modifier keys (W/E/Q/D/S) for left-drag in mouseMoveEvent.
    // Don't gate on autoRepeat: only set membership is needed.
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

    // Plate-control shortcuts. Unhandled keys fall through to the listener and
    // Qt's default propagation so menu mnemonics and dialog accelerators work.
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

        // F, H, V and T (and Shift/Alt variants) are ApplicationShortcuts in MainWindow_Qt so they fire regardless of focus;
        // a duplicate handler here would steal keystrokes from input widgets.

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

        // Playback. Space pauses; arrows step a frame.
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

        // Rewind / fast-forward to the start / end (transport rew/ffwd).
        case Qt::Key_Home:
            if (!ctrl && !alt) { jefe::qt::rewindPlayback(); handled(); return; }
            break;
        case Qt::Key_End:
            if (!ctrl && !alt) { jefe::qt::fastFwdPlayback(); handled(); return; }
            break;

        // Loop mode (FLTK 8/9/0): once / loop / bounce.
        case Qt::Key_8:
            if (!ctrl && !alt) { jefe::qt::setLoopMode(0); handled(); return; }
            break;
        case Qt::Key_9:
            if (!ctrl && !alt) { jefe::qt::setLoopMode(1); handled(); return; }
            break;
        case Qt::Key_0:
            if (!ctrl && !alt) { jefe::qt::setLoopMode(2); handled(); return; }
            break;

        // Playback direction (FLTK '.' fwd / ',' rev). The keys carry '<'/'>'
        // on a US layout — match the glyphs, not the shift state.
        case Qt::Key_Period:
        case Qt::Key_Greater:
            if (!ctrl && !alt) { jefe::qt::setPlayDirection(+1); handled(); return; }
            break;
        case Qt::Key_Comma:
        case Qt::Key_Less:
            if (!ctrl && !alt) { jefe::qt::setPlayDirection(-1); handled(); return; }
            break;

        // Single-frame step (FLTK 'c' fwd / 'x' rev) — same as Left/Right.
        case Qt::Key_C:
            if (!ctrl && !alt) { jefe::qt::stepFrame(+1); handled(); return; }
            break;
        case Qt::Key_X:
            if (!ctrl && !alt) { jefe::qt::stepFrame(-1); handled(); return; }
            break;

        // In / out points (FLTK I/O). Bare = at current frame; Shift = to the
        // timeline ends.
        case Qt::Key_I:
            if (alt && !ctrl) {
                // FLTK Alt+I: set in point at current frame + reload tracks.
                jefe::qt::setInPointAndLoad();
                handled();
                return;
            }
            if (!ctrl && !alt) {
                jefe::qt::setInPoint(shift ? jefe::qt::getFromFrame()
                                           : jefe::qt::getCurrentFrame());
                handled();
                return;
            }
            break;
        case Qt::Key_O:
            if (!ctrl && !alt) {
                jefe::qt::setOutPoint(shift ? jefe::qt::getToFrame()
                                            : jefe::qt::getCurrentFrame());
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
    // Pairs with keyPressEvent's modifier tracking. Qt fires autoRepeat
    // release/press pairs during a held key; the brief drop is invisible at 60Hz.
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
    // Cursor left the viewport: drop held drag-modifier keys, since Qt stops
    // delivering key events to an unfocused widget and the set would go stale.
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

    // Modal-open branch: forward the path to the active plate's strip and skip
    // the fast load; the load window owns the load configuration.
    if (loadWindowOpen_) {
        const int plateIdx = 0;  // always the first plate
        emit fileDroppedWhileLoadWindowOpen(plateIdx, path);
        e->acceptProposedAction();
        return;
    }

    // Modal-closed branch: scale-modifier fast path (plain 1.0, Shift 0.5, Shift+Cmd 0.25).
    // Other combos keep 1.0; Cmd-only is reserved for a future "load into a specific plate" gesture.
    const auto mods = e->keyboardModifiers();
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    // Qt::ControlModifier == Cmd on macOS, Ctrl on Linux/Windows.
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
        // scaleFactor() is the incremental change (1.0 = none); convert to the
        // signed additive delta zoomPlate / zoomAllPlates take.
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
            // Targeted refresh of just the zoom spinbox(es); pinch rate is
            // trackpad-driven so the 16ms throttle is unnecessary.
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
