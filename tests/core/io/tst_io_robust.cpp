// Robustness: malformed, hostile and pathological input for every importer and exporter. Nothing
// may crash or hang, and every produced path must obey the path rules.
#include "iotest.h"

#include "io/exporter.h"
#include "io/htmlmarkdown.h"
#include "io/importer.h"
#include "io/tar.h"
#include "io/zip.h"
#include "util/paths.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

QByteArray randomBytes(QRandomGenerator &rng, qsizetype size)
{
    QByteArray out(size, '\0');
    for (char &c : out)
        c = char(rng.bounded(256));
    return out;
}

/// Text a hostile note could contain.
QStringList pathologicalTexts()
{
    QStringList out;
    out.append(u"[["_s.repeated(5000) + u"]]"_s.repeated(10));
    out.append(u"(("_s.repeated(5000) + u"x"_s);
    out.append(u"> "_s.repeated(300) + u"deep quote"_s);
    QString list;
    for (int i = 0; i < 300; ++i)
        list += QString(i, u'\t') + u"- [ ] item "_s + QString::number(i) + u'\n';
    out.append(list);
    out.append(u"```\nunclosed fence\n[[link]]\n"_s);
    out.append(u"---\nkey: ["_s.repeated(500) + u"\n"_s);
    out.append(QString(10000, u'#'));
    out.append(u"*_~="_s.repeated(5000));
    out.append(u"- TODO 📅 2026-13-45 ⏰ 9999-99-99 🔁 every ⏳ ⏫⏫⏫ ^ ^^ ^-\n  SCHEDULED: <"_s.repeated(200));
    out.append(QString(QChar(0xD800)) + u"lone surrogate [[a"_s + QChar(0xDC00) + u"]]"_s);
    out.append(u"nul"_s + QChar(u'\0') + u"byte [[x]]"_s);
    out.append(u"| a | b |\n"_s + u"| --- |\n"_s.repeated(1000));
    out.append(u"#tag/"_s.repeated(3000));
    out.append(u"![[](()))]]![](<>)[](#)[^]^^__**~~==%%"_s.repeated(500));
    out.append(u"<script>alert(1)</script><img src=x onerror=alert(1)>"_s.repeated(100));
    return out;
}

