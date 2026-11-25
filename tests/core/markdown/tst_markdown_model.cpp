// The parsed model's compact types (types.h, docs/PERFORMANCE.md L8): Boxed (Block::task),
// BlockIdMap (ParsedNote::blockIds), Properties, BlockSource (Block::raw), TaskList (ParsedNote::tasks) — and the golden digest
// of parseNote()'s output over a frozen corpus (tests/common/notecorpus.h), which proves that the
// memory diet changed no value and later guards markdown::ParserVersion (the index cache).
#include "markdown/parse.h"

#include "notecorpus.h"
#include "notedump.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <atomic>
#include <thread>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

/// dumpDigest() of the frozen corpus parsed with the default config. Recorded on the tree before
/// the memory diet (sweep2 82d0357); it changes only when parseNote() returns something
/// different for some input — then bump markdown::ParserVersion and record the new digest.
constexpr const char *kParseDigest = "05109a55abc5e6c28a1bf1008ee82769";

QList<ParsedNote> parseCorpus()
{
    QList<ParsedNote> notes;
    for (const auto &[path, content] : testing::noteCorpus())
        notes.append(markdown::parseNote(path, content));
    return notes;
}

Task sampleTask(const QString &text)
{
    Task t;
    t.status = TaskStatus::Doing;
    t.marker = u"/"_s;
    t.text = text;
    t.line = 3;
    t.due = DateSpec{QDate(2026, 10, 20), QTime(8, 30)};
    t.recurrence = u"every week"_s;
    t.priority = Priority::High;
    return t;
}

} // namespace

