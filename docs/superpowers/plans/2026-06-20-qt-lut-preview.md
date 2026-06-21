# Qt LUT Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-panel LUT preview to the Qt LUT dock — a QPainter 2D curve for 1D LUTs, an auto-spinning GL point cloud for 3D LUTs, an info subpanel, and a shown-by-default toggle.

**Architecture:** The preview widget reaches LUT sample data only through a new `jefe::qt::getLutPreview` bridge accessor returning a Qt-safe `LutPreviewData` struct (the bridge `.cpp` includes `trilerp.h`/glad; the widget never does — TU separation). The 3D cloud is a self-contained `QOpenGLWidget` with its own GL 2.1-compatibility context using immediate-mode `GL_POINTS` (no glad, no shaders).

**Tech Stack:** C++20, Qt6 Widgets + OpenGLWidgets, the `SequenceLoadBridge_qt` bridge.

**Spec:** `docs/superpowers/specs/2026-06-20-qt-lut-preview-design.md`

**Grounding (verified):**
- LUT panel: `LUTPanel_Qt` in `src/qt/FXLutPanel_qt.{h,cpp}` (shares the file with the FX panel). Has `QListWidget* list_`, `QLabel* status_`, `applySelected()`, `refreshList()`. Outer `QVBoxLayout`: status, list (stretch 1), button row. List row 0 = "(No LUT)", row r≥1 = `getLutNames()[r-1]`. `applyLUTToActivePlate(row)` → `setLUT(q, row-1)`.
- `CubeLUT` (`src/trilerp.h`): public `int type` (`enum LUTTYPES{BASELIGHT3DCUBE=0,JEFECHECK1D=1,IMAGELUT2D=2}`), `int size`, `float lut1D[1024]`, `std::vector<std::vector<std::vector<Vec3D>>> cube`, `float maximum1DValue`, `int fromBits,toBits`, `std::string getNameNoPath()`. `Vec3D` (`src/vec3d.h`) = public `double x,y,z`.
- `gfcLUTManager::getLUT(int index)` returns a `CubeLUT` **by value** (range-checked; empty on miss). `getAllNames()` = lutArray order.
- GL profile: `src/main_qt.cpp:93-95` sets `QSurfaceFormat` NoProfile, version 2.1 → immediate mode + fixed-function pipeline are valid in any `QOpenGLWidget`.
- `CMakeLists.txt:45` `file(GLOB JEFECHECK_QT_SOURCES src/qt/*.cpp)` — a new `src/qt/*.cpp` is picked up after a cmake **reconfigure** (`cmake -B build_qt`), not by `cmake --build` alone.

---

## Conventions for every task

- Build: `cmake --build build_qt -j` → `[100%] Built target jefecheck`. After **adding a new source file**, first run `cmake -B build_qt` to re-glob. clangd "QWidget not found" diagnostics are false positives (no Qt include paths in the language server); only the CMake result matters.
- Manual launch: `pkill -f jefecheck.app/Contents/MacOS/jefecheck; sleep 0.5; open build_qt/jefecheck.app`. The LUT dock is one of the right-side docks (`dock.luts`).
- No C++ unit-test harness; C++ is verified by build + manual launch. Automated tests are Appium/Python.
- Commit each task; stage explicit file lists (never `git add -A`).
- End commit messages with: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

## File Structure

- **`src/qt/SequenceLoadBridge_qt.{h,cpp}`** — `LutPreviewData` struct + `getLutPreview(int)`.
- **`src/qt/LUTPreview_qt.{h,cpp}`** (new) — `LutCurveWidget` (QPainter 1D), `LutCloudWidget` (QOpenGLWidget 3D), `LUTPreview_Qt` (container: info labels + `QStackedWidget`).
- **`src/qt/FXLutPanel_qt.{h,cpp}`** — `previewToggle_` + `preview_`, selection wiring.
- **`tests/ui/jefecheck/locators.py`** + **`tests/ui/test_lut_preview.py`** + **`developer_notes.md`**.

---

## Task 1: Bridge — `LutPreviewData` + `getLutPreview`

**Files:** Modify `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`

- [ ] **Step 1: Declare the struct + accessor (header)**

In `src/qt/SequenceLoadBridge_qt.h`, just before `}  // namespace jefe::qt`, add:

