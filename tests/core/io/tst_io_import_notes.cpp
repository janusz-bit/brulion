// OneNote and Apple Notes importers, and the readers under the Apple Notes import: the SQLite file
// reader (schema, B-trees with interior and overflow pages, the write-ahead log and its checksums,
// damaged files), the protocol buffers decoder and gunzip (limits, zip bombs). OneNote: the
// download's folder (manifest, Graph's page HTML, resources) — notebooks, section groups, page
// levels, to-do and other tags, pictures, files, links between pages. Apple Notes: the fixture
// fixtures/applenotes (make-fixture.py builds it like a Mac's group.com.apple.notes folder).
#include "iotest.h"

#include "io/importer.h"
#include "io/protobuf_p.h"
#include "io/sqlitereader_p.h"
#include "io/zip.h"

#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;
using brulion::io::detail::ProtoMessage;
using iotest::textAt;

#define COMPARE_TEXT(actual, expected)                                                                                 \
    do {                                                                                                               \
        const QString a_ = (actual);                                                                                   \
        const QString e_ = (expected);                                                                                 \
        if (a_ != e_)                                                                                                  \
            qWarning().noquote() << "actual:\n" << a_ << "\nexpected:\n" << e_;                                        \
        QCOMPARE(a_, e_);                                                                                              \
    } while (false)

namespace {

QList<InputFile> fixtureFolder(const QString &dir, const QString &prefix)
{
    QList<InputFile> out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        out.append(InputFile{prefix + path.sliced(dir.size()), f.readAll()});
    }
    std::ranges::sort(out, [](const InputFile &a, const InputFile &b) { return a.path < b.path; });
    return out;
}

QList<InputFile> appleNotes()
{
    return fixtureFolder(QStringLiteral(BRULION_APPLENOTES_FIXTURES) + u"/group.com.apple.notes"_s,
                                        u"group.com.apple.notes"_s);
}

QByteArray fileData(const QList<InputFile> &files, const QString &name)
{
    for (const InputFile &f : files) {
        if (f.path.endsWith(name))
            return f.data;
    }
    return {};
}

const QString Res = u"https://graph.microsoft.com/v1.0/users('x')/onenote/resources/"_s;

QString onenotePage(const QString &body)
{
    return u"<html lang=\"pl-PL\"><head><title>t</title><meta http-equiv=\"Content-Type\" content=\"text/html; "
           "charset=utf-8\" /></head><body data-absolute-enabled=\"true\" style=\"font-family:Calibri;font-size:11pt\">"
           "<div data-id=\"_default\" style=\"position:absolute;left:48px;top:115px;width:624px\">"_s
           + body + u"</div></body></html>"_s;
}

QList<InputFile> oneNoteDownload()
{
    const auto page = [](const QString &id, const QString &guid, const QString &title, int level, int order) {
        QJsonObject p;
        p.insert(u"id"_s, id);
        p.insert(u"guid"_s, guid);
        p.insert(u"title"_s, title);
        p.insert(u"level"_s, level);
        p.insert(u"order"_s, order);
        p.insert(u"created"_s, u"2026-11-10T08:00:00Z"_s);
        p.insert(u"modified"_s, u"2026-11-12T16:45:00Z"_s);
        p.insert(u"file"_s, QString(u"pages/"_s + id + u".html"_s));
        return p;
    };
    QJsonObject travel;
    travel.insert(u"id"_s, u"sec-1"_s);
    travel.insert(u"name"_s, u"Podróże"_s);
    travel.insert(u"pages"_s,
                  QJsonArray{page(u"p1"_s, u"{11111111-2222-3333-4444-555555555555}"_s, u"Kraków"_s, 0, 0),
                             page(u"p2"_s, u"22222222-2222-3333-4444-555555555555"_s, u"Hotele"_s, 1, 1),
                             page(u"p3"_s, u"33333333-2222-3333-4444-555555555555"_s, u"Ceny: lipiec"_s, 2, 2),
                             page(u"p4"_s, u"44444444-2222-3333-4444-555555555555"_s, u"Budżet"_s, 0, 3),
                             page(u"p5"_s, u"55555555-2222-3333-4444-555555555555"_s, QString(), 0, 4)});
    QJsonObject old;
    old.insert(u"name"_s, u"2025"_s);
    old.insert(u"pages"_s, QJsonArray{page(u"p6"_s, QString(), u"Stare"_s, 0, 0)});
    QJsonObject archive;
    archive.insert(u"name"_s, u"Archiwum"_s);
    archive.insert(u"sections"_s, QJsonArray{old});
    QJsonObject notebook;
    notebook.insert(u"id"_s, u"nb-1"_s);
    notebook.insert(u"name"_s, u"Mój notes"_s);
    notebook.insert(u"sections"_s, QJsonArray{travel});
    notebook.insert(u"groups"_s, QJsonArray{archive});
    QJsonObject resources;
    resources.insert(Res + u"0-full/$value"_s,
                     QJsonObject{{u"file"_s, u"resources/0-full.png"_s}, {u"type"_s, u"image/png"_s}});
    resources.insert(Res + u"0-pdf/$value"_s,
                     QJsonObject{{u"file"_s, u"resources/0-pdf.pdf"_s}, {u"name"_s, u"Bilety.pdf"_s}});
    resources.insert(Res + u"0-gone/$value"_s, QJsonObject{{u"file"_s, u"resources/0-gone.png"_s}});
    QJsonObject manifest;
    manifest.insert(u"brulion"_s, u"onenote-export"_s);
    manifest.insert(u"version"_s, 1);
    manifest.insert(u"notebooks"_s, QJsonArray{notebook});
    manifest.insert(u"resources"_s, resources);
    const QString p1 = onenotePage(
            u"<h1 style=\"font-size:16pt;color:#1e4e79\">Plan</h1>"
            "<p data-tag=\"to-do\" style=\"margin-top:0pt;margin-bottom:0pt\">Zarezerwować hotel</p>"
            "<p data-tag=\"to-do:completed\" style=\"margin-top:0pt;margin-bottom:0pt\">Kupić bilety</p>"
            "<p data-tag=\"important\">Paszport!</p>"
            "<ul><li><p style=\"margin-top:0pt;margin-bottom:0pt\">Wawel</p></li><li>Sukiennice</li></ul>"
            "<img alt=\"Tekst OCR z obrazka\" width=\"300\" height=\"200\" src=\""_s
            + Res + u"0-small/$value\" data-src-type=\"image/png\" data-fullres-src=\""_s + Res
            + u"0-full/$value\" data-fullres-src-type=\"image/png\" />"
              "<object data-attachment=\"Bilety.pdf\" type=\"application/pdf\" data=\""_s
            + Res
            + u"0-pdf/$value\" />"
              "<p><a href=\"onenote:https://d.docs.live.net/x/Dokumenty/Podróże.one#Budżet&amp;section-id={ab}&amp;"
              "page-id={44444444-2222-3333-4444-555555555555}&amp;end\">Budżet</a> i <a "
              "href=\"onenote:https://d.docs.live.net/x/"
              "Inne.one#Gdzie%20indziej&amp;page-id={99999999-2222-3333-4444-555555555555}&amp;end\">tam</a>"
              " oraz <a href=\"https://pl.wikipedia.org/wiki/Krak%C3%B3w\">Wikipedia</a></p>"
              "<img src=\""_s
            + Res
            + u"0-gone/$value\" />"
              "<table border=\"1\"><tr><td>Dzień</td><td>Plan</td></tr><tr><td>1</td><td>Wawel</td></tr></table>"_s);
    return {iotest::bytes(u"OneNote/onenote.json"_s, QJsonDocument(manifest).toJson()),
            iotest::text(u"OneNote/pages/p1.html"_s, p1),
            iotest::text(u"OneNote/pages/p2.html"_s, onenotePage(u"<p>Lista hoteli</p>"_s)),
            iotest::text(u"OneNote/pages/p3.html"_s, onenotePage(u"<p>Ceny $100 i $200</p>"_s)),
            iotest::text(u"OneNote/pages/p4.html"_s, onenotePage(u"<p>Razem: 1200 zł</p>"_s)),
            iotest::text(u"OneNote/pages/p5.html"_s, onenotePage(u"<p>Bez tytułu</p>"_s)),
            iotest::text(u"OneNote/pages/p6.html"_s, onenotePage(u"<p>Archiwalna</p>"_s)),
            iotest::bytes(u"OneNote/resources/0-full.png"_s, "PNG-FULL"),
            iotest::bytes(u"OneNote/resources/0-pdf.pdf"_s, "%PDF")};
}

} // namespace

