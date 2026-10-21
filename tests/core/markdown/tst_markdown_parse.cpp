// parseNote and the block structure parser.
// Test cases ported from archive/web-v1 (parse / structure tests).
#include "markdown/parse.h"
#include "markdown/structure_p.h"

#include <QElapsedTimer>

#include <QTest>
#include <limits>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// A realistic note mixing Obsidian (frontmatter, Tasks emoji, callouts) and Logseq syntax.
const QString Fixture = uR"(---
title: Tydzień 42
tags: [projekt, "Dom/Remont"]
aliases:
  - Remont łazienki
  - Łazienka
created: 2026-10-14
related: "[[Budżet]]"
---
type:: plan
status:: [[W toku]]

# Remont łazienki #dom

Plan prac na ten tydzień. Zobacz [[Budżet#Koszty|koszty]] i [instrukcję](docs/Instrukcja%20montażu.md#Krok%202). ^plan

## Zadania

- [ ] Kupić płytki 📅 2026-10-16 ⏫ #zakupy/budowlane
  - [x] Zmierzyć ścianę ✅ 2026-10-13
  - [/] Wybrać fugę ⏳ 2026-10-15 10:00 🛫 2026-10-14
  - [-] Zamówić kuriera ❌ 2026-10-12
  - [>] Zapytać sąsiada
- [ ] Zapłacić fakturę 📅 2026-10-20 🔁 co miesiąc ⏰! 2026-10-20 08:00 ^faktura
  kwota:: 1200 zł
  tags:: [[Finanse]], #rachunki
- TODO [#B] Zadzwonić do hydraulika
  SCHEDULED: <2026-10-15 Thu 09:00 .+1w>
  id:: 64f0a1b2-0000-4c4c-8888-123456789abc
- LATER Przeczytać ((faktura)) i !((plan))
- Spotkanie z ekipą ⏰ 2026-10-15 (@2026-10-15 07:30)

> [!warning] Uwaga
> - [ ] Wyłączyć wodę ⏰ 2026-10-16 07:00
> Tekst z #ostrzeżenie

```calc
płytki = 12 * 45 zł
- [ ] nie zadanie #nie-tag [[Nie link]] ⏰ 2026-10-15 10:00
```

| Pozycja | Cena |
|---|---|
| [[Płytki]] | =120*12 |
^koszty

%% Ukryty [[Komentarz]] #ukryty %%
Odnośnik https://sklep.example.com/plytki?id=5. `#kod` i \#escaped.

---
Stopka
)"_s;

const QString Path = u"Projekty/Remont łazienki.md"_s;

QString kindName(BlockKind k)
{
    switch (k) {
    case BlockKind::ListItem: return u"listItem"_s;
    case BlockKind::Paragraph: return u"paragraph"_s;
    case BlockKind::Heading: return u"heading"_s;
    case BlockKind::Code: return u"code"_s;
    case BlockKind::Table: return u"table"_s;
    case BlockKind::Quote: return u"quote"_s;
    case BlockKind::Callout: return u"callout"_s;
    case BlockKind::Rule: return u"hr"_s;
    case BlockKind::Html: return u"html"_s;
    case BlockKind::Frontmatter: return u"frontmatter"_s;
    }
    return {};
}

QString statusName(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

QString priorityName(Priority p)
{
    switch (p) {
    case Priority::None: return QString();
    case Priority::Lowest: return u"lowest"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::High: return u"high"_s;
    case Priority::Highest: return u"highest"_s;
    }
    return {};
}

QString spec(const std::optional<DateSpec> &s)
{
    return s ? s->toString() : QString();
}

QString date(const std::optional<QDate> &d)
{
    return d ? d->toString(u"yyyy-MM-dd"_s) : QString();
}

/// "status|marker|text|line|due|scheduled|start|created|done|cancelled|recurrence|priority"
QString describe(const Task &t)
{
    return QStringList{statusName(t.status),    t.marker,        t.text,
                       QString::number(t.line), spec(t.due),     spec(t.scheduled),
                       spec(t.start),           date(t.created), date(t.doneDate),
                       date(t.cancelledDate),   t.recurrence,    priorityName(t.priority)}
            .join(u'|');
}

QStringList describeTasks(const ParsedNote &n)
{
    QStringList out;
    for (const Task &t : n.tasks)
        out.append(describe(t));
    return out;
}

/// "at|alarm|text|line|recurrence"
QString describe(const Reminder &r)
{
    return QStringList{r.at.toString(), r.alarm ? u"alarm"_s : QString(), r.text, QString::number(r.line), r.recurrence}
            .join(u'|');
}

QStringList describeReminders(const ParsedNote &n)
{
    QStringList out;
    for (const Reminder &r : n.reminders)
        out.append(describe(r));
    return out;
}

QString linkKind(LinkKind k)
{
    switch (k) {
    case LinkKind::Wiki: return u"wiki"_s;
    case LinkKind::Embed: return u"embed"_s;
    case LinkKind::BlockRef: return u"blockref"_s;
    case LinkKind::BlockEmbed: return u"blockembed"_s;
    case LinkKind::Markdown: return u"markdown"_s;
    case LinkKind::Url: return u"url"_s;
    }
    return {};
}

const Block *byLine(const ParsedNote &n, int line)
{
    for (const Block &b : n.blocks) {
        if (b.line == line)
            return &b;
    }
    return nullptr;
}

QStringList rootLabels(const ParsedNote &n)
{
    QStringList out;
    for (const Block &b : n.blocks) {
        if (b.parent < 0)
            out.append(kindName(b.kind) + u'@' + QString::number(b.line));
    }
    return out;
}

int lineCount(const QString &s)
{
    return int(text::splitLines(s).size());
}

/// Structural invariants of a parsed note (checked on every fuzz input too).
bool checkInvariants(const ParsedNote &note, const QString &content, QString *why)
{
    const int lines = lineCount(content);
    for (int i = 0; i < note.blocks.size(); ++i) {
        const Block &b = note.blocks[i];
        if (b.line < 0 || b.line > b.ownEndLine || b.ownEndLine > b.endLine || b.endLine >= lines) {
            *why = u"line range of block %1"_s.arg(i);
            return false;
        }
        if (b.parent >= 0) {
            const Block &p = note.blocks[b.parent];
            if (b.parent >= i || b.line < p.line || b.endLine > p.endLine || b.depth <= p.depth
                || !p.children.contains(i)) {
                *why = u"parent of block %1"_s.arg(i);
                return false;
            }
        }
        for (int c : b.children) {
            if (c <= i || note.blocks[c].parent != i) {
                *why = u"children of block %1"_s.arg(i);
                return false;
            }
        }
    }
    for (const Link &l : note.links) {
        if (l.from >= l.to || l.to > content.size()) {
            *why = u"link range"_s;
            return false;
        }
    }
    for (auto it = note.blockIds.constBegin(); it != note.blockIds.constEnd(); ++it) {
        if (*it < 0 || *it >= note.blocks.size() || note.blocks[*it].id != it.key()) {
            *why = u"block id "_s + it.key();
            return false;
        }
    }
    return true;
}

// --- structure helpers ------------------------------------------------------------------------

QString nodeLabel(const detail::Structure &st, int n)
{
    const detail::Node &node = st.nodes[n];
    QString label = kindName(node.kind) + u'@' + QString::number(node.line) + u'-' + QString::number(node.endLine);
    if (node.ownEndLine != node.endLine)
        label += u'(' + QString::number(node.ownEndLine) + u')';
    label += u":d"_s + QString::number(node.depth);
    if (!node.children.isEmpty()) {
        QStringList kids;
        for (int c : node.children)
            kids.append(nodeLabel(st, c));
        label += u'[' + kids.join(u',') + u']';
    }
    return label;
}

QString outline(const QString &src)
{
    const detail::Structure st = detail::parseStructure(src);
    QStringList roots;
    for (int r : st.roots)
        roots.append(nodeLabel(st, r));
    return roots.join(u", ");
}

QList<int> ownLines(const detail::Node &n)
{
    QList<int> out;
    for (const detail::OwnLine &l : n.own)
        out.append(l.view.line);
    return out;
}

QString roles(const detail::Node &n)
{
    QStringList out;
    for (const detail::OwnLine &l : n.own) {
        switch (l.role) {
        case detail::LineRole::Text: out.append(u"text"_s); break;
        case detail::LineRole::Code: out.append(u"code"_s); break;
        case detail::LineRole::Fence: out.append(u"fence"_s); break;
        case detail::LineRole::Blank: out.append(u"blank"_s); break;
        case detail::LineRole::Meta: out.append(u"meta"_s); break;
        }
    }
    return out.join(u',');
}

} // namespace

class TestParse : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- realistic mixed note ------------------------------------------------------------------

    void fixtureTitleFrontmatterProperties()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(note.title, u"Remont łazienki"_s);
        QCOMPARE(note.path, Path);
        const QVariantMap fm{{u"title"_s, u"Tydzień 42"_s},
                             {u"tags"_s, QVariantList{u"projekt"_s, u"Dom/Remont"_s}},
                             {u"aliases"_s, QVariantList{u"Remont łazienki"_s, u"Łazienka"_s}},
                             {u"created"_s, u"2026-10-14"_s},
                             {u"related"_s, u"[[Budżet]]"_s}};
        QCOMPARE(note.frontmatter, fm);
        QCOMPARE(note.properties.size(), 7);
        QCOMPARE(note.properties.value(u"title"_s), QVariant(u"Tydzień 42"_s));
        QCOMPARE(note.properties.value(u"tags"_s).toStringList(), QStringList({u"projekt"_s, u"Dom/Remont"_s}));
        QCOMPARE(note.properties.value(u"aliases"_s).toStringList(),
                 QStringList({u"Remont łazienki"_s, u"Łazienka"_s}));
        QCOMPARE(note.properties.value(u"created"_s), QVariant(u"2026-10-14"_s));
        QCOMPARE(note.properties.value(u"related"_s), QVariant(u"[[Budżet]]"_s));
        QCOMPARE(note.properties.value(u"type"_s), QVariant(u"plan"_s));
        QCOMPARE(note.properties.value(u"status"_s).toStringList(), QStringList({u"W toku"_s}));
        QCOMPARE(note.aliases, QStringList({u"Remont łazienki"_s, u"Łazienka"_s}));
        QVERIFY(!note.journalDate);
    }

    void fixtureTags()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(note.tags, QStringList({u"projekt"_s, u"dom/remont"_s, u"dom"_s, u"zakupy/budowlane"_s, u"rachunki"_s,
                                         u"finanse"_s, u"ostrzeżenie"_s}));
    }

    void fixtureLinks()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QStringList summary;
        for (const Link &l : note.links)
            summary.append(linkKind(l.kind) + u'|' + l.target + u'|' + (l.heading.isEmpty() ? l.blockId : l.heading)
                           + u'|' + l.display);
        QCOMPARE(summary, QStringList({u"wiki|Budżet||"_s, u"wiki|W toku||"_s, u"wiki|Budżet|Koszty|koszty"_s,
                                       u"markdown|docs/Instrukcja montażu.md|Krok 2|instrukcję"_s, u"wiki|Finanse||"_s,
                                       u"blockref|faktura|faktura|"_s, u"blockembed|plan|plan|"_s, u"wiki|Płytki||"_s,
                                       u"url|https://sklep.example.com/plytki?id=5||"_s}));
        const QStringList lines = Fixture.split(u'\n');
        for (const Link &l : note.links) {
            const QString src = Fixture.sliced(l.from, l.to - l.from);
            QVERIFY(!src.isEmpty());
            QVERIFY2(lines[l.line].contains(src), qPrintable(src));
        }
        QCOMPARE(note.links.first().line, 7);
    }

    void fixtureOutline()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(rootLabels(note),
                 QStringList({u"frontmatter@0"_s, u"frontmatter@9"_s, u"heading@12"_s, u"paragraph@14"_s,
                              u"heading@16"_s, u"listItem@18"_s, u"listItem@23"_s, u"listItem@26"_s, u"listItem@29"_s,
                              u"listItem@30"_s, u"callout@32"_s, u"code@36"_s, u"table@41"_s, u"paragraph@47"_s,
                              u"hr@49"_s, u"paragraph@50"_s}));
        const Block *first = byLine(note, 18);
        QVERIFY(first);
        QCOMPARE(first->ownEndLine, 18);
        QCOMPARE(first->endLine, 22);
        QCOMPARE(first->depth, 0);
        QStringList kids;
        for (int c : first->children)
            kids.append(QString::number(note.blocks[c].line) + u':' + QString::number(note.blocks[c].depth));
        QCOMPARE(kids, QStringList({u"19:1"_s, u"20:1"_s, u"21:1"_s, u"22:1"_s}));
        for (int i = 1; i < note.blocks.size(); ++i)
            QVERIFY(note.blocks[i - 1].line <= note.blocks[i].line);
        QString why;
        QVERIFY2(checkInvariants(note, Fixture, &why), qPrintable(why));
    }

    void fixtureBlockText()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(byLine(note, 12)->text, u"Remont łazienki #dom"_s);
        QCOMPARE(
                byLine(note, 14)->text,
                u"Plan prac na ten tydzień. Zobacz [[Budżet#Koszty|koszty]] i [instrukcję](docs/Instrukcja%20montażu.md#Krok%202)."_s);
        QCOMPARE(byLine(note, 18)->text, u"Kupić płytki #zakupy/budowlane"_s);
        QCOMPARE(byLine(note, 23)->text, u"Zapłacić fakturę"_s);
        QCOMPARE(byLine(note, 26)->text, u"Zadzwonić do hydraulika"_s);
        QCOMPARE(byLine(note, 30)->text, u"Spotkanie z ekipą"_s);
        QCOMPARE(byLine(note, 32)->text, u"Uwaga"_s);
        QCOMPARE(byLine(note, 36)->text,
                 u"płytki = 12 * 45 zł\n- [ ] nie zadanie #nie-tag [[Nie link]] ⏰ 2026-10-15 10:00"_s);
        QCOMPARE(byLine(note, 36)->lang, u"calc"_s);
        QCOMPARE(byLine(note, 41)->text, u"| Pozycja | Cena |\n|---|---|\n| [[Płytki]] | =120*12 |"_s);
        QCOMPARE(byLine(note, 23)->raw, Fixture.split(u'\n').mid(23, 3).join(u'\n'));
    }

    void fixtureTasks()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(describeTasks(note),
                 QStringList({
                         u"todo| |Kupić płytki #zakupy/budowlane|18|2026-10-16|||||||high"_s,
                         u"done|x|Zmierzyć ścianę|19|||||2026-10-13|||"_s,
                         u"doing|/|Wybrać fugę|20||2026-10-15 10:00|2026-10-14|||||"_s,
                         u"cancelled|-|Zamówić kuriera|21||||||2026-10-12||"_s,
                         u"deferred|>|Zapytać sąsiada|22||||||||"_s,
                         u"todo| |Zapłacić fakturę|23|2026-10-20||||||every month|"_s,
                         u"todo|TODO|Zadzwonić do hydraulika|26||2026-10-15 09:00|||||every week when done|medium"_s,
                         u"todo|LATER|Przeczytać ((faktura)) i !((plan))|29||||||||"_s,
                         u"todo| |Wyłączyć wodę|33||||||||"_s,
                 }));
    }

    void fixtureReminders()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(
                describeReminders(note),
                QStringList({u"2026-10-20 08:00|alarm|Zapłacić fakturę|23|every month"_s,
                             u"2026-10-15 09:00||Spotkanie z ekipą|30|"_s, u"2026-10-15 07:30||Spotkanie z ekipą|30|"_s,
                             u"2026-10-16 07:00||Wyłączyć wodę\nTekst z #ostrzeżenie|33|"_s}));
    }

    void fixturePropertiesAndIds()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QVERIFY(note.blockIds.contains(u"faktura"_s));
        const Block &faktura = note.blocks[note.blockIds.value(u"faktura"_s)];
        QCOMPARE(faktura.line, 23);
        QCOMPARE(faktura.properties.size(), 2);
        QCOMPARE(faktura.properties.value(u"kwota"_s), QVariant(u"1200 zł"_s));
        QCOMPARE(faktura.properties.value(u"tags"_s).toStringList(), QStringList({u"Finanse"_s, u"rachunki"_s}));
        QCOMPARE(faktura.tags, QStringList({u"rachunki"_s, u"finanse"_s}));
        QCOMPARE(note.blocks[note.blockIds.value(u"plan"_s)].kind, BlockKind::Paragraph);
        QCOMPARE(note.blocks[note.blockIds.value(u"64f0a1b2-0000-4c4c-8888-123456789abc"_s)].line, 26);
        QCOMPARE(note.blocks[note.blockIds.value(u"koszty"_s)].kind, BlockKind::Table);
        QStringList ids = note.blockIds.keys();
        ids.sort();
        QCOMPARE(ids, QStringList({u"64f0a1b2-0000-4c4c-8888-123456789abc"_s, u"faktura"_s, u"koszty"_s, u"plan"_s}));
        const Block *callout = byLine(note, 32);
        QCOMPARE(callout->properties.size(), 1);
        QCOMPARE(callout->properties.value(u"callouttype"_s), QVariant(u"warning"_s));
    }

    void fixtureHeadings()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QCOMPARE(note.headings.size(), 2);
        QCOMPARE(note.headings[0].level, 1);
        QCOMPARE(note.headings[0].text, u"Remont łazienki #dom"_s);
        QCOMPARE(note.headings[0].line, 12);
        QCOMPARE(note.headings[1].level, 2);
        QCOMPARE(note.headings[1].text, u"Zadania"_s);
        QCOMPARE(note.headings[1].line, 16);
    }

    void fixtureCrlf()
    {
        const ParsedNote note = parseNote(Path, Fixture);
        QString crlf = Fixture;
        crlf.replace(u"\n"_s, u"\r\n"_s);
        const ParsedNote n2 = parseNote(Path, crlf);
        QCOMPARE(describeTasks(n2), describeTasks(note));
        QCOMPARE(describeReminders(n2), describeReminders(note));
        QCOMPARE(n2.tags, note.tags);
        QCOMPARE(n2.blocks.size(), note.blocks.size());
        for (int i = 0; i < note.blocks.size(); ++i) {
            QCOMPARE(n2.blocks[i].kind, note.blocks[i].kind);
            QCOMPARE(n2.blocks[i].line, note.blocks[i].line);
            QCOMPARE(n2.blocks[i].endLine, note.blocks[i].endLine);
            QCOMPARE(n2.blocks[i].text, note.blocks[i].text);
        }
        QCOMPARE(n2.links.size(), note.links.size());
        for (int i = 0; i < note.links.size(); ++i) {
            QCOMPARE(crlf.sliced(n2.links[i].from, n2.links[i].to - n2.links[i].from),
                     Fixture.sliced(note.links[i].from, note.links[i].to - note.links[i].from));
            QCOMPARE(n2.links[i].line, note.links[i].line);
        }
        QString why;
        QVERIFY2(checkInvariants(n2, crlf, &why), qPrintable(why));
    }

    // --- focused features ----------------------------------------------------------------------

    void emptyNotes()
    {
        for (const QString &s : {QString(), u"\n\n"_s, u"   \r\n\t"_s}) {
            const ParsedNote n = parseNote(u"Pusta.md"_s, s);
            QVERIFY(n.blocks.isEmpty());
            QCOMPARE(n.title, u"Pusta"_s);
            QVERIFY(n.tags.isEmpty());
        }
    }

    void journalDate()
    {
        QCOMPARE(parseNote(u"journals/2026-10-14.md"_s, u"- x").journalDate, QDate(2026, 10, 14));
        VaultConfig cfg;
        cfg.journalsFolder = u"Dziennik"_s;
        cfg.journalFileFormat = u"yyyy_MM_dd"_s;
        QCOMPARE(parseNote(u"Dziennik/2026_10_14.md"_s, u"", cfg).journalDate, QDate(2026, 10, 14));
        QVERIFY(!parseNote(u"journals/2026_10_14.md"_s, u"", cfg).journalDate);
    }

    void defaultReminderTime()
    {
        VaultConfig cfg;
        cfg.defaultReminderTime = QTime(7, 15);
        QCOMPARE(parseNote(u"a.md"_s, u"x ⏰ 2026-10-15", cfg).reminders.first().at.toString(), u"2026-10-15 07:15"_s);
        VaultConfig bad;
        bad.defaultReminderTime = QTime();
        QCOMPARE(parseNote(u"a.md"_s, u"x ⏰ 2026-10-15", bad).reminders.first().at.toString(), u"2026-10-15 09:00"_s);
    }

    void remindersAnywhereButCode()
    {
        const QString src =
                u"# Spotkanie ⏰ 2026-10-15 10:00\n\nAkapit\ndruga linia ⏰! 2026-10-16 06:30 🔁 every day\n\n`⏰ 2026-10-17 09:00`\n\n```\n⏰ 2026-10-18 09:00\n```"_s;
        QCOMPARE(describeReminders(parseNote(u"a.md"_s, src)),
                 QStringList({u"2026-10-15 10:00||Spotkanie|0|"_s,
                              u"2026-10-16 06:30|alarm|Akapit\ndruga linia|3|every day"_s}));
    }

    // What starts inside a formula is part of the formula for the index as in the editor
    // (SYNTAX §11): tags, links, task dates and reminders there do not count; outside they do.
    void formulasHideIndexedSyntax_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<QStringList>("links");
        QTest::addColumn<QString>("taskText");
        QTest::addColumn<QString>("due");
        QTest::addColumn<QStringList>("reminders");
        QTest::newRow("tags and links") << u"Wzór $x \\in A #B$ i $[[Strona]]$, poza nim #pilne i [[Budżet]]"_s
                                        << QStringList({u"pilne"_s}) << QStringList({u"Budżet"_s}) << QString()
                                        << QString() << QStringList();
        QTest::newRow("task date inside")
                << u"- [ ] Zapłacić $📅 2026-10-30$ do końca"_s << QStringList() << QStringList()
                << u"Zapłacić $📅 2026-10-30$ do końca"_s << QString() << QStringList();
        QTest::newRow("task date and reminder outside")
                << u"- [ ] Zapłacić $x$ 📅 2026-10-30 ⏰ 2026-10-22 09:00"_s << QStringList() << QStringList()
                << u"Zapłacić $x$"_s << u"2026-10-30"_s << QStringList({u"2026-10-22 09:00"_s});
        QTest::newRow("reminder inside") << u"Spotkanie $⏰ 2026-10-22 09:00$ #ważne"_s << QStringList({u"ważne"_s})
                                         << QStringList() << QString() << QString() << QStringList();
        // The editor shows a list item with its blank lines and code: they end an inline formula.
        QTest::newRow("blank line in an item") << u"- a $x\n\n  #tag$"_s << QStringList({u"tag"_s}) << QStringList()
                                               << QString() << QString() << QStringList();
        QTest::newRow("code in an item") << u"- a $x\n  ```\n  kod\n  ```\n  #tag$"_s << QStringList({u"tag"_s})
                                         << QStringList() << QString() << QString() << QStringList();
        // Table cells are shown one by one: no formula spans cells or rows.
        QTest::newRow("table cells")
                << u"| Usługa | Cena |\n|---|---|\n| [[Netflix]] | $15 |\n| [[Spotify]] #muzyka | 10$ |\n| $x #y$ | =A1 |"_s
                << QStringList({u"muzyka"_s}) << QStringList({u"Netflix"_s, u"Spotify"_s}) << QString() << QString()
                << QStringList();
        QTest::newRow("display formula over a blank line")
                << u"- $$a\n\n  #b [[Ukryta]]$$ #c"_s << QStringList({u"c"_s}) << QStringList() << QString()
                << QString() << QStringList();
    }

    void formulasHideIndexedSyntax()
    {
        QFETCH(QString, src);
        QFETCH(QStringList, tags);
        QFETCH(QStringList, links);
        QFETCH(QString, taskText);
        QFETCH(QString, due);
        QFETCH(QStringList, reminders);
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.tags, tags);
        QStringList targets;
        for (const Link &l : n.links)
            targets.append(l.target);
        QCOMPARE(targets, links);
        QCOMPARE(n.tasks.isEmpty() ? QString() : n.tasks.first().text, taskText);
        QCOMPARE(n.tasks.isEmpty() ? QString() : spec(n.tasks.first().due), due);
        QStringList at;
        for (const Reminder &r : n.reminders)
            at.append(r.at.toString());
        QCOMPARE(at, reminders);
    }

    void formulaLinesAreNoProperties()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"$$\nf:: x \\cdot y\n$$\n\nklucz:: wartość"_s);
        QCOMPARE(n.blocks.size(), 2);
        QVERIFY(n.blocks[0].properties.isEmpty());
        QCOMPARE(n.blocks[0].text, u"$$\nf:: x \\cdot y\n$$"_s);
        QCOMPARE(n.blocks[1].properties.value(u"klucz"_s).toString(), u"wartość"_s);
    }

    void polishAndNestedTags()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"Lista #zakupy/spożywcze i #Żółw oraz #[[Lista życzeń]]");
        QCOMPARE(n.tags, QStringList({u"zakupy/spożywcze"_s, u"żółw"_s, u"lista życzeń"_s}));
        QCOMPARE(n.blocks.first().tags, n.tags);
    }

    void frontmatterTagForms()
    {
        QCOMPARE(parseNote(u"a.md"_s, u"---\ntags: a, b c\n---\n").tags, QStringList({u"a"_s, u"b"_s, u"c"_s}));
        QCOMPARE(parseNote(u"a.md"_s, u"---\ntags: \"#a #b\"\n---\n").tags, QStringList({u"a"_s, u"b"_s}));
        QCOMPARE(parseNote(u"a.md"_s, u"---\ntags: a, #b\n---\n").tags, QStringList({u"a"_s})); // " #" starts a comment
        QCOMPARE(parseNote(u"a.md"_s, u"---\ntag: [X, \"#y\"]\n---\n").tags, QStringList({u"x"_s, u"y"_s}));
        QCOMPARE(parseNote(u"a.md"_s, u"---\ntags:\n  - Praca\n  - praca\n---\n").tags, QStringList({u"praca"_s}));
    }

    void aliases()
    {
        QCOMPARE(parseNote(u"a.md"_s, u"---\nalias: Jeden, Dwa\n---\n").aliases, QStringList({u"Jeden"_s, u"Dwa"_s}));
        QCOMPARE(parseNote(u"a.md"_s, u"alias:: [[Trzy]], Cztery\n\n- x").aliases,
                 QStringList({u"Trzy"_s, u"Cztery"_s}));
    }

    void logseqPageProperties()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"tags:: [[Książki]], fantastyka\nautor:: [[Lem]]\n\n- Solaris");
        QCOMPARE(n.properties.size(), 2);
        QCOMPARE(n.properties.value(u"tags"_s).toStringList(), QStringList({u"Książki"_s, u"fantastyka"_s}));
        QCOMPARE(n.properties.value(u"autor"_s).toStringList(), QStringList({u"Lem"_s}));
        QCOMPARE(n.tags, QStringList({u"książki"_s, u"fantastyka"_s}));
        QCOMPARE(n.blocks.first().kind, BlockKind::Frontmatter);
        QStringList targets;
        for (const Link &l : n.links)
            targets.append(l.target);
        QCOMPARE(targets, QStringList({u"Książki"_s, u"Lem"_s}));
        // Review regression: a byte order mark before the page properties (Windows editors).
        const ParsedNote bom = parseNote(u"a.md"_s, u"\uFEFFtags:: [[Książki]]\nautor:: Lem\n\n- Solaris"_s);
        QCOMPARE(bom.properties.value(u"autor"_s).toString(), u"Lem"_s);
        QCOMPARE(bom.tags, QStringList({u"książki"_s}));
        QCOMPARE(bom.blocks.first().kind, BlockKind::Frontmatter);
        QCOMPARE(bom.blocks.size(), 2);
    }

    void propertyValues()
    {
        const ParsedNote n = parseNote(
                u"a.md"_s,
                u"- item\n  a:: [[X]], #y\n  b:: plain, text\n  c::\n  D-e_f:: 5\n  std::vector is not a property");
        const Properties &p = n.blocks.first().properties;
        QCOMPARE(p.size(), 4);
        QCOMPARE(p.value(u"a"_s).toStringList(), QStringList({u"X"_s, u"y"_s}));
        QCOMPARE(p.value(u"b"_s), QVariant(u"plain, text"_s));
        QCOMPARE(p.value(u"c"_s), QVariant(QString()));
        QCOMPARE(p.value(u"d-e_f"_s), QVariant(u"5"_s));
        QCOMPARE(n.blocks.first().text, u"item\nstd::vector is not a property"_s);
    }

    void blockIdsFirstOrLastLine()
    {
        const ParsedNote n = parseNote(
                u"a.md"_s, u"- item ^first\n  second line\n\npara line 1\npara line 2 ^last\n\n## Head ## ^h1");
        QCOMPARE(n.blockIds.size(), 3);
        QCOMPARE(n.blocks[n.blockIds.value(u"first"_s)].text, u"item\nsecond line"_s);
        QCOMPARE(n.blocks[n.blockIds.value(u"last"_s)].text, u"para line 1\npara line 2"_s);
        QCOMPARE(n.blocks[n.blockIds.value(u"h1"_s)].text, u"Head"_s);
        QCOMPARE(n.headings.first().level, 2);
        QCOMPARE(n.headings.first().text, u"Head"_s);
        QCOMPARE(n.headings.first().line, 6);
    }

    void duplicateIdsKeepFirst()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"a ^dup\n\nb ^dup");
        QCOMPARE(n.blocks[n.blockIds.value(u"dup"_s)].text, u"a"_s);
    }

    void logseqDrawersAndPlanning()
    {
        const QString src =
                u"- DONE Zrobione\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00]--[2026-10-14 Wed 11:00] =>  01:00:00\n  :END:\n  DEADLINE: <2026-10-20 Tue>\n  notatka"_s;
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.blocks.first().text, u"Zrobione\nnotatka"_s);
        QCOMPARE(n.tasks.first().status, TaskStatus::Done);
        QCOMPARE(n.tasks.first().marker, u"DONE"_s);
        QCOMPARE(spec(n.tasks.first().due), u"2026-10-20"_s);
        QVERIFY(n.links.isEmpty());
    }

    void emojiBeatsPlanning()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- TODO [#A] x 📅 2026-10-01 🔽\n  DEADLINE: <2026-10-20 Tue +1m>");
        QCOMPARE(spec(n.tasks.first().due), u"2026-10-01"_s);
        QCOMPARE(n.tasks.first().priority, Priority::Low);
        QCOMPARE(n.tasks.first().recurrence, u"every month"_s);
    }

    void onlyListItemsAreTasks()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"[ ] not a task\n\nTODO not a task\n\n1. [x] ordered task");
        QCOMPARE(n.tasks.size(), 1);
        QCOMPARE(n.tasks.first().text, u"ordered task"_s);
    }

    void unknownCheckboxCharacters()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- [?] question\n- [X] upper");
        QCOMPARE(n.tasks.size(), 2);
        QCOMPARE(n.tasks[0].status, TaskStatus::Todo);
        QCOMPARE(n.tasks[0].marker, u"?"_s);
        QCOMPARE(n.tasks[1].status, TaskStatus::Done);
        QCOMPARE(n.tasks[1].marker, u"X"_s);
    }

    void hiddenTasksInComments()
    {
        const ParsedNote n =
                parseNote(u"a.md"_s, u"tekst\n\n%%\n- [ ] ukryte ⏰ 2026-10-15 09:00 [[X]]\n%%\n\n- [ ] widoczne");
        QCOMPARE(n.tasks.size(), 1);
        QCOMPARE(n.tasks.first().text, u"widoczne"_s);
        QVERIFY(n.reminders.isEmpty());
        QVERIFY(n.links.isEmpty());
    }

    void inlineMarkdownKeptAndCodeSpansRespected()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- [ ] **Ważne** `📅 2026-10-15` [[Link]] 📅 2026-10-16");
        QCOMPARE(n.tasks.first().text, u"**Ważne** `📅 2026-10-15` [[Link]]"_s);
        QCOMPARE(spec(n.tasks.first().due), u"2026-10-16"_s);
    }

    void tasksInCallouts()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"> [!todo] Dziś\n> - [ ] a\n>   - [x] b ✅ 2026-10-14");
        QCOMPARE(n.tasks.size(), 2);
        QCOMPARE(n.tasks[0].text, u"a"_s);
        QCOMPARE(n.tasks[0].line, 1);
        QCOMPARE(n.tasks[1].text, u"b"_s);
        QCOMPARE(n.tasks[1].line, 2);
        QCOMPARE(n.tasks[1].status, TaskStatus::Done);
        QList<int> depths;
        for (const Block &b : n.blocks)
            depths.append(b.depth);
        QCOMPARE(depths, QList<int>({0, 1, 2}));
    }

    void frontmatterLinks()
    {
        const QString src = u"---\nup: \"[[Rodzic]]\"\nlist: [[Dziecko]]\n---\n"_s;
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.links.size(), 2);
        QCOMPARE(n.links[0].target, u"Rodzic"_s);
        QCOMPARE(n.links[0].line, 1);
        QCOMPARE(src.sliced(n.links[0].from, n.links[0].to - n.links[0].from), u"[[Rodzic]]"_s);
        QCOMPARE(n.links[1].target, u"Dziecko"_s);
        QCOMPARE(n.links[1].line, 2);
        QCOMPARE(n.properties.value(u"list"_s).toStringList(), QStringList({u"[[Dziecko]]"_s}));
    }

    void blockAtLineInnermost()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- a\n  - b\n  more a\n\npara");
        QCOMPARE(n.blocks[blockAtLine(n, 1)].text, u"b"_s);
        QCOMPARE(n.blocks[blockAtLine(n, 2)].text, u"a\nmore a"_s);
        QCOMPARE(blockAtLine(n, 3), -1);
        QCOMPARE(n.blocks[blockAtLine(n, 4)].kind, BlockKind::Paragraph);
        QCOMPARE(blockAtLine(n, 99), -1);
    }

    // --- robustness ----------------------------------------------------------------------------

    void fuzz()
    {
        const QStringList pieces{u"- "_s,
                                 u"  "_s,
                                 u"\t"_s,
                                 u"[ ]"_s,
                                 u"[x]"_s,
                                 u"#"_s,
                                 u"##"_s,
                                 u">"_s,
                                 u"[!note]"_s,
                                 u"```"_s,
                                 u"~~~"_s,
                                 u"|"_s,
                                 u"---"_s,
                                 u"%%"_s,
                                 u"`"_s,
                                 u"[["_s,
                                 u"]]"_s,
                                 u"(("_s,
                                 u"))"_s,
                                 u"^id"_s,
                                 u"::"_s,
                                 u"TODO "_s,
                                 u"📅 2026-10-15"_s,
                                 u"⏰ 2026-10-15 09:00"_s,
                                 u"🔁 every day"_s,
                                 u"<div>"_s,
                                 u"\n"_s,
                                 u"\n"_s,
                                 u"\r\n"_s,
                                 u"zażółć"_s,
                                 u"x"_s,
                                 u" "_s,
                                 u"1. "_s,
                                 u"\\"_s,
                                 u"<!--"_s,
                                 u"-->"_s,
                                 u"$"_s,
                                 u"SCHEDULED: <2026-10-15 Thu +1d>"_s,
                                 u"key:: [[v]]"_s,
                                 u":LOGBOOK:"_s};
        quint32 seed = 42;
        const auto rnd = [&](quint32 n) {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return seed % n;
        };
        for (int k = 0; k < 400; ++k) {
            QString s;
            const quint32 len = 1 + rnd(120);
            for (quint32 i = 0; i < len; ++i)
                s += pieces[rnd(quint32(pieces.size()))];
            const ParsedNote note = parseNote(u"fuzz.md"_s, s);
            QString why;
            QVERIFY2(checkInvariants(note, s, &why), qPrintable(why + u": "_s + s));
        }
    }

    void binaryAndHugeLines()
    {
        QString binary;
        for (int i = 0; i < 1000; ++i)
            binary += u"\u0000�"_s + QChar(0xD800);
        parseNote(u"a.md"_s, binary);
        parseNote(u"a.md"_s, QString(100000, u'#'));
        QString brackets;
        for (int i = 0; i < 50000; ++i)
            brackets += u"[["_s;
        parseNote(u"a.md"_s, brackets);
        QVERIFY(true);
    }

    void deepOutline()
    {
        QStringList lines;
        for (int i = 0; i < 3000; ++i)
            lines.append(QString(i, u' ') + u"- x"_s);
        const QString src = lines.join(u'\n');
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.blocks.size(), 3000);
        QCOMPARE(n.blocks.first().endLine, 2999);
        QCOMPARE(n.blocks.last().depth, 2999);
    }

    void performance()
    {
        const QStringList chunk{u"## Sekcja"_s,
                                u"- [ ] Zadanie [[Strona]] #tag/pod 📅 2026-10-15 ⏫ ^id{n}"_s,
                                u"  - podpunkt z `kodem` i https://example.com/x"_s,
                                u"  - TODO Logseq [#A] zadanie"_s,
                                u"    SCHEDULED: <2026-10-15 Thu 09:00 .+1w>"_s,
                                u"Akapit z ((abc123)) i [link](Strona%20B.md) ⏰ 2026-10-15 09:00"_s,
                                u"> [!note] Uwaga"_s,
                                u"> - [x] w callout ✅ 2026-10-14"_s,
                                u"```js"_s,
                                u"const x = 1; // [[nie]]"_s,
                                u"```"_s,
                                u"| a | b |"_s,
                                u"|---|---|"_s,
                                u"| 1 | =A1 |"_s,
                                QString(),
                                u"key:: value"_s,
                                QString()};
        QStringList lines;
        for (int n = 0; lines.size() < 5000; ++n) {
            for (const QString &l : chunk) {
                QString line = l;
                lines.append(line.replace(u"{n}"_s, QString::number(n)));
            }
        }
        const QString src = lines.mid(0, 5000).join(u'\n');
        qint64 best = std::numeric_limits<qint64>::max();
        ParsedNote note;
        for (int k = 0; k < 3; ++k) {
            QElapsedTimer t;
            t.start();
            note = parseNote(u"perf.md"_s, src);
            best = std::min(best, t.elapsed());
        }
        QVERIFY(note.tasks.size() > 800);
        QVERIFY(note.blocks.size() > 2500);
        // Generous bound: the tests run unoptimised (Debug) and possibly on a busy machine.
        QVERIFY2(best < 1500, qPrintable(QString::number(best)));
    }

    // --- review regressions --------------------------------------------------------------------

    void tagAfterQuoteMarker()
    {
        QCOMPARE(parseNote(u"a.md"_s, u">#cytat tekst").tags, QStringList({u"cytat"_s}));
        QCOMPARE(parseNote(u"a.md"_s, u"> [!note]\n>#uwaga").tags, QStringList({u"uwaga"_s}));
    }

    void tasksInPlainQuotes()
    {
        const QString src =
                u"Wstęp\n\n> Cytat #ważne\n> - [ ] zadanie 📅 2026-10-15 #praca\n>   - [x] pod ✅ 2026-10-14\n> ~~~\n> #nie [[Nie]] ⏰ 2026-10-15 09:00\n> ~~~\n\nPo"_s;
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.tasks.size(), 2);
        QCOMPARE(n.tasks[0].text, u"zadanie #praca"_s);
        QCOMPARE(n.tasks[0].line, 3);
        QCOMPARE(n.tasks[1].text, u"pod"_s);
        QCOMPARE(n.tasks[1].line, 4);
        QCOMPARE(n.tasks[1].status, TaskStatus::Done);
        QCOMPARE(spec(n.tasks[0].due), u"2026-10-15"_s);
        const Block *quote = byLine(n, 2);
        QCOMPARE(quote->kind, BlockKind::Quote);
        QCOMPARE(quote->endLine, 7);
        QCOMPARE(quote->ownEndLine, 2);
        QCOMPARE(quote->depth, 0);
        QVERIFY(quote->text.isEmpty());
        QStringList kids;
        for (int c : quote->children)
            kids.append(kindName(n.blocks[c].kind) + u'@' + QString::number(n.blocks[c].line) + u":d"_s
                        + QString::number(n.blocks[c].depth));
        QCOMPARE(kids, QStringList({u"paragraph@2:d1"_s, u"listItem@3:d1"_s, u"code@5:d1"_s}));
        QStringList tags = n.tags;
        tags.sort();
        QCOMPARE(tags, QStringList({u"praca"_s, u"ważne"_s}));
        QVERIFY(n.links.isEmpty());
        QVERIFY(n.reminders.isEmpty());
        QCOMPARE(n.blocks.last().kind, BlockKind::Paragraph);
        QCOMPARE(n.blocks.last().line, 9);
    }

    void quotesWithoutStructureAreLeaves()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"> linia 1\n> linia 2 ^cyt\n>\n> akapit 2");
        QCOMPARE(n.blocks.size(), 1);
        QCOMPARE(n.blocks.first().kind, BlockKind::Quote);
        QCOMPARE(n.blocks.first().endLine, 3);
        QCOMPARE(n.blocks.first().ownEndLine, 3);
        QVERIFY(n.blocks.first().children.isEmpty());
    }

    void commentInItemFenceDoesNotHideRest()
    {
        const QString src =
                u"- ```mermaid\n  %% komentarz diagramu\n  graph TD\n  ```\n- [ ] widoczne #t ⏰ 2026-10-15 08:00\n- koniec %%c%%"_s;
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.tasks.size(), 1);
        QCOMPARE(n.tasks.first().text, u"widoczne #t"_s);
        QCOMPARE(n.tags, QStringList({u"t"_s}));
        QCOMPARE(n.reminders.first().at.toString(), u"2026-10-15 08:00"_s);
        QCOMPARE(rootLabels(n), QStringList({u"listItem@0"_s, u"listItem@4"_s, u"listItem@5"_s}));
    }

    void commentInCalloutFenceDoesNotHideRest()
    {
        const QString src =
                u"> [!info] Diagram\n> ```mermaid\n> %% komentarz\n> graph TD\n> ```\n\n- [ ] zadanie po callout\n\n%% prawdziwy komentarz %%"_s;
        const ParsedNote n = parseNote(u"a.md"_s, src);
        QCOMPARE(n.tasks.size(), 1);
        QCOMPARE(n.tasks.first().text, u"zadanie po callout"_s);
    }

    void prototypeLikeKeys()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- a ^constructor\n- b ^toString\n  constructor:: 1\n  valueOf:: 2");
        QCOMPARE(n.blocks[n.blockIds.value(u"constructor"_s)].line, 0);
        QCOMPARE(n.blocks[n.blockIds.value(u"toString"_s)].line, 1);
        QCOMPARE(n.blocks[1].properties.value(u"constructor"_s), QVariant(u"1"_s));
        QCOMPARE(n.blocks[1].properties.value(u"valueof"_s), QVariant(u"2"_s));
    }

    void commentsNotInText()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- [ ] Zadzwonić %%numer w notatkach%% do mamy ⏰ 2026-10-15 "
                                                  u"09:00\n\nAkapit %%ukryty\nkomentarz%% widoczny `%%kod%%`");
        QCOMPARE(n.tasks.first().text, u"Zadzwonić do mamy"_s);
        QCOMPARE(n.reminders.first().text, u"Zadzwonić do mamy"_s);
        QCOMPARE(n.blocks[1].text, u"Akapit\nwidoczny `%%kod%%`"_s);
        QCOMPARE(n.blocks[1].raw, u"Akapit %%ukryty\nkomentarz%% widoczny `%%kod%%`"_s);
    }

    void commentContinuingOnNextLine()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- [x] zadanie ✅ 2026-10-14 %%notatka\n  ciąg%% dalej");
        QCOMPARE(n.tasks.first().text, u"zadanie"_s);
        QCOMPARE(date(n.tasks.first().doneDate), u"2026-10-14"_s);
        QCOMPARE(n.blocks.first().text, u"zadanie\ndalej"_s);
    }

    // --- structure -----------------------------------------------------------------------------

    void structureNesting_data()
    {
        QTest::addColumn<QString>("src");
        QTest::addColumn<QString>("expected");
        QTest::newRow("spaces")
                << u"- a\n  - b\n    - c\n  - d\n- e"_s
                << u"listItem@0-3(0):d0[listItem@1-2(1):d1[listItem@2-2:d2],listItem@3-3:d1], listItem@4-4:d0"_s;
        QTest::newRow("tabs") << u"- a\n\t- b\n\t\t- c\n- d"_s
                              << u"listItem@0-2(0):d0[listItem@1-2(1):d1[listItem@2-2:d2]], listItem@3-3:d0"_s;
        QTest::newRow("mixed") << u"- a\n  - b\n\t- c\n - d"_s
                               << u"listItem@0-3(0):d0[listItem@1-2(1):d1[listItem@2-2:d2],listItem@3-3:d1]"_s;
        QTest::newRow("one space") << u"- a\n - b"_s << u"listItem@0-1(0):d0[listItem@1-1:d1]"_s;
        QTest::newRow("markers")
                << u"1. a\n2. b\n   1) c\n+ d\n* e"_s
                << u"listItem@0-0:d0, listItem@1-2(1):d0[listItem@2-2:d1], listItem@3-3:d0, listItem@4-4:d0"_s;
        QTest::newRow("ends at blocks")
                << u"- a\n# H\n- b\n```\ncode\n```\n- c\n> q"_s
                << u"listItem@0-0:d0, heading@1-1:d0, listItem@2-2:d0, code@3-5:d0, listItem@6-6:d0, quote@7-7:d0"_s;
        QTest::newRow("hr vs items") << u"- - -\n* * *\n- x"_s << u"hr@0-0:d0, hr@1-1:d0, listItem@2-2:d0"_s;
        QTest::newRow("paragraphs") << u"a\nb\n\nc\n- item"_s
                                    << u"paragraph@0-1:d0, paragraph@3-3:d0, listItem@4-4:d0"_s;
        QTest::newRow("ordered no interrupt") << u"W roku\n2024. było"_s << u"paragraph@0-1:d0"_s;
        QTest::newRow("table") << u"| a | b |\n|---|:-:|\n| 1 | 2 |\nafter"_s << u"table@0-2:d0, paragraph@3-3:d0"_s;
        QTest::newRow("table no pipes") << u"a | b\n--- | ---\n1 | 2"_s << u"table@0-2:d0"_s;
        QTest::newRow("table after text") << u"text\n| a | b |\n|---|---|"_s << u"paragraph@0-0:d0, table@1-2:d0"_s;
        QTest::newRow("quotes") << u"> a\n> b\nlazy\n\n> c"_s << u"quote@0-2:d0, quote@4-4:d0"_s;
        QTest::newRow("html") << u"<div>\n[[x]]\n</div>\n\n<!-- c\n\nstill -->\ntext\n\n<span>inline</span> text"_s
                              << u"html@0-2:d0, html@4-6:d0, paragraph@7-7:d0, paragraph@9-9:d0"_s;
        QTest::newRow("comments") << u"a\n\n%%\n- [ ] hidden\n%%\n\nb"_s << u"paragraph@0-0:d0, paragraph@6-6:d0"_s;
        QTest::newRow("frontmatter") << u"---\na: 1\n---\n\ntitle:: X\ntags:: a\n\n- first"_s
                                     << u"frontmatter@0-2:d0, frontmatter@4-5:d0, listItem@7-7:d0"_s;
        QTest::newRow("properties only at top") << u"- a\nkey:: v"_s << u"listItem@0-1:d0"_s;
        // A line that is only `$$` opens a display formula up to the next such line (SYNTAX §11).
        QTest::newRow("display math") << u"$$\nf(x) = a\n + b\n - c\n$$\n"_s << u"paragraph@0-4:d0"_s;
        QTest::newRow("display math with block syntax")
                << u"$$\nf(x) = a\n+ b\n> c\n1. d\n# e\n---\n```\n$$\npo\n\n- x"_s
                << u"paragraph@0-9:d0, listItem@11-11:d0"_s;
        QTest::newRow("display math after text")
                << u"Tekst:\n$$\na\n- b\n$$\n- c"_s << u"paragraph@0-4:d0, listItem@5-5:d0"_s;
        QTest::newRow("display math unclosed") << u"$$\na\n- b"_s << u"paragraph@0-1:d0, listItem@2-2:d0"_s;
        QTest::newRow("display math ends at a blank line")
                << u"$$\na\n\n- b\n$$"_s << u"paragraph@0-1:d0, listItem@3-4:d0"_s;
        QTest::newRow("display math in an item")
                << u"- Wzór:\n  $$\n  f(x) = a\n  + b\n  - c\n  $$\n- dalej"_s << u"listItem@0-5:d0, listItem@6-6:d0"_s;
        QTest::newRow("item opening display math")
                << u"- $$\n  a\n  - b\n  $$\n- c"_s << u"listItem@0-3:d0, listItem@4-4:d0"_s;
        QTest::newRow("lazy display math in an item")
                << u"- Wzór:\n$$\na\n+ b\n$$\n- c"_s << u"listItem@0-4:d0, listItem@5-5:d0"_s;
        QTest::newRow("display math in a quote") << u"> $$\n> a\n> - b\n> $$"_s << u"quote@0-3:d0"_s;
        QTest::newRow("two display formulas")
                << u"$$\na\n$$\n\n$$\n- b\n$$"_s << u"paragraph@0-2:d0, paragraph@4-6:d0"_s;
        QTest::newRow("$$ inside a line is no fence") << u"Wzór $$a\n+ b$$"_s << u"paragraph@0-0:d0, listItem@1-1:d0"_s;
    }

    void structureNesting()
    {
        QFETCH(QString, src);
        QFETCH(QString, expected);
        QCOMPARE(outline(src), expected);
    }

    void displayMathKeepsItsLines()
    {
        const ParsedNote n = parseNote(u"a.md"_s, u"- Wzór #wzory:\n  $$\n  f(x) = a\n  + b\n  - c\n  $$\n- dalej"_s);
        QCOMPARE(n.blocks.size(), 2);
        QCOMPARE(n.blocks[0].text, u"Wzór #wzory:\n$$\nf(x) = a\n+ b\n- c\n$$"_s);
        QCOMPARE(n.blocks[0].tags, QStringList({u"wzory"_s}));
        QCOMPARE(n.blocks[1].text, u"dalej"_s);
        const ParsedNote top = parseNote(u"a.md"_s, u"$$\nf(x) = a\n + b\n - c\n$$\n"_s);
        QCOMPARE(top.blocks.size(), 1);
        QCOMPARE(top.blocks[0].kind, BlockKind::Paragraph);
        QCOMPARE(top.blocks[0].text, u"$$\nf(x) = a\n+ b\n- c\n$$"_s);
    }

    void continuationAndLazyLines()
    {
        const detail::Structure st = detail::parseStructure(u"- a\n  more a\n  - b\nlazy b\n  back in a\n- c"_s);
        const detail::Node &a = st.nodes[st.roots.first()];
        QCOMPARE(ownLines(a), QList<int>({0, 1, 4}));
        QCOMPARE(ownLines(st.nodes[a.children.first()]), QList<int>({2, 3}));
        QCOMPARE(a.ownEndLine, 4);
        QCOMPARE(a.endLine, 4);
    }

    void blankLinesInsideItems()
    {
        const detail::Structure st = detail::parseStructure(u"- a\n\n  para\n\n- b\n\n\ntext"_s);
        QCOMPARE(outline(u"- a\n\n  para\n\n- b\n\n\ntext"_s), u"listItem@0-2:d0, listItem@4-4:d0, paragraph@7-7:d0"_s);
        QCOMPARE(roles(st.nodes[st.roots.first()]), u"text,blank,text"_s);
    }

    void fencesInsideItems()
    {
        const detail::Structure st =
                detail::parseStructure(u"- item\n  ```js\n  - [ ] not a task\n\n  ```\n  after\n- next"_s);
        const detail::Node &item = st.nodes[st.roots.first()];
        QCOMPARE(roles(item), u"text,fence,code,code,fence,text"_s);
        QVERIFY(item.children.isEmpty());
        QCOMPARE(st.roots.size(), 2);
        const detail::Structure unclosed = detail::parseStructure(u"- a\n  ```\n  code\n- b"_s);
        QCOMPARE(unclosed.nodes[unclosed.roots[0]].line, 0);
        QCOMPARE(unclosed.nodes[unclosed.roots[1]].line, 3);
        const detail::Structure first = detail::parseStructure(u"- ```\n  - x\n  ```\n- y"_s);
        QCOMPARE(roles(first.nodes[first.roots.first()]), u"fence,code,fence"_s);
        QCOMPARE(first.roots.size(), 2);
    }

    void listMarkers()
    {
        auto lm = detail::matchListMarker(u"  - [ ] x");
        QVERIFY(lm);
        QCOMPARE(lm->bullet, QChar(u'-'));
        QCOMPARE(lm->markerEnd, 3);
        QCOMPARE(lm->contentStart, 4);
        QCOMPARE(lm->contentCol, 4);
        lm = detail::matchListMarker(u"10) x");
        QVERIFY(lm);
        QVERIFY(lm->ordered);
        QCOMPARE(lm->number, 10);
        QCOMPARE(lm->contentStart, 4);
        lm = detail::matchListMarker(u"-");
        QVERIFY(lm);
        QCOMPARE(lm->contentStart, 1);
        QCOMPARE(lm->contentCol, 2);
        QVERIFY(!detail::matchListMarker(u"-x"));
        QVERIFY(!detail::matchListMarker(u"1.5 kg"));
        QVERIFY(!detail::matchListMarker(u"1234567890. x"));
        QVERIFY(!detail::isTableStart(u"| a | b |", QStringView(u"|---|")));
    }

    void headings()
    {
        const detail::Structure st = detail::parseStructure(u"# One\n## Two ##\nSetext\n===\nAlso\n---\n#nottag"_s);
        QStringList got;
        for (int r : st.roots) {
            const detail::Node &n = st.nodes[r];
            got.append(kindName(n.kind) + u'/' + QString::number(n.headingLevel) + u'/' + QString::number(n.line) + u'-'
                       + QString::number(n.endLine));
        }
        QCOMPARE(got, QStringList({u"heading/1/0-0"_s, u"heading/2/1-1"_s, u"heading/1/2-3"_s, u"heading/2/4-5"_s,
                                   u"paragraph/0/6-6"_s}));
    }

    void fencedCode()
    {
        const detail::Structure st =
                detail::parseStructure(u"```Calc\nx = 1\n```\n~~~query {.x}\ntype:tasks\n~~~\n```\nopen\n\n"_s);
        QStringList got;
        for (int r : st.roots) {
            const detail::Node &n = st.nodes[r];
            got.append(kindName(n.kind) + u'/' + n.lang + u'/' + QString::number(n.line) + u'-'
                       + QString::number(n.endLine));
        }
        QCOMPARE(got, QStringList({u"code/calc/0-2"_s, u"code/query/3-5"_s, u"code//6-7"_s}));
    }

    void callouts()
    {
        const detail::Structure st = detail::parseStructure(
                u"> [!todo]- Zakupy\n> - [ ] mleko\n>   - [ ] chleb\n> \n> > [!note] inner\n> > text\n\nafter"_s);
        const detail::Node &c = st.nodes[st.roots.first()];
        QCOMPARE(c.kind, BlockKind::Callout);
        QCOMPARE(c.calloutType, u"todo"_s);
        QCOMPARE(c.calloutFold, u"-"_s);
        QCOMPARE(c.line, 0);
        QCOMPARE(c.endLine, 5);
        QCOMPARE(c.ownEndLine, 0);
        QCOMPARE(nodeLabel(st, st.roots.first()),
                 u"callout@0-5(0):d0[listItem@1-2(1):d1[listItem@2-2:d2],callout@4-5(4):d1[paragraph@5-5:d2]]"_s);
        QCOMPARE(st.nodes[st.roots[1]].line, 7);
        QCOMPARE(detail::parseStructure(u"> [!note] t\n> body\nlazy"_s).nodes.first().endLine, 2);
        const detail::Node &item = st.nodes[c.children.first()];
        QCOMPARE(st.text(item.own.first().view), u"- [ ] mleko");
        QCOMPARE(item.own.first().view.offset, st.lines[1].start + 2);
    }

    void trailingIds()
    {
        const detail::Structure st = detail::parseStructure(
                u"| a | b |\n|---|---|\n| 1 | 2 |\n^tbl\n\n```\nx\n```\n\n^code1\n\n- item\n\n^para"_s);
        QStringList got;
        for (int r : st.roots) {
            const detail::Node &n = st.nodes[r];
            got.append(kindName(n.kind) + u'/' + n.trailingId + u'/' + QString::number(n.endLine));
        }
        QCOMPARE(got, QStringList({u"table/tbl/3"_s, u"code/code1/9"_s, u"listItem//11"_s, u"paragraph//13"_s}));
    }

    void nodeAtLineInnermost()
    {
        const detail::Structure st = detail::parseStructure(u"- a\n  - b\n  cont a\n- c"_s);
        QCOMPARE(st.nodes[detail::nodeAtLine(st, 1)].line, 1);
        QCOMPARE(st.nodes[detail::nodeAtLine(st, 2)].line, 0);
        QCOMPARE(st.nodes[detail::nodeAtLine(st, 3)].line, 3);
        QCOMPARE(detail::nodeAtLine(st, 9), -1);
        QList<int> seen;
        for (const auto &[n, parent] : detail::preOrder(detail::parseStructure(u"- a\n  - b\n- c"_s)))
            seen.append(n);
        QCOMPARE(seen.size(), 3);
    }
};

QTEST_GUILESS_MAIN(TestParse)
#include "tst_markdown_parse.moc"
