#include "LUTPreview_qt.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QString>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <algorithm>
#include <QTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

// ---- LutCurveWidget ----
LutCurveWidget::LutCurveWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("lut.preview.curve");
    setAccessibleName("LUT curve preview");
    setMinimumHeight(80);
}

void LutCurveWidget::setCurve(const std::vector<float>& samples, float maxValue) {
    samples_ = samples;
    maxValue_ = (maxValue > 0.f) ? maxValue : 1.f;
    update();
}

void LutCurveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(20, 20, 20));

    // Reserve margins for axis labels/ticks, then make the plot SQUARE so
    // an identity LUT reads as a 45° diagonal.
    const int leftM = 34, bottomM = 22, topM = 8, rightM = 8;
    QRect avail = r.adjusted(leftM, topM, -rightM, -bottomM);
    const int side = std::max(std::min(avail.width(), avail.height()), 10);
    const QRect plot(avail.left(), avail.bottom() - side, side, side);

    // Grid + square border.
    p.setPen(QColor(60, 60, 60));
    p.drawRect(plot);
    for (int i = 1; i < 4; ++i) {
        const int gx = plot.left() + plot.width() * i / 4;
        const int gy = plot.top() + plot.height() * i / 4;
        p.drawLine(gx, plot.top(), gx, plot.bottom());
        p.drawLine(plot.left(), gy, plot.right(), gy);
    }

    if (samples_.size() < 2) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(plot, Qt::AlignCenter, "no 1D data");
        return;
    }
    const int n = (int)samples_.size();

    // Axis labels + numeric ticks. X (in): 0 .. n samples. Y (out): 0 .. max.
    QFont f = p.font();
    f.setPointSizeF(8.0);
    p.setFont(f);
    p.setPen(QColor(150, 150, 150));
    // X ticks.
    p.drawText(QRect(plot.left() - 12, plot.bottom() + 2, 24, 14),
               Qt::AlignCenter, "0");
    p.drawText(QRect(plot.right() - 24, plot.bottom() + 2, 28, 14),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(n));
    // Y ticks (top = max, bottom = 0).
    p.drawText(QRect(0, plot.top() - 6, leftM - 4, 14),
               Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue_, 'g', 3));
    p.drawText(QRect(0, plot.bottom() - 7, leftM - 4, 14),
               Qt::AlignRight | Qt::AlignVCenter, "0");
    // Axis names: "in" under the X axis, "out" rotated along the Y axis.
    p.setPen(QColor(180, 180, 180));
    p.drawText(QRect(plot.left(), plot.bottom() + 6, plot.width(), 16),
               Qt::AlignCenter, "in");
    p.save();
    p.translate(10, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-side / 2, -8, side, 16), Qt::AlignCenter, "out");
    p.restore();

    // Curve: x = input index, y = output / maxValue (inverted), in the square.
    p.setPen(QPen(QColor(0xd4, 0x77, 0x1e), 1.5));
    QPointF prev;
    for (int i = 0; i < n; ++i) {
        const float fx = (float)i / (n - 1);
        float fy = samples_[i] / maxValue_;
        if (fy < 0) fy = 0;
        if (fy > 1) fy = 1;
        const QPointF cur(plot.left() + fx * plot.width(),
                          plot.bottom() - fy * plot.height());
        if (i > 0) p.drawLine(prev, cur);
        prev = cur;
    }
}

// ---- LutCloudWidget ----
LutCloudWidget::LutCloudWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setObjectName("lut.preview.cloud");
    setAccessibleName("LUT cloud inspector");
    setMinimumHeight(90);
    setFocusPolicy(Qt::StrongFocus);   // so the F key reaches keyPressEvent
    // Optional auto-spin (off by default; the camera is the main interaction).
    spin_ = new QTimer(this);
    spin_->setInterval(33);  // ~30fps
    connect(spin_, &QTimer::timeout, this, [this]() {
        yaw_ += 0.4f;
        if (yaw_ >= 360.f) yaw_ -= 360.f;
        update();
    });
    // Deform morph animation (~0.4s ease) between uniform and deformed.
    morph_ = new QTimer(this);
    morph_->setInterval(16);  // ~60fps
    connect(morph_, &QTimer::timeout, this, [this]() {
        const float step = 0.04f;
        if (deformAmt_ < deformTarget_)      deformAmt_ = std::min(deformAmt_ + step, deformTarget_);
        else if (deformAmt_ > deformTarget_) deformAmt_ = std::max(deformAmt_ - step, deformTarget_);
        if (deformAmt_ == deformTarget_) morph_->stop();
        update();
    });
}

