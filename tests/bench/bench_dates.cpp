// Benchmarks of the dates module: natural-language dates (typed phrases, quick capture, a long
// block scanned for every phrase — scaling rows detect O(n²)), recurrence rules, query date
// expressions, series expansion and human formatting.
#include "benchdata.h"

#include "dates/dates.h"

#include <QTest>

using namespace brulion;
using namespace brulion::dates;
using namespace Qt::StringLiterals;

class BenchDates : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parseNaturalPhrase_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("jutro o 18") << u"Zadzwoń do mamy jutro o 18"_s;
        QTest::newRow("przyszły piątek wieczorem") << u"Spotkanie z zespołem w przyszły piątek wieczorem"_s;
        QTest::newRow("za 3 godziny") << u"Wyłączyć piekarnik za 3 godziny"_s;
        QTest::newRow("15 października 2027") << u"Przegląd samochodu 15 października 2027 o 9:30"_s;
        QTest::newRow("english next friday 6pm") << u"Call the bank next friday 6pm"_s;
        QTest::newRow("no date (full scan)")
                << u"Kupić farbę do łazienki, 12.05 zł za litr, wersja 1.2.3 i 1/2 ceny — bez żadnej daty"_s;
    }
    void parseNaturalPhrase()
    {
        QFETCH(QString, text);
        const QDateTime now = bench::fixedNow();
        QBENCHMARK {
            const auto found = parseNaturalDate(text, now);
            Q_UNUSED(found);
        }
    }

    // findNaturalDates over a long text: a paragraph repeated to n characters, a few dates in it.
    // "numbers": a shopping list / budget text full of numbers and amounts (every number is a
    // date candidate checked against its context).
    void findNaturalDatesScaling_data()
    {
        QTest::addColumn<int>("chars");
        QTest::addColumn<bool>("numbers");
        for (int n : bench::sizes({1000, 10000, 50000}, {1000, 10000})) {
            QTest::newRow(qPrintable(u"%1 chars prose"_s.arg(n))) << n << false;
            QTest::newRow(qPrintable(u"%1 chars numbers"_s.arg(n))) << n << true;
        }
    }
    void findNaturalDatesScaling()
    {
        QFETCH(int, chars);
        QFETCH(bool, numbers);
        bench::Rng r(5);
        QString text;
        while (text.size() < chars) {
            if (numbers) {
                const int kg = r.range(1, 20);
                const int zloty = r.range(1, 30);
                const int grosze = r.range(10, 99);
                const int pieces = r.range(1, 12);
                text += u"%1 kg jabłek po %2.%3 zł, %4 szt. "_s.arg(kg).arg(zloty).arg(grosze).arg(pieces);
            } else {
                text += bench::sentence(r) + u' ';
            }
            if (r.chance(5))
                text += u"jutro o 18 "_s;
        }
        const QDateTime now = bench::fixedNow();
        QBENCHMARK {
            const auto all = findNaturalDates(text, now);
            Q_UNUSED(all);
        }
    }

    void parseRecurrence_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("co tydzień w pn") << u"co tydzień w pn"_s;
        QTest::newRow("every 2 weeks on Monday, Friday") << u"every 2 weeks on Monday, Friday"_s;
        QTest::newRow("co miesiąc ostatniego dnia") << u"co miesiąc ostatniego dnia"_s;
        QTest::newRow("invalid") << u"kiedyś tam, jak będzie czas"_s;
    }
    void parseRecurrence()
    {
        QFETCH(QString, text);
        QBENCHMARK {
            const auto rule = dates::parseRecurrence(text);
            Q_UNUSED(rule);
        }
    }

    void resolveDateExpr_data()
    {
        QTest::addColumn<QString>("expr");
        QTest::newRow("today..+7d") << u"today..+7d"_s;
        QTest::newRow("następny-tydzień") << u"następny-tydzień"_s;
        QTest::newRow("2026-10-15") << u"2026-10-15"_s;
    }
    void resolveDateExpr()
    {
        QFETCH(QString, expr);
        const QDate today = bench::fixedToday();
        QBENCHMARK {
            const auto r = dates::resolveDateExpr(expr, today);
            Q_UNUSED(r);
        }
    }

    // A daily series over a 10-year range (capped by max = 500) and a weekly one over a month.
    void occurrencesBetween_data()
    {
        QTest::addColumn<QString>("rule");
        QTest::addColumn<int>("days");
        QTest::newRow("daily, 10 years (max 500)") << u"every day"_s << 3650;
        QTest::newRow("weekly Mon+Fri, 1 month") << u"every week on Monday, Friday"_s << 31;
        QTest::newRow("monthly, anchor 1990, 1 month") << u"every month"_s << 31;
    }
    void occurrencesBetween()
    {
        QFETCH(QString, rule);
        QFETCH(int, days);
        const auto parsed = dates::parseRecurrence(rule);
        QVERIFY(parsed);
        const DateSpec anchor{rule.contains(u"month"_s) ? QDate(1990, 1, 31) : QDate(2026, 1, 1), QTime(9, 0)};
        const QDate from = bench::fixedToday();
        QBENCHMARK {
            const auto all = dates::occurrencesBetween(*parsed, anchor, from, from.addDays(days));
            Q_UNUSED(all);
        }
    }

    void formatHuman()
    {
        const DateSpec spec{QDate(2026, 10, 23), QTime(17, 0)};
        const QDate today = bench::fixedToday();
        QBENCHMARK {
            const QString s = dates::formatHuman(spec, UiLocale::Polish, today);
            Q_UNUSED(s);
        }
    }
};

QTEST_GUILESS_MAIN(BenchDates)
#include "bench_dates.moc"