```cpp
// Qt-safe snapshot of a LUT for the LUT-panel preview. The bridge .cpp
// (which includes trilerp.h) copies the sample data out so the widget
// never touches CubeLUT/glad. guiLutIndex is the LUT-panel row:
// 0 = "(No LUT)" → valid=false; row r≥1 → lutManager.getLUT(r-1).
struct LutPreviewData {
    bool        valid    = false;
    int         type     = 0;      // CubeLUT::LUTTYPES
    bool        is3D     = false;  // type != JEFECHECK1D
    int         size     = 0;      // samples (1D) or cube edge (3D)
    int         fromBits = 0;
    int         toBits   = 0;
    float       max1D    = 1.0f;
    std::string name;
    // 1D: `size` output samples (raw, in [0, max1D]).
    std::vector<float> curve1D;
    // 3D: flat [x,y,z, r,g,b] per sampled point. Positions are normalized
    // grid coords in [0,1]; colors are the clamped mapped RGB. Subsampled
    // so the count stays bounded.
    std::vector<float> points3D;
    int                point3DCount = 0;  // points3D.size() / 6
};

LutPreviewData getLutPreview(int guiLutIndex);
```

- [ ] **Step 2: Implement (cpp)**

In `src/qt/SequenceLoadBridge_qt.cpp`, near the other LUT accessors (after `getLUTOnActivePlate`), add. `lutManager`, `<algorithm>`, `<cmath>` are already available; add `#include <cmath>` to the top includes if not present.

```cpp
LutPreviewData getLutPreview(int guiLutIndex) {
    LutPreviewData d;
    if (guiLutIndex <= 0) return d;          // 0 = "(No LUT)"
    CubeLUT lut = lutManager.getLUT(guiLutIndex - 1);
    if (lut.size <= 0) return d;
    d.valid    = true;
    d.type     = lut.type;
    d.is3D     = (lut.type != CubeLUT::JEFECHECK1D);
    d.size     = lut.size;
    d.fromBits = lut.fromBits;
    d.toBits   = lut.toBits;
    d.max1D    = lut.maximum1DValue > 0.f ? lut.maximum1DValue : 1.f;
    d.name     = lut.getNameNoPath();

    if (!d.is3D) {
        const int n = std::min(lut.size, 1024);
        d.curve1D.reserve(n);
        for (int i = 0; i < n; ++i) d.curve1D.push_back(lut.lut1D[i]);
        return d;
    }

    // 3D: subsample the cube so the emitted point count stays bounded.
    constexpr int kMaxPreviewPoints = 20000;
    const int s = lut.size;
    // per-axis target so target^3 <= kMaxPreviewPoints
    int axisTarget = (int)std::cbrt((double)kMaxPreviewPoints);
    if (axisTarget < 2) axisTarget = 2;
    int stride = (s + axisTarget - 1) / axisTarget;
    if (stride < 1) stride = 1;
    const float denom = (s > 1) ? (float)(s - 1) : 1.f;
    auto clamp01 = [](double v) { return (float)(v < 0 ? 0 : (v > 1 ? 1 : v)); };
    for (int x = 0; x < s; x += stride) {
        for (int y = 0; y < s; y += stride) {
            for (int z = 0; z < s; z += stride) {
                const Vec3D& c = lut.cube[x][y][z];
                d.points3D.push_back(x / denom);
                d.points3D.push_back(y / denom);
                d.points3D.push_back(z / denom);
                d.points3D.push_back(clamp01(c.x));
                d.points3D.push_back(clamp01(c.y));
                d.points3D.push_back(clamp01(c.z));
            }
        }
    }
    d.point3DCount = (int)(d.points3D.size() / 6);
    return d;
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.
(If `CubeLUT::JEFECHECK1D` isn't accessible as a scoped name, the enum is a plain `enum LUTTYPES` inside `CubeLUT` — `CubeLUT::JEFECHECK1D` is correct. If the compiler complains the `cube` indexing is out of range for a degenerate LUT, the `lut.size <= 0` guard already prevents it.)

- [ ] **Step 4: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "qt bridge: getLutPreview — Qt-safe LUT sample snapshot

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Preview widget — container + 1D curve

Create `LUTPreview_qt.{h,cpp}` with the container (`LUTPreview_Qt`: info labels + `QStackedWidget`) and the 1D `LutCurveWidget`. The 3D page is added in Task 3 (here the stack has the curve page + an empty placeholder).

**Files:** Create `src/qt/LUTPreview_qt.h`, `src/qt/LUTPreview_qt.cpp`

- [ ] **Step 1: Header**

Create `src/qt/LUTPreview_qt.h`:

```cpp
// In-panel LUT preview: a 2D curve for 1D LUTs (QPainter) and an
// auto-spinning point cloud for 3D LUTs (QOpenGLWidget), swapped by type,
// plus an info subpanel. Fed Qt-safe sample data via jefe::qt::getLutPreview
// so it never includes trilerp.h/glad (TU separation, developer_notes §1).
#ifndef JEFECHECK_QT_LUT_PREVIEW_H
#define JEFECHECK_QT_LUT_PREVIEW_H