void LutCloudWidget::setCube(const std::vector<float>& cubeRGB, int cubeSize) {
    cube_ = cubeRGB;
    cs_ = cubeSize;
    update();
}

void LutCloudWidget::setShowSolid(bool on) { showSolid_ = on; update(); }
void LutCloudWidget::setShowGrid(bool on)  { showGrid_  = on; update(); }
void LutCloudWidget::setShowDots(bool on)  { showDots_  = on; update(); }
void LutCloudWidget::setMarker(int m)      { marker_ = m; update(); }
void LutCloudWidget::setDeformed(bool on) {
    deformTarget_ = on ? 1.f : 0.f;
    if (morph_ && deformAmt_ != deformTarget_) morph_->start();
}
void LutCloudWidget::setBackground(int g)  { bg_ = std::clamp(g, 0, 100) / 100.f; update(); }

void LutCloudWidget::setAutoSpin(bool on) {
    autoSpin_ = on;
    if (spin_) { if (on && isVisible()) spin_->start(); else spin_->stop(); }
    update();
}

void LutCloudWidget::focusCube() {
    yaw_ = 35.f; pitch_ = 22.f; dist_ = 2.6f;
    target_[0] = target_[1] = target_[2] = 0.5f;
    update();
}

const float* LutCloudWidget::nodeColor(int ix, int iy, int iz) const {
    return &cube_[(((size_t)ix * cs_ + iy) * cs_ + iz) * 3];
}

void LutCloudWidget::nodePos(int ix, int iy, int iz, float out[3]) const {
    // Lerp between the uniform grid position and the output-color position
    // by the (animated) morph factor — 0 = uniform, 1 = fully deformed.
    const float d = (cs_ > 1) ? (float)(cs_ - 1) : 1.f;
    const float ux = ix / d, uy = iy / d, uz = iz / d;
    const float a = deformAmt_;
    if (a <= 0.f) { out[0] = ux; out[1] = uy; out[2] = uz; return; }
    const float* c = nodeColor(ix, iy, iz);
    out[0] = ux + (c[0] - ux) * a;
    out[1] = uy + (c[1] - uy) * a;
    out[2] = uz + (c[2] - uz) * a;
}

void LutCloudWidget::initializeGL() {
    initializeOpenGLFunctions();
    // Note: per-frame GL state (depth test etc.) is set in paintGL, not here.
    // The QPainter used for axis labels resets GL state every frame, so
    // enabling depth test once in initializeGL would only hold for frame 1.
}

void LutCloudWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    // Projection is rebuilt each frame in applyCamera() so the near/far
    // planes track the camera distance (avoids clipping when dollying).
}

void LutCloudWidget::applyCamera() {
    // Dynamic frustum: bracket the cube + axes around the current distance so
    // neither plane clips the scene as the camera dollies. Keeping top = near
    // * 0.6 holds the field of view constant while near varies.
    const double aspect = (height() > 0) ? (double)width() / height() : 1.0;
    double znear = dist_ - 2.0;
    if (znear < 0.05) znear = 0.05;
    const double zfar = dist_ + 8.0;
    const double top = znear * 0.6, right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, znear, zfar);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.f, 0.f, -dist_);    // pull back by camera distance
    glRotatef(pitch_, 1.f, 0.f, 0.f);
    glRotatef(yaw_,   0.f, 1.f, 0.f);
    glTranslatef(-target_[0], -target_[1], -target_[2]);
}

void LutCloudWidget::drawAxes() {
    const float A = 4.0f;
    glLineWidth(1.5f);
    glBegin(GL_LINES);
        glColor3f(1.f, 0.f, 0.f); glVertex3f(-A, 0.f, 0.f); glVertex3f(A, 0.f, 0.f);
        glColor3f(0.f, 1.f, 0.f); glVertex3f(0.f, -A, 0.f); glVertex3f(0.f, A, 0.f);
        glColor3f(0.f, 0.f, 1.f); glVertex3f(0.f, 0.f, -A); glVertex3f(0.f, 0.f, A);
    glEnd();
}

