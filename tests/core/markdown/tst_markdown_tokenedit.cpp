// Token edits of the token menus (markdown/tokenedit.h): actions on a chip's range, TokenRefs
// across copies of a block and editBlockToken() on whole files (byte for byte, CRLF).
#include "markdown/tokenedit.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::markdown;

namespace {

/// The range of the first occurrence of `token` in `body` (the chip's range in the tests).
std::pair<qsizetype, qsizetype> rangeOf(const QString &body, const QString &token)
{
    const qsizetype at = body.indexOf(token);
    return {at, at < 0 ? -1 : at + token.size()};
}

TokenEditContext context()
{
    TokenEditContext c;
    c.today = QDate(2026, 10, 27);
    c.defaultReminderTime = QTime(9, 0);
    return c;
}

} // namespace

class TstMarkdownTokenEdit : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void actions_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("chip");   // the chip's text in the body (its range)
        QTest::addColumn<QString>("action");
        QTest::addColumn<QVariant>("a");
        QTest::addColumn<QVariant>("b");
        QTest::addColumn<QString>("expected");

        const QString task = u"[ ] Zapłacić za żłobek 📅 2026-10-20 ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("set due") << task << u"📅 2026-10-20"_s << u"set"_s << QVariant(u"2026-10-22"_s) << QVariant()
                                 << u"[ ] Zapłacić za żłobek 📅 2026-10-22 ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("set due with time") << task << u"📅 2026-10-20"_s << u"set"_s << QVariant(u"2026-10-22 18:30"_s)
                                           << QVariant() << u"[ ] Zapłacić za żłobek 📅 2026-10-22 18:30 ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("set empty removes") << task << u"📅 2026-10-20"_s << u"set"_s << QVariant(QString()) << QVariant()
                                           << u"[ ] Zapłacić za żłobek ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("set garbage") << task << u"📅 2026-10-20"_s << u"set"_s << QVariant(u"jutro?"_s) << QVariant() << task;
        QTest::newRow("move") << task << u"📅 2026-10-20"_s << u"move"_s << QVariant(1) << QVariant()
                              << u"[ ] Zapłacić za żłobek 📅 2026-10-28 ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("shift week") << task << u"📅 2026-10-20"_s << u"shift"_s << QVariant(7) << QVariant(0)
                                    << u"[ ] Zapłacić za żłobek 📅 2026-10-27 ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("reminder hour later") << task << u"⏰ 2026-10-19 09:00"_s << u"shift"_s << QVariant(0) << QVariant(60)
                                             << u"[ ] Zapłacić za żłobek 📅 2026-10-20 ⏰ 2026-10-19 10:00"_s;
        QTest::newRow("reminder without time hour later")
                << u"Zadzwonić ⏰ 2026-10-19"_s << u"⏰ 2026-10-19"_s << u"shift"_s << QVariant(0) << QVariant(60)
                << u"Zadzwonić ⏰ 2026-10-19 10:00"_s;
        QTest::newRow("alarm on") << task << u"⏰ 2026-10-19 09:00"_s << u"alarm"_s << QVariant(true) << QVariant()
                                  << u"[ ] Zapłacić za żłobek 📅 2026-10-20 ⏰! 2026-10-19 09:00"_s;
        QTest::newRow("alarm on a date does nothing") << task << u"📅 2026-10-20"_s << u"alarm"_s << QVariant(true)
                                                      << QVariant() << task;
        QTest::newRow("reminder dialog") << u"Leki ⏰ 2026-10-19 09:00 🔁 every day"_s << u"⏰ 2026-10-19 09:00"_s
                                         << u"reminder"_s << QVariant(u"2026-10-20 08:15"_s)
                                         << QVariant(QVariantMap{{u"alarm"_s, true}, {u"recurrence"_s, u"co tydzień"_s}})
                                         << u"Leki ⏰! 2026-10-20 08:15 🔁 every week"_s;
        QTest::newRow("reminder dialog keeps rule") << u"Leki (@2026-10-19 09:00)"_s << u"(@2026-10-19 09:00)"_s
                                                    << u"reminder"_s << QVariant(u"2026-10-20 08:15"_s)
                                                    << QVariant(QVariantMap{{u"alarm"_s, false}})
                                                    << u"Leki ⏰ 2026-10-20 08:15"_s;
        QTest::newRow("remove reminder") << task << u"⏰ 2026-10-19 09:00"_s << u"remove"_s << QVariant() << QVariant()
                                         << u"[ ] Zapłacić za żłobek 📅 2026-10-20"_s;

        const QString recurring = u"[ ] Podlać kwiaty 🔁 co tydzień w poniedziałek 📅 2026-10-20"_s;
        QTest::newRow("rule") << recurring << u"🔁 co tydzień w poniedziałek"_s << u"recurrence"_s
                              << QVariant(u"co miesiąc"_s) << QVariant() << u"[ ] Podlać kwiaty 🔁 every month 📅 2026-10-20"_s;
        QTest::newRow("rule when done") << recurring << u"🔁 co tydzień w poniedziałek"_s << u"recurrence"_s
                                        << QVariant(u"every week on Monday when done"_s) << QVariant()
                                        << u"[ ] Podlać kwiaty 🔁 every week on Monday when done 📅 2026-10-20"_s;
        QTest::newRow("rule removed") << recurring << u"🔁 co tydzień w poniedziałek"_s << u"recurrence"_s << QVariant(QString())
                                      << QVariant() << u"[ ] Podlać kwiaty 📅 2026-10-20"_s;
        QTest::newRow("rule prefix only") << u"🔁 co tydzień kupić mleko"_s << u"🔁 co tydzień"_s << u"recurrence"_s
                                          << QVariant(u"every day"_s) << QVariant() << u"🔁 every day kupić mleko"_s;
        QTest::newRow("rule prefix removed") << u"Zakupy 🔁 co tydzień kupić mleko"_s << u"🔁 co tydzień"_s
                                             << u"remove"_s << QVariant() << QVariant() << u"Zakupy kupić mleko"_s;

        QTest::newRow("priority") << u"[ ] Pilne ⏫ zadanie"_s << u"⏫"_s << u"priority"_s << QVariant(u"lowest"_s)
                                  << QVariant() << u"[ ] Pilne ⏬ zadanie"_s;
        QTest::newRow("priority variation selector") << u"[ ] Pilne 🔼️ zadanie"_s << u"🔼️"_s << u"priority"_s
                                                     << QVariant(u"highest"_s) << QVariant() << u"[ ] Pilne 🔺 zadanie"_s;
        QTest::newRow("priority removed") << u"[ ] Pilne ⏫ zadanie"_s << u"⏫"_s << u"priority"_s << QVariant(QString())
                                          << QVariant() << u"[ ] Pilne zadanie"_s;
        QTest::newRow("logseq priority") << u"TODO [#B] zażółć gęślą jaźń"_s << u"[#B]"_s << u"priority"_s
                                         << QVariant(u"highest"_s) << QVariant() << u"TODO [#A] zażółć gęślą jaźń"_s;
        QTest::newRow("logseq low") << u"TODO [#B] zadanie"_s << u"[#B]"_s << u"priority"_s << QVariant(u"lowest"_s)
                                    << QVariant() << u"TODO [#C] zadanie"_s;

        QTest::newRow("block recurrence added") << u"[ ] Leki ⏰ 2026-10-19 09:00 ^abc"_s << u"⏰ 2026-10-19 09:00"_s
                                                << u"blockRecurrence"_s << QVariant(u"codziennie"_s) << QVariant()
                                                << u"[ ] Leki ⏰ 2026-10-19 09:00 🔁 every day ^abc"_s;
        QTest::newRow("block recurrence changed") << u"Leki ⏰ 2026-10-19 09:00 🔁 every day"_s << u"⏰ 2026-10-19 09:00"_s
                                                  << u"blockRecurrence"_s << QVariant(u"every weekday"_s) << QVariant()
                                                  << u"Leki ⏰ 2026-10-19 09:00 🔁 every weekday"_s;
        QTest::newRow("block recurrence removed") << u"Leki ⏰ 2026-10-19 09:00 🔁 every day"_s << u"⏰ 2026-10-19 09:00"_s
                                                  << u"blockRecurrence"_s << QVariant(QString()) << QVariant()
                                                  << u"Leki ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("block priority added") << u"[ ] Leki ⏰ 2026-10-19 09:00"_s << u"⏰ 2026-10-19 09:00"_s
                                              << u"blockPriority"_s << QVariant(u"high"_s) << QVariant()
                                              << u"[ ] Leki ⏰ 2026-10-19 09:00 ⏫"_s;
        QTest::newRow("block priority logseq") << u"TODO [#C] Leki ⏰ 2026-10-19 09:00"_s << u"⏰ 2026-10-19 09:00"_s
                                               << u"blockPriority"_s << QVariant(u"medium"_s) << QVariant()
                                               << u"TODO [#B] Leki ⏰ 2026-10-19 09:00"_s;
        QTest::newRow("block priority removed") << u"[ ] Leki 🔽 ⏰ 2026-10-19 09:00"_s << u"⏰ 2026-10-19 09:00"_s
                                                << u"blockPriority"_s << QVariant(QString()) << QVariant()
                                                << u"[ ] Leki ⏰ 2026-10-19 09:00"_s;

        QTest::newRow("day link move") << u"Spotkanie [[2026-10-20]] w sali"_s << u"[[2026-10-20]]"_s << u"move"_s
                                       << QVariant(1) << QVariant() << u"Spotkanie [[2026-10-28]] w sali"_s;
        QTest::newRow("day link shift keeps alias") << u"Zob. ![[2026-10-20|wtorek]]"_s << u"![[2026-10-20|wtorek]]"_s
                                                    << u"shift"_s << QVariant(7) << QVariant(0)
                                                    << u"Zob. ![[2026-10-27|wtorek]]"_s;
        QTest::newRow("day link set") << u"[[journals/2026-10-20.md]]"_s << u"[[journals/2026-10-20.md]]"_s << u"set"_s
                                      << QVariant(u"2026-12-31"_s) << QVariant() << u"[[journals/2026-12-31.md]]"_s;
        QTest::newRow("no day link") << u"[[Strona]]"_s << u"[[Strona]]"_s << u"move"_s << QVariant(1) << QVariant()
                                     << u"[[Strona]]"_s;

        QTest::newRow("token gone") << u"tekst bez znaczników"_s << u"tekst"_s << u"set"_s << QVariant(u"2026-10-22"_s)
                                    << QVariant() << u"tekst bez znaczników"_s;
        QTest::newRow("unknown action") << task << u"📅 2026-10-20"_s << u"explode"_s << QVariant() << QVariant() << task;
    }

    void actions()
    {
        QFETCH(QString, body);
        QFETCH(QString, chip);
        QFETCH(QString, action);
        QFETCH(QVariant, a);
        QFETCH(QVariant, b);
        QFETCH(QString, expected);
        const auto [from, to] = rangeOf(body, chip);
        QVERIFY(from >= 0);
        QCOMPARE(applyTokenAction(body, from, to, action, a, b, context()), expected);
    }

    void journalFormats()
    {
        TokenEditContext c = context();
        c.config.journalFileFormat = u"dd.MM.yyyy"_s;
        const QString body = u"Wizyta [[20.10.2026]] i [[2026-10-21]]"_s;
        auto [from, to] = rangeOf(body, u"[[20.10.2026]]"_s);
        QCOMPARE(applyTokenAction(body, from, to, u"shift"_s, 1, 0, c), u"Wizyta [[21.10.2026]] i [[2026-10-21]]"_s);
        // The common ISO form stays ISO.
        std::tie(from, to) = rangeOf(body, u"[[2026-10-21]]"_s);
        QCOMPARE(applyTokenAction(body, from, to, u"shift"_s, -1, 0, c), u"Wizyta [[20.10.2026]] i [[2026-10-20]]"_s);
        const auto link = journalLinkAt(body, from, to, c.config);
        QVERIFY(link);
        QCOMPARE(link->date, QDate(2026, 10, 21));
        QCOMPARE(link->target, u"2026-10-21"_s);
    }

    void tokenAtRanges()
    {
        const QString body = u"[ ] a 🔁 co tydzień kupić 📅 2026-10-20 `⏫` 🔽"_s;
        auto [from, to] = rangeOf(body, u"🔁 co tydzień"_s);
        auto t = tokenAt(body, from, to);
        QVERIFY(t);
        QCOMPARE(t->kind, TokenKind::Recurrence);
        QCOMPARE(t->canonical, u"every week"_s);
        QCOMPARE(t->from, from);
        QCOMPARE(t->to, to);
        std::tie(from, to) = rangeOf(body, u"🔽"_s);
        t = tokenAt(body, from, to);
        QVERIFY(t);
        QCOMPARE(t->priority, Priority::Low);
        QVERIFY(!tokenAt(body, -1, 3));
        QVERIFY(!tokenAt(body, 2, 999));
        QVERIFY(!tokenAt(body, 0, 3)); // "[ ]": no token
    }

    void refs_data()
    {
        QTest::addColumn<QString>("source"); // the block as the query result had it
        QTest::addColumn<QString>("chip");
        QTest::addColumn<int>("occurrence"); // which occurrence of `chip` in `source`
        QTest::addColumn<QString>("now");    // the block as the file has it now
        QTest::addColumn<QString>("found");  // what the ref names in `now` ("" = nothing)
        QTest::addColumn<int>("foundAt");    // occurrence of `found` in `now`

        QTest::newRow("same") << u"Zadanie 📅 2026-10-20"_s << u"📅 2026-10-20"_s << 0 << u"Zadanie 📅 2026-10-20"_s
                              << u"📅 2026-10-20"_s << 0;
        QTest::newRow("moved") << u"Zadanie 📅 2026-10-20"_s << u"📅 2026-10-20"_s << 0
                               << u"Zadanie z dłuższym opisem ⏫ 📅 2026-10-20"_s << u"📅 2026-10-20"_s << 0;
        QTest::newRow("second of two equal reminders") << u"Leki ⏰ 2026-10-19 09:00 i ⏰ 2026-10-19 09:00"_s
                                                       << u"⏰ 2026-10-19 09:00"_s << 1
                                                       << u"Leki! ⏰ 2026-10-19 09:00 i ⏰ 2026-10-19 09:00"_s
                                                       << u"⏰ 2026-10-19 09:00"_s << 1;
        QTest::newRow("alarm is the same reminder") << u"Leki ⏰ 2026-10-19 09:00"_s << u"⏰ 2026-10-19 09:00"_s << 0
                                                    << u"Leki ⏰! 2026-10-19 09:00"_s << u"⏰! 2026-10-19 09:00"_s << 0;
        QTest::newRow("value changed") << u"Zadanie 📅 2026-10-20"_s << u"📅 2026-10-20"_s << 0
                                       << u"Zadanie 📅 2026-10-21"_s << QString() << 0;
        QTest::newRow("gone") << u"Zadanie 🔁 every day"_s << u"🔁 every day"_s << 0 << u"Zadanie"_s << QString() << 0;
        QTest::newRow("rule") << u"Zadanie 🔁 every day kupić"_s << u"🔁 every day"_s << 0 << u"Inne 🔁 every day kupić"_s
                              << u"🔁 every day"_s << 0;
        QTest::newRow("logseq and emoji priority") << u"TODO [#A] x"_s << u"[#A]"_s << 0 << u"TODO [#A] x"_s << u"[#A]"_s << 0;
        QTest::newRow("in code is no token") << u"Zadanie 📅 2026-10-20"_s << u"📅 2026-10-20"_s << 0
                                             << u"Zadanie `📅 2026-10-20`"_s << QString() << 0;
    }

    void refs()
    {
        QFETCH(QString, source);
        QFETCH(QString, chip);
        QFETCH(int, occurrence);
        QFETCH(QString, now);
        QFETCH(QString, found);
        QFETCH(int, foundAt);
        qsizetype at = -1;
        for (int k = 0; k <= occurrence; ++k)
            at = source.indexOf(chip, at + 1);
        QVERIFY(at >= 0);
        const auto ref = tokenRefAt(source, at, at + chip.size());
        QVERIFY(ref);
        QCOMPARE(ref->ordinal, occurrence);
        const auto range = findTokenRef(now, *ref);
        if (found.isEmpty()) {
            QVERIFY(!range);
            return;
        }
        QVERIFY(range);
        qsizetype expected = -1;
        for (int k = 0; k <= foundAt; ++k)
            expected = now.indexOf(found, expected + 1);
        QCOMPARE(range->first, expected);
        QCOMPARE(range->second, expected + found.size());
    }

    void refDefaultTime()
    {
        // The index's reminder has the default time filled in.
        TokenRef ref{TokenKind::Reminder, u"2026-10-19 09:00"_s, 0, QTime(9, 0)};
        QVERIFY(findTokenRef(u"Leki ⏰ 2026-10-19"_s, ref));
        ref.defaultTime = QTime();
        QVERIFY(!findTokenRef(u"Leki ⏰ 2026-10-19"_s, ref));
        const TokenRef priority{TokenKind::Priority, u"high"_s, 0, {}};
        QVERIFY(findTokenRef(u"TODO [#A] x"_s, priority));
        QVERIFY(findTokenRef(u"[ ] x ⏫"_s, priority));
    }

    void editBlockTokenInFiles_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("action");
        QTest::addColumn<QVariant>("a");
        QTest::addColumn<QString>("expected");

        const QString page = u"---\r\ntytuł: Plan\r\n---\r\n# Plan\r\n\r\n- [ ] Zapłacić za żłobek 📅 2026-10-20\r\n"
                             u"    ⏰ 2026-10-19 09:00\r\n  - [ ] Dziecko: ⏫ zabrać kurtkę\r\n"_s;
        QTest::newRow("crlf due") << page << 5 << int(TokenKind::Due) << u"2026-10-20"_s << u"move"_s << QVariant(1)
                                  << QString(page).replace(u"📅 2026-10-20"_s, u"📅 2026-10-28"_s);
        QTest::newRow("continuation line, odd indentation") << page << 5 << int(TokenKind::Reminder)
                                                            << u"2026-10-19 09:00"_s << u"alarm"_s << QVariant(true)
                                                            << QString(page).replace(u"⏰ 2026-10-19"_s, u"⏰! 2026-10-19"_s);
        QTest::newRow("line inside the block") << page << 6 << int(TokenKind::Reminder) << u"2026-10-19 09:00"_s
                                               << u"remove"_s << QVariant()
                                               << QString(page).replace(u"    ⏰ 2026-10-19 09:00\r\n"_s, u"   \r\n"_s); // (one space goes with it)
        QTest::newRow("child") << page << 7 << int(TokenKind::Priority) << u"high"_s << u"priority"_s
                               << QVariant(u"low"_s) << QString(page).replace(u"⏫"_s, u"🔽"_s);
        QTest::newRow("token gone") << page << 7 << int(TokenKind::Due) << u"2026-10-20"_s << u"move"_s << QVariant(1)
                                    << page;
        QTest::newRow("frontmatter line") << page << 1 << int(TokenKind::Due) << u"2026-10-20"_s << u"move"_s
                                          << QVariant(1) << page;
        QTest::newRow("out of range") << page << 99 << int(TokenKind::Due) << u"2026-10-20"_s << u"move"_s << QVariant(1)
                                      << page;
        const QString code = u"```\n- [ ] w kodzie 📅 2026-10-20\n```\n- [ ] na zewnątrz 📅 2026-10-20\n"_s;
        QTest::newRow("code fence") << code << 1 << int(TokenKind::Due) << u"2026-10-20"_s << u"move"_s << QVariant(1)
                                    << code;
        QTest::newRow("after the fence") << code << 3 << int(TokenKind::Due) << u"2026-10-20"_s << u"move"_s << QVariant(1)
                                         << u"```\n- [ ] w kodzie 📅 2026-10-20\n```\n- [ ] na zewnątrz 📅 2026-10-28\n"_s;
        const QString noFinalNewline = u"> cytat 🔁 co tydzień"_s;
        QTest::newRow("quote without final newline") << noFinalNewline << 0 << int(TokenKind::Recurrence)
                                                     << u"every week"_s << u"recurrence"_s << QVariant(u"every day"_s)
                                                     << u"> cytat 🔁 every day"_s;
        const QString bom = u"﻿- [ ] zadanie 📅 2026-10-20\n"_s;
        QTest::newRow("bom") << bom << 0 << int(TokenKind::Due) << u"2026-10-20"_s << u"set"_s << QVariant(u"2026-11-01"_s)
                             << u"﻿- [ ] zadanie 📅 2026-11-01\n"_s;
    }

    void editBlockTokenInFiles()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(int, kind);
        QFETCH(QString, value);
        QFETCH(QString, action);
        QFETCH(QVariant, a);
        QFETCH(QString, expected);
        const TokenRef ref{TokenKind(kind), value, 0, {}};
        QCOMPARE(editBlockToken(content, line, ref, action, a, QVariant(), context()), expected);
    }
};

QTEST_GUILESS_MAIN(TstMarkdownTokenEdit)
#include "tst_markdown_tokenedit.moc"
