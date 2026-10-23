// Internal helpers of the index module: name collation, excerpts, edit distance, name ranking,
// link targets.
#include "index/resolve_p.h"
#include "index/suggest_p.h"
#include "index/textutil_p.h"

#include "text/text.h"

#include <QRandomGenerator>
#include <QTest>

#include <algorithm>

using namespace brulion::index_detail;
using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(brulion::index_detail::NameRank)

namespace {

// rankName() as it was before it visited the name's words as views (the reference of
// rankNameMatchesReference()).
qsizetype referenceSubsequenceSpan(QStringView name, QStringView q)
{
    qsizetype start = -1;
    qsizetype j = 0;
    for (qsizetype i = 0; i < name.size() && j < q.size(); ++i) {
        if (name[i] != q[j])
            continue;
        if (j == 0)
            start = i;
        if (++j == q.size())
            return i - start + 1;
    }
    return -1;
}

bool referenceOccursAtWordStart(QStringView name, QStringView q)
{
    for (qsizetype i = name.indexOf(q); i >= 0; i = name.indexOf(q, i + 1)) {
        if (i == 0 || !isTokenChar(codePointBefore(name, i)))
            return true;
    }
    return false;
}

NameRank referenceRankName(QStringView name, QStringView q)
{
    if (q.isEmpty() || name.isEmpty())
        return NameRank::None;
    if (name == q)
        return NameRank::Exact;
    if (name.startsWith(q))
        return NameRank::Prefix;
    if (name.contains(q))
        return referenceOccursAtWordStart(name, q) ? NameRank::WordPrefix : NameRank::Contains;
    const QStringList nameWords = words(name);
    const QStringList queryWords = words(q);
    if (queryWords.size() > 1) {
        const bool all = std::all_of(queryWords.begin(), queryWords.end(), [&](const QString &qw) {
            return std::any_of(nameWords.begin(), nameWords.end(), [&](const QString &w) { return w.startsWith(qw); });
        });
        if (all)
            return NameRank::WordPrefix;
    }
    if (q.size() >= 4) {
        const int max = q.size() >= 7 ? 2 : 1;
        for (const QString &w : nameWords) {
            if (boundedEditDistance(q, QStringView(w).first(std::min(w.size(), q.size())), max) <= max
                || boundedEditDistance(q, w, max) <= max)
                return NameRank::FuzzyClose;
        }
    }
    if (q.size() >= 2) {
        const qsizetype span = referenceSubsequenceSpan(name, q);
        if (span >= 0)
            return span <= q.size() * 2 ? NameRank::FuzzyClose : NameRank::Fuzzy;
    }
    return NameRank::None;
}

/// Random text from Polish words, digits, separators, other scripts and lone surrogates.
QString randomName(QRandomGenerator &r)
{
    static const QStringList parts = {u"kawa"_s,
                                      u"Kawiarnia"_s,
                                      u"dom"_s,
                                      u"Jana"_s,
                                      u"spotkanie"_s,
                                      u"zespołu"_s,
                                      u"Łódź"_s,
                                      u"żółw"_s,
                                      u"źdźbło"_s,
                                      u"Strona"_s,
                                      u"12"_s,
                                      u"007"_s,
                                      u"raport"_s,
                                      u"roczny"_s,
                                      u"Ελλάδα"_s,
                                      u"привет"_s,
                                      u"日記"_s,
                                      u"😀"_s,
                                      u"Z\u0307ywiec"_s,
                                      u"İ"_s,
                                      u"ß"_s,
                                      QString(QChar(0xD800)),
                                      u"é"_s,
                                      u"x"_s};
    static const QStringList separators = {u" "_s, u"/"_s, u"-"_s, u"_"_s, u"  "_s, u"."_s, QString()};
    QString s;
    const int n = int(r.bounded(1, 5));
    for (int i = 0; i < n; ++i) {
        if (i > 0)
            s += separators[r.bounded(int(separators.size()))];
        s += parts[r.bounded(int(parts.size()))];
    }
    return s;
}

/// A query derived from `name` (prefix, word, typo, swapped words) or another random name.
QString randomQuery(QRandomGenerator &r, const QString &name)
{
    QString q;
    switch (r.bounded(7)) {
    case 0: q = name.left(int(r.bounded(1, int(name.size()) + 1))); break;
    case 1: {
        const QStringList w = words(name);
        q = w.isEmpty() ? name : w[r.bounded(int(w.size()))];
        break;
    }
    case 2: {
        q = name;
        if (q.size() > 2) {
            const int i = int(r.bounded(int(q.size()) - 1));
            switch (r.bounded(3)) {
            case 0: q.remove(i, 1); break;
            case 1: std::swap(q[i], q[i + 1]); break;
            default: q.insert(i, u'q'); break;
            }
        }
        break;
    }
    case 3: {
        QStringList w = words(name);
        std::reverse(w.begin(), w.end());
        for (QString &x : w)
            x = x.left(int(r.bounded(1, int(x.size()) + 1)));
        q = w.join(u' ');
        break;
    }
    case 4: { // characters spread out
        const int step = int(r.bounded(2, 5));
        for (qsizetype i = 0; i < name.size(); i += step)
            q += name[i];
        break;
    }
    default: q = randomName(r); break;
    }
    return brulion::text::foldKey(q);
}

} // namespace

