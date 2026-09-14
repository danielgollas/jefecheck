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
#include <vector>

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

// Walks the whole document. With painter == nullptr it only measures and
// returns the page count; with a painter it draws, using totalPages in the
// footers. Both passes make identical break decisions because both measure
// with the same fonts against the same device.
int layoutSummary(QPdfWriter& pdf, QPainter* painter, const gfcReviewSummary::Doc& doc, int totalPages) {
    const Fonts fonts = makeFonts();
    const QRect area = pdf.pageLayout().paintRectPixels(kDpi);
    const int W = area.width();
    const int H = area.height();
    const int footerH = mm(8);
    const int bottom = H - footerH;
    int page = 1;
    int y = 0;

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
            ensureRoom(headerH + mm(20));   // keep a round header with the start of its first entry
            drawText(fonts.round, header, 0, y, W, headerH);
            y += headerH + mm(2);
            if (r.frames.empty()) {
                paragraph(fonts.body, QStringLiteral("No notes"), mm(4));
                continue;
            }
            for (const gfcReviewSummary::Frame& f : r.frames) {
                const int thumbW = mm(80);
                QImage img;
                if (!f.thumbnailPath.empty()) img.load(qs(f.thumbnailPath));
                const int thumbH = (img.isNull() || img.width() <= 0)
                                   ? mm(45)
                                   : std::max(1, int(double(thumbW) * img.height() / img.width() + 0.5));
                const int textX = thumbW + mm(5);
                const int textW = W - textX;
                const int noteX = textX + mm(5);
                const int noteW = textW - mm(5);

                const QString label = qs(gfcReviewSummary::frameLabel(f));
                const int labelH = measure(fonts.frame, label, textW);
                std::vector<int> noteHeights;
                int textH = labelH + mm(1);
                for (const gfcReviewSummary::Note& n : f.notes) {
                    const int nh = std::max(measure(fonts.body, noteLine(n), noteW), mm(3.5));
                    noteHeights.push_back(nh);
                    textH += nh + mm(1);
                }
                const int entryH = std::max(thumbH, textH);
                ensureRoom(entryH);

                if (painter) {
                    const QRect thumbRect(0, y, thumbW, thumbH);
                    if (img.isNull()) {
                        painter->setPen(QPen(Qt::gray, mm(0.3)));
                        painter->setBrush(Qt::NoBrush);
                        painter->drawRect(thumbRect);
                        painter->setFont(fonts.body);
                        painter->drawText(thumbRect, Qt::AlignCenter | Qt::TextWordWrap,
                                          QStringLiteral("thumbnail unavailable"));
                        painter->setPen(Qt::black);
                    } else {
                        painter->drawImage(thumbRect, img);
                    }
                    int ty = y;
                    drawText(fonts.frame, label, textX, ty, textW, labelH);
                    ty += labelH + mm(1);
                    for (size_t ni = 0; ni < f.notes.size(); ++ni) {
                        const gfcReviewSummary::Note& n = f.notes[ni];
                        const QColor swatch = QColor::fromRgbF(std::clamp(n.r, 0.0f, 1.0f),
                                                               std::clamp(n.g, 0.0f, 1.0f),
                                                               std::clamp(n.b, 0.0f, 1.0f));
                        painter->fillRect(QRect(textX, ty + mm(0.6), mm(3), mm(3)), swatch);
                        drawText(fonts.body, noteLine(n), noteX, ty, noteW, noteHeights[ni]);
                        ty += noteHeights[ni] + mm(1);
                    }
                }
                y += entryH + mm(5);
            }
        }
    }
    drawFooter();
    return page;
}

}  // namespace

namespace jefe::qt {

bool writeReviewSummaryPdf(const gfcReviewSummary::Doc& doc, const QString& path,
                           int* pagesOut, QString* err) {
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

        pages = layoutSummary(pdf, nullptr, doc, 0);
        QPainter painter;
        if (!painter.begin(&pdf)) {
            if (err) *err = QStringLiteral("Cannot write %1").arg(path);
            QFile::remove(partial);
            return false;
        }
        layoutSummary(pdf, &painter, doc, pages);
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

    std::printf("NOTE-SUMMARY-PDF: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt
