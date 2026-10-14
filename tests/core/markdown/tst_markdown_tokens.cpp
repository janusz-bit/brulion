// Metadata tokens, task markers, property values, masking and line helpers.
// Test cases ported from archive/web-v1 (tokens/properties/mask/lines tests).
#include "markdown/inline.h"
#include "markdown/lines_p.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "markdown/tokens_p.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

QString kindName(TokenKind k)
{
    switch (k) {
    case TokenKind::Due: return u"due"_s;
    case TokenKind::Scheduled: return u"scheduled"_s;
    case TokenKind::Start: return u"start"_s;
    case TokenKind::Created: return u"created"_s;
    case TokenKind::Done: return u"done"_s;
    case TokenKind::Cancelled: return u"cancelled"_s;
    case TokenKind::Recurrence: return u"recurrence"_s;
    case TokenKind::Priority: return u"priority"_s;
    case TokenKind::Reminder: return u"reminder"_s;
    case TokenKind::ReminderCompat: return u"reminderCompat"_s;
    case TokenKind::TaskId: return u"taskId"_s;
    case TokenKind::DependsOn: return u"dependsOn"_s;
    case TokenKind::OnCompletion: return u"onCompletion"_s;
    case TokenKind::LogseqPriority: return u"logseqPriority"_s;
    case TokenKind::Planning: return u"planning"_s;
    }
    return {};
}

QStringList kinds(const QString &s)
{
    QStringList out;
    for (const Token &t : findTokens(s))
        out.append(kindName(t.kind));
    return out;
}

QString iso(QDate d)
{
    return d.toString(u"yyyy-MM-dd"_s);
}

QString priorityName(Priority p)
{
    switch (p) {
    case Priority::None: return u"none"_s;
    case Priority::Lowest: return u"lowest"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::High: return u"high"_s;
    case Priority::Highest: return u"highest"_s;
    }
    return {};
}

} // namespace

