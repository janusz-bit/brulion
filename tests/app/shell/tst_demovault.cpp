// The sample vault "Przykładowy brulion": every note parses into what it demonstrates (tasks,
// reminders and alarms, calc blocks, tables with formulas, queries, callouts, embeds, block
// references), dates are relative to "now", links resolve (except the one meant to be created
// by a click), and seeding writes into a real vault without overwriting anything.
#include "demovault.h"
#include "olddocs.h"

#include "calc/calc.h"
#include "dates/recurrence.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/markdown.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::shell::DemoFile;

namespace {

const QDateTime kNow(QDate(2026, 10, 14), QTime(10, 0)); // a Wednesday

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

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

    void documentationNotebook()
    {
        // "Dokumentacja Brulionu": its own notebook, every example works, every link resolves
        // (except the one meant to be created by a click), the demo notebook has none of it.
        QVERIFY(!filesByPath(kNow).keys().join(u'\n').contains(u"Dokumentacja"_s));
        QTemporaryDir dir;
        Vault vault(dir.filePath(u"Dokumentacja Brulionu"_s));
        QVERIFY(vault.open());
        const auto docs = shell::docsVaultFiles();
        QVERIFY(docs.size() >= 14);
        const shell::DocsUpdate first = shell::updateDocsVault(vault);
        QVERIFY(first.ok);
        QCOMPARE(first.added, int(docs.size()));
        const shell::DocsUpdate second = shell::updateDocsVault(vault);
        QVERIFY(second.ok);
        QCOMPARE(second.added + second.updated + second.removed + second.kept, 0);
        QVERIFY(vault.exists(shell::docsWelcomePage()));

        VaultIndex index(vault.config());
        for (const FileStat &f : vault.files())
            index.update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());
        QStringList missing;
        for (const MissingPage &m : index.missingPages())
            missing.append(m.name);
        QCOMPARE(missing, QStringList{ u"Notatka do utworzenia"_s });

