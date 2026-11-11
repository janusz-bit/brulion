// The built-in gym extension (src/core/extensions/builtin/silownia.luau) on a real notebook: a
// vault, its index and the NotebookHost. Writing an exercise line back in Brulion's spelling, the
// history, records and progress, a plan written into today's note with progression, today's view
// and its actions, a stale line, the catalog, English notes, hostile lines, two years of workouts.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

const QHash<QString, QString> &sampleNotes()
{
    static const QHash<QString, QString> notes{
        {u"Trening A.md"_s, u"tags:: plan-treningowy\nprzerwa:: 3 min\n\n- [[Przysiad]] 3×5\n- [[Wyciskanie leżąc]] 3×5\n- [[Wiosłowanie sztangą]] 3×5\n"_s},
        {u"Trening B.md"_s, u"tags:: plan-treningowy\n\n- [[Przysiad]] 3×5\n- [[Wyciskanie żołnierskie]] 3×5\n- [[Martwy ciąg]] 1×5\n  progresja:: +5 kg\n- [[Podciąganie]] MC 3×8\n"_s},
        {u"journals/2026-11-02.md"_s, u"- Trening A #trening\n  plan:: [[Trening A]]\n  - [x] [[Przysiad]] 95 kg 3×5\n  - [x] [[Wyciskanie leżąc]] 60 kg × 10; 77,5 kg 3×5\n- Zakupy: mleko, chleb\n"_s},
        {u"journals/2026-11-04.md"_s, u"- Trening B #trening\n  plan:: [[Trening B]]\n  - [x] [[Przysiad]] 97,5 kg 3×5\n  - [x] [[Wyciskanie żołnierskie]] 45 kg × 5, 5, 4\n  - [x] [[Martwy ciąg]] 120 kg 1×5 ✅ 2026-11-04\n  - [x] [[Podciąganie]] MC × 8, 7, 6\n"_s},
        {u"journals/2026-11-06.md"_s, u"- Trening A #trening\n  - [x] [[Przysiad]] 100 kg 3×5\n  - [x] [[Wyciskanie leżąc]] 80 kg × 5, 5, 4 @9\n  - [ ] [[Wiosłowanie sztangą]] 60 kg 3×5\n  - Rozgrzewka na rowerku, bez liczb\n"_s},
        {u"journals/2026-11-08.md"_s, u"- [[Bieg]] 5 km 27:30 #trening\n"_s},
        {u"journals/2026-11-09.md"_s, u"- Trening B #trening\n  - [x] [[przysiad]] 102,5 kg 3×5\n  - [-] [[Martwy ciąg]] 125 kg 1×5\n"_s},
    };
    return notes;
}

} // namespace