class TestTokens : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void constants()
    {
        QCOMPARE(tokens::Due, u"📅");
        QCOMPARE(tokens::Scheduled, u"⏳");
        QCOMPARE(tokens::Start, u"🛫");
        QCOMPARE(tokens::Created, u"➕");
        QCOMPARE(tokens::Done, u"✅");
        QCOMPARE(tokens::Cancelled, u"❌");
        QCOMPARE(tokens::Recurrence, u"🔁");
        QCOMPARE(tokens::Reminder, u"⏰");
        QCOMPARE(tokens::Alarm, u"⏰!");
        QCOMPARE(tokens::priorityEmoji(Priority::Highest), u"🔺");
        QCOMPARE(tokens::priorityEmoji(Priority::High), u"⏫");
        QCOMPARE(tokens::priorityEmoji(Priority::Medium), u"🔼");
        QCOMPARE(tokens::priorityEmoji(Priority::Low), u"🔽");
        QCOMPARE(tokens::priorityEmoji(Priority::Lowest), u"⏬");
        QVERIFY(tokens::priorityEmoji(Priority::None).isEmpty());
    }

    void everyDateSignifier()
    {
        const auto toks =
                findTokens(u"x 📅 2026-10-15 ⏳ 2026-10-14 🛫 2026-10-13 ➕ 2026-10-01 ✅ 2026-10-16 ❌ 2026-10-17"_s);
        QStringList got;
        for (const Token &t : toks)
            got.append(kindName(t.kind) + u'=' + iso(t.date));
        QCOMPARE(got, QStringList({u"due=2026-10-15"_s, u"scheduled=2026-10-14"_s, u"start=2026-10-13"_s,
                                   u"created=2026-10-01"_s, u"done=2026-10-16"_s, u"cancelled=2026-10-17"_s}));
    }

    void optionalTime()
    {
        const auto toks = findTokens(u"📅 2026-10-15 10:00 ⏳ 2026-10-15T07:05 🛫 2026-10-15 9:30"_s);
        QCOMPARE(toks.size(), 3);
        QCOMPARE(toks[0].kind, TokenKind::Due);
        QCOMPARE(toks[0].time, QTime(10, 0));
        QCOMPARE(toks[1].kind, TokenKind::Scheduled);
        QCOMPARE(toks[1].time, QTime(7, 5));
        QCOMPARE(toks[2].time, QTime(9, 30));
        QCOMPARE(toks[0].spec().toString(), u"2026-10-15 10:00"_s);
    }

    void emojiVariants()
    {
        QCOMPARE(kinds(u"📆 2026-01-01 🗓️ 2026-01-02 ⌛2026-01-03 ⏳️ 2026-01-04"_s),
                 QStringList({u"due"_s, u"due"_s, u"scheduled"_s, u"scheduled"_s}));
    }

    void invalidDatesAndTimes()
    {
        QVERIFY(findTokens(u"📅 2026-02-30"_s).isEmpty());
        QVERIFY(findTokens(u"📅 2026-13-45"_s).isEmpty());
        QVERIFY(findTokens(u"📅 2026-10-155"_s).isEmpty());
        const QString s = u"📅 2026-10-15 25:00 rest"_s;
        const auto toks = findTokens(s);
        QCOMPARE(toks.size(), 1);
        QCOMPARE(toks[0].from, 0);
        QCOMPARE(iso(toks[0].date), u"2026-10-15"_s);
        QVERIFY(!toks[0].time);
        QCOMPARE(s.sliced(toks[0].to), u" 25:00 rest"_s);
        // A time followed by more digits is not a time.
        const auto t2 = findTokens(u"📅 2026-10-15 10:000"_s);
        QCOMPARE(t2.size(), 1);
        QVERIFY(!t2[0].time);
    }

    void valueRange()
    {
        const QString s = u"a 📅 2026-10-15 10:00 b"_s;
        const auto toks = findTokens(s);
        QCOMPARE(toks.size(), 1);
        QCOMPARE(s.sliced(toks[0].valueFrom, toks[0].valueTo - toks[0].valueFrom), u"2026-10-15 10:00"_s);
        QCOMPARE(s.sliced(toks[0].from, toks[0].to - toks[0].from), u"📅 2026-10-15 10:00"_s);
    }

    void reminders()
    {
        const auto toks = findTokens(u"⏰ 2026-10-15 09:00 ⏰! 2026-10-15 06:30 ⏰ 2026-10-16"_s);
        QCOMPARE(toks.size(), 3);
        QCOMPARE(toks[0].kind, TokenKind::Reminder);
        QCOMPARE(toks[0].time, QTime(9, 0));
        QVERIFY(!toks[0].alarm);
        QVERIFY(toks[1].alarm);
        QCOMPARE(toks[1].time, QTime(6, 30));
        QCOMPARE(iso(toks[2].date), u"2026-10-16"_s);
        QVERIFY(!toks[2].time);
        // Variation selector before the alarm mark.
        const auto vs = findTokens(u"⏰️! 2026-10-15 06:30"_s);
        QCOMPARE(vs.size(), 1);
        QVERIFY(vs[0].alarm);
    }

    void compatReminders()
    {
        const QString s = u"call (@2026-10-15 09:00) and (@2026-10-16)"_s;
        const auto toks = findTokens(s);
        QCOMPARE(toks.size(), 2);
        QCOMPARE(toks[0].kind, TokenKind::ReminderCompat);
        QCOMPARE(toks[0].time, QTime(9, 0));
        QCOMPARE(s.sliced(toks[0].from, toks[0].to - toks[0].from), u"(@2026-10-15 09:00)"_s);
        QCOMPARE(s.sliced(toks[0].valueFrom, toks[0].valueTo - toks[0].valueFrom), u"2026-10-15 09:00"_s);
        QCOMPARE(iso(toks[1].date), u"2026-10-16"_s);
        QVERIFY(findTokens(u"(@2026-10-15 99:00)"_s).isEmpty());
        QVERIFY(findTokens(u"(@2026-10-15 09:00"_s).isEmpty());
    }

    void recurrenceText()
    {
        const auto toks = findTokens(u"task 🔁 every week on Monday 📅 2026-10-19"_s);
        QCOMPARE(toks.size(), 2);
        QCOMPARE(toks[0].kind, TokenKind::Recurrence);
        QCOMPARE(toks[0].value, u"every week on Monday"_s);
        QCOMPARE(toks[0].canonical, u"every week on Monday"_s);
        QCOMPARE(toks[1].kind, TokenKind::Due);
    }

    void recurrencePolishAndRaw()
    {
        QCOMPARE(recurrenceOf(findTokens(u"🔁 co tydzień w poniedziałek, piątek"_s).first()),
                 u"every week on Monday, Friday"_s);
        QCOMPARE(recurrenceOf(findTokens(u"🔁 co 2 tygodnie po wykonaniu"_s).first()), u"every 2 weeks when done"_s);
        const Token raw = findTokens(u"🔁 every blue moon"_s).first();
        QVERIFY(raw.canonical.isEmpty());
        QCOMPARE(recurrenceOf(raw), u"every blue moon"_s);
        QVERIFY(recurrenceOf(findTokens(u"🔁"_s).first()).isEmpty());
    }

    void recurrenceStops()
    {
        QCOMPARE(findTokens(u"🔁 every day #praca"_s).first().value, u"every day"_s);
        QCOMPARE(findTokens(u"🔁 every day ^abc123"_s).first().value, u"every day"_s);
        QCOMPARE(findTokens(u"🔁 every day\nnext line"_s).first().value, u"every day"_s);
        QCOMPARE(findTokens(u"🔁 every day %%c%%"_s).first().value, u"every day"_s);
        QCOMPARE(findTokens(u"🔁 every day (@2026-10-15)"_s).first().value, u"every day"_s);
        QCOMPARE(findTokens(u"🔁 codziennie ⏰ 2026-10-15 08:00"_s).first().value, u"codziennie"_s);
        // A '#' inside a word or followed by a space does not stop the rule.
        QCOMPARE(findTokens(u"🔁 every C# day"_s).first().value, u"every C# day"_s);
    }

    void priorities()
    {
        QStringList got;
        for (const Token &t : findTokens(u"🔺 ⏫ 🔼 🔽 ⏬"_s))
            got.append(priorityName(t.priority));
        QCOMPARE(got, QStringList({u"highest"_s, u"high"_s, u"medium"_s, u"low"_s, u"lowest"_s}));
        const auto logseq = findTokens(u"TODO [#A] x [#C] [#D]"_s);
        QCOMPARE(logseq.size(), 2);
        QCOMPARE(logseq[0].kind, TokenKind::LogseqPriority);
        QCOMPARE(logseq[0].priority, Priority::High);
        QCOMPARE(logseq[1].priority, Priority::Low);
    }

    void obsidianTasksIds()
    {
        QCOMPARE(kinds(u"🆔 abc123 ⛔ x1,y2 🏁 delete"_s),
                 QStringList({u"taskId"_s, u"dependsOn"_s, u"onCompletion"_s}));
        const auto toks = findTokens(u"⛔ a1 , b2, c3 tail"_s);
        QCOMPARE(toks.first().value, u"a1 , b2, c3"_s);
        QVERIFY(findTokens(u"🆔 "_s).isEmpty());
    }

    void planning()
    {
        const auto toks = findTokens(u"SCHEDULED: <2026-10-15 Thu 10:00 .+1w> DEADLINE: <2026-10-20 Tue>"_s);
        QCOMPARE(toks.size(), 2);
        QCOMPARE(toks[0].kind, TokenKind::Planning);
        QCOMPARE(toks[0].field, TokenKind::Scheduled);
        QCOMPARE(iso(toks[0].date), u"2026-10-15"_s);
        QCOMPARE(toks[0].time, QTime(10, 0));
        QCOMPARE(toks[0].repeater, u".+1w"_s);
        QCOMPARE(toks[1].field, TokenKind::Due);
        QCOMPARE(iso(toks[1].date), u"2026-10-20"_s);
        QVERIFY(toks[1].repeater.isEmpty());
        QVERIFY(findTokens(u"SCHEDULED: <nonsense>"_s).isEmpty());
    }

    void plainTextIsFast() { QVERIFY(findTokens(u"zwykły tekst bez tokenów zażółć gęślą jaźń"_s).isEmpty()); }

    void logseqTimestamp()
    {
        auto ts = parseLogseqTimestamp(u"2026-10-15 Thu");
        QVERIFY(ts);
        QCOMPARE(iso(ts->date), u"2026-10-15"_s);
        QVERIFY(!ts->time);
        QVERIFY(ts->repeater.isEmpty());
        ts = parseLogseqTimestamp(u"2026-10-15 czw 9:00-10:00 ++2d");
        QVERIFY(ts);
        QCOMPARE(ts->time, QTime(9, 0));
        QCOMPARE(ts->repeater, u"++2d"_s);
        QVERIFY(!parseLogseqTimestamp(u"nonsense"));
        QVERIFY(!parseLogseqTimestamp(u"2026-13-01"));
    }

    void blockIds()
    {
        auto id = blockIdAtEnd(u"text ^abc-123");
        QVERIFY(id);
        QCOMPARE(id->id, u"abc-123"_s);
        QCOMPARE(id->from, 4);
        QCOMPARE(id->to, 13);
        id = blockIdAtEnd(u"^solo");
        QVERIFY(id);
        QCOMPARE(id->from, 0);
        QCOMPARE(id->to, 5);
        id = blockIdAtEnd(u"żółw ^id1  ");
        QVERIFY(id);
        QCOMPARE(id->id, u"id1"_s);
        QCOMPARE(id->to, 11);
        QVERIFY(!blockIdAtEnd(u"x^2"));
        QVERIFY(!blockIdAtEnd(u"text ^id more"));
        QVERIFY(!blockIdAtEnd(u"text ^"));
        QCOMPARE(standaloneBlockId(u"  ^tbl1  "), u"tbl1"_s);
        QVERIFY(standaloneBlockId(u"a ^tbl1").isEmpty());
        QVERIFY(isValidBlockId(u"abc-123"));
        QVERIFY(!isValidBlockId(u"abc_123"));
        QVERIFY(!isValidBlockId(u""));
    }

    void removeRanges()
    {
        using detail::removeRanges;
        QCOMPARE(removeRanges(u"a XX b", {{2, 4}}), u"a b"_s);
        QCOMPARE(removeRanges(u"XX b", {{0, 2}}), u"b"_s);
        QCOMPARE(removeRanges(u"a XX", {{2, 4}}), u"a"_s);
        QCOMPARE(removeRanges(u"a XX YY b", {{5, 7}, {2, 4}}), u"a b"_s);
        QCOMPARE(removeRanges(u"abc", {}), u"abc"_s);
        QCOMPARE(removeRanges(u"a\n  XX b", {{4, 6}}), u"a\nb"_s);
    }

    void stripTokens_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("all") << u"Kup **mleko** 📅 2026-10-15 ⏫ 🔁 every week ⏰ 2026-10-15 09:00 ^abc"_s
                             << u"Kup **mleko**"_s;
        QTest::newRow("logseq") << u"TODO [#A] x SCHEDULED: <2026-10-15 Thu>"_s << u"TODO x"_s;
        QTest::newRow("compat") << u"(@2026-10-15 09:00) zadzwoń do mamy"_s << u"zadzwoń do mamy"_s;
        QTest::newRow("code") << u"use `📅 2026-10-15` syntax 📅 2026-10-16"_s << u"use `📅 2026-10-15` syntax"_s;
        QTest::newRow("multiline") << u"a ✅ 2026-01-01\r\nb ^id"_s << u"a\r\nb"_s;
        QTest::newRow("polish") << u"Zapłacić za prąd ⏰! 2026-10-20 08:00 🔁 co miesiąc"_s << u"Zapłacić za prąd"_s;
        QTest::newRow("empty") << QString() << QString();
    }

    void stripTokens()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(markdown::stripTokens(input), expected);
    }

    void formatReminder()
    {
        QCOMPARE(formatReminderToken(DateSpec{QDate(2026, 10, 15), QTime(9, 0)}), u"⏰ 2026-10-15 09:00"_s);
        QCOMPARE(formatReminderToken(DateSpec{QDate(2026, 10, 15), QTime(6, 30)}, true), u"⏰! 2026-10-15 06:30"_s);
        QCOMPARE(formatReminderToken(DateSpec{QDate(2026, 10, 15), std::nullopt}), u"⏰ 2026-10-15"_s);
        const auto toks = findTokens(formatReminderToken(DateSpec{QDate(2026, 12, 24), QTime(18, 45)}, true));
        QCOMPARE(toks.size(), 1);
        QCOMPARE(toks[0].kind, TokenKind::Reminder);
        QCOMPARE(toks[0].time, QTime(18, 45));
        QVERIFY(toks[0].alarm);
    }

    void hostileInputDoesNotHang()
    {
        QString s;
        for (int i = 0; i < 20000; ++i)
            s += u"🔁 📅 (@ [# SCHEDULED: <"_s;
        QElapsedTimer t;
        t.start();
        findTokens(s);
        markdown::stripTokens(s);
        QVERIFY(t.elapsed() < 3000);
    }

    // --- task markers ------------------------------------------------------------------------

    void taskStatuses()
    {
        QCOMPARE(statusFromCheckbox(u' '), TaskStatus::Todo);
        QCOMPARE(statusFromCheckbox(u'X'), TaskStatus::Done);
        QCOMPARE(statusFromCheckbox(u'x'), TaskStatus::Done);
        QCOMPARE(statusFromCheckbox(u'?'), TaskStatus::Todo);
        QCOMPARE(statusFromCheckbox(u'/'), TaskStatus::Doing);
        QCOMPARE(statusFromCheckbox(u'-'), TaskStatus::Cancelled);
        QCOMPARE(statusFromCheckbox(u'>'), TaskStatus::Deferred);
        QCOMPARE(statusFromKeyword(u"IN-PROGRESS"), TaskStatus::Doing);
        QCOMPARE(statusFromKeyword(u"WAIT"), TaskStatus::Deferred);
        QCOMPARE(statusFromKeyword(u"CANCELLED"), TaskStatus::Cancelled);
        QVERIFY(!statusFromKeyword(u"UNKNOWN"));
        QCOMPARE(statusChar(TaskStatus::Cancelled), QChar(u'-'));
    }

    void replacementMarkers()
    {
        QCOMPARE(checkboxCharFor(TaskStatus::Done, QChar(u'X')), QChar(u'X'));
        QCOMPARE(checkboxCharFor(TaskStatus::Done, QChar(u' ')), QChar(u'x'));
        QCOMPARE(keywordFor(TaskStatus::Cancelled, u"CANCELLED"), u"CANCELLED"_s);
        QCOMPARE(keywordFor(TaskStatus::Deferred, u"WAIT"), u"WAIT"_s);
        QCOMPARE(keywordFor(TaskStatus::Todo), u"TODO"_s);
        QCOMPARE(keywordFor(TaskStatus::Doing, u"LATER"), u"NOW"_s);
        QCOMPARE(keywordFor(TaskStatus::Todo, u"NOW"), u"LATER"_s);
    }

    void matchMarkers()
    {
        auto m = matchTaskMarker(u"- [x] a", 2);
        QVERIFY(m);
        QCOMPARE(m->kind, TaskMarker::Kind::Checkbox);
        QCOMPARE(m->status, TaskStatus::Done);
        QCOMPARE(m->marker, u"x"_s);
        QCOMPARE(m->from, 2);
        QCOMPARE(m->to, 5);
        QCOMPARE(m->contentStart, 6);
        m = matchTaskMarker(u"- TODO", 2);
        QVERIFY(m);
        QCOMPARE(m->kind, TaskMarker::Kind::Keyword);
        QCOMPARE(m->status, TaskStatus::Todo);
        QCOMPARE(m->contentStart, 6);
        m = matchTaskMarker(u"- WAITING na kuriera", 2);
        QVERIFY(m);
        QCOMPARE(m->marker, u"WAITING"_s);
        QVERIFY(!matchTaskMarker(u"- TODOs", 2));
        QVERIFY(!matchTaskMarker(u"- [x](url)", 2));
        QVERIFY(!matchTaskMarker(u"- []", 2));
        QVERIFY(!matchTaskMarker(u"- [ab] c", 2));
    }

    // --- properties --------------------------------------------------------------------------

    void topLevelCommas()
    {
        QCOMPARE(detail::splitTopLevelCommas(u"[[a, b]], c, \"d, e\", ((x,y))"),
                 QStringList({u"[[a, b]]"_s, u" c"_s, u" \"d, e\""_s, u" ((x,y))"_s}));
    }

    void refNames()
    {
        QCOMPARE(detail::refName(u"[[Strona]]"), u"Strona"_s);
        QCOMPARE(detail::refName(u"#[[Dwa słowa]]"), u"Dwa słowa"_s);
        QCOMPARE(detail::refName(u"#zakupy/spożywcze"), u"zakupy/spożywcze"_s);
        QVERIFY(detail::refName(u"#123").isEmpty());
        QVERIFY(detail::refName(u"tekst").isEmpty());
        QCOMPARE(detail::cleanName(u" \"#[[x]]\" "), u"x"_s);
        QCOMPARE(detail::cleanName(u"'y'"), u"y"_s);
    }

    void propertyValues()
    {
        QCOMPARE(parsePropertyValue(u"related"_s, u"[[A]], [[B C]], #d").toStringList(),
                 QStringList({u"A"_s, u"B C"_s, u"d"_s}));
        QCOMPARE(parsePropertyValue(u"related"_s, u"[[A]]").toStringList(), QStringList({u"A"_s}));
        QCOMPARE(parsePropertyValue(u"note"_s, u"[[A]] and text"), QVariant(u"[[A]] and text"_s));
        QCOMPARE(parsePropertyValue(u"price"_s, u"12,50 zł"), QVariant(u"12,50 zł"_s));
        QCOMPARE(parsePropertyValue(u"empty"_s, u"   "), QVariant(QString()));
        QCOMPARE(parsePropertyValue(u"tags"_s, u"a, #b, [[c d]]").toStringList(),
                 QStringList({u"a"_s, u"b"_s, u"c d"_s}));
        QCOMPARE(parsePropertyValue(u"alias"_s, u"Jedyny").toStringList(), QStringList({u"Jedyny"_s}));
        QCOMPARE(parsePropertyValue(u"aliases"_s, u"").toStringList(), QStringList());
        QCOMPARE(parsePropertyValue(u"aliases"_s, u"").typeId(), QMetaType::QStringList);
    }

    void propertyLines()
    {
        auto p = matchPropertyLine(u"key:: value  ");
        QVERIFY(p);
        QCOMPARE(p->key, u"key"_s);
        QCOMPARE(p->valueFrom, 6);
        QCOMPARE(p->valueTo, 11);
        p = matchPropertyLine(u"D-e_f::");
        QVERIFY(p);
        QCOMPARE(p->valueFrom, p->valueTo);
        QVERIFY(!matchPropertyLine(u"std::vector is not"));
        QVERIFY(!matchPropertyLine(u" key:: v"));
        QVERIFY(!matchPropertyLine(u"klucz ąę:: v"));
        QVERIFY(!matchPropertyLine(u":: v"));
    }

    void frontmatterConversion()
    {
        const QVariantMap data{{u"Title"_s, u"T"_s},
                               {u"count"_s, qlonglong(3)},
                               {u"ok"_s, true},
                               {u"ratio"_s, 1.5},
                               {u"list"_s, QVariantList{u"a"_s, qlonglong(2), QVariantList{u"Link"_s}}},
                               {u"map"_s, QVariantMap{{u"a"_s, 1}}},
                               {u"none"_s, QVariant()}};
        const Properties props = frontmatterProperties(data);
        QCOMPARE(props.size(), 5);
        QCOMPARE(props.value(u"title"_s), QVariant(u"T"_s));
        QCOMPARE(props.value(u"count"_s), QVariant(u"3"_s));
        QCOMPARE(props.value(u"ok"_s), QVariant(u"true"_s));
        QCOMPARE(props.value(u"ratio"_s), QVariant(u"1.5"_s));
        QCOMPARE(props.value(u"list"_s).toStringList(), QStringList({u"a"_s, u"2"_s, u"[[Link]]"_s}));
    }

    void namesFromValues()
    {
        QCOMPARE(frontmatterGet({{u"Tags"_s, u"x"_s}}, {u"tags"_s}), QVariant(u"x"_s));
        QVERIFY(!frontmatterGet({}, {u"tags"_s}).isValid());
        QCOMPARE(namesFrom(u"a, b c"_s, true), QStringList({u"a"_s, u"b"_s, u"c"_s}));
        QCOMPARE(namesFrom(u"a, b c"_s, false), QStringList({u"a"_s, u"b c"_s}));
        QCOMPARE(namesFrom(QVariantList{u"#x"_s, u"#y #z"_s, qlonglong(5), QVariantList{u"Link"_s}}, true),
                 QStringList({u"x"_s, u"y"_s, u"z"_s, u"5"_s, u"Link"_s}));
        QCOMPARE(namesFrom(QVariant(), true), QStringList());
        QCOMPARE(uniqueCaseInsensitive({u"Łódź"_s, u"łódź"_s, u"Kraków"_s}), QStringList({u"Łódź"_s, u"Kraków"_s}));
    }

    // --- masking -----------------------------------------------------------------------------

    void maskCodeSpans()
    {
        const QString s = u"a `#x` b ``c ` d`` e"_s;
        const QString m = detail::maskInline(s);
        QCOMPARE(m.size(), s.size());
        QCOMPARE(m, u"a "_s + QString(4, u' ') + u" b "_s + QString(9, u' ') + u" e"_s);
    }

    void maskLeavesUnmatched()
    {
        QCOMPARE(detail::maskInline(u"a ` b #tag"), u"a ` b #tag"_s);
        QCOMPARE(detail::maskInline(u"50%% off #sale"), u"50%% off #sale"_s);
        QCOMPARE(detail::maskInline(u"<!-- open"), u"<!-- open"_s);
    }

    void maskComments()
    {
        QCOMPARE(detail::maskInline(u"x %%[[hidden]]%% y"), u"x "_s + QString(14, u' ') + u" y"_s);
        QCOMPARE(detail::maskInline(u"a <!-- #c --> b"), u"a "_s + QString(11, u' ') + u" b"_s);
        QCOMPARE(detail::maskInline(u"a %%one\ntwo%% b"),
                 u"a "_s + QString(5, u' ') + u'\n' + QString(5, u' ') + u" b"_s);
        QCOMPARE(detail::maskInline(u"x %%c%% `y`", {false, true}), u"x %%c%% "_s + QString(3, u' '));
        QCOMPARE(detail::maskInline(u"\\`not code\\` `code`"), u"\\`not code\\` "_s + QString(6, u' '));
        QCOMPARE(detail::maskInline(u"`%%` real %%c%%"), QString(4, u' ') + u" real "_s + QString(5, u' '));
        QCOMPARE(maskInlineCode(u"ą `ę` ł"), u"ą     ł"_s);
    }

    void maskPairsRuns()
    {
        QCOMPARE(detail::maskInline(u"`a` ``b`` `c`` d`"),
                 QString(3, u' ') + u' ' + QString(5, u' ') + u' ' + QString(7, u' '));
        QCOMPARE(detail::maskInline(u"``x` y``"), QString(8, u' '));
    }

    void maskIsLinear()
    {
        QString s = u"`"_s;
        for (int i = 0; i < 20000; ++i)
            s += u" x "_s;
        s += u"``"_s;
        for (int i = 0; i < 20000; ++i)
            s += u" y"_s;
        QString runs;
        for (int k = 0; k < 1500; ++k) {
            if (k)
                runs += u" x "_s;
            runs += QString(k + 1, u'`');
        }
        QElapsedTimer t;
        t.start();
        detail::maskInline(s);
        const QString m = detail::maskInline(runs);
        QVERIFY(t.elapsed() < 2000);
        QCOMPARE(m, runs); // no run has a partner of the same length
    }

    void fences()
    {
        auto f = detail::fenceOpen(u"```ts title");
        QVERIFY(f);
        QCOMPARE(f->ch, QChar(u'`'));
        QCOMPARE(f->length, 3);
        QCOMPARE(f->info, u"ts title"_s);
        f = detail::fenceOpen(u"  ~~~~");
        QVERIFY(f);
        QCOMPARE(f->length, 4);
        QVERIFY(f->info.isEmpty());
        QVERIFY(!detail::fenceOpen(u"``not"));
        QVERIFY(!detail::fenceOpen(u"``` a`b"));
        const auto open = *detail::fenceOpen(u"````");
        QVERIFY(!detail::isFenceClose(u"```", open));
        QVERIFY(detail::isFenceClose(u"`````", open));
        QVERIFY(!detail::isFenceClose(u"~~~~", open));
        QVERIFY(!detail::isFenceClose(u"```` x", open));
    }

    void docComments_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("multi-line") << u"a\n%%\n- [ ] hidden\n%%\nb"_s << u"a\n  \n            \n  \nb"_s;
        QTest::newRow("in code") << u"```\n%%\n```\n`%%` text"_s << u"```\n%%\n```\n`%%` text"_s;
        QTest::newRow("unclosed") << u"start %% never closed\n- [ ] task"_s << u"start %% never closed\n- [ ] task"_s;
        QTest::newRow("crlf") << u"x %%a\r\nb%% y"_s << u"x    \r\n    y"_s;
        QTest::newRow("fence in list item")
                << u"- ```mermaid\n  %% diagram comment\n  graph TD\n  ```\n- [ ] visible %%c%%"_s
                << u"- ```mermaid\n  %% diagram comment\n  graph TD\n  ```\n- [ ] visible      "_s;
        QTest::newRow("fence in callout") << u"> [!note]\n> ```mermaid\n> %% c\n> ```\n\n- [ ] task\n%%x%%"_s
                                          << u"> [!note]\n> ```mermaid\n> %% c\n> ```\n\n- [ ] task\n     "_s;
        QTest::newRow("nested quotes") << u"> > 1. ~~~\n> >    %% x\n> >    ~~~\nafter %%y%%"_s
                                       << u"> > 1. ~~~\n> >    %% x\n> >    ~~~\nafter      "_s;
    }

    void docComments()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto lines = text::splitLines(input);
        const QString m = detail::maskRanges(input, detail::docCommentRanges(input, lines));
        QCOMPARE(m.size(), input.size());
        QCOMPARE(m, expected);
    }

    // --- line helpers ------------------------------------------------------------------------

    void indentation()
    {
        using detail::indentOf;
        QCOMPARE(indentOf(u"abc").col, 0);
        QCOMPARE(indentOf(u"  - x").col, 2);
        QCOMPARE(indentOf(u"  - x").chars, 2);
        QCOMPARE(indentOf(u"\t- x").col, 4);
        QCOMPARE(indentOf(u"\t- x").chars, 1);
        QCOMPARE(indentOf(u"  \t- x").col, 4);
        QCOMPARE(indentOf(u"  \t- x").chars, 3);
        QCOMPARE(indentOf(u"\t\tx").col, 8);
        QCOMPARE(detail::charsForColumns(u"    text", 2), 2);
        QCOMPARE(detail::charsForColumns(u"\ttext", 2), 1);
        QCOMPARE(detail::charsForColumns(u"\t  text", 6), 3);
        QCOMPARE(detail::charsForColumns(u"\ttext", 4), 1);
        QCOMPARE(detail::charsForColumns(u" text", 4), 1);
        QCOMPARE(detail::columnsOf(u"\t  "), 6);
        QVERIFY(detail::isBlank(u""));
        QVERIFY(detail::isBlank(u" \t "));
        QVERIFY(!detail::isBlank(u" x"));
        QCOMPARE(detail::blankOut(u"a😀\nb"), u"   \n "_s);
    }

    void safeDecode()
    {
        QCOMPARE(detail::safeDecode(u"Za%C5%BC%C3%B3%C5%82%C4%87"), u"Zażółć"_s);
        QCOMPARE(detail::safeDecode(u"100%25%zz.md"), u"100%%zz.md"_s);
        QCOMPARE(detail::safeDecode(u"bad%C5 end"), u"bad%C5 end"_s);
        QCOMPARE(detail::safeDecode(u"no escapes"), u"no escapes"_s);
    }
};

QTEST_GUILESS_MAIN(TestTokens)
#include "tst_markdown_tokens.moc"
