// Paper exporters (io/paper.h): PDF painted by Qt's PDF writer (fonts embedded, the page size of
// ExportInput::paperSize) and DOCX written by Brulion (docx_p.h) — both from the paper document
// (paperdoc_p.h) — and printNotes() for the app's printer.
#include "io/docx_p.h"
#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/messages_p.h"
#include "io/paper.h"
#include "io/paperdoc_p.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QPagedPaintDevice>
#include <QPdfWriter>

namespace brulion::io {

using namespace Qt::StringLiterals;
using namespace detail;

PaperSize paperSizeFor(const QLocale &locale)
{
    switch (locale.territory()) {
    case QLocale::UnitedStates:
    case QLocale::Canada:
    case QLocale::Mexico:
    case QLocale::Philippines:
    case QLocale::Chile:
    case QLocale::Colombia:
    case QLocale::Venezuela:
    case QLocale::CostaRica:
    case QLocale::Guatemala:
    case QLocale::ElSalvador:
    case QLocale::PuertoRico:
    case QLocale::DominicanRepublic: return PaperSize::Letter;
    default: return PaperSize::A4;
    }
}

QString paperSizeId(PaperSize size)
{
    return size == PaperSize::Letter ? u"letter"_s : u"a4"_s;
}

PaperSize paperSizeFromId(QStringView id)
{
    return id.compare(u"letter", Qt::CaseInsensitive) == 0 ? PaperSize::Letter : PaperSize::A4;
}

QPageLayout paperLayout(PaperSize size)
{
    return QPageLayout(pageSizeOf(size), QPageLayout::Portrait, QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
}

int printNotes(const ExportInput &input, QPagedPaintDevice *device, QStringList *warnings)
{
    if (!device || !qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return 0;
    PaperDocument document(cleanExportInput(input), device->pageLayout());
    const int pages = document.paint(device);
    if (warnings)
        warnings->append(document.warnings());
    return pages;
}

namespace detail {

namespace {

/// "<name>-<date><extension>" like the other exporters, without parsing the notes again.
QString paperFileName(const ExportInput &input, const QString &extension)
{
    ExportInput named;
    named.vaultName = input.vaultName;
    named.now = input.now;
    return ExportVault(named).fileName({}, extension);
}

bool haveFonts(ExportResult &result)
{
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return true;
    result.warnings.append(IoMessages::tr("PDF and Word documents are made by the app (they need its fonts)."));
    return false;
}

class PdfExporter final : public Exporter {
public:
    QString id() const override { return u"pdf"_s; }
    QString name() const override { return IoMessages::tr("PDF document"); }
    QString fileExtension() const override { return u".pdf"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        ExportResult r;
        if (!haveFonts(r))
            return r;
        const QPageLayout layout = paperLayout(paperSizeFromId(input.paperSize));
        PaperDocument document(input, layout);
        QByteArray pdf;
        {
            QBuffer buffer(&pdf);
            if (!buffer.open(QIODevice::WriteOnly))
                return r;
            QPdfWriter writer(&buffer);
            // CSS pixels: the document's sizes (pictures, margins) are laid out as on paper.
            writer.setResolution(96);
            writer.setPageLayout(layout);
            writer.setTitle(document.title());
            writer.setCreator(u"Brulion"_s);
            writer.setPdfVersion(QPagedPaintDevice::PdfVersion_1_6);
            document.paint(&writer);
        }
        r.warnings += document.warnings();
        r.files.append(ExportedFile{paperFileName(input, u".pdf"_s), u"application/pdf"_s, pdf});
        return r;
    }
};

class DocxExporter final : public Exporter {
public:
    QString id() const override { return u"docx"_s; }
    QString name() const override { return IoMessages::tr("Word document (DOCX)"); }
    QString fileExtension() const override { return u".docx"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        ExportResult r;
        if (!haveFonts(r))
            return r;
        const PaperSize paper = paperSizeFromId(input.paperSize);
        PaperDocument document(input, paperLayout(paper));
        r.warnings += document.warnings();
        const QByteArray docx =
                writeDocx(document, {.title = document.title(), .created = input.now, .paper = paper}, &r.warnings);
        r.files.append(ExportedFile{paperFileName(input, u".docx"_s),
                                    u"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_s,
                                    docx});
        return r;
    }
};

} // namespace

std::unique_ptr<Exporter> makePdfExporter()
{
    return std::make_unique<PdfExporter>();
}

std::unique_ptr<Exporter> makeDocxExporter()
{
    return std::make_unique<DocxExporter>();
}

} // namespace detail

} // namespace brulion::io
