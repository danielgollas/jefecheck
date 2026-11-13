// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcPlateLayout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace jefe { namespace platelayout {

std::vector<int> visiblePlates(int framingMode) {
    switch (framingMode) {
        case FRAMINGDOUBLE_ID:
        case FRAMINGDOUBLEVERT_ID: return {0, 1};
        case FRAMINGQUAD_ID:       return {0, 1, 2, 3};
        default:                   return {0};  // single, stereo, overlay
    }
}

std::vector<int> platesShown(PlateMode mode, int framingMode, int activePlate) {
    switch (mode) {
        case PlateMode::Active:  return {activePlate};
        case PlateMode::All:     return {0, 1, 2, 3};
        case PlateMode::Visible:
        default:                 return visiblePlates(framingMode);
    }
}

std::vector<GridPos> arrange(PlateMode mode, int framingMode, int activePlate,
                             DockShape shape, int maxCols) {
    const std::vector<int> shown = platesShown(mode, framingMode, activePlate);
    std::vector<GridPos> out;
    if (maxCols < 1) maxCols = 1;
    const int n = (int)shown.size();
    for (int i = 0; i < n; ++i) {
        GridPos g{shown[i], 0, 0};
        switch (shape) {
            case DockShape::Column:
                g.row = i; g.col = 0; break;
            case DockShape::Row:
                g.row = i / maxCols; g.col = i % maxCols; break;
            case DockShape::Floating:
            default:
                if (mode == PlateMode::Active) { g.row = 0; g.col = 0; }
                else if (mode == PlateMode::All) { g.row = i / 2; g.col = i % 2; }
                else if (framingMode == FRAMINGDOUBLEVERT_ID) { g.row = i; g.col = 0; }
                else { g.row = i / 2; g.col = i % 2; }
                break;
        }
        out.push_back(g);
    }
    return out;
}

std::vector<Cell> cellsFor(int framingMode, int w, int h) {
    const int hw = w / 2, hh = h / 2;
    switch (framingMode) {
        case FRAMINGDOUBLE_ID:
            return {{0, 0, 0, hw, h}, {1, hw, 0, w - hw, h}};
        case FRAMINGDOUBLEVERT_ID:
            return {{0, 0, 0, w, hh}, {1, 0, hh, w, h - hh}};
        case FRAMINGQUAD_ID:
            return {{0, 0, 0, hw, hh}, {1, hw, 0, w - hw, hh},
                    {2, 0, hh, hw, h - hh}, {3, hw, hh, w - hw, h - hh}};
        default:
            return {{0, 0, 0, w, h}};
    }
}

int plateAtPoint(int framingMode, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h) return -1;
    for (const Cell& c : cellsFor(framingMode, w, h))
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return c.plate;
    return -1;
}

bool showActiveFrame(int framingMode) { return visiblePlates(framingMode).size() > 1; }

Cell activeFrameRectGL(int framingMode, int activePlate, int w, int h) {
    for (const Cell& c : cellsFor(framingMode, w, h))
        if (c.plate == activePlate) return {c.plate, c.x, h - (c.y + c.h), c.w, c.h};
    return {-1, 0, 0, 0, 0};
}

int rowMaxCols(int budgetPx, int cardW, int gap) {
    if (cardW + gap <= 0) return 1;
    return std::clamp((budgetPx + gap) / (cardW + gap), 1, 4);
}

int activeFrameThicknessPx(float dpiScale) {
    return std::max(1, (int)std::lround(dpiScale));
}

// ---------------------------------------------------------------- self-test
namespace {
using V_ = std::vector<int>;
using G_ = std::vector<GridPos>;
using C_ = std::vector<Cell>;
int g_fail = 0;
void expect(bool ok, const char* name) {
    if (!ok) { ++g_fail; std::printf("PLATELAYOUT-TEST %s: FAIL\n", name); }
}
#define EXPECT(...) expect((__VA_ARGS__), #__VA_ARGS__)
}

