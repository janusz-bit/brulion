// The sample vault "Przykładowy brulion": every note parses into what it demonstrates (tasks,
// reminders and alarms, calc blocks, tables with formulas, queries, callouts, embeds, block
// references), dates are relative to "now", links resolve (except the one meant to be created
// by a click), and seeding writes into a real vault without overwriting anything.
#include "demovault.h"

#include "calc/calc.h"
#include "dates/recurrence.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/markdown.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::shell::DemoFile;

namespace {

const QDateTime kNow(QDate(2026, 10, 14), QTime(10, 0)); // a Wednesday

QHash<QString, QString> filesByPath(const QDateTime &now)
{
    QHash<QString, QString> out;
    for (const DemoFile &f : shell::demoVaultFiles(now))
        out.insert(f.path, f.content);
    return out;
}

int countBlocks(const ParsedNote &note, BlockKind kind, const QString &lang = {})
{
    int n = 0;
    for (const Block &b : note.blocks) {
        if (b.kind == kind && (lang.isEmpty() || b.lang == lang))
            ++n;
    }
    return n;
}

} // namespace

class TestDemoVault : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void files()
    {
        const auto files = filesByPath(kNow);
        QVERIFY(files.contains(shell::demoWelcomePage()));
        QVERIFY(files.contains(u"journals/2026-10-14.md"_s));
        QVERIFY(files.contains(u"journals/2026-10-13.md"_s));
        QVERIFY(files.contains(u"journals/2026-10-07.md"_s));
        QVERIFY(files.contains(u"Projekty/Remont mieszkania.md"_s));
        QVERIFY(files.contains(u"Książki 2026.md"_s));
        QVERIFY(files.contains(u"Przepisy.md"_s));
        QVERIFY(files.contains(u"templates/journal.md"_s));
        QVERIFY(files.contains(u"assets/plan-mieszkania.svg"_s));
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QVERIFY2(!it.value().contains(u"{today}"_s) && !it.value().contains(u"{in2}"_s), qPrintable(it.key()));
            QVERIFY2(it.value().endsWith(u'\n'), qPrintable(it.key()));
        }
    }

    void guidePage()
    {
        const auto files = filesByPath(kNow);
        const ParsedNote note = markdown::parseNote(shell::demoWelcomePage(), files.value(shell::demoWelcomePage()));
        QCOMPARE(note.title, u"Witaj w Brulionie"_s);
        QVERIFY(note.aliases.contains(u"Przewodnik"_s));
        QVERIFY(note.tags.contains(u"przewodnik"_s));
        QVERIFY(note.tags.contains(u"projekt/dom"_s));
        QVERIFY(note.tags.contains(u"do przeczytania"_s));

        // Tasks in every state, with dates, priority and recurrence.
        QList<TaskStatus> statuses;
        bool due = false, scheduled = false, recurring = false, priority = false;
        for (const Task &t : note.tasks) {
            statuses.append(t.status);
            due |= t.due.has_value();
            scheduled |= t.scheduled.has_value();
            recurring |= !t.recurrence.isEmpty();
            priority |= t.priority != Priority::None;
        }
        QVERIFY(note.tasks.size() >= 7);
        QVERIFY(statuses.contains(TaskStatus::Todo) && statuses.contains(TaskStatus::Doing)
                && statuses.contains(TaskStatus::Done));
        QVERIFY(due && scheduled && recurring && priority);

        // Reminders (relative to now), an alarm among them; all in the future.
        QVERIFY(note.reminders.size() >= 4);
        bool alarm = false;
        for (const Reminder &r : note.reminders) {
            alarm |= r.alarm;
            QVERIFY(r.at.toLocalDateTime() > kNow);
            QVERIFY(r.at.hasTime());
        }
        QVERIFY(alarm);

        // Links of every kind.
        QList<LinkKind> kinds;
        for (const Link &l : note.links)
            kinds.append(l.kind);
        QVERIFY(kinds.contains(LinkKind::Wiki));
        QVERIFY(kinds.contains(LinkKind::Embed));
        QVERIFY(kinds.contains(LinkKind::BlockRef));
        QVERIFY(kinds.contains(LinkKind::BlockEmbed));

        // Calculations, a table with formulas, a query, callouts.
        QCOMPARE(countBlocks(note, BlockKind::Code, u"calc"_s), 2);
        QCOMPARE(countBlocks(note, BlockKind::Code, u"query"_s), 1);
        QVERIFY(countBlocks(note, BlockKind::Table) >= 2);
        QVERIFY(countBlocks(note, BlockKind::Callout) >= 3);
        QVERIFY(files.value(shell::demoWelcomePage()).contains(u"=sum(above)"_s));
        // Every section of the guide.
        QStringList headings;
        for (const Heading &h : note.headings)
            headings.append(h.text);
        for (const QString &section :
             {u"Dokument i konspekt"_s, u"Konspekt"_s, u"Linki i odwołania"_s, u"Tagi"_s, u"Zadania"_s,
              u"Przypomnienia i alarmy"_s, u"Obliczenia"_s, u"Tabele z formułami"_s, u"Zapytania"_s, u"Osadzenia"_s,
              u"Ramki (callouty)"_s, u"Szablony"_s, u"Kalendarz i dziennik"_s, u"Synchronizacja"_s,
              u"Import i eksport"_s, u"Skróty klawiszowe"_s})
            QVERIFY2(headings.contains(section), qPrintable(section));
    }

    void journalsAndProjects()
    {
        const auto files = filesByPath(kNow);
        const ParsedNote today = markdown::parseNote(u"journals/2026-10-14.md"_s, files.value(u"journals/2026-10-14.md"_s));
        QCOMPARE(today.journalDate, QDate(2026, 10, 14));
        QVERIFY(!today.links.isEmpty());
        const ParsedNote weekAgo = markdown::parseNote(u"journals/2026-10-07.md"_s, files.value(u"journals/2026-10-07.md"_s));
        QCOMPARE(weekAgo.journalDate, QDate(2026, 10, 7));

        const ParsedNote remont = markdown::parseNote(u"Projekty/Remont mieszkania.md"_s,
                                                      files.value(u"Projekty/Remont mieszkania.md"_s));
        QVERIFY(remont.blockIds.contains(u"budzet1"_s));
        QVERIFY(remont.tags.contains(u"projekt/dom"_s));
        QCOMPARE(countBlocks(remont, BlockKind::Table), 1);
        QCOMPARE(countBlocks(remont, BlockKind::Code, u"calc"_s), 1);
        QVERIFY(countBlocks(remont, BlockKind::Code, u"query"_s) == 1);
        bool monthly = false;
        for (const Task &t : remont.tasks) {
            if (t.recurrence == u"every month on the 10th") {
                monthly = true;
                QCOMPARE(t.due->date, QDate(2026, 11, 10)); // the next 10th after 14 Oct
            }
        }
        QVERIFY(monthly);
        QVERIFY(files.value(u"Projekty/Remont mieszkania.md"_s).contains(u"=[Cena]*[Ilość]"_s));

        const ParsedNote books = markdown::parseNote(u"Książki 2026.md"_s, files.value(u"Książki 2026.md"_s));
        QVERIFY(files.value(u"Książki 2026.md"_s).contains(u"=avg(above)"_s));
        QCOMPARE(books.tasks.size(), 3);
        const ParsedNote recipes = markdown::parseNote(u"Przepisy.md"_s, files.value(u"Przepisy.md"_s));
        QCOMPARE(recipes.reminders.size(), 1);
        QCOMPARE(recipes.reminders.first().at.date, QDate(2026, 10, 17)); // next Saturday
        QStringList recipeHeadings;
        for (const Heading &h : recipes.headings)
            recipeHeadings.append(h.text);
        QVERIFY(recipeHeadings.contains(u"Szarlotka babci"_s));
    }

    void showsBothNoteModes()
    {
        // The guide reads as a document, the renovation project and the journal as outlines
        // (docs/SYNTAX.md §4 "Tryb notatki"), whatever the notebook's defaults.
        const auto files = filesByPath(kNow);
        VaultConfig swapped; // the other defaults
        swapped.pageMode = NoteMode::Outline;
        swapped.journalMode = NoteMode::Document;
        for (const VaultConfig &config : {VaultConfig(), swapped}) {
            const auto mode = [&](const QString &path) {
                return markdown::resolveNoteMode(files.value(path),
                                                 markdown::journalDateFromPath(path, config).has_value(), config);
            };
            QCOMPARE(mode(shell::demoWelcomePage()).mode, NoteMode::Document);
            QVERIFY(mode(shell::demoWelcomePage()).isExplicit());
            QCOMPARE(mode(u"Projekty/Remont mieszkania.md"_s).mode, NoteMode::Outline);
            QVERIFY(mode(u"Projekty/Remont mieszkania.md"_s).isExplicit());
        }
        // The journal follows the notebook's journal mode (outline by default).
        QCOMPARE(markdown::resolveNoteMode(files.value(u"journals/2026-10-14.md"_s), true, VaultConfig()).mode,
                 NoteMode::Outline);
        // The guide explains the modes and the key that stores them.
        const QString guide = files.value(shell::demoWelcomePage());
        QVERIFY(guide.contains(u"brulion: document"_s) && guide.contains(u"`brulion: outline`"_s));
        QVERIFY(guide.contains(u"Przełącz tryb notatki"_s));
    }

    void datesFollowNow()
    {
        // On a Saturday the "next Saturday" is a week later; after the 10th the next month's.
        const QDateTime saturday(QDate(2027, 2, 13), QTime(8, 0));
        const auto files = filesByPath(saturday);
        QVERIFY(files.contains(u"journals/2027-02-13.md"_s));
        QVERIFY(files.contains(u"journals/2027-02-06.md"_s));
        QVERIFY(files.value(u"Przepisy.md"_s).contains(u"⏰ 2027-02-20 10:00"_s));
        QVERIFY(files.value(u"Projekty/Remont mieszkania.md"_s).contains(u"📅 2027-03-10"_s));
        const auto early = filesByPath(QDateTime(QDate(2026, 12, 3), QTime(8, 0)));
        QVERIFY(early.value(u"Projekty/Remont mieszkania.md"_s).contains(u"📅 2026-12-10"_s));
        QVERIFY(early.value(shell::demoWelcomePage()).contains(u"⏰! 2026-12-07 05:45"_s)); // next Monday
    }

    void seedAndResolve()
    {
        QTemporaryDir dir;
        Vault vault(dir.filePath(u"Przykładowy brulion"_s));
        QVERIFY(vault.open());
        const int written = shell::seedDemoVault(vault, kNow);
        QCOMPARE(written, int(shell::demoVaultFiles(kNow).size()));
        QCOMPARE(shell::seedDemoVault(vault, kNow), 0); // nothing overwritten

        // Changed files are kept.
        QVERIFY(vault.writeText(u"Przepisy.md"_s, u"- moje\n"_s));
        QCOMPARE(shell::seedDemoVault(vault, kNow), 0);
        QCOMPARE(vault.readText(u"Przepisy.md"_s), u"- moje\n"_s);
        QVERIFY(vault.remove(u"Przepisy.md"_s));

        // Every link resolves, except the page a click is meant to create.
        VaultIndex index(vault.config());
        for (const FileStat &f : vault.files())
            index.update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());
        QVERIFY(vault.writeText(u"Przepisy.md"_s, filesByPath(kNow).value(u"Przepisy.md"_s)));
        index.update(u"Przepisy.md"_s, vault.readText(u"Przepisy.md"_s));
        QStringList missing;
        for (const MissingPage &m : index.missingPages())
            missing.append(m.name);
        QCOMPARE(missing, QStringList{ u"Pomysły na prezenty"_s });
        QVERIFY(index.blockById(u"budzet1"_s).has_value());
        QCOMPARE(index.resolveLink(u"plan-mieszkania.svg"_s).value_or(QString()), u"assets/plan-mieszkania.svg"_s);
        QCOMPARE(index.journals().size(), 3);
        QVERIFY(index.backlinks(u"Projekty/Remont mieszkania.md"_s).size() >= 3);
    }

    void everyExampleWorks()
    {
        // The guide shows live examples: every calc line gives a result, every table formula a
        // value, every query parses and finds something, every recurrence rule is understood.
        const auto files = filesByPath(kNow);
        int calcLines = 0, formulas = 0, queries = 0, rules = 0;
        QTemporaryDir dir;
        Vault vault(dir.filePath(u"Przykładowy brulion"_s));
        QVERIFY(vault.open());
        QVERIFY(shell::seedDemoVault(vault, kNow) > 0);
        VaultIndex index(vault.config());
        for (const FileStat &f : vault.files())
            index.update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());

        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (!paths::isMarkdown(it.key()) || it.key().startsWith(u"templates/"_s))
                continue;
            const QString &content = it.value();
            for (const calc::CalcBlock &block : calc::findCalcBlocks(content)) {
                calc::Scope scope;
                for (const calc::CalcLineResult &r : calc::evaluateCalc(block.source, scope)) {
                    if (r.input.trimmed().isEmpty())
                        continue;
                    QVERIFY2(!r.hasError(), qPrintable(it.key() + u": "_s + r.input + u" → "_s + r.error));
                    QVERIFY2(r.hasValue(), qPrintable(it.key() + u": "_s + r.input));
                    ++calcLines;
                }
            }
            for (const calc::TableSpan &span : calc::findTables(content)) {
                const auto model = calc::parseTable(QStringView(content).mid(span.from, span.to - span.from));
                QVERIFY(model.has_value());
                const calc::TableEval eval = calc::evaluateTable(*model);
                for (qsizetype row = 0; row < eval.values.size(); ++row) {
                    for (qsizetype col = 0; col < eval.values.at(row).size(); ++col) {
                        const QString v = eval.values.at(row).at(col);
                        if (v.isNull())
                            continue;
                        QVERIFY2(v != calc::kErrorMarker && v != calc::kCycleMarker && !v.isEmpty(),
                                 qPrintable(it.key() + u": "_s + model->rows.at(row).at(col) + u" → "_s + v));
                        ++formulas;
                    }
                }
            }
            static const QRegularExpression inlineCalc(u"`=([^`]+)`"_s);
            for (const auto &m : inlineCalc.globalMatch(content)) {
                const calc::CalcLineResult r = calc::evaluateInline(m.captured(1), calc::Scope());
                QVERIFY2(r.hasValue() && !r.hasError(), qPrintable(m.captured(0) + u" → "_s + r.error));
            }
            static const QRegularExpression queryBlock(u"```query\n(.*?)```"_s,
                                                       QRegularExpression::DotMatchesEverythingOption);
            for (const auto &m : queryBlock.globalMatch(content)) {
                const auto parsed = brulion::parseQuery(m.captured(1));
                QVERIFY2(std::holds_alternative<Query>(parsed),
                         qPrintable(m.captured(1) + u" → "_s
                                    + (std::holds_alternative<QString>(parsed) ? std::get<QString>(parsed) : QString())));
                const QueryResult result = brulion::runQuery(std::get<Query>(parsed), index, kNow.date());
                QVERIFY2(result.total > 0, qPrintable(it.key() + u": "_s + m.captured(1)));
                ++queries;
            }
            const ParsedNote note = markdown::parseNote(it.key(), content);
            for (const Task &t : note.tasks) {
                if (t.recurrence.isEmpty())
                    continue;
                QVERIFY2(dates::parseRecurrence(t.recurrence).has_value(), qPrintable(t.recurrence));
                ++rules;
            }
        }
        QVERIFY(calcLines >= 20);
        QVERIFY(formulas >= 10);
        QCOMPARE(queries, 2);
        QVERIFY(rules >= 2);
    }

    void journalTemplate()
    {
        const auto files = filesByPath(kNow);
        markdown::TemplateVars vars;
        vars.now = kNow;
        vars.title = u"2026-10-15"_s;
        const QString text = markdown::applyTemplate(files.value(u"templates/journal.md"_s), vars).text;
        const ParsedNote note = markdown::parseNote(u"journals/2026-10-15.md"_s, text);
        QCOMPARE(note.headings.size(), 2);
        QCOMPARE(note.tasks.size(), 1);
        const QString meeting = markdown::applyTemplate(files.value(u"templates/Spotkanie.md"_s), vars).text;
        QVERIFY(meeting.contains(u"# 2026-10-15\nData: 2026-10-14 10:00"_s));
    }
};

QTEST_GUILESS_MAIN(TestDemoVault)
#include "tst_demovault.moc"
