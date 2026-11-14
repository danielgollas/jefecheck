// SPDX-License-Identifier: GPL-2.0-or-later
// Headless self-test for the FX UI model (--fx-ui-test <fxDir>).
#include "gfcFXUi.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "gfcfxstack.h"
#include "xmlParser.h"

using namespace fxui;

namespace {

int g_fails = 0;

void check(const std::string& name, bool cond)
{
    std::printf("FX-UI %s %s\n", cond ? "PASS" : "FAIL", name.c_str());
    if (!cond) ++g_fails;
}

bool approx(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

bool rangeIs(Range r, float lo, float hi) { return approx(r.lo, lo) && approx(r.hi, hi); }

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

    // Every bundled float names its control style, and its slider range is usable:
    // explicit soft ends sit inside the hard limits, lo < hi, and the default is on the slider.
    std::string unstyled, badRange;
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

                const char* type = widget.getAttribute("type");
                if (!type || std::string(type) != "float") continue;
                const char* varName = widget.getAttribute("varName");
                const std::string where = " " + name + " " + (varName ? varName : "?");
                if (!widget.getAttribute("style")) unstyled += where;
                // The loader's defaults when an attribute is absent (gfcFXWidget).
                auto number = [&](const char* attr, float fallback) {
                    const char* v = widget.getAttribute(attr);
                    return v ? static_cast<float>(std::atof(v)) : fallback;
                };
                const float hardMin = number("minimum", -999.f), hardMax = number("maximum", 999.f);
                const float def = number("default", 0.f);
                WidgetUi checked = ui;
                const Range soft = softRange(hardMin, hardMax, def, checked);
                const bool endsInside = (!ui.hasSoftMin || (ui.softMin >= hardMin && ui.softMin <= hardMax)) &&
                                        (!ui.hasSoftMax || (ui.softMax >= hardMin && ui.softMax <= hardMax));
                if (!endsInside || !(soft.lo < soft.hi) || def < soft.lo || def > soft.hi ||
                    checked.warnings.size() != ui.warnings.size()) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf, " (soft %g..%g, hard %g..%g, default %g)", soft.lo, soft.hi,
                                  hardMin, hardMax, def);
                    badRange += where + buf;
                }
            }
        }
        std::string detail;
        for (const std::string& w : warnings) detail += " [" + w + "]";
        check("bundled: " + name + " has no widget warnings" + detail, warnings.empty());
    }
    check("bundled floats styled:" + unstyled, unstyled.empty());
    check("bundled defaults inside soft range:" + badRange, badRange.empty());
}

gfcFX namedFX(const std::string& name, std::uint32_t id = 0)
{
    gfcFX fx;
    fx.name = name;
    fx.instanceId = id;
    return fx;
}

std::vector<std::uint32_t> idsOf(gfcFXStack& stack)
{
    std::vector<std::uint32_t> ids;
    for (int i = 0; i < stack.getNumOfFXs(); ++i) ids.push_back(stack.fxAt(i).instanceId);
    return ids;
}

// FX instance ids on plain gfcFX objects: no shaders, no fxManager.
void stackInstanceIds()
{
    using Ids = std::vector<std::uint32_t>;
    {
        gfcFXStack s;
        const std::uint32_t a = s.addFX(namedFX("a"));
        const std::uint32_t b = s.addFX(namedFX("b"));
        const std::uint32_t c = s.addFX(namedFX("c"));
        check("ids assigned 1, 2, 3", idsOf(s) == Ids({1, 2, 3}));
        check("ids addFX returns the id it assigned", a == 1 && b == 2 && c == 3);
        check("ids indexOfInstance finds 2 at index 1", s.indexOfInstance(2) == 1);
        check("ids indexOfInstance of an absent id is -1", s.indexOfInstance(9) == -1 && s.indexOfInstance(0) == -1);
    }
    {
        gfcFXStack s;
        const gfcFX same = namedFX("same");
        s.addFX(same);
        s.addFX(same);
        const Ids ids = idsOf(s);
        check("ids copy of same FX distinct", ids.size() == 2 && ids[0] != 0 && ids[1] != 0 && ids[0] != ids[1]);
    }
    {
        gfcFXStack x, y;
        x.addFX(namedFX("x1"));
        x.addFX(namedFX("x2"));
        y.addFX(namedFX("y1"));
        x.appendFXStack(y);
        check("ids append dedupes ids -> 1, 2, 3", idsOf(x) == Ids({1, 2, 3}));
        check("ids append keeps the appended FX last", x.getNumOfFXs() == 3 && x.fxAt(2).name == "y1");
    }
    {
        gfcFXStack s;
        const std::uint32_t kept = s.addFX(namedFX("kept", 7));
        const std::uint32_t clash = s.addFX(namedFX("clash", 7));
        check("ids duplicate id: first keeps it, second is fresh", kept == 7 && clash == 8);
    }
    {
        gfcFXStack s;
        s.addFX(namedFX("a"));
        const std::uint32_t big = s.addFX(namedFX("big", 4294967294u));
        const std::uint32_t after = s.addFX(namedFX("after"));
        check("ids out-of-range id 4294967294 replaced by a fresh one", big == 2 && after == 3);
    }
    {
        gfcFXStack s;
        const std::uint32_t top = s.addFX(namedFX("top", 0x7FFFFFFFu));
        const std::uint32_t next = s.addFX(namedFX("next"));
        const std::uint32_t third = s.addFX(namedFX("third"));
        check("ids at the top of the range stay valid, non-zero and distinct",
              top == 0x7FFFFFFFu && next != 0 && next < 0x80000000u && third != 0 && third < 0x80000000u &&
                  next != top && third != top && third != next);
    }
    {
        // The Qt bridge removes an FX by clearing the stack and re-adding the rest.
        gfcFXStack s;
        s.addFX(namedFX("a"));
        s.addFX(namedFX("b"));
        s.addFX(namedFX("c"));
        std::vector<gfcFX> all = s.getAllFXs();
        s.clearStack();
        for (int i = 0; i < (int)all.size(); ++i)
            if (i != 2) s.addFX(all[i]);
        const std::uint32_t added = s.addFX(namedFX("d"));
        check("ids no reuse after remove -> 4", added == 4 && idsOf(s) == Ids({1, 2, 4}));
    }
    {
        gfcFXStack s;
        s.addFX(namedFX("a"));
        s.addFX(namedFX("b"));
        XMLNode top = XMLNode::createXMLTopNode("stack");
        s.saveStackToNode(top);
        int size = 0;
        XMLSTR text = top.createXMLString(0, &size);
        const std::string xml = text ? std::string(text, static_cast<size_t>(size)) : std::string();
        free(text);  // xmlParser allocates with malloc
        check("ids saved: second FX has instanceId=\"2\"", xml.find("instanceId=\"2\"") != std::string::npos);
    }
}

}  // namespace

