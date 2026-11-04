// Paper documents (io/paper.h): the paper size by locale, the paper document made of the HTML
// document export (checkbox markers, pictures, diagrams, links, tables, one note's title), the PDF
// export (pages, embedded fonts, paper sizes, links, pictures), the DOCX export read back (every
// part well-formed XML, styles, numbering, hyperlinks, tables, media) — and through LibreOffice
// when it is installed (skipped otherwise) —, printNotes() on a paged device, and hostile input:
// huge tables, deep nesting, broken pictures, giant diagrams.
#include "iotest.h"

#include "io/docx_p.h"
#include "io/exporter.h"
#include "io/exporters_p.h"
#include "io/paper.h"
#include "io/paperdoc_p.h"
#include "io/zip.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QImageReader>
#include <QPdfWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QXmlStreamReader>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

QByteArray png(int width, int height, QColor color = QColor(0x2c, 0x6a, 0xa3))
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

const QString Remont = u"# Remont łazienki\n"
                       u"\n"
                       u"Zażółć gęślą jaźń — **pogrubione**, *kursywa*, ~~skreślone~~, `kod`, ==ważne== i "
                       u"[strona](https://example.org/remont).\n"
                       u"\n"
                       u"## Zadania\n"
                       u"\n"
                       u"- [ ] Kupić płytki 📅 2026-11-20\n"
                       u"- [x] Zmierzyć ściany\n"
                       u"- Zwykły punkt\n"
                       u"  - Podpunkt ąę\n"
                       u"\n"
                       u"3. trzeci\n"
                       u"4. czwarty\n"
                       u"\n"
                       u"| Pomieszczenie | Koszt |\n"
                       u"| --- | ---: |\n"
                       u"| Łazienka | 1200 zł |\n"
                       u"| Kuchnia | 800 zł |\n"
                       u"\n"
                       u"```cpp\n"
                       u"int żółw = 1;\n"
                       u"return żółw;\n"
                       u"```\n"
                       u"\n"
                       u"> Cytat o ścianach\n"
                       u"\n"
                       u"Wzór $x^2 + y_1$ i link do [[Zakupy]].\n"
                       u"\n"
                       u"![[assets/płytka.png]]\n"
                       u"\n"
                       u"```mermaid\n"
                       u"flowchart LR\n"
                       u"  A[Zakupy] --> B[Montaż]\n"
                       u"```\n"_s;

ExportInput page(const QString &content = Remont)
{
    ExportInput in;
    in.files = {iotest::text(u"Projekty/Remont łazienki.md"_s, content), iotest::text(u"Zakupy.md"_s, u"- mleko\n"_s),
                iotest::bytes(u"assets/płytka.png"_s, png(1600, 800))};
    in.vaultName = u"Remont łazienki"_s;
    in.now = QDateTime(QDate(2026, 11, 3), QTime(10, 30), iotest::warsaw());
    return in;
}

/// Only the page of `in` (its attachments too): the page export's input.
ExportInput onlyPage(ExportInput in)
{
    in.files.removeIf([](const FileEntry &f) { return f.path == u"Zakupy.md"; });
    return in;
}

ExportResult exportAs(const QString &id, const ExportInput &input)
{
    const Exporter *e = exporterById(id);
    Q_ASSERT(e);
    return runExport(*e, input);
}

QString allText(const QTextDocument &doc)
{
    return doc.toPlainText();
}

bool hasSvg()
{
    return QImageReader::supportedImageFormats().contains("svg");
}

/// Every XML part of a ZIP well-formed; the parts by path.
QHash<QString, QByteArray> docxParts(const QByteArray &docx)
{
    QHash<QString, QByteArray> out;
    QStringList warnings;
    for (const FileEntry &f : readZip(docx, &warnings))
        out.insert(f.path, f.data);
    if (!warnings.isEmpty())
        qWarning() << warnings;
    return out;
}

QString xmlError(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
        reader.readNext();
    return reader.hasError() ? reader.errorString() + u" at line "_s + QString::number(reader.lineNumber()) : QString();
}

/// The text of the w:t elements of a WordprocessingML part.
QString wordText(const QByteArray &xml)
{
    QString out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.qualifiedName() == u"w:t")
            out += reader.readElementText();
        else if (reader.isEndElement() && reader.qualifiedName() == u"w:p")
            out += u'\n';
    }
    return out;
}

