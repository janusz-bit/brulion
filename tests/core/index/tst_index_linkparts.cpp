// The path parts of a link (docs/SYNTAX.md §2 "Path parts"): what a parent part of [[A/B/C]]
// opens — the page beside the folder holding the link's page, the page the prefix names (path,
// title, alias), the folder with notes, else a new page — and the index's folders with notes
// (VaultIndex::noteFolder()), kept through adds, removals, renames and the index cache.
#include "index/index.h"
#include "index/indexcache.h"
#include "markdown/inline.h"
#include "util/paths.h"

#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

/// An index of `files`: "path" or "path|alias1,alias2" (frontmatter aliases); non-Markdown files
/// are attachments.
std::unique_ptr<VaultIndex> indexOf(const QStringList &files)
{
    auto index = std::make_unique<VaultIndex>();
    for (const QString &f : files) {
        const qsizetype bar = f.indexOf(u'|');
        const QString path = bar < 0 ? f : f.left(bar);
        QString content = u"- Notatka\n"_s;
        if (bar >= 0)
            content = u"---\naliases: ["_s + f.mid(bar + 1) + u"]\n---\n"_s + content;
        index->update(path, paths::isMarkdown(path) ? content : QString());
    }
    return index;
}

QString kindName(markdown::LinkPartTarget::Kind kind)
{
    switch (kind) {
    case markdown::LinkPartTarget::Kind::Page: return u"page"_s;
    case markdown::LinkPartTarget::Kind::Folder: return u"folder"_s;
    case markdown::LinkPartTarget::Kind::New: return u"new"_s;
    }
    return {};
}

/// "kind path prefix" of resolveLinkPart(), "none" for nullopt.
QString describe(const std::optional<markdown::LinkPartTarget> &t)
{
    return t ? kindName(t->kind) + u' ' + t->path + u" | "_s + t->prefix : u"none"_s;
}

} // namespace

