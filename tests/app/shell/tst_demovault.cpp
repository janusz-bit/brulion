// The sample vault in both languages ("Sample notebook", in Polish "Przykładowy brulion"): every
// note parses into what it demonstrates (tasks, reminders and alarms, calc blocks, tables with
// formulas, queries, callouts, embeds, block references), dates are relative to "now", links
// resolve (except the one meant to be created by a click), and seeding writes into a real vault
// without overwriting anything. The documentation notebook of each language: every example
// works, and it is kept up to date without touching what the user changed.
#include "demovault.h"
#include "olddocs.h"

#include "calc/calc.h"
#include "chart/chart.h"
#include "dates/natural.h"
#include "dates/recurrence.h"
#include "diagram/diagram.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/markdown.h"
#include "markdown/math.h"
#include "util/paths.h"
#include "vault/localstore.h"
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

QHash<QString, QString> filesByPath(const QDateTime &now, const QString &language)
{
    QHash<QString, QString> out;
    for (const DemoFile &f : shell::demoVaultFiles(now, language))
        out.insert(f.path, f.content);
    return out;
}

/// What the sample notes of one language are called and say.
struct Sample {
    QString folder, project, books, recipes, picture, meetingTemplate;
    QString title, alias, guideTag, toReadTag, blockId, formula, pie, toCreate, toggleCommand;
    QString docsNotebook, switchButton, meetingDate;
    QStringList headings;
};

Sample sample(const QString &language)
{
    if (language == u"pl")
        return {u"Przykładowy brulion"_s, u"Projekty/Remont mieszkania.md"_s, u"Książki 2026.md"_s, u"Przepisy.md"_s,
                u"assets/plan-mieszkania.svg"_s, u"templates/Spotkanie.md"_s, u"Witaj w Brulionie"_s,
                u"Przewodnik"_s, u"przewodnik"_s, u"do przeczytania"_s, u"budzet1"_s, u"=[Cena]*[Ilość]"_s,
                u"Szarlotka babci"_s, u"Pomysły na prezenty"_s, u"Przełącz tryb notatki"_s,
                u"„Dokumentacja Brulionu”"_s, u"„Przełącz notatnik”"_s, u"Data: 2026-10-14 10:00"_s,
                {u"Dokument i konspekt"_s, u"Konspekt"_s, u"Linki i odwołania"_s, u"Tagi"_s, u"Zadania"_s,
                 u"Przypomnienia i alarmy"_s, u"Obliczenia"_s, u"Tabele z formułami"_s, u"Zapytania"_s,
                 u"Osadzenia"_s, u"Ramki (callouty)"_s, u"Szablony"_s, u"Kalendarz i dziennik"_s,
                 u"Synchronizacja"_s, u"Import i eksport"_s, u"Dokumentacja"_s, u"Skróty klawiszowe"_s}};
    return {u"Sample notebook"_s, u"Projects/Home renovation.md"_s, u"Books 2026.md"_s, u"Recipes.md"_s,
            u"assets/floor-plan.svg"_s, u"templates/Meeting.md"_s, u"Welcome to Brulion"_s,
            u"Guide"_s, u"guide"_s, u"to read"_s, u"budget1"_s, u"=[Price]*[Qty]"_s,
            u"Grandma's apple pie"_s, u"Gift ideas"_s, u"Toggle note mode"_s,
            u"“Brulion Documentation”"_s, u"“Switch notebook”"_s, u"Date: 2026-10-14 10:00"_s,
            {u"Documents and outlines"_s, u"Outliner"_s, u"Links and references"_s, u"Tags"_s, u"Tasks"_s,
             u"Reminders and alarms"_s, u"Calculations"_s, u"Tables with formulas"_s, u"Queries"_s,
             u"Embeds"_s, u"Callouts"_s, u"Templates"_s, u"Calendar and journal"_s, u"Sync"_s,
             u"Import and export"_s, u"Documentation"_s, u"Keyboard shortcuts"_s}};
}