void LutCloudWidget::drawDots() {
    float p[3];
    if (marker_ == MarkerDot) {
        glBegin(GL_POINTS);
        for (int x = 0; x < cs_; ++x) for (int y = 0; y < cs_; ++y) for (int z = 0; z < cs_; ++z) {
            const float* c = nodeColor(x, y, z);
            nodePos(x, y, z, p);
            glColor3f(c[0], c[1], c[2]);
            glVertex3f(p[0], p[1], p[2]);
        }
        glEnd();
        return;
    }
    const float h = 0.012f;   // marker half-size
    for (int x = 0; x < cs_; ++x) for (int y = 0; y < cs_; ++y) for (int z = 0; z < cs_; ++z) {
        const float* c = nodeColor(x, y, z);
        nodePos(x, y, z, p);
        glColor3f(c[0], c[1], c[2]);
        if (marker_ == MarkerCube) {
            // 6 quad faces of a small cube centered at p.
            const float xa=p[0]-h, xb=p[0]+h, ya=p[1]-h, yb=p[1]+h, za=p[2]-h, zb=p[2]+h;
            glBegin(GL_QUADS);
                glVertex3f(xa,ya,za); glVertex3f(xb,ya,za); glVertex3f(xb,yb,za); glVertex3f(xa,yb,za);
                glVertex3f(xa,ya,zb); glVertex3f(xb,ya,zb); glVertex3f(xb,yb,zb); glVertex3f(xa,yb,zb);
                glVertex3f(xa,ya,za); glVertex3f(xa,yb,za); glVertex3f(xa,yb,zb); glVertex3f(xa,ya,zb);
                glVertex3f(xb,ya,za); glVertex3f(xb,yb,za); glVertex3f(xb,yb,zb); glVertex3f(xb,ya,zb);
                glVertex3f(xa,ya,za); glVertex3f(xb,ya,za); glVertex3f(xb,ya,zb); glVertex3f(xa,ya,zb);
                glVertex3f(xa,yb,za); glVertex3f(xb,yb,za); glVertex3f(xb,yb,zb); glVertex3f(xa,yb,zb);
            glEnd();
        } else { // MarkerTetra — 4 triangular faces.
            const float a=p[0], b=p[1], c2=p[2];
            const float v0[3]={a, b+h, c2};
            const float v1[3]={a-h, b-h, c2+h};
            const float v2[3]={a+h, b-h, c2+h};
            const float v3[3]={a, b-h, c2-h};
            glBegin(GL_TRIANGLES);
                glVertex3fv(v0); glVertex3fv(v1); glVertex3fv(v2);
                glVertex3fv(v0); glVertex3fv(v2); glVertex3fv(v3);
                glVertex3fv(v0); glVertex3fv(v3); glVertex3fv(v1);
                glVertex3fv(v1); glVertex3fv(v3); glVertex3fv(v2);
            glEnd();
        }
    }
}

void LutCloudWidget::drawGrid() {
    float pa[3], pb[3];
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int x = 0; x < cs_; ++x) for (int y = 0; y < cs_; ++y) for (int z = 0; z < cs_; ++z) {
        const float* ca = nodeColor(x, y, z);
        nodePos(x, y, z, pa);
        if (x + 1 < cs_) { const float* cb = nodeColor(x+1,y,z); nodePos(x+1,y,z,pb);
            glColor3f(ca[0],ca[1],ca[2]); glVertex3fv(pa); glColor3f(cb[0],cb[1],cb[2]); glVertex3fv(pb); }
        if (y + 1 < cs_) { const float* cb = nodeColor(x,y+1,z); nodePos(x,y+1,z,pb);
            glColor3f(ca[0],ca[1],ca[2]); glVertex3fv(pa); glColor3f(cb[0],cb[1],cb[2]); glVertex3fv(pb); }
        if (z + 1 < cs_) { const float* cb = nodeColor(x,y,z+1); nodePos(x,y,z+1,pb);
            glColor3f(ca[0],ca[1],ca[2]); glVertex3fv(pa); glColor3f(cb[0],cb[1],cb[2]); glVertex3fv(pb); }
    }
    glEnd();
}