class TestExtensionsGym : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;
    QList<QPair<int, QString>> m_timers;

    bool open(const QHash<QString, QString> &files, const QString &language = u"pl"_s)
    {
        m_engine.reset();
        m_host.reset();
        m_index.reset();
        m_vault.reset();
        m_timers.clear();
        const QString dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = dir + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly))
                return false;
            f.write(it.value().toUtf8());
        }
        QDir().mkpath(dir);
        m_vault = std::make_unique<Vault>(dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!m_vault->open())
            return false;
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 11), QTime(18, 30)); };
        options.language = language;
        options.uiLanguage = language;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_host->onTimer = [this](const QString &, int seconds, const QString &label) { m_timers.append({seconds, label}); };
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}))
            return false;
        const Result loaded = m_engine->load(builtinExtension(u"silownia"_s));
        if (!loaded.ok())
            qWarning("%s", qPrintable(loaded.error));
        return loaded.ok();
    }

    QString file(const QString &path) const { return m_vault->readText(path); }
    QString lineOf(const QString &path, int line) const { return file(path).split(u'\n').value(line); }

    Result command(const QString &name, const QVariantMap &ctx = {}) { return m_engine->runCommand(u"silownia"_s, name, ctx); }
    QVariantMap ctxAt(const QString &path, int line)
    {
        QVariantMap ctx{{u"page"_s, path}, {u"selection"_s, QString()}};
        const QVariantMap block = m_host->blockAt(path, line);
        if (!block.isEmpty())
            ctx.insert(u"block"_s, block);
        return ctx;
    }

    QVariantMap render(const QString &view, const QVariantMap &params = {})
    {
        const Result r = m_engine->render(u"silownia"_s, view, params);
        if (!r.ok())
            qWarning("%s", qPrintable(r.error));
        return r.value.toMap();
    }

    // The first row of today's view whose link names `exercise`.
    static QVariantMap rowOf(const QVariantMap &view, const QString &exercise)
    {
        for (const QVariant &child : view.value(u"children"_s).toList()) {
            const QVariantMap row = child.toMap();
            if (row.value(u"type"_s) != u"row"_s)
                continue;
            const QVariantList controls = row.value(u"children"_s).toList();
            if (controls.size() > 1 && controls[1].toMap().value(u"page"_s) == exercise)
                return row;
        }
        return {};
    }

    static QVariantMap control(const QVariantMap &row, const QString &type, int nth = 1)
    {
        int seen = 0;
        for (const QVariant &c : row.value(u"children"_s).toList())
            if (c.toMap().value(u"type"_s) == type && ++seen == nth)
                return c.toMap();
        return {};
    }

    Result act(const QVariantMap &node, const QVariantMap &extra = {})
    {
        QVariantMap args = node.value(u"args"_s).toMap();
        for (auto it = extra.cbegin(); it != extra.cend(); ++it)
            args.insert(it.key(), it.value());
        return m_engine->act(u"silownia"_s, u"today"_s, node.value(u"action"_s).toString(), args);
    }

