// SPDX-License-Identifier: GPL-2.0-or-later
// Headless self-test for the FX UI model (--fx-ui-test <fxDir>).
#include "gfcFXUi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "xmlParser.h"

using namespace fxui;

namespace {

int g_fails = 0;

void check(const std::string& name, bool cond)
{
    std::printf("FX-UI %s %s\n", cond ? "PASS" : "FAIL", name.c_str());
    if (!cond) ++g_fails;
}

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

bool rangeIs(Range r, float lo, float hi) { return near(r.lo, lo) && near(r.hi, hi); }

bool hasWarning(const WidgetUi& ui, const std::string& text)
{
    return std::any_of(ui.warnings.begin(), ui.warnings.end(),
                       [&](const std::string& w) { return w.find(text) != std::string::npos; });
}

struct Want { RowKind kind; std::string group; std::vector<int> items; };

bool rowsAre(const std::vector<Row>& got, const std::vector<Want>& want)
{
    if (got.size() != want.size()) return false;
    for (size_t i = 0; i < got.size(); ++i)
        if (got[i].kind != want[i].kind || got[i].group != want[i].group || got[i].items != want[i].items)
            return false;
    return true;
}

Item it(Kind k, Style s = Style::Default, const std::string& g = "") { return Item{k, s, g}; }

void bundledFiles(const std::string& fxDir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::directory_iterator d(fxDir, ec), end; !ec && d != end; d.increment(ec))
        if (d->path().extension() == ".jfx") files.push_back(d->path());
    std::sort(files.begin(), files.end());
    check("bundled: found .jfx files in " + fxDir, !files.empty());

    for (const fs::path& p : files) {
        const std::string name = p.filename().string();
        XMLNode top = XMLNode::openFileHelper(p.string().c_str());
        check("bundled: " + name + " parses", top.loaded);
        if (!top.loaded) continue;
        XMLNode groups = top.getChildNode("root").getChildNode("groups");
        std::vector<std::string> warnings;
        const int nGroups = groups.nChildNode("group");
        int gi = 0;
        for (int g = 0; g < nGroups; ++g) {
            XMLNode group = groups.getChildNode("group", &gi);
            const int nWidgets = group.nChildNode("widget");
            int wi = 0;
            for (int w = 0; w < nWidgets; ++w) {
                XMLNode widget = group.getChildNode("widget", &wi);
                WidgetUi ui = readWidgetUi(widget.getAttribute("style"),
                                           widget.getAttribute("softMinimum"),
                                           widget.getAttribute("softMaximum"));
                warnings.insert(warnings.end(), ui.warnings.begin(), ui.warnings.end());
            }
        }
        std::string detail;
        for (const std::string& w : warnings) detail += " [" + w + "]";
        check("bundled: " + name + " has no widget warnings" + detail, warnings.empty());
    }
}

}  // namespace

