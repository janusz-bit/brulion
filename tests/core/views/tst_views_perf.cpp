// Database views on a large notebook (docs/PERFORMANCE.md "Database views"): property filters,
// a board's groups and a base over 4000 notes stay within their budgets, and a QueryCache brings a
// property query up to date after one edit for a fraction of a full run. CPU time and medians
// (tests/common/testtiming.h): a loaded machine cannot fail it.
#include "index/querycache.h"
#include "views/bases.h"
#include "views/view.h"

#include "testtiming.h"

#include <QTest>

using namespace brulion;
using namespace brulion::views;
using namespace Qt::StringLiterals;

namespace {

const QDate Today(2026, 10, 14);
constexpr int Notes = 4000;

QString note(int i)
{
    static const QStringList statuses{u"planowany"_s, u"w toku"_s, u"zrobione"_s, u"wstrzymany"_s};
    return u"---\nstatus: %1\nkoszt: %2\ntermin: 2026-%3-%4\ntags: [%5]\nwłaściciel: \"[[Osoba %6]]\"\n---\n"
           u"# Notatka %7\n\n- [ ] Zadanie %7 📅 2026-10-%4\n  etap:: %8\n- Treść o żółwiach i #dom\n"_s
            .arg(statuses.at(i % 4))
            .arg(i * 7 % 1000)
            .arg(10 + i % 3, 2, 10, QChar(u'0'))
            .arg(1 + i % 28, 2, 10, QChar(u'0'))
            .arg(i % 5 == 0 ? u"projekt"_s : u"notatka"_s)
            .arg(i % 20)
            .arg(i)
            .arg(i % 2 ? u"zakupy"_s : u"montaż"_s);
}

qint64 cpuMs(const std::function<void()> &work, int runs = 3)
{
    QList<qint64> samples;
    for (int i = 0; i < runs; ++i) {
        test::CpuTimer t;
        t.start();
        work();
        samples.append(t.nsecsElapsed() / 1000000);
    }
    return test::medianOf(samples);
}

} // namespace

class TestViewsPerf : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

private Q_SLOTS:
    void initTestCase()
    {
        for (int i = 0; i < Notes; ++i)
            m_index.update(u"Notatki/%1.md"_s.arg(i, 5, 10, QChar(u'0')), note(i));
        for (int i = 0; i < 20; ++i)
            m_index.update(u"Ludzie/Osoba %1.md"_s.arg(i), u"Osoba."_s);
    }

    void propertyFiltersStayClose()
    {
        const Query plain = std::get<Query>(parseQuery(u"type:pages tag:projekt"_s));
        const Query props = std::get<Query>(parseQuery(u"type:pages koszt::>500 status::\"w toku\",planowany "
                                                       u"właściciel::[[Osoba 3]] sort:-koszt"_s));
        int total = 0;
        const qint64 plainMs = cpuMs([&] { total = runQuery(plain, m_index, Today).total; });
        QCOMPARE(total, Notes / 5);
        const qint64 propsMs = cpuMs([&] { total = runQuery(props, m_index, Today).total; });
        QVERIFY(total > 0);
        qInfo("tag filter %lld ms, three property filters and a property sort %lld ms (%d notes)", plainMs, propsMs,
              Notes);
        QVERIFY2(propsMs <= 4000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(propsMs)));
        QVERIFY2(propsMs <= std::max<qint64>(10, plainMs) * 12, qPrintable(u"%1 vs %2"_s.arg(propsMs).arg(plainMs)));
    }

    void cacheFollowsOneEdit()
    {
        VaultIndex index;
        for (int i = 0; i < Notes; ++i)
            index.update(u"Notatki/%1.md"_s.arg(i, 5, 10, QChar(u'0')), note(i));
        const Query q = std::get<Query>(parseQuery(u"type:tasks etap::zakupy koszt::<300 group:status::"_s));
        QueryCache cache;
        cache.run(q, index, Today);
        QList<qint64> edits;
        for (int round = 0; round < 5; ++round) {
            index.update(u"Notatki/00007.md"_s, note(7) + (round % 2 ? u"\nDopisek."_s : QString()));
            test::CpuTimer t;
            t.start();
            cache.run(q, index, Today);
            edits.append(t.nsecsElapsed());
        }
        const qint64 full = test::medianOf({cpuMs([&] { runQuery(q, index, Today); }, 3) * 1000000});
        const qint64 edit = test::medianOf(edits);
        qInfo("after one edit %lld µs, a full run %lld µs", edit / 1000, full / 1000);
        QCOMPARE(cache.result(index).total, runQuery(q, index, Today).total);
        QVERIFY2(edit <= full / 4 + 2000000, qPrintable(u"%1 vs %2"_s.arg(edit).arg(full)));
    }

    void boardAndBaseStayWithinBudget()
    {
        const Query board = std::get<Query>(parseQuery(u"type:pages view:board group:status sort:-koszt"_s));
        ViewOptions o;
        o.today = Today;
        o.now = QDateTime(Today, QTime(10, 0));
        int rows = 0;
        const qint64 boardMs = cpuMs([&] {
            const QueryResult r = runQuery(board, m_index, Today);
            rows = int(std::get<ViewResult>(buildQueryView(board, r, m_index, o)).rows.size());
        });
        QCOMPARE(rows, DefaultQueryLimit);
        const Base base = std::get<Base>(parseBase(
                u"filters:\n  and:\n    - file.hasTag(\"projekt\")\n    - 'status != \"zrobione\"'\nformulas:\n"
                u"  brutto: \"koszt * 1.23\"\nviews:\n  - type: table\n    order: [file.name, status, koszt, "
                u"formula.brutto]\n    sort:\n      - property: koszt\n        direction: DESC\n    summaries:\n"
                u"      koszt: Sum\n"_s));
        int total = 0;
        const qint64 baseMs = cpuMs([&] {
            const ViewResult v = std::get<ViewResult>(runBase(base, 0, m_index, o));
            rows = int(v.rows.size());
            total = v.total;
        });
        QCOMPARE(total, Notes / 5 * 3 / 4);
        QCOMPARE(rows, DefaultQueryLimit);
        qInfo("a board of %d notes %lld ms, a base over %d notes %lld ms", Notes, boardMs, Notes, baseMs);
        QVERIFY2(boardMs <= 6000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(boardMs)));
        QVERIFY2(baseMs <= 12000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(baseMs)));
    }
};

QTEST_GUILESS_MAIN(TestViewsPerf)
#include "tst_views_perf.moc"