        int queries = 0;
        for (const DemoFile &f : docs) {
            const QString &content = f.content;
            for (const calc::CalcBlock &block : calc::findCalcBlocks(content)) {
                calc::Scope scope;
                for (const calc::CalcLineResult &r : calc::evaluateCalc(block.source, scope)) {
                    if (!r.input.trimmed().isEmpty())
                        QVERIFY2(r.hasValue() && !r.hasError(), qPrintable(f.path + u": "_s + r.input));
                }
            }
            for (const calc::TableSpan &span : calc::findTables(content)) {
                const auto model = calc::parseTable(QStringView(content).mid(span.from, span.to - span.from));
                QVERIFY(model.has_value());
                const calc::TableEval eval = calc::evaluateTable(*model);
                for (const QStringList &row : eval.values) {
                    for (const QString &v : row)
                        QVERIFY2(v.isNull() || (v != calc::kErrorMarker && v != calc::kCycleMarker && !v.isEmpty()),
                                 qPrintable(f.path));
                }
            }
            static const QRegularExpression inlineCalc(u"`=([^`]+)`"_s);
            for (const auto &m : inlineCalc.globalMatch(content)) {
                const calc::CalcLineResult r = calc::evaluateInline(m.captured(1), calc::Scope());
                QVERIFY2(r.hasValue() && !r.hasError(), qPrintable(f.path + u": "_s + m.captured(0)));
            }
            static const QRegularExpression queryBlock(u"```query\n(.*?)```"_s,
                                                       QRegularExpression::DotMatchesEverythingOption);
            for (const auto &m : queryBlock.globalMatch(content)) {
                const auto parsed = brulion::parseQuery(m.captured(1));
                QVERIFY2(std::holds_alternative<Query>(parsed), qPrintable(f.path));
                QVERIFY(brulion::runQuery(std::get<Query>(parsed), index, kNow.date()).total > 0);
                ++queries;
            }
        }
        QVERIFY(queries >= 1);
    }

    void documentationUpdates()
    {
        // The documentation follows the app: the next version's pages replace those nobody
        // changed, new pages arrive, dropped ones go; what the user changed or deleted stays so.
        // The record of what was written survives reopening the notebook (its LocalStore).
        QTemporaryDir dir;
        const QString root = dir.filePath(u"Dokumentacja Brulionu"_s);
        const QString state = dir.filePath(u"stan"_s);
        const QList<DemoFile> v1 = {
                {u"Spis treści.md"_s, u"# Spis treści\n- [[Zadania]]\n"_s},
                {u"Zadania.md"_s, u"- [ ] zadanie\n"_s},
                {u"Obliczenia.md"_s, u"```calc\n2 + 2\n```\n"_s},
                {u"Stara strona.md"_s, u"stara\n"_s},
                {u"Żółw.md"_s, u"żółw\n"_s},
        };
        const QList<DemoFile> v2 = {
                {u"Spis treści.md"_s, u"# Spis treści\n- [[Zadania]]\n- [[Wzory]]\n"_s},
                {u"Zadania.md"_s, u"- [ ] zadanie ⏫\n"_s},
                {u"Obliczenia.md"_s, u"```calc\n2 + 3\n```\n"_s},
                {u"Wzory.md"_s, u"$x^2$\n"_s},
        };
        {
            Vault vault(root, state);
            QVERIFY(vault.open());
            QCOMPARE(shell::updateDocsVault(vault, v1).added, 5);
            QVERIFY(vault.writeText(u"Zadania.md"_s, u"- [ ] moje zadanie\n"_s)); // the user's version
            QVERIFY(vault.writeText(u"Żółw.md"_s, u"mój żółw\n"_s));
            QVERIFY(vault.removePermanently(u"Obliczenia.md"_s)); // deleted on purpose
        }
        {
            Vault vault(root, state);
            QVERIFY(vault.open());
            const shell::DocsUpdate update = shell::updateDocsVault(vault, v2);
            QVERIFY(update.ok);
            QCOMPARE(update.updated, 1); // Spis treści
            QCOMPARE(update.added, 1);   // Wzory
            QCOMPARE(update.removed, 1); // Stara strona
            QCOMPARE(update.kept, 1);    // Zadania
            QCOMPARE(vault.readText(u"Spis treści.md"_s), v2.at(0).content);
            QCOMPARE(vault.readText(u"Zadania.md"_s), u"- [ ] moje zadanie\n"_s);
            QVERIFY(!vault.exists(u"Obliczenia.md"_s));
            QCOMPARE(vault.readText(u"Wzory.md"_s), v2.at(3).content);
            QVERIFY(!vault.exists(u"Stara strona.md"_s));
            QCOMPARE(vault.trash().size(), 1);                       // not lost for good
            QCOMPARE(vault.readText(u"Żółw.md"_s), u"mój żółw\n"_s); // dropped, but the user's now
            // The previous version of an updated page is in its history.
            QVERIFY(!vault.history()->list(u"Spis treści.md"_s).isEmpty());
        }
        {
            Vault vault(root, state);
            QVERIFY(vault.open());
            QVERIFY(vault.removePermanently(u"Wzory.md"_s));
            const shell::DocsUpdate again = shell::updateDocsVault(vault, v2);
            QCOMPARE(again.added + again.updated + again.removed, 0);
            QVERIFY(!vault.exists(u"Wzory.md"_s));
            QVERIFY(vault.exists(u"Żółw.md"_s));
        }
        {
            // Emptied to start over (every page deleted): written anew.
            Vault vault(root, state);
            QVERIFY(vault.open());
            for (const FileStat &f : vault.markdownFiles())
                QVERIFY(vault.removePermanently(f.path));
            QCOMPARE(shell::updateDocsVault(vault, v2).added, v2.size());
        }
    }

    void documentationFromOlderVersions_data()
    {
        QTest::addColumn<QString>("tableOfContents");
        QTest::addColumn<bool>("formulasAdded");
        // 0.3.0–0.3.2 had no page about formulas: it arrives. In a notebook written by 0.3.3+
        // it was there, so its absence means the user deleted it.
        QTest::newRow("0.3.0") << olddocs::kTableOfContents030 << true;
        QTest::newRow("0.3.3") << olddocs::tableOfContents033() << false;
    }

    void documentationFromOlderVersions()
    {
        // Notebooks written by 0.3.0–0.3.5 have no record of what was written: their pages are
        // recognised by the hashes of those versions.
        QFETCH(QString, tableOfContents);
        QFETCH(bool, formulasAdded);
        QTemporaryDir dir;
        Vault vault(dir.filePath(u"Dokumentacja Brulionu"_s), dir.filePath(u"stan"_s));
        QVERIFY(vault.open());
        const auto docs = shell::docsVaultFiles();
        for (const DemoFile &f : docs) {
            if (f.path == u"Wzory matematyczne.md" || f.path == u"Obliczenia.md")
                continue; // not written / deleted by the user
            QString content = f.content;
            if (f.path == u"Spis treści.md")
                content = tableOfContents;
            else if (f.path == u"Zadania.md")
                content = u"- [ ] moje notatki o zadaniach\n"_s;
            QVERIFY(writeRaw(vault.absolutePath(f.path), content.toUtf8()));
        }
        QVERIFY(vault.open()); // rescan

        const shell::DocsUpdate update = shell::updateDocsVault(vault);
        QVERIFY(update.ok);
        const auto current = [&](const QString &path) {
            for (const DemoFile &f : docs) {
                if (f.path == path)
                    return f.content;
            }
            return QString();
        };
        QCOMPARE(vault.readText(u"Spis treści.md"_s), current(u"Spis treści.md"_s)); // updated
        QCOMPARE(vault.exists(u"Wzory matematyczne.md"_s), formulasAdded);
        QVERIFY(!vault.exists(u"Obliczenia.md"_s));                                       // stays deleted
        QCOMPARE(vault.readText(u"Zadania.md"_s), u"- [ ] moje notatki o zadaniach\n"_s); // kept
        QCOMPARE(update.updated, tableOfContents == current(u"Spis treści.md"_s) ? 0 : 1);
        QCOMPARE(update.added, formulasAdded ? 1 : 0);
        QCOMPARE(update.kept, 1);
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
