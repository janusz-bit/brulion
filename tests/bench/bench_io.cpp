// Benchmarks of the io module: HTML -> Markdown (QTextDocument path) for big lists / tables /
// typical pages at several sizes (super-linear scaling shows up between rows), importers
// (Markdown folder, Logseq graph, HTML files, Evernote ENEX, the Trilium fixtures, a ZIP of 1k
// notes) and exporters (Markdown / Obsidian / Logseq ZIP, HTML site, single HTML, JSON) on 1k
// notes.
#include "benchdata.h"

#include "io/io.h"

#include <QFile>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

QString htmlList(int items, bool nested)
{
    bench::Rng r{quint64(items)};
    QString h = u"<html><body><h1>Lista</h1><ul>"_s;
    for (int i = 0; i < items; ++i) {
        h += u"<li>"_s + bench::sentence(r, 3, 8).toHtmlEscaped();
        if (nested && i % 10 == 0)
            h += u"<ul><li>podpunkt a</li><li>podpunkt <b>b</b></li></ul>"_s;
        h += u"</li>"_s;
    }
    h += u"</ul></body></html>"_s;
    bench::reportInput(u"htmlList %1%2"_s.arg(items).arg(nested ? u" nested"_s : QString()), h);
    return h;
}

QString htmlParagraphs(int paragraphs)
{
    bench::Rng r(quint64(paragraphs) + 1);
    QString h = u"<html><head><title>Strona</title></head><body>"_s;
    for (int i = 0; i < paragraphs; ++i) {
        if (i % 10 == 0)
            h += u"<h2>Sekcja %1</h2>"_s.arg(i / 10);
        const QString text = bench::sentence(r).toHtmlEscaped();
        const QString bold = bench::sentence(r, 2, 4).toHtmlEscaped();
        h += u"<p>"_s + text + u" <a href=\"https://example.com/%1\">link</a> <b>"_s.arg(i) + bold
             + u"</b> <i>kursywa</i></p>"_s;
    }
    h += u"</body></html>"_s;
    bench::reportInput(u"htmlParagraphs %1"_s.arg(paragraphs), h);
    return h;
}

QString htmlTable(int rows)
{
    QString h = u"<table><tr><th>Pozycja</th><th>Ilość</th><th>Cena</th></tr>"_s;
    for (int i = 0; i < rows; ++i)
        h += u"<tr><td>Rzecz %1 żółw</td><td>%2</td><td>%3,50 zł</td></tr>"_s.arg(i).arg(i % 9).arg(i % 50);
    return h + u"</table>"_s;
}

QByteArray enex(int notes)
{
    bench::Rng r(3);
    QString xml = u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<en-export export-date=\"20261014T101010Z\" "
                  u"application=\"Evernote\" version=\"10.0\">\n"_s;
    for (int n = 0; n < notes; ++n) {
        QString body;
        for (int p = 0; p < 10; ++p)
            body += u"<div>"_s + bench::sentence(r).toHtmlEscaped() + u"</div>"_s;
        body += u"<ul><li>mleko</li><li>chleb</li><li><en-todo checked=\"true\"/>masło</li></ul>"_s;
        xml += u"<note><title>Notatka %1</title><created>20261014T080000Z</created><updated>20261015T093000Z</updated>"
               u"<tag>dom</tag><content><![CDATA[<en-note>%2</en-note>]]></content></note>\n"_s.arg(n)
                       .arg(body);
    }
    return (xml + u"</en-export>\n"_s).toUtf8();
}

