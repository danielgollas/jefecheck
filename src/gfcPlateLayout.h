// SPDX-License-Identifier: GPL-2.0-or-later
// Pure (GL-free, Qt-free) plate layout model shared by the viewport hit-test,
// the Plate Manager mini-map and the plate-card arrangement.
#pragma once
#include <vector>
#include "UIConstants.h"

namespace jefe { namespace platelayout {

enum class PlateMode { Active = 0, Visible = 1, All = 2 };
enum class DockShape { Row, Column, Floating };

// Top-down pixel rectangle for a plate (or GL bottom-up from activeFrameRectGL).
struct Cell {
    int plate; int x, y, w, h;
    bool operator==(const Cell& o) const {
        return plate == o.plate && x == o.x && y == o.y && w == o.w && h == o.h;
    }
};
struct GridPos {
    int plate; int row; int col;
    bool operator==(const GridPos& o) const {
        return plate == o.plate && row == o.row && col == o.col;
    }
};

constexpr int kAccentR = 212, kAccentG = 119, kAccentB = 30;

std::vector<int> visiblePlates(int framingMode);
std::vector<int> platesShown(PlateMode mode, int framingMode, int activePlate);
std::vector<GridPos> arrange(PlateMode mode, int framingMode, int activePlate,
                             DockShape shape, int maxCols);
std::vector<Cell> cellsFor(int framingMode, int w, int h);
int plateAtPoint(int framingMode, int x, int y, int w, int h);
bool showActiveFrame(int framingMode);
Cell activeFrameRectGL(int framingMode, int activePlate, int w, int h);
int rowMaxCols(int budgetPx, int cardW, int gap);
int activeFrameThicknessPx(float dpiScale);

bool runPlateLayoutSelfTest();

}} // namespace jefe::platelayout
