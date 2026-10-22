// Import framework (registry, detection, input preparation, path rules, appending to journals)
// and the tree importers: Markdown, Obsidian, Bear, HTML.
#include "iotest.h"

#include "io/importer.h"
#include "io/tar.h"
#include "io/zip.h"
#include "markdown/frontmatter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;
using iotest::bytes;
using iotest::text;

namespace {

/// Body of a note without its frontmatter.
QString bodyOf(const QString &content)
{
    return content.sliced(markdown::parseFrontmatter(content).bodyStart);
}

QVariantMap frontmatterOf(const QString &content)
{
    return markdown::parseFrontmatter(content).data;
}

ImportResult importWith(const QString &id, const QList<InputFile> &files, const ImportOptions &o = iotest::options())
{
    const Importer *importer = importerById(id);
    Q_ASSERT(importer);
    return runImport(*importer, files, o);
}

/// An importer returning a fixed result (for the path rules of runImport()).
class FixedImporter final : public Importer {
public:
    explicit FixedImporter(QList<ImportedFile> files) : m_files(std::move(files)) { }
    QString id() const override { return u"fixed"_s; }
    QString name() const override { return u"Fixed"_s; }
    QStringList fileTypes() const override { return {}; }
    int detect(const QList<InputFile> &) const override { return 0; }
    ImportResult run(const QList<InputFile> &, const ImportOptions &) const override
    {
        ImportResult r;
        r.files = m_files;
        r.warnings.append(u"from importer"_s);
        return r;
    }

private:
    QList<ImportedFile> m_files;
};

QByteArray keepJson()
{
    QJsonObject o;
    o.insert(u"title"_s, u"Zakupy"_s);
    o.insert(u"textContent"_s, u"mleko"_s);
    o.insert(u"userEditedTimestampUsec"_s, 1760000000000000.0);
    return QJsonDocument(o).toJson();
}

} // namespace

