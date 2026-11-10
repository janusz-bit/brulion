// Searching a PDF page's text (docs/UI.md "PDF viewer": the viewer's search): our own matcher on the
// text a backend extracts — case- and diacritics-insensitive (text::foldKey: "gesla" finds "gęślą",
// "ZOLW" finds "żółw"), any run of white space equal to any other (a line break of the PDF between
// two words of the query) — and the rectangles of a match from the boxes of its characters. Pure,
// so tested on every platform (tests/platform/tst_pdfsearch.cpp); the backends of pdfpages.h use it
// (Qt PDF and poppler directly, Android through the spellings its own search is asked for).
#pragma once

#include <QList>
#include <QRectF>
#include <QStringList>
#include <QStringView>

namespace brulion::platform {

/// A range of a page's text, in its UTF-16 units.
struct PdfTextRange {
    qsizetype start = 0;
    qsizetype length = 0;
    friend bool operator==(const PdfTextRange &, const PdfTextRange &) = default;
};

/// Where `query` occurs in `text`: text::foldKey on both (case and diacritics), every run of white
/// space (space, tab, CR, LF, NBSP, U+2028/2029 …) equal to any other run, soft hyphens (U+00AD)
/// and U+0002 (pdfium's hyphen at a line's end) ignored; left to right, not overlapping, at most
/// `maxHits`. Ranges in `text`'s UTF-16 units, through foldKeyWithMap's map: a range never splits a
/// surrogate pair or a base letter from its combining marks. The query is trimmed; one that folds to
/// nothing (empty, only spaces / marks) finds nothing.
QList<PdfTextRange> findPdfText(QStringView text, QStringView query, int maxHits);

/// `box` (from a PDF) may be used: not empty, every coordinate finite.
bool pdfUsableBox(const QRectF &box);

/// The rectangles of `range` from per-unit boxes (`boxes` — one per unit of the text, as a backend
/// gives them; empty boxes, its synthetic spaces and line breaks, skipped): consecutive boxes merged
/// while their vertical centres lie within half the taller one's height — one rectangle per line.
/// A range outside the boxes is clipped to them.
QList<QRectF> pdfRangeRects(const QList<QRectF> &boxes, PdfTextRange range);

/// The distinct spellings in `text` of `ranges` (white space as in the text), at most `max`, in
/// order of their first range: Android lets PdfRenderer search each of them (its own search is
/// case-insensitive only — "żółw" does not find "ZOLW" there).
QStringList pdfMatchSpellings(QStringView text, const QList<PdfTextRange> &ranges, int max = 16);

} // namespace brulion::platform
