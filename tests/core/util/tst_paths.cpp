#include "util/paths.h"

#include <QSet>
#include <QTest>

using namespace brulion;

class TestPaths : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void normalize()
    {
        QCOMPARE(paths::normalize(QStringLiteral("/a//b/./c/../d.md")), QStringLiteral("a/b/d.md"));
        QCOMPARE(paths::normalize(QStringLiteral("a\\b")), QStringLiteral("a/b"));
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
