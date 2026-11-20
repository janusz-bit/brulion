// The built-in flashcards extension (src/core/extensions/builtin/fiszki.luau, docs/SYNTAX.md
// "Flashcards") on a real notebook: a vault, its index and the NotebookHost writing to disk.
//   * FSRS-6 against the reference implementations' vectors (py-fsrs 6.3.3, ts-fsrs, fsrs-rs):
//     the review sequence's intervals and every step's stability and difficulty, the memory state
//     after a lapse and same-day reviews, the first review of each grade, learning and relearning
//     steps, retrievability, the SM-2 conversion, the fuzz's ranges and its seeded generator;
//   * every card format of the Obsidian Spaced Repetition plugin (single-line, both ways,
//     multi-line, reversed multi-line, clozes numbered and not, Anki's) and of Logseq (#card blocks,
//     {{cloze}} with a cue), with Polish text, decks by tag line / frontmatter / the card's own tag,
//     and malformed input (unclosed clozes, a separator in inline code or a fence, empty sides,
//     property lines, HTML comments, a line of 100 000 characters);
//   * the state read (FSRS and SM-2 segments, a new sibling's placeholder, Logseq's properties
//     and card-fsrs, the later of the two) and written back with every other byte kept (CRLF, a
//     BOM, no final line break, a block id, a comment on its own line, a code fence at the end),
//     Logseq's properties in one write, the review log and the undo of both;
//   * the queue: learning steps due, reviews due, the day's new-card limit counted from the log,
//     studying ahead and more new cards; the decks, review and statistics views' nodes, the
//     review session (reveal, grade, undo) through the view's actions;
//   * 4000 cards counted and drawn within the time limit.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

const QString kCards = u"fiszki"_s;

/// A card of the `list` command as the tests compare it: "kind sibling/siblings question → answer".
QString shortOf(const QVariantMap &c)
{
    return u"%1 %2/%3 %4 → %5"_s.arg(c.value(u"kind"_s).toString())
            .arg(c.value(u"sibling"_s).toInt())
            .arg(c.value(u"siblings"_s).toInt())
            .arg(c.value(u"question"_s).toString(), c.value(u"answer"_s).toString());
}

/// "2026-11-20 18:30" local → the UTC text the extension writes (b.toUtc).
QString utc(const QString &local)
{
    const QDateTime t(QDate::fromString(local.left(10), Qt::ISODate), QTime::fromString(local.mid(11), u"HH:mm"_s),
                      QTimeZone::LocalTime);
    return t.toUTC().toString(u"yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"_s);
}

} // namespace

