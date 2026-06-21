#include "LUTPreview_qt.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QString>
#include <algorithm>
#include <QTimer>
#include <QMouseEvent>
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
    setMinimumHeight(120);
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
    setAccessibleName("LUT cloud preview");
    setMinimumHeight(120);
    // Slow auto-spin: advance yaw and repaint, but only while visible
    // (started/stopped in show/hideEvent so a hidden pane costs nothing).
    spin_ = new QTimer(this);
    spin_->setInterval(33);  // ~30fps
    connect(spin_, &QTimer::timeout, this, [this]() {
        yaw_ += 0.5f;
        if (yaw_ >= 360.f) yaw_ -= 360.f;
        update();
    });
}

void LutCloudWidget::setPoints(const std::vector<float>& p, int c) {
    pts_ = p;
    count_ = c;
    update();
}

void LutCloudWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.5f, 0.5f, 0.5f, 1.f);   // medium neutral gray
    glEnable(GL_DEPTH_TEST);
    glPointSize(3.0f);
}

void LutCloudWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    // Manual perspective frustum (no glu — cross-platform). 2.1 fixed-function.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = (h > 0) ? (double)w / h : 1.0;
    const double n = 0.5, top = n * 0.55, right = top * aspect;
    glFrustum(-right, right, -top, top, n, 10.0);
    glMatrixMode(GL_MODELVIEW);
}

void LutCloudWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.f, 0.f, -2.2f);     // pull the cube back into the frustum
    glRotatef(pitch_, 1.f, 0.f, 0.f);
    glRotatef(yaw_,   0.f, 1.f, 0.f);
    glTranslatef(-0.5f, -0.5f, -0.5f); // pivot around the cube center; (0,0,0)
                                       // (the black corner) is the color-space origin

    // Axes: long red/green/blue lines through the origin, one per channel
    // (R→X, G→Y, B→Z). Drawn "infinite" by extending well past the cube.
    const float A = 4.0f;
    glBegin(GL_LINES);
        glColor3f(1.f, 0.f, 0.f); glVertex3f(-A, 0.f, 0.f); glVertex3f(A, 0.f, 0.f);
        glColor3f(0.f, 1.f, 0.f); glVertex3f(0.f, -A, 0.f); glVertex3f(0.f, A, 0.f);
        glColor3f(0.f, 0.f, 1.f); glVertex3f(0.f, 0.f, -A); glVertex3f(0.f, 0.f, A);
    glEnd();

    if (count_ > 0) {
        glBegin(GL_POINTS);
        for (int i = 0; i < count_; ++i) {
            const float* v = &pts_[(size_t)i * 6];
            glColor3f(v[3], v[4], v[5]);
            glVertex3f(v[0], v[1], v[2]);
        }
        glEnd();
    }
}

void LutCloudWidget::showEvent(QShowEvent*) { if (spin_) spin_->start(); }
void LutCloudWidget::hideEvent(QHideEvent*) { if (spin_) spin_->stop(); }

void LutCloudWidget::mousePressEvent(QMouseEvent* e) {
    lastX_ = (int)e->position().x();
    lastY_ = (int)e->position().y();
}

void LutCloudWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    const int x = (int)e->position().x(), y = (int)e->position().y();
    yaw_   += (x - lastX_) * 0.5f;
    pitch_ += (y - lastY_) * 0.5f;
    lastX_ = x; lastY_ = y;
    update();
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
    emptyPage_->setMinimumHeight(120);
    curve_ = new LutCurveWidget(this);
    cloud_ = new LutCloudWidget(this);
    stack_->addWidget(emptyPage_);   // index 0
    stack_->addWidget(curve_);       // index 1
    stack_->addWidget(cloud_);       // index 2
    stack_->setCurrentIndex(0);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 4, 0, 0);
    outer->setSpacing(4);
    outer->addLayout(info);
    outer->addWidget(stack_, 1);
}

void LUTPreview_Qt::setLut(const jefe::qt::LutPreviewData& d) {
    if (!d.valid) {
        nameLbl_->setText("—"); typeLbl_->setText("—");
        sizeLbl_->setText("—"); depthLbl_->setText("—"); maxLbl_->setText("—");
        stack_->setCurrentIndex(0);   // empty page
        return;
    }
    nameLbl_->setText(QString::fromStdString(d.name));
    typeLbl_->setText(d.is3D ? "3D cube" : "1D");
    sizeLbl_->setText(d.is3D ? QString("%1³").arg(d.size)
                             : QString("%1 samples").arg(d.size));
    depthLbl_->setText(QString("%1→%2 bit").arg(d.fromBits).arg(d.toBits));
    maxLbl_->setText(d.is3D ? QString("—") : QString::number(d.max1D, 'g', 4));

    if (d.is3D) {
        cloud_->setPoints(d.points3D, d.point3DCount);
        stack_->setCurrentIndex(2);
    } else {
        curve_->setCurve(d.curve1D, d.max1D);
        stack_->setCurrentIndex(1);
    }
}