#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <vector>

#include "SequenceLoadBridge_qt.h"   // jefe::qt::LutPreviewData

class QLabel;
class QStackedWidget;
class QPaintEvent;
class QTimer;
class QMouseEvent;
class QShowEvent;
class QHideEvent;

// 1D LUT curve (input → output) drawn with QPainter.
class LutCurveWidget : public QWidget {
    Q_OBJECT
public:
    explicit LutCurveWidget(QWidget* parent = nullptr);
    void setCurve(const std::vector<float>& samples, float maxValue);
protected:
    void paintEvent(QPaintEvent* e) override;
private:
    std::vector<float> samples_;
    float maxValue_ = 1.0f;
};

// 3D LUT point cloud (auto-spin, drag to nudge) in its own GL 2.1 context.
class LutCloudWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit LutCloudWidget(QWidget* parent = nullptr);
    void setPoints(const std::vector<float>& flatXYZRGB, int count);
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
private:
    std::vector<float> pts_;   // [x,y,z,r,g,b]...
    int   count_ = 0;
    float yaw_ = 30.f, pitch_ = 20.f;
    int   lastX_ = 0, lastY_ = 0;
    QTimer* spin_ = nullptr;
};

// Container: info labels + a QStackedWidget that swaps curve/cloud/empty.
class LUTPreview_Qt : public QWidget {
    Q_OBJECT
public:
    explicit LUTPreview_Qt(QWidget* parent = nullptr);
    void setLut(const jefe::qt::LutPreviewData& d);
private:
    QLabel* nameLbl_  = nullptr;
    QLabel* typeLbl_  = nullptr;
    QLabel* sizeLbl_  = nullptr;
    QLabel* depthLbl_ = nullptr;
    QLabel* maxLbl_   = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget*         emptyPage_ = nullptr;
    LutCurveWidget*  curve_ = nullptr;
    LutCloudWidget*  cloud_ = nullptr;
};

#endif
```

- [ ] **Step 2: Implementation — curve + container (cpp)**

Create `src/qt/LUTPreview_qt.cpp` with the includes, `LutCurveWidget`, and `LUTPreview_Qt`. (The `LutCloudWidget` methods are added in Task 3; to keep this file compiling now, include a minimal stub for the cloud — see Step 3.)

```cpp
#include "LUTPreview_qt.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QString>

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

    const int pad = 10;
    const QRect plot = r.adjusted(pad, pad, -pad, -pad);
    // Grid + axes.
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
    // Curve: x = input index across width, y = output / maxValue (inverted).
    p.setPen(QPen(QColor(0xd4, 0x77, 0x1e), 1.5));
    const int n = (int)samples_.size();
    QPointF prev;
    for (int i = 0; i < n; ++i) {
        const float fx = (float)i / (n - 1);
        float fy = samples_[i] / maxValue_;
        if (fy < 0) fy = 0; if (fy > 1) fy = 1;
        const QPointF cur(plot.left() + fx * plot.width(),
                          plot.bottom() - fy * plot.height());
        if (i > 0) p.drawLine(prev, cur);
        prev = cur;
    }
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
```

- [ ] **Step 3: Temporary cloud stub (so it links now)**

Append a minimal `LutCloudWidget` to `LUTPreview_qt.cpp` so the file compiles/links before Task 3 fills it in:

```cpp
// ---- LutCloudWidget (filled in Task 3) ----
#include <QTimer>
LutCloudWidget::LutCloudWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setObjectName("lut.preview.cloud");
    setAccessibleName("LUT cloud preview");
    setMinimumHeight(120);
}
void LutCloudWidget::setPoints(const std::vector<float>& p, int c) { pts_ = p; count_ = c; update(); }
void LutCloudWidget::initializeGL() { initializeOpenGLFunctions(); }
void LutCloudWidget::resizeGL(int, int) {}
void LutCloudWidget::paintGL() {}
void LutCloudWidget::showEvent(QShowEvent*) {}
void LutCloudWidget::hideEvent(QHideEvent*) {}
void LutCloudWidget::mousePressEvent(QMouseEvent*) {}
void LutCloudWidget::mouseMoveEvent(QMouseEvent*) {}
```

- [ ] **Step 4: Reconfigure + build**

Run: `cmake -B build_qt` (re-glob to pick up the new file), then `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 5: Commit**

```bash
git add src/qt/LUTPreview_qt.h src/qt/LUTPreview_qt.cpp
git commit -m "qt: LUT preview widget — container + 1D curve (cloud stubbed)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: 3D point-cloud cloud widget

Replace the stub `LutCloudWidget` methods with the real auto-spinning immediate-mode `GL_POINTS` renderer.

**Files:** Modify `src/qt/LUTPreview_qt.cpp`

- [ ] **Step 1: Add GL + event includes**

At the top of `src/qt/LUTPreview_qt.cpp`, add (alongside the existing includes):

```cpp
#include <QTimer>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>
```

(Remove the duplicate `#include <QTimer>` that the Task-2 stub added.)

- [ ] **Step 2: Replace the stub cloud methods**

Replace the entire `// ---- LutCloudWidget (filled in Task 3) ----` block from Task 2 with:

```cpp
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
    glClearColor(0.08f, 0.08f, 0.08f, 1.f);
    glEnable(GL_DEPTH_TEST);
    glPointSize(3.0f);
}

void LutCloudWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    // Manual perspective frustum (no glu — cross-platform). 2.1 fixed-function.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = (h > 0) ? (double)w / h : 1.0;
    const double n = 0.5, f = 10.0, top = n * 0.55, right = top * aspect;
    glFrustum(-right, right, -top, top, n, f);
    glMatrixMode(GL_MODELVIEW);
}

void LutCloudWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.f, 0.f, -2.2f);     // pull the cube back into the frustum
    glRotatef(pitch_, 1.f, 0.f, 0.f);
    glRotatef(yaw_,   0.f, 1.f, 0.f);
    glTranslatef(-0.5f, -0.5f, -0.5f); // center the unit cube on the origin

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
```

> Note: `glBegin/glEnd/glVertex3f/glColor3f/glFrustum/glRotatef/glTranslatef/glMatrixMode/glLoadIdentity` are fixed-function/immediate-mode calls valid under the app's GL 2.1 NoProfile context (`main_qt.cpp`). They resolve against the system GL the `QOpenGLWidget` context uses — **not** glad. `QOpenGLFunctions` supplies `glClear/glEnable/glViewport/glPointSize/glClearColor/glPointSize` via the inherited interface.

- [ ] **Step 3: Reconfigure-safe build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.
If `glFrustum`/`glBegin` are undeclared, add the platform GL header near the top of the file:
```cpp
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
```

- [ ] **Step 4: Commit**

```bash
git add src/qt/LUTPreview_qt.cpp
git commit -m "qt: LUT preview 3D point cloud (auto-spin, drag-nudge, GL_POINTS)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Integrate into the LUT panel

Add the toggle + preview pane to `LUTPanel_Qt` and wire selection.

**Files:** Modify `src/qt/FXLutPanel_qt.h`, `src/qt/FXLutPanel_qt.cpp`

- [ ] **Step 1: Header members + include**

In `src/qt/FXLutPanel_qt.h`, add the forward decls / members to `LUTPanel_Qt`. Add near the other forward declarations:
```cpp
class QCheckBox;
class LUTPreview_Qt;
```
In `LUTPanel_Qt`'s `private:` section add:
```cpp
    void updatePreview();
    QCheckBox*     previewToggle_ = nullptr;
    LUTPreview_Qt* preview_       = nullptr;
```

- [ ] **Step 2: Construct toggle + pane (cpp)**

In `src/qt/FXLutPanel_qt.cpp`, add includes:
```cpp
#include <QCheckBox>
#include "LUTPreview_qt.h"
```
In the `LUTPanel_Qt` constructor, after the button `row` is built and before `auto* outer = new QVBoxLayout(this);`, create the toggle + preview:
```cpp
    previewToggle_ = new QCheckBox("Preview", this);
    previewToggle_->setChecked(true);
    previewToggle_->setObjectName("lut.preview.toggle");
    previewToggle_->setAccessibleName("Show LUT preview");

    preview_ = new LUTPreview_Qt(this);
```
Then extend the `outer` layout (after `outer->addLayout(row);`) with:
```cpp
    outer->addWidget(previewToggle_);
    outer->addWidget(preview_, /*stretch*/ 1);