int pdfPages(const QByteArray &pdf)
{
    static const QRegularExpression pageObject(u"/Type\\s*/Page\\b(?!s)"_s);
    return int(QString::fromLatin1(pdf).count(pageObject));
}

} // namespace

class TestIoPaper : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void paperSizes_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("paper");
        QTest::newRow("Polska") << u"pl_PL"_s << u"a4"_s;
        QTest::newRow("USA") << u"en_US"_s << u"letter"_s;
        QTest::newRow("Kanada") << u"fr_CA"_s << u"letter"_s;
        QTest::newRow("Meksyk") << u"es_MX"_s << u"letter"_s;
        QTest::newRow("Wielka Brytania") << u"en_GB"_s << u"a4"_s;
        QTest::newRow("Niemcy") << u"de_DE"_s << u"a4"_s;
        QTest::newRow("C") << u"C"_s << u"a4"_s;
    }
    void paperSizes()
    {
        QFETCH(QString, locale);
        QFETCH(QString, paper);
        QCOMPARE(paperSizeId(paperSizeFor(QLocale(locale))), paper);
        QCOMPARE(paperSizeFromId(paper), paperSizeFor(QLocale(locale)));
    }

    void initTestCase()
    {
        qInfo("SVG pictures: %s", hasSvg() ? "yes" : "no (the qsvg plugin is missing)");
        // BRULION_PAPER_SAMPLES=<folder>: the fixture's PDF and DOCX kept there for a look.
        if (const QString dir = qEnvironmentVariable("BRULION_PAPER_SAMPLES"); !dir.isEmpty()) {
            for (const QString &id : {u"pdf"_s, u"docx"_s}) {
                const ExportedFile f = exportAs(id, onlyPage(page())).files.first();
                QFile out(dir + u'/' + f.name);
                QVERIFY(out.open(QIODevice::WriteOnly));
                out.write(f.data);
            }
        }
    }

    void paperIds()
    {
        QCOMPARE(paperSizeFromId(u"LETTER"), PaperSize::Letter);
        QCOMPARE(paperSizeFromId(u"b5"), PaperSize::A4);
        QCOMPARE(paperSizeFromId(u""), PaperSize::A4);
        const QPageLayout a4 = paperLayout(PaperSize::A4);
        QCOMPARE(a4.pageSize().id(), QPageSize::A4);
        QCOMPARE(a4.margins(QPageLayout::Millimeter), QMarginsF(20, 20, 20, 20));
        QCOMPARE(paperLayout(PaperSize::Letter).pageSize().id(), QPageSize::Letter);
    }

    // ---- The paper document ------------------------------------------------------------------

    void documentOfOnePage()
    {
        detail::PaperDocument paper(detail::cleanExportInput(onlyPage(page())), paperLayout(PaperSize::A4));
        QTextDocument *doc = paper.document();
        QCOMPARE(paper.title(), u"Remont łazienki"_s);
        QCOMPARE(paper.noteCount(), 1);
        const QString text = allText(*doc);
        // The title once: no notebook heading, no contents, the repeated "# Remont łazienki" gone.
        QCOMPARE(text.count(u"Remont łazienki"_s), 1);
        QVERIFY(!text.contains(u"Contents"_s));
        QVERIFY(text.contains(u"Zażółć gęślą jaźń"_s));
        QVERIFY2(!text.contains(u"☐"_s) && !text.contains(u"☑"_s), qPrintable(text));
        QVERIFY(!text.contains(u"<"_s));
        // Dates in words: emoji fonts are colour bitmaps a PDF does not show.
        QVERIFY2(text.contains(u"due: 2026-11-20"_s), qPrintable(text));
        QVERIFY(!text.contains(u"📅"_s));

        int unchecked = 0;
        int checked = 0;
        int headings = 0;
        int code = 0;
        bool superscript = false;
        bool externalLink = false;
        bool internalAnchor = false;
        bool bold = false;
        bool strike = false;
        QList<int> listStarts;
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            const QTextBlockFormat bf = b.blockFormat();
            if (bf.marker() == QTextBlockFormat::MarkerType::Unchecked)
                ++unchecked;
            if (bf.marker() == QTextBlockFormat::MarkerType::Checked)
                ++checked;
            if (bf.headingLevel() > 0)
                ++headings;
            if (bf.nonBreakableLines())
                ++code;
            if (QTextList *list = b.textList(); list && list->format().style() == QTextListFormat::ListDecimal
                                                && !listStarts.contains(list->format().start()))
                listStarts.append(list->format().start());
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat cf = it.fragment().charFormat();
                superscript |= cf.verticalAlignment() == QTextCharFormat::AlignSuperScript;
                bold |= cf.fontWeight() >= QFont::Bold && b.blockFormat().headingLevel() == 0;
                strike |= cf.fontStrikeOut();
                if (cf.isAnchor() && cf.anchorHref() == u"https://example.org/remont")
                    externalLink = true;
                if (cf.isAnchor() && cf.anchorHref().startsWith(u'#'))
                    internalAnchor = true;
            }
        }
        QCOMPARE(unchecked, 1);
        QCOMPARE(checked, 1);
        QVERIFY(headings >= 2);
        QVERIFY(code >= 1);
        QVERIFY(superscript);
        QVERIFY(bold);
        QVERIFY(strike);
        QVERIFY(externalLink);
        QVERIFY(!internalAnchor);       // [[Zakupy]]: text, no link into nowhere
        QCOMPARE(listStarts.size(), 1); // the numbered list

        // The table: its rows and its header.
        QTextTable *table = nullptr;
        for (QTextFrame *f : doc->rootFrame()->childFrames()) {
            if ((table = qobject_cast<QTextTable *>(f)))
                break;
        }
        QVERIFY(table);
        QCOMPARE(table->rows(), 3);
        QCOMPARE(table->columns(), 2);
        QCOMPARE(table->format().headerRowCount(), 1);

        // The picture (scaled to the text width) and the diagram.
        QVERIFY(paper.pictures().size() >= 1);
        bool photo = false;
        for (const detail::PaperPicture &p : paper.pictures()) {
            QVERIFY(!p.image.isNull());
            QVERIFY(p.format == u"png" || p.format == u"jpeg");
            QVERIFY(p.size.width() <= paper.textWidth() + 0.5);
            if (p.image.size() == QSize(1600, 800)) {
                photo = true;
                QCOMPARE(qRound(p.size.width()), qRound(paper.textWidth()));
                QCOMPARE(qRound(p.size.height() * 2), qRound(paper.textWidth()));
            }
        }
        QVERIFY(photo);
        if (hasSvg()) {
            QCOMPARE(paper.pictures().size(), 2);
            QVERIFY(!text.contains(u"flowchart LR"_s));
            QVERIFY(!text.contains(u"Diagram source"_s));
        } else {
            QVERIFY(text.contains(u"flowchart LR"_s));
        }
        QVERIFY2(paper.warnings().isEmpty(), qPrintable(paper.warnings().join(u'\n')));
    }

    void chipsInWords_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("words");
        QTest::newRow("due") << u"- [ ] Płytki 📅 2026-11-20\n"_s << u"due: 2026-11-20"_s;
        QTest::newRow("scheduled") << u"- [ ] Płytki ⏳ 2026-11-18\n"_s << u"scheduled: 2026-11-18"_s;
        QTest::newRow("start") << u"- [ ] Płytki 🛫 2026-11-17\n"_s << u"starts: 2026-11-17"_s;
        QTest::newRow("done") << u"- [x] Płytki ✅ 2026-11-02\n"_s << u"done: 2026-11-02"_s;
        QTest::newRow("priority") << u"- [ ] Płytki ⏫\n"_s << u"priority: high"_s;
        QTest::newRow("recurring task") << u"- [ ] Śmieci 📅 2026-11-04 🔁 every week\n"_s << u"repeats: every week"_s;
        QTest::newRow("reminder") << u"Zadzwonić ⏰ 2026-11-04 09:00\n"_s << u"reminder: 2026-11-04 09:00"_s;
        QTest::newRow("alarm") << u"Pobudka ⏰! 2026-11-04 06:30\n"_s << u"alarm: 2026-11-04 06:30"_s;
        QTest::newRow("emoji of the text stays") << u"Kot 🐈 śpi\n"_s << u"Kot 🐈 śpi"_s;
    }
    void chipsInWords()
    {
        QFETCH(QString, content);
        QFETCH(QString, words);
        ExportInput in;
        in.files = {iotest::text(u"Daty.md"_s, content)};
        detail::PaperDocument paper(detail::cleanExportInput(in), paperLayout(PaperSize::A4));
        const QString text = allText(*paper.document());
        QVERIFY2(text.contains(words), qPrintable(text));
        for (const QString &emoji : {u"📅"_s, u"⏳"_s, u"🛫"_s, u"✅"_s, u"⏫"_s, u"⏰"_s, u"🔁"_s})
            QVERIFY2(!text.contains(emoji), qPrintable(text));
    }

    void documentOfSeveralNotes()
    {
        detail::PaperDocument paper(detail::cleanExportInput(page()), paperLayout(PaperSize::A4));
        QCOMPARE(paper.noteCount(), 2);
        QCOMPARE(paper.title(), u"Remont łazienki"_s); // the notebook's name given
        int breaks = 0;
        for (QTextBlock b = paper.document()->begin(); b.isValid(); b = b.next()) {
            if (b.blockFormat().pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore)
                ++breaks;
        }
        QCOMPARE(breaks, 1); // the second note on its own page
        QVERIFY(allText(*paper.document()).contains(u"mleko"_s));
    }

    void documentLoadsOnlyItsPictures_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("shown");
        QTest::newRow("picture from the internet") << u"![kot](https://example.org/kot.png)\n"_s << u"kot"_s;
        QTest::newRow("local file") << u"![passwd](file:///etc/passwd)\n"_s << u"passwd"_s;
        QTest::newRow("app resource") << u"![ikona](qrc:/icons/x.svg)\n"_s << u"ikona"_s;
        QTest::newRow("missing attachment") << u"![[nie ma.png]]\n"_s << u"nie ma.png"_s;
        QTest::newRow("raw html") << u"<img src=\"/etc/hostname\">\n"_s << u"<img"_s;
    }
    void documentLoadsOnlyItsPictures()
    {
        QFETCH(QString, content);
        QFETCH(QString, shown);
        ExportInput in;
        in.files = {iotest::text(u"Obrazy.md"_s, content)};
        detail::PaperDocument paper(detail::cleanExportInput(in), paperLayout(PaperSize::A4));
        QVERIFY(paper.pictures().isEmpty());
        QVERIFY2(allText(*paper.document()).contains(shown), qPrintable(allText(*paper.document())));
        // Nothing in the document is a picture (no broken-picture icon either).
        for (QTextBlock b = paper.document()->begin(); b.isValid(); b = b.next()) {
            for (auto it = b.begin(); !it.atEnd(); ++it)
                QVERIFY(!it.fragment().charFormat().isImageFormat());
        }
        QVERIFY(paper.document()->resource(QTextDocument::ImageResource, QUrl(u"file:///etc/passwd"_s)).isNull());
    }

    void brokenPictures_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("not a picture") << QByteArray("\x89PNG-to-nie-jest-obraz");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("cut png") << png(40, 40).left(60);
        QTest::newRow("svg error") << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect");
    }
    void brokenPictures()
    {
        QFETCH(QByteArray, data);
        ExportInput in;
        in.files = {iotest::text(u"Zdjęcia.md"_s, u"Przed ![[zepsuty.png]] po\n\n![[zepsuty.svg]]\n"_s),
                    iotest::bytes(u"zepsuty.png"_s, data), iotest::bytes(u"zepsuty.svg"_s, data)};
        for (const QString &id : {u"pdf"_s, u"docx"_s}) {
            const ExportResult r = exportAs(id, in);
            QCOMPARE(r.files.size(), 1);
            QVERIFY(!r.files.first().data.isEmpty());
            QVERIFY2(!r.warnings.isEmpty(), qPrintable(id)); // the picture could not be shown
        }
        detail::PaperDocument paper(detail::cleanExportInput(in), paperLayout(PaperSize::A4));
        QVERIFY(allText(*paper.document()).contains(u"Przed"_s));
        QVERIFY(allText(*paper.document()).contains(u"po"_s));
        QVERIFY(paper.pictures().isEmpty());
    }

    // ---- PDF ---------------------------------------------------------------------------------

    void pdf()
    {
        const ExportResult r = exportAs(u"pdf"_s, onlyPage(page()));
        QCOMPARE(r.files.size(), 1);
        const ExportedFile &f = r.files.first();
        QCOMPARE(f.name, u"Remont łazienki-2026-11-03.pdf"_s);
        QCOMPARE(f.mime, u"application/pdf"_s);
        QVERIFY(f.data.startsWith("%PDF-1.6"));
        QVERIFY(f.data.trimmed().endsWith("%%EOF"));
        QVERIFY(pdfPages(f.data) >= 1);
        // Fonts embedded (TrueType / CFF subsets), the picture, the link, the title.
        QVERIFY(f.data.contains("/FontFile2") || f.data.contains("/FontFile3"));
        QVERIFY(f.data.contains("/Subtype /Image") || f.data.contains("/Subtype/Image"));
        QVERIFY(f.data.contains("https://example.org/remont"));
        QVERIFY(!f.data.contains("data:image"));
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(u'\n')));
    }

    void pdfPaperSizes_data()
    {
        QTest::addColumn<QString>("paper");
        QTest::addColumn<QSizeF>("points");
        QTest::newRow("A4") << u"a4"_s << QSizeF(595.3, 841.9);
        QTest::newRow("Letter") << u"letter"_s << QSizeF(612, 792);
        QTest::newRow("unknown: A4") << u"tabloid"_s << QSizeF(595.3, 841.9);
    }
    void pdfPaperSizes()
    {
        QFETCH(QString, paper);
        QFETCH(QSizeF, points);
        ExportInput in = onlyPage(page());
        in.paperSize = paper;
        const QByteArray pdf = exportAs(u"pdf"_s, in).files.first().data;
        static const QRegularExpression box(u"/MediaBox \\[0 0 ([\\d.]+) ([\\d.]+)\\]"_s);
        const auto m = box.match(QString::fromLatin1(pdf));
        QVERIFY(m.hasMatch());
        QVERIFY(qAbs(m.captured(1).toDouble() - points.width()) < 1);
        QVERIFY(qAbs(m.captured(2).toDouble() - points.height()) < 1);
    }

    void pdfOfManyNotesHasAPagePerNote()
    {
        ExportInput in;
        for (int i = 0; i < 5; ++i)
            in.files.append(iotest::text(u"Notatka %1.md"_s.arg(i), u"Treść notatki ąę %1\n"_s.arg(i)));
        const ExportResult r = exportAs(u"pdf"_s, in);
        QCOMPARE(pdfPages(r.files.first().data), 5);
    }

    void printOnAPagedDevice()
    {
        QByteArray out;
        QBuffer buffer(&out);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setResolution(96);
        writer.setPageLayout(paperLayout(PaperSize::Letter));
        QStringList warnings;
        const int pages = printNotes(page(), &writer, &warnings);
        QVERIFY(pages >= 2); // two notes, one page each at least
        buffer.close();
        QCOMPARE(pdfPages(out), pages);
        QVERIFY(warnings.isEmpty());
        QCOMPARE(printNotes(page(), nullptr), 0);
    }

    // ---- DOCX --------------------------------------------------------------------------------

    void docx()
    {
        const ExportResult r = exportAs(u"docx"_s, onlyPage(page()));
        QCOMPARE(r.files.size(), 1);
        const ExportedFile &f = r.files.first();
        QCOMPARE(f.name, u"Remont łazienki-2026-11-03.docx"_s);
        QCOMPARE(f.mime, u"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_s);
        QVERIFY(isZip(f.data));
        const QHash<QString, QByteArray> parts = docxParts(f.data);
        for (const QString &part : {u"[Content_Types].xml"_s, u"_rels/.rels"_s, u"word/document.xml"_s,
                                    u"word/styles.xml"_s, u"word/numbering.xml"_s, u"word/settings.xml"_s,
                                    u"word/_rels/document.xml.rels"_s, u"docProps/core.xml"_s, u"docProps/app.xml"_s}) {
            QVERIFY2(parts.contains(part), qPrintable(part));
            QVERIFY2(xmlError(parts.value(part)).isEmpty(), qPrintable(part + u": "_s + xmlError(parts.value(part))));
        }
        const QByteArray document = parts.value(u"word/document.xml"_s);
        const QString text = wordText(document);
        QVERIFY2(text.contains(u"Zażółć gęślą jaźń"_s), qPrintable(text));
        QCOMPARE(text.count(u"Remont łazienki"_s), 1);
        QVERIFY(text.contains(u"int żółw = 1;"_s));
        QVERIFY(text.contains(u"1200 zł"_s));
        QVERIFY(!text.contains(u"☐"_s));
        // Styles of headings, code, quotes; character styles of code and links.
        QVERIFY(document.contains("<w:pStyle w:val=\"Heading1\"/>"));
        QVERIFY(document.contains("<w:pStyle w:val=\"Heading2\"/>"));
        QVERIFY(document.contains("<w:pStyle w:val=\"SourceCode\"/>"));
        QVERIFY(document.contains("<w:rStyle w:val=\"VerbatimChar\"/>"));
        QVERIFY(document.contains("<w:b/>"));
        QVERIFY(document.contains("<w:i/>"));
        QVERIFY(document.contains("<w:strike/>"));
        QVERIFY(document.contains("<w:vertAlign w:val=\"superscript\"/>"));
        // Lists: the task's checkboxes, bullets on two levels, numbers from 3.
        QVERIFY(document.contains("<w:numId w:val=\"2\"/>"));
        QVERIFY(document.contains("<w:numId w:val=\"3\"/>"));
        QVERIFY(document.contains("<w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/>"));
        const QByteArray numbering = parts.value(u"word/numbering.xml"_s);
        QVERIFY(document.contains("<w:numId w:val=\"4\"/>")); // the numbered list
        QVERIFY(numbering.contains("<w:num w:numId=\"4\"><w:abstractNumId w:val=\"1\"/>"));
        QVERIFY(numbering.contains("☐") && numbering.contains("☑"));
        // The table with its header row.
        QVERIFY(document.contains("<w:tbl>"));
        QVERIFY(document.contains("<w:tblHeader/>"));
        QCOMPARE(document.count("<w:gridCol "), 2);
        // The link to the web; the page link is text.
        const QByteArray rels = parts.value(u"word/_rels/document.xml.rels"_s);
        QVERIFY(rels.contains("Target=\"https://example.org/remont\" TargetMode=\"External\""));
        QVERIFY(document.contains("<w:hyperlink r:id="));
        QVERIFY(text.contains(u"Zakupy"_s));
        // Pictures: the photo (its own PNG) and the diagram.
        QVERIFY(document.contains("<a:blip r:embed="));
        QVERIFY(parts.contains(u"word/media/image1.png"_s));
        QCOMPARE(parts.value(u"word/media/image1.png"_s), png(1600, 800));
        if (hasSvg())
            QVERIFY(parts.contains(u"word/media/image2.png"_s));
        QVERIFY(parts.value(u"[Content_Types].xml"_s).contains("Extension=\"png\""));
        QVERIFY(parts.value(u"docProps/core.xml"_s).contains("<dc:title>Remont łazienki</dc:title>"));
        QVERIFY(parts.value(u"docProps/core.xml"_s).contains("2026-11-03T09:30:00Z"));
        // The page size: A4 by default, Letter when asked.
        QVERIFY(document.contains("<w:pgSz w:w=\"11906\" w:h=\"16838\"/>"));
        ExportInput letter = onlyPage(page());
        letter.paperSize = u"letter"_s;
        QVERIFY(docxParts(exportAs(u"docx"_s, letter).files.first().data)
                        .value(u"word/document.xml"_s)
                        .contains("<w:pgSz w:w=\"12240\" w:h=\"15840\"/>"));
    }

    void docxEscapesEverything_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("expected");
        QTest::newRow("markup") << u"<w:p>& \"cudzysłów\" </w:body>\n"_s << u"<w:p>& \"cudzysłów\" </w:body>"_s;
        QTest::newRow("control characters") << u"a\u0001b\u0008c\u001Fd\n"_s << u"abcd"_s;
        QTest::newRow("lone surrogate") << (u"x"_s + QChar(0xD800) + u"y\n"_s) << u"xy"_s;
        QTest::newRow("emoji") << u"Kot 🐈 ąę\n"_s << u"Kot 🐈 ąę"_s;
        QTest::newRow("link with quotes") << u"[a](https://example.org/?q=\"x\"&y=1)\n"_s << u"a"_s;
    }
    void docxEscapesEverything()
    {
        QFETCH(QString, content);
        QFETCH(QString, expected);
        ExportInput in;
        in.files = {iotest::text(u"Wrogi.md"_s, content)};
        const auto parts = docxParts(exportAs(u"docx"_s, in).files.first().data);
        for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
            if (it.key().endsWith(u".xml") || it.key().endsWith(u".rels"))
                QVERIFY2(xmlError(it.value()).isEmpty(), qPrintable(it.key() + u": "_s + xmlError(it.value())));
        }
        QVERIFY2(wordText(parts.value(u"word/document.xml"_s)).contains(expected),
                 qPrintable(wordText(parts.value(u"word/document.xml"_s))));
    }

    void docxColumnsAreCapped()
    {
        QStringList header;
        QStringList delimiter;
        QStringList row;
        for (int c = 0; c < 80; ++c) {
            header.append(u"K%1"_s.arg(c));
            delimiter.append(u"---"_s);
            row.append(u"ż%1"_s.arg(c));
        }
        ExportInput in;
        in.files = {iotest::text(u"Szeroka.md"_s, u"| "_s + header.join(u" | "_s) + u" |\n| "_s
                                                          + delimiter.join(u" | "_s) + u" |\n| "_s + row.join(u" | "_s)
                                                          + u" |\n"_s)};
        const ExportResult r = exportAs(u"docx"_s, in);
        const QByteArray document = docxParts(r.files.first().data).value(u"word/document.xml"_s);
        QCOMPARE(document.count("<w:gridCol "), detail::MaxDocxColumns);
        QVERIFY(xmlError(document).isEmpty());
        QVERIFY(!r.warnings.isEmpty());
    }

    void docxThroughLibreOffice()
    {
        const QString soffice = QStandardPaths::findExecutable(u"soffice"_s);
        if (soffice.isEmpty())
            QSKIP("LibreOffice (soffice) is not installed");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(u"remont.docx"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(exportAs(u"docx"_s, onlyPage(page())).files.first().data);
        file.close();

        QProcess process;
        // Its own profile: never the user's LibreOffice settings.
        process.start(soffice,
                      {u"-env:UserInstallation="_s + QUrl::fromLocalFile(dir.filePath(u"profile"_s)).toString(),
                       u"--headless"_s, u"--norestore"_s, u"--convert-to"_s, u"txt:Text (encoded):UTF8"_s,
                       u"--outdir"_s, dir.path(), file.fileName()});
        if (!process.waitForFinished(240000)) {
            process.kill();
            QSKIP("LibreOffice did not finish in time");
        }
        QFile converted(dir.filePath(u"remont.txt"_s));
        // LibreOffice ran: a document it cannot open is a broken package.
        QVERIFY2(converted.open(QIODevice::ReadOnly), process.readAllStandardError().constData());
        const QString text = QString::fromUtf8(converted.readAll());
        QVERIFY2(text.contains(u"Zażółć gęślą jaźń"_s), qPrintable(text));
        QVERIFY(text.contains(u"Kupić płytki"_s));
        QVERIFY(text.contains(u"1200 zł"_s));
        QVERIFY(text.contains(u"int żółw = 1;"_s));
        QVERIFY(!text.contains(u"Diagram source"_s));
    }

    // ---- Hostile input -------------------------------------------------------------------------

    void hostile_data()
    {
        QTest::addColumn<QString>("content");
        QString huge = u"| a | b | c | d | e |\n| - | - | - | - | - |\n"_s;
        for (int r = 0; r < 6000; ++r)
            huge += u"| ż%1 | ó | ł | ę | ą |\n"_s.arg(r);
        QTest::newRow("huge table") << huge;
        QString deep;
        for (int d = 0; d < 60; ++d)
            deep += QString(d * 2, u' ') + u"- poziom "_s + QString::number(d) + u'\n';
        QTest::newRow("deep list") << deep;
        QString quotes;
        for (int d = 0; d < 40; ++d)
            quotes += QString(d + 1, u'>') + u" cytat "_s + QString::number(d) + u"\n\n"_s;
        QTest::newRow("deep quotes") << quotes;
        QString tasks;
        for (int d = 0; d < 30; ++d)
            tasks += QString(d * 2, u' ') + (d % 2 ? u"- [x] zrobione "_s : u"- [ ] do zrobienia "_s)
                     + QString::number(d) + u'\n';
        QTest::newRow("deep tasks") << tasks;
        QTest::newRow("long word") << QString(20000, u'ż') + u'\n';
        QTest::newRow("giant diagram") << u"```mermaid\nflowchart TD\n"_s + [] {
            QString s;
            for (int i = 0; i < 400; ++i)
                s += u"  N%1[Węzeł %1] --> N%2\n"_s.arg(i).arg(i + 1);
            return s;
        }() + u"```\n"_s;
        QTest::newRow("broken mermaid") << u"```mermaid\nflowchart LR\n  A[nie zamknięte --> B\n```\n"_s;
        QTest::newRow("raw html") << u"<table><tr><td>x</td></tr></table>\n<svg><script>alert(1)</script></svg>\n"_s;
        QTest::newRow("empty") << QString();
    }
    void hostile()
    {
        QFETCH(QString, content);
        ExportInput in;
        in.files = {iotest::text(u"Wroga strona.md"_s, content)};
        QElapsedTimer timer;
        timer.start();
        for (const QString &id : {u"pdf"_s, u"docx"_s}) {
            QElapsedTimer one;
            one.start();
            const ExportResult r = exportAs(id, in);
            qInfo("%s %s: %lld ms", QTest::currentDataTag(), qPrintable(id), qint64(one.elapsed()));
            QCOMPARE(r.files.size(), 1);
            const QByteArray data = r.files.first().data;
            if (id == u"pdf") {
                QVERIFY(data.startsWith("%PDF-"));
                QVERIFY(pdfPages(data) >= 1);
            } else {
                const auto parts = docxParts(data);
                QVERIFY(xmlError(parts.value(u"word/document.xml"_s)).isEmpty());
                QVERIFY(xmlError(parts.value(u"word/numbering.xml"_s)).isEmpty());
            }
        }
        qInfo("%s: %lld ms", QTest::currentDataTag(), qint64(timer.elapsed()));
    }

    void hugeTableIsCut()
    {
        QString huge = u"| a | b |\n| - | - |\n"_s;
        for (int r = 0; r < 30000; ++r)
            huge += u"| %1 | x |\n"_s.arg(r);
        ExportInput in;
        in.files = {iotest::text(u"Tabela.md"_s, huge)};
        detail::PaperDocument paper(detail::cleanExportInput(in), paperLayout(PaperSize::A4));
        QTextTable *table = nullptr;
        for (QTextFrame *f : paper.document()->rootFrame()->childFrames()) {
            if ((table = qobject_cast<QTextTable *>(f)))
                break;
        }
        QVERIFY(table);
        QCOMPARE(table->rows(), detail::PaperDocument::MaxTableRows);
        QCOMPARE(paper.warnings().size(), 1);
        // "(28501 more rows are left out)": the header and 30000 rows.
        QVERIFY(allText(*paper.document()).contains(QString::number(30001 - detail::PaperDocument::MaxTableRows)));
        // A wide table keeps fewer rows.
        QString wide = u"|"_s + QString(u" k |"_s).repeated(50) + u"\n|"_s + QString(u" - |"_s).repeated(50) + u'\n';
        for (int r = 0; r < 1000; ++r)
            wide += u"|"_s + QString(u" ż |"_s).repeated(50) + u'\n';
        in.files = {iotest::text(u"Szeroka.md"_s, wide)};
        detail::PaperDocument widePaper(detail::cleanExportInput(in), paperLayout(PaperSize::A4));
        table = nullptr;
        for (QTextFrame *f : widePaper.document()->rootFrame()->childFrames()) {
            if ((table = qobject_cast<QTextTable *>(f)))
                break;
        }
        QVERIFY(table);
        QCOMPARE(table->columns(), 50);
        QCOMPARE(table->rows(), detail::PaperDocument::MaxTableCells / 50);
    }

    void deepListsAreClamped()
    {
        QString deep;
        for (int d = 0; d < 30; ++d)
            deep += QString(d * 2, u' ') + u"- poziom "_s + QString::number(d) + u'\n';
        ExportInput in;
        in.files = {iotest::text(u"Głęboko.md"_s, deep)};
        detail::PaperDocument paper(detail::cleanExportInput(in), paperLayout(PaperSize::A4));
        int deepest = 0;
        for (QTextBlock b = paper.document()->begin(); b.isValid(); b = b.next()) {
            if (QTextList *l = b.textList())
                deepest = std::max(deepest, l->format().indent());
            QVERIFY(b.blockFormat().leftMargin() <= paper.textWidth() * detail::PaperDocument::MaxIndentFraction + 0.5);
        }
        QVERIFY(deepest >= 2);
        QVERIFY(deepest <= detail::PaperDocument::MaxListIndent);
        QVERIFY(allText(*paper.document()).contains(u"poziom 29"_s));
    }
};

QTEST_MAIN(TestIoPaper)
#include "tst_io_paper.moc"
