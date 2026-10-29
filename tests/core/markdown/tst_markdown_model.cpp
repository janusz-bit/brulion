// The parsed model's compact types (types.h, docs/PERFORMANCE.md L8): Boxed (Block::task),
// BlockIdMap (ParsedNote::blockIds), Properties, BlockSource (Block::raw) — and the golden digest
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
constexpr const char *kParseDigest = "35b66981cba24be59c0974a85b774894";

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