QByteArray enexCode(int blocks)
{
    QString body;
    for (int b = 0; b < blocks; ++b)
        body += u"<div>Przykład %1:</div><div style=\"--en-codeblock:true;\"><div>int x = %1;</div><div>return x;</div></div>"_s
                        .arg(b);
    return (u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<en-export><note><title>Kod</title><content><![CDATA[<en-note>"_s
            + body + u"</en-note>]]></content></note></en-export>\n"_s)
            .toUtf8();
}

QList<InputFile> markdownFolder(int notes)
{
    QList<InputFile> files;
    for (const bench::VaultFile &f : bench::makeVault(notes, 21))
        files.append(InputFile{u"Eksport/"_s + f.path, f.content.toUtf8()});
    return files;
}

QList<InputFile> logseqGraph(int pages)
{
    QList<InputFile> files;
    files.append(InputFile{u"graf/logseq/config.edn"_s,
                           "{:meta/version 1\n :journal/page-title-format \"MMM do, yyyy\"}\n"});
    bench::Rng r(4);
    for (int i = 0; i < pages; ++i) {
        const int blocks = r.range(5, 40);
        const QString body = bench::outlineNote(r, blocks, pages, i);
        if (i % 3 == 0) {
            const QDate d = bench::fixedToday().addDays(-i);
            files.append(InputFile{u"graf/journals/%1.md"_s.arg(d.toString(u"yyyy_MM_dd"_s)), body.toUtf8()});
        } else {
            files.append(InputFile{u"graf/pages/Strona %1.md"_s.arg(i), (u"tags:: praca\n\n"_s + body).toUtf8()});
        }
    }
    return files;
}

QList<InputFile> htmlFiles(int files)
{
    QList<InputFile> out;
    for (int i = 0; i < files; ++i)
        out.append(InputFile{u"strony/strona-%1.html"_s.arg(i), htmlParagraphs(20 + i % 20).toUtf8()});
    return out;
}

ImportOptions importOptions()
{
    ImportOptions o;
    o.targetFolder = u"Import"_s;
    o.timeZone = QTimeZone("Europe/Warsaw");
    return o;
}

QByteArray readFixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_SOURCE_DIR) + u"/tests/core/io/fixtures/trilium/"_s + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

} // namespace

