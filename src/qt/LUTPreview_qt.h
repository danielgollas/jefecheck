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