int fxUiSelfTest(const std::string& fxDir)
{
    g_fails = 0;

    // readWidgetUi
    check("readWidgetUi Knob -> Knob", readWidgetUi("Knob", nullptr, nullptr).declared == Style::Knob);
    {
        WidgetUi ui = readWidgetUi("Knobb", nullptr, nullptr);
        check("readWidgetUi Knobb -> Default", ui.declared == Style::Default);
        check("readWidgetUi Knobb -> one warning naming it", ui.warnings.size() == 1 && hasWarning(ui, "Knobb"));
    }
    {
        WidgetUi ui = readWidgetUi(nullptr, "0.5", nullptr);
        check("readWidgetUi softMinimum 0.5", ui.hasSoftMin && ui.softMin == 0.5f && ui.warnings.empty());
    }
    {
        WidgetUi ui = readWidgetUi(nullptr, "abc", nullptr);
        check("readWidgetUi softMinimum abc -> absent", !ui.hasSoftMin);
        check("readWidgetUi softMinimum abc -> one warning", ui.warnings.size() == 1);
    }

    // resolveStyle
    check("resolveStyle Float/Default -> Slider", resolveStyle(Kind::Float, Style::Default) == Style::Slider);
    check("resolveStyle Bool/Knob -> Default", resolveStyle(Kind::Bool, Style::Knob) == Style::Default);

    // softRange, no explicit ends
    struct SR { float hmin, hmax, def, lo, hi; };
    const SR cases[] = {
        {0, 9999, 1, 0, 2},       {-9999, 9999, 1, 0, 2}, {-9999, 9999, 0, -1, 1},
        {0, 9999, 5, 0, 10},      {0, 1, 0.5f, 0, 1},     {0, 10, 2.2f, 0, 10},
        {0, 1024, 685, 0, 1024},  {0, 999, 0.01f, 0, 1},
    };
    for (const SR& c : cases) {
        WidgetUi ui;
        Range r = softRange(c.hmin, c.hmax, c.def, ui);
        char name[160];
        std::snprintf(name, sizeof name, "softRange (%g, %g, %g) -> (%g, %g) got (%g, %g)",
                      c.hmin, c.hmax, c.def, c.lo, c.hi, r.lo, r.hi);
        check(name, rangeIs(r, c.lo, c.hi) && ui.warnings.empty());
    }
    {
        WidgetUi ui;
        ui.hasSoftMin = ui.hasSoftMax = true;
        ui.softMin = 3; ui.softMax = 1;
        Range r = softRange(0, 9999, 1, ui);
        check("softRange inverted explicit -> rule (0, 2)", rangeIs(r, 0, 2));
        check("softRange inverted explicit -> warning", hasWarning(ui, "soft range ignored"));
    }
    {
        WidgetUi ui;
        ui.hasSoftMin = ui.hasSoftMax = true;
        ui.softMin = -5; ui.softMax = 5;
        Range r = softRange(0, 10, 1, ui);
        check("softRange explicit (-5, 5) on hard (0, 10) -> (0, 5)", rangeIs(r, 0, 5) && ui.warnings.empty());
    }

    // quantise
    check("quantise 0.12349 step 0.001 -> 0.123", std::fabs(quantise(0.12349f, 0.001f) - 0.123f) <= 1e-6f);
    check("quantise 7.3 step 0 -> 7.3", quantise(7.3f, 0.f) == 7.3f);

    // toNormalized / fromNormalized
    check("toNormalized 5 in (0, 2) -> 1", toNormalized(5, {0, 2}) == 1.f);
    check("toNormalized 1 in (0, 2) -> 0.5", toNormalized(1, {0, 2}) == 0.5f);
    check("toNormalized 1 in (1, 1) -> 0", toNormalized(1, {1, 1}) == 0.f);
    check("fromNormalized 0.5 -> 1", near(fromNormalized(0.5f, {0, 2}, 0, 9999, 0.001f), 1));
    check("fromNormalized 1.7 -> 2", near(fromNormalized(1.7f, {0, 2}, 0, 9999, 0.001f), 2));
    check("fromNormalized 0 hard min 0.5 -> 0.5", near(fromNormalized(0, {0, 2}, 0.5f, 9999, 0.001f), 0.5f));

    // scrub
    check("scrub +100px -> 2", near(scrub(1, 100, {0, 2}, 0, 9999, 0.001f, false), 2));
    check("scrub +100px fine -> 1.1", near(scrub(1, 100, {0, 2}, 0, 9999, 0.001f, true), 1.1f));
    check("scrub -100px from 0.2 -> 0 (hard clamp)", near(scrub(0.2f, -100, {0, 2}, 0, 9999, 0.001f, false), 0));

    // isModified / isWhite
    check("isModified 1.0005 vs 1 step 0.001 -> false", !isModified(1.0005f, 1, 0.001f));
    check("isModified 1.002 vs 1 step 0.001 -> true", isModified(1.002f, 1, 0.001f));
    check("isWhite 255,255,255", isWhite(255, 255, 255));
    check("isWhite 150,150,150 -> false", !isWhite(150, 150, 150));

    // layoutRows
    const Style S = Style::Slider, K = Style::Knob;
    check("layoutRows BCS -> four slider rows",
          rowsAre(layoutRows({it(Kind::NewLine), it(Kind::Float, S), it(Kind::NewLine), it(Kind::Float, S),
                              it(Kind::Float, S), it(Kind::NewLine), it(Kind::Float, S), it(Kind::NewLine)}, true),
                  {{RowKind::Slider, "", {1}}, {RowKind::Slider, "", {3}},
                   {RowKind::Slider, "", {4}}, {RowKind::Slider, "", {6}}}));
    check("layoutRows ADD -> Flow{1,2} Slider{4} Slider{5}",
          rowsAre(layoutRows({it(Kind::NewLine), it(Kind::Texture), it(Kind::Texture), it(Kind::NewLine),
                              it(Kind::Float, S), it(Kind::Float, S), it(Kind::NewLine)}, true),
                  {{RowKind::Flow, "", {1, 2}}, {RowKind::Slider, "", {4}}, {RowKind::Slider, "", {5}}}));
    check("layoutRows knobs -> Flow{0,1} Flow{4}",
          rowsAre(layoutRows({it(Kind::Float, K), it(Kind::Float, K), it(Kind::Spacer), it(Kind::NewLine),
                              it(Kind::Float, K)}, true),
                  {{RowKind::Flow, "", {0, 1}}, {RowKind::Flow, "", {4}}}));
    check("layoutRows Bool Slider Bool -> Flow{0} Slider{1} Flow{2}",
          rowsAre(layoutRows({it(Kind::Bool), it(Kind::Float, S), it(Kind::Bool)}, true),
                  {{RowKind::Flow, "", {0}}, {RowKind::Slider, "", {1}}, {RowKind::Flow, "", {2}}}));
    check("layoutRows Spacer NewLine NewLine -> no rows",
          layoutRows({it(Kind::Spacer), it(Kind::NewLine), it(Kind::NewLine)}, true).empty());
    {
        const std::vector<Item> grouped = {it(Kind::Bool, Style::Default, "A"), it(Kind::Float, S, "A"),
                                           it(Kind::Bool, Style::Default, "B")};
        check("layoutRows groups with headings",
              rowsAre(layoutRows(grouped, true),
                      {{RowKind::Heading, "A", {}}, {RowKind::Flow, "A", {0}}, {RowKind::Slider, "A", {1}},
                       {RowKind::Heading, "B", {}}, {RowKind::Flow, "B", {2}}}));
        const std::vector<Row> noHead = layoutRows(grouped, false);
        check("layoutRows groups without headings",
              rowsAre(noHead, {{RowKind::Flow, "A", {0}}, {RowKind::Slider, "A", {1}}, {RowKind::Flow, "B", {2}}}));
    }

    bundledFiles(fxDir);

    std::printf("FX-UI %s (%d failed)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_fails);
    return g_fails;
}
