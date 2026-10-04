// Review summary PDF (docs/superpowers/specs/2026-09-14-review-summary-export-design.md).
// QtGui only — no glad, no managers (developer_notes.md §1).
#ifndef JEFECHECK_QT_REVIEW_SUMMARY_PDF_H
#define JEFECHECK_QT_REVIEW_SUMMARY_PDF_H

#include <QString>

#include "../gfcReviewSummary.h"

namespace jefe::qt {

/**
 * Lays @a doc out as an A4 portrait PDF at @a path: a header block, one section
 * per media (each after the first on a new page), a header line per round, and
 * one entry per frame — the thumbnail from Frame::thumbnailPath on the left (a
 * "thumbnail unavailable" box when empty or unreadable), the notes on the right.
 * Writes <path>.partial and renames it. Returns false with @a err filled on
 * failure; @a pagesOut receives the page count.
 */
bool writeReviewSummaryPdf(const gfcReviewSummary::Doc& doc, const QString& path,
                           int* pagesOut, QString* err);

/** Prints NOTE-SUMMARY-PDF: pass=N fail=N; returns the failed-check count. */
int reviewSummaryPdfSelfTest();

}  // namespace jefe::qt

#endif
