// In-panel LUT preview: a 2D curve for 1D LUTs (QPainter) and an interactive
// 3D cube inspector for 3D LUTs (QOpenGLWidget). Fed Qt-safe sample data via
// jefe::qt::getLutPreview so it never includes trilerp.h/glad (TU separation,
// developer_notes §1).
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
class QWheelEvent;
class QKeyEvent;
class QShowEvent;
class QHideEvent;
class QCheckBox;
class QComboBox;
class QSlider;

// 1D LUT curve (input -> output) drawn with QPainter.
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

// 3D LUT cube inspector: structured cube grid rendered as composable
// solid faces / grid lattice / dots, with a Maya-style orbit/pan/dolly
// camera, uniform-vs-deformed placement, and a background-brightness control.
class LutCloudWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit LutCloudWidget(QWidget* parent = nullptr);
    void setCube(const std::vector<float>& cubeRGB, int cubeSize);

    enum Marker { MarkerDot = 0, MarkerCube = 1, MarkerTetra = 2 };

public slots:
    void setShowSolid(bool on);
    void setShowGrid(bool on);
    void setShowDots(bool on);
    void setMarker(int marker);       // Marker enum
    void setDeformed(bool on);
    void setAutoSpin(bool on);
    void setBackground(int gray0to100);
    void focusCube();                 // reset camera to frame the cube

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void applyCamera();               // load projection+modelview from camera
    void nodePos(int ix, int iy, int iz, float out[3]) const;
    const float* nodeColor(int ix, int iy, int iz) const;
    void drawAxes();
    void drawDots();
    void drawGrid();
    void drawSolid();
    void drawAxisLabels(QPainter& p, const float* proj, const float* mv);

    std::vector<float> cube_;   // cubeSize^3 * 3 (output RGB per node)
    int   cs_ = 0;              // cube edge

    // Render modes (composable).
    bool showSolid_ = false;
    bool showGrid_  = false;
    bool showDots_  = true;
    int  marker_    = MarkerDot;
    // Deform morph: nodePos lerps uniform↔output-color by deformAmt_, which
    // eases toward deformTarget_ (0 or 1) over a short animation.
    float deformAmt_    = 0.f;
    float deformTarget_ = 0.f;
    QTimer* morph_ = nullptr;

    // Camera (orbit around target).
    float yaw_ = 35.f, pitch_ = 22.f, dist_ = 2.6f;
    float target_[3] = {0.5f, 0.5f, 0.5f};
    float bg_ = 0.5f;          // background gray 0..1

    int   lastX_ = 0, lastY_ = 0;
    bool  autoSpin_ = false;
    QTimer* spin_ = nullptr;
};

// Container: info labels + a controls strip (3D only) + a QStackedWidget
// swapping curve / cloud / empty.
class QBoxLayout;
class QResizeEvent;

class LUTPreview_Qt : public QWidget {
    Q_OBJECT
public:
    explicit LUTPreview_Qt(QWidget* parent = nullptr);
    void setLut(const jefe::qt::LutPreviewData& d);
protected:
    void resizeEvent(QResizeEvent* e) override;
private:
    QBoxLayout* mainBox_ = nullptr;   // flips H/V by aspect
    QWidget*    side_ = nullptr;       // info + controls section
    QLabel* nameLbl_  = nullptr;
    QLabel* typeLbl_  = nullptr;
    QLabel* sizeLbl_  = nullptr;
    QLabel* depthLbl_ = nullptr;
    QLabel* maxLbl_   = nullptr;
    QWidget*        controls_ = nullptr;   // 3D-only control strip
    QStackedWidget* stack_ = nullptr;
    QWidget*         emptyPage_ = nullptr;
    LutCurveWidget*  curve_ = nullptr;
    LutCloudWidget*  cloud_ = nullptr;
};

#endif
