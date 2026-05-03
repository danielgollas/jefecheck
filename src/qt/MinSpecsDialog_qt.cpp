#include "MinSpecsDialog_qt.h"

#include "GLSystemInfo.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

namespace {

QCheckBox* check(const QString& label, bool on, const QString& objName) {
    auto* cb = new QCheckBox(label);
    cb->setObjectName(objName);
    cb->setChecked(on);
    // Display-only — clicking shouldn't toggle, since the underlying
    // GL state is fixed for this run. Keep them readable rather than
    // greyed out (Qt's disabled state is hard to read in the dark
    // theme), so we suppress focus + drop the toggle on click.
    cb->setFocusPolicy(Qt::NoFocus);
    cb->setAttribute(Qt::WA_TransparentForMouseEvents);
    return cb;
}

}  // namespace

MinSpecsDialog_Qt::MinSpecsDialog_Qt(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("JefeCheck Host System Specs"));
    setObjectName("dialog.minspecs");
    setAccessibleName("Host System Specs");

    const auto& info = jefe::qt::glSystemInfo();

    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel(tr("JefeCheck Video Card Check"));
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    root->addWidget(title);

    auto addLabel = [&](const QString& objName, const QString& text) {
        auto* l = new QLabel(text);
        l->setObjectName(objName);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        root->addWidget(l);
    };

    addLabel("dialog.minspecs.glversion",
             tr("GL Version: %1").arg(QString::fromStdString(info.version)));
    addLabel("dialog.minspecs.glvendor",
             tr("GL Vendor: %1").arg(QString::fromStdString(info.vendor)));
    addLabel("dialog.minspecs.glrenderer",
             tr("GL Renderer: %1").arg(QString::fromStdString(info.renderer)));
    addLabel("dialog.minspecs.maxtex",
             tr("Maximum Texture Size: %1×%1").arg(info.maxTextureSize));
    addLabel("dialog.minspecs.maxviewport",
             tr("Maximum Viewport Size: %1 × %2")
                 .arg(info.maxViewportX).arg(info.maxViewportY));

    auto addCheckGroup = [&](const QString& title) {
        auto* group = new QGroupBox(title);
        auto* layout = new QVBoxLayout(group);
        root->addWidget(group);
        return layout;
    };

    auto* basic = addCheckGroup(tr("Required for Basic Playback"));
    basic->addWidget(check(tr("Texture Rectangle"), info.textureRectangle,
                           "dialog.minspecs.texturerectangle"));

    auto* fx = addCheckGroup(tr("Required for FX Processing"));
    fx->addWidget(check(tr("Shader Objects"), info.shaderObjects,
                        "dialog.minspecs.shaderobjects"));
    fx->addWidget(check(tr("GL Shading Language"), info.glsl,
                        "dialog.minspecs.glsl"));
    fx->addWidget(check(tr("Frame Buffer Objects"), info.fbo,
                        "dialog.minspecs.fbo"));

    auto* hdr = addCheckGroup(tr("Required for HDR Processing"));
    hdr->addWidget(check(tr("Texture Float"), info.textureFloat,
                         "dialog.minspecs.texturefloat"));
    hdr->addWidget(check(tr("Texture Half"), info.textureHalf,
                         "dialog.minspecs.texturehalf"));

    auto* opt = addCheckGroup(tr("Optional Extensions"));
    opt->addWidget(check(tr("Pixel Buffer Objects"), info.pbo,
                         "dialog.minspecs.pbo"));
    opt->addWidget(check(tr("S3TC Texture Compression"), info.s3tc,
                         "dialog.minspecs.s3tc"));

    if (!info.captured) {
        auto* warn = new QLabel(tr(
            "<i>GL info not yet captured — open the main window first.</i>"));
        warn->setObjectName("dialog.minspecs.warning");
        root->addWidget(warn);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->setObjectName("dialog.minspecs.buttons");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}
