// Item references (notebook/itemref.h): "Dom.md#L12@3f9a01bc", "Dom.md#^abc123", "^abc123" and
// "((abc123))" are formatted and parsed; malformed ones are refused; resolve() finds a line again
// after lines were inserted above it, refuses one whose text changed and prefers the nearest of
// identical lines.
#include "notebook/itemref.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::notebook;

Q_DECLARE_METATYPE(std::optional<ItemRef>)

class TestNotebookItemRef : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("page");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("hash");
        QTest::addColumn<QString>("blockId");
        QTest::newRow("line and hash") << u"Projekty/Dom.md#L12@3f9a01bc"_s << true << u"Projekty/Dom.md"_s << 11
                                       << u"3f9a01bc"_s << QString();
        QTest::newRow("line only") << u"Dom.md#L1"_s << true << u"Dom.md"_s << 0 << QString() << QString();
        QTest::newRow("Polish path") << u"Zwierzęta/Żółw.md#L3@00ff00aa"_s << true << u"Zwierzęta/Żółw.md"_s << 2
                                     << u"00ff00aa"_s << QString();
        QTest::newRow("page block") << u"Dom.md#^abc123"_s << true << u"Dom.md"_s << -1 << QString() << u"abc123"_s;
        QTest::newRow("page name block") << u"Dom#^abc"_s << true << u"Dom"_s << -1 << QString() << u"abc"_s;
        QTest::newRow("caret id") << u"^abc123"_s << true << QString() << -1 << QString() << u"abc123"_s;
        QTest::newRow("block ref") << u"((abc123))"_s << true << QString() << -1 << QString() << u"abc123"_s;
        QTest::newRow("logseq uuid") << u"((6512bd43-d9ca-4f7a-8c2e-1a2b3c4d5e6f))"_s << true << QString() << -1
                                     << QString() << u"6512bd43-d9ca-4f7a-8c2e-1a2b3c4d5e6f"_s;
        QTest::newRow("plain page") << u"Projekty/Dom"_s << true << u"Projekty/Dom"_s << -1 << QString() << QString();
        QTest::newRow("hash in a name") << u"C# notatki"_s << true << u"C# notatki"_s << -1 << QString() << QString();
        QTest::newRow("spaces trimmed") << u"  Dom.md#L2  "_s << true << u"Dom.md"_s << 1 << QString() << QString();
        QTest::newRow("no line number") << u"Dom.md#L"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("line zero") << u"Dom.md#L0"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("negative line") << u"Dom.md#L-1"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("plus sign") << u"Dom.md#L+1"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("bad hash") << u"Dom.md#L1@zz"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("upper-case hash") << u"Dom.md#L1@3F9A01BC"_s << false << QString() << -1 << QString()
                                         << QString();
        QTest::newRow("no page") << u"#L4"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("bad id") << u"^ab c"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("empty ref") << u"(())"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("NUL") << u"Dom.md\0#L1"_s << false << QString() << -1 << QString() << QString();
        QTest::newRow("too long") << QString(10000, u'a') << false << QString() << -1 << QString() << QString();
        QTest::newRow("empty") << u"   "_s << false << QString() << -1 << QString() << QString();
    }
    void parse()
    {
        QFETCH(QString, text);
        QFETCH(bool, valid);
        QFETCH(QString, page);
        QFETCH(int, line);
        QFETCH(QString, hash);
        QFETCH(QString, blockId);
        const auto ref = parseItemRef(text);
        QCOMPARE(ref.has_value(), valid);
        if (!ref)
            return;
        QCOMPARE(ref->page, page);
        QCOMPARE(ref->line, line);
        QCOMPARE(ref->lineHash, hash);
        QCOMPARE(ref->blockId, blockId);
        // Round trip through the formatter (blocks without a page as "^id").
        QCOMPARE(parseItemRef(formatRef(*ref)), ref);
    }

    void format()
    {
        QCOMPARE(lineHash(u"- [ ] Kupić mleko"), lineHash(u"- [ ] Kupić mleko"));
        QCOMPARE(lineHash(u"a").size(), 8);
        QVERIFY(lineHash(u"a") != lineHash(u"b"));
        QCOMPARE(formatLineRef(u"Dom.md"_s, 0, u"x"), u"Dom.md#L1@"_s + lineHash(u"x"));
        QCOMPARE(formatBlockRef(u"Dom.md"_s, u"abc"_s), u"Dom.md#^abc"_s);
        QCOMPARE(formatBlockRef(QString(), u"abc"_s), u"^abc"_s);
        QVERIFY(looksLikeItemRef(u"Dom.md#L3"));
        QVERIFY(looksLikeItemRef(u"((x1))"));
        QVERIFY(!looksLikeItemRef(u"C# notatki"));
        QVERIFY(!looksLikeItemRef(u"Dom"));
    }

    void resolveLines()
    {
        const QString content = u"- [ ] a\n- [ ] b\n- [ ] c\n"_s;
        const ItemRef ref{u"x.md"_s, 1, lineHash(u"- [ ] b"), QString()};
        // Unchanged.
        ResolvedLine r = resolve(content, ref);
        QCOMPARE(r.status, ResolvedLine::Ok);
        QCOMPARE(r.line, 1);
        // Two lines inserted above: moved.
        r = resolve(u"# Nowe\n\n"_s + content, ref);
        QCOMPARE(r.status, ResolvedLine::Moved);
        QCOMPARE(r.line, 3);
        // The text changed: stale.
        r = resolve(u"- [ ] a\n- [x] b ✅ 2026-10-14\n- [ ] c\n"_s, ref);
        QCOMPARE(r.status, ResolvedLine::Stale);
        // Gone with the note shorter: stale too.
        QCOMPARE(resolve(u"- [ ] a\n"_s, ref).status, ResolvedLine::Stale);
        // Identical lines: the nearest wins.
        const ItemRef dup{u"x.md"_s, 3, lineHash(u"- [ ] zakupy"), QString()};
        r = resolve(u"- [ ] zakupy\nx\nx\ny\n- [ ] zakupy\n"_s, dup);
        QCOMPARE(r.status, ResolvedLine::Moved);
        QCOMPARE(r.line, 4);
        // Far away (beyond 50 lines): stale, never a guess.
        QString far = u"- [ ] daleko\n"_s;
        for (int i = 0; i < 80; ++i)
            far.prepend(u"- wiersz\n"_s);
        QCOMPARE(resolve(far, ItemRef{u"x.md"_s, 0, lineHash(u"- [ ] daleko"), {}}).status, ResolvedLine::Stale);
        // CRLF: the line text is without its terminator.
        r = resolve(u"- a\r\n- [ ] b\r\n"_s, ref);
        QCOMPARE(r.status, ResolvedLine::Ok);
        // Without a hash: the line as it is; out of range: not found.
        QCOMPARE(resolve(content, ItemRef{u"x.md"_s, 2, {}, {}}).status, ResolvedLine::Ok);
        QCOMPARE(resolve(content, ItemRef{u"x.md"_s, 3, {}, {}}).status, ResolvedLine::NotFound);
    }

    void resolveBlocks()
    {
        const QString content = u"- a\n- b ^abc123\n  - dziecko\n- c\n  id:: 6512bd43-d9ca-4f7a-8c2e-1a2b3c4d5e6f\n"_s;
        ResolvedLine r = resolve(content, ItemRef{QString(), -1, {}, u"abc123"_s});
        QCOMPARE(r.status, ResolvedLine::Ok);
        QCOMPARE(r.line, 1);
        r = resolve(content, ItemRef{QString(), -1, {}, u"6512bd43-d9ca-4f7a-8c2e-1a2b3c4d5e6f"_s});
        QCOMPARE(r.line, 3);
        QCOMPARE(resolve(content, ItemRef{QString(), -1, {}, u"brak"_s}).status, ResolvedLine::NotFound);
    }
};

QTEST_GUILESS_MAIN(TestNotebookItemRef)
#include "tst_notebook_itemref.moc"