bool runPlateLayoutSelfTest() {
    g_fail = 0;
    const int S = FRAMINGSINGLE_ID, D = FRAMINGDOUBLE_ID, V = FRAMINGDOUBLEVERT_ID, Q = FRAMINGQUAD_ID;
    using PM = PlateMode; using DS = DockShape;
    EXPECT(visiblePlates(S) == V_{0});           EXPECT(visiblePlates(D) == V_{0,1});
    EXPECT(visiblePlates(V) == V_{0,1});         EXPECT(visiblePlates(Q) == V_{0,1,2,3});
    EXPECT(visiblePlates(FRAMINGSTEREO_ID) == V_{0});
    EXPECT(platesShown(PM::Active, Q, 2) == V_{2});
    EXPECT(platesShown(PM::Visible, D, 0) == V_{0,1});
    EXPECT(platesShown(PM::All, S, 0) == V_{0,1,2,3});
    EXPECT(arrange(PM::Visible, D, 0, DS::Floating, 4) == (G_{{0,0,0},{1,0,1}}));
    EXPECT(arrange(PM::Visible, V, 0, DS::Floating, 4) == (G_{{0,0,0},{1,1,0}}));
    EXPECT(arrange(PM::Visible, Q, 0, DS::Floating, 4) == (G_{{0,0,0},{1,0,1},{2,1,0},{3,1,1}}));
    EXPECT(arrange(PM::All,     S, 0, DS::Floating, 4) == (G_{{0,0,0},{1,0,1},{2,1,0},{3,1,1}}));
    EXPECT(arrange(PM::Active,  Q, 3, DS::Floating, 4) == (G_{{3,0,0}}));
    EXPECT(arrange(PM::All, S, 0, DS::Row, 4) == (G_{{0,0,0},{1,0,1},{2,0,2},{3,0,3}}));
    EXPECT(arrange(PM::All, S, 0, DS::Row, 2) == (G_{{0,0,0},{1,0,1},{2,1,0},{3,1,1}}));
    EXPECT(arrange(PM::All, S, 0, DS::Row, 0) == (G_{{0,0,0},{1,1,0},{2,2,0},{3,3,0}}));
    EXPECT(arrange(PM::Visible, Q, 0, DS::Column, 4) == (G_{{0,0,0},{1,1,0},{2,2,0},{3,3,0}}));
    EXPECT(cellsFor(Q, 44, 24) == (C_{{0,0,0,22,12},{1,22,0,22,12},{2,0,12,22,12},{3,22,12,22,12}}));
    EXPECT(cellsFor(D, 45, 24) == (C_{{0,0,0,22,24},{1,22,0,23,24}}));
    EXPECT(cellsFor(V, 44, 24) == (C_{{0,0,0,44,12},{1,0,12,44,12}}));
    EXPECT(cellsFor(S, 44, 24) == (C_{{0,0,0,44,24}}));
    EXPECT(plateAtPoint(Q, 30, 5, 44, 24) == 1);  EXPECT(plateAtPoint(Q, 5, 20, 44, 24) == 2);
    EXPECT(plateAtPoint(V, 5, 20, 44, 24) == 1);  EXPECT(plateAtPoint(Q, -1, 0, 44, 24) == -1);
    EXPECT(plateAtPoint(Q, 44, 0, 44, 24) == -1);
    EXPECT(!showActiveFrame(S));  EXPECT(showActiveFrame(D));  EXPECT(showActiveFrame(Q));
    EXPECT(activeFrameRectGL(Q, 0, 800, 600) == (Cell{0, 0, 300, 400, 300}));
    EXPECT(activeFrameRectGL(V, 0, 800, 600) == (Cell{0, 0, 300, 800, 300}));
    EXPECT(activeFrameRectGL(D, 1, 800, 600) == (Cell{1, 400, 0, 400, 600}));
    EXPECT(activeFrameRectGL(D, 3, 800, 600).plate == -1);
    EXPECT(rowMaxCols(1000, 480, 8) == 2);  EXPECT(rowMaxCols(4000, 480, 8) == 4);
    EXPECT(rowMaxCols(100, 480, 8) == 1);
    EXPECT(activeFrameThicknessPx(1.0f) == 1);  EXPECT(activeFrameThicknessPx(1.5f) == 2);
    EXPECT(activeFrameThicknessPx(2.0f) == 2);
    std::printf(g_fail ? "PLATELAYOUT-TEST FAIL\n" : "PLATELAYOUT-TEST PASS\n");
    std::fflush(stdout);  // caller leaves via _Exit, which skips stdio flush
    return g_fail == 0;
}

}} // namespace jefe::platelayout
