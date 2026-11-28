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
//   * whole-note reviews (the plugin's #review notes): its `osrSchedule` ported with its own tests,
//     the state read and written in the frontmatter (other keys, quoted values, CRLF, a BOM, a
//     malformed date, no frontmatter), the queue, the review view's session with undo;
//   * 5000 cards counted and drawn within the time limit, and again after another note changed.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
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

    Result act2(const QString &action, const QVariantMap &args)
    {
        return m_engine->act(kCards, u"notes"_s, action, args);
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

    // The plugin's card settings (docs/SYNTAX.md §15 "Settings of the cards"): separators of one's
    // own (the longest looked for first), a separator switched off, the end-of-card marker, the
    // cloze kinds; cases of the plugin's own parser tests with Polish text and malformed input.
    void cardSettings_data()
    {
        QTest::addColumn<QString>("note");
        QTest::addColumn<QVariantMap>("settings");
        QTest::addColumn<QStringList>("expected");
        const QString tag = u"---\ntags: [fiszki]\n---\n"_s;
        const auto s = [](const QString &key, const QVariant &value) { return QVariantMap{{key, value}}; };
        QTest::newRow("a single-line separator of one's own")
                << tag + u"stolica => Warszawa\nżółw::turtle\n"_s << s(u"singleLineSeparator"_s, u"=>"_s)
                << QStringList{u"basic 1/1 stolica → Warszawa"_s};
        QTest::newRow("both ways: the longer separator first")
                << tag + u"a <=> b\nc => d\n"_s
                << QVariantMap{{u"singleLineSeparator"_s, u"=>"_s}, {u"singleLineReversedSeparator"_s, u"<=>"_s}}
                << QStringList{u"reversed 1/2 a → b"_s, u"reversed 2/2 b → a"_s, u"basic 1/1 c → d"_s};
        QTest::newRow("the both-ways separator off: :: is looked for")
                << tag + u"a:::b\n"_s << s(u"singleLineReversedSeparator"_s, QString())
                << QStringList{u"basic 1/1 a → :b"_s};
        QTest::newRow("the single-line separator off")
                << tag + u"a::b\nc:::d\n"_s << s(u"singleLineSeparator"_s, QString())
                << QStringList{u"reversed 1/2 c → d"_s, u"reversed 2/2 d → c"_s};
        QTest::newRow("a separator in inline code is none, of one's own too")
                << tag + u"`a=>b` i c=>d\nPytanie `kod`=>odpowiedź\n"_s << s(u"singleLineSeparator"_s, u"=>"_s)
                << QStringList{u"basic 1/1 Pytanie `kod` → odpowiedź"_s};
        QTest::newRow("multi-line separators of one's own")
                << tag + u"Pytanie\n---\nOdpowiedź\n\nGęś\n----\ngoose\n\nZ\n?\nX\n"_s
                << QVariantMap{{u"multilineSeparator"_s, u"---"_s}, {u"multilineReversedSeparator"_s, u"----"_s}}
                << QStringList{u"multiline 1/1 Pytanie → Odpowiedź"_s, u"multireversed 1/2 Gęś → goose"_s,
                               u"multireversed 2/2 goose → Gęś"_s};
        QTest::newRow("a multi-line separator written with spaces still is a line of its own")
                << tag + u"Pytanie\n  ///  \nOdpowiedź\n"_s << s(u"multilineSeparator"_s, u" /// "_s)
                << QStringList{u"multiline 1/1 Pytanie → Odpowiedź"_s};
        QTest::newRow("multi-line separator off")
                << tag + u"Q\n?\nA\n\nG\n??\nB\n"_s << s(u"multilineSeparator"_s, QString())
                << QStringList{u"multireversed 1/2 G → B"_s, u"multireversed 2/2 B → G"_s};
        QTest::newRow("an end marker: blank lines inside a card")
                << tag + u"Pytanie pierwsze\n?\nOdpowiedź\n\nz pustą linią\n---\nDrugie\n?\nodp\n---\n"_s
                << s(u"multilineEndMarker"_s, u"---"_s)
                << QStringList{u"multiline 1/1 Pytanie pierwsze → Odpowiedź\n\nz pustą linią"_s,
                               u"multiline 1/1 Drugie → odp"_s};
        QTest::newRow("an end marker: a blank line before no card, text after it, the last card unterminated")
                << tag + u"tekst::karta\n\nA\n?\nB\n\n\nC\n---\nluźne\n\nD\n?\nE"_s
                << s(u"multilineEndMarker"_s, u"---"_s)
                << QStringList{u"basic 1/1 tekst → karta"_s, u"multiline 1/1 A → B\n\n\nC"_s,
                               u"multiline 1/1 D → E"_s};
        QTest::newRow("an end marker: a cloze card goes on through blank lines")
                << tag + u"Stolica ==Polski== to\n\nWarszawa ==jest==\n---\n"_s << s(u"multilineEndMarker"_s, u"---"_s)
                << QStringList{u"cloze 1/2 Stolica **[…]** to\n\nWarszawa jest → Stolica ==Polski== to\n\nWarszawa jest"_s,
                               u"cloze 2/2 Stolica Polski to\n\nWarszawa **[…]** → Stolica Polski to\n\nWarszawa ==jest=="_s};
        QTest::newRow("no end marker: a blank line ends a card (the default)")
                << tag + u"A\n?\nB\n\nC\n---\n"_s << QVariantMap{} << QStringList{u"multiline 1/1 A → B"_s};
        QTest::newRow("bold clozes are off by default") << tag + u"Stolicą jest **Warszawa**.\n"_s << QVariantMap{}
                                                        << QStringList{};
        QTest::newRow("bold clozes")
                << tag + u"Stolicą jest **Warszawa**.\n"_s << s(u"clozeBold"_s, true)
                << QStringList{u"cloze 1/1 Stolicą jest **[…]**. → Stolicą jest ==Warszawa==."_s};
        QTest::newRow("numbered bold clozes with a hint")
                << tag + u"**1;;Kopernik;;astronom** w **2;;Toruniu**\n"_s << s(u"clozeBold"_s, true)
                << QStringList{u"cloze 1/2 **[astronom]** w Toruniu → ==Kopernik== w Toruniu"_s,
                               u"cloze 2/2 Kopernik w **[…]** → Kopernik w ==Toruniu=="_s};
        QTest::newRow("bold in inline code and an unclosed bold are text")
                << tag + u"`**kod**` i **tekst** oraz **otwarte\n"_s << s(u"clozeBold"_s, true)
                << QStringList{u"cloze 1/1 `**kod**` i **[…]** oraz **otwarte → `**kod**` i ==tekst== oraz **otwarte"_s};
        QTest::newRow("highlights off: ==x== is text, Anki's still a cloze")
                << tag + u"Są ==Rysy== i {{c1::Giewont}}.\n"_s << s(u"clozeHighlights"_s, false)
                << QStringList{u"cloze 1/1 Są ==Rysy== i **[…]**. → Są ==Rysy== i ==Giewont==."_s};
        QTest::newRow("highlights and bold together")
                << tag + u"==a== i **b**\n"_s << s(u"clozeBold"_s, true)
                << QStringList{u"cloze 1/2 **[…]** i b → ==a== i b"_s, u"cloze 2/2 a i **[…]** → a i ==b=="_s};
        QTest::newRow("curly clozes")
                << tag + u"{{Warszawa}} i {{Kraków}}\n"_s << s(u"clozeCurly"_s, true)
                << QStringList{u"cloze 1/2 **[…]** i Kraków → ==Warszawa== i Kraków"_s,
                               u"cloze 2/2 Warszawa i **[…]** → Warszawa i ==Kraków=="_s};
        QTest::newRow("numbered curly clozes ask only the numbered (the plugin's rule)")
                << tag + u"Stolica to {{Warszawa}}, a {{1;;Kraków;;dawna}}.\n"_s << s(u"clozeCurly"_s, true)
                << QStringList{u"cloze 1/1 Stolica to Warszawa, a **[dawna]**. → Stolica to Warszawa, a ==Kraków==."_s};
        QTest::newRow("curly clozes leave Anki's and Logseq's alone")
                << tag + u"{{c1::żółw}} {{cloze Paryż}}\n\n{{cloze Rzym}}\n"_s << s(u"clozeCurly"_s, true)
                << QStringList{u"cloze 1/1 **[…]** {{cloze Paryż}} → ==żółw== {{cloze Paryż}}"_s};
        QTest::newRow("curly clozes off by default") << tag + u"{{Warszawa}}\n"_s << QVariantMap{} << QStringList{};
        QTest::newRow("empty curly and bold clozes are none")
                << tag + u"{{}} i **** i {{  }}\n"_s << QVariantMap{{u"clozeCurly"_s, true}, {u"clozeBold"_s, true}}
                << QStringList{};
        QTest::newRow("settings that cannot be used are the defaults")
                << tag + u"a::b\nc\n?\nd\n"_s
                << QVariantMap{{u"singleLineSeparator"_s, 5},
                               {u"multilineSeparator"_s, u"x\ny"_s},
                               {u"multilineEndMarker"_s, QString(41, u'x')},
                               {u"clozeBold"_s, u"maybe"_s}}
                << QStringList{u"basic 1/1 a → b"_s, u"multiline 1/1 c → d"_s};
        QTest::newRow("a property line is no card whatever the separators")
                << tag + u"Stolica:: Warszawa\nid:: 64a1\nx => y\n"_s << s(u"singleLineSeparator"_s, u"=>"_s)
                << QStringList{u"basic 1/1 x → y"_s};
    }
    void cardSettings()
    {
        QFETCH(QString, note);
        QFETCH(QVariantMap, settings);
        QFETCH(QStringList, expected);
        QVERIFY(open({{u"Nauka.md"_s, note}}));
        QCOMPARE(shortCards(settings), expected);
    }

    // The cards depend on the settings: the same notebook read with other settings is read again.
    void cardSettingsChangeTheCards()
    {
        QVERIFY(open({{u"Nauka.md"_s, u"#fiszki\n\nA => B\n\nC::D\n\nE ==F==\n"_s}}));
        QCOMPARE(shortCards().size(), 2);
        QCOMPARE(shortCards({{u"singleLineSeparator"_s, u"=>"_s}}),
                 (QStringList{u"basic 1/1 A → B"_s, u"cloze 1/1 E **[…]** → E ==F=="_s}));
        QCOMPARE(shortCards({{u"clozeHighlights"_s, false}}), (QStringList{u"basic 1/1 C → D"_s}));
        QCOMPARE(shortCards().size(), 2);
    }

    // A grade of a card with an end marker writes its state on the card's last line, not on a blank
    // line before the marker; the line is found again.
    void endMarkerState()
    {
        const QVariantMap markers{{u"multilineEndMarker"_s, u"---"_s}};
        QVERIFY(open({{u"Nauka.md"_s, u"#fiszki\n\nPytanie\n?\nOdpowiedź\n\n\n---\nInne::karta\n"_s}}));
        QVariantMap args = markers;
        const QList<QVariantMap> list = cards(markers);
        QCOMPARE(list.size(), 2);
        args.insert(u"id"_s, list.first().value(u"id"_s));
        args.insert(u"grade"_s, u"good"_s);
        const QVariantMap r = command(u"grade"_s, args);
        QVERIFY2(r.value(u"error"_s).toString().isEmpty(), qPrintable(r.value(u"message"_s).toString()));
        const QStringList lines = onDisk(u"Nauka.md"_s).split(u'\n');
        QCOMPARE(lines.size(), 10);
        QVERIFY2(lines[4].startsWith(u"Odpowiedź <!--SR:!fsrs,"_s), qPrintable(lines[4]));
        QCOMPARE(lines[5], QString());
        QCOMPARE(lines[6], QString());
        QCOMPARE(lines[7], u"---"_s);
        QCOMPARE(lines[8], u"Inne::karta"_s);
        QCOMPARE(cards(markers).first().value(u"memory"_s).toMap().value(u"state"_s).toString(), u"learning"_s);
    }

    // "Folders as decks" (the plugin's convertFoldersToDecks): every note in a folder is read, tagged
    // or not, and its cards are in its folder's deck whatever their tags say; notes outside any
    // folder keep their tags'; the review log's pages are no cards.
    void foldersAsDecks()
    {
        QVERIFY(open({{u"Nauka/Hiszpański/Czasowniki.md"_s, u"comer::jeść\n\n#fiszki/inne uno::jeden\n"_s},
                      {u"Nauka/Historia.md"_s, u"---\ntags: [fiszki]\n---\nKopernik::astronom\n"_s},
                      {u"Żywność/Owoce.md"_s, u"jabłko::apple\n"_s},
                      {u"Luźne.md"_s, u"#fiszki/x\n\ncat::kot\n"_s},
                      {u"Luźne bez tagu.md"_s, u"a::b\n"_s},
                      {u"Fiszki/Powtórki 2026-11.md"_s, u"- 2026-11-20 18:30 dobrze · nowa · A::B\n"_s},
                      {u"Flashcards/Reviews 2026-11.md"_s, u"- 2026-11-20 18:30 good · new · C::D\n"_s}}));
        // Off: only the tagged notes.
        QStringList questions;
        for (const QVariantMap &c : cards())
            questions << c.value(u"question"_s).toString();
        QCOMPARE(questions, (QStringList{u"cat"_s, u"Kopernik"_s, u"comer"_s, u"uno"_s})); // (a tag on a card line: a deck note)
        const QVariantMap on{{u"foldersAsDecks"_s, true}};
        QHash<QString, QStringList> decks;
        for (const QVariantMap &c : cards(on))
            decks.insert(c.value(u"question"_s).toString(), c.value(u"decks"_s).toStringList());
        QCOMPARE(decks.size(), 5);
        QCOMPARE(decks.value(u"comer"_s), QStringList{u"nauka/hiszpański"_s});
        QCOMPARE(decks.value(u"uno"_s), QStringList{u"nauka/hiszpański"_s}); // its own tag is no deck
        QCOMPARE(decks.value(u"Kopernik"_s), QStringList{u"nauka"_s});
        QCOMPARE(decks.value(u"jabłko"_s), QStringList{u"żywność"_s});
        QCOMPARE(decks.value(u"cat"_s), QStringList{u"fiszki/x"_s}); // outside any folder: its tag's
        QVERIFY(!decks.contains(u"a"_s));
        QVERIFY(!decks.contains(u"A"_s));
        QVERIFY(!decks.contains(u"C"_s));
        QCOMPARE(cards({{u"foldersAsDecks"_s, true}, {u"deck"_s, u"tag:nauka"_s}}).size(), 3);
        QCOMPARE(cards({{u"foldersAsDecks"_s, true}, {u"deck"_s, u"tag:nauka/hiszpański"_s}}).size(), 2);
        QCOMPARE(cards({{u"foldersAsDecks"_s, true}, {u"deck"_s, u"tag:Żywność"_s}}).size(), 1);
        // The decks by tag are the folders, shown without a "#".
        QStringList titles;
        for (const QVariantMap &n : nodes(render(u"decks"_s, on)))
            if (n.value(u"type"_s) == u"open"_s && n.value(u"view"_s) == u"review"_s)
                titles << n.value(u"label"_s).toString().trimmed() + u'|' + n.value(u"params"_s).toMap().value(u"deck"_s).toString();
        QCOMPARE(titles, (QStringList{u"Ucz się wszystkich|"_s, u"#fiszki|tag:fiszki"_s, u"x|tag:fiszki/x"_s,
                                      u"nauka|tag:nauka"_s, u"hiszpański|tag:nauka/hiszpański"_s,
                                      u"żywność|tag:żywność"_s}));
        // A notebook with no folders is the same with it on.
        QVERIFY(open({{u"Płaska.md"_s, u"#fiszki\n\nx::y\n"_s}}));
        QCOMPARE(cards(on).size(), 1);
        QCOMPARE(cards(on).first().value(u"decks"_s).toStringList(), QStringList{u"fiszki"_s});
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

    // ---- whole-note reviews (the plugin's #review notes, docs/SYNTAX.md §15 "Notes to review") ----

    // The plugin's `osrSchedule` (its tests/unit/scheduling/scheduling.test.ts, ported): the three
    // grades from a new note, with a two-day delay, load balancing over each range of intervals;
    // then what is ours — the one decimal without load balancing, the ease's floor, the interval's
    // cap.
    void noteSchedule_data()
    {
        QTest::addColumn<QString>("grade");
        QTest::addColumn<double>("interval");
        QTest::addColumn<double>("ease");
        QTest::addColumn<double>("late");
        QTest::addColumn<QVariantMap>("histogram"); // days from today → notes; none: no load balancing's data
        QTest::addColumn<QVariantMap>("more");
        QTest::addColumn<double>("wantInterval");
        QTest::addColumn<double>("wantEase");
        const QVariantMap none{};
        const QVariantMap empty{{u"histogram"_s, QVariantMap{}}};
        QTest::newRow("new, easy") << u"easy"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << empty << 4.0 << 270.0;
        QTest::newRow("new, good") << u"good"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << empty << 3.0 << 250.0;
        QTest::newRow("new, hard") << u"hard"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << empty << 1.0 << 230.0;
        QTest::newRow("late, easy") << u"easy"_s << 10.0 << 250.0 << 2.0 << QVariantMap{} << empty << 42.0 << 270.0;
        QTest::newRow("late, good") << u"good"_s << 10.0 << 250.0 << 2.0 << QVariantMap{} << empty << 28.0 << 250.0;
        QTest::newRow("late, hard") << u"hard"_s << 10.0 << 250.0 << 2.0 << QVariantMap{} << empty << 5.0 << 230.0;
        QTest::newRow("no fuzz up to a week") << u"good"_s << 2.0 << 250.0 << 0.0 << QVariantMap{{u"5"_s, 2}} << none << 5.0
                                              << 250.0;
        QTest::newRow("a day each side up to three weeks")
                << u"good"_s << 7.0 << 250.0 << 0.0 << QVariantMap{{u"17"_s, 4}, {u"18"_s, 5}, {u"19"_s, 3}} << none << 19.0 << 250.0;
        QTest::newRow("an unused day wins at once") << u"good"_s << 10.0 << 250.0 << 0.0
                                                    << QVariantMap{{u"23"_s, 5}, {u"26"_s, 1}} << none << 25.0 << 250.0;
        QTest::newRow("three days each side up to half a year")
                << u"good"_s << 25.0 << 250.0 << 0.0
                << QVariantMap{{u"2"_s, 5}, {u"59"_s, 8}, {u"60"_s, 9}, {u"61"_s, 3}, {u"62"_s, 5}, {u"63"_s, 4}, {u"64"_s, 4},
                               {u"65"_s, 8}, {u"66"_s, 2}, {u"67"_s, 10}}
                << none << 66.0 << 250.0;
        QTest::newRow("a week each side beyond")
                << u"good"_s << 500.0 << 250.0 << 0.0
                << QVariantMap{{u"1245"_s, 7}, {u"1246"_s, 4}, {u"1247"_s, 2}, {u"1248"_s, 9}, {u"1249"_s, 5}, {u"1250"_s, 4},
                               {u"1251"_s, 1}, {u"1252"_s, 1}, {u"1254"_s, 1}}
                << none << 1253.0 << 250.0;
        // Ours: no load balancing keeps one decimal (a Good is 2.5, an Easy 3.51 → 3.5) ...
        const QVariantMap off{{u"loadBalance"_s, false}};
        QTest::newRow("off, good") << u"good"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << off << 2.5 << 250.0;
        QTest::newRow("off, easy") << u"easy"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << off << 3.5 << 270.0;
        QTest::newRow("off, hard keeps one day") << u"hard"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << off << 1.0 << 230.0;
        // ... the ease never goes below 130, an interval below a day counts as one, the cap holds,
        // the hard factor is a setting.
        QTest::newRow("ease floor") << u"hard"_s << 5.0 << 140.0 << 0.0 << QVariantMap{} << empty << 3.0 << 130.0;
        QTest::newRow("interval below a day") << u"good"_s << 0.0 << 250.0 << 0.0 << QVariantMap{} << empty << 3.0 << 250.0;
        QTest::newRow("cap") << u"easy"_s << 30000.0 << 250.0 << 0.0 << QVariantMap{} << empty << 36525.0 << 270.0;
        QTest::newRow("a hard factor") << u"hard"_s << 10.0 << 250.0 << 0.0 << QVariantMap{} << QVariantMap{{u"lapsesIntervalChange"_s, 0.8}, {u"histogram"_s, QVariantMap{}}}
                                       << 8.0 << 230.0;
        // The algorithm from the notebook's config (.brulion/config.json "flashcards", as the app and
        // brulion-cli hand it over): a larger easy bonus, no load balancing.
        FlashcardsConfig notebook;
        notebook.easyBonus = 2;
        notebook.loadBalance = false;
        QTest::newRow("the notebook's easy bonus") << u"easy"_s << 1.0 << 250.0 << 0.0 << QVariantMap{}
                                                   << flashcardsSettings(notebook) << 5.4 << 270.0;
        notebook = FlashcardsConfig{};
        notebook.maxNoteInterval = 30;
        QTest::newRow("the notebook's cap") << u"easy"_s << 300.0 << 250.0 << 0.0 << QVariantMap{}
                                            << flashcardsSettings(notebook) << 30.0 << 270.0;
        QTest::newRow("grades by number") << u"3"_s << 10.0 << 250.0 << 0.0 << QVariantMap{} << empty << 25.0 << 250.0;
        QTest::newRow("grades in Polish") << u"łatwe"_s << 1.0 << 250.0 << 0.0 << QVariantMap{} << empty << 4.0 << 270.0;
    }
    void noteSchedule()
    {
        QFETCH(QString, grade);
        QFETCH(double, interval);
        QFETCH(double, ease);
        QFETCH(double, late);
        QFETCH(QVariantMap, histogram);
        QFETCH(QVariantMap, more);
        QFETCH(double, wantInterval);
        QFETCH(double, wantEase);
        QVERIFY(open({}));
        QVariantMap args = more;
        args.insert(u"grade"_s, grade);
        args.insert(u"interval"_s, interval);
        args.insert(u"ease"_s, ease);
        args.insert(u"late"_s, late);
        if (!histogram.isEmpty())
            args.insert(u"histogram"_s, histogram);
        const QVariantMap r = command(u"schedule-note"_s, args);
        QVERIFY2(r.value(u"error"_s).toString().isEmpty(), qPrintable(r.value(u"message"_s).toString()));
        QCOMPARE(r.value(u"interval"_s).toDouble(), wantInterval);
        QCOMPARE(r.value(u"ease"_s).toDouble(), wantEase);
    }

    void noteBadGrade()
    {
        QVERIFY(open({{u"Notatka.md"_s, u"#review\n"_s}}));
        for (const QString &grade : {u"again"_s, u"1"_s, u"5"_s, u"wcale"_s, QString()}) {
            QCOMPARE(command(u"schedule-note"_s, {{u"grade"_s, grade}}).value(u"error"_s).toString(), u"bad_argument"_s);
            QCOMPARE(command(u"review-note"_s, {{u"page"_s, u"Notatka"_s}, {u"grade"_s, grade}}).value(u"error"_s).toString(),
                     u"bad_argument"_s);
        }
        QCOMPARE(onDisk(u"Notatka.md"_s), u"#review\n"_s);
        QCOMPARE(command(u"review-note"_s, {{u"page"_s, u"Nie ma"_s}, {u"grade"_s, u"good"_s}}).value(u"error"_s).toString(),
                 u"note_not_found"_s);
        QVERIFY(open({{u"Zwykła.md"_s, u"Bez tagu.\n"_s}}));
        QCOMPARE(command(u"review-note"_s, {{u"page"_s, u"Zwykła"_s}, {u"grade"_s, u"good"_s}}).value(u"error"_s).toString(),
                 u"not_review_note"_s);
        QCOMPARE(onDisk(u"Zwykła.md"_s), u"Bez tagu.\n"_s);
    }

    // A note reviewed on 2026-11-20: its frontmatter written in the plugin's keys with every other
    // byte kept — other keys, quoted values, comments, the order of the three, line endings, a BOM,
    // no final line break — made when missing, malformed state a new note's.
    void noteState_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("grade");
        QTest::addColumn<QString>("after");
        QTest::newRow("no frontmatter: made")
                << u"#review\n\nZażółć gęślą jaźń\n"_s << u"good"_s
                << u"---\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\n#review\n\nZażółć gęślą jaźń\n"_s;
        QTest::newRow("CRLF") << u"#review\r\n\r\nŁąka\r\n"_s << u"good"_s
                              << u"---\r\nsr-due: 2026-11-23\r\nsr-interval: 3\r\nsr-ease: 250\r\n---\r\n#review\r\n\r\nŁąka\r\n"_s;
        QTest::newRow("a BOM stays first") << u"﻿#review\nTreść\n"_s << u"good"_s
                                           << u"﻿---\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\n#review\nTreść\n"_s;
        QTest::newRow("a rule is no frontmatter") << u"#review\n---\nTreść\n"_s << u"easy"_s
                                                  << u"---\nsr-due: 2026-11-24\nsr-interval: 4\nsr-ease: 270\n---\n#review\n---\nTreść\n"_s;
        QTest::newRow("other keys stay; the state goes last")
                << u"---\ntags: [review, nauka]\nautor: Żółć\n---\nTreść\n"_s << u"good"_s
                << u"---\ntags: [review, nauka]\nautor: Żółć\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\nTreść\n"_s;
        QTest::newRow("an empty frontmatter") << u"---\n---\n#review\n"_s << u"hard"_s
                                              << u"---\nsr-due: 2026-11-21\nsr-interval: 1\nsr-ease: 230\n---\n#review\n"_s;
        QTest::newRow("closed by dots") << u"---\ntags: [review]\n...\nTreść\n"_s << u"good"_s
                                        << u"---\ntags: [review]\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n...\nTreść\n"_s;
        QTest::newRow("no final line break") << u"---\ntags: [review]\n---\nTreść"_s << u"good"_s
                                             << u"---\ntags: [review]\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\nTreść"_s;
        QTest::newRow("a partial state is new; the keys go after the first")
                << u"---\nsr-due: 2026-11-01\ntags: [review]\n---\nTreść\n"_s << u"good"_s
                << u"---\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\ntags: [review]\n---\nTreść\n"_s;
        QTest::newRow("quoted values and a comment keep their style")
                << u"---\ntags: [review]\nsr-due: \"2026-11-20\"\nsr-interval: 3\nsr-ease: 250 # moja\n---\nTreść\n"_s << u"easy"_s
                << u"---\ntags: [review]\nsr-due: \"2026-12-01\"\nsr-interval: 11\nsr-ease: 270 # moja\n---\nTreść\n"_s;
        QTest::newRow("the plugin's other date format is read, the preferred one written")
                << u"---\ntags: [review]\nsr-due: 20-11-2026\nsr-interval: 3\nsr-ease: 250\n---\n"_s << u"good"_s
                << u"---\ntags: [review]\nsr-due: 2026-11-28\nsr-interval: 8\nsr-ease: 250\n---\n"_s;
        QTest::newRow("JavaScript's date format") << u"---\ntags: [review]\nsr-due: Fri Nov 20 2026\nsr-interval: 3\nsr-ease: 250\n---\n"_s
                                                  << u"good"_s
                                                  << u"---\ntags: [review]\nsr-due: 2026-11-28\nsr-interval: 8\nsr-ease: 250\n---\n"_s;
        QTest::newRow("a malformed date: a new note")
                << u"---\ntags: [review]\nsr-due: jutro\nsr-interval: 30\nsr-ease: 300\n---\n"_s << u"good"_s
                << u"---\ntags: [review]\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\n"_s;
        QTest::newRow("an impossible day: a new note")
                << u"---\ntags: [review]\nsr-due: 2026-02-30\nsr-interval: 30\nsr-ease: 300\n---\n"_s << u"good"_s
                << u"---\ntags: [review]\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\n"_s;
        QTest::newRow("an interval of 0: a new note")
                << u"---\ntags: [review]\nsr-due: 2026-11-10\nsr-interval: 0\nsr-ease: 300\n---\n"_s << u"good"_s
                << u"---\ntags: [review]\nsr-due: 2026-11-23\nsr-interval: 3\nsr-ease: 250\n---\n"_s;
        QTest::newRow("overdue and hard: the keys keep their order and place")
                << u"---\nsr-ease: 250\nsr-interval: 10\nsr-due: 2026-11-10\ntags: [review]\n---\n"_s << u"hard"_s
                << u"---\nsr-ease: 230\nsr-interval: 6\nsr-due: 2026-11-26\ntags: [review]\n---\n"_s;
        QTest::newRow("the interval as the plugin read it: a number with a unit")
                << u"---\ntags: [review]\nsr-due: 2026-11-18\nsr-interval: 4 dni\nsr-ease: 260\n---\n"_s << u"good"_s
                << u"---\ntags: [review]\nsr-due: 2026-12-03\nsr-interval: 13\nsr-ease: 260\n---\n"_s;
    }
    void noteState()
    {
        QFETCH(QString, before);
        QFETCH(QString, grade);
        QFETCH(QString, after);
        QVERIFY(open({{u"Notatka.md"_s, before}}));
        QCOMPARE(command(u"notes"_s).value(u"counts"_s).toMap().value(u"total"_s).toInt(), 1);
        const QVariantMap r = command(u"review-note"_s, {{u"page"_s, u"Notatka"_s}, {u"grade"_s, grade}});
        QVERIFY2(r.value(u"error"_s).toString().isEmpty(), qPrintable(r.value(u"message"_s).toString()));
        QCOMPARE(onDisk(u"Notatka.md"_s), after);
        // Read back: the state as written, due on its day.
        const QVariantMap note = command(u"notes"_s).value(u"notes"_s).toList().value(0).toMap();
        QCOMPARE(note.value(u"due"_s).toString(), r.value(u"after"_s).toMap().value(u"due"_s).toString());
        QCOMPARE(note.value(u"interval"_s).toDouble(), r.value(u"after"_s).toMap().value(u"interval"_s).toDouble());
    }

    // Which notes, in which order: a tag (also a sub-tag, a block's, any letter case, a tag of one's
    // own), due notes by their day — the earliest first —, then new ones by path; counts; the
    // next grades' intervals (load balanced over the notes' due days); the due and new counts
    // next to the cards'.
    void noteQueue()
    {
        const auto note = [](const QString &tag, const QString &state) {
            return u"---\ntags: [%1]\n%2---\nTreść\n"_s.arg(tag, state);
        };
        const auto state = [](const QString &due) { return u"sr-due: %1\nsr-interval: 3\nsr-ease: 250\n"_s.arg(due); };
        QVERIFY(open({{u"A.md"_s, note(u"review"_s, state(u"2026-11-18"_s))},
                      {u"B.md"_s, note(u"Review"_s, state(u"2026-11-20"_s))},
                      {u"C.md"_s, note(u"review"_s, state(u"2026-11-21"_s))},
                      {u"D.md"_s, u"Nowa notatka #review\n"_s},
                      {u"Nauka/E.md"_s, u"#review/anatomia\n\nŚledziona\n"_s},
                      {u"F.md"_s, note(u"nauka"_s, state(u"2026-11-01"_s))},
                      {u"Inna/G.md"_s, note(u"review"_s, state(u"2026-11-10"_s))},
                      {u"Fiszki.md"_s, u"#fiszki\n\nA::B\n"_s}}));
        const QVariantMap r = command(u"notes"_s);
        QStringList order;
        for (const QVariant &n : r.value(u"notes"_s).toList())
            order << n.toMap().value(u"path"_s).toString() + u' ' + n.toMap().value(u"status"_s).toString();
        QCOMPARE(order, (QStringList{u"A.md due"_s, u"B.md due"_s, u"C.md scheduled"_s, u"D.md new"_s, u"Inna/G.md due"_s,
                                     u"Nauka/E.md new"_s}));
        const QVariantMap counts = r.value(u"counts"_s).toMap();
        QCOMPARE(counts.value(u"due"_s).toInt(), 3);
        QCOMPARE(counts.value(u"new"_s).toInt(), 2);
        QCOMPARE(counts.value(u"scheduled"_s).toInt(), 4);
        QCOMPARE(counts.value(u"total"_s).toInt(), 6);
        QCOMPARE(r.value(u"tags"_s).toStringList(), QStringList{u"review"_s});
        // To review now: due ones by day (G, A, B), then the new.
        QStringList now;
        for (const QVariant &n : command(u"notes"_s, {{u"due"_s, true}}).value(u"notes"_s).toList())
            now << n.toMap().value(u"path"_s).toString();
        QCOMPARE(now, (QStringList{u"Inna/G.md"_s, u"A.md"_s, u"B.md"_s, u"D.md"_s, u"Nauka/E.md"_s}));
        // What each grade would schedule: A is two days late.
        const QVariantMap a = r.value(u"notes"_s).toList().value(0).toMap().value(u"next"_s).toMap();
        QCOMPARE(a.value(u"good"_s).toMap().value(u"interval"_s).toDouble(), 10.0);
        QCOMPARE(a.value(u"good"_s).toMap().value(u"due"_s).toString(), u"2026-11-30"_s);
        QCOMPARE(a.value(u"hard"_s).toMap().value(u"interval"_s).toDouble(), 2.0);
        QCOMPARE(a.value(u"easy"_s).toMap().value(u"interval"_s).toDouble(), 18.0);
        // The counts beside the cards'.
        const QVariantMap n = command(u"count"_s);
        QCOMPARE(n.value(u"notesDue"_s).toInt(), 3);
        QCOMPARE(n.value(u"notesNew"_s).toInt(), 2);
        QCOMPARE(n.value(u"notesTotal"_s).toInt(), 6);
        QCOMPARE(n.value(u"total"_s).toInt(), 1); // the one card
        // The decks' entry: the note's counts, once.
        QList<QVariantMap> entries;
        for (const QVariantMap &node : nodes(render(u"decks"_s)))
            if (node.value(u"type"_s) == u"open"_s && node.value(u"view"_s) == u"notes"_s)
                entries << node;
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].value(u"label"_s).toString(), u"Notatki do powtórki"_s);
        bool counted = false;
        for (const QVariantMap &node : nodes(render(u"decks"_s)))
            counted = counted || node.value(u"text"_s).toString() == u"3 do powtórki · 2 nowych · 6 wszystkich"_s;
        QVERIFY(counted);
        // A tag of one's own (the setting): only those notes.
        QVERIFY(open({{u"X.md"_s, u"#powtórka\n"_s}, {u"Y.md"_s, u"#review\n"_s}}));
        QCOMPARE(command(u"notes"_s, {{u"reviewTags"_s, u"#Powtórka, nauka"_s}}).value(u"counts"_s).toMap().value(u"total"_s).toInt(), 1);
        QCOMPARE(command(u"notes"_s, {{u"reviewTags"_s, QStringList{u"powtórka"_s, u"review"_s}}}).value(u"counts"_s).toMap().value(u"total"_s).toInt(), 2);
        QCOMPARE(command(u"notes"_s).value(u"counts"_s).toMap().value(u"total"_s).toInt(), 1);
        // Notes without cards: the decks show the entry alone; without either, the empty state.
        QCOMPARE(firstOfType(render(u"decks"_s), u"open"_s, QString()).value(u"view"_s).toString(), u"notes"_s);
        QVERIFY(open({}));
        QCOMPARE(firstOfType(render(u"decks"_s), u"empty"_s).value(u"icon"_s).toString(), u"layers"_s);
        QCOMPARE(firstOfType(render(u"notes"_s), u"empty"_s).value(u"title"_s).toString(), u"Nie ma notatek do powtórki"_s);
    }

    // The review view: the note as Markdown (without its frontmatter), the plugin's three grades
    // with the days they schedule, Skip, Undo (the note as it was, byte for byte), a note changed
    // meanwhile refused, a long note in chunks.
    void noteSession()
    {
        const QString first = u"---\ntags: [review]\nsr-due: 2026-11-19\nsr-interval: 4\nsr-ease: 260\n---\n# Notatka\n\nTreść ąę\n"_s;
        const QString second = u"#review\n\nDruga\n"_s;
        QVERIFY(open({{u"Notatka.md"_s, first}, {u"Nowa.md"_s, second}}));
        const auto gradeButtons = [](const QList<QVariantMap> &all) {
            QList<QVariantMap> out;
            for (const QVariantMap &n : all)
                if (n.value(u"action"_s) == u"grade"_s)
                    out << n;
            return out;
        };
        QVariantMap view = render(u"notes"_s);
        QStringList texts;
        for (const QVariantMap &n : nodes(view))
            if (n.value(u"type"_s) == u"markdown"_s)
                texts << n.value(u"text"_s).toString();
        QCOMPARE(texts, QStringList{u"# Notatka\n\nTreść ąę"_s});
        QList<QVariantMap> grades = gradeButtons(nodes(view));
        QCOMPARE(grades.size(), 3);
        QCOMPARE(grades[0].value(u"hint"_s).toString(), u"2 dni"_s);
        QCOMPARE(grades[1].value(u"hint"_s).toString(), u"12 dni"_s);
        QCOMPARE(grades[2].value(u"hint"_s).toString(), u"18 dni"_s);
        QCOMPARE(grades[1].value(u"key"_s).toString(), u"2"_s);
        QCOMPARE(firstOfType(view, u"button"_s, u"skip"_s).value(u"key"_s).toString(), u"s"_s);
        // Good: written, the next note shown (a new one: 1, 3 and 4 days).
        QVERIFY(act2(u"grade"_s, grades[1].value(u"args"_s).toMap()).ok());
        QCOMPARE(onDisk(u"Notatka.md"_s), QString(first).replace(u"2026-11-19"_s, u"2026-12-02"_s).replace(u"sr-interval: 4"_s, u"sr-interval: 12"_s));
        view = render(u"notes"_s);
        QCOMPARE(firstOfType(view, u"markdown"_s).value(u"text"_s).toString(), u"#review\n\nDruga"_s);
        grades = gradeButtons(nodes(view));
        QCOMPARE(grades[0].value(u"hint"_s).toString(), u"1 dzień"_s);
        QCOMPARE(grades[1].value(u"hint"_s).toString(), u"3 dni"_s);
        QCOMPARE(grades[2].value(u"hint"_s).toString(), u"4 dni"_s);
        // Skip it: nothing left today; the skipped can be shown again.
        QVERIFY(act2(u"skip"_s, firstOfType(view, u"button"_s, u"skip"_s).value(u"args"_s).toMap()).ok());
        view = render(u"notes"_s);
        QCOMPARE(firstOfType(view, u"empty"_s).value(u"title"_s).toString(), u"Notatki na dziś przejrzane"_s);
        QVERIFY(act2(u"unskip"_s, {}).ok());
        view = render(u"notes"_s);
        QCOMPARE(firstOfType(view, u"markdown"_s).value(u"text"_s).toString(), u"#review\n\nDruga"_s);
        // Undo the first grade (a button of the done view too): the note as it was.
        QVERIFY(act2(u"undo"_s, {}).ok());
        QCOMPARE(onDisk(u"Notatka.md"_s), first);
        view = render(u"notes"_s);
        QCOMPARE(firstOfType(view, u"markdown"_s).value(u"text"_s).toString(), u"# Notatka\n\nTreść ąę"_s);
        // Grade a new note: its frontmatter is made; undone, the note is as it was again.
        QVERIFY(act2(u"skip"_s, firstOfType(view, u"button"_s, u"skip"_s).value(u"args"_s).toMap()).ok());
        view = render(u"notes"_s);
        QVERIFY(act2(u"grade"_s, gradeButtons(nodes(view))[2].value(u"args"_s).toMap()).ok());
        QVERIFY(onDisk(u"Nowa.md"_s).startsWith(u"---\nsr-due: 2026-11-24\nsr-interval: 4\nsr-ease: 270\n---\n#review\n"_s));
        QVERIFY(act2(u"undo"_s, {}).ok());
        QCOMPARE(onDisk(u"Nowa.md"_s), second);
        // The header changed meanwhile (another device): the grade is refused, nothing written.
        QVERIFY(act2(u"unskip"_s, {}).ok());
        view = render(u"notes"_s);
        const QVariantMap stale = gradeButtons(nodes(view))[0].value(u"args"_s).toMap();
        const QString edited = QString(first).replace(u"sr-ease: 260"_s, u"sr-ease: 300"_s);
        QVERIFY(writeFile(u"Notatka.md"_s, edited));
        m_index->update(u"Notatka.md"_s, edited);
        const Result r = act2(u"grade"_s, stale);
        QVERIFY(r.ok());
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"warning"_s);
        QCOMPARE(onDisk(u"Notatka.md"_s), edited);
    }

    // A long note is drawn in chunks cut at blank lines (never inside a code fence), the rest of a
    // very long one is left to the editor.
    void noteChunks()
    {
        QString body;
        for (int i = 0; i < 400; ++i)
            body += u"Akapit numer %1 ma trochę tekstu, żeby zająć miejsce w notatce.\n\n"_s.arg(i);
        QVERIFY(open({{u"Długa.md"_s, u"#review\n\n"_s + body}}));
        int chunks = 0;
        int size = 0;
        for (const QVariantMap &n : nodes(render(u"notes"_s)))
            if (n.value(u"type"_s) == u"markdown"_s) {
                ++chunks;
                size += n.value(u"text"_s).toString().size();
                QVERIFY(n.value(u"text"_s).toString().size() < 3300);
            }
        QVERIFY2(chunks > 7, qPrintable(QString::number(chunks)));
        QVERIFY(size > body.size() * 9 / 10);
        // A fence with blank lines inside stays whole.
        QString fenced = u"#review\n\n```\n"_s;
        for (int i = 0; i < 400; ++i)
            fenced += u"kod %1\n\n"_s.arg(i);
        fenced += u"```\n\nKoniec\n"_s;
        QVERIFY(open({{u"Kod.md"_s, fenced}}));
        QStringList texts;
        for (const QVariantMap &n : nodes(render(u"notes"_s)))
            if (n.value(u"type"_s) == u"markdown"_s)
                texts << n.value(u"text"_s).toString();
        QCOMPARE(texts.size(), 2);
        QVERIFY(texts[0].startsWith(u"#review\n\n```\nkod 0"_s) && texts[0].trimmed().endsWith(u"```"_s));
        QCOMPARE(texts[1], u"Koniec"_s);
        // Very long: cut, with a line saying so.
        QString huge = u"#review\n\n"_s;
        for (int i = 0; i < 2500; ++i)
            huge += u"Akapit %1 — %2\n\n"_s.arg(i).arg(QString(60, u'x'));
        QVERIFY(open({{u"Ogromna.md"_s, huge}}));
        bool cut = false;
        for (const QVariantMap &n : nodes(render(u"notes"_s)))
            cut = cut || n.value(u"text"_s).toString() == u"Dalsza część notatki jest w edytorze."_s;
        QVERIFY(cut);
    }

    // Many cards: counted and drawn within the time limit (the decks of 5000 cards on 100 pages),
    // drawn again after another note changed (the deck pages not parsed again).
    void manyCards()
    {
        QHash<QString, QString> files;
        for (int p = 0; p < 100; ++p) {
            QString note = u"#fiszki/talia%1\n\n"_s.arg(p % 8);
            for (int i = 0; i < 50; ++i)
                note += i % 3 == 0 ? u"pytanie %1 %2::odpowiedź <!--SR:!2026-11-%3,4,250-->\n"_s.arg(p).arg(i).arg(10 + i % 15)
                                   : u"pytanie %1 %2::odpowiedź\n"_s.arg(p).arg(i);
            files.insert(u"Talie/%1.md"_s.arg(p), note);
        }
        QVERIFY(open(files));
        QElapsedTimer timer;
        timer.start();
        const QVariantMap n = command(u"count"_s);
        const qint64 first = timer.elapsed();
        QCOMPARE(n.value(u"total"_s).toInt(), 5000);
        QVERIFY(!firstOfType(render(u"decks"_s), u"stats"_s).isEmpty());
        QVERIFY(!firstOfType(render(u"review"_s), u"markdown"_s).isEmpty());
        QVERIFY(writeFile(u"Inna.md"_s, u"- zmiana\n"_s));
        m_index->update(u"Inna.md"_s, u"- zmiana\n"_s);
        timer.restart();
        QCOMPARE(command(u"count"_s).value(u"total"_s).toInt(), 5000);
        const qint64 again = timer.elapsed();
        QVERIFY(!firstOfType(render(u"decks"_s), u"stats"_s).isEmpty());
        qInfo("5000 cards counted in %lld ms, again after another note changed in %lld ms", first, again);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsFlashcards)
#include "tst_extensions_flashcards.moc"