private Q_SLOTS:
    void tidy_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("expected"); // "" = not an exercise: nothing changes
        const auto same = [](const char *line) { QTest::newRow(line) << QString::fromUtf8(line) << QString::fromUtf8(line); };
        const auto to = [](const char *line, const char *expected) {
            QTest::newRow(line) << QString::fromUtf8(line) << QString::fromUtf8(expected);
        };
        same("- [[Przysiad]] 100 kg 5×5");
        same("- [[Wyciskanie leżąc]] 60 kg × 10; 80 kg × 5, 5, 4 @9");
        to("- [[Wyciskanie leżąc]] 60 kg × 10, 80 kg × 5, 5, 4", "- [[Wyciskanie leżąc]] 60 kg × 10; 80 kg × 5, 5, 4");
        to("- Przysiad 4x8 60kg", "- Przysiad 60 kg 4×8");
        to("- Przysiad: 3 x 5 x 100 kg", "- Przysiad 100 kg 3×5");
        same("- [[Wyciskanie żołnierskie]] 42,5 kg 3×8 @7,5");
        to("- [[Wyciskanie żołnierskie]] 42.5 kg 3x8", "- [[Wyciskanie żołnierskie]] 42,5 kg 3×8");
        same("- [[Podciąganie]] MC × 10, 8, 6");
        same("- [[Podciąganie]] 3×8");
        to("- [[Wiosłowanie]] 3 x 8", "- [[Wiosłowanie]] 3×8");
        same("- [[Dipy]] MC+10 kg 3×8");
        same("- [[Podciąganie]] MC-20 kg × 5");
        same("- [[Plank]] 3×60 s");
        to("- [[Plank]] 3x2 min", "- [[Plank]] 3×2 min");
        same("- [[Plank]] 45 s");
        same("- [[Bieg]] 5 km 27:30");
        same("- [[Rower]] 42,2 km 1:35:10");
        same("- [x] [[Wiosłowanie sztangą|Wiosłowanie]] 60 kg × 8 ✅ 2026-11-04");
        to("- [x] [[Wiosłowanie sztangą|Wiosłowanie]] 60kg x8 ✅ 2026-11-04",
           "- [x] [[Wiosłowanie sztangą|Wiosłowanie]] 60 kg × 8 ✅ 2026-11-04");
        same("- [[Przysiad]] 225 lb 3×5 #nogi ^ab12cd");
        to("- [[Przysiad]] 225lbs 3x5 #nogi ^ab12cd", "- [[Przysiad]] 225 lb 3×5 #nogi ^ab12cd");
        same("- Rozciąganie 10 min");
        same("- [[Przysiad]] 100 kg × 5 ciężko, kolano boli");
        same("- [[Przysiad]] 100 kg 3×5 (kolano boli 2 dni, 3 serie lżej)");
        same("- [[Przysiad]] 100 kg 3×5 — 2 tygodnie przerwy");
        same("- [[Ćwiczenie 21]] 10 kg × 21");
        same("- Bieg 21,1 km");
        to("- [[Przysiad]] 100 kg × 5,5,4", "- [[Przysiad]] 100 kg × 5, 5, 4");
        to("- [[Przysiad]] 100kg 3x5", "- [[Przysiad]] 100 kg 3×5");
        same("- [[Przysiad]] 100 kg × 1");
        to("  - [ ] [[Przysiad]]  100 kg   5 x 5", "  - [ ] [[Przysiad]] 100 kg 5×5");
        to("- TODO [[Przysiad]] 100 kg 5x5", "- TODO [[Przysiad]] 100 kg 5×5");
        for (const char *none : {"- Trening A", "- Trening: Push", "- [[Przysiad]]", "- 100 kg 5×5", "- [[Przysiad", "- [[]] 5×5"})
            QTest::newRow(none) << QString::fromUtf8(none) << QString();
    }
    void tidy()
    {
        QFETCH(QString, line);
        QFETCH(QString, expected);
        QVERIFY(open({{u"Linie.md"_s, u"# Linie\r\n\r\n"_s + line + u"\r\n- dalej\r\n"_s}}));
        const Result r = command(u"tidy"_s, ctxAt(u"Linie.md"_s, 2));
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QString after = file(u"Linie.md"_s);
        if (expected.isEmpty()) {
            QVERIFY2(r.value.toString().startsWith(u"Ustaw kursor"_s), qPrintable(r.value.toString()));
            QCOMPARE(after, u"# Linie\r\n\r\n"_s + line + u"\r\n- dalej\r\n"_s);
            return;
        }
        QCOMPARE(after, u"# Linie\r\n\r\n"_s + expected + u"\r\n- dalej\r\n"_s); // line endings kept
        // writing is stable: tidying again changes nothing
        QVERIFY(command(u"tidy"_s, ctxAt(u"Linie.md"_s, 2)).ok());
        QCOMPARE(file(u"Linie.md"_s), after);
    }

    void tidyInEnglish()
    {
        QVERIFY(open({{u"Lines.md"_s, u"- [[Squat]] 3x8 100kg\n- [[Pull-up]] bw x 10, 8\n"_s}}, u"en"_s));
        QVERIFY(command(u"tidy"_s, ctxAt(u"Lines.md"_s, 0)).ok());
        QVERIFY(command(u"tidy"_s, ctxAt(u"Lines.md"_s, 1)).ok());
        QCOMPARE(file(u"Lines.md"_s), u"- [[Squat]] 100 kg 3×8\n- [[Pull-up]] BW × 10, 8\n"_s);
        QVERIFY(command(u"tidy"_s, ctxAt(u"Lines.md"_s, 5)).value.toString().startsWith(u"Put the cursor"_s));
    }

    void historyAndProgress()
    {
        QVERIFY(open(sampleNotes()));
        const QVariantMap list = render(u"progress"_s);
        QStringList names;
        for (const QVariant &row : list.value(u"children"_s).toList())
            names << row.toMap().value(u"children"_s).toList().value(0).toMap().value(u"label"_s).toString();
        // done lines only ([ ] planned, [-] cancelled are not history), names compared folded
        QCOMPARE(names, (QStringList{u"Bieg"_s, u"Martwy ciąg"_s, u"Podciąganie"_s, u"Przysiad"_s, u"Wyciskanie leżąc"_s,
                                     u"Wyciskanie żołnierskie"_s}));
        const QVariantMap squatRow = list.value(u"children"_s).toList().value(3).toMap();
        QCOMPARE(squatRow.value(u"children"_s).toList().value(1).toMap().value(u"text"_s).toString(),
                 u"Rekord: 102,5 kg × 5 (szac. 1RM 119,6 kg), 2026-11-09 · 4 sesje"_s);

        const QVariantMap squat = render(u"progress"_s, {{u"exercise"_s, u"Przysiad"_s}});
        const QVariantList items = squat.value(u"children"_s).toList();
        QVariantMap chart;
        QVariantMap table;
        for (const QVariant &item : items) {
            if (item.toMap().value(u"type"_s) == u"chart"_s)
                chart = item.toMap();
            if (item.toMap().value(u"type"_s) == u"table"_s)
                table = item.toMap();
        }
        QCOMPARE(chart.value(u"labels"_s).toStringList(), (QStringList{u"2026-11-02"_s, u"2026-11-04"_s, u"2026-11-06"_s, u"2026-11-09"_s}));
        const QVariantList estimates = chart.value(u"series"_s).toList().value(0).toMap().value(u"values"_s).toList();
        QCOMPARE(estimates.size(), 4);
        QCOMPARE(qRound(estimates[0].toDouble() * 10), 1108);
        QCOMPARE(qRound(estimates[3].toDouble() * 10), 1196);
        // newest first
        QCOMPARE(table.value(u"rows"_s).toList().value(0).toStringList(),
                 (QStringList{u"2026-11-09"_s, u"102,5 kg 3×5"_s, u"1537,5 kg"_s, u"119,6 kg"_s}));
        QCOMPARE(table.value(u"rows"_s).toList().value(3).toStringList(),
                 (QStringList{u"2026-11-02"_s, u"95 kg 3×5"_s, u"1425 kg"_s, u"110,8 kg"_s}));
        // one session: no chart; an unknown exercise: just the link
        const QVariantMap run = render(u"progress"_s, {{u"exercise"_s, u"Bieg"_s}});
        for (const QVariant &item : run.value(u"children"_s).toList())
            QVERIFY(item.toMap().value(u"type"_s) != u"chart"_s);
    }

    void startAWorkoutFromAPlan()
    {
        QVERIFY(open(sampleNotes()));
        Result r = command(u"start"_s);
        QCOMPARE(r.status, Status::Suspended);
        QCOMPARE(r.value.toMap().value(u"options"_s).toStringList(), (QStringList{u"Trening A"_s, u"Trening B"_s}));
        r = m_engine->resume(u"Trening B"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Trening „Trening B” dopisany do dzisiejszej notatki"_s);
        // progression: the squat's 3×5 at 102,5 kg all done → +2,5; the press missed a rep → the
        // same; the deadlift's last done session (the cancelled one does not count) → +5 kg (its
        // plan's progresja::); pull-ups stay bodyweight
        QCOMPARE(file(u"journals/2026-11-11.md"_s).trimmed(),
                 u"- Trening B #trening\n  plan:: [[Trening B]]\n  - [ ] [[Przysiad]] 105 kg 3×5\n"
                 "  - [ ] [[Wyciskanie żołnierskie]] 45 kg 3×5\n  - [ ] [[Martwy ciąg]] 125 kg × 5\n  - [ ] [[Podciąganie]] MC 3×8"_s);
        // cancelled
        QCOMPARE(command(u"start"_s).status, Status::Suspended);
        QCOMPARE(m_engine->resume(QVariant()).value.toString(), u"Anulowano"_s);
        // no plans at all
        QVERIFY(open({{u"journals/2026-11-02.md"_s, u"- x\n"_s}}));
        QVERIFY(command(u"start"_s).value.toString().startsWith(u"Brak planów"_s));
    }

    void todaysWorkoutOnAPhone()
    {
        QVERIFY(open(sampleNotes()));
        command(u"start"_s);
        QVERIFY(m_engine->resume(u"Trening B"_s).ok());
        const QString today = u"journals/2026-11-11.md"_s;

        QVariantMap view = render(u"today"_s);
        QCOMPARE(view.value(u"children"_s).toList().value(0).toMap().value(u"text"_s).toString(), u"Trening B"_s);
        QVariantMap row = rowOf(view, u"Przysiad"_s);
        QCOMPARE(control(row, u"text"_s).value(u"text"_s).toString(), u"105 kg 3×5"_s);
        QCOMPARE(control(row, u"check"_s).value(u"checked"_s).toBool(), false);

        // "+ seria": one more set like the last, and the rest timer (Trening B sets none: 2 minutes)
        QVERIFY(act(control(row, u"button"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 4×5"_s);
        QCOMPARE(m_timers.value(0), (QPair<int, QString>{120, u"Przerwa"_s}));

        // the steppers change the last set
        row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(control(row, u"stepper"_s, 1).value(u"step"_s).toDouble(), 2.5);
        QVERIFY(act(control(row, u"stepper"_s, 1), {{u"value"_s, 107.5}}).ok());
        row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"stepper"_s, 2), {{u"value"_s, 3}}).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 3×5; 107,5 kg × 3"_s);
        // "− seria"
        row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"button"_s, 2)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 3×5"_s);

        // tick it: Obsidian Tasks' done date, and the history has today's squat
        row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"check"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [x] [[Przysiad]] 105 kg 3×5 ✅ 2026-11-11"_s);
        row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(control(row, u"check"_s).value(u"checked"_s).toBool(), true);
        // a change of a done line keeps its done date; unticking removes it
        QVERIFY(act(control(row, u"button"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [x] [[Przysiad]] 105 kg 4×5 ✅ 2026-11-11"_s);
        row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"check"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 4×5"_s);

        // a line changed elsewhere (a sync arrived) is refused, not overwritten
        const QVariantMap stale = rowOf(render(u"today"_s), u"Martwy ciąg"_s);
        QStringList lines = file(today).split(u'\n');
        lines[4] = u"  - [ ] [[Martwy ciąg]] 130 kg × 5"_s;
        QVERIFY(m_vault->writeText(today, lines.join(u'\n')));
        m_index->update(today, lines.join(u'\n'));
        const Result r = act(control(stale, u"button"_s));
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.contains(u"stale"_s), qPrintable(r.error));
        QCOMPARE(lineOf(today, 4), u"  - [ ] [[Martwy ciąg]] 130 kg × 5"_s);

        // bodyweight rows have a reps stepper only
        row = rowOf(render(u"today"_s), u"Podciąganie"_s);
        QCOMPARE(control(row, u"stepper"_s, 1).value(u"unit"_s).toString(), QString());
        QVERIFY(control(row, u"stepper"_s, 2).isEmpty());
    }

    void aPlanWithoutHistory()
    {
        // No weight in the plan and none done yet: the sets as planned, the weight left to fill in
        // (the stepper starts at 0), never "MC".
        QVERIFY(open({{u"Nowy plan.md"_s, u"tags:: plan-treningowy\n\n- [[Przysiad]] 3×5\n- [[Pompki]] MC 3×10\n"_s}}));
        command(u"start"_s);
        QVERIFY(m_engine->resume(u"Nowy plan"_s).ok());
        QCOMPARE(file(u"journals/2026-11-11.md"_s).trimmed(),
                 u"- Nowy plan #trening\n  plan:: [[Nowy plan]]\n  - [ ] [[Przysiad]] 3×5\n  - [ ] [[Pompki]] MC 3×10"_s);
        QVariantMap row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(control(row, u"stepper"_s, 1).value(u"value"_s).toDouble(), 0.0);
        QVERIFY(act(control(row, u"stepper"_s, 1), {{u"value"_s, 60}}).ok());
        QCOMPARE(lineOf(u"journals/2026-11-11.md"_s, 2), u"  - [ ] [[Przysiad]] 60 kg 3×5"_s); // every set's
        row = rowOf(render(u"today"_s), u"Pompki"_s);
        QVERIFY(control(row, u"stepper"_s, 2).isEmpty()); // MC: reps only
    }

    void restFromThePlanPage()
    {
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"journals/2026-11-11.md"_s, u"- Trening A #trening\n  plan:: [[Trening A]]\n  - [ ] [[Przysiad]] 100 kg 3×5\n"_s);
        QVERIFY(open(notes));
        const QVariantMap row = rowOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(control(row, u"button"_s).value(u"args"_s).toMap().value(u"rest"_s).toInt(), 180);
    }

    void emptyToday()
    {
        QVERIFY(open(sampleNotes()));
        const QVariantList items = render(u"today"_s).value(u"children"_s).toList();
        QCOMPARE(items.value(0).toMap().value(u"type"_s).toString(), u"empty"_s);
        QCOMPARE(items.value(0).toMap().value(u"title"_s).toString(), u"Dziś bez treningu"_s);
        QCOMPARE(items.value(1).toMap().value(u"command"_s).toString(), u"start"_s);
    }

    void addSetFromThePalette()
    {
        QVERIFY(open(sampleNotes()));
        Result r = command(u"add-set"_s, ctxAt(u"journals/2026-11-04.md"_s, 3));
        QCOMPARE(r.status, Status::Suspended);
        const QVariantList fields = r.value.toMap().value(u"fields"_s).toList();
        QCOMPARE(fields.value(0).toMap().value(u"value"_s).toDouble(), 45.0);
        QCOMPARE(fields.value(1).toMap().value(u"value"_s).toDouble(), 4.0);
        r = m_engine->resume(QVariantMap{{u"weight"_s, 47.5}, {u"reps"_s, 3}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(lineOf(u"journals/2026-11-04.md"_s, 3), u"  - [x] [[Wyciskanie żołnierskie]] 45 kg × 5, 5, 4; 47,5 kg × 3"_s);
        QCOMPARE(m_timers.size(), 1);
        // on a workout's title
        QVERIFY(command(u"add-set"_s, ctxAt(u"journals/2026-11-04.md"_s, 0)).value.toString().startsWith(u"Ustaw kursor"_s));
    }

    void exerciseFromTheCatalog()
    {
        QVERIFY(open(sampleNotes()));
        Result r = command(u"from-catalog"_s);
        QCOMPARE(r.status, Status::Suspended);
        r = m_engine->resume(u"WYCISKANIE HANTLI"_s);
        QCOMPARE(r.status, Status::Suspended);
        QVERIFY(r.value.toMap().value(u"options"_s).toStringList().contains(u"Wyciskanie hantli na ławce skośnej"_s));
        r = m_engine->resume(u"Wyciskanie hantli na ławce skośnej"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"Utworzono stronę „Wyciskanie hantli na ławce skośnej”"_s);
        const QString page = m_host->pathOf(u"Wyciskanie hantli na ławce skośnej"_s);
        QVERIFY(!page.isEmpty());
        QCOMPARE(file(page), u"kategoria:: klatka piersiowa\nmięśnie:: klatka piersiowa, przednie aktony barków\nsprzęt:: hantle\n"_s);
        // one match is taken at once; an existing page is kept; English names find it too
        r = command(u"from-catalog"_s);
        r = m_engine->resume(u"deadlift"_s);
        QCOMPARE(r.status, Status::Suspended); // Martwy ciąg, rumuński, sumo
        m_engine->cancel();
        r = command(u"from-catalog"_s);
        r = m_engine->resume(u"kółko"_s);
        QCOMPARE(r.value.toString(), u"Utworzono stronę „Kółko do brzucha”"_s);
        r = command(u"from-catalog"_s);
        r = m_engine->resume(u"ab wheel"_s);
        QCOMPARE(r.value.toString(), u"Strona „Kółko do brzucha” już istnieje"_s);
        r = command(u"from-catalog"_s);
        QCOMPARE(m_engine->resume(u"xyz"_s).value.toString(), u"Nie ma takiego ćwiczenia w katalogu"_s);
    }

    void englishNotes()
    {
        QVERIFY(open({{u"Workout A.md"_s, u"tags:: workout-plan\n\n- [[Squat]] 3×5\n- [[Pull-up]] BW 3×8\n"_s},
                      {u"journals/2026-11-09.md"_s, u"- Workout A #workout\n  - [x] [[Squat]] 100 kg 3×5\n  - [x] [[Pull-up]] BW × 8, 8, 8\n"_s}},
                     u"en"_s));
        command(u"start"_s);
        const Result r = m_engine->resume(u"Workout A"_s);
        QCOMPARE(r.value.toString(), u"Workout “Workout A” added to today's note"_s);
        QCOMPARE(file(u"journals/2026-11-11.md"_s).trimmed(),
                 u"- Workout A #workout\n  plan:: [[Workout A]]\n  - [ ] [[Squat]] 102.5 kg 3×5\n  - [ ] [[Pull-up]] BW 3×8"_s);
        const QVariantMap row = rowOf(render(u"today"_s), u"Squat"_s);
        QCOMPARE(control(row, u"button"_s).value(u"label"_s).toString(), u"+ set"_s);
    }

    void hostileLines()
    {
        QString big = u"- Trening #trening\n"_s;
        big += u"  - [[X]] "_s + QString(100000, u'1') + u'\n';
        big += u"  - [[X]] "_s + QString(50000, u'@') + u'\n';
        big += u"  - [[X]] "_s + u"5x5 "_s.repeated(20000) + u'\n';
        big += u"  - [[X]] 999999999999x5\n"_s;
        big += u"  - [[X]] "_s + u"mc+"_s.repeated(20000) + u'\n';
        big += u"  - [[Y]] 100 kg 5×5\n"_s;
        QVERIFY(open({{u"journals/2026-11-11.md"_s, big}}));
        QElapsedTimer clock;
        clock.start();
        const QVariantMap view = render(u"today"_s);
        QVERIFY(!view.isEmpty());
        QVERIFY(!rowOf(view, u"Y"_s).isEmpty());
        // the capped line: 100 sets at most
        const QVariantMap capped = rowOf(view, u"X"_s);
        QVERIFY(!capped.isEmpty());
        QVERIFY(render(u"progress"_s).value(u"type"_s) == u"column"_s);
        QVERIFY2(clock.elapsed() < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(clock.elapsed())));
    }

    void twoYearsOfWorkouts()
    {
        QHash<QString, QString> notes;
        const QDate start(2024, 11, 1);
        for (int i = 1; i <= 400; ++i) {
            const QDate day = start.addDays(i * 18 / 10);
            notes.insert(u"journals/%1.md"_s.arg(day.toString(Qt::ISODate)),
                         u"- Trening A #trening\n  - [x] [[Przysiad]] %1 kg 3×5\n  - [x] [[Wyciskanie leżąc]] %2 kg × 5, 5, 4\n"
                         "  - [x] [[Podciąganie]] MC × 8, 7, 6\n  - [x] [[Plank]] 3×60 s\n"_s.arg(60 + i / 4).arg(40 + i / 8));
        }
        QVERIFY(open(notes));
        QElapsedTimer clock;
        clock.start();
        const QVariantMap squat = render(u"progress"_s, {{u"exercise"_s, u"Przysiad"_s}});
        const qint64 one = clock.elapsed();
        clock.restart();
        const QVariantMap list = render(u"progress"_s);
        const qint64 all = clock.elapsed();
        qInfo("progress of one exercise over 400 workouts: %lld ms; the list of exercises: %lld ms", one, all);
        QCOMPARE(list.value(u"children"_s).toList().size(), 4);
        QVERIFY(one < 1500 * BRULION_TEST_SLOWDOWN && all < 1500 * BRULION_TEST_SLOWDOWN);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsGym)
#include "tst_extensions_gym.moc"