class BenchIo : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- HTML -> Markdown -----------------------------------------------------------------------

    void htmlToMarkdown_data()
    {
        QTest::addColumn<QString>("html");
        for (int n : bench::sizes({1000, 5000, 20000}, {1000, 2000, 5000}))
            QTest::newRow(qPrintable(u"flat list %1 items"_s.arg(n))) << htmlList(n, false);
        for (int n : bench::sizes({1000, 5000}, {1000, 2000}))
            QTest::newRow(qPrintable(u"nested list %1 items"_s.arg(n))) << htmlList(n, true);
        for (int n : bench::sizes({100, 1000, 5000}, {100, 1000}))
            QTest::newRow(qPrintable(u"%1 paragraphs"_s.arg(n))) << htmlParagraphs(n);
        for (int n : bench::sizes({100, 1000, 5000}, {100, 1000}))
            QTest::newRow(qPrintable(u"table %1 rows"_s.arg(n))) << htmlTable(n);
    }
    void htmlToMarkdown()
    {
        QFETCH(QString, html);
        QBENCHMARK {
            const HtmlMarkdown md = io::htmlToMarkdown(html);
            Q_UNUSED(md);
        }
    }

    // ---- importers ------------------------------------------------------------------------------

    void importers_data()
    {
        QTest::addColumn<QString>("importer");
        QTest::addColumn<QList<InputFile>>("files");
        const int n = bench::smallProfile() ? 300 : 1000;
        QTest::newRow(qPrintable(u"markdown %1 notes"_s.arg(n))) << u"markdown"_s << markdownFolder(n);
        QTest::newRow(qPrintable(u"logseq %1 pages"_s.arg(n))) << u"logseq"_s << logseqGraph(n);
        QTest::newRow("html 200 files") << u"html"_s << htmlFiles(200);
        QTest::newRow("evernote 200 notes")
                << u"evernote"_s << QList<InputFile>{InputFile{u"Notatki.enex"_s, enex(200)}};
        // One note with many code blocks (each one is a regex match from an offset + a replace).
        for (int blocks : {100, 1000})
            QTest::newRow(qPrintable(u"evernote 1 note, %1 code blocks"_s.arg(blocks)))
                    << u"evernote"_s << QList<InputFile>{InputFile{u"Kod.enex"_s, enexCode(blocks)}};
        QTest::newRow("trilium html.zip")
                << u"trilium"_s << QList<InputFile>{InputFile{u"html.zip"_s, readFixture(u"html.zip"_s)}};
        QTest::newRow("trilium markdown.zip")
                << u"trilium"_s << QList<InputFile>{InputFile{u"markdown.zip"_s, readFixture(u"markdown.zip"_s)}};
        // A ZIP of notes: unpacking (prepareImportFiles) + the Markdown importer.
        QList<FileEntry> entries;
        for (const InputFile &f : markdownFolder(n))
            entries.append(FileEntry{f.path, f.data});
        QTest::newRow(qPrintable(u"markdown zip %1 notes"_s.arg(n)))
                << u"markdown"_s << QList<InputFile>{InputFile{u"eksport.zip"_s, writeZip(entries)}};
    }
    void importers()
    {
        QFETCH(QString, importer);
        QFETCH(QList<InputFile>, files);
        const Importer *imp = importerById(importer);
        if (!imp || files.isEmpty() || files.first().data.isEmpty())
            QSKIP("importer or fixture not in this source tree (older baseline)");
        const ImportOptions options = importOptions();
        qsizetype produced = 0;
        QBENCHMARK {
            const ImportResult r = runImport(*imp, files, options);
            produced = r.files.size();
        }
        QVERIFY(produced > 0);
    }

    // ---- clipboard ------------------------------------------------------------------------------

    // The HTML flavour of copied blocks (io::clipboardHtml, Workspace::renderHtml): every block of
    // an outline selected and copied (bench_markdown selectAllCopy is the Markdown of it).
    void clipboardHtml_data()
    {
        QTest::addColumn<int>("blocks");
        for (int n : {500, 5000, 20000})
            QTest::newRow(qPrintable(u"%1 blocks"_s.arg(n))) << n;
    }
    void clipboardHtml()
    {
        QFETCH(int, blocks);
        bench::Rng r{quint64(blocks)};
        const QString markdown = bench::outlineNote(r, blocks, 500);
        bench::reportInput(u"clipboardHtml %1 blocks"_s.arg(blocks), markdown);
        ClipboardHtmlOptions options;
        options.blockText = [](const QString &id) { return u"Tekst bloku "_s + id; };
        qsizetype size = 0;
        QBENCHMARK {
            size = io::clipboardHtml(markdown, options).size();
        }
        QVERIFY(size > markdown.size());
    }

    // ---- exporters ------------------------------------------------------------------------------

    // The tag names the vault size (1000 notes; 300 in the small profile) so that runs of the two
    // profiles never compare different inputs under one name.
    void exporters_data()
    {
        QTest::addColumn<QString>("exporter");
        const int n = bench::smallProfile() ? 300 : 1000;
        for (const QString &id : {u"markdown-zip"_s, u"obsidian-zip"_s, u"logseq-zip"_s, u"html-site"_s,
                                  u"html-single"_s, u"json"_s, u"ics"_s, u"tasks-csv"_s})
            QTest::newRow(qPrintable(u"%1, %2 notes"_s.arg(id).arg(n))) << id;
    }
    void exporters()
    {
        QFETCH(QString, exporter);
        const Exporter *exp = exporterById(exporter);
        QVERIFY(exp);
        ExportInput input;
        for (const bench::VaultFile &f : bench::makeVault(bench::smallProfile() ? 300 : 1000, 31))
            input.files.append(FileEntry{f.path, f.content.toUtf8()});
        input.vaultName = u"Brulion"_s;
        input.now = bench::fixedNow();
        qsizetype bytes = 0;
        QBENCHMARK {
            const ExportResult r = runExport(*exp, input);
            bytes = r.files.isEmpty() ? 0 : r.files.first().data.size();
        }
        QVERIFY(bytes > 0);
    }
};

QTEST_MAIN(BenchIo)
#include "bench_io.moc"
