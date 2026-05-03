// "Host System Specs" — read-only summary of the GL state captured at
// startup. Mirrors the FLTK minSpecsWindow's content but as a modal
// QDialog with a Close button. Triggered from Help → System Specs.
#ifndef JEFECHECK_QT_MINSPECS_DIALOG_H
#define JEFECHECK_QT_MINSPECS_DIALOG_H

#include <QDialog>

class MinSpecsDialog_Qt : public QDialog {
    Q_OBJECT
public:
    explicit MinSpecsDialog_Qt(QWidget* parent = nullptr);
};

#endif
