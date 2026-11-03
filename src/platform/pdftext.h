// A PDF page's text picked by pointing and what a PDF says about itself (docs/UI.md "PDF viewer":
// selecting and copying text, the table of contents, links): pure, so tested on every platform
// (tests/platform/tst_pdftext.cpp). The backends of pdfpages.h use it — poppler for its whole
// selection (its per-character boxes), every backend to clean what it hands over.
//
//   pdfCaretAt() / pdfRangeBetween()   the characters between two points of a page, from one box
//                                      per UTF-16 unit of its text (pdfRangeRects() of pdfsearch.h
//                                      then gives the rectangles, one per line)
//   pdfCopyText()                      a selection's text as it goes to the clipboard
//   pdfCleanTitle()                    an outline entry's title as it is shown
//   pdfLinkUrlAllowed()                a link of a PDF that may be handed to the browser / mail
//                                      client (Workspace::openUrl's schemes: http, https, mailto)
#pragma once

#include "pdfsearch.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringView>

namespace brulion::platform {

/// The longest outline title kept (in UTF-16 units; longer ones end with "…").
inline constexpr int MaxPdfTitleLength = 300;
/// The longest link address kept.
inline constexpr int MaxPdfUrlLength = 2048;

/// The box nearest to `point`: one holding it, else on the nearest line (boxes whose vertical span
/// is nearest, within half a point) the horizontally nearest. Empty and non-finite boxes are never
/// chosen; −1 when none is left or the point is not finite.
qsizetype pdfNearestBox(const QList<QRectF> &boxes, QPointF point);

/// The caret position (0 … boxes.size()) nearest to `point`: before or after the character whose
/// box holds it (its left or right half), else the nearest character's — a point beside a line takes
/// its nearer end, a point between lines the nearer line; a point above all the text is its start
/// (0), below it its end (boxes.size()). Empty boxes (a backend's synthetic
/// spaces and line breaks) are never chosen; −1 when every box is empty or none is finite.
qsizetype pdfCaretAt(const QList<QRectF> &boxes, QPointF point);

/// The characters between the carets at `from` and `to` (either order); empty without boxes or when
/// both points give one caret.
PdfTextRange pdfRangeBetween(const QList<QRectF> &boxes, QPointF from, QPointF to);

/// The word of `text` around unit `index` (letters, digits, combining marks, surrogate pairs,
/// apostrophes and hyphens inside a word): what a long press selects. A unit that is no part of a
/// word gives itself alone; an index outside the text nothing.
PdfTextRange pdfWordAt(QStringView text, qsizetype index);

/// A selection's text for the clipboard: CR LF and lone CRs as LF, soft hyphens and pdfium's
/// line-end hyphen marker (U+0002) dropped, other control characters except tab and LF dropped,
/// lone surrogates dropped, trailing white space trimmed.
QString pdfCopyText(QStringView text);

/// An outline title as shown: line breaks, tabs and format characters (a bidi override) as spaces,
/// other control characters dropped, runs of white space as one, trimmed, lone surrogates dropped, at most
/// MaxPdfTitleLength units (a surrogate pair kept whole, "…" appended).
QString pdfCleanTitle(QStringView title);

/// A link's address that may open: http(s) with a host or mailto: with an address, no white space
/// or control characters, at most MaxPdfUrlLength. Never javascript:, file:, an app's scheme, a
/// relative path or a launch action.
bool pdfLinkUrlAllowed(QStringView url);

} // namespace brulion::platform