class TestIndexLinkParts : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void resolveLinkPart_data()
    {
        QTest::addColumn<QStringList>("files");
        QTest::addColumn<QString>("target");
        QTest::addColumn<int>("part");
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("expected"); // "kind path | prefix", "none"
        const QString nfd = u"FIRMA/żółw"_s;   // żółw decomposed
        QTest::newRow("page at the root") << QStringList{u"Firma.md"_s, u"Firma/Adam cośtam.md"_s}
                                          << u"Firma/Adam cośtam"_s << 1 << QString() << u"page Firma.md | Firma"_s;
        QTest::newRow("folder only") << QStringList{u"Firma/Adam cośtam.md"_s} << u"Firma/Adam cośtam"_s << 1
                                     << QString() << u"folder Firma | Firma"_s;
        QTest::newRow("nothing") << QStringList{u"Inne.md"_s} << u"Firma/Adam cośtam"_s << 1 << QString()
                                 << u"new  | Firma"_s;
        QTest::newRow("page beside the deeper folder")
                << QStringList{u"Klienci/Firma.md"_s, u"Klienci/Firma/Adam.md"_s, u"Firma.md"_s} << u"Firma/Adam"_s << 1
                << QString() << u"page Klienci/Firma.md | Firma"_s;
        QTest::newRow("deeper folder without its page") << QStringList{u"Klienci/Firma/Adam.md"_s} << u"Firma/Adam"_s
                                                        << 1 << QString() << u"folder Klienci/Firma | Firma"_s;
        QTest::newRow("title elsewhere beats the folder")
                << QStringList{u"Archiwum/Firma.md"_s, u"Klienci/Firma/Adam.md"_s} << u"Firma/Adam"_s << 1 << QString()
                << u"page Archiwum/Firma.md | Firma"_s;
        QTest::newRow("alias") << QStringList{u"Spółki/ACME.md|Firma"_s, u"Firma/Adam.md"_s} << u"Firma/Adam"_s << 1
                               << QString() << u"page Spółki/ACME.md | Firma"_s;
        QTest::newRow("alias with a slash") << QStringList{u"Ludzie/Adam.md|Firma/Adam"_s} << u"Firma/Adam"_s << 1
                                            << QString() << u"new  | Firma"_s;
        QTest::newRow("case and NFD") << QStringList{u"firma/Żółw.md"_s} << nfd << 1 << QString()
                                      << u"folder firma | FIRMA"_s;
        QTest::newRow("second part") << QStringList{u"A/B.md"_s, u"A/B/C.md"_s} << u"A/B/C"_s << 2 << QString()
                                     << u"page A/B.md | A/B"_s;
        QTest::newRow("first of three") << QStringList{u"A/B/C.md"_s} << u"A/B/C"_s << 1 << QString()
                                        << u"folder A | A"_s;
        QTest::newRow("second of three, folder")
                << QStringList{u"A/B/C.md"_s} << u"A/B/C"_s << 2 << QString() << u"folder A/B | A/B"_s;
        QTest::newRow("relative to the note")
                << QStringList{u"Klienci/Notatka.md"_s, u"Klienci/Firma/Adam.md"_s} << u"Firma/Adam"_s << 1
                << u"Klienci/Notatka.md"_s << u"folder Klienci/Firma | Firma"_s;
        QTest::newRow("relative, unresolved link")
                << QStringList{u"Klienci/Notatka.md"_s, u"Klienci/Firma/Stara.md"_s} << u"Firma/Nowa"_s << 1
                << u"Klienci/Notatka.md"_s << u"folder Klienci/Firma | Firma"_s;
        QTest::newRow("root folder first") << QStringList{u"Firma/X.md"_s, u"Klienci/Firma/Y.md"_s, u"Klienci/N.md"_s}
                                           << u"Firma/Nowa"_s << 1 << u"Klienci/N.md"_s << u"folder Firma | Firma"_s;
        QTest::newRow("attachments-only folder")
                << QStringList{u"Zdjęcia/kot.png"_s} << u"Zdjęcia/Nowa"_s << 1 << QString() << u"new  | Zdjęcia"_s;
        QTest::newRow("part named like an attachment") << QStringList{u"plan.pdf"_s, u"Inne.md"_s} << u"plan.pdf/x"_s
                                                       << 1 << QString() << u"new  | plan.pdf"_s;
        QTest::newRow("written .md") << QStringList{u"Firma.md"_s, u"Firma/Adam.md"_s} << u"Firma/Adam.md"_s << 1
                                     << QString() << u"page Firma.md | Firma"_s;
        QTest::newRow(".markdown note") << QStringList{u"Firma.markdown"_s, u"Firma/Adam.md"_s} << u"Firma/Adam"_s << 1
                                        << QString() << u"page Firma.markdown | Firma"_s;
        QTest::newRow("journals folder") << QStringList{u"journals/2026-10-14.md"_s} << u"journals/2026-10-15"_s << 1
                                         << QString() << u"folder journals | journals"_s;
        QTest::newRow("Polish deep path")
                << QStringList{u"Projekty/Zażółć gęślą/Notatki.md"_s} << u"Projekty/Zażółć gęślą/Notatki"_s << 2
                << QString() << u"folder Projekty/Zażółć gęślą | Projekty/Zażółć gęślą"_s;
        QTest::newRow("spaces as written")
                << QStringList{u"Firma/Adam.md"_s} << u"Firma / Adam"_s << 1 << QString() << u"folder Firma | Firma"_s;
        QTest::newRow("not split") << QStringList{u"Firma.md"_s} << u"Firma"_s << 1 << QString() << u"none"_s;
        QTest::newRow("part out of range") << QStringList{u"A/B.md"_s} << u"A/B"_s << 2 << QString() << u"none"_s;
        QTest::newRow("part 0") << QStringList{u"A/B.md"_s} << u"A/B"_s << 0 << QString() << u"none"_s;
        QTest::newRow("negative part") << QStringList{u"A/B.md"_s} << u"A/B"_s << -1 << QString() << u"none"_s;
        QTest::newRow("trailing slash") << QStringList{u"Firma/Adam.md"_s} << u"Firma/Adam/"_s << 1 << QString()
                                        << u"none"_s;
        QTest::newRow("attachment target")
                << QStringList{u"assets/plan.png"_s} << u"assets/plan.png"_s << 1 << QString() << u"none"_s;
        QTest::newRow("empty index") << QStringList() << u"Firma/Adam"_s << 1 << QString() << u"new  | Firma"_s;
    }
    void resolveLinkPart()
    {
        QFETCH(QStringList, files);
        QFETCH(QString, target);
        QFETCH(int, part);
        QFETCH(QString, from);
        QFETCH(QString, expected);
        const auto index = indexOf(files);
        QCOMPARE(describe(index->resolveLinkPart(target, part, from)), expected);
    }

    void noteFolders()
    {
        VaultIndex index;
        QVERIFY(!index.noteFolder(u"Firma"_s));
        index.update(u"Firma/Adam.md"_s, u"- a"_s);
        QCOMPARE(index.noteFolder(u"Firma"_s).value_or(QString()), u"Firma"_s);
        // Case-insensitive, NFC, with the index's spelling; slashes around ignored.
        QCOMPARE(index.noteFolder(u"firma/"_s).value_or(QString()), u"Firma"_s);
        QCOMPARE(index.noteFolder(u"/FIRMA"_s).value_or(QString()), u"Firma"_s);
        QVERIFY(!index.noteFolder(QString()));
        QVERIFY(!index.noteFolder(u"/"_s));
        QVERIFY(!index.noteFolder(u"Firma/Adam"_s)); // a note, not a folder
        index.update(u"Firma/Ewa.md"_s, u"- b"_s);
        index.remove(u"Firma/Adam.md"_s);
        QVERIFY(index.noteFolder(u"Firma"_s)); // Ewa still there
        index.remove(u"Firma/Ewa.md"_s);
        QVERIFY(!index.noteFolder(u"Firma"_s)); // gone with its last note
        // Nested: every ancestor counts.
        index.update(u"A/B/C.md"_s, u"- c"_s);
        QCOMPARE(index.noteFolder(u"A"_s).value_or(QString()), u"A"_s);
        QCOMPARE(index.noteFolder(u"a/b"_s).value_or(QString()), u"A/B"_s);
        // Polish names in any normalization.
        index.update(u"Zwierzęta/Żółw.md"_s, u"- d"_s);
        QCOMPARE(index.noteFolder(u"zwierzęta"_s).value_or(QString()), u"Zwierzęta"_s);
        // Attachments do not count.
        index.update(u"Zdjęcia/kot.png"_s, QString());
        QVERIFY(!index.noteFolder(u"Zdjęcia"_s));
        // A rename moves the folder.
        index.update(u"Stare/Notatka.md"_s, u"- e"_s);
        index.rename(u"Stare/Notatka.md"_s, u"Nowe/Notatka.md"_s, u"- e"_s);
        QVERIFY(!index.noteFolder(u"Stare"_s));
        QCOMPARE(index.noteFolder(u"Nowe"_s).value_or(QString()), u"Nowe"_s);
        // A note updated in place keeps the count.
        index.update(u"Nowe/Notatka.md"_s, u"- zmienione"_s);
        QVERIFY(index.noteFolder(u"Nowe"_s));
        index.remove(u"Nowe/Notatka.md"_s);
        QVERIFY(!index.noteFolder(u"Nowe"_s));
        index.clear();
        QVERIFY(!index.noteFolder(u"A"_s));
    }

    void folderSpellingFollowsTheNotes()
    {
        // A case-only folder rename, one note at a time (Workspace renames file by file): the
        // folder's spelling moves with its notes, so a folder part opens the folder that exists.
        VaultIndex index;
        index.update(u"projekty/a.md"_s, u"- a"_s);
        index.update(u"projekty/b.md"_s, u"- b"_s);
        index.update(u"projekty/żółw/c.md"_s, u"- c"_s);
        QCOMPARE(index.noteFolder(u"Projekty"_s).value_or(QString()), u"projekty"_s);
        index.rename(u"projekty/a.md"_s, u"Projekty/a.md"_s, u"- a"_s);
        QCOMPARE(index.noteFolder(u"Projekty"_s).value_or(QString()), u"projekty"_s); // two notes left
        index.rename(u"projekty/b.md"_s, u"Projekty/b.md"_s, u"- b"_s);
        QCOMPARE(index.noteFolder(u"Projekty"_s).value_or(QString()), u"projekty"_s); // c is left
        index.rename(u"projekty/żółw/c.md"_s, u"Projekty/Żółw/c.md"_s, u"- c"_s);
        QCOMPARE(index.noteFolder(u"projekty"_s).value_or(QString()), u"Projekty"_s);
        QCOMPARE(index.noteFolder(u"PROJEKTY/ŻÓŁW"_s).value_or(QString()), u"Projekty/Żółw"_s);
        QCOMPARE(describe(index.resolveLinkPart(u"Projekty/Nowy pomysł"_s, 1)), u"folder Projekty | Projekty"_s);
        // Edits in place do not change the spelling.
        index.update(u"Projekty/a.md"_s, u"- zmienione"_s);
        QCOMPARE(index.noteFolder(u"projekty"_s).value_or(QString()), u"Projekty"_s);

        // Two folders differing only in case (a case-sensitive file system after a sync): the
        // first keeps the spelling while it has notes, then the one with the most notes left.
        VaultIndex two;
        two.update(u"Firma/a.md"_s, u"- a"_s);
        two.update(u"firma/b.md"_s, u"- b"_s);
        two.update(u"FIRMA/c.md"_s, u"- c"_s);
        two.update(u"FIRMA/d.md"_s, u"- d"_s);
        QCOMPARE(two.noteFolder(u"firma"_s).value_or(QString()), u"Firma"_s);
        two.update(u"Firma/a.md"_s, u"- zmienione"_s); // its only note edited: no flip
        QCOMPARE(two.noteFolder(u"firma"_s).value_or(QString()), u"Firma"_s);
        two.remove(u"Firma/a.md"_s);
        QCOMPARE(two.noteFolder(u"firma"_s).value_or(QString()), u"FIRMA"_s);
        two.remove(u"FIRMA/c.md"_s);
        two.remove(u"FIRMA/d.md"_s);
        QCOMPARE(two.noteFolder(u"Firma"_s).value_or(QString()), u"firma"_s);
        two.remove(u"firma/b.md"_s);
        QVERIFY(!two.noteFolder(u"Firma"_s));
        // A spelling that comes back after it was gone starts counting again.
        two.update(u"Firma/e.md"_s, u"- e"_s);
        two.update(u"firma/f.md"_s, u"- f"_s);
        two.remove(u"firma/f.md"_s);
        two.remove(u"Firma/e.md"_s);
        QVERIFY(!two.noteFolder(u"firma"_s));
        // A removal of a note the index does not know leaves the counts alone.
        two.update(u"Firma/g.md"_s, u"- g"_s);
        two.remove(u"firma/brak.md"_s);
        QCOMPARE(two.noteFolder(u"firma"_s).value_or(QString()), u"Firma"_s);
    }

    void partsFollowTheIndex()
    {
        // The folder gets its first note or loses its last: the part's target follows.
        VaultIndex index;
        index.update(u"Inne.md"_s, u"- x"_s);
        QCOMPARE(describe(index.resolveLinkPart(u"Firma/Adam"_s, 1)), u"new  | Firma"_s);
        index.update(u"Firma/Ewa.md"_s, u"- y"_s);
        QCOMPARE(describe(index.resolveLinkPart(u"Firma/Adam"_s, 1)), u"folder Firma | Firma"_s);
        index.update(u"Firma.md"_s, u"- z"_s);
        QCOMPARE(describe(index.resolveLinkPart(u"Firma/Adam"_s, 1)), u"page Firma.md | Firma"_s);
        index.remove(u"Firma.md"_s);
        index.remove(u"Firma/Ewa.md"_s);
        QCOMPARE(describe(index.resolveLinkPart(u"Firma/Adam"_s, 1)), u"new  | Firma"_s);
    }

    void foldersSurviveTheCache()
    {
        // An index restored from the index cache (decoded records, no parsing) answers as the
        // fresh one: its notes come back through the same attach path.
        const QStringList files{u"Klienci/Firma/Adam.md"_s, u"Projekty/Zażółć gęślą/Notatki.md"_s, u"Inne.md"_s,
                                u"Zdjęcia/kot.png"_s};
        const auto fresh = indexOf(files);
        const indexcache::StatFn stat = [](const VaultPath &path) -> std::optional<FileStat> {
            return FileStat{path, false, 1000 + path.size(), 1'700'000'000'000};
        };
        const indexcache::Identity identity = indexcache::Identity::current(VaultConfig(), u"vault-żółw"_s);
        const auto reader = indexcache::Reader::open(indexcache::encode(*fresh, identity, stat), identity);
        QVERIFY(reader);
        VaultIndex restored;
        int decoded = 0;
        for (const VaultPath &path : fresh->paths()) {
            const QString content = fresh->content(path);
            const indexcache::Record *record = reader->find(path);
            QVERIFY(record);
            std::optional<ParsedNote> note = reader->decodeNote(*record, path, content, VaultConfig());
            QVERIFY(note);
            restored.updatePrepared(VaultIndex::prepareParsed(path, content, std::move(*note), VaultConfig()));
            ++decoded;
        }
        QCOMPARE(decoded, 3);
        restored.update(u"Zdjęcia/kot.png"_s, QString());
        for (const QString &folder : {u"Klienci"_s, u"Klienci/Firma"_s, u"projekty/zażółć gęślą"_s, u"Zdjęcia"_s})
            QCOMPARE(restored.noteFolder(folder), fresh->noteFolder(folder));
        QCOMPARE(describe(restored.resolveLinkPart(u"Firma/Adam"_s, 1)), u"folder Klienci/Firma | Firma"_s);
        QCOMPARE(describe(restored.resolveLinkPart(u"Projekty/Zażółć gęślą/Nowa"_s, 2)),
                 describe(fresh->resolveLinkPart(u"Projekty/Zażółć gęślą/Nowa"_s, 2)));
    }
};

QTEST_GUILESS_MAIN(TestIndexLinkParts)
#include "tst_index_linkparts.moc"