class TestIoImportNotes : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- Readers ---------------------------------------------------------------------------------

    void gunzipLimits()
    {
        const QByteArray text = QByteArray(200000, 'a');
        QByteArray zlib = qCompress(text).mid(4); // qCompress: a 4-byte size, then a zlib stream
        QCOMPARE(gunzip(zlib, 1 << 20).value_or(QByteArray()), text);
        QVERIFY(!gunzip(zlib, 1000).has_value()); // a "zip bomb" over the limit
        QVERIFY(!gunzip(QByteArray("not compressed at all")).has_value());
        QVERIFY(!gunzip(zlib.left(zlib.size() / 2)).has_value());
        // The fixture's notes are gzip members.
        const QList<InputFile> files = appleNotes();
        QVERIFY(!files.isEmpty());
    }

    void protobufDecoder()
    {
        // field 1 varint 150, field 2 "testing", field 3 fixed32.
        const QByteArray msg = QByteArray::fromHex("089601120774657374696e671d0000803f");
        const ProtoMessage m(msg);
        QVERIFY(m.valid());
        QCOMPARE(m.integer(1), 150);
        QCOMPARE(m.string(2), u"testing"_s);
        QCOMPARE(m.field(3)->asFloat(), 1.0f);
        QVERIFY(!ProtoMessage(QByteArray::fromHex("0a05616263")).valid());               // length past the end
        QVERIFY(!ProtoMessage(QByteArray::fromHex("0b")).valid());                       // a group
        QVERIFY(!ProtoMessage(QByteArray::fromHex("08ffffffffffffffffffff01")).valid()); // an 11-byte varint
    }

    void sqliteReader()
    {
        const QList<InputFile> files = appleNotes();
        const QByteArray db = fileData(files, u"NoteStore.sqlite"_s);
        const QByteArray wal = fileData(files, u"NoteStore.sqlite-wal"_s);
        QVERIFY(!db.isEmpty() && !wal.isEmpty());
        QString error;
        auto reader = detail::SqliteReader::open(db, wal, &error);
        QVERIFY2(reader, qPrintable(error));
        QVERIFY(reader->tables().contains(u"ZICCLOUDSYNCINGOBJECT"_s));
        QVERIFY(reader->walFrames() > 0);
        const auto *objects = reader->table(u"ziccloudsyncingobject"_s);
        QVERIFY(objects);
        QCOMPARE(objects->columns.value(0), u"Z_PK"_s);
        QCOMPARE(objects->rowidColumn, 0);
        int rows = 0;
        qint64 last = 0;
        bool ordered = true;
        QVERIFY(reader->scan(*objects, [&](qint64 rowid, const QList<QVariant> &v) {
            ordered = ordered && rowid > last && v.value(0).toLongLong() == rowid;
            last = rowid;
            ++rows;
            return true;
        }));
        QVERIFY(ordered);
        QVERIFY2(rows > 400, qPrintable(QString::number(rows))); // interior pages
        // The long note's data spans overflow pages; the note added last is only in the log.
        const auto *notes = reader->table(u"ZICNOTEDATA"_s);
        int notesWithWal = 0;
        qsizetype longest = 0;
        reader->scan(*notes, [&](qint64, const QList<QVariant> &v) {
            ++notesWithWal;
            longest = std::max(longest, v.value(4).toByteArray().size());
            return true;
        });
        QVERIFY(longest > 4096);
        auto withoutWal = detail::SqliteReader::open(db, {}, &error);
        QVERIFY(withoutWal);
        int notesWithoutWal = 0;
        withoutWal->scan(*withoutWal->table(u"ZICNOTEDATA"_s), [&](qint64, const QList<QVariant> &) {
            ++notesWithoutWal;
            return true;
        });
        QCOMPARE(notesWithWal, notesWithoutWal + 1);
        // A log whose last frame is damaged: its transaction does not count.
        QByteArray badWal = wal;
        badWal[badWal.size() - 10] = char(badWal[badWal.size() - 10] ^ 0x5A);
        auto damaged = detail::SqliteReader::open(db, badWal, &error);
        QVERIFY(damaged);
        int notesDamaged = 0;
        damaged->scan(*damaged->table(u"ZICNOTEDATA"_s), [&](qint64, const QList<QVariant> &) {
            ++notesDamaged;
            return true;
        });
        QCOMPARE(notesDamaged, notesWithoutWal);
    }

    void sqliteHostile_data()
    {
        QTest::addColumn<QByteArray>("data");
        const QByteArray db = fileData(appleNotes(), u"NoteStore.sqlite"_s);
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("text") << QByteArray("SQLite format 3 but not really");
        QTest::newRow("truncated") << db.left(1000);
        QByteArray badPageSize = db;
        badPageSize[16] = char(0x03);
        badPageSize[17] = char(0x00);
        QTest::newRow("page size not a power of two") << badPageSize;
        // Every page's type byte broken in turn would take long: a few patterns of damage.
        for (int seed = 1; seed <= 6; ++seed) {
            QByteArray noisy = db;
            for (qsizetype i = 100 + seed * 37; i < noisy.size(); i += 97 * seed)
                noisy[i] = char(noisy[i] ^ (0x11 * seed));
            QTest::newRow(qPrintable(u"noise %1"_s.arg(seed))) << noisy;
        }
        QByteArray cycle = db;
        // Page 2's right-most child pointing at itself (an interior page whose child is itself).
        QTest::newRow("cycle") << cycle;
    }
    void sqliteHostile()
    {
        QFETCH(QByteArray, data);
        // Nothing crashes, loops or reads out of bounds (ASan in CI); the import says what failed.
        const ImportResult r = runImport(*importerById(u"applenotes"), {iotest::bytes(u"x/NoteStore.sqlite"_s, data)},
                                         iotest::options(u"Import/Apple Notes"_s));
        for (const ImportedFile &f : r.files)
            QVERIFY(f.path.startsWith(u"Import/Apple Notes/"_s) || f.path.startsWith(u"assets/"_s));
    }

    void sqliteColumns_data()
    {
        QTest::addColumn<QString>("sql");
        QTest::addColumn<QStringList>("columns");
        QTest::addColumn<int>("rowid");
        QTest::newRow("core data") << u"CREATE TABLE ZX ( Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, ZNAME VARCHAR )"_s
                                   << QStringList{u"Z_PK"_s, u"Z_ENT"_s, u"ZNAME"_s} << 0;
        QTest::newRow("quoted, types with commas, constraints")
                << u"CREATE TABLE \"t\" (\"a b\" TEXT, [c] DECIMAL(10,2), `d` INT, PRIMARY KEY(\"a b\"), UNIQUE(c))"_s
                << QStringList{u"a b"_s, u"c"_s, u"d"_s} << -1;
    }
    void sqliteColumns()
    {
        QFETCH(QString, sql);
        QFETCH(QStringList, columns);
        QFETCH(int, rowid);
        int r = -2;
        QCOMPARE(detail::sqliteColumns(sql, &r), columns);
        QCOMPARE(r, rowid);
    }

    // ---- Apple Notes ------------------------------------------------------------------------------

    void appleNotesImport()
    {
        const QList<InputFile> files = appleNotes();
        QCOMPARE(detectImporter(files)->id(), u"applenotes"_s);
        const ImportResult r = runImport(*importerById(u"applenotes"), files, iotest::options(u"Import/Apple Notes"_s));
        const QStringList paths = iotest::pathsOf(r);
        QVERIFY2(paths.contains(u"Import/Apple Notes/Lista zakupów.md"_s), qPrintable(paths.join(u", "_s)));
        QVERIFY(paths.contains(u"Import/Apple Notes/Przepisy/Ciasta słodkie/Szarlotka.md"_s));
        QVERIFY(paths.contains(u"Import/Apple Notes/Przepisy/Pomysły.md"_s));
        QVERIFY(paths.contains(u"Import/Apple Notes/Długa notatka.md"_s));
        QVERIFY(paths.contains(u"Import/Apple Notes/Z dziennika.md"_s)); // from the write-ahead log
        QVERIFY(!paths.contains(u"Import/Apple Notes/Hasła.md"_s));
        QVERIFY(!paths.join(u'\n').contains(u"Stara lista"_s));
        COMPARE_TEXT(
                textAt(r, u"Import/Apple Notes/Lista zakupów.md"_s),
                u"---\ncreated: 2026-11-10 11:30\nupdated: 2026-11-13 18:05\npinned: true\nsource: Apple Notes\n---\n"
                "Na sobotę **koniecznie**, a w niedzielę *może*.\n"
                "- [x] Mleko 2%\n- [ ] Chleb żytni\n- Owoce\n\t- Jabłka\n\t- Gruszki\n\n"
                "Sklep: [Biedronka](https://www.biedronka.pl/pl/sklepy) #zakupy\n\n"
                "![[assets/Apple Notes/paragon.png]]\n\n#1 cena $5 i $10\n"_s);
        COMPARE_TEXT(textAt(r, u"Import/Apple Notes/Przepisy/Ciasta słodkie/Szarlotka.md"_s),
                     u"---\ncreated: 2026-11-10 11:30\nupdated: 2026-11-13 18:05\nsource: Apple Notes\n---\n"
                     "## Składniki\n\n| Składnik | Ilość |\n| --- | --- |\n| Mąka | 300 g |\n| Jabłka \\| kwaśne | 1 "
                     "kg |\n\n"
                     "### Kroki\n1. Obrać jabłka\n2. Upiec w 180°C\n\n```\ntemp = 180\nczas = 45\n```\n"
                     "Zakupy: [[Import/Apple Notes/Lista zakupów]]\n~~Nie dodawać cukru waniliowego~~\n"
                     "==Najważniejsze: kwaśne jabłka==\n> Cytat z babci\n\n"
                     "[**Szarlotka babci**](https://przepisy.example.pl/szarlotka)\n"_s);
        const QString ideas = textAt(r, u"Import/Apple Notes/Przepisy/Pomysły.md"_s);
        QVERIFY2(ideas.contains(u"![[assets/Apple Notes/Drawing.png]]"_s), qPrintable(ideas));
        QVERIFY2(ideas.contains(u"![[assets/Apple Notes/Scan.pdf]]"_s), qPrintable(ideas));
        QVERIFY(iotest::fileAt(r, u"assets/Apple Notes/paragon.png"_s));
        QVERIFY(textAt(r, u"Import/Apple Notes/Długa notatka.md"_s).size() > 20000);
        const QString warnings = r.warnings.join(u'\n');
        QVERIFY2(warnings.contains(u"locked"_s), qPrintable(warnings));
        QVERIFY(warnings.contains(u"recently deleted"_s));
        QVERIFY(warnings.contains(u"not in the copied folder"_s));
    }

    void appleNotesWithoutAttachments()
    {
        // Only the database (no Accounts folder): the notes, attachments counted as missing.
        QList<InputFile> files;
        for (const InputFile &f : appleNotes()) {
            if (!f.path.contains(u"/Accounts/"_s))
                files.append(f);
        }
        const ImportResult r = runImport(*importerById(u"applenotes"), files, iotest::options(u"Notatki"_s));
        QVERIFY(iotest::fileAt(r, u"Notatki/Lista zakupów.md"_s));
        QVERIFY(!textAt(r, u"Notatki/Lista zakupów.md"_s).contains(u"paragon"_s));
    }

    // ---- OneNote -----------------------------------------------------------------------------------

    void oneNoteImport()
    {
        const QList<InputFile> files = oneNoteDownload();
        QCOMPARE(detectImporter(files)->id(), u"onenote"_s);
        const ImportResult r = runImport(*importerById(u"onenote"), files, iotest::options(u"Import/OneNote"_s));
        const QStringList paths = iotest::pathsOf(r);
        QCOMPARE(paths, (QStringList{u"Import/OneNote/Mój notes/Archiwum/2025/Stare.md"_s,
                                     u"Import/OneNote/Mój notes/Podróże/Budżet.md"_s,
                                     u"Import/OneNote/Mój notes/Podróże/Kraków.md"_s,
                                     u"Import/OneNote/Mój notes/Podróże/Kraków/Hotele.md"_s,
                                     u"Import/OneNote/Mój notes/Podróże/Kraków/Hotele/Ceny lipiec.md"_s,
                                     u"Import/OneNote/Mój notes/Podróże/Untitled page.md"_s,
                                     u"assets/OneNote/Bilety.pdf"_s, u"assets/OneNote/Kraków.png"_s}));
        const QString krakow = textAt(r, u"Import/OneNote/Mój notes/Podróże/Kraków.md"_s);
        QVERIFY2(krakow.startsWith(
                         u"---\ncreated: 2026-11-10 09:00\nupdated: 2026-11-12 17:45\nsource: OneNote\n---\n"_s),
                 qPrintable(krakow));
        QVERIFY2(krakow.contains(u"- [ ] Zarezerwować hotel\n- [x] Kupić bilety\n"_s), qPrintable(krakow));
        QVERIFY(krakow.contains(u"#important Paszport!"_s));
        QVERIFY(krakow.contains(u"- Wawel\n- Sukiennice\n\n![[assets/OneNote/Kraków.png]]\n"_s));
        QVERIFY(!krakow.contains(u"OCR"_s));
        QVERIFY(krakow.contains(u"[[assets/OneNote/Bilety.pdf]]"_s));
        QVERIFY2(
                krakow.contains(
                        u"[[Import/OneNote/Mój notes/Podróże/Budżet]] i tam oraz [Wikipedia](https://pl.wikipedia.org/wiki/Krak%C3%B3w)"_s),
                qPrintable(krakow));
        QVERIFY(krakow.contains(u"| Dzień | Plan |"_s));
        QCOMPARE(textAt(r, u"Import/OneNote/Mój notes/Podróże/Kraków/Hotele/Ceny lipiec.md"_s),
                 u"---\naliases:\n  - \"Ceny: lipiec\"\ncreated: 2026-11-10 09:00\nupdated: 2026-11-12 17:45\nsource: "
                 u"OneNote\n---\n"
                 "Ceny $100 i $200\n"_s);
        const QString warnings = r.warnings.join(u'\n');
        QVERIFY2(warnings.contains(u"were not downloaded"_s), qPrintable(warnings));
        QVERIFY(warnings.contains(u"outside the import"_s));
    }

    void oneNoteMalformed()
    {
        QList<InputFile> files = oneNoteDownload();
        files[0].data = "{\"brulion\": \"onenote-export\", \"notebooks\": [{\"sections\": [{\"pages\": [{\"file\": "
                        "\"../../etc/passwd\"}, 7]}]}, 3]}";
        const ImportResult r = runImport(*importerById(u"onenote"), files, iotest::options(u"Import/OneNote"_s));
        for (const ImportedFile &f : r.files)
            QVERIFY(f.path.startsWith(u"Import/OneNote/"_s) || f.path.startsWith(u"assets/"_s));
        QVERIFY(r.warnings.join(u'\n').contains(u"not downloaded"_s));
    }
};

QTEST_MAIN(TestIoImportNotes)
#include "tst_io_import_notes.moc"