void LutCloudWidget::drawSolid() {
    // The outer shell: the 6 boundary faces (each axis at its min and max
    // index), each drawn as Gouraud-shaded triangle strips with per-vertex
    // output colors. No interior fill.
    const int m = cs_ - 1;
    if (m < 1) return;

    // emit one vertex (node a,b,c with axis `fixed` held) — `face` picks
    // which axis is constant: 0 → x=k, 1 → y=k, 2 → z=k.
    auto emitVert = [&](int axis, int k, int u, int v) {
        int x, y, z;
        if (axis == 0)      { x = k; y = u; z = v; }
        else if (axis == 1) { x = u; y = k; z = v; }
        else                { x = u; y = v; z = k; }
        float p[3]; const float* c = nodeColor(x, y, z);
        nodePos(x, y, z, p);
        glColor3f(c[0], c[1], c[2]);
        glVertex3fv(p);
    };
    // A face = the (cs × cs) lattice on a plane; emit it as per-row triangle
    // strips over u (rows) and v (columns).
    auto face = [&](int axis, int k) {
        for (int u = 0; u < m; ++u) {
            glBegin(GL_TRIANGLE_STRIP);
            for (int v = 0; v < cs_; ++v) {
                emitVert(axis, k, u,     v);
                emitVert(axis, k, u + 1, v);
            }
            glEnd();
        }
    };
    face(0, 0); face(0, m);   // x = min / max
    face(1, 0); face(1, m);   // y = min / max
    face(2, 0); face(2, m);   // z = min / max
}

void LutCloudWidget::paintGL() {
    // Wrap the raw GL in beginNativePainting/endNativePainting so QPainter
    // (used for the axis labels below) saves/restores GL state cleanly. Set
    // depth state every frame — QPainter disables GL_DEPTH_TEST when it runs,
    // so enabling it only in initializeGL would break occlusion from frame 2.
    QPainter painter(this);
    painter.beginNativePainting();

    glClearColor(bg_, bg_, bg_, 1.f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glPointSize(3.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    applyCamera();

    drawAxes();
    if (cs_ > 0 && !cube_.empty()) {
        if (showSolid_) drawSolid();
        if (showGrid_)  drawGrid();
        if (showDots_)  drawDots();
    }

    // Capture the matrices for label projection while the GL state is current.
    GLfloat proj[16], mv[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);

    painter.endNativePainting();

    // Axis labels (R/G/B) over the GL, recolored for contrast vs the bg.
    drawAxisLabels(painter, proj, mv);
}

void LutCloudWidget::drawAxisLabels(QPainter& p, const float* proj, const float* mv) {
    const float dpr = (float)devicePixelRatioF();
    const int W = (int)(width()), H = (int)(height());
    auto project = [&](float x, float y, float z, QPointF& out) -> bool {
        // eye = mv * v ; clip = proj * eye  (column-major GL matrices)
        float ex = mv[0]*x + mv[4]*y + mv[8]*z + mv[12];
        float ey = mv[1]*x + mv[5]*y + mv[9]*z + mv[13];
        float ez = mv[2]*x + mv[6]*y + mv[10]*z + mv[14];
        float ew = mv[3]*x + mv[7]*y + mv[11]*z + mv[15];
        float cx = proj[0]*ex + proj[4]*ey + proj[8]*ez + proj[12]*ew;
        float cy = proj[1]*ex + proj[5]*ey + proj[9]*ez + proj[13]*ew;
        float cw = proj[3]*ex + proj[7]*ey + proj[11]*ez + proj[15]*ew;
        if (cw <= 0.0001f) return false;
        // NDC -> widget pixels (logical, since QPainter uses logical coords).
        out.setX((cx / cw * 0.5f + 0.5f) * W);
        out.setY((1.f - (cy / cw * 0.5f + 0.5f)) * H);
        return true;
    };
    (void)dpr;
    const QColor txt = (bg_ > 0.5f) ? QColor(20, 20, 20) : QColor(235, 235, 235);
    p.setPen(txt);
    QFont f = p.font(); f.setPointSizeF(9.0); f.setBold(true); p.setFont(f);
    QPointF s;
    if (project(1.15f, 0.f, 0.f, s)) p.drawText(s, "R");
    if (project(0.f, 1.15f, 0.f, s)) p.drawText(s, "G");
    if (project(0.f, 0.f, 1.15f, s)) p.drawText(s, "B");
}

void LutCloudWidget::showEvent(QShowEvent*) { if (spin_ && autoSpin_) spin_->start(); }
void LutCloudWidget::hideEvent(QHideEvent*) { if (spin_) spin_->stop(); }

void LutCloudWidget::mousePressEvent(QMouseEvent* e) {
    lastX_ = (int)e->position().x();
    lastY_ = (int)e->position().y();
    setFocus();
}

void LutCloudWidget::mouseMoveEvent(QMouseEvent* e) {
    const int x = (int)e->position().x(), y = (int)e->position().y();
    const int dx = x - lastX_, dy = y - lastY_;
    lastX_ = x; lastY_ = y;
    const bool alt = (e->modifiers() & Qt::AltModifier);
    // Maya convention: orbit Alt+LMB, pan Alt+MMB, dolly Alt+RMB. Without
    // Alt, LMB still orbits (forgiving on a trackpad with no middle button).
    if (alt && (e->buttons() & Qt::MiddleButton)) {
        const float k = dist_ * 0.0025f;
        target_[0] -= dx * k; target_[1] += dy * k;   // pan in screen plane
    } else if (alt && (e->buttons() & Qt::RightButton)) {
        dist_ *= (1.f + dy * 0.01f);                  // dolly
        if (dist_ < 0.2f) dist_ = 0.2f;
    } else if (e->buttons() & Qt::LeftButton) {
        yaw_ += dx * 0.4f; pitch_ += dy * 0.4f;       // orbit
    } else {
        return;
    }
    update();
}

void LutCloudWidget::wheelEvent(QWheelEvent* e) {
    const float d = e->angleDelta().y() / 120.f;      // notches
    dist_ *= (1.f - d * 0.1f);
    if (dist_ < 0.2f) dist_ = 0.2f;
    update();
}

void LutCloudWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_F) { focusCube(); return; }
    QOpenGLWidget::keyPressEvent(e);
}

