// util/strings.h: where a text may be cut (never inside a surrogate pair) and random text of an alphabet.
#include "util/strings.h"

#include <QSet>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

class TestStrings : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cutLength_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<qsizetype>("max");
        QTest::addColumn<qsizetype>("kept");
        const QString emoji = u"ab😀cd"_s; // a b <high> <low> c d
        QTest::newRow("shorter") << u"zażółć"_s << qsizetype(10) << qsizetype(6);
        QTest::newRow("exact") << u"zażółć"_s << qsizetype(6) << qsizetype(6);
        QTest::newRow("polish letters are one unit each") << u"zażółć gęślą"_s << qsizetype(4) << qsizetype(4);
        QTest::newRow("before the pair") << emoji << qsizetype(2) << qsizetype(2);
        QTest::newRow("inside the pair: before it") << emoji << qsizetype(3) << qsizetype(2);
        QTest::newRow("after the pair") << emoji << qsizetype(4) << qsizetype(4);
        QTest::newRow("only a pair, cut inside") << u"😀"_s << qsizetype(1) << qsizetype(0);
        QTest::newRow("zero") << emoji << qsizetype(0) << qsizetype(0);
        QTest::newRow("negative") << emoji << qsizetype(-5) << qsizetype(0);
        QTest::newRow("empty") << QString() << qsizetype(3) << qsizetype(0);
        QTest::newRow("nothing cut: a lone high surrogate at the end stays")
            << (u"ab"_s + QChar(0xD83D)) << qsizetype(3) << qsizetype(3);
    }
    void cutLength()
    {
        QFETCH(QString, text);
        QFETCH(qsizetype, max);
        QFETCH(qsizetype, kept);
        QCOMPARE(strings::cutLength(text, max), kept);
    }

    void cutNeverSplitsAPairAnywhere()
    {
        const QString s = u"😀ą😀😀ę1😀"_s;
        for (qsizetype max = 0; max <= s.size() + 1; ++max) {
            const qsizetype n = strings::cutLength(s, max);
            QVERIFY(n <= std::max<qsizetype>(max, 0));
            QVERIFY(n <= s.size());
            QVERIFY2(n == 0 || n == s.size() || !s.at(n - 1).isHighSurrogate(), qPrintable(QString::number(max)));
            QVERIFY(max >= s.size() || n >= max - 1);
        }
    }

    void randomTextUsesOnlyTheAlphabetAndTheLength()
    {
        const QString alphabet = u"abcdef0123456789"_s;
        QSet<QString> seen;
        QSet<QChar> used;
        for (int i = 0; i < 200; ++i) {
            const QString id = strings::randomText(alphabet, 12);
            QCOMPARE(id.size(), 12);
            for (QChar c : id) {
                QVERIFY2(alphabet.contains(c), qPrintable(id));
                used.insert(c);
            }
            seen.insert(id);
        }
        QCOMPARE(seen.size(), 200);           // 16^12: no repeats in 200
        QCOMPARE(used.size(), alphabet.size()); // every letter turns up in 2400 draws
    }

    void randomTextOfNothing()
    {
        QCOMPARE(strings::randomText(u"ab"_s, 0), QString());
        QCOMPARE(strings::randomText(u"ab"_s, -3), QString());
        QCOMPARE(strings::randomText(u"x"_s, 3), u"xxx"_s);
        QCOMPARE(strings::randomText(QString(), 3), QString());
    }
};

QTEST_GUILESS_MAIN(TestStrings)
#include "tst_strings.moc"
