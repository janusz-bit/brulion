#include "io/docx_p.h"

#include "io/messages_p.h"
#include "io/paperdoc_p.h"
#include "io/zip.h"

#include <QFontDatabase>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

#include <algorithm>
#include <cmath>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr qreal TwipsPerCssPixel = 15;  // 1440 per inch, 96 px per inch
constexpr qint64 EmuPerCssPixel = 9525; // 914400 per inch

constexpr int BulletNum = 1;
constexpr int UncheckedNum = 2;
constexpr int CheckedNum = 3;
constexpr int FirstOrderedNum = 4;

/// Text for XML: escaped, without the characters XML 1.0 forbids (control characters, lone
/// surrogates, U+FFFE / U+FFFF).
QString xmlText(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            out += c;
            out += text[++i];
            continue;
        }
        if (c.isSurrogate() || c.unicode() == 0xFFFE || c.unicode() == 0xFFFF)
            continue;
        switch (c.unicode()) {
        case u'&': out += u"&amp;"; break;
        case u'<': out += u"&lt;"; break;
        case u'>': out += u"&gt;"; break;
        case u'"': out += u"&quot;"; break;
        default:
            if (c.unicode() < 0x20 && c != u'\t' && c != u'\n' && c != u'\r')
                break;
            out += c;
        }
    }
    return out;
}

QString hex(const QColor &color)
{
    return color.name(QColor::HexRgb).mid(1).toUpper();
}

QString num(qint64 value)
{
    return QString::number(value);
}

bool isExternalLink(const QString &href)
{
    return href.startsWith(u"http://", Qt::CaseInsensitive) || href.startsWith(u"https://", Qt::CaseInsensitive)
           || href.startsWith(u"mailto:", Qt::CaseInsensitive);
}

class Writer {
public:
    Writer(const PaperDocument &document, const DocxOptions &options, QStringList *warnings)
        : m_document(document)
        , m_options(options)
        , m_warnings(warnings)
        , m_mono(QFontDatabase::systemFont(QFontDatabase::FixedFont).family())
    { }

    QByteArray run()
    {
        QString body;
        writeItems(m_document.document()->rootFrame()->begin(), body);
        const bool letter = m_options.paper == PaperSize::Letter; // else A4; the page's size in twips below
        const QString document =
                uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"><w:body>)"_s
                + body + u"<w:sectPr><w:pgSz w:w=\""_s + num(letter ? 12240 : 11906) + u"\" w:h=\""_s
                + num(letter ? 15840 : 16838)
                + u"\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" w:header=\"567\" "
                  u"w:footer=\"567\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>"_s;

        QList<FileEntry> entries = {
                {u"[Content_Types].xml"_s, contentTypes().toUtf8()},
                {u"_rels/.rels"_s, packageRels().toUtf8()},
                {u"docProps/core.xml"_s, coreProperties().toUtf8()},
                {u"docProps/app.xml"_s, QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"><Application>Brulion</Application></Properties>)")},
                {u"word/document.xml"_s, document.toUtf8()},
                {u"word/styles.xml"_s, styles().toUtf8()},
                {u"word/numbering.xml"_s, numbering().toUtf8()},
                {u"word/settings.xml"_s, QByteArray(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:settings xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:defaultTabStop w:val="720"/><w:compat><w:compatSetting w:name="compatibilityMode" w:uri="http://schemas.microsoft.com/office/word" w:val="15"/></w:compat></w:settings>)")},
                {u"word/_rels/document.xml.rels"_s, documentRels().toUtf8()},
        };
        entries.append(m_media);
        // Every entry deflated: LibreOffice refuses a package with stored entries that have data
        // descriptors (what the writer gives pictures it stores).
        return writeZip(entries, {.storeCompressedFormats = false, .modified = m_options.created});
    }

private:
    // ---- Content ------------------------------------------------------------------------------

