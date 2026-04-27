#include "FXLutPanel_qt.h"

#include <QLabel>
#include <QVBoxLayout>

FXStackPanel_Qt::FXStackPanel_Qt(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(new QLabel("FX Stack — placeholder", this));
    layout->addStretch(1);
}

LUTPanel_Qt::LUTPanel_Qt(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(new QLabel("LUTs — placeholder", this));
    layout->addStretch(1);
}