// ---- LUTPreview_Qt ----
LUTPreview_Qt::LUTPreview_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("lut.preview");
    setAccessibleName("LUT preview");

    auto* info = new QGridLayout();
    info->setContentsMargins(0, 0, 0, 0);
    info->setHorizontalSpacing(8);
    info->setVerticalSpacing(2);
    auto mkVal = [this](const char* objName) {
        auto* l = new QLabel("—", this);
        l->setObjectName(objName);
        l->setStyleSheet("color:#ccc;");
        return l;
    };
    auto mkKey = [this](const char* text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet("color:#888;");
        return l;
    };
    nameLbl_  = mkVal("lut.preview.name");
    typeLbl_  = mkVal("lut.preview.type");
    sizeLbl_  = mkVal("lut.preview.size");
    depthLbl_ = mkVal("lut.preview.depth");
    maxLbl_   = mkVal("lut.preview.max");
    info->addWidget(mkKey("Name:"),  0, 0); info->addWidget(nameLbl_,  0, 1);
    info->addWidget(mkKey("Type:"),  1, 0); info->addWidget(typeLbl_,  1, 1);
    info->addWidget(mkKey("Size:"),  2, 0); info->addWidget(sizeLbl_,  2, 1);
    info->addWidget(mkKey("Depth:"), 3, 0); info->addWidget(depthLbl_, 3, 1);
    info->addWidget(mkKey("Max:"),   4, 0); info->addWidget(maxLbl_,   4, 1);
    info->setColumnStretch(1, 1);

    stack_ = new QStackedWidget(this);
    emptyPage_ = new QWidget(this);
    emptyPage_->setMinimumHeight(60);
    curve_ = new LutCurveWidget(this);
    cloud_ = new LutCloudWidget(this);
    stack_->addWidget(emptyPage_);   // index 0
    stack_->addWidget(curve_);       // index 1
    stack_->addWidget(cloud_);       // index 2
    stack_->setCurrentIndex(0);

    // ---- 3D inspector controls (shown only for 3D LUTs) ----
    controls_ = new QWidget(this);
    auto* cg = new QGridLayout(controls_);
    cg->setContentsMargins(0, 2, 0, 2);
    cg->setHorizontalSpacing(8);
    cg->setVerticalSpacing(2);

    auto* solid = new QCheckBox("Solid", controls_);
    solid->setObjectName("lut.preview.mode.solid");
    auto* grid = new QCheckBox("Grid", controls_);
    grid->setObjectName("lut.preview.mode.grid");
    auto* dots = new QCheckBox("Dots", controls_);
    dots->setObjectName("lut.preview.mode.dots");
    dots->setChecked(true);
    auto* deformed = new QCheckBox("Deformed", controls_);
    deformed->setObjectName("lut.preview.deformed");
    auto* autospin = new QCheckBox("Spin", controls_);
    autospin->setObjectName("lut.preview.autospin");

    auto* marker = new QComboBox(controls_);
    marker->setObjectName("lut.preview.marker");
    marker->addItem("Dot");          // LutCloudWidget::MarkerDot
    marker->addItem("Mini-cube");    // MarkerCube
    marker->addItem("Tetrahedron");  // MarkerTetra

    auto* bg = new QSlider(Qt::Horizontal, controls_);
    bg->setObjectName("lut.preview.bg");
    bg->setRange(0, 100);
    bg->setValue(50);
    bg->setToolTip("Background brightness");

    cg->addWidget(solid,    0, 0);
    cg->addWidget(grid,     0, 1);
    cg->addWidget(dots,     0, 2);
    cg->addWidget(marker,   0, 3);
    cg->addWidget(deformed, 1, 0);
    cg->addWidget(autospin, 1, 1);
    cg->addWidget(new QLabel("Bg", controls_), 1, 2, Qt::AlignRight);
    cg->addWidget(bg,       1, 3);
    cg->setColumnStretch(3, 1);

    connect(solid,    &QCheckBox::toggled, cloud_, &LutCloudWidget::setShowSolid);
    connect(grid,     &QCheckBox::toggled, cloud_, &LutCloudWidget::setShowGrid);
    connect(dots,     &QCheckBox::toggled, cloud_, &LutCloudWidget::setShowDots);
    connect(deformed, &QCheckBox::toggled, cloud_, &LutCloudWidget::setDeformed);
    connect(autospin, &QCheckBox::toggled, cloud_, &LutCloudWidget::setAutoSpin);
    connect(marker, QOverload<int>::of(&QComboBox::currentIndexChanged),
            cloud_, &LutCloudWidget::setMarker);
    connect(bg, &QSlider::valueChanged, cloud_, &LutCloudWidget::setBackground);

    // Group the info + controls into a "side" section so it can flow next to
    // (wide aspect) or above (tall aspect) the canvas — see resizeEvent.
    side_ = new QWidget(this);
    auto* sideL = new QVBoxLayout(side_);
    sideL->setContentsMargins(0, 0, 0, 0);
    sideL->setSpacing(4);
    sideL->addLayout(info);
    sideL->addWidget(controls_);
    sideL->addStretch(1);

    mainBox_ = new QBoxLayout(QBoxLayout::TopToBottom, this);
    mainBox_->setContentsMargins(0, 4, 0, 0);
    mainBox_->setSpacing(6);
    mainBox_->addWidget(side_);
    mainBox_->addWidget(stack_, /*stretch*/ 1);

    controls_->setVisible(false);   // until a 3D LUT is selected
}

