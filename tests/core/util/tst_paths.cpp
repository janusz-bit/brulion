#include "util/paths.h"

#include <QSet>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

class TestPaths : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void normalize_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("normalized");
        QTest::newRow("dots and empty segments") << u"/a//b/./c/../d.md"_s << u"a/b/d.md"_s;
        QTest::newRow("backslash") << u"a\\b"_s << u"a/b"_s;
        QTest::newRow("clean, as it is") << u"Projekty/Dom.md"_s << u"Projekty/Dom.md"_s;
        QTest::newRow("Polish, clean") << u"Zwierzęta/Żółw i łódź.md"_s << u"Zwierzęta/Żółw i łódź.md"_s;
        QTest::newRow("decomposed to NFC") << u"Z\u0307o\u0301\u0142w.md"_s << u"Żółw.md"_s;
        QTest::newRow("trailing slash") << u"Projekty/"_s << u"Projekty"_s;
        QTest::newRow("leading ./") << u"./Dom.md"_s << u"Dom.md"_s;
        QTest::newRow("a dot segment at the end") << u"a/."_s << u"a"_s;
        QTest::newRow("up past the root") << u"../../a.md"_s << u"a.md"_s;
        QTest::newRow("up to nothing") << u"a/.."_s << QString();
        QTest::newRow("dots inside a name stay") << u"a.b/..c/c..md"_s << u"a.b/..c/c..md"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("only a slash") << u"/"_s << QString();
        QTest::newRow("a space segment stays") << u"a/ /b"_s << u"a/ /b"_s;
    }
    void normalize()
    {
        QFETCH(QString, path);
        QFETCH(QString, normalized);
        QCOMPARE(paths::normalize(path), normalized);
        QCOMPARE(paths::normalize(paths::normalize(path)), normalized);
    }
    void stripMarkdownExtension_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("stripped");
        QTest::addColumn<QString>("title");
        QTest::newRow("md") << u"Projekty/Dom.md"_s << u"Projekty/Dom"_s << u"Dom"_s;
        QTest::newRow("markdown, upper case") << u"Żółw.MARKDOWN"_s << u"Żółw"_s << u"Żółw"_s;
        QTest::newRow("Md") << u"a/Łąka.Md"_s << u"a/Łąka"_s << u"Łąka"_s;
        QTest::newRow("only the last one") << u"Notatka.md.md"_s << u"Notatka.md"_s << u"Notatka.md"_s;
        QTest::newRow("another extension") << u"assets/plan.png"_s << u"assets/plan.png"_s << u"plan.png"_s;
        QTest::newRow("md inside the name") << u"a.md/b.mdx"_s << u"a.md/b.mdx"_s << u"b.mdx"_s;
        QTest::newRow("no dot") << u"Projekty/md"_s << u"Projekty/md"_s << u"md"_s;
        QTest::newRow("only the extension") << u".md"_s << QString() << QString();
        QTest::newRow("empty") << QString() << QString() << QString();
    }
    void stripMarkdownExtension()
    {
        QFETCH(QString, path);
        QFETCH(QString, stripped);
        QFETCH(QString, title);
        QCOMPARE(paths::stripMarkdownExtension(path), stripped);
        QCOMPARE(paths::pageTitle(path), title);
    }
    void ignored()
    {
        QVERIFY(paths::isIgnored(QStringLiteral(".obsidian/app.json")));
        QVERIFY(paths::isIgnored(QStringLiteral("notes/.stversions/x.md")));
        QVERIFY(paths::isIgnored(QStringLiteral("notes/~syncthing~x.md.tmp")));
        QVERIFY(!paths::isIgnored(QStringLiteral("journals/2026-10-14.md")));
    }
    void syncthingConflict()
    {
        const auto c = paths::parseSyncthingConflict(QStringLiteral("a/Notatka.sync-conflict-20261014-101112-ABCDEFG.md"));
        QVERIFY(c.has_value());
        QCOMPARE(c->original, QStringLiteral("a/Notatka.md"));
        QCOMPARE(c->time, QStringLiteral("2026-10-14T10:11:12"));
        QCOMPARE(c->device, QStringLiteral("ABCDEFG"));
        QVERIFY(!paths::parseSyncthingConflict(QStringLiteral("a/Notatka.md")).has_value());
    }
    void picturePaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("picture");
        QTest::newRow("jpg, upper case") << QStringLiteral("assets/foto.JPG") << true;
        QTest::newRow("jpeg") << QStringLiteral("assets/zdjęcie z wakacji.jpeg") << true;
        QTest::newRow("png") << QStringLiteral("assets/żółw.png") << true;
        QTest::newRow("gif") << QStringLiteral("animacja.gif") << true;
        QTest::newRow("webp") << QStringLiteral("assets/łąka.WebP") << true;
        QTest::newRow("svg") << QStringLiteral("assets/plan-mieszkania.svg") << true;
        QTest::newRow("bmp") << QStringLiteral("skan.bmp") << true;
        QTest::newRow("avif") << QStringLiteral("zdj.avif") << true;
        // The HTML export showed .ico files as pictures while the app opened them as text.
        QTest::newRow("ico") << QStringLiteral("assets/favicon.ico") << true;
        QTest::newRow("markdown") << QStringLiteral("notatka.md") << false;
        QTest::newRow("zip") << QStringLiteral("assets/archiwum.zip") << false;
        QTest::newRow("pdf") << QStringLiteral("assets/umowa.pdf") << false;
        QTest::newRow("compressed svg") << QStringLiteral("assets/mapa.svgz") << false;
        QTest::newRow("heic (no decoder in Qt)") << QStringLiteral("IMG_0001.heic") << false;
        QTest::newRow("a name, not an extension") << QStringLiteral("assets/svg") << false;
        QTest::newRow("a hidden file") << QStringLiteral("assets/.png") << false;
        QTest::newRow("picture extension in a folder") << QStringLiteral("zdjęcia.png/opis.md") << false;
        QTest::newRow("empty") << QString() << false;
    }
    void picturePaths()
    {
        QFETCH(QString, path);
        QFETCH(bool, picture);
        QCOMPARE(paths::isPicturePath(path), picture);
    }
    void pictureExtensions()
    {
        // One list: what isPicturePath accepts is exactly what the file dialogs and the export
        // name (lower-case, with the dot, no duplicates).
        const QStringList &ext = paths::pictureExtensions();
        QVERIFY(ext.size() >= 9);
        QCOMPARE(QSet<QString>(ext.cbegin(), ext.cend()).size(), ext.size());
        for (const QString &e : ext) {
            QVERIFY2(e.startsWith(u'.') && e == e.toLower(), qPrintable(e));
            QVERIFY2(paths::isPicturePath(u"assets/obraz"_s + e), qPrintable(e));
            QVERIFY2(paths::isPicturePath(u"assets/OBRAZ"_s + e.toUpper()), qPrintable(e));
        }
        QVERIFY(ext.contains(u".ico"_s));
        QVERIFY(!ext.contains(u".md"_s));
    }
    void uniquePath()
    {
        const QSet<QString> taken = {QStringLiteral("żółw.md"), QStringLiteral("żółw 2.md")};
        QCOMPARE(paths::uniquePath(QStringLiteral("żółw.md"), [&](const QString &p) { return taken.contains(p); }),
                 QStringLiteral("żółw 3.md"));
    }
    void sanitize()
    {
        QCOMPARE(paths::sanitizeFileName(QStringLiteral("  Zakupy: ser/mleko?  ")), QStringLiteral("Zakupy ser mleko"));
        QCOMPARE(paths::sanitizeFileName(QStringLiteral("...")), QStringLiteral("Untitled"));
    }
};

QTEST_GUILESS_MAIN(TestPaths)
#include "tst_paths.moc"
