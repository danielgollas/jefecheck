#include "ReviewSummaryPdf_qt.h"

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

namespace jefe::qt {

namespace {

constexpr int kDpi = 300;

int mm(double v) { return int(v * kDpi / 25.4 + 0.5); }

QString qs(const std::string& s) { return QString::fromStdString(s); }

const QString& emDash() { static const QString s = QString::fromUtf8("\xE2\x80\x94"); return s; }
const QString& enDash() { static const QString s = QString::fromUtf8("\xE2\x80\x93"); return s; }
const QString& middleDot() { static const QString s = QString::fromUtf8("\xC2\xB7"); return s; }

struct Fonts {
    QFont title, heading, round, frame, body, footer;
};

Fonts makeFonts() {
    Fonts f;
    f.title.setPointSizeF(16);   f.title.setBold(true);
    f.heading.setPointSizeF(13); f.heading.setBold(true);
    f.round.setPointSizeF(10.5); f.round.setBold(true);
    f.frame.setPointSizeF(9.5);  f.frame.setBold(true);
    f.body.setPointSizeF(9);
    f.footer.setPointSizeF(8);
    return f;
}

QString counted(int n, const char* one, const char* many) {
    return QString::number(n) + " " + (n == 1 ? one : many);
}

QString noteLine(const gfcReviewSummary::Note& n) {
    QString s = qs(n.type) + "  " + qs(n.author) + "  frames ";
    s += n.always ? QStringLiteral("all") : QString::number(n.from) + enDash() + QString::number(n.to);
    if (n.type == "text") s += "  \"" + qs(n.text) + "\"";
    return s;
}

/** Where everything in one frame entry goes: thumbnail on the left, frame
    label and note lines on the right. Measuring and drawing both use this, so
    the page-break decisions and the drawing always agree. */
struct FrameEntryLayout {
    QImage img;                     // null when the thumbnail is unavailable
    int thumbW = 0;                 // drawn thumbnail size
    int thumbH = 0;
    int textX = 0;
    int textW = 0;
    int noteX = 0;
    int noteW = 0;
    QString label;
    int labelH = 0;
    std::vector<int> noteHeights;   // one per note shown, in stored order
    int hiddenNotes = 0;            // notes left out behind moreLine
    QString moreLine;               // "+N more notes", empty when none are hidden
    int moreH = 0;
    int height = 0;                 // whole entry
};

/** Lays out @a f within @a maxHeight. Entries are never split across pages,
    so a note list taller than that is cut to the notes that fit and closed
    with a "+N more notes" line (the TXT and CSV outputs still list them all). */
FrameEntryLayout layoutFrameEntry(const gfcReviewSummary::Frame& f, const Fonts& fonts, int W, int maxHeight,
                                  const std::function<int(const QFont&, const QString&, int)>& measure) {
    FrameEntryLayout e;
    const int column = mm(80);
    if (!f.thumbnailPath.empty()) e.img.load(qs(f.thumbnailPath));
    e.thumbW = column;
    e.thumbH = (e.img.isNull() || e.img.width() <= 0)
               ? mm(45)
               : std::max(1, int(double(column) * e.img.height() / e.img.width() + 0.5));
    if (e.thumbH > maxHeight) {
        // A very tall image: shrink it to the page, keeping its aspect.
        e.thumbW = std::max(1, int(double(e.thumbW) * maxHeight / e.thumbH + 0.5));
        e.thumbH = maxHeight;
    }
    e.textX = column + mm(5);
    e.textW = W - e.textX;
    e.noteX = e.textX + mm(5);
    e.noteW = e.textW - mm(5);

    const int gap = mm(1);
    e.label = qs(gfcReviewSummary::frameLabel(f));
    e.labelH = measure(fonts.frame, e.label, e.textW);
    int textH = e.labelH + gap;
    const int total = int(f.notes.size());
    e.moreH = std::max(measure(fonts.body, "+" + counted(total, "more note", "more notes"), e.noteW), mm(3.5));
    for (int i = 0; i < total; ++i) {
        const int nh = std::max(measure(fonts.body, noteLine(f.notes[i]), e.noteW), mm(3.5));
        // Keep room for the "+N more notes" line unless this is the last note.
        const int reserve = (i == total - 1) ? 0 : e.moreH + gap;
        if (textH + nh + gap + reserve > maxHeight) break;
        e.noteHeights.push_back(nh);
        textH += nh + gap;
    }
    e.hiddenNotes = total - int(e.noteHeights.size());
    if (e.hiddenNotes > 0) {
        e.moreLine = "+" + counted(e.hiddenNotes, "more note", "more notes");
        textH += e.moreH + gap;
    }
    e.height = std::max(e.thumbH, textH);
    return e;
}

// Where one frame entry landed (for self-test).
struct EntryTrace {
    int page = 0;
    int top = 0;
    int bottom = 0;        // top + entry height
    int hiddenNotes = 0;   // notes left out behind "+N more notes"
};

// Trace of round-header and first-entry page assignments (for self-test).
struct LayoutTrace {
    std::vector<int> roundHeaderPages;     // page of each round header
    std::vector<int> roundFirstEntryPages; // page of first frame in each round (-1 if no frames)
    std::vector<EntryTrace> entries;       // every frame entry, in layout order
    int footerTop = 0;                     // y where the footer band starts
};

// Walks the whole document. With painter == nullptr it only measures and
// returns the page count; with a painter it draws, using totalPages in the
// footers. Both passes make identical break decisions because both measure
// with the same fonts against the same device.
int layoutSummary(QPdfWriter& pdf, QPainter* painter, const gfcReviewSummary::Doc& doc, int totalPages, LayoutTrace* trace = nullptr) {
    const Fonts fonts = makeFonts();
    const QRect area = pdf.pageLayout().paintRectPixels(kDpi);
    const int W = area.width();
    const int H = area.height();
    const int footerH = mm(8);
    const int bottom = H - footerH;
    int page = 1;
    int y = 0;
    if (trace) trace->footerTop = bottom;

    auto measure = [&](const QFont& font, const QString& text, int width) {
        const QFontMetricsF fm(font, &pdf);
        return int(fm.boundingRect(QRectF(0, 0, width, 1e7), Qt::TextWordWrap, text).height() + 0.5);
    };
    auto drawText = [&](const QFont& font, const QString& text, int x, int top, int width, int height) {
        if (!painter) return;
        painter->setFont(font);
        painter->setPen(Qt::black);
        painter->drawText(QRect(x, top, width, height), Qt::TextWordWrap, text);
    };
    auto drawFooter = [&]() {
        if (!painter) return;
        painter->setFont(fonts.footer);
        painter->setPen(Qt::darkGray);
        painter->drawText(QRect(0, H - footerH, W, footerH), Qt::AlignHCenter | Qt::AlignBottom,
                          qs(doc.title) + " " + emDash() + " page " + QString::number(page) +
                          " of " + QString::number(totalPages));
        painter->setPen(Qt::black);
    };
    auto newPage = [&]() {
        drawFooter();
        if (painter) pdf.newPage();
        ++page;
        y = 0;
    };
    // Entries are never split: move to a new page unless already at its top.
    auto ensureRoom = [&](int height) {
        if (y > 0 && y + height > bottom) newPage();
    };
    auto paragraph = [&](const QFont& font, const QString& text, int gapAfter) {
        const int h = measure(font, text, W);
        ensureRoom(h);
        drawText(font, text, 0, y, W, h);
        y += h + gapAfter;
    };

    // Header block.
    paragraph(fonts.title, "Review summary " + emDash() + " " + qs(doc.title), mm(1));
    paragraph(fonts.body, "Exported " + qs(gfcReviewSummary::isoUtc(doc.exportedAt)) +
                          " by JefeCheck " + qs(doc.appVersion), 0);
    paragraph(fonts.body, counted(int(doc.media.size()), "media", "media") + " " + middleDot() + " " +
                          counted(gfcReviewSummary::roundCount(doc), "round", "rounds") + " " + middleDot() + " " +
                          counted(gfcReviewSummary::noteCount(doc), "note", "notes"), mm(6));

    for (size_t mi = 0; mi < doc.media.size(); ++mi) {
        const gfcReviewSummary::Media& m = doc.media[mi];
        if (mi > 0) newPage();
        paragraph(fonts.heading, qs(m.displayName), mm(3));
        if (!m.notesReadable || m.rounds.empty()) {
            paragraph(fonts.body, m.notesReadable ? QStringLiteral("No notes") : QStringLiteral("Notes unreadable"), mm(4));
            continue;
        }
        for (size_t ri = 0; ri < m.rounds.size(); ++ri) {
            const gfcReviewSummary::Round& r = m.rounds[ri];
            const QString header = "Round " + QString::number(ri + 1) + " " + emDash() + " " + qs(r.author) +
                                   " " + emDash() + " created " + qs(gfcReviewSummary::isoUtc(r.created)) +
                                   " " + emDash() + " " + (r.locked ? "locked" : "open");
            const int headerH = measure(fonts.round, header, W);
            // The first entry shares its page with the round header, so it gets
            // the page minus the header; later entries get a whole page.
            const int firstEntryMaxH = bottom - (headerH + mm(2));

            // Reserve the header plus the first entry (if any), so the header is
            // never left alone at the bottom of a page.
            int firstEntryH = 0;
            if (r.frames.empty()) {
                // "No notes" line is body text
                firstEntryH = measure(fonts.body, QStringLiteral("No notes"), W);
            } else {
                firstEntryH = layoutFrameEntry(r.frames[0], fonts, W, firstEntryMaxH, measure).height;
            }
            ensureRoom(headerH + mm(2) + firstEntryH);
            if (trace) trace->roundHeaderPages.push_back(page);
            
            drawText(fonts.round, header, 0, y, W, headerH);
            y += headerH + mm(2);
            if (r.frames.empty()) {
                if (trace) trace->roundFirstEntryPages.push_back(-1);
                paragraph(fonts.body, QStringLiteral("No notes"), mm(4));
                continue;
            }
            
            bool firstEntry = true;
            for (const gfcReviewSummary::Frame& f : r.frames) {
                const FrameEntryLayout e =
                    layoutFrameEntry(f, fonts, W, firstEntry ? firstEntryMaxH : bottom, measure);
                ensureRoom(e.height);
                if (firstEntry && trace) trace->roundFirstEntryPages.push_back(page);
                firstEntry = false;
                if (trace) trace->entries.push_back(EntryTrace{page, y, y + e.height, e.hiddenNotes});

                if (painter) {
                    const QRect thumbRect(0, y, e.thumbW, e.thumbH);
                    if (e.img.isNull()) {
                        painter->setPen(QPen(Qt::gray, mm(0.3)));
                        painter->setBrush(Qt::NoBrush);
                        painter->drawRect(thumbRect);
                        painter->setFont(fonts.body);
                        painter->drawText(thumbRect, Qt::AlignCenter | Qt::TextWordWrap,
                                          QStringLiteral("thumbnail unavailable"));
                        painter->setPen(Qt::black);
                    } else {
                        painter->drawImage(thumbRect, e.img);
                    }
                    int ty = y;
                    drawText(fonts.frame, e.label, e.textX, ty, e.textW, e.labelH);
                    ty += e.labelH + mm(1);
                    for (size_t ni = 0; ni < e.noteHeights.size(); ++ni) {
                        const gfcReviewSummary::Note& n = f.notes[ni];
                        const QColor swatch = QColor::fromRgbF(std::clamp(n.r, 0.0f, 1.0f),
                                                               std::clamp(n.g, 0.0f, 1.0f),
                                                               std::clamp(n.b, 0.0f, 1.0f));
                        painter->fillRect(QRect(e.textX, ty + mm(0.6), mm(3), mm(3)), swatch);
                        drawText(fonts.body, noteLine(n), e.noteX, ty, e.noteW, e.noteHeights[ni]);
                        ty += e.noteHeights[ni] + mm(1);
                    }
                    if (!e.moreLine.isEmpty()) {
                        drawText(fonts.body, e.moreLine, e.noteX, ty, e.noteW, e.moreH);
                    }
                }
                y += e.height + mm(5);
            }
        }
    }
    drawFooter();
    return page;
}

}  // namespace

// Internal test entry point that allows capturing layout trace.
bool writeReviewSummaryPdfWithTrace(const gfcReviewSummary::Doc& doc, const QString& path,
                                     int* pagesOut, QString* err, LayoutTrace* trace) {
    const QString partial = path + ".partial";
    QFile::remove(partial);
    int pages = 0;
    {
        QPdfWriter pdf(partial);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setResolution(kDpi);
        pdf.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
        pdf.setTitle(qs(doc.title));
        pdf.setCreator("JefeCheck " + qs(doc.appVersion));

        pages = layoutSummary(pdf, nullptr, doc, 0, trace);
        QPainter painter;
        if (!painter.begin(&pdf)) {
            if (err) *err = QStringLiteral("Cannot write %1").arg(path);
            QFile::remove(partial);
            return false;
        }
        layoutSummary(pdf, &painter, doc, pages, trace);
        painter.end();
    }
    QFile::remove(path);
    if (!QFile::rename(partial, path)) {
        if (err) *err = QStringLiteral("Cannot rename %1").arg(partial);
        QFile::remove(partial);
        return false;
    }
    if (pagesOut) *pagesOut = pages;
    return true;
}

bool writeReviewSummaryPdf(const gfcReviewSummary::Doc& doc, const QString& path,
                           int* pagesOut, QString* err) {
    return writeReviewSummaryPdfWithTrace(doc, path, pagesOut, err, nullptr);
}

int reviewSummaryPdfSelfTest() {
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
        } else {
            ++fail;
            std::fprintf(stderr, "NOTE-SUMMARY-PDF FAIL: %s\n", msg);
        }
    };

    const QString dir = QDir::tempPath() + "/jefe_summary_pdf_test_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(dir);
    QImage thumb(640, 360, QImage::Format_RGB32);
    thumb.fill(QColor(40, 120, 60));
    const QString thumbPath = dir + "/thumb.png";
    check(thumb.save(thumbPath), "fixture thumbnail saved");

    gfcReviewSummary::Note note;
    note.id = "n1";
    note.type = "text";
    note.author = "Supervisor";
    note.from = 12;
    note.to = 12;
    note.text = "too warm here";

    gfcReviewSummary::Frame withThumb;
    withThumb.frame = 12;
    withThumb.notes.push_back(note);
    withThumb.thumbnailPath = thumbPath.toStdString();
    gfcReviewSummary::Frame noThumb = withThumb;
    noThumb.thumbnailPath.clear();

    gfcReviewSummary::Round locked;
    locked.id = "r1";
    locked.author = "Supervisor";
    locked.created = 1789364663;
    locked.locked = true;
    locked.frames.push_back(withThumb);
    gfcReviewSummary::Round empty;
    empty.id = "r2";
    empty.author = "Artist";
    empty.created = 1789400000;
    gfcReviewSummary::Round missing = locked;
    missing.frames.clear();
    missing.frames.push_back(noThumb);

    gfcReviewSummary::Media a;
    a.mediaPath = "/shots/a.exr";
    a.displayName = "a.exr";
    a.rounds.push_back(locked);
    gfcReviewSummary::Media b;
    b.mediaPath = "/shots/b.exr";
    b.displayName = "b.exr";
    b.rounds.push_back(missing);
    b.rounds.push_back(empty);

    gfcReviewSummary::Doc doc;
    doc.title = "PDF self-test";
    doc.exportedAt = 1789400000;
    doc.appVersion = "1.7.0";
    doc.media = {a, b};

    const QString out = dir + "/summary.pdf";
    int pages = 0;
    QString err;
    check(writeReviewSummaryPdf(doc, out, &pages, &err), "writer reports success");
    check(pages == 2, "the second media starts page 2");

    QFile f(out);
    const QByteArray bytes = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    check(bytes.startsWith("%PDF-"), "file starts with %PDF-");
    check(bytes.trimmed().endsWith("%%EOF"), "file ends with %%EOF");
    // Page dictionaries are written uncompressed: count "/Type /Page" but not "/Type /Pages".
    int pageDicts = 0;
    for (qsizetype at = bytes.indexOf("/Type /Page"); at >= 0; at = bytes.indexOf("/Type /Page", at + 1)) {
        if (at + 11 >= bytes.size() || bytes.at(at + 11) != 's') ++pageDicts;
    }
    check(pages > 0 && pageDicts == pages, "page dictionaries match the reported page count");
    check(!QFile::exists(out + ".partial"), "no partial file left");

    QString err2;
    int pages2 = 0;
    check(!writeReviewSummaryPdf(doc, "/nonexistent_dir_jefe/x.pdf", &pages2, &err2) && !err2.isEmpty(),
          "an unwritable path reports an error");

    // Test that round headers stay on the same page as their first entry.
    // Build a document with enough content in round 1 to fill most of a page,
    // then add round 2 with a tall first entry that would be orphaned if the
    // reserve height is insufficient (mm(20) vs actual entry height).
    gfcReviewSummary::Doc docHeaderTest;
    docHeaderTest.title = "Header orphan test";
    docHeaderTest.exportedAt = 1789400000;
    docHeaderTest.appVersion = "1.7.0";
    
    gfcReviewSummary::Media m;
    m.mediaPath = "/test/media.exr";
    m.displayName = "media.exr";
    
    // Round 1: many frames to fill most of the page
    gfcReviewSummary::Round r1;
    r1.id = "r1";
    r1.author = "Round 1";
    r1.created = 1789400000;
    for (int i = 0; i < 18; ++i) {
        gfcReviewSummary::Frame f;
        f.frame = i;
        f.thumbnailPath = thumbPath.toStdString();
        for (int j = 0; j < 2; ++j) {
            gfcReviewSummary::Note n;
            n.id = "n_r1_" + std::to_string(i) + "_" + std::to_string(j);
            n.type = "text";
            n.author = "Reviewer";
            n.from = i;
            n.to = i;
            n.text = "Note";
            f.notes.push_back(n);
        }
        r1.frames.push_back(f);
    }
    m.rounds.push_back(r1);
    
    // Round 2: has a tall first frame to test the page reservation fix
    gfcReviewSummary::Round r2;
    r2.id = "r2";
    r2.author = "Round 2";
    r2.created = 1789400001;
    gfcReviewSummary::Frame f2;
    f2.frame = 100;
    f2.thumbnailPath = thumbPath.toStdString();
    for (int j = 0; j < 6; ++j) {
        gfcReviewSummary::Note n;
        n.id = "n_r2_" + std::to_string(j);
        n.type = "text";
        n.author = "Reviewer";
        n.from = 100;
        n.to = 100;
        n.text = "Note";
        f2.notes.push_back(n);
    }
    r2.frames.push_back(f2);
    m.rounds.push_back(r2);
    
    docHeaderTest.media = {m};
    
    const QString outHeaderTest = dir + "/header_test.pdf";
    int pagesHeaderTest = 0;
    QString errHeaderTest;
    LayoutTrace traceHeaderTest;
    check(writeReviewSummaryPdfWithTrace(docHeaderTest, outHeaderTest, &pagesHeaderTest, &errHeaderTest, &traceHeaderTest),
          "header-orphan test document renders");
    
    // Verify that round headers are on the same page as their first entries.
    bool headerPageMismatch = false;
    if (traceHeaderTest.roundHeaderPages.size() == traceHeaderTest.roundFirstEntryPages.size()) {
        for (size_t i = 0; i < traceHeaderTest.roundHeaderPages.size(); ++i) {
            if (traceHeaderTest.roundFirstEntryPages[i] >= 0 &&
                traceHeaderTest.roundHeaderPages[i] != traceHeaderTest.roundFirstEntryPages[i]) {
                headerPageMismatch = true;
                break;
            }
        }
    } else {
        headerPageMismatch = true;
    }
    check(!headerPageMismatch, "round headers on same page as first entry");

    // A frame entry with more notes than one page can hold is capped to what
    // fits next to its thumbnail, with a "+N more notes" line -- entries are
    // never split, so an uncapped one would run past the footer line.
    gfcReviewSummary::Doc docLong;
    docLong.title = "Long entry test";
    docLong.exportedAt = 1789400000;
    docLong.appVersion = "1.7.0";
    gfcReviewSummary::Media longMedia;
    longMedia.mediaPath = "/test/long.exr";
    longMedia.displayName = "long.exr";
    gfcReviewSummary::Round longRound;
    longRound.id = "r1";
    longRound.author = "Supervisor";
    longRound.created = 1789400000;
    gfcReviewSummary::Frame longFrame;
    longFrame.frame = 7;
    longFrame.thumbnailPath = thumbPath.toStdString();
    for (int j = 0; j < 80; ++j) {
        gfcReviewSummary::Note n;
        n.id = "n_long_" + std::to_string(j);
        n.type = "text";
        n.author = "Supervisor";
        n.from = 7;
        n.to = 7;
        n.text = "note " + std::to_string(j + 1);
        longFrame.notes.push_back(n);
    }
    longRound.frames.push_back(longFrame);
    longMedia.rounds.push_back(longRound);
    docLong.media = {longMedia};

    const QString outLong = dir + "/long_entry.pdf";
    int pagesLong = 0;
    QString errLong;
    LayoutTrace traceLong;
    check(writeReviewSummaryPdfWithTrace(docLong, outLong, &pagesLong, &errLong, &traceLong),
          "long-entry document renders");
    bool longFits = !traceLong.entries.empty() && traceLong.footerTop > 0;
    bool longCapped = !traceLong.entries.empty();
    for (const EntryTrace& e : traceLong.entries) {
        std::printf("NOTE-SUMMARY-PDF long entry: page=%d top=%d bottom=%d footerTop=%d hidden=%d\n",
                    e.page, e.top, e.bottom, traceLong.footerTop, e.hiddenNotes);
        if (e.bottom > traceLong.footerTop) longFits = false;
        if (e.hiddenNotes <= 0 || e.hiddenNotes >= 80) longCapped = false;
    }
    check(longFits, "an 80-note entry ends above the footer line");
    check(longCapped, "an 80-note entry hides the notes that do not fit behind +N more notes");
    check(!traceLong.roundHeaderPages.empty() &&
          traceLong.roundHeaderPages == traceLong.roundFirstEntryPages,
          "an 80-note entry stays on its round header's page");

    std::printf("NOTE-SUMMARY-PDF: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt
