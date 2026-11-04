// Paper documents: notes laid out for paper without Pandoc or LaTeX — the "pdf" exporter (a PDF
// with embedded fonts), the "docx" exporter (Office Open XML written by Brulion) and printing
// (printNotes(): any paged paint device, e.g. the app's QPrinter). Part of the io module — see
// io/io.h for the overview.
//
// All three start from the HTML document export ("html-single": the same rendering of headings,
// lists, tasks, tables, code, math, pictures, Mermaid diagrams and embeds) turned into a
// QTextDocument that loads only the pictures of the export itself (never a file or the network):
// checkboxes as the list's markers, diagrams and other SVG pictures as pictures, links inside
// the document as text, pictures scaled to the page.
#pragma once

#include "io/exporter.h"

#include <QLocale>
#include <QPageLayout>
#include <QStringList>

class QPagedPaintDevice;

namespace brulion::io {

/// The paper of the PDF and DOCX exports (ExportInput::paperSize).
enum class PaperSize { A4, Letter };

/// The paper usual where `locale` is: Letter in the United States, Canada, Mexico, the
/// Philippines, Chile, Colombia, Venezuela, Costa Rica, Guatemala, El Salvador, Puerto Rico and
/// the Dominican Republic; A4 everywhere else (Poland, the C locale).
PaperSize paperSizeFor(const QLocale &locale);
/// "a4" | "letter".
QString paperSizeId(PaperSize size);
/// The size an id names ("a4", "letter", case-insensitive); A4 for anything else.
PaperSize paperSizeFromId(QStringView id);
/// The page layout of the exports: portrait, 20 mm margins.
QPageLayout paperLayout(PaperSize size);

/// Paints the notes of `input` (cleaned like runExport(); the order and look of the PDF export)
/// on `device` with the device's page layout, a page number at the bottom of each page. Returns
/// the number of pages painted (0: nothing to paint or the device could not be painted on);
/// problems (pictures that could not be shown) are appended to `warnings`. Needs a
/// QGuiApplication (fonts); the device is painted at its resolution — QPrinter in its default
/// ScreenResolution mode lays out like the PDF export.
int printNotes(const ExportInput &input, QPagedPaintDevice *device, QStringList *warnings = nullptr);

} // namespace brulion::io