    /// The tables, frames and paragraphs from `it` on; true when the last of them is a paragraph.
    bool writeItems(QTextFrame::iterator it, QString &out)
    {
        bool endsWithParagraph = false;
        for (; !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(child))
                    writeTable(table, out);
                else
                    writeItems(child->begin(), out);
                endsWithParagraph = false;
            } else if (it.currentBlock().isValid()) {
                writeBlock(it.currentBlock(), out);
                endsWithParagraph = true;
            }
        }
        return endsWithParagraph;
    }

    void writeCell(const QTextTableCell &cell, QString &out)
    {
        // A cell ends with a paragraph (also after a table in it).
        if (!writeItems(cell.begin(), out))
            out += u"<w:p/>"_s;
    }

    void writeTable(QTextTable *table, QString &out)
    {
        const int columns = std::min(table->columns(), MaxDocxColumns);
        if (table->columns() > MaxDocxColumns && m_warnings)
            m_warnings->append(IoMessages::tr("A table has more columns than Word allows: only its first %1 are in "
                                              "the document.")
                                       .arg(MaxDocxColumns));
        if (columns <= 0 || table->rows() <= 0)
            return;
        const qint64 width = qint64(m_document.textWidth() * TwipsPerCssPixel);
        const qint64 columnWidth = std::max<qint64>(1, width / columns);
        const QString border = u"w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"BDB6A6\""_s;
        out += u"<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/><w:tblBorders><w:top "_s + border + u"/><w:left "_s
               + border + u"/><w:bottom "_s + border + u"/><w:right "_s + border + u"/><w:insideH "_s + border
               + u"/><w:insideV "_s + border
               + u"/></w:tblBorders><w:tblCellMar><w:left w:w=\"80\" w:type=\"dxa\"/><w:right w:w=\"80\" "
                 u"w:type=\"dxa\"/></w:tblCellMar><w:tblLook w:val=\"04A0\" w:firstRow=\"1\" w:lastRow=\"0\" "
                 u"w:firstColumn=\"0\" w:lastColumn=\"0\" w:noHBand=\"0\" w:noVBand=\"1\"/></w:tblPr><w:tblGrid>"_s;
        for (int c = 0; c < columns; ++c)
            out += u"<w:gridCol w:w=\""_s + num(columnWidth) + u"\"/>"_s;
        out += u"</w:tblGrid>"_s;
        const int headerRows = table->format().headerRowCount();
        for (int r = 0; r < table->rows(); ++r) {
            out += u"<w:tr>"_s;
            if (r < headerRows)
                out += u"<w:trPr><w:tblHeader/></w:trPr>"_s;
            for (int c = 0; c < columns;) {
                const QTextTableCell cell = table->cellAt(r, c);
                if (!cell.isValid()) {
                    out += u"<w:tc><w:tcPr><w:tcW w:w=\""_s + num(columnWidth)
                           + u"\" w:type=\"dxa\"/></w:tcPr><w:p/></w:tc>"_s;
                    ++c;
                    continue;
                }
                const int span = std::clamp(cell.columnSpan() - (c - cell.column()), 1, columns - c);
                QString properties = u"<w:tcW w:w=\""_s + num(columnWidth * span) + u"\" w:type=\"dxa\"/>"_s;
                if (span > 1)
                    properties += u"<w:gridSpan w:val=\""_s + num(span) + u"\"/>"_s;
                if (cell.rowSpan() > 1)
                    properties += cell.row() == r ? u"<w:vMerge w:val=\"restart\"/>"_s : u"<w:vMerge/>"_s;
                if (r < headerRows)
                    properties += u"<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"F3EEE2\"/>"_s;
                out += u"<w:tc><w:tcPr>"_s + properties + u"</w:tcPr>"_s;
                if (cell.row() == r)
                    writeCell(cell, out);
                else
                    out += u"<w:p/>"_s; // covered by the cell above
                out += u"</w:tc>"_s;
                c += span;
            }
            out += u"</w:tr>"_s;
        }
        out += u"</w:tbl>"_s;
    }

    struct Numbering {
        int id = 0; ///< w:numId, 0 outside lists
        int level = 0;
    };

    Numbering numberingOf(const QTextBlock &block)
    {
        const QTextList *list = block.textList();
        if (!list)
            return {};
        const QTextListFormat format = list->format();
        const int level = std::clamp(format.indent() - 1, 0, 8);
        switch (block.blockFormat().marker()) {
        case QTextBlockFormat::MarkerType::Checked: return {CheckedNum, level};
        case QTextBlockFormat::MarkerType::Unchecked: return {UncheckedNum, level};
        case QTextBlockFormat::MarkerType::NoMarker: break;
        }
        const QTextListFormat::Style style = format.style();
        const bool ordered = style == QTextListFormat::ListDecimal || style == QTextListFormat::ListLowerAlpha
                             || style == QTextListFormat::ListUpperAlpha || style == QTextListFormat::ListLowerRoman
                             || style == QTextListFormat::ListUpperRoman;
        if (!ordered)
            return {BulletNum, level};
        if (const auto it = m_orderedNums.constFind(list); it != m_orderedNums.constEnd())
            return {*it, level};
        const int id = FirstOrderedNum + int(m_ordered.size());
        m_ordered.append({level, std::max(1, format.start())});
        m_orderedNums.insert(list, id);
        return {id, level};
    }

    void writeBlock(const QTextBlock &block, QString &out)
    {
        const QTextBlockFormat bf = block.blockFormat();
        QString properties;
        QString style;
        const int heading = bf.headingLevel();
        if (heading >= 1 && heading <= 6)
            style = u"Heading"_s + num(heading);
        else if (bf.nonBreakableLines())
            style = u"SourceCode"_s;
        else if (bf.intProperty(QTextFormat::BlockQuoteLevel) > 0)
            style = u"Quote"_s;
        if (!style.isEmpty())
            properties += u"<w:pStyle w:val=\""_s + style + u"\"/>"_s;
        if (bf.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
            properties += u"<w:pageBreakBefore/>"_s;
        if (const auto [id, level] = numberingOf(block); id != 0)
            properties += u"<w:numPr><w:ilvl w:val=\""_s + num(level) + u"\"/><w:numId w:val=\""_s + num(id)
                          + u"\"/></w:numPr>"_s;
        if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
            properties +=
                    u"<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" w:color=\"BDB6A6\"/></w:pBdr>"_s;
        if (bf.background().style() != Qt::NoBrush && style.isEmpty())
            properties +=
                    u"<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""_s + hex(bf.background().color()) + u"\"/>"_s;
        if (!block.textList() && bf.leftMargin() > 0 && style != u"SourceCode")
            properties += u"<w:ind w:left=\""_s + num(qint64(bf.leftMargin() * TwipsPerCssPixel)) + u"\"/>"_s;
        if (bf.alignment() & Qt::AlignHCenter)
            properties += u"<w:jc w:val=\"center\"/>"_s;
        else if (bf.alignment() & Qt::AlignRight)
            properties += u"<w:jc w:val=\"right\"/>"_s;
        out += u"<w:p>"_s;
        if (!properties.isEmpty())
            out += u"<w:pPr>"_s + properties + u"</w:pPr>"_s;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid())
                writeFragment(fragment, heading > 0, out);
        }
        out += u"</w:p>"_s;
    }

    QString runProperties(const QTextCharFormat &cf, bool link, bool heading) const
    {
        QString out;
        const QStringList families = cf.fontFamilies().toStringList();
        const bool mono = cf.fontFixedPitch() || families.contains(m_mono) || families.contains(u"Courier New"_s);
        if (link)
            out += u"<w:rStyle w:val=\"Hyperlink\"/>"_s;
        else if (mono)
            out += u"<w:rStyle w:val=\"VerbatimChar\"/>"_s;
        if (cf.fontWeight() >= QFont::DemiBold && !heading)
            out += u"<w:b/>"_s;
        if (cf.fontItalic())
            out += u"<w:i/>"_s;
        if (cf.fontStrikeOut())
            out += u"<w:strike/>"_s;
        if (cf.fontUnderline() && !link)
            out += u"<w:u w:val=\"single\"/>"_s;
        if (!link && cf.foreground().style() != Qt::NoBrush) {
            const QColor color = cf.foreground().color();
            if (color.isValid() && color != QColor(Qt::black) && color != QColor(0x1f, 0x24, 0x30))
                out += u"<w:color w:val=\""_s + hex(color) + u"\"/>"_s;
        }
        if (!heading && cf.hasProperty(QTextFormat::FontPointSize) && cf.fontPointSize() > 0
            && cf.fontPointSize() < 10.5)
            out += u"<w:sz w:val=\""_s + num(qRound(cf.fontPointSize() * 2)) + u"\"/>"_s;
        if (!mono && cf.background().style() != Qt::NoBrush)
            out += u"<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""_s + hex(cf.background().color()) + u"\"/>"_s;
        if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript)
            out += u"<w:vertAlign w:val=\"superscript\"/>"_s;
        else if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript)
            out += u"<w:vertAlign w:val=\"subscript\"/>"_s;
        return out.isEmpty() ? QString() : u"<w:rPr>"_s + out + u"</w:rPr>"_s;
    }

    void writeFragment(const QTextFragment &fragment, bool heading, QString &out)
    {
        const QTextCharFormat cf = fragment.charFormat();
        const QString text = fragment.text();
        if (cf.isImageFormat()) {
            for (qsizetype i = 0; i < text.size(); ++i)
                out += drawing(cf.toImageFormat());
            return;
        }
        const QString href = cf.isAnchor() ? cf.anchorHref() : QString();
        const bool link = isExternalLink(href);
        const QString rPr = runProperties(cf, link, heading);
        QString runs;
        QString pending;
        const auto flush = [&] {
            if (!pending.isEmpty())
                runs += u"<w:r>"_s + rPr + u"<w:t xml:space=\"preserve\">"_s + xmlText(pending) + u"</w:t></w:r>"_s;
            pending.clear();
        };
        for (const QChar c : text) {
            if (c == QChar::LineSeparator || c == u'\n' || c == QChar::ParagraphSeparator) {
                flush();
                runs += u"<w:r>"_s + rPr + u"<w:br/></w:r>"_s;
            } else if (c == u'\t') {
                flush();
                runs += u"<w:r>"_s + rPr + u"<w:tab/></w:r>"_s;
            } else if (c != QChar::ObjectReplacementCharacter) {
                pending += c;
            }
        }
        flush();
        if (link)
            out += u"<w:hyperlink r:id=\""_s + linkRelation(href) + u"\" w:history=\"1\">"_s + runs
                   + u"</w:hyperlink>"_s;
        else
            out += runs;
    }

    QString linkRelation(const QString &href)
    {
        if (const auto it = m_links.constFind(href); it != m_links.constEnd())
            return *it;
        const QString id = u"rId"_s + num(m_nextRelation++);
        m_relations += u"<Relationship Id=\""_s + id
                       + u"\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
                         u"Target=\""_s
                       + xmlText(href) + u"\" TargetMode=\"External\"/>"_s;
        m_links.insert(href, id);
        return id;
    }

    QString drawing(const QTextImageFormat &format)
    {
        const auto it = m_document.pictures().constFind(format.name());
        if (it == m_document.pictures().constEnd() || it->data.isEmpty())
            return {};
        QString relation = m_pictureRelations.value(format.name());
        if (relation.isEmpty()) {
            relation = u"rId"_s + num(m_nextRelation++);
            const QString file =
                    u"image"_s + num(m_media.size() + 1) + (it->format == u"jpeg" ? u".jpeg"_s : u".png"_s);
            m_media.append(FileEntry{u"word/media/"_s + file, it->data});
            m_relations += u"<Relationship Id=\""_s + relation
                           + u"\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                             u"Target=\"media/"_s
                           + file + u"\"/>"_s;
            m_pictureRelations.insert(format.name(), relation);
        }
        const QSizeF size =
                format.width() > 0 && format.height() > 0 ? QSizeF(format.width(), format.height()) : it->size;
        const QString cx = num(std::max<qint64>(1, std::llround(size.width() * EmuPerCssPixel)));
        const QString cy = num(std::max<qint64>(1, std::llround(size.height() * EmuPerCssPixel)));
        const QString id = num(++m_drawings);
        return u"<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\""_s + cx
               + u"\" cy=\""_s + cy + u"\"/><wp:docPr id=\""_s + id + u"\" name=\"Picture "_s + id + u"\" descr=\""_s
               + xmlText(it->alt)
               + u"\"/><wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
                 u"<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                 u"<pic:pic><pic:nvPicPr><pic:cNvPr id=\""_s
               + id + u"\" name=\"Picture "_s + id
               + u"\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\""_s + relation
               + u"\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
                 u"<a:ext cx=\""_s
               + cx + u"\" cy=\""_s + cy
               + u"\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>"
                 u"</a:graphicData></a:graphic></wp:inline></w:drawing></w:r>"_s;
    }

    // ---- Package parts -------------------------------------------------------------------------

    QString contentTypes() const
    {
        return uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/><Default Extension="jpeg" ContentType="image/jpeg"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/><Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/><Override PartName="/word/settings.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"/><Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/><Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/></Types>)"_s;
    }

    QString packageRels() const
    {
        return uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/></Relationships>)"_s;
    }

    QString documentRels() const
    {
        return uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings" Target="settings.xml"/>)"_s
               + m_relations + u"</Relationships>"_s;
    }

    QString coreProperties() const
    {
        QString out = uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"><dc:title>)"_s
                      + xmlText(m_options.title) + u"</dc:title>"_s;
        if (m_options.created.isValid()) {
            const QString when = m_options.created.toUTC().toString(u"yyyy-MM-ddTHH:mm:ssZ"_s);
            out += u"<dcterms:created xsi:type=\"dcterms:W3CDTF\">"_s + when
                   + u"</dcterms:created><dcterms:modified xsi:type=\"dcterms:W3CDTF\">"_s + when
                   + u"</dcterms:modified>"_s;
        }
        return out + u"</cp:coreProperties>"_s;
    }

    QString styles() const
    {
        QString headings;
        static constexpr int sizes[] = {36, 30, 26, 23, 21, 21};
        for (int level = 1; level <= 6; ++level) {
            headings += u"<w:style w:type=\"paragraph\" w:styleId=\"Heading"_s + num(level)
                        + u"\"><w:name w:val=\"heading "_s + num(level)
                        + u"\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:uiPriority w:val=\"9\"/>"
                          u"<w:qFormat/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before=\""_s
                        + num(level <= 2 ? 280 : 200) + u"\" w:after=\"100\"/><w:outlineLvl w:val=\""_s + num(level - 1)
                        + u"\"/></w:pPr><w:rPr><w:b/><w:color w:val=\"1F2430\"/><w:sz w:val=\""_s
                        + num(sizes[level - 1]) + u"\"/></w:rPr></w:style>"_s;
        }
        return uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Calibri" w:cs="Calibri"/><w:sz w:val="22"/><w:szCs w:val="22"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="120" w:line="264" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style><w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:spacing w:after="200"/></w:pPr><w:rPr><w:b/><w:sz w:val="44"/></w:rPr></w:style>)"_s
               + headings
               + uR"(<w:style w:type="paragraph" w:styleId="Quote"><w:name w:val="Quote"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:pBdr><w:left w:val="single" w:sz="18" w:space="8" w:color="DDD6C6"/></w:pBdr><w:ind w:left="360"/></w:pPr><w:rPr><w:color w:val="535A6A"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="SourceCode"><w:name w:val="Source Code"/><w:basedOn w:val="Normal"/><w:qFormat/><w:pPr><w:shd w:val="clear" w:color="auto" w:fill="F3EEE2"/><w:spacing w:after="0" w:line="240" w:lineRule="auto"/></w:pPr><w:rPr><w:rFonts w:ascii="Consolas" w:hAnsi="Consolas" w:cs="Consolas"/><w:sz w:val="19"/></w:rPr></w:style><w:style w:type="character" w:default="1" w:styleId="DefaultParagraphFont"><w:name w:val="Default Paragraph Font"/><w:uiPriority w:val="1"/><w:semiHidden/></w:style><w:style w:type="character" w:styleId="VerbatimChar"><w:name w:val="Verbatim Char"/><w:basedOn w:val="DefaultParagraphFont"/><w:rPr><w:rFonts w:ascii="Consolas" w:hAnsi="Consolas" w:cs="Consolas"/><w:sz w:val="20"/><w:shd w:val="clear" w:color="auto" w:fill="F3EEE2"/></w:rPr></w:style><w:style w:type="character" w:styleId="Hyperlink"><w:name w:val="Hyperlink"/><w:basedOn w:val="DefaultParagraphFont"/><w:rPr><w:color w:val="2C6AA3"/><w:u w:val="single"/></w:rPr></w:style><w:style w:type="table" w:default="1" w:styleId="TableNormal"><w:name w:val="Normal Table"/><w:tblPr><w:tblInd w:w="0" w:type="dxa"/><w:tblCellMar><w:top w:w="0" w:type="dxa"/><w:left w:w="108" w:type="dxa"/><w:bottom w:w="0" w:type="dxa"/><w:right w:w="108" w:type="dxa"/></w:tblCellMar></w:tblPr></w:style></w:styles>)"_s;
    }

    /// Abstract numbering `id`: nine levels of `format` ("bullet", "decimal") whose text is
    /// `text(level)`, in font `font` (none: the paragraph's).
    static QString abstractNum(int id, QStringView format, QString (*text)(int), const QString &font = {})
    {
        QString runFont;
        if (!font.isEmpty())
            runFont = u"<w:rPr><w:rFonts w:ascii=\""_s + font + u"\" w:hAnsi=\""_s + font + u"\" w:cs=\""_s + font
                      + u"\"/></w:rPr>"_s;
        QString out = u"<w:abstractNum w:abstractNumId=\""_s + num(id)
                      + u"\"><w:multiLevelType w:val=\"hybridMultilevel\"/>"_s;
        for (int i = 0; i < 9; ++i) {
            out += u"<w:lvl w:ilvl=\""_s + num(i) + u"\"><w:start w:val=\"1\"/><w:numFmt w:val=\""_s + format
                   + u"\"/><w:lvlText w:val=\""_s + text(i) + u"\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\""_s
                   + num(720 * (i + 1)) + u"\" w:hanging=\"360\"/></w:pPr>"_s + runFont + u"</w:lvl>"_s;
        }
        return out + u"</w:abstractNum>"_s;
    }

    QString numbering() const
    {
        const QString symbols = u"Segoe UI Symbol"_s; // has the checkboxes
        QString out = uR"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">)"_s;
        out += abstractNum(0, u"bullet", [](int level) -> QString {
            static const QString bullets[] = {u"•"_s, u"◦"_s, u"▪"_s};
            return bullets[level % 3];
        });
        out += abstractNum(1, u"decimal", [](int level) -> QString { return u'%' + num(level + 1) + u'.'; });
        out += abstractNum(2, u"bullet", [](int) { return u"☐"_s; }, symbols);
        out += abstractNum(3, u"bullet", [](int) { return u"☑"_s; }, symbols);
        out += u"<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
               u"<w:num w:numId=\"2\"><w:abstractNumId w:val=\"2\"/></w:num>"
               u"<w:num w:numId=\"3\"><w:abstractNumId w:val=\"3\"/></w:num>"_s;
        for (qsizetype i = 0; i < m_ordered.size(); ++i) {
            out += u"<w:num w:numId=\""_s + num(FirstOrderedNum + i)
                   + u"\"><w:abstractNumId w:val=\"1\"/><w:lvlOverride w:ilvl=\""_s + num(m_ordered[i].level)
                   + u"\"><w:startOverride w:val=\""_s + num(m_ordered[i].start) + u"\"/></w:lvlOverride></w:num>"_s;
        }
        return out + u"</w:numbering>"_s;
    }

    const PaperDocument &m_document;
    const DocxOptions &m_options;
    QStringList *m_warnings;
    QString m_mono;
    QString m_relations;
    int m_nextRelation = 10;
    QHash<QString, QString> m_links;
    QHash<QString, QString> m_pictureRelations;
    QList<FileEntry> m_media;
    int m_drawings = 0;
    QHash<const QTextList *, int> m_orderedNums;
    struct OrderedList {
        int level = 0;
        int start = 1;
    };
    QList<OrderedList> m_ordered; ///< the numbered lists: w:numId FirstOrderedNum + index
};

} // namespace

QByteArray writeDocx(const PaperDocument &document, const DocxOptions &options, QStringList *warnings)
{
    return Writer(document, options, warnings).run();
}

} // namespace brulion::io::detail
