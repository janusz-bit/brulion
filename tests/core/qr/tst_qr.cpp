// The core module qr (src/core/qr): codes that read back as their text (qrdecode.h, a reader
// written apart from the generator), the smallest version and as much error correction as it
// allows, the text of a note's codes (Markdown with its title after the frontmatter, plain text)
// and that text cut into codes — joined again byte for byte, never inside a character.
#include "qr/qrcode.h"
#include "qr/qrtext.h"

#include "qrdecode.h"

#include <QTest>
#include <QTextBoundaryFinder>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace brulion::qr;

namespace {

/// `code` read back by the test's own reader (a failure names why).
std::optional<qrtest::Decoded> readBack(const Code &code, QString *why)
{
    return qrtest::decode(code.size, code.modules, why);
}

char eccLetter(Ecc ecc)
{
    switch (ecc) {
    case Ecc::Low: return 'L';
    case Ecc::Medium: return 'M';
    case Ecc::Quartile: return 'Q';
    case Ecc::High: return 'H';
    }
    return '?';
}

/// `text` repeated until it is `count` characters (UTF-16) or more.
QString repeated(const QString &text, qsizetype count)
{
    QString out;
    while (out.size() < count)
        out += text;
    return out;
}

/// The places `text` may be cut between characters (grapheme cluster boundaries).
QList<qsizetype> graphemeBoundaries(const QString &text)
{
    QList<qsizetype> out{0};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    for (qsizetype at = finder.toNextBoundary(); at > 0; at = finder.toNextBoundary())
        out.append(at);
    return out;
}

} // namespace