class TestIoImport : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void registry()
    {
        QStringList ids;
        for (const Importer *i : importers()) {
            ids.append(i->id());
            QVERIFY(!i->name().isEmpty());
            QVERIFY(!i->fileTypes().isEmpty());
            QCOMPARE(i->detect({}), 0);
            // Every importer survives empty input.
            QVERIFY(runImport(*i, {}, iotest::options()).files.isEmpty());
        }
        QCOMPARE(ids, QStringList({u"obsidian"_s, u"logseq"_s, u"notion"_s, u"evernote"_s, u"keep"_s, u"joplin"_s,
                                   u"simplenote"_s, u"roam"_s, u"trilium"_s, u"bear"_s, u"markdown"_s, u"html"_s,
                                   u"ics"_s}));
        QCOMPARE(importerById(u"keep")->name(), u"Google Keep"_s);
        QVERIFY(importerById(u"nope") == nullptr);
    }

    void detect_data()
    {
        QTest::addColumn<QList<InputFile>>("files");
        QTest::addColumn<QString>("expected");
        QTest::newRow("obsidian") << QList<InputFile>{text(u"V/.obsidian/app.json"_s, u"{}"_s),
                                                      text(u"V/a.md"_s, u"x"_s)}
                                  << u"obsidian"_s;
        QTest::newRow("obsidian zip") << QList<InputFile>{iotest::zip(
                u"vault.zip"_s, {{u".obsidian/app.json"_s, "{}"}, {u"Notatka.md"_s, "x"}})}
                                      << u"obsidian"_s;
        QTest::newRow("logseq") << QList<InputFile>{text(u"logseq/config.edn"_s, u"{}"_s),
                                                    text(u"pages/a.md"_s, u"- x"_s)}
                                << u"logseq"_s;
        QTest::newRow("logseq folders") << QList<InputFile>{text(u"g/journals/2026_10_14.md"_s, u"- x"_s),
                                                            text(u"g/pages/a.md"_s, u"- y"_s)}
                                        << u"logseq"_s;
        QTest::newRow("notion") << QList<InputFile>{iotest::zip(
                u"Export.zip"_s, {{u"Plan 0123456789abcdef0123456789abcdef.md"_s, "# Plan"}})}
                                << u"notion"_s;
        QTest::newRow("evernote") << QList<InputFile>{text(u"Moje notatki.enex"_s, u"<en-export/>"_s)} << u"evernote"_s;
        QTest::newRow("keep") << QList<InputFile>{bytes(u"Takeout/Keep/Zakupy.json"_s, keepJson())} << u"keep"_s;
        QTest::newRow("joplin") << QList<InputFile>{text(
                u"0123456789abcdef0123456789abcdef.md"_s,
                u"Tytuł\n\nTreść\n\nid: 0123456789abcdef0123456789abcdef\ntype_: 1"_s)}
                                << u"joplin"_s;
        QTest::newRow("simplenote") << QList<InputFile>{text(u"source/notes.json"_s, u"{\"activeNotes\": []}"_s)}
                                    << u"simplenote"_s;
        QTest::newRow("roam") << QList<InputFile>{text(u"graph.json"_s, u"[{\"title\": \"A\", \"children\": []}]"_s)}
                              << u"roam"_s;
        QTest::newRow("bear") << QList<InputFile>{text(u"Notatka.textbundle/text.md"_s, u"# x"_s)} << u"bear"_s;
        QTest::newRow("markdown") << QList<InputFile>{text(u"a.md"_s, u"x"_s), text(u"b.txt"_s, u"y"_s)}
                                  << u"markdown"_s;
        QTest::newRow("html") << QList<InputFile>{text(u"a.html"_s, u"<p>x</p>"_s)} << u"html"_s;
        QTest::newRow("ics") << QList<InputFile>{text(u"kalendarz.ics"_s, u"BEGIN:VCALENDAR\nEND:VCALENDAR"_s)}
                             << u"ics"_s;
        QTest::newRow("ics by content") << QList<InputFile>{text(u"export"_s, u"BEGIN:VCALENDAR\nEND:VCALENDAR"_s)}
                                        << u"ics"_s;
        // A vault or a notes folder that merely contains a calendar or an Evernote export is still
        // a vault / notes folder (the other file is imported as an attachment).
        QTest::newRow("obsidian with ics attachment")
                << QList<InputFile>{text(u"V/.obsidian/app.json"_s, u"{}"_s), text(u"V/a.md"_s, u"x"_s),
                                    text(u"V/zał/spotkanie.ics"_s, u"BEGIN:VCALENDAR\nEND:VCALENDAR"_s)}
                << u"obsidian"_s;
        QTest::newRow("markdown with enex backup") << QList<InputFile>{text(u"a.md"_s, u"x"_s), text(u"b.md"_s, u"y"_s),
                                                                       text(u"stare/kopia.enex"_s, u"<en-export/>"_s)}
                                                   << u"markdown"_s;
        QTest::newRow("notion with ics attachment")
                << QList<InputFile>{text(u"Plan 0123456789abcdef0123456789abcdef.md"_s, u"# Plan"_s),
                                    text(u"Plan 0123456789abcdef0123456789abcdef/termin.ics"_s, u"BEGIN:VCALENDAR"_s)}
                << u"notion"_s;
        QTest::newRow("several calendars")
                << QList<InputFile>{text(u"a.ics"_s, u"BEGIN:VCALENDAR"_s), text(u"b.ics"_s, u"BEGIN:VCALENDAR"_s)}
                << u"ics"_s;
        QTest::newRow("nothing") << QList<InputFile>{bytes(u"foto.png"_s, "\x89PNG")} << QString();
        QTest::newRow("empty") << QList<InputFile>{} << QString();
    }

    void detect()
    {
        QFETCH(QList<InputFile>, files);
        QFETCH(QString, expected);
        const Importer *importer = detectImporter(files);
        QCOMPARE(importer ? importer->id() : QString(), expected);
    }

    void prepare()
    {
        const QByteArray inner = writeZip({{u"a.md"_s, "A"}, {u"b/c.md"_s, "C"}, {u"__MACOSX/._a.md"_s, "junk"}});
        const QByteArray tar = iotest::tarArchive({{"0123.md", "raw", '0', {}}, {"resources/x.png", "png", '0', {}}});
        QStringList warnings;
        const QList<InputFile> prepared = prepareImportFiles(
                {bytes(u"folder/x.zip"_s, inner), bytes(u"export.jex"_s, tar), text(u"../../evil.md"_s, u"e"_s),
                 text(u"C:\\Users\\jan\\notatka.md"_s, u"n"_s), text(u".DS_Store"_s, u"x"_s),
                 text(u"a/Thumbs.db"_s, u"x"_s), text(u"._forks.md"_s, u"x"_s), text(u"dup.md"_s, u"1"_s),
                 text(u"dup.md"_s, u"2"_s), text(u"same.md"_s, u"s"_s), text(u"same.md"_s, u"s"_s),
                 bytes(u"doc.docx"_s, writeZip({{u"word/document.xml"_s, "<w/>"}})),
                 bytes(u"bad.zip"_s, "PK\x03\x04garbage")},
                &warnings);
        QStringList got;
        for (const InputFile &f : prepared)
            got.append(f.path);
        QCOMPARE(got,
                 QStringList({u"folder/a.md"_s, u"folder/b/c.md"_s, u"0123.md"_s, u"resources/x.png"_s, u"evil.md"_s,
                              u"Users/jan/notatka.md"_s, u"dup.md"_s, u"same.md"_s, u"doc.docx"_s, u"bad.zip"_s}));
        QCOMPARE(iotest::entryData(prepared, u"dup.md"_s), QByteArray("1"));
        QCOMPARE(warnings.size(), 2); // duplicate with other content, damaged archive
    }

    void pathRules()
    {
        FixedImporter importer({
                ImportedFile{u"../x.md"_s, "x", false},
                ImportedFile{u"/abs/y.md"_s, "y", false},
                ImportedFile{u".brulion/config.json"_s, "{}", false},
                ImportedFile{u"a/.trash/old.md"_s, "old", false},
                ImportedFile{u"a.md"_s, "first", false},
                ImportedFile{u"A.md"_s, "second", false},
                ImportedFile{u"journals/2026-10-14.md"_s, "- rano\n", true},
                ImportedFile{u"journals/2026-10-14.md"_s, "- wieczór\n", true},
                ImportedFile{u"Strona.md"_s, "nowa strona", false},
                ImportedFile{u"Inna.md"_s, "Zobacz [[Strona]] i [[Strona#Sekcja|tu]].", false},
                ImportedFile{u"x\u0001y.md"_s, "ctl", false},
        });
        ImportOptions o = iotest::options();
        o.exists = [](const VaultPath &p) { return p.compare(u"strona.md"_s, Qt::CaseInsensitive) == 0; };
        const ImportResult r = runImport(importer, {}, o);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"A 2.md"_s, u"Inna.md"_s, u"Strona 2.md"_s, u"a.md"_s, u"abs/y.md"_s,
                                                  u"journals/2026-10-14.md"_s, u"x.md"_s, u"xy.md"_s}));
        QCOMPARE(iotest::textAt(r, u"a.md"_s), u"first"_s);
        QCOMPARE(iotest::textAt(r, u"A 2.md"_s), u"second"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s), u"- rano\n\n- wieczór\n"_s);
        QVERIFY(iotest::fileAt(r, u"journals/2026-10-14.md"_s)->append);
        QVERIFY(!iotest::fileAt(r, u"a.md"_s)->append);
        QCOMPARE(iotest::textAt(r, u"Inna.md"_s), u"Zobacz [[Strona 2]] i [[Strona 2#Sekcja|tu]]."_s);
        QVERIFY(r.warnings.contains(u"from importer"_s));
        QCOMPARE(r.warnings.size(), 5); // importer + 2 protected + duplicate + existing
    }

    void pathRulesPageAfterJournal()
    {
        // A page at the path of a daily note produced earlier must not share its path.
        FixedImporter importer({
                ImportedFile{u"journals/2026-10-14.md"_s, "- dziennik\n", true},
                ImportedFile{u"journals/2026-10-14.md"_s, "strona", false},
        });
        const ImportResult r = runImport(importer, {}, iotest::options());
        QCOMPARE(iotest::pathsOf(r), QStringList({u"journals/2026-10-14 2.md"_s, u"journals/2026-10-14.md"_s}));
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s), u"- dziennik\n"_s);
        QVERIFY(iotest::fileAt(r, u"journals/2026-10-14.md"_s)->append);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14 2.md"_s), u"strona"_s);
        QVERIFY(!iotest::fileAt(r, u"journals/2026-10-14 2.md"_s)->append);
    }

    void prepareLimitsUnpackedTotal()
    {
        // Each archive alone is within the limits; together they are not. The budget covers all
        // archives of one import (archives in archives too), not each archive separately.
        const QByteArray part = writeZip({{u"duże.md"_s, QByteArray(600 << 10, 'x')}});
        const QByteArray outer = writeZip({{u"a.textpack"_s, part}, {u"b.textpack"_s, part}});
        ZipReadOptions limits;
        limits.maxTotalSize = 1 << 20;
        QStringList warnings;
        const QList<InputFile> prepared =
                prepareImportFiles({bytes(u"x.zip"_s, part), bytes(u"y.zip"_s, outer)}, &warnings, limits);
        qsizetype unpacked = 0;
        for (const InputFile &f : prepared)
            unpacked += f.path.endsWith(u"duże.md"_s) ? f.data.size() : 0;
        QVERIFY2(unpacked <= limits.maxTotalSize, qPrintable(QString::number(unpacked)));
        QVERIFY(unpacked > 0);
        QVERIFY(!warnings.isEmpty());
    }

    void appendImported_data()
    {
        QTest::addColumn<QByteArray>("existing");
        QTest::addColumn<QByteArray>("imported");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("crlf, frontmatter dropped")
                << QByteArray("# Dziennik\r\n\r\n- rano\r\n") << QByteArray("---\ntags: [a]\n---\n- wieczór\n- noc\n")
                << QByteArray("# Dziennik\r\n\r\n- rano\r\n\r\n- wieczór\r\n- noc\r\n");
        QTest::newRow("existing empty") << QByteArray("  \n") << QByteArray("---\na: 1\n---\nx\n")
                                        << QByteArray("---\na: 1\n---\nx\n");
        QTest::newRow("bom kept") << QByteArray("\xEF\xBB\xBF- a") << QByteArray("- b")
                                  << QByteArray("\xEF\xBB\xBF- a\n\n- b\n");
        QTest::newRow("nothing to add") << QByteArray("- a\n") << QByteArray("---\nx: 1\n---\n\n")
                                        << QByteArray("- a\n");
        // The existing text is untouched: a hard line break (two trailing spaces) stays.
        QTest::newRow("hard break kept") << QByteArray("- a  \n  \n\n") << QByteArray("- b\n")
                                         << QByteArray("- a  \n\n- b\n");
    }

    void appendImported()
    {
        QFETCH(QByteArray, existing);
        QFETCH(QByteArray, imported);
        QFETCH(QByteArray, expected);
        QCOMPARE(io::appendImported(existing, imported), expected);
    }

    // --- Markdown ---------------------------------------------------------------------------------

    void markdownFolder()
    {
        const QList<InputFile> files = {
                text(u"notes/Projekty/Dom.md"_s,
                     u"# Dom\n\n![plan](../img/plan%201.png) [[Budżet]] ![[plan 1.png]] [x](https://x.org/a b)\n"
                     "```\n[[nie link]](../img/plan%201.png)\n```\n"_s),
                text(u"notes/Budżet.txt"_s, u"Zażółć gęślą jaźń\r\nlinia 2"_s),
                bytes(u"notes/img/plan 1.png"_s, "PNG"),
                text(u"notes/.obsidian/workspace.json"_s, u"{}"_s),
                text(u"notes/.git/HEAD"_s, u"ref"_s),
        };
        const ImportResult r = importWith(u"markdown"_s, files, iotest::options(u"Import"_s));
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Import/Budżet.md"_s, u"Import/Projekty/Dom.md"_s, u"assets/Import/img/plan 1.png"_s}));
        QCOMPARE(iotest::textAt(r, u"Import/Projekty/Dom.md"_s),
                 u"# Dom\n\n![plan](../../assets/Import/img/plan%201.png) [[Budżet]] ![[plan 1.png]] "
                 "[x](https://x.org/a b)\n```\n[[nie link]](../img/plan%201.png)\n```\n"_s);
        QCOMPARE(iotest::textAt(r, u"Import/Budżet.md"_s), u"Zażółć gęślą jaźń\r\nlinia 2"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Import/img/plan 1.png"_s)->data, QByteArray("PNG"));
        QVERIFY(r.warnings.isEmpty());
    }

    void markdownLegacyEncodings_data()
    {
        // Files that are not UTF-8: windows-1250 (Polish Windows Notepad) is recognised by its
        // Polish letters, anything else is windows-1252 (never C1 control characters).
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        QTest::newRow("windows-1250") << QByteArray("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1\r\n\xA3\xF3"
                                                    "d\x9F, \x8C\x8F"
                                                    "dziebko \xAF\xA5\xD1")
                                      << u"Zażółć gęślą jaźń\r\nŁódź, ŚŹdziebko ŻĄŃ"_s;
        QTest::newRow("windows-1252 punctuation") << QByteArray("\x93"
                                                                "Cytat\x94 \x96 5 \x80\x85 \x91x\x92")
                                                  << u"“Cytat” – 5 €… ‘x’"_s;
        QTest::newRow("spanish stays western") << QByteArray("\xBFQu\xE9? a\xF1o") << u"¿Qué? año"_s;
        QTest::newRow("french stays western") << QByteArray("c\x9Cur \xE0 la cr\xE8me") << u"cœur à la crème"_s;
        QTest::newRow("utf-8 untouched") << u"Zażółć “x”"_s.toUtf8() << u"Zażółć “x”"_s;
    }

    void markdownLegacyEncodings()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        const ImportResult r = importWith(u"markdown"_s, {bytes(u"stara.txt"_s, data)});
        QCOMPARE(iotest::textAt(r, u"stara.md"_s), expected);
    }

    void legacyTablesMatchCodecs_data()
    {
        QTest::addColumn<QByteArray>("codec");
        QTest::addColumn<QByteArray>("sample"); // letters that make decodeText() pick the codec
        QTest::newRow("windows-1250") << QByteArray("windows-1250") << QByteArray("\xB9\xB3\x9C");
        QTest::newRow("windows-1252") << QByteArray("windows-1252") << QByteArray("\xE0\xE8\xEC\xF2");
    }

    void legacyTablesMatchCodecs()
    {
        // The built-in tables (no ICU on Android / WebAssembly) agree with ICU where it exists.
        QFETCH(QByteArray, codec);
        QFETCH(QByteArray, sample);
        QStringDecoder reference(codec.constData());
        if (!reference.isValid())
            QSKIP("codec not available (Qt without ICU)");
        for (int b = 0x80; b <= 0xFF; ++b) {
            const QByteArray byte(1, char(b));
            // Undefined code points: ICU and the tables may differ (both keep a placeholder).
            const QString expected = reference(byte);
            if (expected == QString(QChar(0xFFFD)) || expected.isEmpty()
                || expected.front().category() == QChar::Other_Control)
                continue;
            const ImportResult r = importWith(u"markdown"_s, {bytes(u"t.txt"_s, sample + "\xFF" + byte)});
            const QString text = iotest::textAt(r, u"t.md"_s);
            QVERIFY2(text.endsWith(expected), qPrintable(QString::number(b, 16) + u' ' + text));
        }
    }

    void markdownExistingPaths()
    {
        ImportOptions o = iotest::options();
        o.exists = [](const VaultPath &p) { return p == u"Notatka.md" || p == u"assets/Markdown/a.png"; };
        const ImportResult r = importWith(
                u"markdown"_s, {text(u"Notatka.md"_s, u"![](a.png) [[Notatka]]"_s), bytes(u"a.png"_s, "A")}, o);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Notatka 2.md"_s, u"assets/Markdown/a 2.png"_s}));
        QCOMPARE(iotest::textAt(r, u"Notatka 2.md"_s), u"![](assets/Markdown/a%202.png) [[Notatka 2]]"_s);
        QVERIFY(r.warnings.isEmpty());
    }

    void uniqueNamesSkipExistingSuffixes()
    {
        // Many notes with one title: " 2", " 3", … skipping names that exist in the vault.
        ImportOptions o = iotest::options();
        o.exists = [](const VaultPath &p) { return p == u"Bez tytułu.md" || p == u"Bez tytułu 3.md"; };
        QList<InputFile> files;
        for (int i = 0; i < 4; ++i)
            files.append(iotest::bytes(u"Keep/n"_s + QString::number(i) + u".json"_s,
                                       QByteArray("{\"title\":\"Bez tytułu\",\"textContent\":\"")
                                               + QByteArray::number(i)
                                               + "\",\"isTrashed\":false,\"userEditedTimestampUsec\":1}"));
        const ImportResult r = importWith(u"keep"_s, files, o);
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Bez tytułu 2.md"_s, u"Bez tytułu 4.md"_s, u"Bez tytułu 5.md"_s, u"Bez tytułu 6.md"_s}));
    }

    void windowsReservedAndBidiNames()
    {
        // Vaults sync to Windows too: names it reserves ("CON", "nul.txt", "COM1") get a "_";
        // bidirectional controls, which make a name look like another, are dropped.
        const ImportResult r =
                importWith(u"markdown"_s,
                           {text(u"CON.md"_s, u"1"_s), text(u"Aux.md"_s, u"2"_s), text(u"lpt1.md"_s, u"3"_s),
                            text(u"con.txt.md"_s, u"4"_s), text(u"\u202Efdp.exe.md"_s, u"5"_s),
                            text(u"Konsola.md"_s, u"6"_s), text(u"Prn/a.md"_s, u"7"_s), bytes(u"nul.png"_s, "P")},
                           iotest::options());
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Aux_.md"_s, u"CON_.md"_s, u"Konsola.md"_s, u"Prn_/a.md"_s,
                              u"assets/Markdown/nul_.png"_s, u"con_.txt.md"_s, u"fdp.exe.md"_s, u"lpt1_.md"_s}));
    }

    void attachmentSubfolderAndSuffixLinks()
    {
        ImportOptions o = iotest::options(u"Import/Stare"_s);
        o.attachmentSubfolder = u"z importu/2026"_s;
        o.config.attachmentsFolder = u"Załączniki"_s;
        const ImportResult r = importWith(u"markdown"_s,
                                          {text(u"a/b/c/Głęboka.md"_s, u"[[Projekty/Dom]] ![x](../../../img.png)"_s),
                                           text(u"Projekty/Dom.md"_s, u"dom"_s), bytes(u"img.png"_s, "I")},
                                          o);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Import/Stare/Projekty/Dom.md"_s, u"Import/Stare/a/b/c/Głęboka.md"_s,
                                                  u"Załączniki/z importu/2026/img.png"_s}));
        QCOMPARE(iotest::textAt(r, u"Import/Stare/a/b/c/Głęboka.md"_s),
                 u"[[Import/Stare/Projekty/Dom]] ![x](../../../../../Załączniki/z%20importu/2026/img.png)"_s);
    }

    // --- Obsidian ---------------------------------------------------------------------------------

    void obsidianVault()
    {
        const QByteArray vault = writeZip({
                {u"Vault/.obsidian/app.json"_s, "{}"},
                {u"Vault/Notes/Dom.md"_s, u"---\ntags: [dom]\n---\n[[Notes/Inne]] ![[attachments/zdjęcie.png]] "
                                          u"[[Inne#Sekcja|alias]] [md](Inne.md)"
                                          " ![[zdjęcie.png]] - [ ] zadanie 📅 2026-10-15 ^abc\n"_s.toUtf8()},
                {u"Vault/Notes/Inne.md"_s, "## Sekcja\n"},
                {u"Vault/attachments/zdjęcie.png"_s, "IMG"},
                {u"Vault/.trash/old.md"_s, "old"},
                {u"Vault/Plan.canvas"_s, "{}"},
        });
        const ImportResult r =
                importWith(u"obsidian"_s, {bytes(u"Vault.zip"_s, vault)}, iotest::options(u"Obsidian"_s));
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Obsidian/Notes/Dom.md"_s, u"Obsidian/Notes/Inne.md"_s, u"assets/Obsidian/Plan.canvas"_s,
                              u"assets/Obsidian/attachments/zdjęcie.png"_s}));
        QCOMPARE(iotest::textAt(r, u"Obsidian/Notes/Dom.md"_s),
                 u"---\ntags: [dom]\n---\n[[Obsidian/Notes/Inne]] ![[assets/Obsidian/attachments/zdjęcie.png]] "
                 "[[Inne#Sekcja|alias]] [md](Inne.md) ![[zdjęcie.png]] - [ ] zadanie 📅 2026-10-15 ^abc\n"_s);
    }

    void obsidianZipAttachmentStaysAttachment()
    {
        // A .zip attached to a note of a vault is an attachment, not part of the export (only an
        // archive of archives — Notion's multi-part export — is unpacked level by level).
        const QByteArray attached = writeZip({{u"wewnątrz.md"_s, "nie importuj"}});
        const QByteArray vault = writeZip({{u".obsidian/app.json"_s, "{}"},
                                           {u"Notatka.md"_s, "Kopia: [[kopia.zip]]"},
                                           {u"pliki/kopia.zip"_s, attached}});
        const ImportResult r = importWith(u"obsidian"_s, {bytes(u"vault.zip"_s, vault)}, iotest::options(u"O"_s));
        QCOMPARE(iotest::pathsOf(r), QStringList({u"O/Notatka.md"_s, u"assets/O/pliki/kopia.zip"_s}));
        QCOMPARE(iotest::fileAt(r, u"assets/O/pliki/kopia.zip"_s)->data, attached);
        // Archives of archives are still unpacked.
        const QList<InputFile> prepared = prepareImportFiles(
                {bytes(u"Export.zip"_s, writeZip({{u"Part-1.zip"_s, writeZip({{u"a.md"_s, "A"}})},
                                                  {u"Part-2.zip"_s, writeZip({{u"b.md"_s, "B"}})}}))});
        QStringList got;
        for (const InputFile &f : prepared)
            got.append(f.path);
        QCOMPARE(got, QStringList({u"a.md"_s, u"b.md"_s}));
    }

    // --- Bear -------------------------------------------------------------------------------------

    void bearBundles()
    {
        const QByteArray backup = writeZip({
                {u"Lista zakupów.textbundle/text.md"_s,
                 u"# Lista zakupów\n#zakupy/dom #multi word tag# ::ważne:: (#inny tag#)\n![](assets/foto%20a.jpg)\n"
                 "`#not a tag#`\n```\n#code tag#\n```\n"_s.toUtf8()},
                {u"Lista zakupów.textbundle/assets/foto a.jpg"_s, "JPG"},
                {u"Lista zakupów.textbundle/info.json"_s,
                 R"({"net.shinyfrog.bear": {"creationDate": "2026-10-14T08:00:00Z", "modificationDate": "2026-10-15T10:30:00Z"}})"},
                {u"Inna.md"_s, "#a b c# tekst"},
        });
        const ImportResult r = importWith(u"bear"_s, {bytes(u"Bear Notes.bear2bk"_s, backup)});
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Inna.md"_s, u"Lista zakupów.md"_s, u"assets/Bear/Lista zakupów/foto a.jpg"_s}));
        const QString note = iotest::textAt(r, u"Lista zakupów.md"_s);
        const QVariantMap fm = frontmatterOf(note);
        QCOMPARE(fm.value(u"created"_s).toString(), u"2026-10-14 10:00"_s);
        QCOMPARE(fm.value(u"updated"_s).toString(), u"2026-10-15 12:30"_s);
        QCOMPARE(bodyOf(note),
                 u"# Lista zakupów\n#zakupy/dom #[[multi word tag]] ==ważne== (#[[inny tag]])\n"
                 "![](assets/Bear/Lista%20zakupów/foto%20a.jpg)\n`#not a tag#`\n```\n#code tag#\n```\n"_s);
        QCOMPARE(iotest::textAt(r, u"Inna.md"_s), u"#[[a b c]] tekst"_s);
    }

    // --- HTML -------------------------------------------------------------------------------------

    void htmlSite()
    {
        const QList<InputFile> files = {
                text(u"site/index.html"_s,
                     u"<html><head><title>Strona główna</title></head><body><h1>Strona główna</h1>"
                     "<p>Zobacz <a href=\"o-nas.html#zespol\">O nas</a> i <a href=\"pliki/cennik.pdf\">cennik</a>.</p>"
                     "<p><img src=\"img/logo.png\"><img src=\"data:image/png;base64,iVBORw0KGgo=\">"
                     "<img src=\"https://x.org/a.png\" alt=\"Zew\"><img src=\"brak.png\" alt=\"brak\"></p></body></html>"_s),
                text(u"site/o-nas.html"_s, u"<h1>o-nas</h1><p>Zespół</p>"_s),
                bytes(u"site/img/logo.png"_s, "LOGO"),
                bytes(u"site/pliki/cennik.pdf"_s, "%PDF"),
                text(u"site/style.css"_s, u"p{}"_s),
        };
        const ImportResult r = importWith(u"html"_s, files, iotest::options(u"Web"_s));
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Web/index.md"_s, u"Web/o-nas.md"_s, u"assets/Web/cennik.pdf"_s,
                                                  u"assets/Web/index-1.png"_s, u"assets/Web/logo.png"_s}));
        const QString index = iotest::textAt(r, u"Web/index.md"_s);
        QCOMPARE(frontmatterOf(index).value(u"aliases"_s).toStringList(), QStringList({u"Strona główna"_s}));
        QCOMPARE(bodyOf(index),
                 u"Zobacz [[Web/o-nas|O nas]] i [[assets/Web/cennik.pdf|cennik]].\n\n![[assets/Web/logo.png]]"
                 "![[assets/Web/index-1.png]]![Zew](https://x.org/a.png)brak\n"_s);
        QCOMPARE(iotest::textAt(r, u"Web/o-nas.md"_s), u"Zespół\n"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Web/index-1.png"_s)->data, QByteArray::fromBase64("iVBORw0KGgo="));
        QCOMPARE(r.warnings.size(), 1); // brak.png
    }

    void htmlDollarsShowAsWritten()
    {
        // Dollars of a web page are text (SYNTAX §11): prices never pair into a formula in the
        // imported note, in the editor or the export; code keeps its dollars as written.
        const ImportResult r =
                importWith(u"html"_s, {text(u"ceny.html"_s, u"<p>Cena $10, <i>Netflix</i> 15$</p><p>$5 i $10</p>"
                                                            "<ul><li>Wzór $x$ i <code>$y$</code></li></ul>"_s)});
        const QString note = iotest::textAt(r, u"ceny.md"_s);
        QCOMPARE(bodyOf(note), u"Cena \\$10, *Netflix* 15$\n\n$5 i $10\n\n- Wzór \\$x$ i `$y$`\n"_s);
        for (const bool exported : {false, true}) {
            QCOMPARE(iotest::shownText(bodyOf(note), exported), u"Cena $10, Netflix 15$\n$5 i $10\nWzór $x$ i $y$"_s);
        }
    }

    void htmlLegacyCharset()
    {
        // Polish pages saved by old browsers: windows-1250 declared in a <meta> tag.
        QStringEncoder encoder("windows-1250");
        if (!encoder.isValid())
            QSKIP("windows-1250 is not available (Qt without ICU)");
        const QByteArray page = encoder.encode(
                u"<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1250\">"
                "<title>Zażółć</title></head><body><p>Gęślą jaźń ĄĆĘŁŃÓŚŹŻ</p></body></html>"_s);
        QVERIFY(!encoder.hasError());
        const ImportResult r = importWith(u"html"_s, {bytes(u"stara.html"_s, page)});
        const QString note = iotest::textAt(r, u"stara.md"_s);
        QCOMPARE(bodyOf(note), u"Gęślą jaźń ĄĆĘŁŃÓŚŹŻ\n"_s);
        QCOMPARE(frontmatterOf(note).value(u"aliases"_s).toStringList(), QStringList({u"Zażółć"_s}));
    }

    void htmlDataUriText()
    {
        // Non-base64 data: URIs carry UTF-8 text (SVG drawings, …).
        const ImportResult r = importWith(
                u"html"_s,
                {text(u"a.html"_s, u"<p><img src=\"data:image/svg+xml;utf8,%3Csvg%3E%C5%BC ż%3C/svg%3E\"></p>"_s)});
        QCOMPARE(iotest::fileAt(r, u"assets/HTML/a-1.svg"_s) ? iotest::fileAt(r, u"assets/HTML/a-1.svg"_s)->data
                                                             : QByteArray(),
                 u"<svg>ż ż</svg>"_s.toUtf8());
    }

    void htmlGarbage()
    {
        QByteArray garbage;
        for (int i = 0; i < 4000; ++i)
            garbage += char((i * 37) % 256);
        const ImportResult r = importWith(u"html"_s, {bytes(u"zły.html"_s, garbage), text(u"pusty.htm"_s, QString())});
        QCOMPARE(iotest::pathsOf(r), QStringList({u"pusty.md"_s, u"zły.md"_s}));
    }
};

QTEST_MAIN(TestIoImport)
#include "tst_io_import.moc"
