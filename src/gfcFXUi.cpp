// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcFXUi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace fxui {

namespace {

bool equalsNoCase(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
            return false;
    return *a == *b;
}

// Parses a whole attribute as a finite float. False for empty, trailing junk,
// NaN or infinity.
bool parseFloat(const char* text, float& out)
{
    char* end = nullptr;
    const float v = std::strtof(text, &end);
    if (end == text || !std::isfinite(v)) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return false;
    out = v;
    return true;
}

void readSoftEnd(const char* attr, const char* attrName, bool& has, float& value,
                 std::vector<std::string>& warnings)
{
    if (!attr) return;
    if (parseFloat(attr, value)) {
        has = true;
    } else {
        warnings.push_back(std::string("unparsable ") + attrName + " '" + attr + "'");
    }
}

float clampTo(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

Range ruleRange(float hardMin, float hardMax, float defaultValue)
{
    Range r;
    if (hardMax - hardMin <= kHugeSpan) {
        r = {hardMin, hardMax};
    } else if (hardMin >= 0.f) {
        r = {hardMin, std::max(hardMin + 1.f, 2.f * defaultValue)};
    } else {
        const float s = std::max(std::fabs(defaultValue), 1.f);
        r = {defaultValue - s, defaultValue + s};
    }
    r.lo = clampTo(r.lo, hardMin, hardMax);
    r.hi = clampTo(r.hi, hardMin, hardMax);
    return r;
}

}  // namespace

WidgetUi readWidgetUi(const char* style, const char* softMinimum, const char* softMaximum)
{
    WidgetUi ui;
    if (style) {
        if (equalsNoCase(style, "slider")) ui.declared = Style::Slider;
        else if (equalsNoCase(style, "knob")) ui.declared = Style::Knob;
        else if (equalsNoCase(style, "field")) ui.declared = Style::Field;
        else ui.warnings.push_back(std::string("unknown style '") + style + "'");
    }
    readSoftEnd(softMinimum, "softMinimum", ui.hasSoftMin, ui.softMin, ui.warnings);
    readSoftEnd(softMaximum, "softMaximum", ui.hasSoftMax, ui.softMax, ui.warnings);
    return ui;
}

Style resolveStyle(Kind kind, Style declared)
{
    if (kind != Kind::Float) return Style::Default;
    return declared == Style::Default ? Style::Slider : declared;
}

Range softRange(float hardMin, float hardMax, float defaultValue, WidgetUi& ui)
{
    const Range rule = ruleRange(hardMin, hardMax, defaultValue);
    if (!ui.hasSoftMin && !ui.hasSoftMax) return rule;

    Range r = rule;
    if (ui.hasSoftMin) r.lo = clampTo(ui.softMin, hardMin, hardMax);
    if (ui.hasSoftMax) r.hi = clampTo(ui.softMax, hardMin, hardMax);
    if (r.lo >= r.hi) {
        ui.warnings.push_back("soft range ignored");
        return rule;
    }
    return r;
}

float quantise(float v, float step)
{
    if (step <= 0.f) return v;
    return std::round(v / step) * step;
}

float toNormalized(float value, Range soft)
{
    if (soft.lo == soft.hi) return 0.f;
    return clampTo((value - soft.lo) / (soft.hi - soft.lo), 0.f, 1.f);
}

float fromNormalized(float t, Range soft, float hardMin, float hardMax, float step)
{
    t = clampTo(t, 0.f, 1.f);
    return clampTo(quantise(soft.lo + t * (soft.hi - soft.lo), step), hardMin, hardMax);
}

float scrub(float value, float pixels, Range soft, float hardMin, float hardMax, float step, bool fine)
{
    float delta = pixels * (soft.hi - soft.lo) / kScrubPixelsPerSpan;
    if (fine) delta *= kFineFactor;
    return clampTo(quantise(value + delta, step), hardMin, hardMax);
}

bool isModified(float value, float defaultValue, float step)
{
    return std::fabs(value - defaultValue) > std::max(step / 2.f, 1e-6f);
}

bool isWhite(int r, int g, int b) { return r == 255 && g == 255 && b == 255; }

std::vector<Row> layoutRows(const std::vector<Item>& items, bool showGroupHeadings)
{
    std::vector<Row> rows;
    bool flowOpen = false;
    Row flow;

    auto closeFlow = [&] {
        if (!flowOpen) return;
        flowOpen = false;
        while (!flow.items.empty() && items[flow.items.back()].kind == Kind::Spacer) flow.items.pop_back();
        if (!flow.items.empty()) rows.push_back(flow);
    };

    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const Item& item = items[i];
        if (i == 0 || item.group != items[i - 1].group) {
            closeFlow();
            if (showGroupHeadings && !item.group.empty()) rows.push_back({RowKind::Heading, item.group, {}});
        }
        if (item.kind == Kind::NewLine) {
            closeFlow();
        } else if (item.kind == Kind::Float && item.style == Style::Slider) {
            closeFlow();
            rows.push_back({RowKind::Slider, item.group, {i}});
        } else {
            if (!flowOpen) {
                flow = {RowKind::Flow, item.group, {}};
                flowOpen = true;
            }
            flow.items.push_back(i);
        }
    }
    closeFlow();
    return rows;
}

}  // namespace fxui