int fxUiSelfTest(const std::string& fxDir)
{
    g_fails = 0;

    // .jfx and session files store "0.5"; a comma-decimal system locale must not change
    // how atof/strtof (gfcfx.cpp, xmlParser users, readWidgetUi) read them.
    check("C numeric locale: strtof(\"0.5\") == 0.5 and atof(\"0.5\") == 0.5",
          std::strtof("0.5", nullptr) == 0.5f && std::atof("0.5") == 0.5);

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
    {
        WidgetUi ui = readWidgetUi(nullptr, "nan", nullptr);
        check("readWidgetUi softMinimum nan -> absent", !ui.hasSoftMin);
        check("readWidgetUi softMinimum nan -> one warning", ui.warnings.size() == 1);
    }
    {
        WidgetUi ui = readWidgetUi(nullptr, nullptr, "inf");
        check("readWidgetUi softMaximum inf -> absent, one warning", !ui.hasSoftMax && ui.warnings.size() == 1);
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
        check("softRange explicit (-5, 5) on hard (0, 10) -> (0, 5)", rangeIs(r, 0, 5));
        check("softRange explicit (-5, 5) on hard (0, 10) -> one clamped warning",
              ui.warnings.size() == 1 && hasWarning(ui, "soft range clamped to hard range"));
    }
    {
        WidgetUi ui;
        ui.hasSoftMin = ui.hasSoftMax = true;
        ui.softMin = 0; ui.softMax = 10;
        Range r = softRange(0, 10, 1, ui);
        check("softRange explicit ends on the hard limits -> no warning", rangeIs(r, 0, 10) && ui.warnings.empty());
    }
    {
        WidgetUi ui;
        ui.hasSoftMax = true;
        ui.softMax = 1.5f;
        Range r = softRange(0, 9999, 1, ui);
        check("softRange only softMax 1.5 on hard (0, 9999) def 1 -> (0, 1.5)",
              rangeIs(r, 0, 1.5f) && ui.warnings.empty());
    }
    {
        WidgetUi ui;
        ui.hasSoftMin = true;
        ui.softMin = 3;
        Range r = softRange(0, 9999, 1, ui);
        check("softRange only softMin 3 on hard (0, 9999) def 1 -> rule (0, 2)", rangeIs(r, 0, 2));
        check("softRange only softMin 3 inverts -> warning", hasWarning(ui, "soft range ignored"));
    }

    // quantise
    check("quantise 0.12349 step 0.001 -> 0.123", std::fabs(quantise(0.12349f, 0.001f) - 0.123f) <= 1e-6f);
    check("quantise 7.3 step 0 -> 7.3", quantise(7.3f, 0.f) == 7.3f);

    // toNormalized / fromNormalized
    check("toNormalized 5 in (0, 2) -> 1", toNormalized(5, {0, 2}) == 1.f);
    check("toNormalized 1 in (0, 2) -> 0.5", toNormalized(1, {0, 2}) == 0.5f);
    check("toNormalized 1 in (1, 1) -> 0", toNormalized(1, {1, 1}) == 0.f);
    check("fromNormalized 0.5 -> 1", approx(fromNormalized(0.5f, {0, 2}, 0, 9999, 0.001f), 1));
    check("fromNormalized 1.7 -> 2", approx(fromNormalized(1.7f, {0, 2}, 0, 9999, 0.001f), 2));
    check("fromNormalized 0 hard min 0.5 -> 0.5", approx(fromNormalized(0, {0, 2}, 0.5f, 9999, 0.001f), 0.5f));

    // scrub
    check("scrub +100px -> 2", approx(scrub(1, 100, {0, 2}, 0, 9999, 0.001f, false), 2));
    check("scrub +100px fine -> 1.1", approx(scrub(1, 100, {0, 2}, 0, 9999, 0.001f, true), 1.1f));
    check("scrub -100px from 0.2 -> 0 (hard clamp)", approx(scrub(0.2f, -100, {0, 2}, 0, 9999, 0.001f, false), 0));

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

    stackInstanceIds();

    bundledFiles(fxDir);

    std::printf("FX-UI %s (%d failed)\n", g_fails == 0 ? "ALL PASS" : "FAILED", g_fails);
    return g_fails;
}
