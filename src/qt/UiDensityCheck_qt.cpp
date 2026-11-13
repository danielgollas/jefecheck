// SPDX-License-Identifier: GPL-2.0-or-later
#include "UiDensityCheck_qt.h"

#include "UiMetrics_qt.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstdio>

namespace jefe::qt::ui {

namespace {

// The six control classes the 24 px rule applies to.
bool isControl(const QWidget* w) {
    return qobject_cast<const QPushButton*>(w) || qobject_cast<const QToolButton*>(w) ||
           qobject_cast<const QComboBox*>(w) || qobject_cast<const QLineEdit*>(w) ||
           qobject_cast<const QAbstractSpinBox*>(w);
}

bool isExempt(const QWidget* w) { return w->property(kDensityExemptProperty).toBool(); }

QString pathOf(const QWidget* w, const QWidget* root) {
    QStringList parts;
    for (const QWidget* p = w; p; p = p->parentWidget()) {
        parts.prepend(p->objectName().isEmpty() ? QString::fromLatin1(p->metaObject()->className())
                                                : p->objectName());
        if (p == root) break;
    }
    return parts.join('.');
}

void collectManaged(const QLayout* l, QSet<const QWidget*>& out) {
    for (int i = 0; i < l->count(); ++i) {
        QLayoutItem* it = l->itemAt(i);
        if (!it) continue;
        if (QWidget* w = it->widget()) out.insert(w);
        else if (QLayout* sub = it->layout()) collectManaged(sub, out);
    }
}

// Widgets whose children are Qt internals (the line edit inside a spin box,
// a combo's popup view) are checked as one unit, not walked.
bool hasInternalChildren(const QWidget* w) {
    return qobject_cast<const QAbstractSpinBox*>(w) || qobject_cast<const QComboBox*>(w) ||
           qobject_cast<const QLineEdit*>(w) || qobject_cast<const QAbstractScrollArea*>(w);
}

void walk(const QWidget* w, const QWidget* root, QList<DensityViolation>& out) {
    auto add = [&](const QString& rule, const QString& detail) {
        out.append({pathOf(w, root), rule, detail});
    };
    const_cast<QWidget*>(w)->ensurePolished();

    const bool control = isControl(w);
    const bool textual = control || qobject_cast<const QLabel*>(w) ||
                         qobject_cast<const QCheckBox*>(w) || qobject_cast<const QRadioButton*>(w);

    if (control && w->height() != kControlH)
        add("height", QString("height %1, want %2").arg(w->height()).arg(kControlH));

    if (const auto* b = qobject_cast<const QAbstractButton*>(w)) {
        if (b->text().isEmpty() && !b->icon().isNull() &&
            (b->width() != kControlH || b->height() != kControlH))
            add("square", QString("icon button %1x%2, want %3x%3")
                              .arg(b->width()).arg(b->height()).arg(kControlH));
    }

    if (textual) {
        const int px = w->font().pixelSize();
        if (px != kFontPx)
            add("font", px < 0 ? QString("point-size font (%1pt), want %2px")
                                     .arg(w->font().pointSizeF()).arg(kFontPx)
                               : QString("font %1px, want %2px").arg(px).arg(kFontPx));
        if (w->fontMetrics().height() > w->contentsRect().height())
            add("textclip", QString("text height %1 > contents height %2")
                                .arg(w->fontMetrics().height()).arg(w->contentsRect().height()));
    }

    if (hasInternalChildren(w)) return;

    // Children: collect the visible ones.
    QList<const QWidget*> kids;
    for (QObject* o : w->children()) {
        auto* c = qobject_cast<QWidget*>(o);
        if (!c || c->isWindow() || !c->isVisibleTo(root) || isExempt(c)) continue;
        kids.append(c);
    }

    const bool scrollViewport = w->parentWidget() &&
                                qobject_cast<const QAbstractScrollArea*>(w->parentWidget()) &&
                                static_cast<const QAbstractScrollArea*>(w->parentWidget())->viewport() == w;
    if (!scrollViewport) {
        for (const QWidget* c : kids)
            if (!w->rect().contains(c->geometry()))
                out.append({pathOf(c, root), "clip",
                            QString("geometry %1,%2 %3x%4 outside parent %5x%6")
                                .arg(c->x()).arg(c->y()).arg(c->width()).arg(c->height())
                                .arg(w->width()).arg(w->height())});
    }

    if (w->layout()) {
        QSet<const QWidget*> managed;
        collectManaged(w->layout(), managed);
        for (int i = 0; i < kids.size(); ++i)
            for (int j = i + 1; j < kids.size(); ++j) {
                if (!managed.contains(kids[i]) || !managed.contains(kids[j])) continue;
                if (kids[i]->geometry().intersects(kids[j]->geometry()))
                    out.append({pathOf(kids[j], root), "overlap",
                                QString("overlaps sibling %1").arg(pathOf(kids[i], root))});
            }
    }

    for (const QWidget* c : kids) walk(c, root, out);
}

// --- self-test --------------------------------------------------------------

QIcon testIcon() {
    QPixmap pm(16, 16);
    pm.fill(Qt::red);
    return QIcon(pm);
}

// Shows a parentless root off-screen and lets its layout settle.
void settle(QWidget& root) {
    root.setAttribute(Qt::WA_DontShowOnScreen, true);
    root.ensurePolished();
    root.show();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

bool report(const char* name, bool ok) {
    std::printf("DENSITY-SELFTEST %s %s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace

QList<DensityViolation> checkDensity(const QWidget* root) {
    QList<DensityViolation> out;
    if (!root || !root->isVisible() || isExempt(root)) return out;
    walk(root, root, out);
    return out;
}

bool runDensityCheckerSelfTest() {
    bool all = true;
    const QString base = "QWidget { font-size: 13px; }";

    {   // tall_button
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* b = new QPushButton("Go"); b->setFixedHeight(30); lay->addWidget(b);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("tall_button", v.size() == 1 && v[0].rule == "height");
    }
    {   // exact_button
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* b = new QPushButton("Go"); b->setFixedHeight(24); lay->addWidget(b);
        settle(root);
        all &= report("exact_button", checkDensity(&root).isEmpty());
    }
    {   // icon_not_square
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QHBoxLayout(&root);
        auto* b = new QPushButton; b->setIcon(testIcon()); b->setFixedSize(30, 24); lay->addWidget(b);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("icon_not_square", v.size() == 1 && v[0].rule == "square");
    }
    {   // point_font
        QWidget root;
        auto* lay = new QVBoxLayout(&root);
        auto* l = new QLabel("Text"); l->setStyleSheet("font-size:10pt;"); lay->addWidget(l);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("point_font", v.size() == 1 && v[0].rule == "font");
    }
    {   // overlap
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QHBoxLayout(&root);
        auto* a = new QLabel("First label"); auto* b = new QLabel("Second label");
        lay->addWidget(a); lay->addWidget(b);
        settle(root);
        b->setGeometry(a->geometry());
        int n = 0;
        for (const auto& v : checkDensity(&root)) n += v.rule == "overlap";
        all &= report("overlap", n >= 1);
    }
    {   // exempt
        QWidget root; root.setStyleSheet(base);
        root.setProperty(kDensityExemptProperty, true);
        auto* lay = new QVBoxLayout(&root);
        auto* b = new QPushButton("Go"); b->setFixedHeight(30); lay->addWidget(b);
        settle(root);
        all &= report("exempt", checkDensity(&root).isEmpty());
    }
    {   // hidden
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* b = new QPushButton("Go"); b->setFixedHeight(30); lay->addWidget(b);
        b->hide();
        settle(root);
        all &= report("hidden", checkDensity(&root).isEmpty());
    }
    return all;
}

}  // namespace jefe::qt::ui
