// Internal helpers of the index module: name collation, excerpts, edit distance, name ranking,
// link targets, the compact path sets of the lookup maps; the derived lists patched instead of
// built again, and the change log (changesSince()).
#include "index/resolve_p.h"
#include "index/smallsets_p.h"
#include "index/suggest_p.h"
#include "index/textutil_p.h"
#include "index/vaultindex_p.h"

#include "text/text.h"
#include "util/paths.h"

#include <QRandomGenerator>
#include <QTest>

#include <algorithm>

using namespace brulion::index_detail;
using brulion::IndexChanges;
using brulion::VaultConfig;
using brulion::VaultIndex;
using brulion::VaultPath;
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

    // NameTable::overlay() (the names after a move): every resolution is that of a copy of the
    // whole table changed the same way — random notebooks with duplicate names, aliases and
    // attachments, random moves and removals.
    void nameTableOverlayResolvesLikeACopy()
    {
        QRandomGenerator r(20261023);
        const QStringList names{u"Dom"_s, u"dom"_s, u"Łąka"_s, u"Plan"_s, u"Żółw"_s};
        const QStringList folders{QString(), u"A"_s, u"Projekty"_s, u"Projekty/Dom"_s, u"B/C"_s};
        const auto pathIn = [](const QString &folder, const QString &file) {
            return folder.isEmpty() ? file : folder + u'/' + file;
        };
        for (int round = 0; round < 200; ++round) {
            struct File {
                QString path;
                QStringList aliases;
                bool note = true;
            };
            QList<File> files;
            for (int i = 0; i < 10; ++i) {
                const QString folder = folders.at(r.bounded(int(folders.size())));
                const QString name = names.at(r.bounded(int(names.size())));
                File f;
                f.note = r.bounded(4) != 0;
                f.path = pathIn(folder, name + (f.note ? u".md"_s : u".png"_s));
                if (f.note && r.bounded(3) == 0)
                    f.aliases = {names.at(r.bounded(int(names.size()))) + u" alias"_s};
                if (std::none_of(files.cbegin(), files.cend(), [&](const File &o) { return o.path == f.path; }))
                    files.append(f);
            }
            NameTable base;
            for (const File &f : std::as_const(files))
                f.note ? base.addNote(f.path, f.aliases) : base.addAttachment(f.path);
            NameTable copy = base;
            NameTable overlay;
            overlay.overlay(&base);
            // Moves (remove, then add elsewhere) and removals, the same on both.
            QStringList present;
            for (int op = 0; op < 4 && !files.isEmpty(); ++op) {
                const File f = files.takeAt(r.bounded(int(files.size())));
                f.note ? copy.removeNote(f.path, f.aliases) : copy.removeAttachment(f.path);
                f.note ? overlay.removeNote(f.path, f.aliases) : overlay.removeAttachment(f.path);
                if (r.bounded(3) == 0)
                    continue;
                File moved = f;
                moved.path = pathIn(folders.at(r.bounded(int(folders.size()))), brulion::paths::basename(f.path));
                moved.note ? copy.addNote(moved.path, moved.aliases) : copy.addAttachment(moved.path);
                moved.note ? overlay.addNote(moved.path, moved.aliases) : overlay.addAttachment(moved.path);
                files.append(moved);
            }
            QStringList targets;
            for (const QString &n : names) {
                targets << n << n + u".md"_s << n + u".png"_s << n + u" alias"_s;
                for (const QString &folder : folders)
                    targets << pathIn(folder, n) << pathIn(folder, n + u".png"_s);
            }
            for (const QString &t : std::as_const(targets)) {
                QCOMPARE(overlay.resolveNote(t, TargetForm::Parsed), copy.resolveNote(t, TargetForm::Parsed));
                for (const QString &folder : folders) {
                    const QString from = pathIn(folder, u"Notatka.md"_s);
                    QCOMPARE(overlay.resolve(t, from, TargetForm::Parsed), copy.resolve(t, from, TargetForm::Parsed));
                }
            }
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

    // ---- PathSet / PathCounts (smallsets_p.h) ---------------------------------------------------

    // Random insert/remove sequences against QSet: the same paths in every state, across the
    // switch from the sorted list to the set (SmallLimit) and back (SmallLimit / 2).
    void pathSetMatchesQSet_data()
    {
        QTest::addColumn<int>("universe");
        QTest::addColumn<int>("steps");
        QTest::addColumn<quint32>("seed");
        QTest::newRow("empty") << 0 << 0 << 1u;
        QTest::newRow("one path") << 1 << 50 << 2u;
        QTest::newRow("up to the limit") << int(PathSet::SmallLimit) << 400 << 3u;
        QTest::newRow("over the limit") << int(PathSet::SmallLimit) + 1 << 600 << 4u;
        QTest::newRow("many paths") << 300 << 5000 << 5u;
    }
    void pathSetMatchesQSet()
    {
        QFETCH(int, universe);
        QFETCH(int, steps);
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        auto pathOf = [](int i) {
            // NFC and NFD spellings of the same name are distinct keys, as in a QSet.
            return i % 3 == 0 ? u"Projekty/Łódź %1.md"_s.arg(i)
                              : (i % 3 == 1 ? u"Żółw/%1.md"_s.arg(i) : u"Z\u0307o\u0301\u0142w/%1.md"_s.arg(i));
        };
        PathSet set;
        QSet<VaultPath> reference;
        QVERIFY(set.isEmpty());
        // Fill everything first (the switch to the big set), then random changes.
        auto insert = [&](const VaultPath &p) {
            const bool fresh = !reference.contains(p);
            reference.insert(p);
            return set.insert(p) == fresh;
        };
        for (int i = 0; i < universe; ++i)
            QVERIFY(insert(pathOf(i)));
        for (int step = 0; step < steps; ++step) {
            const VaultPath p = pathOf(int(rng.bounded(universe)));
            if (rng.bounded(2) == 0)
                QVERIFY(insert(p));
            else
                QCOMPARE(set.remove(p), reference.remove(p));
            QCOMPARE(set.size(), reference.size());
            QCOMPARE(set.contains(p), reference.contains(p));
        }
        QList<VaultPath> expected(reference.cbegin(), reference.cend());
        std::sort(expected.begin(), expected.end());
        QCOMPARE(set.sorted(), expected);
        QList<VaultPath> iterated;
        for (const VaultPath &p : set)
            iterated.append(p);
        std::sort(iterated.begin(), iterated.end());
        QCOMPARE(iterated, expected);
        QList<VaultPath> visited;
        set.forEach([&](const VaultPath &p) { visited.append(p); });
        std::sort(visited.begin(), visited.end());
        QCOMPARE(visited, expected);
        QCOMPARE(set.isEmpty(), reference.isEmpty());
        // A copy is independent.
        PathSet copy = set;
        copy.insert(u"Nowa ścieżka.md"_s);
        QVERIFY(!set.contains(u"Nowa ścieżka.md"_s));
        QCOMPARE(copy.size(), set.size() + 1);
    }

    void pathSetBorders()
    {
        PathSet set;
        for (int i = 0; i < PathSet::SmallLimit; ++i)
            QVERIFY(set.insert(u"n%1.md"_s.arg(i, 3, 10, QLatin1Char('0'))));
        QVERIFY(!set.insert(u"n000.md"_s));
        QVERIFY(set.insert(u"n999.md"_s)); // 33: the big set
        QCOMPARE(set.size(), PathSet::SmallLimit + 1);
        for (int i = 0; i < PathSet::SmallLimit / 2 + 1; ++i)
            QVERIFY(set.remove(u"n%1.md"_s.arg(i, 3, 10, QLatin1Char('0'))));
        QCOMPARE(set.size(), PathSet::SmallLimit / 2); // back to the sorted list
        QVERIFY(!set.remove(u"n000.md"_s));
        QCOMPARE(set.sorted().constLast(), u"n999.md"_s);
        QCOMPARE(set.min([](const VaultPath &a, const VaultPath &b) { return a > b; }), u"n999.md"_s);
        QCOMPARE(PathSet().min([](const VaultPath &a, const VaultPath &b) { return a < b; }), QString());
    }

    void pathCountsMatchesQHash_data()
    {
        QTest::addColumn<int>("universe");
        QTest::addColumn<int>("steps");
        QTest::addColumn<quint32>("seed");
        QTest::newRow("one path") << 1 << 60 << 11u;
        QTest::newRow("around the limit") << int(PathCounts::SmallLimit) + 3 << 2000 << 12u;
        QTest::newRow("many paths") << 200 << 6000 << 13u;
    }
    void pathCountsMatchesQHash()
    {
        QFETCH(int, universe);
        QFETCH(int, steps);
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        PathCounts counts;
        QHash<VaultPath, int> reference;
        for (int step = 0; step < steps; ++step) {
            const VaultPath p = u"Dziennik/2026-10-%1 środa.md"_s.arg(rng.bounded(universe));
            switch (rng.bounded(4)) {
            case 0:
            case 1:
                counts.add(p);
                ++reference[p];
                break;
            case 2: {
                const bool dropped = counts.subtract(p);
                const auto it = reference.find(p);
                const bool expected = it != reference.end() && --*it == 0;
                if (expected)
                    reference.erase(it);
                QCOMPARE(dropped, expected);
                break;
            }
            default: {
                const int n = int(rng.bounded(4)); // 0 removes
                counts.set(p, n);
                if (n > 0)
                    reference.insert(p, n);
                else
                    reference.remove(p);
            }
            }
            QCOMPARE(counts.size(), reference.size());
            QCOMPARE(counts.value(p), reference.value(p));
        }
        QList<VaultPath> keys = reference.keys();
        std::sort(keys.begin(), keys.end());
        QCOMPARE(counts.keys(), keys);
        QHash<VaultPath, int> iterated;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            iterated.insert(it.key(), it.value());
        QCOMPARE(iterated, reference);
        QVERIFY(counts.remove(keys.isEmpty() ? u"x"_s : keys.first()) == !keys.isEmpty());
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

    // An edit that keeps the note's tags patches every list read before instead of building it
    // again; the lists equal a fresh index's.
    void listsArePatchedNotRebuilt()
    {
        const QHash<QString, QString> start = {
                {u"journals/2026-10-14.md"_s, u"- [ ] zadanie 📅 2026-10-15 #praca\n- przypomnienie ⏰ 2026-10-16 10:00\n"_s},
                {u"Żółw.md"_s, u"- [x] karmienie #zwierzęta\n```query\ntag:praca\n```\n"_s},
                {u"Dom.md"_s, u"---\nstatus: aktywny\n---\n- [ ] malowanie ⏰ 2026-10-20 18:00\n"_s},
                {u"zebra.md"_s, u"- tekst o Żółw\n"_s},
        };
        VaultIndex index;
        for (auto it = start.cbegin(); it != start.cend(); ++it)
            index.update(it.key(), it.value());
        const auto readAll = [](const VaultIndex &i) {
            QStringList out = i.paths();
            for (const brulion::TaskRef &t : i.tasks())
                out << t.path + u':' + QString::number(t.task.line) + u':' + t.task.text;
            for (const brulion::ReminderRef &r : i.reminders())
                out << r.path + u':' + r.reminder.at.toString() + u':' + r.reminder.text;
            for (const brulion::JournalRef &j : i.journals())
                out << j.date.toString(Qt::ISODate) + j.path;
            for (const brulion::TagCount &t : i.tags())
                out << t.tag + u'=' + QString::number(t.count) + t.display;
            out << i.notesWithQueries();
            for (const brulion::PropertyKeyInfo &k : i.propertyKeys())
                out << k.key + QString::number(k.count);
            out << QString::number(i.unlinkedReferences(u"Żółw.md"_s).size()); // byRelevance
            return out;
        };
        readAll(index);
        const QStringList lists = {u"sorted"_s, u"byRelevance"_s, u"paths"_s,      u"tasks"_s,       u"reminders"_s,
                                   u"journals"_s, u"tags"_s,      u"queryNotes"_s, u"propertyKeys"_s};
        const auto counts = [&] {
            QList<int> out;
            for (const QString &l : lists)
                out << IndexAccess::buildCount(index, l.toStdString());
            return out;
        };
        const QList<int> built = counts();
        for (const int c : built)
            QCOMPARE(c, 1);
        QCOMPARE(IndexAccess::buildCount(index, "no such list"), -1);

        // Text edits keeping the tags and properties; a new task and a reminder.
        QHash<QString, QString> files = start;
        files.insert(u"journals/2026-10-14.md"_s,
                     u"- [ ] zadanie zmienione 📅 2026-10-15 #praca\n- [ ] nowe ⏰ 2026-10-14 08:00\n"
                     u"- przypomnienie ⏰ 2026-10-16 10:00\n"_s);
        files.insert(u"Dom.md"_s, u"---\nstatus: aktywny\n---\n- [/] malowanie salonu ⏰ 2026-10-20 18:00\n"_s);
        for (const QString &p : {u"journals/2026-10-14.md"_s, u"Dom.md"_s})
            index.update(p, files.value(p));
        const QStringList patched = readAll(index);
        QCOMPARE(counts(), built);
        VaultIndex fresh;
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            fresh.update(it.key(), it.value());
        QCOMPARE(patched, readAll(fresh));

        // A new note and a removed one: patched too (tags change: built again).
        files.insert(u"Ąbc.md"_s, u"- [ ] w nowej notatce\n"_s);
        index.update(u"Ąbc.md"_s, files.value(u"Ąbc.md"_s));
        files.remove(u"zebra.md"_s);
        index.remove(u"zebra.md"_s);
        files.insert(u"Żółw.md"_s, u"- [x] karmienie #zwierzeta\n"_s); // tag spelling and the query gone
        index.update(u"Żółw.md"_s, files.value(u"Żółw.md"_s));
        VaultIndex fresh2;
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            fresh2.update(it.key(), it.value());
        QCOMPARE(readAll(index), readAll(fresh2));
        const QList<int> after = counts();
        for (qsizetype i = 0; i < lists.size(); ++i)
            QCOMPARE(after[i], built[i] + (lists[i] == u"tags" ? 1 : 0));

        // Many changes nobody reads (a bulk load): dropped, built once at the next read.
        for (int i = 0; i < MaxUnreadPatches + 5; ++i) {
            files.insert(u"bulk/%1.md"_s.arg(i), u"- [ ] zadanie %1\n"_s.arg(i));
            index.update(u"bulk/%1.md"_s.arg(i), files.value(u"bulk/%1.md"_s.arg(i)));
        }
        VaultIndex fresh3;
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            fresh3.update(it.key(), it.value());
        QCOMPARE(readAll(index), readAll(fresh3));
        QCOMPARE(IndexAccess::buildCount(index, "tasks"), built[lists.indexOf(u"tasks"_s)] + 1);
    }

    void changesSince()
    {
        VaultIndex index;
        QCOMPARE(index.changesSince(0).complete, true);
        QVERIFY(index.changesSince(0).notes.isEmpty());
        QVERIFY(!index.changesSince(1).complete); // a future version
        QVERIFY(!index.changesSince(-1).complete);
        VaultIndex other;
        QVERIFY(index.serial() != other.serial());

        // A new note: names change.
        int v = index.version();
        index.update(u"Żółw.md"_s, u"- tekst ^blok\n- [[Brak]]\n"_s);
        IndexChanges c = index.changesSince(v);
        QVERIFY(c.complete);
        QCOMPARE(c.notes, QList<VaultPath>({u"Żółw.md"_s}));
        QVERIFY(c.names);
        QCOMPARE(index.changesSince(index.version()).complete, true);
        QVERIFY(index.changesSince(index.version()).notes.isEmpty());

        // An edit keeping the names: just the note; with dependents through ((id)).
        index.update(u"Ala.md"_s, u"- pokazuje ((blok))\n"_s);
        index.update(u"Ola.md"_s, u"- nic\n"_s);
        v = index.version();
        index.update(u"Żółw.md"_s, u"- tekst zmieniony ^blok\n- [[Brak]]\n"_s);
        c = index.changesSince(v);
        QCOMPARE(c.notes, QList<VaultPath>({u"Ala.md"_s, u"Żółw.md"_s}));
        QVERIFY(!c.names);
        index.update(u"Żółw.md"_s, u"- tekst zmieniony ^blok\n- [[Brak]]\n"_s); // unchanged: no-op
        QCOMPARE(index.changesSince(v).notes, c.notes);

        // A relink: [[Brak]] resolves once Brak.md appears.
        v = index.version();
        index.update(u"Brak.md"_s, u"- jestem\n"_s);
        c = index.changesSince(v);
        QCOMPARE(c.notes, QList<VaultPath>({u"Brak.md"_s, u"Żółw.md"_s}));
        QVERIFY(c.names);

        // Aliases change names; a duplicate block id with a smaller path takes the block over.
        v = index.version();
        index.update(u"Ola.md"_s, u"---\naliases: [Ała]\n---\n- nic\n"_s);
        QVERIFY(index.changesSince(v).names);
        v = index.version();
        index.update(u"A.md"_s, u"- inny ^blok\n"_s);
        c = index.changesSince(v);
        QVERIFY(c.notes.contains(u"Ala.md"_s));
        QCOMPARE(index.blockById(u"blok"_s)->path, u"A.md"_s);

        // Removal and rename: names, both paths.
        v = index.version();
        index.remove(u"Ola.md"_s);
        c = index.changesSince(v);
        QCOMPARE(c.notes, QList<VaultPath>({u"Ola.md"_s}));
        QVERIFY(c.names);
        v = index.version();
        index.rename(u"Brak.md"_s, u"x/Nowy.md"_s, u"- jestem\n"_s);
        c = index.changesSince(v);
        QCOMPARE(c.notes, QList<VaultPath>({u"Brak.md"_s, u"x/Nowy.md"_s, u"Żółw.md"_s}));
        QVERIFY(c.names);

        // Attachments: names, no notes unless a link now resolves.
        v = index.version();
        index.update(u"zdjęcie.png"_s, QString());
        c = index.changesSince(v);
        QVERIFY(c.complete && c.names);
        QVERIFY(c.notes.isEmpty());

        // Accumulated: the union since an older version.
        const int older = index.version();
        index.update(u"Ala.md"_s, u"- inaczej\n"_s);
        index.update(u"A.md"_s, u"- znowu ^blok\n"_s);
        c = index.changesSince(older);
        QCOMPARE(c.notes, QList<VaultPath>({u"A.md"_s, u"Ala.md"_s}));
        QVERIFY(!c.names);

        // A config change that parses again, clear(), and a log that no longer reaches back.
        v = index.version();
        VaultConfig config;
        config.journalsFolder = u"dziennik"_s;
        index.setConfig(config);
        QVERIFY(!index.changesSince(v).complete);
        QVERIFY(index.changesSince(index.version()).complete);
        v = index.version();
        config.newPagesFolder = u"strony"_s; // parsing unchanged: no change
        index.setConfig(config);
        QCOMPARE(index.version(), v);
        index.clear();
        QVERIFY(!index.changesSince(v).complete);
        v = index.version();
        for (int i = 0; i <= VaultIndex::MaxLoggedChanges; ++i)
            index.update(u"n.md"_s, u"- wersja %1\n"_s.arg(i));
        QVERIFY(!index.changesSince(v).complete);
        QVERIFY(index.changesSince(v + 1).complete);
        QCOMPARE(index.changesSince(v + 1).notes, QList<VaultPath>({u"n.md"_s}));
    }
};

QTEST_GUILESS_MAIN(TestIndexInternals)
#include "tst_index_internals.moc"