class TestIndexInternals : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void compareNames_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("sign");
        QTest::newRow("numbers by value") << u"Strona 2"_s << u"Strona 10"_s << -1;
        QTest::newRow("leading zeros") << u"Strona 007"_s << u"Strona 8"_s << -1;
        QTest::newRow("l before ł") << u"Lublin"_s << u"Łódź"_s << -1;
        QTest::newRow("ł before m") << u"Łódź"_s << u"Mława"_s << -1;
        QTest::newRow("z < ź < ż") << u"Zakopane"_s << u"Źródło"_s << -1;
        QTest::newRow("ź before ż") << u"Źródło"_s << u"Żywiec"_s << -1;
        QTest::newRow("a before ą regardless of the rest") << u"az"_s << u"ąb"_s << -1;
        QTest::newRow("c before ć") << u"Cyrk"_s << u"Ćma"_s << -1;
        QTest::newRow("ó after o") << u"Oza"_s << u"Ósemka"_s << -1;
        QTest::newRow("case-insensitive") << u"apple"_s << u"Banana"_s << -1;
        QTest::newRow("prefix first") << u"Dom"_s << u"Dom Jana"_s << -1;
        QTest::newRow("same letters: code units decide") << u"Dom"_s << u"dom"_s << -1;
        QTest::newRow("other accents like base") << u"éclair"_s << u"ecz"_s << -1;
        QTest::newRow("nfd equals nfc") << u"Z\u0307ywiec"_s << u"Żywiec"_s << 0;
        QTest::newRow("equal") << u"Kawa"_s << u"Kawa"_s << 0;
        QTest::newRow("empty first") << QString() << u"a"_s << -1;
    }
    void compareNames()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, sign);
        const int ab = brulion::index_detail::compareNames(a, b);
        const int ba = brulion::index_detail::compareNames(b, a);
        if (sign == 0) {
            // NFD vs NFC: equal letters, tie broken by code units (so not 0 unless identical).
            QCOMPARE(ab == 0, a == b);
            QCOMPARE(ab, -ba);
        } else {
            QCOMPARE(ab < 0, sign < 0);
            QCOMPARE(ba < 0, sign > 0);
        }
    }

    void excerptAround()
    {
        QCOMPARE(brulion::index_detail::excerptAround(u"  krótka\tlinia  "_s, 2, 8), u"krótka linia"_s);
        const QString words = QString(u"abc "_s).repeated(60); // 240 characters
        const QString e = brulion::index_detail::excerptAround(words, 120, 123, 40);
        QVERIFY(e.startsWith(u'…') && e.endsWith(u'…'));
        QVERIFY(e.size() <= 42);
        QVERIFY(!e.contains(u"  "_s));
        // Match at the very start / end.
        QVERIFY(!brulion::index_detail::excerptAround(words, 0, 3, 40).startsWith(u'…'));
        QVERIFY(!brulion::index_detail::excerptAround(words, 236, 239, 40).endsWith(u'…'));
        // Never cuts a surrogate pair.
        const QString emoji = QString(u"😀"_s).repeated(100);
        const QString cut = brulion::index_detail::excerptAround(emoji, 101, 103, 31);
        for (qsizetype i = 0; i < cut.size(); ++i) {
            if (cut[i].isHighSurrogate())
                QVERIFY(i + 1 < cut.size() && cut[i + 1].isLowSurrogate());
            if (cut[i].isLowSurrogate())
                QVERIFY(i > 0 && cut[i - 1].isHighSurrogate());
        }
    }

    void lineExcerpt_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("start");
        QTest::addColumn<QString>("expected");
        QTest::newRow("list item") << u"  - tekst żółwia"_s << 4 << u"tekst żółwia"_s;
        QTest::newRow("task") << u"- [x] zrobione ^abc-1"_s << 6 << u"zrobione"_s;
        QTest::newRow("ordered") << u"12. punkt"_s << 4 << u"punkt"_s;
        QTest::newRow("heading") << u"## Nagłówek"_s << 3 << u"Nagłówek"_s;
        QTest::newRow("quote") << u"> > cytat"_s << 4 << u"cytat"_s;
        QTest::newRow("match inside the marker keeps it") << u"- [ ] x"_s << 0 << u"- [ ] x"_s;
        QTest::newRow("id kept when matched") << u"tekst ^abc"_s << 7 << u"tekst ^abc"_s;
        QTest::newRow("caret without blank") << u"a^b"_s << 0 << u"a^b"_s;
        QTest::newRow("plain") << u"zwykły tekst"_s << 0 << u"zwykły tekst"_s;
    }
    void lineExcerpt()
    {
        QFETCH(QString, line);
        QFETCH(int, start);
        QFETCH(QString, expected);
        QCOMPARE(brulion::index_detail::lineExcerpt(line, start, start + 1), expected);
    }

    void excerptStart()
    {
        QCOMPARE(brulion::index_detail::excerptStart(u"krótki\ntekst"_s), u"krótki tekst"_s);
        const QString longText = QString(u"słowo "_s).repeated(40);
        const QString e = brulion::index_detail::excerptStart(longText, 50);
        QVERIFY(e.endsWith(u'…'));
        QVERIFY(e.size() <= 51);
        QVERIFY(!e.contains(u"słow…"_s)); // cut at a word boundary
        const QString noSpaces = QString(u'x').repeated(200);
        QCOMPARE(brulion::index_detail::excerptStart(noSpaces, 50).size(), 51);
    }

    void editDistance_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("max");
        QTest::addColumn<int>("expected");
        QTest::newRow("equal") << u"kawa"_s << u"kawa"_s << 1 << 0;
        QTest::newRow("substitution") << u"kawa"_s << u"kara"_s << 1 << 1;
        QTest::newRow("insertion") << u"kawa"_s << u"kawka"_s << 1 << 1;
        QTest::newRow("deletion") << u"kawa"_s << u"kaw"_s << 1 << 1;
        QTest::newRow("transposition") << u"kawa"_s << u"kwaa"_s << 1 << 1;
        QTest::newRow("two edits, max 1") << u"kawa"_s << u"kora"_s << 1 << 2;
        QTest::newRow("two edits, max 2") << u"kawa"_s << u"kora"_s << 2 << 2;
        QTest::newRow("length gap") << u"kawa"_s << u"kawalerka"_s << 2 << 3;
        QTest::newRow("empty") << QString() << u"ab"_s << 2 << 2;
        QTest::newRow("polish letters") << u"żółw"_s << u"żołw"_s << 1 << 1;
        QTest::newRow("long") << u"spotkanie"_s << u"spatkonie"_s << 2 << 2;
    }
    void editDistance()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, max);
        QFETCH(int, expected);
        QCOMPARE(boundedEditDistance(a, b, max), expected);
        QCOMPARE(boundedEditDistance(b, a, max), expected);
    }

    void rankName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("query");
        QTest::addColumn<NameRank>("rank");
        QTest::newRow("exact") << u"kawa"_s << u"kawa"_s << NameRank::Exact;
        QTest::newRow("prefix") << u"kawiarnia"_s << u"kaw"_s << NameRank::Prefix;
        QTest::newRow("word prefix") << u"dobra kawa"_s << u"kaw"_s << NameRank::WordPrefix;
        QTest::newRow("word prefix after slash") << u"projekty/dom"_s << u"dom"_s << NameRank::WordPrefix;
        QTest::newRow("contains") << u"sekawa"_s << u"kaw"_s << NameRank::Contains;
        QTest::newRow("all words start words") << u"jan kowalski"_s << u"kow jan"_s << NameRank::WordPrefix;
        QTest::newRow("prefix of the first word") << u"spotkanie zespolu"_s << u"spot"_s << NameRank::Prefix;
        QTest::newRow("one typo") << u"spotkanie"_s << u"sptkanie"_s << NameRank::FuzzyClose;
        QTest::newRow("typo in word beginning") << u"raport roczny"_s << u"rapo5"_s << NameRank::FuzzyClose;
        QTest::newRow("characters close together") << u"karwasz"_s << u"kw"_s << NameRank::FuzzyClose;
        QTest::newRow("characters spread out") << u"kalendarz wydarzen"_s << u"kw"_s << NameRank::Fuzzy;
        QTest::newRow("no match") << u"herbata"_s << u"kawa"_s << NameRank::None;
        QTest::newRow("empty query") << u"herbata"_s << QString() << NameRank::None;
        QTest::newRow("empty name") << QString() << u"a"_s << NameRank::None;
    }
    void rankName()
    {
        QFETCH(QString, name);
        QFETCH(QString, query);
        QFETCH(NameRank, rank);
        QCOMPARE(brulion::index_detail::rankName(name, query), rank);
    }

    void rankNameMatchesReference()
    {
        // Property: ranking with the query's words computed once and the name's words visited
        // as views gives the old ranking.
        QRandomGenerator r(20261017);
        QList<int> seen(int(NameRank::Exact) + 1, 0);
        for (int n = 0; n < 50000; ++n) {
            const QString name = brulion::text::foldKey(randomName(r));
            const QString q = randomQuery(r, name);
            const NameRank expected = referenceRankName(name, q);
            QCOMPARE(brulion::index_detail::rankName(name, q), expected);
            QCOMPARE(brulion::index_detail::rankName(name, q, words(q)), expected);
            ++seen[int(expected)];
        }
        for (qsizetype rank = 0; rank < seen.size(); ++rank)
            QVERIFY2(seen[rank] > 100, qPrintable(u"rank %1 seen %2 times"_s.arg(rank).arg(seen[rank])));
    }

    void forEachWordVisitsWords()
    {
        QRandomGenerator r(7);
        for (int n = 0; n < 20000; ++n) {
            const QString s = randomName(r);
            QStringList seen;
            QVERIFY(forEachWord(s, [&](QStringView w) {
                seen.append(w.toString());
                return true;
            }));
            QCOMPARE(seen, words(s));
            if (seen.isEmpty())
                continue;
            // Stopping at word k visits k + 1 words and reports the stop.
            const qsizetype k = r.bounded(int(seen.size()));
            qsizetype visits = 0;
            QVERIFY(!forEachWord(s, [&](QStringView) { return visits++ < k; }));
            QCOMPARE(visits, k + 1);
        }
    }

    void nameSortKeysCompareLikeNames()
    {
        // Sorts compare precomputed keys: the same order as compareNames().
        QRandomGenerator r(11);
        for (int n = 0; n < 20000; ++n) {
            QString a = randomName(r);
            QString b = r.bounded(4) == 0 ? a : randomName(r);
            if (r.bounded(3) == 0)
                b = b.toUpper();
            QCOMPARE(compareNameKeys(nameSortKey(a), a, nameSortKey(b), b), brulion::index_detail::compareNames(a, b));
        }
    }

    // lower(): QString::toLower() with an ASCII fast path — the same result for any text.
    void lowerMatchesToLower_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("lower already") << u"projekty/dom.md"_s;
        QTest::newRow("upper ASCII") << u"Projekty/DOM Jana.MD"_s;
        QTest::newRow("brackets and digits around A and Z") << u"@[`{AZaz09"_s;
        QTest::newRow("Polish") << u"Zażółć GĘŚLĄ Jaźń"_s;
        QTest::newRow("ASCII, then Polish") << u"ABC/Łąka"_s;
        QTest::newRow("Greek and German") << u"ΣΊΣΥΦΟΣ Straße"_s;
    }
    void lowerMatchesToLower()
    {
        QFETCH(QString, text);
        QCOMPARE(brulion::index_detail::lower(text), text.toLower());
        QRandomGenerator r(23);
        for (int n = 0; n < 2000; ++n) {
            QString s;
            for (int i = r.bounded(12); i > 0; --i)
                s += QChar(char16_t(r.bounded(3) == 0 ? 0xA0 + r.bounded(0x200) : 0x20 + r.bounded(0x5F)));
            QCOMPARE(brulion::index_detail::lower(s), s.toLower());
        }
    }

    void cleanTarget_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("clean");
        QTest::addColumn<QString>("key");
        QTest::newRow("plain") << u"Dom"_s << u"Dom"_s << u"dom"_s;
        QTest::newRow("path + md") << u"Projekty/Dom.md"_s << u"Projekty/Dom.md"_s << u"dom"_s;
        QTest::newRow("brackets, alias, heading") << u"[[Projekty/Dom#Plan|dom]]"_s << u"Projekty/Dom"_s << u"dom"_s;
        QTest::newRow("escaped pipe") << u"Dom\\|alias"_s << u"Dom"_s << u"dom"_s;
        QTest::newRow("block") << u"Dom#^abc"_s << u"Dom"_s << u"dom"_s;
        QTest::newRow("backslashes") << u"a\\b\\Żółw"_s << u"a/b/Żółw"_s << u"żółw"_s;
        QTest::newRow("attachment") << u"assets/Foto.PNG"_s << u"assets/Foto.PNG"_s << u"foto.png"_s;
        QTest::newRow("relative") << u"../Zakupy.md"_s << u"../Zakupy.md"_s << u"zakupy"_s;
        QTest::newRow("only fragment") << u"#^abc"_s << QString() << QString();
        QTest::newRow("nfd") << u"Z\u0307o\u0301\u0142w"_s << u"Z\u0307o\u0301\u0142w"_s << u"żółw"_s;
    }
    void cleanTarget()
    {
        QFETCH(QString, target);
        QFETCH(QString, clean);
        QFETCH(QString, key);
        QCOMPARE(brulion::index_detail::cleanTarget(target), clean);
        QCOMPARE(linkKey(target), key);
    }

    void parsedTargetsKeepHashAndPipe_data()
    {
        // Link::target from the parser has no display text / fragment any more; a decoded
        // Markdown target may contain '#' or '|' as part of the file name ([C#](C%23.md)).
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("clean");
        QTest::addColumn<QString>("key");
        QTest::newRow("hash") << u"C#.md"_s << u"C#.md"_s << u"c#"_s;
        QTest::newRow("pipe") << u"Notatki/A|B.md"_s << u"Notatki/A|B.md"_s << u"a|b"_s;
        QTest::newRow("spaces trimmed") << u"  Dom  "_s << u"Dom"_s << u"dom"_s;
        QTest::newRow("backslashes") << u"a\\Dom"_s << u"a/Dom"_s << u"dom"_s;
        QTest::newRow("brackets stay") << u"[[x]]"_s << u"[[x]]"_s << u"[[x]]"_s;
    }
    void parsedTargetsKeepHashAndPipe()
    {
        QFETCH(QString, target);
        QFETCH(QString, clean);
        QFETCH(QString, key);
        QCOMPARE(brulion::index_detail::cleanTarget(target, TargetForm::Parsed), clean);
        QCOMPARE(linkKey(target, TargetForm::Parsed), key);
        // Aliases can be reached by raw and parsed targets alike.
        QVERIFY(aliasLinkKeys(target).contains(key));
        QVERIFY(aliasLinkKeys(target).contains(linkKey(target)));
    }

    void attachmentExtensions_data()
    {
        QTest::addColumn<QString>("ext");
        QTest::addColumn<bool>("attachment");
        QTest::newRow("png") << u".png"_s << true;
        QTest::newRow("upper case") << u".PDF"_s << true;
        QTest::newRow("letters and digits") << u".mp3"_s << true;
        QTest::newRow("long but real") << u".excalidraw"_s << true;
        QTest::newRow("too long") << u".abcdefghijk"_s << false;
        QTest::newRow("markdown") << u".md"_s << false;
        QTest::newRow("markdown long") << u".markdown"_s << false;
        QTest::newRow("none") << QString() << false;
        QTest::newRow("only the dot") << u"."_s << false;
        QTest::newRow("Dr. Smith") << u". smith"_s << false;
        QTest::newRow("date: Spotkanie 14.10") << u".10"_s << false;
        QTest::newRow("version: v1.2") << u".2"_s << false;
        QTest::newRow("polish letters") << u".łódź"_s << false;
        QTest::newRow("punctuation") << u".a-b"_s << false;
    }
    void attachmentExtensions()
    {
        QFETCH(QString, ext);
        QFETCH(bool, attachment);
        QCOMPARE(isAttachmentExtension(ext), attachment);
    }

    void globMatch_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<bool>("prefixOnly");
        QTest::addColumn<bool>("match");
        QTest::newRow("literal") << u"zakupy"_s << u"zakupy"_s << false << true;
        QTest::newRow("literal, longer text") << u"zakupy2"_s << u"zakupy"_s << false << false;
        QTest::newRow("literal prefix") << u"zakupy2"_s << u"zakupy"_s << true << true;
        QTest::newRow("star inside") << u"journals/2026-10-13"_s << u"journals/*-13"_s << false << true;
        QTest::newRow("star both sides") << u"zakupy"_s << u"*akup*"_s << false << true;
        QTest::newRow("star only") << u"cokolwiek"_s << u"*"_s << false << true;
        QTest::newRow("star matches empty") << u"ab"_s << u"a*b"_s << false << true;
        QTest::newRow("needs backtracking") << u"aXbXc"_s << u"a*b*c"_s << false << true;
        QTest::newRow("last literal missing") << u"abc"_s << u"a*d"_s << false << false;
        QTest::newRow("prefix with star") << u"projekty/dom/plan"_s << u"proj*/dom"_s << true << true;
        QTest::newRow("prefix mismatch") << u"projekty/dom"_s << u"*/kot"_s << true << false;
        QTest::newRow("empty pattern") << u"x"_s << QString() << false << false;
        QTest::newRow("empty pattern, prefix") << u"x"_s << QString() << true << true;
        QTest::newRow("empty text") << QString() << u"*"_s << false << true;
        QTest::newRow("regex syntax is literal") << u"a.b"_s << u"a.b"_s << false << true;
        QTest::newRow("dot is no wildcard") << u"axb"_s << u"a.b"_s << false << false;
        QTest::newRow("polish") << u"zażółć"_s << u"za*ć"_s << false << true;
        QTest::newRow("emoji") << u"🚀 start"_s << u"🚀*"_s << false << true;
        // Exponential for a backtracking regex (\A.*a.*a…\z) — linear here.
        const QString as = QString(60, u'a');
        const QString stars = QString(u"*a"_s).repeated(16);
        QTest::newRow("many stars, match") << as + u"xy"_s << stars + u"*x*y"_s << false << true;
        QTest::newRow("many stars, early match only")
                << QString(20, u'a') + u"x"_s + as + u"y"_s << stars + u"*x*y"_s << false << true;
        QTest::newRow("many stars, no match") << u"x"_s + as << stars + u"*x"_s << false << false;
    }
    void globMatch()
    {
        QFETCH(QString, text);
        QFETCH(QString, pattern);
        QFETCH(bool, prefixOnly);
        QFETCH(bool, match);
        QCOMPARE(brulion::index_detail::globMatch(text, pattern, prefixOnly), match);
    }

    void targetKinds()
    {
        QVERIFY(isExternalTarget(u"https://example.com"));
        QVERIFY(isExternalTarget(u"mailto:jan@example.com"));
        QVERIFY(isExternalTarget(u"obsidian://open?vault=x"));
        QVERIFY(!isExternalTarget(u"C:/notes/a.md"));
        QVERIFY(!isExternalTarget(u"C:\\notes\\a.md"));
        QVERIFY(!isExternalTarget(u"Projekty/Dom.md"));
        QVERIFY(!isExternalTarget(u"1abc:x"));
        QVERIFY(!isExternalTarget(u"Żółw:x")); // schemes are ASCII
        QVERIFY(!isExternalTarget(u""));
        QVERIFY(isPageTarget(u"Dom"_s));
        QVERIFY(isPageTarget(u"Dom.md"_s));
        QVERIFY(isPageTarget(u"Dr. Smith"_s));
        QVERIFY(isPageTarget(u"Dom#^abc"_s));
        QVERIFY(!isPageTarget(u"foto.png"_s));
        QVERIFY(!isPageTarget(u"plik.PDF"_s));
        QVERIFY(isPageTarget(u"Spotkanie 14.10.2026"_s)); // a date, not an extension
        QVERIFY(isPageTarget(u"v1.2"_s));
        QVERIFY(isPageTarget(u"C#.md"_s, TargetForm::Parsed));
        QVERIFY(!isPageTarget(u"a#b.png"_s, TargetForm::Parsed));
        QCOMPARE(pickBest({u"b/Note.md"_s, u"a/Note.md"_s, u"long/Note.md"_s}), u"a/Note.md"_s);
        QCOMPARE(pickBest({}), QString());
    }
};

QTEST_GUILESS_MAIN(TestIndexInternals)
#include "tst_index_internals.moc"
