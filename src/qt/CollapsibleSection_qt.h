// A reusable collapsible "accordion" section with a Nuke/Maya-style disclosure
// header: a full-width header row (▸/▾ triangle + title) that toggles a content
// area. Qt has no native accordion widget (QToolBox is single-open and dated;
// a checkable QGroupBox reads as a clunky checkbox), so pro DCC apps roll their
// own — this is that, as a small reusable widget.
//
// Usage:
//   auto* sec = new CollapsibleSection("Connection log", parent);
//   sec->setContentWidget(myTextEdit);
//   sec->setExpanded(false);
//
// Multiple sections collapse independently (unlike QToolBox). Style hooks:
// objectName "collapsible.header" (the QToolButton) and "collapsible.content".
#ifndef JEFECHECK_QT_COLLAPSIBLE_SECTION_H
#define JEFECHECK_QT_COLLAPSIBLE_SECTION_H

#include <QWidget>

class QToolButton;
class QVBoxLayout;

class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    // Place `w` inside the collapsible content area (reparents it). Replaces any
    // previous content.
    void setContentWidget(QWidget* w);

    void setExpanded(bool expanded);
    bool isExpanded() const;

    void setTitle(const QString& title);

signals:
    void toggled(bool expanded);

private:
    void applyArrow(bool expanded);

    QToolButton* header_ = nullptr;
    QWidget*     content_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    QWidget*     contentWidget_ = nullptr;
};

#endif