class TestMarkdownModel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- golden digest ----------------------------------------------------------------------

    void modelDigest()
    {
        const QList<ParsedNote> notes = parseCorpus();
        QVERIFY(notes.size() > 80);
        const QByteArray digest = testing::dumpDigest(notes);
        QVERIFY2(digest == kParseDigest,
                 qPrintable(u"parseNote() output changed (digest %1): bump markdown::ParserVersion and update "
                            u"kParseDigest"_s.arg(QString::fromLatin1(digest))));
    }

    // The QString overload (slices share the caller's text) and the QStringView one parse alike.
    void stringAndViewParsesAgree()
    {
        for (const auto &[path, content] : testing::noteCorpus()) {
            const ParsedNote a = markdown::parseNote(path, content);
            const ParsedNote b = markdown::parseNote(path, QStringView(content));
            QCOMPARE(testing::dumpNote(a), testing::dumpNote(b));
        }
    }

    // ---- BlockSource (Block::raw) -------------------------------------------------------------

    void blockSourceSlices_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("block");
        QTest::addColumn<QString>("raw");
        QTest::addColumn<bool>("slice");
        QTest::newRow("LF lines") << u"- pierwszy\n  ciąg dalszy\n- drugi\n"_s << 0 << u"- pierwszy\n  ciąg dalszy"_s
                                  << true;
        QTest::newRow("last line without LF") << u"- a\n- żółw"_s << 1 << u"- żółw"_s << true;
        QTest::newRow("CRLF") << u"- pierwszy\r\n  ciąg\r\n"_s << 0 << u"- pierwszy\n  ciąg"_s << false;
        QTest::newRow("CRLF one line") << u"- jeden\r\n- dwa\r\n"_s << 0 << u"- jeden"_s << true;
        QTest::newRow("lone CR") << u"Akapit\rdalej\r"_s << 0 << u"Akapit\ndalej"_s << false;
        QTest::newRow("non-consecutive own lines") << u"- rodzic\n  - dziecko\n  ciąg dalszy rodzica\n"_s << 0
                                                   << u"- rodzic\n  ciąg dalszy rodzica"_s << false;
        QTest::newRow("quote") << u"> cytat\n> dalej\n"_s << 0 << u"> cytat\n> dalej"_s << true;
        QTest::newRow("callout") << u"> [!note] Uwaga\n> treść\n"_s << 1 << u"> treść"_s << true;
        QTest::newRow("BOM first line") << u"﻿# Tytuł\nAkapit\n"_s << 0 << u"﻿# Tytuł"_s << true;
        QTest::newRow("code fence") << u"```\nkod\n\nkod\n```\n"_s << 0 << u"```\nkod\n\nkod\n```"_s << true;
        QTest::newRow("frontmatter") << u"---\ntytuł: Łódź\n---\ntekst\n"_s << 0 << u"---\ntytuł: Łódź\n---"_s << true;
    }
    void blockSourceSlices()
    {
        QFETCH(QString, content);
        QFETCH(int, block);
        QFETCH(QString, raw);
        QFETCH(bool, slice);
        const ParsedNote note = markdown::parseNote(u"Notatka.md"_s, content);
        QVERIFY(block < note.blocks.size());
        const BlockSource &source = note.blocks[block].raw;
        QCOMPARE(source.toString(), raw);
        QCOMPARE(source.view(), QStringView(raw));
        QCOMPARE(source.size(), raw.size());
        QCOMPARE(source.isSlice(), slice);
        if (slice) {
            // The slice shares the caller's text: no copy of the note.
            QCOMPARE(source.buffer().constData(), content.constData());
            QCOMPARE(QStringView(content).sliced(source.from(), source.to() - source.from()), QStringView(raw));
        }
        // Parsed from a view: valid slices of the parser's own copy.
        const ParsedNote fromView = markdown::parseNote(u"Notatka.md"_s, QStringView(content));
        QCOMPARE(fromView.blocks[block].raw.toString(), raw);
        QVERIFY(fromView.blocks[block].raw.buffer().constData() != content.constData() || !slice);
        // Every block of every corpus note: raw is its own lines joined with '\n'.
        QVERIFY(source == QStringView(raw));
        QVERIFY(source == BlockSource(raw));
    }

    void blockSourceCopies()
    {
        BlockSource empty;
        QVERIFY(empty.isEmpty());
        QVERIFY(!empty.isSlice());
        QCOMPARE(empty.toString(), QString());
        const QString doc = u"- jeden\n- dwa\n"_s;
        const BlockSource slice(doc, 8, 13);
        QCOMPARE(slice.toString(), u"- dwa"_s);
        QVERIFY(slice.startsWith(u"- d"));
        const BlockSource copy = slice;
        QCOMPARE(copy.buffer().constData(), doc.constData());
        QVERIFY(copy == slice);
        const BlockSource own(u"- dwa"_s);
        QVERIFY(own == slice);
        QVERIFY(!own.isSlice());
        QCOMPARE(own.from(), 0);
        QCOMPARE(own.to(), 5);
    }

    // ---- Boxed<Task> --------------------------------------------------------------------------

    void boxedTask()
    {
        Boxed<Task> empty;
        QVERIFY(!empty);
        QVERIFY(!empty.has_value());
        QVERIFY(!std::optional<Task>(empty).has_value());

        Boxed<Task> a = sampleTask(u"Zażółć gęślą jaźń"_s);
        QVERIFY(a.has_value());
        QCOMPARE(a->text, u"Zażółć gęślą jaźń"_s);

        // A copy shares until one side changes it.
        Boxed<Task> b = a;
        QVERIFY(b.isSharedWith(a));
        const Boxed<Task> &constB = b;
        QCOMPARE(constB->priority, Priority::High); // const access never detaches
        QVERIFY(b.isSharedWith(a));
        b->text = u"Łódź"_s;
        QVERIFY(!b.isSharedWith(a));
        QCOMPARE(a->text, u"Zażółć gęślą jaźń"_s);
        QCOMPARE(b->text, u"Łódź"_s);

        // std::optional both ways.
        const std::optional<Task> opt = a;
        QVERIFY(opt.has_value());
        QCOMPARE(opt->text, a->text);
        QCOMPARE(opt->due->time, std::optional<QTime>(QTime(8, 30)));
        Boxed<Task> fromOpt = opt;
        QCOMPARE(fromOpt->recurrence, u"every week"_s);
        fromOpt = std::optional<Task>();
        QVERIFY(!fromOpt);
        fromOpt = std::nullopt;
        QVERIFY(!fromOpt);

        // reset / emplace / moves.
        Boxed<Task> c = a;
        c.reset();
        QVERIFY(!c);
        QVERIFY(a);
        Task &made = c.emplace(sampleTask(u"nowe"_s));
        made.line = 42;
        QCOMPARE(c->line, 42);
        Boxed<Task> moved = std::move(c);
        QVERIFY(moved);
        QVERIFY(!c); // NOLINT(bugprone-use-after-move): moved-from is empty
        Boxed<Task> assigned;
        assigned = moved;
        QVERIFY(assigned.isSharedWith(moved));
        assigned = sampleTask(u"przypisane"_s);
        QVERIFY(!assigned.isSharedWith(moved));
        QCOMPARE(moved->line, 42);

        // In a Block: copies of blocks share the task.
        Block block;
        block.task = sampleTask(u"w bloku"_s);
        const Block copy = block;
        QVERIFY(copy.task.isSharedWith(block.task));
        block.task->status = TaskStatus::Done;
        QCOMPARE(copy.task->status, TaskStatus::Doing);
    }

    // ---- TaskList (ParsedNote::tasks) --------------------------------------------------------------

    void taskListSharesTheBlocksTasks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("texts");
        QTest::addColumn<QList<bool>>("sameAsBlockText");
        QTest::newRow("none") << u"# Nagłówek\nAkapit\n"_s << QStringList() << QList<bool>();
        QTest::newRow("one line") << u"- [ ] Zapłacić czynsz\n"_s << QStringList{u"Zapłacić czynsz"_s}
                                  << QList<bool>{true};
        QTest::newRow("tokens") << u"- [x] Kupić farbę 📅 2026-11-25 ✅ 2026-11-24\n"_s
                                << QStringList{u"Kupić farbę"_s} << QList<bool>{true};
        QTest::newRow("Logseq marker") << u"- TODO żółw\n- DONE łódź\n"_s << QStringList{u"żółw"_s, u"łódź"_s}
                                       << QList<bool>{true, true};
        // A task's text is its first line; the block's holds every line.
        QTest::newRow("two lines") << u"- [ ] pierwsza\n  druga linia\n"_s << QStringList{u"pierwsza"_s}
                                   << QList<bool>{false};
        QTest::newRow("nested") << u"- [ ] rodzic\n  - [/] dziecko ⏳ 2026-12-01\n"_s
                                << QStringList{u"rodzic"_s, u"dziecko"_s} << QList<bool>{true, true};
        QTest::newRow("CRLF") << u"- [ ] ąć\r\n- [-] ęł\r\n"_s << QStringList{u"ąć"_s, u"ęł"_s}
                              << QList<bool>{true, true};
        QTest::newRow("empty task") << u"- [ ] \n"_s << QStringList{QString()} << QList<bool>{true};
    }
    void taskListSharesTheBlocksTasks()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, texts);
        QFETCH(QList<bool>, sameAsBlockText);
        const ParsedNote note = markdown::parseNote(u"Zadania.md"_s, content);
        QCOMPARE(note.tasks.size(), texts.size());
        QCOMPARE(note.tasks.isEmpty(), texts.isEmpty());
        qsizetype t = 0;
        for (const Block &b : note.blocks) {
            if (!b.task)
                continue;
            QVERIFY(t < note.tasks.size());
            QVERIFY(note.tasks.isSharedWith(t, b.task)); // the block's object, not a copy
            QCOMPARE(&note.tasks.at(t), &*b.task);
            QCOMPARE(note.tasks[t].text, texts.at(t));
            // A one-line task's text is its block's (shared, not a second string).
            QCOMPARE(b.task->text.isSharedWith(b.text), sameAsBlockText.at(t) && !b.text.isEmpty());
            ++t;
        }
        QCOMPARE(t, note.tasks.size());
        // It reads like a QList<Task>.
        QStringList seen;
        for (const Task &task : note.tasks)
            seen.append(task.text);
        QCOMPARE(seen, texts);
        QCOMPARE(note.tasks.toList().size(), texts.size());
        if (!texts.isEmpty()) {
            QCOMPARE(note.tasks.first().text, texts.first());
            QCOMPARE(note.tasks.last().text, texts.last());
            const auto found = std::ranges::find(note.tasks, note.tasks.last().line, &Task::line);
            QVERIFY(found != note.tasks.cend());
            QCOMPARE(found - note.tasks.cbegin(), note.tasks.size() - 1);
            QCOMPARE(found->text, texts.last());
            QVERIFY(std::ranges::all_of(note.tasks, [](const Task &x) { return x.line >= 0; }));
        }
        // A copy of the note shares the tasks too.
        const ParsedNote copy = note;
        for (qsizetype i = 0; i < copy.tasks.size(); ++i)
            QCOMPARE(&copy.tasks.at(i), &note.tasks.at(i));
    }

    void taskListOwnTasks()
    {
        TaskList list;
        QVERIFY(list.isEmpty());
        QCOMPARE(list.begin(), list.end());
        list.append(Boxed<Task>()); // a block without a task adds nothing
        QVERIFY(list.isEmpty());
        list.append(sampleTask(u"własne"_s));
        Boxed<Task> boxed = sampleTask(u"wspólne"_s);
        list.append(boxed);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).text, u"własne"_s);
        QVERIFY(list.isSharedWith(1, boxed));
        QVERIFY(!list.isSharedWith(0, boxed));
        auto it = list.begin();
        QCOMPARE(it->text, u"własne"_s);
        ++it;
        QCOMPARE((*it).text, u"wspólne"_s);
        QCOMPARE(list.end() - list.begin(), 2);
        QCOMPARE(list.begin()[1].text, u"wspólne"_s);
        QVERIFY(list.begin() < list.end());
        list.squeeze();
        list.clear();
        QVERIFY(list.isEmpty());
        static_assert(std::random_access_iterator<TaskList::const_iterator>);
        static_assert(std::ranges::random_access_range<const TaskList>);
    }

    // Copies made on other threads while the original is read: the reference count is atomic
    // (a TSan/ASan build would flag a race or a double free).
    void boxedTaskAcrossThreads()
    {
        const Boxed<Task> original = sampleTask(u"wspólne"_s);
        std::atomic<int> seen{0};
        auto work = [&] {
            for (int i = 0; i < 10000; ++i) {
                Boxed<Task> copy = original;
                if (copy->text.size() == 7)
                    seen.fetch_add(1, std::memory_order_relaxed);
                if (i % 3 == 0)
                    copy->line = i; // detaches: the original is untouched
            }
        };
        std::thread t1(work);
        std::thread t2(work);
        for (int i = 0; i < 10000; ++i)
            QCOMPARE(original->line, 3);
        t1.join();
        t2.join();
        QCOMPARE(seen.load(), 20000);
        QCOMPARE(original->line, 3);
    }
};

QTEST_GUILESS_MAIN(TestMarkdownModel)
#include "tst_markdown_model.moc"
