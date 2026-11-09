// The paper document behind the PDF and DOCX exporters and printNotes() (io/paper.h): the HTML
// document export ("html-single") of the notes made into a QTextDocument for paper. Internal to
// the io module.
//
// The HTML (our own renderer's, every note text escaped) is adapted first, on its exact tags:
//   <input type="checkbox" …>   ☐ / ☑, then the list item's checkbox marker (QTextBlockFormat)
//   <figure class="mermaid">    the diagram's SVG as a picture (its <style> of dark colours
//                               dropped); the diagram's source as code when it cannot be drawn
//   <svg>…</svg>                other inline SVG (charts…) as a picture, or left out (a warning)
//   <img src="data:…">          the picture decoded (image/* only) and scaled to the text width
//                               and page height; a picture from the internet without a copy is
//                               its address as a link ("Picture: …"), a broken one its text
//   <a href="#…|data:…">        links inside the document and to embedded attachments become
//                               text in the link colour (http, https, mailto stay links)
//   <mark> <del> <audio> <video> what QTextDocument understands
//   <table>                     borders; one larger than MaxTableRows / MaxTableCells keeps its
//                               first rows
// The document loads only those pictures (loadResource: never a file, the app's resources or the
// network). Its one note: the TOC and the notebook's heading are left out, a first heading that
// repeats the title too; several: each note after the first starts a page.
#pragma once

#include "io/exporter.h"
#include "io/paper.h"

#include <QHash>
#include <QImage>
#include <QPageLayout>
#include <QSizeF>
#include <QStringList>

#include <memory>

class QPagedPaintDevice;
class QTextDocument;

namespace brulion::io::detail {

/// A picture of the paper document.
struct PaperPicture {
    QImage image;
    /// What the DOCX embeds: PNG or JPEG bytes (the attachment's own when it is one of those).
    QByteArray data;
    QString format; ///< "png" | "jpeg"
    /// The size it is shown at, in CSS pixels (1/96 inch).
    QSizeF size;
    QString alt;
};

class PaperDocument {
public:
    /// The most rows and cells of one table on paper (larger tables keep their first rows: Qt
    /// lays a table out across pages in time that grows with the square of its rows).
    static constexpr int MaxTableRows = 1500;
    static constexpr int MaxTableCells = 10000;
    /// The deepest list level and the widest indentation (a fraction of the text width).
    static constexpr int MaxListIndent = 8;
    static constexpr qreal MaxIndentFraction = 0.35;
    /// The largest side of a picture kept for paper, in pixels (larger ones are scaled down).
    static constexpr int MaxPictureSide = 2400;

    /// The notes of `input` (cleaned: runExport()) for pages of `layout`.
    PaperDocument(const ExportInput &input, const QPageLayout &layout);
    ~PaperDocument();
    PaperDocument(const PaperDocument &) = delete;
    PaperDocument &operator=(const PaperDocument &) = delete;

    QTextDocument *document() const;
    /// The pictures by their resource names ("brulion-picture:1"…).
    const QHash<QString, PaperPicture> &pictures() const { return m_pictures; }
    const QStringList &warnings() const { return m_warnings; }
    /// The note's title (one note), else the notebook's name.
    QString title() const { return m_title; }
    int noteCount() const { return m_notes; }
    const QPageLayout &layout() const { return m_layout; }
    /// The text width of the layout in CSS pixels.
    qreal textWidth() const;

    /// Lays the document out for `device` and paints it page by page (a page number at the
    /// bottom); the number of pages painted.
    int paint(QPagedPaintDevice *device);

private:
    QString adapt(QString body);
    QString pictureHtml(const QString &src, const QString &alt);
    QString svgPicture(const QString &svg, const QString &alt);
    QString addPicture(QImage image, QByteArray data, QString format, const QString &alt);
    void finish();

    QPageLayout m_layout;
    QHash<QString, PaperPicture> m_pictures; // outlives m_document, which loads them
    std::unique_ptr<QTextDocument> m_document;
    QStringList m_warnings;
    QString m_title;
    int m_notes = 0;
};

/// The page size of a paper size.
QPageSize pageSizeOf(PaperSize size);

} // namespace brulion::io::detail