QList<QList<InputFile>> hostileInputs()
{
    QRandomGenerator rng(20261015);
    QList<QList<InputFile>> sets;
    const QStringList names = {u"a.md"_s,
                               u"notes.json"_s,
                               u"export.enex"_s,
                               u"cal.ics"_s,
                               u"x.zip"_s,
                               u"x.jex"_s,
                               u"index.html"_s,
                               u"db.csv"_s,
                               u"n.txt"_s,
                               u"B.textbundle/text.md"_s,
                               u"logseq/config.edn"_s,
                               u"pages/a.md"_s,
                               u"journals/2026_10_14.md"_s,
                               u"Keep/n.json"_s,
                               u"0123456789abcdef0123456789abcdef.md"_s,
                               u"Page 0123456789abcdef0123456789abcdef.md"_s};
    // Random bytes under every interesting name.
    for (int round = 0; round < 3; ++round) {
        QList<InputFile> set;
        for (const QString &n : names)
            set.append(iotest::bytes(n, randomBytes(rng, rng.bounded(1, 3000))));
        sets.append(set);
    }
    // Truncated archives.
    QList<FileEntry> entries;
    for (const QString &n : names)
        entries.append(FileEntry{n, randomBytes(rng, 500)});
    const QByteArray zip = writeZip(entries);
    for (const double cut : {0.1, 0.5, 0.9, 0.99})
        sets.append({iotest::bytes(u"cut.zip"_s, zip.left(qsizetype(zip.size() * cut)))});
    QList<iotest::TarMember> members;
    for (const QString &n : names)
        members.append(iotest::TarMember{n.toUtf8(), randomBytes(rng, 700), '0', {}});
    const QByteArray tar = iotest::tarArchive(members);
    sets.append({iotest::bytes(u"cut.jex"_s, tar.left(tar.size() / 2))});
    // A tar header claiming a huge size.
    QByteArray huge = iotest::tarArchive({{"big.md", "x", '0', {}}});
    huge.replace(124, 11, "77777777777");
    sets.append({iotest::bytes(u"huge.tar"_s, huge)});
    // Deeply nested archives.
    QByteArray nested = writeZip({{u"deep.md"_s, "# głęboko"}});
    for (int i = 0; i < 8; ++i)
        nested = writeZip({{u"level.zip"_s, nested}});
    sets.append({iotest::bytes(u"nested.zip"_s, nested)});
    // Hostile structures for the structured formats.
    sets.append({iotest::text(u"a.enex"_s, u"<en-export>"_s + u"<note><title>x"_s.repeated(1000))});
    sets.append({iotest::text(u"a.ics"_s, u"BEGIN:VCALENDAR\n"_s + u"BEGIN:VEVENT\nDTSTART:2026"_s.repeated(2000))});
    sets.append({iotest::text(u"Keep/a.json"_s, u"["_s.repeated(10000))});
    sets.append({iotest::text(u"roam.json"_s, u"[{\"title\":\"a\",\"children\":"_s.repeated(2000))});
    sets.append({iotest::text(u"site.html"_s, u"<script>"_s.repeated(5000) + u"<div>"_s.repeated(3000)
                                                      + u"<table><tr><td>"_s.repeated(500))});
    sets.append({iotest::text(u"source/notes.json"_s, u"{\"activeNotes\":[1,\"x\",null,{\"content\":42}]}"_s)});
    sets.append({iotest::text(u"g.json"_s, u"[{\"title\":5,\"children\":[{\"string\":7,\"children\":\"x\"}]}]"_s)});
    // Pathological Markdown under the names the importers look at.
    QList<InputFile> notes;
    int k = 0;
    for (const QString &t : pathologicalTexts()) {
        notes.append(iotest::text(u"pages/p"_s + QString::number(k) + u".md"_s, t));
        notes.append(iotest::text(u"journals/2026_10_"_s + QString::number(10 + k % 10) + u".md"_s, t));
        ++k;
    }
    notes.append(iotest::text(u"logseq/config.edn"_s, u"{:journal/page-title-format \"''''\"}"_s));
    sets.append(notes);
    // Paths trying to escape the vault.
    sets.append({iotest::text(u"../../../etc/passwd.md"_s, u"x"_s), iotest::text(u"/abs/olute.md"_s, u"y"_s),
                 iotest::text(u".brulion/config.json"_s, u"{}"_s), iotest::text(u"a/../../b.md"_s, u"z"_s),
                 iotest::text(u"C:\\Windows\\x.md"_s, u"w"_s), iotest::text(u"con.md"_s, u"c"_s)});
    return sets;
}

void verifyPaths(const ImportResult &r, const QString &context)
{
    for (const ImportedFile &f : r.files) {
        const QString msg = context + u": "_s + f.path;
        QVERIFY2(!f.path.isEmpty(), qPrintable(msg));
        QVERIFY2(!f.path.startsWith(u'/'), qPrintable(msg));
        QVERIFY2(!f.path.split(u'/').contains(u".."_s), qPrintable(msg));
        QVERIFY2(!paths::isIgnored(f.path), qPrintable(msg));
        QVERIFY2(paths::normalize(f.path) == f.path, qPrintable(msg));
        QVERIFY2(!f.path.contains(u'\\'), qPrintable(msg));
    }
}

} // namespace