class TestExtensionsFlashcards : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QDateTime m_now{QDate(2026, 11, 20), QTime(18, 30)};
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;
    QString m_dir;

    bool open(const QHash<QString, QString> &files, const QString &language = u"pl"_s)
    {
        m_engine.reset();
        m_host.reset();
        m_index.reset();
        m_vault.reset();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QDir().mkpath(m_dir);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            if (!writeFile(it.key(), it.value()))
                return false;
        m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!m_vault->open())
            return false;
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        {
            VaultIndex::Batch batch(*m_index);
            for (auto it = files.cbegin(); it != files.cend(); ++it)
                if (it.key().endsWith(u".md"_s))
                    m_index->update(it.key(), m_vault->readText(it.key()));
        }
        NotebookHost::Options options;
        options.now = [this] { return m_now; };
        options.language = language;
        options.uiLanguage = language;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}))
            return false;
        const Result loaded = m_engine->load(builtinExtension(kCards));
        if (!loaded.ok())
            qWarning("%s", qPrintable(loaded.error));
        return loaded.ok();
    }

    bool writeFile(const QString &path, const QString &content)
    {
        const QString file = m_dir + u'/' + path;
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(content.toUtf8());
        return true;
    }

    /// The file's text (a BOM decoded as U+FEFF, not dropped).
    QString onDisk(const QString &path) const
    {
        QFile f(m_dir + u'/' + path);
        if (!f.open(QIODevice::ReadOnly))
            return u"<missing>"_s;
        const QByteArray bytes = f.readAll();
        const bool bom = bytes.startsWith("\xEF\xBB\xBF");
        return (bom ? QString(QChar(0xFEFF)) : QString()) + QString::fromUtf8(bom ? bytes.mid(3) : bytes);
    }

    QVariantMap command(const QString &name, const QVariantMap &args = {})
    {
        const Result r = m_engine->runCommand(kCards, name,
                                              {{u"page"_s, QString()}, {u"selection"_s, QString()}, {u"args"_s, args}});
        if (!r.ok())
            qWarning("%s: %s", qPrintable(name), qPrintable(r.error));
        return r.value.toMap();
    }

    QList<QVariantMap> cards(const QVariantMap &args = {})
    {
        QList<QVariantMap> out;
        for (const QVariant &c : command(u"list"_s, args).value(u"cards"_s).toList())
            out << c.toMap();
        return out;
    }

    QStringList shortCards(const QVariantMap &args = {})
    {
        QStringList out;
        for (const QVariantMap &c : cards(args))
            out << shortOf(c);
        return out;
    }

    QVariantMap cardWhere(const QString &question)
    {
        for (const QVariantMap &c : cards())
            if (c.value(u"question"_s).toString() == question)
                return c;
        return {};
    }

    QVariantList steps(const QVariantList &grades, const QVariantMap &more = {})
    {
        QVariantMap args = more;
        args.insert(u"grades"_s, grades);
        if (!args.contains(u"fuzz"_s))
            args.insert(u"fuzz"_s, false);
        if (!args.contains(u"start"_s))
            args.insert(u"start"_s, u"2022-11-29 12:30"_s);
        return command(u"schedule"_s, args).value(u"steps"_s).toList();
    }

    QVariantMap render(const QString &view, const QVariantMap &params = {})
    {
        const Result r = m_engine->render(kCards, view, params);
        if (!r.ok())
            qWarning("%s", qPrintable(r.error));
        return r.value.toMap();
    }

    Result act(const QString &action, const QVariantMap &args)
    {
        return m_engine->act(kCards, u"review"_s, action, args);
    }

    /// Every node of a view, depth first.
    static void walk(const QVariantMap &node, QList<QVariantMap> *out)
    {
        out->append(node);
        for (const QVariant &c : node.value(u"children"_s).toList())
            walk(c.toMap(), out);
    }
    static QList<QVariantMap> nodes(const QVariantMap &root)
    {
        QList<QVariantMap> out;
        walk(root, &out);
        return out;
    }
    static QVariantMap firstOfType(const QVariantMap &root, const QString &type, const QString &action = QString())
    {
        for (const QVariantMap &n : nodes(root))
            if (n.value(u"type"_s) == type && (action.isEmpty() || n.value(u"action"_s) == action))
                return n;
        return {};
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    // ---- FSRS-6 ---------------------------------------------------------------------------

    // py-fsrs test_review_card / test_custom_scheduler_args, ts-fsrs FSRS-6 ivl_history: fuzz off,
    // steps (1 min, 10 min) / (10 min), each review at the due of the one before.
    void reviewSequence()
    {
        QVERIFY(open({}));
        const QVariantList grades{3, 3, 3, 3, 3, 3, 1, 1, 3, 3, 3, 3, 3};
        const QVariantList s = steps(grades);
        QCOMPARE(s.size(), grades.size());
        QList<int> intervals;
        for (const QVariant &v : s)
            intervals << v.toMap().value(u"interval"_s).toInt();
        QCOMPARE(intervals, (QList<int>{0, 2, 11, 46, 163, 498, 0, 0, 2, 4, 7, 12, 21}));
        // Each step's stability and difficulty as py-fsrs computes them (its port, f64).
        const QList<std::pair<double, double>> sd{
                {2.3065, 2.1181039705},          {2.3065, 2.1112142358},          {10.9710482631, 2.1043313908},
                {46.3168584401, 2.0974554288},   {162.9998157747, 2.0905863426},  {497.8765551246, 2.0837241256},
                {6.8904125076, 7.3832023200},    {2.1545983743, 9.1251047661},    {2.1545983743, 9.1112080307},
                {3.9831233795, 9.0973251919},    {7.2362544769, 9.0834562360},    {12.4830435787, 9.0696011491},
                {20.7703572850, 9.0557599172}};
        const QStringList states{u"learning"_s, u"review"_s,     u"review"_s,     u"review"_s, u"review"_s,
                                 u"review"_s,   u"relearning"_s, u"relearning"_s, u"review"_s, u"review"_s,
                                 u"review"_s,   u"review"_s,     u"review"_s};
        for (qsizetype i = 0; i < s.size(); ++i) {
            const QVariantMap m = s[i].toMap();
            QVERIFY2(qAbs(m.value(u"stability"_s).toDouble() - sd[i].first) < 1e-6, qPrintable(QString::number(i)));
            QVERIFY2(qAbs(m.value(u"difficulty"_s).toDouble() - sd[i].second) < 1e-6, qPrintable(QString::number(i)));
            QCOMPARE(m.value(u"state"_s).toString(), states[i]);
        }
        QCOMPARE(s[0].toMap().value(u"minutes"_s).toInt(), 10); // the second learning step
        QCOMPARE(s[6].toMap().value(u"minutes"_s).toInt(), 10); // relearning
        QCOMPARE(s[6].toMap().value(u"lapses"_s).toInt(), 1);
        QCOMPARE(s.last().toMap().value(u"reps"_s).toInt(), 13);
    }

    // py-fsrs test_memo_state, ts-fsrs memory state[short-term], fsrs-rs test_memory_state: Again,
    // then Good at day offsets 0, 1, 3, 8, 21 (the same-day formula of FSRS-6 included).
    void memoryState()
    {
        QVERIFY(open({}));
        const QVariantList s = steps({1, 3, 3, 3, 3, 3},
                                     {{u"at"_s, QVariantList{u"2022-11-29 12:30"_s, u"2022-11-29 12:30"_s,
                                                             u"2022-11-30 12:30"_s, u"2022-12-03 12:30"_s,
                                                             u"2022-12-11 12:30"_s, u"2023-01-01 12:30"_s}}});
        QCOMPARE(s.size(), 6);
        const QVariantMap last = s.last().toMap();
        QVERIFY2(qAbs(last.value(u"stability"_s).toDouble() - 53.62691) < 1e-4, qPrintable(last.value(u"stability"_s).toString()));
        QVERIFY2(qAbs(last.value(u"difficulty"_s).toDouble() - 6.3574867) < 1e-4, qPrintable(last.value(u"difficulty"_s).toString()));
        // The steps between: 0.212 / 6.4133 after Again, 0.2466891878 after the same-day Good.
        QVERIFY(qAbs(s[0].toMap().value(u"stability"_s).toDouble() - 0.212) < 1e-9);
        QVERIFY(qAbs(s[1].toMap().value(u"stability"_s).toDouble() - 0.2466891878) < 1e-8);
        QVERIFY(qAbs(s[2].toMap().value(u"stability"_s).toDouble() - 2.0214772516) < 1e-8);
        QVERIFY(qAbs(s[3].toMap().value(u"stability"_s).toDouble() - 7.8636988410) < 1e-8);
        QVERIFY(qAbs(s[4].toMap().value(u"stability"_s).toDouble() - 21.9177131532) < 1e-8);
    }

    // The first review of a new card with each grade (ts-fsrs "first repeat", fsrs-rs next_states):
    // S0 = w[g-1], D0 clamped, Again 1 min, Hard 6 min (the middle of 1 and 10, as ts-fsrs rounds it),
    // Good 10 min, Easy straight to review in 8 days; and the retrievability at that due.
    void firstReview_data()
    {
        QTest::addColumn<int>("grade");
        QTest::addColumn<double>("stability");
        QTest::addColumn<double>("difficulty");
        QTest::addColumn<QString>("state");
        QTest::addColumn<int>("minutes");
        QTest::newRow("again") << 1 << 0.212 << 6.4133 << u"learning"_s << 1;
        QTest::newRow("hard") << 2 << 1.2931 << 5.11217071 << u"learning"_s << 6;
        QTest::newRow("good") << 3 << 2.3065 << 2.11810397 << u"learning"_s << 10;
        QTest::newRow("easy") << 4 << 8.2956 << 1.0 << u"review"_s << 8 * 1440;
    }
    void firstReview()
    {
        QFETCH(int, grade);
        QFETCH(double, stability);
        QFETCH(double, difficulty);
        QFETCH(QString, state);
        QFETCH(int, minutes);
        QVERIFY(open({}));
        const QVariantMap m = steps({grade}).value(0).toMap();
        QVERIFY(qAbs(m.value(u"stability"_s).toDouble() - stability) < 1e-7);
        QVERIFY(qAbs(m.value(u"difficulty"_s).toDouble() - difficulty) < 1e-7);
        QCOMPARE(m.value(u"state"_s).toString(), state);
        QCOMPARE(m.value(u"minutes"_s).toInt(), minutes);
        if (grade == 4) // ts-fsrs "get retrievability": 8 days after a first Easy
            QVERIFY(qAbs(m.value(u"r"_s).toDouble() - 0.9024733) < 1e-6);
    }

    // py-fsrs's step tests: Hard repeats a later step, a lone relearning step's Hard is 1.5 × it,
    // Again in review relearns 10 minutes, then Good graduates (at least a day).
    void steps_()
    {
        QVERIFY(open({}));
        // Good (step 1), Hard: the second step again.
        QCOMPARE(steps({3, 2}).value(1).toMap().value(u"minutes"_s).toInt(), 10);
        // Learning, review, Again: relearning 10 minutes; Hard there: 15; Good: back to review.
        const QVariantList s = steps({3, 3, 1, 2, 3});
        QCOMPARE(s[2].toMap().value(u"state"_s).toString(), u"relearning"_s);
        QCOMPARE(s[2].toMap().value(u"minutes"_s).toInt(), 10);
        QCOMPARE(s[3].toMap().value(u"minutes"_s).toInt(), 15);
        QCOMPARE(s[4].toMap().value(u"state"_s).toString(), u"review"_s);
        QVERIFY(s[4].toMap().value(u"interval"_s).toInt() >= 1);
        // Ten Easy grades in a row: the difficulty bottoms out at 1 (py test_repeated_correct_reviews).
        QCOMPARE(steps({4, 4, 4, 4, 4, 4, 4, 4, 4, 4}).last().toMap().value(u"difficulty"_s).toDouble(), 1.0);
        // A same-day Hard never lowers the stability (FSRS-6: SInc ≥ 1 from Hard up).
        const QVariantList same = steps({4, 2}, {{u"at"_s, QVariantList{u"2026-11-20 08:00"_s, u"2026-11-20 09:00"_s}}});
        QVERIFY(same[1].toMap().value(u"stability"_s).toDouble() >= same[0].toMap().value(u"stability"_s).toDouble());
        // A lower desired retention, longer intervals; a higher, shorter.
        const int at90 = steps({4}).value(0).toMap().value(u"interval"_s).toInt();
        const int at80 = steps({4}, {{u"retention"_s, 0.8}}).value(0).toMap().value(u"interval"_s).toInt();
        const int at95 = steps({4}, {{u"retention"_s, 95}}).value(0).toMap().value(u"interval"_s).toInt();
        QVERIFY2(at80 > at90 && at90 > at95, qPrintable(u"%1 %2 %3"_s.arg(at80).arg(at90).arg(at95)));
        // Grades in words, both languages; a grade that is none is refused.
        QCOMPARE(steps({u"dobrze"_s, u"good"_s}).size(), 2);
        QCOMPARE(command(u"schedule"_s, {{u"grades"_s, QVariantList{u"wow"_s}}}).value(u"error"_s).toString(),
                 u"bad_argument"_s);
    }

    // fsrs-rs test_memory_from_sm2 (FSRS-6 defaults, retention 0.9): what an SM-2 card stands for.
    void sm2Conversion_data()
    {
        QTest::addColumn<double>("ease");
        QTest::addColumn<int>("interval");
        QTest::addColumn<double>("stability");
        QTest::addColumn<double>("difficulty");
        QTest::newRow("2.5 × 10 d") << 2.5 << 10 << 10.0 << 6.914055235771552;
        QTest::newRow("1.3 × 20 d (clamped)") << 1.3 << 20 << 20.0 << 10.0;
        QTest::newRow("2.0 × 15 d") << 2.0 << 15 << 15.0 << 8.0856746;
    }
    void sm2Conversion()
    {
        QFETCH(double, ease);
        QFETCH(int, interval);
        QFETCH(double, stability);
        QFETCH(double, difficulty);
        QVERIFY(open({}));
        const QVariantMap m = command(u"schedule"_s, {{u"sm2"_s, QVariantList{ease, interval}}});
        QVERIFY(qAbs(m.value(u"stability"_s).toDouble() - stability) < 1e-4);
        QVERIFY2(qAbs(m.value(u"difficulty"_s).toDouble() - difficulty) < 1e-4, qPrintable(m.value(u"difficulty"_s).toString()));
    }

    // The fuzz: py-fsrs's ranges, ts-fsrs 6's generator (FNV-1a + Mulberry32), and a fuzzed
    // interval that is the same on every run and stays in its range.
    void fuzz()
    {
        QVERIFY(open({}));
        const QVariantMap draw = command(u"schedule"_s, {{u"draw"_s, u"string-card-1"_s}});
        QCOMPARE(draw.value(u"hash"_s).toLongLong(), 2086300423ll);
        QVERIFY(qAbs(draw.value(u"draw"_s).toDouble() - 0.643895001616329) < 1e-12);
        const QList<std::tuple<int, int, int>> ranges{{3, 2, 4}, {10, 8, 12}, {30, 27, 33}, {100, 93, 107}};
        for (const auto &[days, low, high] : ranges) {
            const QVariantMap r = command(u"schedule"_s, {{u"fuzzRange"_s, days}});
            QCOMPARE(r.value(u"low"_s).toInt(), low);
            QCOMPARE(r.value(u"high"_s).toInt(), high);
        }
        // Good, Good, Good with fuzz: the third interval (11 days unfuzzed) within 9 … 13, and the
        // same for the same card; other cards spread over the range.
        QSet<int> seen;
        for (int k = 0; k < 40; ++k) {
            const QVariantMap args{{u"fuzz"_s, true}, {u"seed"_s, u"card-%1"_s.arg(k)}};
            const int a = steps({3, 3, 3}, args).value(2).toMap().value(u"interval"_s).toInt();
            const int b = steps({3, 3, 3}, args).value(2).toMap().value(u"interval"_s).toInt();
            QCOMPARE(a, b);
            QVERIFY2(a >= 9 && a <= 13, qPrintable(QString::number(a)));
            seen << a;
        }
        QVERIFY(seen.size() >= 3);
    }

    // ---- cards in notes -------------------------------------------------------------------

    void parse_data()
    {
        QTest::addColumn<QString>("note");
        QTest::addColumn<QStringList>("expected");
        const QString tag = u"---\ntags: [fiszki]\n---\n"_s;
        QTest::newRow("single line") << tag + u"Stolica Peru::Lima\nStolica Polski :: Warszawa\n"_s
                                     << QStringList{u"basic 1/1 Stolica Peru → Lima"_s,
                                                    u"basic 1/1 Stolica Polski → Warszawa"_s};
        QTest::newRow("both ways") << tag + u"żółw:::turtle\n"_s
                                   << QStringList{u"reversed 1/2 żółw → turtle"_s, u"reversed 2/2 turtle → żółw"_s};
        QTest::newRow("a::b:::c is both ways at :::") << tag + u"a::b:::c\n"_s
                                                     << QStringList{u"reversed 1/2 a::b → c"_s, u"reversed 2/2 c → a::b"_s};
        QTest::newRow("multi-line") << tag + u"Jaka rzeka płynie\nprzez Kraków?\n?\nWisła\nze źródłami w Beskidach\n"_s
                                    << QStringList{u"multiline 1/1 Jaka rzeka płynie\nprzez Kraków? → Wisła\nze źródłami w Beskidach"_s};
        QTest::newRow("multi-line both ways") << tag + u"Gęś\n??\ngoose\n"_s
                                              << QStringList{u"multireversed 1/2 Gęś → goose"_s,
                                                             u"multireversed 2/2 goose → Gęś"_s};
        QTest::newRow("cloze") << tag + u"Najwyższe są ==Rysy== w ==Tatrach==.\n"_s
                               << QStringList{u"cloze 1/2 Najwyższe są **[…]** w Tatrach. → Najwyższe są ==Rysy== w Tatrach."_s,
                                              u"cloze 2/2 Najwyższe są Rysy w **[…]**. → Najwyższe są Rysy w ==Tatrach==."_s};
        QTest::newRow("numbered cloze with a hint") << tag + u"==1;;Kopernik;;astronom== urodził się w ==2;;Toruniu== (==1;;1473==).\n"_s
                                                    << QStringList{u"cloze 1/2 **[astronom]** urodził się w Toruniu (**[…]**). → ==Kopernik== urodził się w Toruniu (==1473==)."_s,
                                                                   u"cloze 2/2 Kopernik urodził się w **[…]** (1473). → Kopernik urodził się w ==Toruniu== (1473)."_s};
        QTest::newRow("Anki cloze") << tag + u"Słowo {{c1::żółw::zwierzę}} i {{c2::jeż}}.\n"_s
                                    << QStringList{u"cloze 1/2 Słowo **[zwierzę]** i jeż. → Słowo ==żółw== i jeż."_s,
                                                   u"cloze 2/2 Słowo żółw i **[…]**. → Słowo żółw i ==jeż==."_s};
        QTest::newRow("list items, a block id") << tag + u"- dom::house ^abc123\n- [ ] kot::cat\n"_s
                                                << QStringList{u"basic 1/1 dom → house"_s, u"basic 1/1 kot → cat"_s};
        QTest::newRow("a property line is no card") << tag + u"Stolica:: Warszawa\nid:: 64a1\n- Stolica:: Warszawa\n"_s
                                                    << QStringList{u"basic 1/1 Stolica → Warszawa"_s};
        QTest::newRow("separator in inline code") << tag + u"`a::b` i c::d\nPytanie z `kodem`::odpowiedź\n"_s
                                                  << QStringList{u"basic 1/1 Pytanie z `kodem` → odpowiedź"_s};
        QTest::newRow("code fence") << tag + u"```\nkod::nie karta\n```\n\nPytanie?\n?\n```lua\nprint(1)\n\n```\n"_s
                                    << QStringList{u"multiline 1/1 Pytanie? → ```lua\nprint(1)\n\n```"_s};
        QTest::newRow("HTML comment") << tag + u"<!-- ukryte::nie -->\nwidoczne::tak\n"_s
                                      << QStringList{u"basic 1/1 widoczne → tak"_s};
        QTest::newRow("question mark without a question") << tag + u"?\nA1\n\nQ2\n?\nA2\n"_s
                                                          << QStringList{u"multiline 1/1 Q2 → A2"_s};
        QTest::newRow("unclosed cloze, empty sides") << tag + u"tylko ==otwarty\n::\n====\n"_s
                                                     << QStringList{};
        QTest::newRow("no flashcard tag") << u"Stolica Peru::Lima\n"_s << QStringList{};
        QTest::newRow("tag in the body, sub-deck") << u"# Hiszpański\n\n#flashcards/hiszpański\n\nperro::pies\n"_s
                                                   << QStringList{u"basic 1/1 perro → pies"_s};
        QTest::newRow("frontmatter not a card") << u"---\ntags: [fiszki]\nopis:: nie::karta\n---\nA::B\n"_s
                                                << QStringList{u"basic 1/1 A → B"_s};
        QTest::newRow("scheduling comment on its own line") << tag + u"pies::dog\n<!--SR:!2026-11-18,4,270-->\nkot::cat\n"_s
                                                            << QStringList{u"basic 1/1 pies → dog"_s, u"basic 1/1 kot → cat"_s};
        QTest::newRow("CRLF") << QString(tag + u"Zima::winter\n\nRok\n?\nyear\n"_s).replace(u"\n"_s, u"\r\n"_s)
                              << QStringList{u"basic 1/1 Zima → winter"_s, u"multiline 1/1 Rok → year"_s};
        QTest::newRow("a line of 100 000 characters") << tag + QString(100000, u'x') + u"::"_s + QString(10, u'y') + u"\n"_s
                                                      << QStringList{u"basic 1/1 "_s + QString(100000, u'x') + u" → "_s + QString(10, u'y')};
    }
    void parse()
    {
        QFETCH(QString, note);
        QFETCH(QStringList, expected);
        QVERIFY(open({{u"Nauka.md"_s, note}}));
        QCOMPARE(shortCards(), expected);
    }

    // Decks: the frontmatter's tags, a line of tags above (the last one), a card's own tag at its
    // start; sub-decks are in their parent deck too.
    void decks()
    {
        QVERIFY(open({{u"Języki/Hiszpański.md"_s, u"---\ntags: [flashcards/hiszpański]\n---\nperro::pies\n\n"
                                                  "#flashcards/hiszpański/czasowniki\n\ncomer::jeść\n"
                                                  "#flashcards/hiszpański/liczby uno::jeden\n"_s},
                      {u"Logseq.md"_s, u"- Co znaczy „brulion”? #card #słówka\n  - zeszyt\n"_s}}));
        QHash<QString, QStringList> byQuestion;
        for (const QVariantMap &c : cards())
            byQuestion.insert(c.value(u"question"_s).toString(), c.value(u"decks"_s).toStringList());
        QCOMPARE(byQuestion.value(u"perro"_s), QStringList{u"flashcards/hiszpański"_s});
        QCOMPARE(byQuestion.value(u"comer"_s), QStringList{u"flashcards/hiszpański/czasowniki"_s});
        QCOMPARE(byQuestion.value(u"uno"_s), QStringList{u"flashcards/hiszpański/liczby"_s});
        QCOMPARE(byQuestion.value(u"Co znaczy „brulion”?"_s), (QStringList{u"card"_s, u"słówka"_s})); // (its tags no words)
        QCOMPARE(cards({{u"deck"_s, u"tag:flashcards/hiszpański"_s}}).size(), 3);
        QCOMPARE(cards({{u"deck"_s, u"tag:flashcards/hiszpański/czasowniki"_s}}).size(), 1);
        QCOMPARE(cards({{u"deck"_s, u"folder:Języki"_s}}).size(), 3);
        QCOMPARE(cards({{u"deck"_s, u"page:logseq"_s}}).size(), 1);
        // Other tags make other notes decks.
        QCOMPARE(cards({{u"tags"_s, u"słówka"_s}}).size(), 1);
    }

    // Logseq: the block the question, its children the answer, clozes hidden all at once (a cue
    // shown), its properties no part of either.
    void logseq()
    {
        QVERIFY(open({{u"pages/Polski.md"_s,
                       u"- Stolicą Francji jest {{cloze Paryż}} #card\n"
                       "- Co znaczy „brulion”? #card\n"
                       "  card-repeats:: 1\n"
                       "  - zeszyt na **notatki**\n"
                       "    - też: szkic\n"
                       "- Najdłuższa rzeka to {{cloze Wisła \\\\ rzeka}} #card\n"
                       "- zwykły blok\n"_s}}));
        QCOMPARE(shortCards(), (QStringList{u"logseq 1/1 Stolicą Francji jest **[…]** → Stolicą Francji jest ==Paryż=="_s,
                                            u"logseq 1/1 Co znaczy „brulion”? → zeszyt na **notatki**\n  - też: szkic"_s,
                                            u"logseq 1/1 Najdłuższa rzeka to **(rzeka)** → Najdłuższa rzeka to ==Wisła=="_s}));
    }

    // ---- the state ------------------------------------------------------------------------

    // Read: an SM-2 segment converted, an FSRS segment as written, the placeholder of a sibling
    // never reviewed, Logseq's own review, card-fsrs when it is the later.
    void readState()
    {
        const QString fsrs = u"fsrs,"_s + utc(u"2026-11-23 09:00"_s) + u",3,3.17,5.28,2,4,1,0,"_s + utc(u"2026-11-20 09:00"_s);
        QVERIFY(open({{u"Nauka.md"_s, u"#fiszki\n\nsm::two <!--SR:!2026-11-25,10,250-->\nfs::rs <!--SR:!"_s + fsrs
                                          + u"-->\nraz:::dwa <!--SR:!2000-01-01,1,250!2026-11-22,3,230-->\n"_s},
                      {u"Logseq.md"_s, u"- logseq1 #card\n  card-last-interval:: 4\n  card-repeats:: 2\n"
                                       "  card-ease-factor:: 2.6\n  card-next-schedule:: 2026-11-24T08:00:00.000Z\n"
                                       "  card-last-reviewed:: 2026-11-20T08:00:00.000Z\n  card-last-score:: 5\n"
                                       "  - odpowiedź\n"_s}}));
        const QVariantMap sm = cardWhere(u"sm"_s).value(u"memory"_s).toMap();
        QCOMPARE(sm.value(u"state"_s).toString(), u"review"_s);
        QCOMPARE(sm.value(u"due"_s).toString(), u"2026-11-25 00:00"_s);
        QCOMPARE(sm.value(u"interval"_s).toInt(), 10);
        QCOMPARE(sm.value(u"stability"_s).toDouble(), 10.0);
        QCOMPARE(sm.value(u"difficulty"_s).toDouble(), 6.9141);
        QCOMPARE(sm.value(u"lastReview"_s).toString(), u"2026-11-15 00:00"_s);
        const QVariantMap fs = cardWhere(u"fs"_s).value(u"memory"_s).toMap();
        QCOMPARE(fs.value(u"due"_s).toString(), u"2026-11-23 09:00"_s);
        QCOMPARE(fs.value(u"stability"_s).toDouble(), 3.17);
        QCOMPARE(fs.value(u"difficulty"_s).toDouble(), 5.28);
        QCOMPARE(fs.value(u"reps"_s).toInt(), 4);
        QCOMPARE(fs.value(u"lapses"_s).toInt(), 1);
        QCOMPARE(fs.value(u"lastReview"_s).toString(), u"2026-11-20 09:00"_s);
        QCOMPARE(cardWhere(u"raz"_s).value(u"memory"_s).toMap().value(u"state"_s).toString(), u"new"_s);
        QCOMPARE(cardWhere(u"dwa"_s).value(u"memory"_s).toMap().value(u"due"_s).toString(), u"2026-11-22 00:00"_s);
        const QVariantMap lq = cardWhere(u"logseq1"_s).value(u"memory"_s).toMap();
        QCOMPARE(lq.value(u"state"_s).toString(), u"review"_s);
        QCOMPARE(lq.value(u"reps"_s).toInt(), 2);
        QCOMPARE(lq.value(u"interval"_s).toInt(), 4);
        QCOMPARE(lq.value(u"stability"_s).toDouble(), 4.0);
    }

    // Written: a grade puts the plugin's FSRS segment into the card's line (or its comment line),
    // every other byte as it was.
    void writeState_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("question");
        QTest::addColumn<QString>("after"); // %1 the comment written
        QTest::newRow("single line") << u"#fiszki\n\nA::B\nC::D\n"_s << u"A"_s << u"#fiszki\n\nA::B %1\nC::D\n"_s;
        QTest::newRow("CRLF, no final break") << u"#fiszki\r\n\r\nA::B\r\nC::D"_s << u"C"_s
                                              << u"#fiszki\r\n\r\nA::B\r\nC::D %1"_s;
        QTest::newRow("BOM") << u"﻿A::B #fiszki\n"_s << u"A"_s << u"﻿A::B #fiszki %1\n"_s;
        QTest::newRow("block id stays last") << u"#fiszki\n- A::B ^abc-1\n"_s << u"A"_s << u"#fiszki\n- A::B %1 ^abc-1\n"_s;
        QTest::newRow("comment on its own line") << u"#fiszki\n\nA::B\n<!--SR:!2026-11-18,4,270-->\nC::D\n"_s << u"A"_s
                                                 << u"#fiszki\n\nA::B\n%1\nC::D\n"_s;
        QTest::newRow("multi-line") << u"#fiszki\n\nPytanie\n?\nOdpowiedź\n\nX::Y\n"_s << u"Pytanie"_s
                                    << u"#fiszki\n\nPytanie\n?\nOdpowiedź %1\n\nX::Y\n"_s;
        QTest::newRow("ends with a fence") << u"#fiszki\n\nKod?\n?\n```\nx\n```\n"_s << u"Kod?"_s
                                           << u"#fiszki\n\nKod?\n?\n```\nx\n```\n%1\n"_s;
    }
    void writeState()
    {
        QFETCH(QString, before);
        QFETCH(QString, question);
        QFETCH(QString, after);
        QVERIFY(open({{u"Nauka.md"_s, before}}));
        const QVariantMap card = cardWhere(question);
        QVERIFY(!card.isEmpty());
        const QVariantMap r = command(u"grade"_s, {{u"id"_s, card.value(u"id"_s)}, {u"grade"_s, u"good"_s}});
        QVERIFY2(r.value(u"error"_s).toString().isEmpty(), qPrintable(r.value(u"message"_s).toString()));
        const QVariantMap m = r.value(u"after"_s).toMap();
        const QString comment = u"<!--SR:!fsrs,%1,%2,%3,%4,%5,%6,%7,%8,%9-->"_s
                                        .arg(utc(m.value(u"due"_s).toString()))
                                        .arg(m.value(u"interval"_s).toInt())
                                        .arg(QString::number(m.value(u"stability"_s).toDouble()),
                                             QString::number(m.value(u"difficulty"_s).toDouble()))
                                        .arg(m.value(u"state"_s).toString() == u"review"_s ? 2 : 1)
                                        .arg(m.value(u"reps"_s).toInt())
                                        .arg(m.value(u"lapses"_s).toInt())
                                        .arg(m.value(u"step"_s).toInt())
                                        .arg(utc(u"2026-11-20 18:30"_s));
        QCOMPARE(onDisk(u"Nauka.md"_s), after.arg(comment));
        // Read back as written.
        const QVariantMap again = cardWhere(question).value(u"memory"_s).toMap();
        QCOMPARE(again.value(u"stability"_s).toDouble(), m.value(u"stability"_s).toDouble());
        QCOMPARE(again.value(u"lastReview"_s).toString(), u"2026-11-20 18:30"_s);
        // The review log: a line for the grade, in the notes' language.
        const QString log = onDisk(u"Fiszki/Powtórki 2026-11.md"_s);
        QVERIFY2(log.contains(u"- 2026-11-20 18:30 dobrze · "_s) && log.contains(question), qPrintable(log));
    }

    // Siblings: the second side of a two-way card keeps the first's placeholder in front of it,
    // an SM-2 sibling stays as it was; a Logseq card gets its six properties and card-fsrs in one
    // write, in Logseq's order.
    void writeSiblingsAndLogseq()
    {
        QVERIFY(open({{u"Nauka.md"_s, u"#fiszki\n\nraz:::dwa\ntrzy:::cztery <!--SR:!2026-11-25,10,250-->\n"_s},
                      {u"Logseq.md"_s, u"- Pytanie #card\n  - Odpowiedź\n- Inny\n"_s}}));
        command(u"grade"_s, {{u"id"_s, cardWhere(u"dwa"_s).value(u"id"_s)}, {u"grade"_s, 3}});
        command(u"grade"_s, {{u"id"_s, cardWhere(u"cztery"_s).value(u"id"_s)}, {u"grade"_s, 4}});
        const QStringList lines = onDisk(u"Nauka.md"_s).split(u'\n');
        QVERIFY2(lines[2].startsWith(u"raz:::dwa <!--SR:!2000-01-01,1,250!fsrs,"_s), qPrintable(lines[2]));
        QVERIFY2(lines[3].startsWith(u"trzy:::cztery <!--SR:!2026-11-25,10,250!fsrs,"_s), qPrintable(lines[3]));
        command(u"grade"_s, {{u"id"_s, cardWhere(u"Pytanie"_s).value(u"id"_s)}, {u"grade"_s, u"łatwe"_s}, {u"fuzz"_s, false}});
        const QStringList logseq = onDisk(u"Logseq.md"_s).split(u'\n');
        QCOMPARE(logseq[0], u"- Pytanie #card"_s);
        QCOMPARE(logseq[1], u"  card-last-interval:: 8"_s);
        QCOMPARE(logseq[2], u"  card-repeats:: 1"_s);
        QCOMPARE(logseq[3], u"  card-ease-factor:: 3"_s);
        QCOMPARE(logseq[4], u"  card-next-schedule:: "_s + utc(u"2026-11-28 18:30"_s));
        QCOMPARE(logseq[5], u"  card-last-reviewed:: "_s + utc(u"2026-11-20 18:30"_s));
        QCOMPARE(logseq[6], u"  card-last-score:: 5"_s);
        QVERIFY(logseq[7].startsWith(u"  card-fsrs:: "_s + utc(u"2026-11-28 18:30"_s) + u",8,8.2956,1,2,1,0,0,"_s));
        QCOMPARE(logseq[8], u"  - Odpowiedź"_s);
        QCOMPARE(logseq[9], u"- Inny"_s);
        // Read back: card-fsrs, the later review.
        QCOMPARE(cardWhere(u"Pytanie"_s).value(u"memory"_s).toMap().value(u"stability"_s).toDouble(), 8.2956);
        // Reviewed in Logseq afterwards (its card-last-reviewed later): Logseq's numbers count.
        QString text = onDisk(u"Logseq.md"_s);
        text.replace(u"card-last-reviewed:: "_s + utc(u"2026-11-20 18:30"_s), u"card-last-reviewed:: 2026-11-29T10:00:00.000Z"_s);
        text.replace(u"card-last-interval:: 8"_s, u"card-last-interval:: 20"_s);
        QVERIFY(writeFile(u"Logseq.md"_s, text));
        m_index->update(u"Logseq.md"_s, text);
        QCOMPARE(cardWhere(u"Pytanie"_s).value(u"memory"_s).toMap().value(u"stability"_s).toDouble(), 20.0);
    }

    // A grade through the review view: reveal, grade (the state and the log written), undo (both
    // as they were, byte for byte).
    void reviewSession()
    {
        const QString note = u"---\ntags: [fiszki]\n---\nStolica Peru::Lima\nStolica Chile::Santiago\n"_s;
        QVERIFY(open({{u"Nauka.md"_s, note}}));
        const QVariantMap noFuzz{{u"fuzz"_s, false}};
        QVariantMap view = render(u"review"_s, noFuzz);
        QVariantMap reveal = firstOfType(view, u"button"_s, u"reveal"_s);
        QCOMPARE(reveal.value(u"key"_s).toString(), u"space"_s);
        QCOMPARE(firstOfType(view, u"markdown"_s).value(u"text"_s).toString(), u"Stolica Peru"_s);
        QVERIFY(act(u"reveal"_s, reveal.value(u"args"_s).toMap()).ok());
        view = render(u"review"_s, noFuzz);
        QList<QVariantMap> grades;
        for (const QVariantMap &n : nodes(view))
            if (n.value(u"action"_s) == u"grade"_s)
                grades << n;
        QCOMPARE(grades.size(), 4);
        QCOMPARE(grades[0].value(u"hint"_s).toString(), u"1 min"_s);
        QCOMPARE(grades[2].value(u"hint"_s).toString(), u"10 min"_s);
        QCOMPARE(grades[3].value(u"hint"_s).toString(), u"8 dni"_s);
        QCOMPARE(grades[3].value(u"key"_s).toString(), u"4"_s);
        QVERIFY(act(u"grade"_s, grades[2].value(u"args"_s).toMap()).ok());
        QVERIFY(onDisk(u"Nauka.md"_s).contains(u"Stolica Peru::Lima <!--SR:!fsrs,"_s));
        QVERIFY(onDisk(u"Fiszki/Powtórki 2026-11.md"_s).contains(u"dobrze · nowa · Stolica Peru"_s));
        // The next card; its args name its line as it is now.
        view = render(u"review"_s);
        QCOMPARE(firstOfType(view, u"markdown"_s).value(u"text"_s).toString(), u"Stolica Chile"_s);
        const QVariantMap undo = firstOfType(view, u"button"_s, u"undo"_s);
        QCOMPARE(undo.value(u"key"_s).toString(), u"u"_s);
        QVERIFY(act(u"undo"_s, undo.value(u"args"_s).toMap()).ok());
        QCOMPARE(onDisk(u"Nauka.md"_s), note);
        QVERIFY(!onDisk(u"Fiszki/Powtórki 2026-11.md"_s).contains(u"Stolica Peru"_s));
        // The card graded again shows with its answer.
        view = render(u"review"_s);
        QCOMPARE(firstOfType(view, u"markdown"_s).value(u"text"_s).toString(), u"Stolica Peru"_s);
        QVERIFY(!firstOfType(view, u"button"_s, u"grade"_s).isEmpty());
        // A line changed meanwhile: the grade is refused, nothing written.
        const QVariantMap stale = firstOfType(view, u"button"_s, u"grade"_s).value(u"args"_s).toMap();
        QString changed = note + u"nowa::linia\n"_s;
        QVERIFY(writeFile(u"Nauka.md"_s, changed.replace(u"Lima"_s, u"Lima!"_s)));
        m_index->update(u"Nauka.md"_s, onDisk(u"Nauka.md"_s));
        const Result r = act(u"grade"_s, stale);
        QVERIFY(r.ok());
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"warning"_s);
        QVERIFY(!onDisk(u"Nauka.md"_s).contains(u"<!--SR:"_s));
    }

    // The queue: learning cards whose step is over, reviews due, new cards up to the day's limit
    // (counted from the log), studying ahead, more new cards.
    void queue()
    {
        QString note = u"#fiszki\n\n"_s;
        for (int i = 1; i <= 30; ++i)
            note += u"nowe %1::new %1\n"_s.arg(i);
        note += u"stara::old <!--SR:!2026-11-20,5,250-->\n"_s; // due today
        note += u"jutro::tomorrow <!--SR:!2026-11-21,5,250-->\n"_s; // due tomorrow
        note += u"ucząca::learning <!--SR:!fsrs,"_s + utc(u"2026-11-20 18:20"_s) + u",0,2.3,2.1,1,1,0,1,"_s
                + utc(u"2026-11-20 18:10"_s) + u"-->\n"_s; // a learning step over
        QString log = u"x\n\n"_s;
        for (int i = 0; i < 15; ++i)
            log += u"- 2026-11-20 08:%1 good · new · coś\n"_s.arg(i, 2, 10, u'0');
        log += u"- 2026-11-19 08:00 again · review · wczoraj\n"_s;
        QVERIFY(open({{u"Nauka.md"_s, note}, {u"Flashcards/Reviews 2026-11.md"_s, log}}));
        QVariantMap n = command(u"count"_s);
        QCOMPARE(n.value(u"review"_s).toInt(), 1);
        QCOMPARE(n.value(u"due"_s).toInt(), 2); // the review and the learning card
        QCOMPARE(n.value(u"new"_s).toInt(), 5); // 20 a day, 15 started today (the English log counts too)
        QCOMPARE(command(u"count"_s, {{u"newPerDay"_s, 10}}).value(u"new"_s).toInt(), 0);
        const QList<QVariantMap> due = cards({{u"due"_s, true}});
        QCOMPARE(due.size(), 7);
        QCOMPARE(due[0].value(u"question"_s).toString(), u"ucząca"_s); // learning first
        QCOMPARE(due[1].value(u"question"_s).toString(), u"stara"_s);  // then reviews
        QCOMPARE(due[2].value(u"question"_s).toString(), u"nowe 1"_s);  // then new, as written
        QCOMPARE(cards({{u"due"_s, true}, {u"ahead"_s, 1}}).size(), 8);
        QCOMPARE(cards({{u"due"_s, true}, {u"more"_s, 10}}).size(), 17);
        // The statistics: the log's grades and the retention of reviews.
        const QVariantMap st = command(u"stats"_s);
        QCOMPARE(st.value(u"reviewedToday"_s).toInt(), 15);
        QCOMPARE(st.value(u"retention"_s).toDouble(), 0.0); // the one review was forgotten
        QCOMPARE(st.value(u"total"_s).toInt(), 33);
        QCOMPARE(st.value(u"perDay"_s).toList().size(), 30);
        QCOMPARE(st.value(u"forecast"_s).toList().value(1).toMap().value(u"count"_s).toInt(), 1); // tomorrow
    }

    // The views: decks by tag, page and folder with their counts; the statistics; the empty states.
    void views()
    {
        QVERIFY(open({}));
        QCOMPARE(firstOfType(render(u"decks"_s), u"empty"_s).value(u"icon"_s).toString(), u"layers"_s);
        QVERIFY(open({{u"Nauka/Geografia.md"_s, u"#fiszki/geografia\n\nPeru::Lima\n"_s},
                      {u"Nauka/Języki/Angielski.md"_s, u"#flashcards\n\ndog::pies\n"_s}}));
        QStringList titles;
        for (const QVariantMap &n : nodes(render(u"decks"_s)))
            if (n.value(u"type"_s) == u"open"_s && n.value(u"view"_s) == u"review"_s)
                titles << n.value(u"label"_s).toString().trimmed() + u'|' + n.value(u"params"_s).toMap().value(u"deck"_s).toString();
        QCOMPARE(titles, (QStringList{u"Ucz się wszystkich|"_s, u"#fiszki|tag:fiszki"_s, u"geografia|tag:fiszki/geografia"_s,
                                      u"#flashcards|tag:flashcards"_s}));
        titles.clear();
        for (const QVariantMap &n : nodes(render(u"decks"_s, {{u"group"_s, u"folder"_s}})))
            if (n.value(u"type"_s) == u"open"_s && n.value(u"view"_s) == u"review"_s)
                titles << n.value(u"params"_s).toMap().value(u"deck"_s).toString();
        QCOMPARE(titles, (QStringList{QString(), u"folder:Nauka"_s, u"folder:Nauka/Języki"_s}));
        const QVariantMap stats = render(u"stats"_s);
        QCOMPARE(firstOfType(stats, u"stats"_s).value(u"items"_s).toList().size(), 4);
        QVERIFY(!firstOfType(stats, u"chart"_s).isEmpty());
        // A deck with nothing to study: done, with studying ahead offered.
        const QVariantMap done = render(u"review"_s, {{u"deck"_s, u"tag:nic"_s}});
        QCOMPARE(firstOfType(done, u"empty"_s).value(u"title"_s).toString(), u"W tej talii nie ma fiszek"_s);
        // English.
        QVERIFY(open({{u"Words.md"_s, u"#flashcards\n\ndog::pies\n"_s}}, u"en"_s));
        QCOMPARE(firstOfType(render(u"review"_s), u"button"_s, u"reveal"_s).value(u"label"_s).toString(), u"Show answer"_s);
    }

    // Many cards: counted and drawn within the time limit (the decks of 4000 cards on 80 pages).
    void manyCards()
    {
        QHash<QString, QString> files;
        for (int p = 0; p < 80; ++p) {
            QString note = u"#fiszki/talia%1\n\n"_s.arg(p % 8);
            for (int i = 0; i < 50; ++i)
                note += i % 3 == 0 ? u"pytanie %1 %2::odpowiedź <!--SR:!2026-11-%3,4,250-->\n"_s.arg(p).arg(i).arg(10 + i % 15)
                                   : u"pytanie %1 %2::odpowiedź\n"_s.arg(p).arg(i);
            files.insert(u"Talie/%1.md"_s.arg(p), note);
        }
        QVERIFY(open(files));
        const QVariantMap n = command(u"count"_s);
        QCOMPARE(n.value(u"total"_s).toInt(), 4000);
        QVERIFY(!firstOfType(render(u"decks"_s), u"stats"_s).isEmpty());
        QVERIFY(!firstOfType(render(u"review"_s), u"markdown"_s).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestExtensionsFlashcards)
#include "tst_extensions_flashcards.moc"
