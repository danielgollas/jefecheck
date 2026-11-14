// SPDX-License-Identifier: GPL-2.0-or-later
// GL-free model behind the FX panel's parameter widgets: widget styles read
// from .jfx attributes, soft (slider) ranges inside the hard limits, value
// mapping/scrubbing, and the grouping of widgets into panel rows.
#ifndef GFCFXUI_H
#define GFCFXUI_H

#include <string>
#include <vector>

namespace fxui {

// A span (hardMax - hardMin) above this gets a derived soft range.
constexpr float kHugeSpan = 100.f;
// Dragging this many pixels moves the value across the whole soft range.
constexpr float kScrubPixelsPerSpan = 200.f;
// Scrub speed multiplier when the fine modifier is held.
constexpr float kFineFactor = 0.1f;

enum class Style { Default, Slider, Knob, Field };
enum class Kind { Float, Bool, Choice, Texture, Cube, Lut, Spacer, NewLine, Other };

struct WidgetUi {
    Style declared = Style::Default;
    bool hasSoftMin = false, hasSoftMax = false;
    float softMin = 0.f, softMax = 0.f;
    std::vector<std::string> warnings;
};

// Each argument is the raw XML attribute, or nullptr when absent. Never fails;
// problems are reported in WidgetUi::warnings.
WidgetUi readWidgetUi(const char* style, const char* softMinimum, const char* softMaximum);

// Float + Default -> Slider; any non-float kind -> Default.
Style resolveStyle(Kind kind, Style declared);

struct Range { float lo = 0.f, hi = 0.f; };

// The slider range for a float. Appends a warning to ui.warnings on fallback.
Range softRange(float hardMin, float hardMax, float defaultValue, WidgetUi& ui);

// Rounds v to a multiple of step when step > 0.
float quantise(float v, float step);

// Position of value in soft, clamped to [0, 1]; 0 when lo == hi.
float toNormalized(float value, Range soft);

// Clamps t to [0, 1], maps into soft, quantises, clamps to [hardMin, hardMax].
float fromNormalized(float t, Range soft, float hardMin, float hardMax, float step);

// Moves value by pixels of drag (field: horizontal; knob: vertical, up positive).
float scrub(float value, float pixels, Range soft, float hardMin, float hardMax, float step, bool fine);

bool isModified(float value, float defaultValue, float step);

bool isWhite(int r, int g, int b);

struct Item { Kind kind; Style style; std::string group; };  // style already resolved

enum class RowKind { Heading, Slider, Flow };

struct Row { RowKind kind; std::string group; std::vector<int> items; };  // items index the input

std::vector<Row> layoutRows(const std::vector<Item>& items, bool showGroupHeadings);

}  // namespace fxui

// Headless self-test (--fx-ui-test <fxDir>). Prints FX-UI PASS|FAIL lines,
// returns the number of failures.
int fxUiSelfTest(const std::string& fxDir);

#endif  // GFCFXUI_H
