// The table of contents of a PDF (its outline, ISO 32000-1 §12.3.3) read from the file's own bytes,
// for the PDF viewer's outline where the renderer gives none safely (Qt PDF's QPdfBookmarkModel
// follows a hostile outline into a stack overflow: platform/pdfpages_qtpdf.cpp). Pure and
// GUI-free; the PDF comes from anywhere, so every step is bounded:
//
//  * objects: the cross-reference tables (with /Prev and /XRefStm) and streams (Flate, PNG
//    predictors), objects inside object streams; a broken or missing table: the file scanned for
//    "n g obj" (the last one of a number wins, as a viewer's repair does);
//  * the outline: every entry visited once (a loop of /First, /Next ends there), at most
//    Limits::maxItems entries and Limits::maxDepth levels, nesting of arrays and dictionaries,
//    stream sizes and the number of objects capped;
//  * where an entry leads: /Dest or a /GoTo action's /D — an explicit destination (a page object or
//    number, /XYZ, /FitH, /FitBH, /FitR's top) or a name looked up in the catalog's /Dests or its
//    /Names /Dests tree; the page by its place in the /Pages tree, the top as a fraction of the
//    page's height (its /MediaBox, inherited) from the top;
//  * titles: UTF-16BE (with its BOM), UTF-8 (with its BOM) or PDFDocEncoding.
//
// An encrypted PDF (/Encrypt in its trailer) gives no entries: its titles are encrypted strings.
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>

namespace brulion::pdfoutline {

struct Entry {
    QString title; ///< as written (one line or not: the caller cleans it)
    int level = 0; ///< 0 for the top level
    int page = 0;  ///< 1-based; 0: none (a group's heading, another file, a destination not found)
    double top = -1; ///< where on the page, a fraction of its height from the top; −1: its top
    friend bool operator==(const Entry &, const Entry &) = default;
};

struct Limits {
    int maxItems = 5000;
    int maxDepth = 32;
    int maxObjects = 1'000'000;            ///< objects of one file looked at
    qint64 maxStreamBytes = 64LL << 20;     ///< a decoded stream
    int maxPages = 100'000;                 ///< pages of the /Pages tree
};

/// The outline of the PDF `pdf` (its whole file) in reading order; empty when it has none, is
/// encrypted or cannot be read.
QList<Entry> readOutline(QByteArrayView pdf, const Limits &limits = {});

} // namespace brulion::pdfoutline
