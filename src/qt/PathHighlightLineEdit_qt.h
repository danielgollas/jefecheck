// QLineEdit subclass that overlays a colored underlay on a configurable
// character range. The TrackStrip uses it to highlight the digit portion
// of a sequence filename that the loader identified as the frame number,
// without mutating the QLineEdit's text() content (the loader still sees
// a literal file path).
//
// Display-only: setHighlightRange records the range, paintEvent draws a
// soft-yellow rectangle behind the matching glyph cells using the same
// font metrics QLineEdit uses to render its text. While the line edit
// has keyboard focus the highlight is suppressed so editing isn't
// visually noisy.
#ifndef JEFECHECK_QT_PATH_HIGHLIGHT_LINE_EDIT_H
#define JEFECHECK_QT_PATH_HIGHLIGHT_LINE_EDIT_H

#include <QLineEdit>

class PathHighlightLineEdit_Qt : public QLineEdit {
    Q_OBJECT
public:
    using QLineEdit::QLineEdit;

    // Mark characters [start, start+length) for highlight. The caller is
    // responsible for clearing/refreshing the range whenever text()
    // changes — the widget doesn't try to track edits.
    void setHighlightRange(int start, int length);
    void clearHighlight();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int highlightStart_ = -1;
    int highlightLength_ = 0;
};

#endif