void LUTPreview_Qt::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (!mainBox_) return;
    // Flow the two sections by aspect: side-by-side when the pane is wide,
    // stacked when it's tall, so neither section starves the canvas.
    const bool wide = width() > height() * 1.25;
    const auto dir = wide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (mainBox_->direction() != dir) {
        mainBox_->setDirection(dir);
        // Keep the side section compact when alongside the canvas.
        side_->setMaximumWidth(wide ? 200 : QWIDGETSIZE_MAX);
    }
}

void LUTPreview_Qt::setLut(const jefe::qt::LutPreviewData& d) {
    if (!d.valid) {
        nameLbl_->setText("—"); typeLbl_->setText("—");
        sizeLbl_->setText("—"); depthLbl_->setText("—"); maxLbl_->setText("—");
        stack_->setCurrentIndex(0);   // empty page
        controls_->setVisible(false);
        return;
    }
    nameLbl_->setText(QString::fromStdString(d.name));
    typeLbl_->setText(d.is3D ? "3D cube" : "1D");
    sizeLbl_->setText(d.is3D ? QString("%1³").arg(d.size)
                             : QString("%1 samples").arg(d.size));
    depthLbl_->setText(QString("%1→%2 bit").arg(d.fromBits).arg(d.toBits));
    maxLbl_->setText(d.is3D ? QString("—") : QString::number(d.max1D, 'g', 4));

    if (d.is3D) {
        cloud_->setCube(d.cubeRGB, d.cubeSize);
        controls_->setVisible(true);
        stack_->setCurrentIndex(2);
    } else {
        controls_->setVisible(false);
        curve_->setCurve(d.curve1D, d.max1D);
        stack_->setCurrentIndex(1);
    }
}
