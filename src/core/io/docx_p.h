// The paper document (paperdoc_p.h) as an Office Open XML word processing document, written by
// Brulion (no Pandoc, no LibreOffice): a ZIP with word/document.xml (paragraphs; headings as the
// "heading 1…6" styles, code as "Source Code", quotes as "Quote"; lists with numbering — bullets,
// numbers that start where the note's do, checkboxes ☐ / ☑ of tasks; bold, italic, strikeout,
// code, superscript and subscript, colours of dates and tags; links to the web and mail as
// hyperlinks; tables with their header rows; pictures as embedded PNG / JPEG media),
// styles.xml, numbering.xml, settings.xml, the relationships and the document's properties.
// Word allows 63 columns: a wider table keeps its first 63 (a warning); list levels deeper
// than nine are the ninth. Internal to the io module.
#pragma once

#include "io/paper.h"

#include <QByteArray>
#include <QDateTime>
#include <QStringList>

namespace brulion::io::detail {

class PaperDocument;

struct DocxOptions {
    QString title;
    /// The document's creation time (docProps/core.xml) and the ZIP entries' time.
    QDateTime created;
    PaperSize paper = PaperSize::A4;
};

/// Word's limit of a table's columns.
inline constexpr int MaxDocxColumns = 63;

QByteArray writeDocx(const PaperDocument &document, const DocxOptions &options, QStringList *warnings);

} // namespace brulion::io::detail