void addLanguages()
{
    QTest::addColumn<QString>("language");
    QTest::newRow("en") << u"en"_s;
    QTest::newRow("pl") << u"pl"_s;
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
    void files_data() { addLanguages(); }
    void files()
    {
        QFETCH(QString, language);
        const Sample names = sample(language);
        const auto files = filesByPath(kNow, language);
        QCOMPARE(shell::demoWelcomePage(language), names.title + u".md"_s);
        QCOMPARE(shell::sampleNotebookFolder(language), names.folder);
        QVERIFY(files.contains(shell::demoWelcomePage(language)));
        QVERIFY(files.contains(u"journals/2026-10-14.md"_s));
        QVERIFY(files.contains(u"journals/2026-10-13.md"_s));
        QVERIFY(files.contains(u"journals/2026-10-07.md"_s));
        QVERIFY(files.contains(names.project));
        QVERIFY(files.contains(names.books));
        QVERIFY(files.contains(names.recipes));
        QVERIFY(files.contains(u"templates/journal.md"_s));
        QVERIFY(files.contains(names.meetingTemplate));
        QVERIFY(files.contains(names.picture));
        QCOMPARE(files.size(), 10);
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QVERIFY2(!it.value().contains(u"{today}"_s) && !it.value().contains(u"{in2}"_s), qPrintable(it.key()));
            QVERIFY2(it.value().endsWith(u'\n'), qPrintable(it.key()));
        }
    }

    void languages()
    {
        // English unless Polish: any other language gets the English notes.
        QCOMPARE(shell::sampleNotebookFolder(u"de"), u"Sample notebook"_s);
        QCOMPARE(shell::demoWelcomePage(QString()), u"Welcome to Brulion.md"_s);
        QCOMPARE(shell::docsWelcomePage(u"en"), u"Contents.md"_s);
        QCOMPARE(shell::docsWelcomePage(u"pl"), u"Spis treści.md"_s);
        const auto english = filesByPath(kNow, u"en"_s);
        const auto polish = filesByPath(kNow, u"pl"_s);
        QCOMPARE(filesByPath(kNow, u"fr"_s), english);
        // The two samples are written in their own language (only the journal's template file
        // and the daily notes share their paths).
        QVERIFY(!english.value(u"journals/2026-10-14.md"_s).contains(u"Remont"_s));
        QVERIFY(!polish.value(u"journals/2026-10-14.md"_s).contains(u"renovation"_s));
        // English examples are written the English way: decimal points, "of", dollars.
        const QString welcome = english.value(u"Welcome to Brulion.md"_s);
        QVERIFY(welcome.contains(u"price = 12.50"_s) && welcome.contains(u"20% of 150"_s));
        QVERIFY(!welcome.contains(u"zł"_s));
    }

    void dateExamples_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("phrase");
        QTest::addColumn<QString>("expected");
        // The phrases the guide pages tell readers to type (kNow is Wednesday, 2026-10-14 10:00).
        QTest::newRow("en tomorrow") << u"en"_s << u"tomorrow at 9"_s << u"2026-10-15 09:00"_s;
        QTest::newRow("en friday") << u"en"_s << u"friday at 6pm"_s << u"2026-10-16 18:00"_s;
        QTest::newRow("en hours") << u"en"_s << u"in 2 hours"_s << u"2026-10-14 12:00"_s;
        QTest::newRow("pl jutro") << u"pl"_s << u"jutro o 9"_s << u"2026-10-15 09:00"_s;
        QTest::newRow("pl piątek") << u"pl"_s << u"w piątek 18:00"_s << u"2026-10-16 18:00"_s;
        QTest::newRow("pl godziny") << u"pl"_s << u"za 2 godziny"_s << u"2026-10-14 12:00"_s;
    }

    void dateExamples()
    {
        QFETCH(QString, language);
        QFETCH(QString, phrase);
        QFETCH(QString, expected);
        const QString welcome = filesByPath(kNow, language).value(shell::demoWelcomePage(language));
        QVERIFY2(welcome.contains(phrase), qPrintable(phrase));
        const auto locale = language == u"pl" ? dates::UiLocale::Polish : dates::UiLocale::English;
        const auto parsed = dates::parseNaturalDate(phrase, kNow, Qt::Monday, locale);
        QVERIFY2(parsed.has_value(), qPrintable(phrase));
        QCOMPARE(parsed->spec.toString(), expected);
        // The whole phrase, nothing left over.
        QCOMPARE(parsed->start, 0);
        QCOMPARE(parsed->length, phrase.size());
    }

    void guidePage_data() { addLanguages(); }
    void guidePage()
    {
        QFETCH(QString, language);
        const Sample names = sample(language);
        const auto files = filesByPath(kNow, language);
        const QString welcome = shell::demoWelcomePage(language);
        const ParsedNote note = markdown::parseNote(welcome, files.value(welcome));
        QCOMPARE(note.title, names.title);
        QVERIFY(note.aliases.contains(names.alias));
        QVERIFY(note.tags.contains(names.guideTag));
        QVERIFY(note.tags.contains(language == u"pl" ? u"projekt/dom"_s : u"project/home"_s));
        QVERIFY(note.tags.contains(names.toReadTag));

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
        QVERIFY(files.value(welcome).contains(u"=sum(above)"_s));
        // Every section of the guide, in this order.
        QStringList headings;
        for (const Heading &h : note.headings) {
            if (h.level == 2)
                headings.append(h.text);
        }
        QCOMPARE(headings, names.headings);
    }

    void journalsAndProjects_data() { addLanguages(); }
    void journalsAndProjects()
    {
        QFETCH(QString, language);
        const Sample names = sample(language);
        const auto files = filesByPath(kNow, language);
        const ParsedNote today = markdown::parseNote(u"journals/2026-10-14.md"_s, files.value(u"journals/2026-10-14.md"_s));
        QCOMPARE(today.journalDate, QDate(2026, 10, 14));
        QVERIFY(!today.links.isEmpty());
        const ParsedNote weekAgo = markdown::parseNote(u"journals/2026-10-07.md"_s, files.value(u"journals/2026-10-07.md"_s));
        QCOMPARE(weekAgo.journalDate, QDate(2026, 10, 7));

        const ParsedNote remont = markdown::parseNote(names.project, files.value(names.project));
        QVERIFY(remont.blockIds.contains(names.blockId));
        QVERIFY(remont.tags.contains(language == u"pl" ? u"projekt/dom"_s : u"project/home"_s));
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
        QVERIFY(files.value(names.project).contains(names.formula));

        const ParsedNote books = markdown::parseNote(names.books, files.value(names.books));
        QVERIFY(files.value(names.books).contains(u"=avg(above)"_s));
        QCOMPARE(books.tasks.size(), 3);
        const ParsedNote recipes = markdown::parseNote(names.recipes, files.value(names.recipes));
        QCOMPARE(recipes.reminders.size(), 1);
        QCOMPARE(recipes.reminders.first().at.date, QDate(2026, 10, 17)); // next Saturday
        QStringList recipeHeadings;
        for (const Heading &h : recipes.headings)
            recipeHeadings.append(h.text);
        QVERIFY(recipeHeadings.contains(names.pie));
    }

    void welcomePageFindsTheDocumentation_data() { addLanguages(); }
    void welcomePageFindsTheDocumentation()
    {
        // Desktop and Android: the documentation notebook, through the switcher or Ctrl+Shift+B.
        // The browser has none: the documentation online, in the same language. The rest of the
        // sample is the same.
        QFETCH(QString, language);
        const Sample names = sample(language);
        const QString welcome = shell::demoWelcomePage(language);
        const auto withNotebook = shell::demoVaultFiles(kNow, language, true);
        const auto browser = shell::demoVaultFiles(kNow, language, false);
        QCOMPARE(withNotebook.size(), browser.size());
        for (qsizetype i = 0; i < browser.size(); ++i) {
            QCOMPARE(browser.at(i).path, withNotebook.at(i).path);
            if (browser.at(i).path != welcome)
                QCOMPARE(browser.at(i).content, withNotebook.at(i).content);
        }
        const QString local = filesByPath(kNow, language).value(welcome);
        QCOMPARE(local, withNotebook.first().content);
        QVERIFY(local.contains(names.docsNotebook));
        QVERIFY(local.contains(names.switchButton) && local.contains(u"Ctrl+Shift+B"_s));
        const QString web = browser.first().content;
        QCOMPARE(browser.first().path, welcome);
        QVERIFY(!web.contains(names.switchButton) && !web.contains(u"Ctrl+Shift+B"_s));
        QVERIFY(!web.contains(u"{docs}"_s) && !local.contains(u"{docs}"_s));
        const ParsedNote note = markdown::parseNote(welcome, web);
        bool online = false;
        for (const Link &l : note.links)
            online |= l.target == u"https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/"_s + language;
        QVERIFY(online);
    }

    void showsBothNoteModes_data() { addLanguages(); }
    void showsBothNoteModes()
    {
        // The guide reads as a document, the renovation project and the journal as outlines
        // (docs/SYNTAX.md §4 "Note mode"), whatever the notebook's defaults.
        QFETCH(QString, language);
        const Sample names = sample(language);
        const QString welcome = shell::demoWelcomePage(language);
        const auto files = filesByPath(kNow, language);
        VaultConfig swapped; // the other defaults
        swapped.pageMode = NoteMode::Outline;
        swapped.journalMode = NoteMode::Document;
        for (const VaultConfig &config : {VaultConfig(), swapped}) {
            const auto mode = [&](const QString &path) {
                return markdown::resolveNoteMode(files.value(path),
                                                 markdown::journalDateFromPath(path, config).has_value(), config);
            };
            QCOMPARE(mode(welcome).mode, NoteMode::Document);
            QVERIFY(mode(welcome).isExplicit());
            QCOMPARE(mode(names.project).mode, NoteMode::Outline);
            QVERIFY(mode(names.project).isExplicit());
        }
        // The journal follows the notebook's journal mode (outline by default).
        QCOMPARE(markdown::resolveNoteMode(files.value(u"journals/2026-10-14.md"_s), true, VaultConfig()).mode,
                 NoteMode::Outline);
        // The guide explains the modes and the key that stores them.
        const QString guide = files.value(welcome);
        QVERIFY(guide.contains(u"brulion: document"_s) && guide.contains(u"`brulion: outline`"_s));
        QVERIFY(guide.contains(names.toggleCommand));
    }

    void datesFollowNow_data() { addLanguages(); }
    void datesFollowNow()
    {
        // On a Saturday the "next Saturday" is a week later; after the 10th the next month's.
        QFETCH(QString, language);
        const Sample names = sample(language);
        const QDateTime saturday(QDate(2027, 2, 13), QTime(8, 0));
        const auto files = filesByPath(saturday, language);
        QVERIFY(files.contains(u"journals/2027-02-13.md"_s));
        QVERIFY(files.contains(u"journals/2027-02-06.md"_s));
        QVERIFY(files.value(names.recipes).contains(u"⏰ 2027-02-20 10:00"_s));
        QVERIFY(files.value(names.project).contains(u"📅 2027-03-10"_s));
        const auto early = filesByPath(QDateTime(QDate(2026, 12, 3), QTime(8, 0)), language);
        QVERIFY(early.value(names.project).contains(u"📅 2026-12-10"_s));
        QVERIFY(early.value(shell::demoWelcomePage(language)).contains(u"⏰! 2026-12-07 05:45"_s)); // next Monday
    }

    void seedAndResolve_data() { addLanguages(); }
    void seedAndResolve()
    {
        QFETCH(QString, language);
        const Sample names = sample(language);
        QTemporaryDir dir;
        Vault vault(dir.filePath(names.folder));
        QVERIFY(vault.open());
        const int written = shell::seedDemoVault(vault, kNow, language);
        QCOMPARE(written, int(shell::demoVaultFiles(kNow, language).size()));
        QCOMPARE(shell::seedDemoVault(vault, kNow, language), 0); // nothing overwritten

        // Changed files are kept.
        QVERIFY(vault.writeText(names.recipes, u"- moje\n"_s));
        QCOMPARE(shell::seedDemoVault(vault, kNow, language), 0);
        QCOMPARE(vault.readText(names.recipes), u"- moje\n"_s);
        QVERIFY(vault.remove(names.recipes));

        // Every link resolves, except the page a click is meant to create.
        VaultIndex index(vault.config());
        for (const FileStat &f : vault.files())
            index.update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());
        QVERIFY(vault.writeText(names.recipes, filesByPath(kNow, language).value(names.recipes)));
        index.update(names.recipes, vault.readText(names.recipes));
        QStringList missing;
        for (const MissingPage &m : index.missingPages())
            missing.append(m.name);
        QCOMPARE(missing, QStringList{ names.toCreate });
        QVERIFY(index.blockById(names.blockId).has_value());
        const QString picture = names.picture.mid(names.picture.indexOf(u'/') + 1);
        QCOMPARE(index.resolveLink(picture).value_or(QString()), names.picture);
        QCOMPARE(index.journals().size(), 3);
        QVERIFY(index.backlinks(names.project).size() >= 3);
        // Every heading a link or an embed points at is on its page.
        int headingLinks = 0;
        for (const FileStat &f : vault.markdownFiles()) {
            for (const Link &link : index.note(f.path)->links) {
                if ((link.kind != LinkKind::Wiki && link.kind != LinkKind::Embed) || link.heading.isEmpty())
                    continue;
                const ParsedNote *target = index.note(index.resolveLink(link.target, f.path).value_or(QString()));
                QVERIFY2(target, qPrintable(f.path + u": "_s + link.target));
                QVERIFY2(markdown::findHeading(target->headings, link.heading) >= 0,
                         qPrintable(f.path + u": "_s + link.target + u'#' + link.heading));
                ++headingLinks;
            }
        }
        QVERIFY(headingLinks >= 3);
    }

    void everyExampleWorks_data() { addLanguages(); }
    void everyExampleWorks()
    {
        // The guide shows live examples: every calc line gives a result, every table formula a
        // value, every query parses and finds something, every recurrence rule is understood.
        QFETCH(QString, language);
        const auto files = filesByPath(kNow, language);
        int calcLines = 0, formulas = 0, queries = 0, rules = 0;
        QTemporaryDir dir;
        Vault vault(dir.filePath(sample(language).folder));
        QVERIFY(vault.open());
        QVERIFY(shell::seedDemoVault(vault, kNow, language) > 0);
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

    void documentationNotebook_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("toCreate");
        QTest::newRow("en") << u"en"_s << u"Brulion Documentation"_s << u"Note to create"_s;
        QTest::newRow("pl") << u"pl"_s << u"Dokumentacja Brulionu"_s << u"Notatka do utworzenia"_s;
    }

    void documentationNotebook()
    {
        // "Brulion Documentation" ("Dokumentacja Brulionu" in Polish): its own notebook, every
        // example works, every link resolves (except the one meant to be created by a click), the
        // demo notebook has none of it.
        QFETCH(QString, language);
        QFETCH(QString, folder);
        QFETCH(QString, toCreate);
        QVERIFY(!filesByPath(kNow, language).keys().join(u'\n').contains(u"Dokumentacja"_s));
        QVERIFY(!filesByPath(kNow, language).keys().join(u'\n').contains(u"Documentation"_s));
        QTemporaryDir dir;
        Vault vault(dir.filePath(folder));
        QVERIFY(vault.open());
        const auto docs = shell::docsVaultFiles(language);
        QVERIFY(docs.size() >= 14);
        const shell::DocsUpdate first = shell::updateDocsVault(vault, language);
        QVERIFY(first.ok);
        QCOMPARE(first.added, int(docs.size()));
        const shell::DocsUpdate second = shell::updateDocsVault(vault, language);
        QVERIFY(second.ok);
        QCOMPARE(second.added + second.updated + second.removed + second.kept, 0);
        QVERIFY(vault.exists(shell::docsWelcomePage(language)));
        // The other language's pages are not among them.
        QStringList paths;
        for (const DemoFile &f : docs)
            paths.append(f.path);
        for (const DemoFile &f : shell::docsVaultFiles(language == u"pl" ? u"en" : u"pl"))
            QVERIFY2(paths.contains(f.path) || !vault.exists(f.path), qPrintable(f.path));

        VaultIndex index(vault.config());
        for (const FileStat &f : vault.files())
            index.update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());
        QStringList missing;
        for (const MissingPage &m : index.missingPages())
            missing.append(m.name);
        QVERIFY2(missing == QStringList{toCreate}, qPrintable(missing.join(u", "_s)));
        // Every [[Page#Heading]] of the guide leads to a heading of its page (markdown::findHeading,
        // the matcher of links and embeds), and every [[Page#^id]] to a block id there.
        int headingLinks = 0;
        for (const DemoFile &f : docs) {
            for (const Link &link : markdown::parseNote(f.path, f.content).links) {
                if (link.kind != LinkKind::Wiki && link.kind != LinkKind::Embed)
                    continue;
                if (link.heading.isEmpty() && link.blockId.isEmpty())
                    continue;
                const QString path =
                        link.target.isEmpty() ? f.path : index.resolveLink(link.target, f.path).value_or(QString());
                const ParsedNote *target = index.note(path);
                QVERIFY2(target, qPrintable(f.path + u": "_s + link.target));
                if (!link.blockId.isEmpty()) {
                    QVERIFY2(target->blockIds.contains(link.blockId), qPrintable(f.path + u": ^"_s + link.blockId));
                    continue;
                }
                QList<Heading> headings;
                for (const Block &b : target->blocks) {
                    if (b.kind == BlockKind::Heading)
                        headings.append(Heading{b.headingLevel, b.raw.toString().section(u'\n', 0, 0), b.line});
                }
                QVERIFY2(markdown::findHeading(headings, link.heading) >= 0,
                         qPrintable(f.path + u": "_s + link.target + u'#' + link.heading));
                ++headingLinks;
            }
        }
        QVERIFY(headingLinks >= 3);

        int queries = 0;
        int diagrams = 0;
        int charts = 0;
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
            // Every ```mermaid example draws (a type Brulion does not draw yet only where shown so).
            static const QRegularExpression mermaidBlock(u"```mermaid\n(.*?)```"_s,
                                                         QRegularExpression::DotMatchesEverythingOption);
            for (const auto &m : mermaidBlock.globalMatch(content)) {
                const diagram::Result r = diagram::render(m.captured(1));
                QVERIFY2(r.ok() || r.kind == diagram::Kind::Unsupported,
                         qPrintable(f.path + u": "_s + m.captured(1).section(u'\n', 0, 0)));
                ++diagrams;
            }
            // Every ```chart example draws, with the data of its page.
            static const QRegularExpression chartBlock(u"```chart\n(.*?)```"_s,
                                                       QRegularExpression::DotMatchesEverythingOption);
            for (const auto &m : chartBlock.globalMatch(content)) {
                const int line = int(QStringView(content).left(m.capturedStart()).count(u'\n'));
                const chart::Result r = chart::render(m.captured(1), chart::pageSources(content, line));
                QVERIFY2(r.ok(), qPrintable(f.path + u": "_s + (r.error ? r.error->message() : QString())));
                ++charts;
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
        QVERIFY(diagrams >= 5);
        QVERIFY(charts >= 2);
    }

    void listOfChangesHasNoLiveSyntax_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("tag");
        QTest::newRow("en") << u"en"_s << u"Changelog.md"_s << u"documentation"_s;
        QTest::newRow("pl") << u"pl"_s << u"Lista zmian.md"_s << u"dokumentacja"_s;
    }

    void listOfChangesHasNoLiveSyntax()
    {
        // The list of changes only describes syntax, in code: an example written as text would
        // act in the documentation notebook — a task, a reminder, a tag, a property or a formula.
        QFETCH(QString, language);
        QFETCH(QString, page);
        QFETCH(QString, tag);
        QString content;
        for (const DemoFile &f : shell::docsVaultFiles(language)) {
            if (f.path == page)
                content = f.content;
        }
        QVERIFY(!content.isEmpty());
        // Each section's bullets stay one list: no blank line between two of them.
        static const QRegularExpression splitList(u"^- [^\n]*\n\n- [^\n]*"_s,
                                                  QRegularExpression::MultilineOption);
        const QRegularExpressionMatch split = splitList.match(content);
        QVERIFY2(!split.hasMatch(), qPrintable(split.captured(0).left(200)));
        const ParsedNote note = markdown::parseNote(page, content);
        QVERIFY(note.tasks.isEmpty());
        QVERIFY(note.reminders.isEmpty());
        QCOMPARE(note.tags, QStringList{tag});
        QVERIFY(note.blockIds.isEmpty());
        static const QRegularExpression codeSpan(u"(`+)(?!`)(.+?)(?<!`)\\1(?!`)"_s);
        for (const Block &b : note.blocks) {
            if (b.kind == BlockKind::Frontmatter)
                continue; // the guide's own tags, aliases and note mode
            QVERIFY2(b.properties.isEmpty(), qPrintable(b.raw.toString()));
            QString text = b.raw.toString(); // code spans masked, as the renderer does before formulas
            for (const auto &m : codeSpan.globalMatchView(b.raw.view()))
                text.replace(m.capturedStart(), m.capturedLength(), QString(m.capturedLength(), u' '));
            QVERIFY2(markdown::findMathSpans(text).isEmpty(), qPrintable(b.raw.toString()));
        }
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
            // On disk at once, not only after the store's delay: the app may be killed meanwhile.
            QVERIFY(LocalStore(vault.store()->filePath()).contains(u"docs/written"_s));
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
        const auto docs = shell::docsVaultFiles(u"pl");
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

        const shell::DocsUpdate update = shell::updateDocsVault(vault, u"pl");
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

    void englishDocumentationIgnoresOldPolishPages()
    {
        // The 0.3.0–0.3.5 hashes are those of Polish pages: an English documentation notebook
        // without a record never takes Polish pages for its own (nothing of them is trashed).
        QTemporaryDir dir;
        Vault vault(dir.filePath(u"Brulion Documentation"_s), dir.filePath(u"state"_s));
        QVERIFY(vault.open());
        QVERIFY(writeRaw(vault.absolutePath(u"Spis treści.md"_s), olddocs::kTableOfContents030.toUtf8()));
        QVERIFY(vault.open()); // rescan
        const shell::DocsUpdate update = shell::updateDocsVault(vault, u"en");
        QVERIFY(update.ok);
        QCOMPARE(update.removed, 0);
        QCOMPARE(update.updated, 0);
        QCOMPARE(vault.readText(u"Spis treści.md"_s), olddocs::kTableOfContents030);
        QCOMPARE(update.added, int(shell::docsVaultFiles(u"en").size()));
        // And a Polish one keeps its pages when the English ones are written elsewhere.
        Vault polish(dir.filePath(u"Dokumentacja Brulionu"_s), dir.filePath(u"stan"_s));
        QVERIFY(polish.open());
        QCOMPARE(shell::updateDocsVault(polish, u"pl").added, int(shell::docsVaultFiles(u"pl").size()));
        QCOMPARE(shell::updateDocsVault(polish, u"pl").added, 0);
        QVERIFY(polish.exists(u"Spis treści.md"_s));
    }

    void journalTemplate_data() { addLanguages(); }
    void journalTemplate()
    {
        QFETCH(QString, language);
        const Sample names = sample(language);
        const auto files = filesByPath(kNow, language);
        markdown::TemplateVars vars;
        vars.now = kNow;
        vars.title = u"2026-10-15"_s;
        const QString text = markdown::applyTemplate(files.value(u"templates/journal.md"_s), vars).text;
        const ParsedNote note = markdown::parseNote(u"journals/2026-10-15.md"_s, text);
        QCOMPARE(note.headings.size(), 2);
        QCOMPARE(note.tasks.size(), 1);
        const QString meeting = markdown::applyTemplate(files.value(names.meetingTemplate), vars).text;
        QVERIFY(meeting.contains(u"# 2026-10-15\n"_s + names.meetingDate));
    }
};

QTEST_GUILESS_MAIN(TestDemoVault)
#include "tst_demovault.moc"