class TestIoRobust : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void importersSurviveHostileInput()
    {
        const QList<QList<InputFile>> sets = hostileInputs();
        for (const Importer *importer : importers()) {
            for (qsizetype s = 0; s < sets.size(); ++s) {
                ImportOptions o = iotest::options(u"Import"_s);
                const ImportResult r = runImport(*importer, sets[s], o);
                verifyPaths(r, importer->id() + u" set "_s + QString::number(s));
                if (QTest::currentTestFailed())
                    return;
            }
        }
        // Detection on hostile input never crashes either.
        for (const QList<InputFile> &set : sets)
            detectImporter(set);
    }

    void exportersSurvivePathologicalNotes()
    {
        ExportInput in;
        int k = 0;
        for (const QString &t : pathologicalTexts()) {
            in.files.append(iotest::text(u"p"_s + QString::number(k) + u".md"_s, t));
            in.files.append(iotest::text(u"journals/2026-10-"_s + QString::number(10 + k % 10) + u".md"_s, t));
            ++k;
        }
        in.files.append(iotest::bytes(u"assets/bin.png"_s, QByteArray(1000, '\xFF')));
        in.files.append(iotest::text(u"../escape.md"_s, u"[[p1]]"_s));
        in.now = QDateTime(QDate(2026, 10, 15), QTime(12, 0), iotest::warsaw());
        for (const Exporter *e : exporters()) {
            const ExportResult r = runExport(*e, in);
            QCOMPARE(r.files.size(), 1);
            QVERIFY2(!r.files.first().data.isEmpty(), qPrintable(e->id()));
            if (r.files.first().mime == u"application/zip") {
                QStringList warnings;
                const QList<FileEntry> entries = readZip(r.files.first().data, &warnings);
                QVERIFY2(warnings.isEmpty(), qPrintable(e->id()));
                for (const FileEntry &f : entries)
                    QVERIFY2(!f.path.contains(u".."_s), qPrintable(f.path));
            }
        }
    }

    void htmlExportNeverEmitsActiveContent()
    {
        ExportInput in;
        in.files = {iotest::text(u"x.md"_s, pathologicalTexts().last()
                                                    + u"\n\n[a](javascript:alert(1)) [b](data:text/html,x) "
                                                      "<iframe src=x></iframe> ![c](vbscript:x)"_s)};
        for (const QString &id : {u"html-site"_s, u"html-single"_s}) {
            const ExportedFile f = runExport(*exporterById(id), in).files.first();
            QString html;
            if (f.mime == u"application/zip") {
                for (const FileEntry &e : readZip(f.data)) {
                    if (e.path.endsWith(u"x.html"_s))
                        html = QString::fromUtf8(e.data);
                }
            } else {
                html = QString::fromUtf8(f.data);
            }
            QVERIFY(!html.isEmpty());
            QVERIFY2(!html.contains(u"<script"_s, Qt::CaseInsensitive), qPrintable(id));
            QVERIFY2(!html.contains(u"<iframe"_s, Qt::CaseInsensitive), qPrintable(id));
            QVERIFY2(!html.contains(u"onerror="_s) || html.contains(u"onerror=alert(1)&gt;"_s), qPrintable(id));
            QVERIFY2(!html.contains(u"href=\"javascript:"_s), qPrintable(id));
            QVERIFY2(!html.contains(u"href=\"data:text"_s), qPrintable(id));
            QVERIFY2(!html.contains(u"src=\"vbscript:"_s), qPrintable(id));
        }
    }

    void pathologicalHtmlStaysLinear()
    {
        // Unclosed quotes and tags in their thousands: tags are found without rescanning the rest
        // of the document for every '<' (a generous bound; each takes milliseconds).
        const QStringList inputs = {u"<img alt=\""_s.repeated(20000),
                                    u"<input title='x "_s.repeated(20000),
                                    u"<img src=\"a\" "_s.repeated(20000) + u">"_s,
                                    u"<blockquote title='x "_s.repeated(20000),
                                    u"<meta content=\""_s.repeated(20000),
                                    u"<pre "_s.repeated(10000) + u"<code \""_s.repeated(10000)};
        for (const QString &html : inputs) {
            QElapsedTimer timer;
            timer.start();
            htmlToMarkdown(html);
            QVERIFY2(timer.elapsed() < 10000 * BRULION_TEST_SLOWDOWN, qPrintable(html.left(20) + u": "_s + QString::number(timer.elapsed())));
        }
    }

    void zipBombLimits()
    {
        // 50 MB of zeros compresses to ~50 KB; the limits reject it before allocating.
        const QByteArray bomb = writeZip({{u"zeros.md"_s, QByteArray(50 << 20, '\0')}});
        QVERIFY(bomb.size() < (1 << 20));
        ZipReadOptions o;
        o.maxEntrySize = 1 << 20;
        QStringList warnings;
        QVERIFY(readZip(bomb, &warnings, o).isEmpty());
        QCOMPARE(warnings.size(), 1);
    }
};

QTEST_MAIN(TestIoRobust)
#include "tst_io_robust.moc"
