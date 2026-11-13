// SPDX-License-Identifier: GPL-2.0-or-later
#include "UiDensityCheck_qt.h"

#include "UiMetrics_qt.h"
#include "FlowLayout_qt.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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
           qobject_cast<const QLineEdit*>(w) || qobject_cast<const QTextEdit*>(w) ||
           qobject_cast<const QPlainTextEdit*>(w);
}

// The widgets an item view hosts through setIndexWidget/setItemWidget. Its own
// internals (scroll bars, header, delegate editors) are not index widgets.
void collectIndexWidgets(const QAbstractItemView* v, const QModelIndex& parent,
                         QList<const QWidget*>& out, int& budget) {
    const QAbstractItemModel* m = v->model();
    if (!m) return;
    for (int r = 0; r < m->rowCount(parent); ++r)
        for (int c = 0; c < m->columnCount(parent); ++c) {
            if (--budget < 0) return;
            const QModelIndex idx = m->index(r, c, parent);
            if (const QWidget* iw = v->indexWidget(idx)) out.append(iw);
            if (c == 0 && m->hasChildren(idx)) collectIndexWidgets(v, idx, out, budget);
        }
}

// valign: controls on one line (a QHBoxLayout, or one line of a FlowLayout)
// share a vertical centre. Compared in doubled pixels so a 1 px rounding passes.
void checkValign(const QLayout* l, const QWidget* root, QList<DensityViolation>& out) {
    auto centre2 = [](const QWidget* w) { return 2 * w->y() + w->height(); };
    auto checkLine = [&](const QList<const QWidget*>& line) {
        if (line.size() < 2) return;
        const QWidget* ref = line.first();
        for (const QWidget* w : line) if (w->height() > ref->height()) ref = w;
        for (const QWidget* w : line)
            if (std::abs(centre2(w) - centre2(ref)) > 2)
                out.append({pathOf(w, root), "valign",
                            QString("centre y %1 vs %2 for %3 on the same line")
                                .arg(centre2(w) / 2.0).arg(centre2(ref) / 2.0).arg(pathOf(ref, root))});
    };
    QList<const QWidget*> items;
    for (int i = 0; i < l->count(); ++i) {
        QLayoutItem* it = l->itemAt(i);
        if (!it) continue;
        if (const QLayout* sub = it->layout()) { checkValign(sub, root, out); continue; }
        const QWidget* w = it->widget();
        if (!w || w->isWindow() || !w->isVisibleTo(root) || isExempt(w)) continue;
        if (w->height() > kControlH * 3 / 2) continue;   // multi-line labels, lists, canvases
        items.append(w);
    }
    if (dynamic_cast<const FlowLayout_Qt*>(l)) {
        // Lines are the runs of items whose vertical extents overlap.
        std::sort(items.begin(), items.end(), [](const QWidget* a, const QWidget* b) { return a->y() < b->y(); });
        QList<const QWidget*> line;
        int lineBottom = 0;
        for (const QWidget* w : items) {
            if (!line.isEmpty() && w->y() >= lineBottom) { checkLine(line); line.clear(); }
            if (line.isEmpty()) lineBottom = w->y() + w->height();
            else lineBottom = std::max(lineBottom, w->y() + w->height());
            line.append(w);
        }
        checkLine(line);
    } else if (const auto* box = qobject_cast<const QBoxLayout*>(l)) {
        if (box->direction() == QBoxLayout::LeftToRight || box->direction() == QBoxLayout::RightToLeft)
            checkLine(items);
    }
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

    // hclip: text that needs more width than the widget has.
    if (const auto* pb = qobject_cast<const QPushButton*>(w)) {
        if (!pb->text().isEmpty() && pb->sizeHint().width() > pb->width())
            add("hclip", QString("button needs %1 px, has %2").arg(pb->sizeHint().width()).arg(pb->width()));
    } else if (const auto* tb = qobject_cast<const QToolButton*>(w)) {
        if (!tb->text().isEmpty() && tb->toolButtonStyle() != Qt::ToolButtonIconOnly &&
            tb->sizeHint().width() > tb->width())
            add("hclip", QString("button needs %1 px, has %2").arg(tb->sizeHint().width()).arg(tb->width()));
    } else if (const auto* cb = qobject_cast<const QComboBox*>(w)) {
        // A combo that shrinks on purpose (min-contents sizing, or an elided owner) may cut its text.
        if (cb->sizeAdjustPolicy() != QComboBox::AdjustToMinimumContentsLengthWithIcon &&
            !cb->property(kDensityElidedProperty).toBool() && cb->sizeHint().width() > cb->width())
            add("hclip", QString("combo needs %1 px, has %2").arg(cb->sizeHint().width()).arg(cb->width()));
    } else if (const auto* sb = qobject_cast<const QAbstractSpinBox*>(w)) {
        // sizeHint covers the widest value of the range (plus prefix/suffix and steppers).
        if (sb->sizeHint().width() > sb->width())
            add("hclip", QString("spin box needs %1 px, has %2").arg(sb->sizeHint().width()).arg(sb->width()));
    } else if (qobject_cast<const QCheckBox*>(w) || qobject_cast<const QRadioButton*>(w)) {
        const auto* ab = static_cast<const QAbstractButton*>(w);
        if (!ab->text().isEmpty() && ab->sizeHint().width() > ab->width())
            add("hclip", QString("check needs %1 px, has %2").arg(ab->sizeHint().width()).arg(ab->width()));
    } else if (const auto* lb = qobject_cast<const QLabel*>(w)) {
        const bool plain = lb->textFormat() == Qt::PlainText ||
                           (lb->textFormat() == Qt::AutoText && !Qt::mightBeRichText(lb->text()));
        if (plain && !lb->wordWrap() && !lb->text().isEmpty() && !lb->property(kDensityElidedProperty).toBool() &&
            lb->fontMetrics().horizontalAdvance(lb->text()) > lb->contentsRect().width())
            add("hclip", QString("label text %1 px > contents width %2")
                             .arg(lb->fontMetrics().horizontalAdvance(lb->text())).arg(lb->contentsRect().width()));
    }

    if (hasInternalChildren(w)) return;

    if (const auto* view = qobject_cast<const QAbstractItemView*>(w)) {
        QList<const QWidget*> hosted;
        int budget = 4096;
        collectIndexWidgets(view, view->rootIndex(), hosted, budget);
        // Rows scroll vertically, but a hosted widget wider than the viewport has its
        // right-hand controls cut off.
        const int vpW = view->viewport()->width();
        for (const QWidget* c : hosted) {
            if (!c->isVisibleTo(root) || isExempt(c)) continue;
            if (c->x() < 0 || c->x() + c->width() > vpW)
                out.append({pathOf(c, root), "clip",
                            QString("hosted widget x %1..%2 outside viewport width %3")
                                .arg(c->x()).arg(c->x() + c->width()).arg(vpW)});
            walk(c, root, out);
        }
        return;
    }

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

    if (w->layout()) checkValign(w->layout(), root, out);

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
    // The app stylesheet caps every QPushButton at 24 px; a deliberately tall fixture must opt out of it.
    const QString kTallButton = "QPushButton { min-height: 28px; max-height: 28px; }";

    {   // tall_button
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* b = new QPushButton("Go"); b->setFixedHeight(30); b->setStyleSheet(kTallButton); lay->addWidget(b);
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
    {   // scroll_tall_button: scroll-area contents are still checked
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* sa = new QScrollArea; sa->setWidgetResizable(true); lay->addWidget(sa);
        auto* inner = new QWidget; auto* il = new QVBoxLayout(inner);
        auto* b = new QPushButton("Go"); b->setFixedHeight(30); b->setStyleSheet(kTallButton); il->addWidget(b);
        sa->setWidget(inner);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("scroll_tall_button", v.size() == 1 && v[0].rule == "height");
    }
    {   // hclip_button
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* b = new QPushButton("A very long button caption"); b->setFixedWidth(60); lay->addWidget(b);
        settle(root);
        const auto v = checkDensity(&root);
        int n = 0; for (const auto& x : v) if (x.rule == "hclip") ++n;
        all &= report("hclip_button", n == 1);
    }
    {   // hclip_label, and the jefeElided opt-out
        for (bool elided : {false, true}) {
            QWidget root; root.setStyleSheet(base);
            auto* lay = new QVBoxLayout(&root);
            auto* l = new QLabel("A very long label text that cannot fit"); l->setFixedWidth(60);
            if (elided) l->setProperty(kDensityElidedProperty, true);
            lay->addWidget(l);
            settle(root);
            const auto v = checkDensity(&root);
            int n = 0; for (const auto& x : v) if (x.rule == "hclip") ++n;
            all &= report(elided ? "hclip_label_elided" : "hclip_label", n == (elided ? 0 : 1));
        }
    }
    auto count = [](const QList<DensityViolation>& v, const char* rule) {
        int n = 0; for (const auto& x : v) n += x.rule == rule; return n;
    };
    {   // clip: a child placed outside its parent
        QWidget root; root.setStyleSheet(base); root.resize(100, 60);
        auto* l = new QLabel("Hi", &root); l->setGeometry(80, 10, 60, 24);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("clip", v.size() == 1 && v[0].rule == "clip");
    }
    {   // textclip: a label shorter than its text
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* l = new QLabel("Hi"); l->setFixedHeight(8); lay->addWidget(l);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("textclip", v.size() == 1 && v[0].rule == "textclip");
    }
    {   // square_with_text: an icon button with a caption is not held to the square
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QHBoxLayout(&root);
        auto* b = new QPushButton("Add files"); b->setIcon(testIcon()); b->setFixedHeight(kControlH); lay->addWidget(b);
        settle(root);
        all &= report("square_with_text", checkDensity(&root).isEmpty());
    }
    {   // hclip_combo, and its two opt-outs
        enum Mode { Plain, MinContents, Elided };
        for (Mode m : {Plain, MinContents, Elided}) {
            QWidget root; root.setStyleSheet(base);
            auto* lay = new QVBoxLayout(&root);
            auto* c = new QComboBox; c->addItems({"Once", "A much longer combo entry"}); c->setFixedWidth(60);
            if (m == MinContents) { c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); c->setMinimumContentsLength(3); }
            if (m == Elided) c->setProperty(kDensityElidedProperty, true);
            lay->addWidget(c);
            settle(root);
            const char* name = m == Plain ? "hclip_combo" : m == MinContents ? "hclip_combo_mincontents" : "hclip_combo_elided";
            all &= report(name, count(checkDensity(&root), "hclip") == (m == Plain ? 1 : 0));
        }
    }
    {   // hclip_spin: too narrow for the widest value of its range
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* sp = new QSpinBox; sp->setRange(1, 99999999); sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        sp->setFixedWidth(40); lay->addWidget(sp);
        settle(root);
        all &= report("hclip_spin", count(checkDensity(&root), "hclip") == 1);
    }
    {   // hclip_check and hclip_radio
        for (bool radio : {false, true}) {
            QWidget root; root.setStyleSheet(base);
            auto* lay = new QVBoxLayout(&root);
            QAbstractButton* b = radio ? static_cast<QAbstractButton*>(new QRadioButton("A long radio caption"))
                                       : static_cast<QAbstractButton*>(new QCheckBox("A long checkbox caption"));
            b->setFixedWidth(40); lay->addWidget(b);
            settle(root);
            all &= report(radio ? "hclip_radio" : "hclip_check", count(checkDensity(&root), "hclip") == 1);
        }
    }
    {   // valign_hbox: one item of a row moved 4 px below the shared centre
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QHBoxLayout(&root);
        auto* a = new QPushButton("One"); auto* b = new QPushButton("Two");
        lay->addWidget(a); lay->addWidget(b);
        settle(root);
        b->move(b->x(), b->y() + 4);
        all &= report("valign_hbox", count(checkDensity(&root), "valign") == 1);
    }
    {   // valign_flow: a FlowLayout line centres a short item on the tall one
        QWidget root; root.setStyleSheet(base);
        auto* flow = new FlowLayout_Qt(&root, 0, kGap, kGapTight);
        auto* b = new QPushButton("Go"); b->setAttribute(Qt::WA_LayoutUsesWidgetRect);
        auto* l = new QLabel("Short"); l->setFixedHeight(16);
        flow->addWidget(b); flow->addWidget(l);
        root.resize(300, 40);
        settle(root);
        all &= report("valign_flow_centred", count(checkDensity(&root), "valign") == 0);
        l->move(l->x(), b->y());   // top-aligned, as an uncentred line would place it
        all &= report("valign_flow", count(checkDensity(&root), "valign") == 1);
    }
    {   // item_widget: an item view's hosted widgets are checked
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* list = new QListWidget; lay->addWidget(list);
        auto* card = new QWidget; auto* cl = new QHBoxLayout(card); cl->setContentsMargins(0, 0, 0, 0);
        auto* b = new QPushButton("Go"); b->setFixedHeight(30); b->setStyleSheet(kTallButton); cl->addWidget(b);
        auto* it = new QListWidgetItem(list); it->setSizeHint(QSize(0, 34));
        list->setItemWidget(it, card);
        settle(root);
        const auto v = checkDensity(&root);
        all &= report("item_widget", v.size() == 1 && v[0].rule == "height");
    }
    {   // item_widget_wide: a hosted widget wider than the viewport
        QWidget root; root.setStyleSheet(base);
        auto* lay = new QVBoxLayout(&root);
        auto* list = new QListWidget; list->setFixedWidth(200); lay->addWidget(list);
        auto* card = new QWidget;
        auto* it = new QListWidgetItem(list); it->setSizeHint(QSize(0, kControlH));
        list->setItemWidget(it, card);
        settle(root);
        card->setGeometry(0, 0, 400, kControlH);
        const auto v = checkDensity(&root);
        all &= report("item_widget_wide", v.size() == 1 && v[0].rule == "clip");
    }
    return all;
}

}  // namespace jefe::qt::ui