class TestQr : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- One code -------------------------------------------------------------------------------

    void readsBack_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("ascii") << u"Ala ma kota"_s;
        QTest::newRow("polish") << u"Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ"_s;
        QTest::newRow("emoji") << u"Lista 🛒: mleko, chleb ☕ 👨‍👩‍👧"_s;
        QTest::newRow("markdown") << u"# Zakupy\n\n- [ ] mleko\n- [x] chleb ✅ 2026-11-13\n  - razowy\n"_s;
        QTest::newRow("version 9, 8-bit count") << repeated(u"ż"_s, 115);   // 230 bytes: version 9 at L
        QTest::newRow("version 10, 16-bit count") << repeated(u"ż"_s, 116); // 232 bytes: version 10
        QTest::newRow("version 7, version information") << repeated(u"abc"_s, 150);
        QTest::newRow("full") << repeated(u"zażółć "_s, 1876); // 268 × 11 bytes: version 40
        QTest::newRow("most bytes") << QString(MaxBytes, u'x');
    }
    void readsBack()
    {
        QFETCH(QString, text);
        const QByteArray utf8 = text.toUtf8();
        QVERIFY(utf8.size() <= MaxBytes);
        const Code code = encode(text);
        QVERIFY(!code.isNull());
        QCOMPARE(code.size, code.version * 4 + 17);
        QCOMPARE(code.modules.size(), qsizetype(code.size) * code.size);
        QString why;
        const std::optional<qrtest::Decoded> decoded = readBack(code, &why);
        QVERIFY2(decoded.has_value(), qPrintable(why));
        QCOMPARE(decoded->bytes, utf8);
        QCOMPARE(QString::fromUtf8(decoded->bytes), text);
        QCOMPARE(decoded->version, code.version);
        QCOMPARE(decoded->ecc, eccLetter(code.ecc));
        QCOMPARE(decoded->mask, code.mask);
        QVERIFY(code.mask >= 0 && code.mask <= 7);
    }

    // The smallest version that holds the bytes at L, the correction raised as far as it allows
    // (ISO/IEC 18004 table 7: version 1 holds 17 / 14 / 11 / 7 bytes, version 2 32 / 26 / 20 / 14).
    void smallestVersion_data()
    {
        QTest::addColumn<int>("bytes");
        QTest::addColumn<int>("version");
        QTest::addColumn<char>("ecc");
        QTest::newRow("0") << 0 << 1 << 'H';
        QTest::newRow("7") << 7 << 1 << 'H';
        QTest::newRow("8") << 8 << 1 << 'Q';
        QTest::newRow("11") << 11 << 1 << 'Q';
        QTest::newRow("12") << 12 << 1 << 'M';
        QTest::newRow("14") << 14 << 1 << 'M';
        QTest::newRow("15") << 15 << 1 << 'L';
        QTest::newRow("17") << 17 << 1 << 'L';
        QTest::newRow("18") << 18 << 2 << 'Q';
        QTest::newRow("32") << 32 << 2 << 'L';
        QTest::newRow("2953") << 2953 << 40 << 'L';
    }
    void smallestVersion()
    {
        QFETCH(int, bytes);
        QFETCH(int, version);
        QFETCH(char, ecc);
        const Code code = encodeBytes(QByteArray(bytes, 'a'));
        QCOMPARE(code.version, version);
        QCOMPARE(eccLetter(code.ecc), ecc);
    }

    void tooLong()
    {
        QVERIFY(encodeBytes(QByteArray(MaxBytes + 1, 'a')).isNull());
        // 2 bytes a letter: 1477 letters are 2954 bytes.
        QVERIFY(encode(QString(1477, u'ą')).isNull());
        QVERIFY(!encode(QString(1476, u'ą')).isNull());
        const Code none = encodeBytes(QByteArray(10'000, 'a'));
        QCOMPARE(none.size, 0);
        QCOMPARE(none.version, 0);
        QVERIFY(none.modules.isEmpty());
        QVERIFY(!none.dark(0, 0));
    }

    void outsideIsLight()
    {
        const Code code = encode(u"Brulion"_s);
        QVERIFY(code.dark(0, 0)); // a finder pattern's corner
        QVERIFY(!code.dark(-1, 0));
        QVERIFY(!code.dark(0, -1));
        QVERIFY(!code.dark(code.size, 0));
        QVERIFY(!code.dark(0, code.size));
    }

    // --- The text of a note's codes ---------------------------------------------------------------

    void utf8Size_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("ascii") << u"abc"_s;
        QTest::newRow("polish") << u"żółć"_s;
        QTest::newRow("euro") << u"€ 10"_s;
        QTest::newRow("emoji") << u"🛒☕"_s;
        QTest::newRow("combining") << u"a"_s + QChar(0x0328);
        QTest::newRow("lone high surrogate") << QString(QChar(0xD83D)) + u"a"_s;
        QTest::newRow("lone low surrogate") << u"a"_s + QChar(0xDE00);
        QTest::newRow("high surrogate at the end") << u"a"_s + QChar(0xD83D);
    }
    void utf8Size()
    {
        QFETCH(QString, text);
        // What the code holds: the text's UTF-8, a lone surrogate as U+FFFD (EF BF BD).
        QByteArray expected;
        for (qsizetype i = 0; i < text.size(); ++i) {
            const bool pair = text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate();
            if (text[i].isSurrogate() && !pair)
                expected += "\xEF\xBF\xBD";
            else
                expected += QStringView(text).sliced(i, pair ? 2 : 1).toUtf8();
            if (pair)
                ++i;
        }
        QCOMPARE(brulion::qr::utf8Size(text), expected.size());
        const Code code = encode(text);
        QString why;
        const std::optional<qrtest::Decoded> decoded = readBack(code, &why);
        QVERIFY2(decoded.has_value(), qPrintable(why));
        QCOMPARE(decoded->bytes, expected);
    }

    void noteText_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<bool>("plain");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("expected");
        const QString list = u"- mleko\n- [ ] chleb\n- [x] masło\n  - zagnieżdżone\n"_s;
        QTest::newRow("title") << u"- mleko\n- chleb\n"_s << false << u"Zakupy"_s << u"# Zakupy\n\n- mleko\n- chleb"_s;
        QTest::newRow("no title") << u"- mleko\n- chleb\n"_s << false << QString() << u"- mleko\n- chleb"_s;
        QTest::newRow("own title") << u"# Lista na sobotę\n\n- mleko\n"_s << false << u"Zakupy"_s
                                   << u"# Lista na sobotę\n\n- mleko"_s;
        QTest::newRow("own setext title")
                << u"Lista\n=====\n\n- mleko\n"_s << false << u"Zakupy"_s << u"Lista\n=====\n\n- mleko"_s;
        QTest::newRow("second level heading")
                << u"## Sobota\n- mleko\n"_s << false << u"Zakupy"_s << u"# Zakupy\n\n## Sobota\n- mleko"_s;
        QTest::newRow("frontmatter") << u"---\ntags: dom\n---\n- mleko\n"_s << false << u"Zakupy"_s
                                     << u"---\ntags: dom\n---\n# Zakupy\n\n- mleko"_s;
        QTest::newRow("frontmatter, blank line") << u"---\ntags: dom\n---\n\n- mleko\n"_s << false << u"Zakupy"_s
                                                 << u"---\ntags: dom\n---\n\n# Zakupy\n\n- mleko"_s;
        QTest::newRow("frontmatter, own title")
                << u"---\ntags: dom\n---\n# Plan\n"_s << false << u"Zakupy"_s << u"---\ntags: dom\n---\n# Plan"_s;
        QTest::newRow("page properties") << u"tags:: dom\nicon:: 🛒\n\n- mleko\n"_s << false << u"Zakupy"_s
                                         << u"tags:: dom\nicon:: 🛒\n\n# Zakupy\n\n- mleko"_s;
        QTest::newRow("crlf") << u"- a\r\n- b\r\n"_s << false << QString() << u"- a\n- b"_s;
        QTest::newRow("cr") << u"- a\r- b\r"_s << false << u"T"_s << u"# T\n\n- a\n- b"_s;
        QTest::newRow("bom") << QString(QChar(0xFEFF)) + u"- a\n"_s << false << QString() << u"- a"_s;
        QTest::newRow("blank lines around") << u"\n\n  \n- a\n\n\n  "_s << false << QString() << u"- a"_s;
        QTest::newRow("indented first line") << u"\n    kod\n"_s << false << QString() << u"    kod"_s;
        QTest::newRow("title on one line") << u"- a\n"_s << false << u"  Dwa\nwiersze  "_s << u"# Dwa wiersze\n\n- a"_s;
        QTest::newRow("empty") << QString() << false << u"Zakupy"_s << QString();
        QTest::newRow("blank") << u"  \n\n\t\n"_s << false << u"Zakupy"_s << QString();
        QTest::newRow("frontmatter only") << u"---\ntags: dom\n---\n"_s << false << u"Zakupy"_s << QString();
        QTest::newRow("page properties only") << u"tags:: dom\n"_s << true << u"Zakupy"_s << QString();
        QTest::newRow("plain") << list << true << QString() << u"• mleko\n☐ chleb\n☑ masło\n  • zagnieżdżone"_s;
        QTest::newRow("plain, title") << list << true << u"Zakupy"_s
                                      << u"Zakupy\n\n• mleko\n☐ chleb\n☑ masło\n  • zagnieżdżone"_s;
        QTest::newRow("plain, own title") << u"# Lista\n\nTekst **pogrubiony** i [[Strona|link]].\n"_s << true
                                          << u"Zakupy"_s << u"Lista\n\nTekst pogrubiony i link."_s;
        QTest::newRow("plain, frontmatter") << u"---\na: b\n---\nTreść\n"_s << true << u"T"_s << u"T\n\nTreść"_s;
        QTest::newRow("plain, block id") << u"- zadanie ^abc123\n"_s << true << QString() << u"• zadanie"_s;
        QTest::newRow("plain, crlf") << u"- a\r\n- b\r\n"_s << true << QString() << u"• a\n• b"_s;
    }
    void noteText()
    {
        QFETCH(QString, markdown);
        QFETCH(bool, plain);
        QFETCH(QString, title);
        QFETCH(QString, expected);
        NoteTextOptions options;
        options.format = plain ? TextFormat::PlainText : TextFormat::Markdown;
        options.title = title;
        QCOMPARE(brulion::qr::noteText(markdown, options), expected);
    }

    void blockReferences()
    {
        NoteTextOptions options;
        options.format = TextFormat::PlainText;
        options.blockText = [](const QString &id) { return id == u"abc123" ? u"tekst bloku"_s : QString(); };
        const QString text = brulion::qr::noteText(u"- Zobacz ((abc123)) i ((nieznany))\n"_s, options);
        QVERIFY2(text.contains(u"tekst bloku"_s), qPrintable(text));
        QVERIFY2(!text.contains(u"((abc123))"_s), qPrintable(text));
        // Markdown keeps the references as written.
        options.format = TextFormat::Markdown;
        QCOMPARE(brulion::qr::noteText(u"- Zobacz ((abc123))\n"_s, options), u"- Zobacz ((abc123))"_s);
    }

    void hasOwnTitle_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<bool>("own");
        QTest::newRow("heading") << u"# Plan\n"_s << true;
        QTest::newRow("setext") << u"Plan\n===\n"_s << true;
        QTest::newRow("after frontmatter") << u"---\na: b\n---\n# Plan\n"_s << true;
        QTest::newRow("after page properties") << u"tags:: dom\n\n# Plan\n"_s << true;
        QTest::newRow("second level") << u"## Plan\n"_s << false;
        QTest::newRow("later") << u"Tekst\n\n# Plan\n"_s << false;
        QTest::newRow("in a list") << u"- # Plan\n"_s << false;
        QTest::newRow("empty") << QString() << false;
    }
    void hasOwnTitle()
    {
        QFETCH(QString, markdown);
        QFETCH(bool, own);
        QCOMPARE(brulion::qr::hasOwnTitle(markdown), own);
    }

    // --- Cut into codes -----------------------------------------------------------------------------

    void splitSmall()
    {
        QCOMPARE(splitText(QString()), QStringList());
        QCOMPARE(splitText(u"abc"_s), QStringList{u"abc"_s});
        const QString full(MaxBytes, u'x');
        QCOMPARE(splitText(full), QStringList{full});
        QCOMPARE(splitText(full + u'x').size(), 2);
    }

    void splitJoins_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("maxBytes");
        // How many parts: -1 as few as the size allows (its bytes / maxBytes, rounded up).
        QTest::addColumn<int>("parts");
        QTest::newRow("sentences") << repeated(u"Ala ma kota. "_s, 13'000) << 2953 << 5;
        QTest::newRow("lines") << repeated(u"Linia: zażółć gęślą jaźń\n"_s, 9000) << 2953 << -1;
        QTest::newRow("one word") << QString(5000, u'x') << 2953 << 2;
        QTest::newRow("polish letters") << QString(3000, u'ż') << 2953 << 3;
        QTest::newRow("emoji") << repeated(u"🛒"_s, 2000) << 2953 << 2;
        QTest::newRow("combining marks") << repeated(u"a"_s + QChar(0x0328), 4000) << 2953 << 3;
        QTest::newRow("emoji sequences") << repeated(u"👨‍👩‍👧"_s, 3000) << 2953 << -1;
        QTest::newRow("small codes") << repeated(u"Zażółć gęślą jaźń. "_s, 500) << 100 << 8;
        // "a🛒b" in 4 bytes a part (1 is taken as 4): no two parts hold it.
        QTest::newRow("tiny codes") << u"a🛒b"_s << 1 << 3;
        QTest::newRow("a cluster too large") << u"a"_s + QString(3000, QChar(0x0301)) << 2953 << 3;
    }
    void splitJoins()
    {
        QFETCH(QString, text);
        QFETCH(int, maxBytes);
        QFETCH(int, parts);
        const QStringList split = splitText(text, maxBytes);
        QCOMPARE(split.join(QString()), text);
        const qsizetype limit = std::max(maxBytes, 4);
        const qsizetype fewest = (brulion::qr::utf8Size(text) + limit - 1) / limit;
        QCOMPARE(split.size(), parts < 0 ? fewest : qsizetype(parts));
        const QList<qsizetype> boundaries = graphemeBoundaries(text);
        const bool clusterTooLarge = qstrcmp(QTest::currentDataTag(), "a cluster too large") == 0;
        qsizetype at = 0;
        for (const QString &part : split) {
            QVERIFY(!part.isEmpty());
            QVERIFY2(brulion::qr::utf8Size(part) <= limit, qPrintable(QString::number(brulion::qr::utf8Size(part))));
            QVERIFY(!part.front().isLowSurrogate());
            QVERIFY(!part.back().isHighSurrogate());
            if (!clusterTooLarge)
                QVERIFY2(boundaries.contains(at), qPrintable(u"a part starts inside a character at %1"_s.arg(at)));
            at += part.size();
        }
    }

    // About the same size each, not one full code and a few bytes left.
    void splitEvenly()
    {
        const QStringList two = splitText(QString(5000, u'x'));
        QCOMPARE(two.size(), 2);
        QCOMPARE(two[0].size(), 2500);
        QCOMPARE(two[1].size(), 2500);
        const QStringList three = splitText(QString(MaxBytes * 2 + 3, u'x'));
        QCOMPARE(three.size(), 3);
        for (const QString &part : three)
            QVERIFY(part.size() >= 1960 && part.size() <= 1975);
    }

    // After a line break near the even share, else after a space.
    void splitAtLineBreaks()
    {
        const QString lines = repeated(u"Pozycja listy zakupów numer sto.\n"_s, 5000); // 34 bytes a line
        const QStringList parts = splitText(lines);
        QCOMPARE(parts.size(), 2);
        QVERIFY(parts[0].endsWith(u'\n'));
        QVERIFY(qAbs(brulion::qr::utf8Size(parts[0]) - brulion::qr::utf8Size(parts[1])) <= 2 * 34);

        const QString words = repeated(u"słowo "_s, 5000).trimmed();
        const QStringList cut = splitText(words);
        QCOMPARE(cut.size(), 2);
        QVERIFY(cut[0].endsWith(u' '));

        // A line break far from the share does not make the parts uneven: a space near it wins.
        const QString mixed = u"Krótki wiersz\n"_s + repeated(u"wyraz "_s, 4000);
        const QStringList even = splitText(mixed);
        QCOMPARE(even.size(), 2);
        QVERIFY(even[0].endsWith(u' '));
        QVERIFY(qAbs(brulion::qr::utf8Size(even[0]) - brulion::qr::utf8Size(even[1])) < 600);
    }

    // A long note: its text in codes that read back and join into the note.
    void noteInCodes()
    {
        QString note;
        for (int i = 1; i <= 120; ++i)
            note += u"- [ ] Zadanie %1: zadzwonić do pani Łucji w sprawie żółtej ściany 🎨\n"_s.arg(i);
        NoteTextOptions options;
        options.title = u"Remont"_s;
        const QString text = brulion::qr::noteText(note, options);
        QVERIFY(text.startsWith(u"# Remont\n\n- [ ] Zadanie 1:"_s));
        const QStringList parts = splitText(text);
        QCOMPARE(parts.size(), int((brulion::qr::utf8Size(text) + MaxBytes - 1) / MaxBytes));
        QVERIFY(parts.size() >= 3);
        QString joined;
        for (const QString &part : parts) {
            const Code code = encode(part);
            QVERIFY(!code.isNull());
            QString why;
            const std::optional<qrtest::Decoded> decoded = readBack(code, &why);
            QVERIFY2(decoded.has_value(), qPrintable(why));
            joined += QString::fromUtf8(decoded->bytes);
        }
        QCOMPARE(joined, text);
    }
};

QTEST_GUILESS_MAIN(TestQr)
#include "tst_qr.moc"