```
Wire the toggle and selection (after `refreshList();` at the end of the ctor, add):
```cpp
    connect(previewToggle_, &QCheckBox::toggled, this, [this](bool on) {
        preview_->setVisible(on);
        if (on) updatePreview();
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        updatePreview();
    });
    updatePreview();
```

- [ ] **Step 3: updatePreview + refresh hook**

Add the method to `src/qt/FXLutPanel_qt.cpp`:
```cpp
void LUTPanel_Qt::updatePreview() {
    if (!preview_ || !previewToggle_ || !previewToggle_->isChecked()) return;
    const int row = list_->currentRow();
    preview_->setLut(jefe::qt::getLutPreview(row));
}
```
And at the end of `LUTPanel_Qt::refreshList()`, add a refresh so a newly-dropped LUT updates the preview:
```cpp
    updatePreview();
```
(Place it after the existing pre-select logic in `refreshList`. Guard is inside `updatePreview`, so it's safe even before `preview_` exists — but `refreshList()` is also called in the ctor *before* `preview_` is constructed; `updatePreview` null-checks `preview_`, so this is safe.)

- [ ] **Step 4: Build + manual verify**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`. Launch, open the LUT dock. Expected:
- A "Preview" checkbox (checked) with an info subpanel + canvas below the LUT list.
- Select a **1D LUT** → curve renders with axes/grid; info shows "Type: 1D", sample count, depth.
- Select a **3D LUT** (.cube/.cub) → a point cloud auto-spins; drag nudges it; info shows "Type: 3D cube", `N³`.
- Select "(No LUT)" → preview clears (empty page, "—" labels).
- Uncheck "Preview" → pane hides; re-check → returns and updates.

- [ ] **Step 5: Commit**

```bash
git add src/qt/FXLutPanel_qt.h src/qt/FXLutPanel_qt.cpp
git commit -m "qt: wire LUT preview pane + toggle into the LUT panel

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Locator + smoke test + docs

**Files:** Modify `tests/ui/jefecheck/locators.py`, create `tests/ui/test_lut_preview.py`, modify `developer_notes.md`

- [ ] **Step 1: Locators**

In `tests/ui/jefecheck/locators.py`, add near other panel locators:
```python
LUT_PANEL = "lut.panel"
LUT_PREVIEW = "lut.preview"
LUT_PREVIEW_TOGGLE = "lut.preview.toggle"
```

- [ ] **Step 2: Smoke test**

Create `tests/ui/test_lut_preview.py`:
```python
"""LUT preview pane presence.

The preview canvases (1D curve / 3D cloud) are painted/GL widgets whose
content isn't AX-addressable, so the smoke test asserts the container and
toggle resolve. Curve/cloud rendering is verified manually per the plan.
"""
from jefecheck import locators


def test_lut_preview_toggle_present(app):
    """The LUT preview toggle is present and addressable."""
    toggle = app.by_object_name(locators.LUT_PREVIEW_TOGGLE)
    assert toggle is not None


def test_lut_preview_pane_present(app):
    """The LUT preview container is present and addressable."""
    pane = app.by_object_name(locators.LUT_PREVIEW)
    assert pane is not None
```

- [ ] **Step 3: Validate parse**

Run: `python3 -c "import ast; ast.parse(open('tests/ui/test_lut_preview.py').read()); ast.parse(open('tests/ui/jefecheck/locators.py').read()); print('parse OK')"`
Expected: `parse OK`.

- [ ] **Step 4: Dev notes**

In `developer_notes.md`, add a section (after §15) covering: the LUT preview is a panel widget (`LUTPreview_Qt`) that reads sample data only via `jefe::qt::getLutPreview` (Qt-safe `LutPreviewData`; no `trilerp.h`/glad in the widget — TU separation); 1D → `LutCurveWidget` (QPainter), 3D → `LutCloudWidget` (a second `QOpenGLWidget` with its own GL 2.1 context using immediate-mode `GL_POINTS`, auto-spin gated on visibility); 3D points subsampled to ≤20k; the FLTK fullscreen `gfcPlateManager::showLutPreview` path is intentionally left unused.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/jefecheck/locators.py tests/ui/test_lut_preview.py developer_notes.md
git commit -m "qt: LUT preview locators + smoke test + developer notes

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final review (after all tasks)

- Build green; all task commits present.
- Manual pass of Task 4 Step 4 (1D curve, 3D auto-spin + drag, info labels, toggle, no-LUT clear).
- Confirm idle cost: with the pane hidden (toggle off) the spin timer is stopped.
- Open a PR against `qt-experimental`; squash-merge after review.
