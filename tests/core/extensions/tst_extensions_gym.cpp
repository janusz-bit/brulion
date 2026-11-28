// The built-in gym extension (src/core/extensions/builtin/silownia.luau) on a real notebook: a
// vault, its index and the NotebookHost. Writing an exercise line back in Brulion's spelling, the
// history, records and progress, a plan written into today's note with progression, today's view
// and its actions, a stale line, the catalog, English notes, hostile lines, two years of workouts.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

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
        if (!m_engine->start(m_host.get(), Limits{test::kRoomyMs, 64ll << 20}))
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

    // A card's controls in order, through its lines (the rows in the card).
    static QVariantList controlsOf(const QVariantMap &card)
    {
        QVariantList out;
        for (const QVariant &line : card.value(u"children"_s).toList())
            out << line.toMap().value(u"children"_s).toList();
        return out;
    }

    // The first card of today's view whose link names `exercise`.
    static QVariantMap cardOf(const QVariantMap &view, const QString &exercise)
    {
        for (const QVariant &child : view.value(u"children"_s).toList()) {
            const QVariantMap card = child.toMap();
            if (card.value(u"type"_s) != u"card"_s)
                continue;
            const QVariantList controls = controlsOf(card);
            if (controls.size() > 1 && controls[1].toMap().value(u"page"_s) == exercise)
                return card;
        }
        return {};
    }

    static QVariantMap control(const QVariantMap &card, const QString &type, int nth = 1)
    {
        int seen = 0;
        for (const QVariant &c : controlsOf(card))
            if (c.toMap().value(u"type"_s) == type && ++seen == nth)
                return c.toMap();
        return {};
    }

    // The kinds of a card's lines and their controls: "row: check link text|row: stepper stepper|…".
    static QString linesOf(const QVariantMap &card)
    {
        QStringList lines;
        for (const QVariant &line : card.value(u"children"_s).toList()) {
            QStringList types;
            for (const QVariant &c : line.toMap().value(u"children"_s).toList())
                types << c.toMap().value(u"type"_s).toString();
            lines << line.toMap().value(u"type"_s).toString() + u": "_s + types.join(u' ');
        }
        return lines.join(u'|');
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
        QVariantMap row = cardOf(view, u"Przysiad"_s);
        QCOMPARE(control(row, u"text"_s).value(u"text"_s).toString(), u"105 kg 3×5"_s);
        QCOMPARE(control(row, u"check"_s).value(u"checked"_s).toBool(), false);

        // "+ seria": one more set like the last, and the rest timer (Trening B sets none: 2 minutes)
        QVERIFY(act(control(row, u"button"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 4×5"_s);
        QCOMPARE(m_timers.value(0), (QPair<int, QString>{120, u"Przerwa"_s}));

        // without a rest:: anywhere, the gym's own setting decides (Settings → Extensions → Gym)
        QCOMPARE(m_engine->settingsOf(u"silownia"_s).value(0).toMap().value(u"key"_s).toString(), u"rest"_s);
        QVERIFY(m_host->setSetting(u"silownia"_s, u"rest"_s, 90).isEmpty());
        QCOMPARE(control(cardOf(render(u"today"_s), u"Przysiad"_s), u"button"_s)
                         .value(u"args"_s)
                         .toMap()
                         .value(u"rest"_s)
                         .toInt(),
                 90);
        QVERIFY(m_engine->evaluate(u"silownia"_s, u"b.setSetting(\"rest\", nil)"_s, u"t"_s).ok());
        QCOMPARE(control(cardOf(render(u"today"_s), u"Przysiad"_s), u"button"_s)
                         .value(u"args"_s)
                         .toMap()
                         .value(u"rest"_s)
                         .toInt(),
                 120);

        // every card has the same lines, however long its texts: the exercise, the last set's
        // steppers, the set buttons
        row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(linesOf(row), u"row: check link text|row: stepper stepper|row: button button"_s);

        // the steppers change the last set
        QCOMPARE(control(row, u"stepper"_s, 1).value(u"step"_s).toDouble(), 2.5);
        QVERIFY(act(control(row, u"stepper"_s, 1), {{u"value"_s, 107.5}}).ok());
        row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"stepper"_s, 2), {{u"value"_s, 3}}).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 3×5; 107,5 kg × 3"_s);
        // "− seria"
        row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"button"_s, 2)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 3×5"_s);

        // tick it: Obsidian Tasks' done date, and the history has today's squat
        row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"check"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [x] [[Przysiad]] 105 kg 3×5 ✅ 2026-11-11"_s);
        row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(control(row, u"check"_s).value(u"checked"_s).toBool(), true);
        // a change of a done line keeps its done date; unticking removes it
        QVERIFY(act(control(row, u"button"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [x] [[Przysiad]] 105 kg 4×5 ✅ 2026-11-11"_s);
        row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QVERIFY(act(control(row, u"check"_s)).ok());
        QCOMPARE(lineOf(today, 2), u"  - [ ] [[Przysiad]] 105 kg 4×5"_s);

        // a line changed elsewhere (a sync arrived) is refused, not overwritten
        const QVariantMap stale = cardOf(render(u"today"_s), u"Martwy ciąg"_s);
        QStringList lines = file(today).split(u'\n');
        lines[4] = u"  - [ ] [[Martwy ciąg]] 130 kg × 5"_s;
        QVERIFY(m_vault->writeText(today, lines.join(u'\n')));
        m_index->update(today, lines.join(u'\n'));
        const Result r = act(control(stale, u"button"_s));
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.contains(u"stale"_s), qPrintable(r.error));
        QCOMPARE(lineOf(today, 4), u"  - [ ] [[Martwy ciąg]] 130 kg × 5"_s);

        // bodyweight rows have a reps stepper only
        row = cardOf(render(u"today"_s), u"Podciąganie"_s);
        QCOMPARE(control(row, u"stepper"_s, 1).value(u"unit"_s).toString(), QString());
        QVERIFY(control(row, u"stepper"_s, 2).isEmpty());
        QCOMPARE(linesOf(row), u"row: check link text|row: stepper|row: button button"_s);
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
        QVariantMap row = cardOf(render(u"today"_s), u"Przysiad"_s);
        QCOMPARE(control(row, u"stepper"_s, 1).value(u"value"_s).toDouble(), 0.0);
        QVERIFY(act(control(row, u"stepper"_s, 1), {{u"value"_s, 60}}).ok());
        QCOMPARE(lineOf(u"journals/2026-11-11.md"_s, 2), u"  - [ ] [[Przysiad]] 60 kg 3×5"_s); // every set's
        row = cardOf(render(u"today"_s), u"Pompki"_s);
        QVERIFY(control(row, u"stepper"_s, 2).isEmpty()); // MC: reps only
    }

    void restFromThePlanPage()
    {
        QHash<QString, QString> notes = sampleNotes();
        notes.insert(u"journals/2026-11-11.md"_s, u"- Trening A #trening\n  plan:: [[Trening A]]\n  - [ ] [[Przysiad]] 100 kg 3×5\n"_s);
        QVERIFY(open(notes));
        const QVariantMap row = cardOf(render(u"today"_s), u"Przysiad"_s);
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
        // its catalog id, muscles, equipment, kind and level as properties, the instructions under them
        const QString text = file(page);
        QVERIFY2(
                text.startsWith(
                        u"katalog:: incline-dumbbell-press\nmiesnie:: klatka piersiowa\n"
                        "miesnie-pomocnicze:: barki, triceps\nsprzet:: hantle\nkategoria:: siłowe\n"
                        "poziom:: początkujący\n\n## Wykonanie\n\n1. "_s), // Polish instructions: no note
                qPrintable(text));
        QVERIFY(text.contains(u"\n2. "_s) && text.endsWith(u"\n"_s));
        // one match is taken at once; an existing page is kept; English names find it too
        r = command(u"from-catalog"_s);
        r = m_engine->resume(u"deadlift"_s);
        QCOMPARE(r.status, Status::Suspended); // Martwy ciąg, rumuński, sumo, …: the best first
        QCOMPARE(r.value.toMap().value(u"options"_s).toStringList().value(0), u"Martwy ciąg"_s);
        QVERIFY(r.value.toMap().value(u"options"_s).toStringList().size() <= 50);
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

    // The catalog's command for the command line: { total, exercises } or { error, unknown }.
    QVariantMap search(const QVariantMap &args)
    {
        const Result r = command(u"catalog-search"_s, {{u"page"_s, QString()}, {u"args"_s, args}});
        if (!r.ok())
            qWarning("%s", qPrintable(r.error));
        return r.value.toMap();
    }
    static QStringList ids(const QVariantMap &found)
    {
        QStringList out;
        for (const QVariant &e : found.value(u"exercises"_s).toList())
            out << e.toMap().value(u"id"_s).toString();
        return out;
    }

    void catalogKeepsItsIds()
    {
        // Brulion's 51 exercises keep their ids and names (pages and notes name them); the rest is
        // free-exercise-db's, every id once.
        QVERIFY(open({}));
        const QVariantMap all = search({{u"limit"_s, 2000}});
        QVERIFY2(all.value(u"total"_s).toInt() > 850, qPrintable(all.value(u"total"_s).toString()));
        const QStringList every = ids(all);
        QCOMPARE(QSet<QString>(every.cbegin(), every.cend()).size(), every.size());
        const QStringList own{u"back-squat"_s,
                              u"front-squat"_s,
                              u"goblet-squat"_s,
                              u"leg-press"_s,
                              u"lunge"_s,
                              u"bulgarian-split-squat"_s,
                              u"romanian-deadlift"_s,
                              u"leg-curl"_s,
                              u"leg-extension"_s,
                              u"hip-thrust"_s,
                              u"calf-raise"_s,
                              u"deadlift"_s,
                              u"sumo-deadlift"_s,
                              u"pull-up"_s,
                              u"chin-up"_s,
                              u"barbell-row"_s,
                              u"dumbbell-row"_s,
                              u"lat-pulldown"_s,
                              u"seated-cable-row"_s,
                              u"face-pull"_s,
                              u"bench-press"_s,
                              u"incline-bench-press"_s,
                              u"dumbbell-bench-press"_s,
                              u"incline-dumbbell-press"_s,
                              u"dumbbell-fly"_s,
                              u"dip"_s,
                              u"push-up"_s,
                              u"overhead-press"_s,
                              u"dumbbell-shoulder-press"_s,
                              u"lateral-raise"_s,
                              u"rear-delt-fly"_s,
                              u"shrug"_s,
                              u"barbell-curl"_s,
                              u"dumbbell-curl"_s,
                              u"hammer-curl"_s,
                              u"triceps-pushdown"_s,
                              u"skull-crusher"_s,
                              u"close-grip-bench-press"_s,
                              u"plank"_s,
                              u"side-plank"_s,
                              u"hanging-leg-raise"_s,
                              u"crunch"_s,
                              u"russian-twist"_s,
                              u"ab-wheel"_s,
                              u"kettlebell-swing"_s,
                              u"clean"_s,
                              u"burpee"_s,
                              u"running"_s,
                              u"rowing"_s,
                              u"cycling"_s,
                              u"jump-rope"_s};
        QCOMPARE(own.size(), 51);
        for (const QString &id : own)
            QVERIFY2(every.contains(id), qPrintable(id));
        const QVariantMap squat = search({{u"id"_s, u"back-squat"_s}}).value(u"exercises"_s).toList().value(0).toMap();
        QCOMPARE(squat.value(u"pl"_s).toString(), u"Przysiad ze sztangą"_s);
        QCOMPARE(squat.value(u"en"_s).toString(), u"Back squat"_s);
        QCOMPARE(squat.value(u"primary"_s).toStringList(), QStringList{u"quadriceps"_s});
        QCOMPARE(squat.value(u"muscles"_s).toString(), u"czworogłowe uda"_s);
        QCOMPARE(squat.value(u"equipmentName"_s).toString(), u"sztanga"_s);
        QCOMPARE(squat.value(u"source"_s).toString(), u"Barbell_Squat"_s);
        QVERIFY(squat.value(u"instructions"_s).toList().size() >= 3);
        QCOMPARE(search({{u"id"_s, u"nie-ma"_s}}).value(u"unknown"_s).toString(), u"id"_s);
    }

    // Polish instructions (exercise-instructions-pl.json): at least 120 common exercises, each an
    // exercise of the catalog with an English description, step for step — a step of one is a step of
    // the other — written in Polish (diacritics), none left empty.
    void polishInstructionsMatchTheEnglish()
    {
        // (Files, read when asked for: resolvedData() is what b.data() gives.)
        const QVariantMap data = builtinExtension(u"silownia"_s).data;
        const QVariantMap polish = resolvedData(data.value(u"instructions-pl"_s)).toMap();
        const QVariantMap english = resolvedData(data.value(u"instructions"_s)).toMap();
        QSet<QString> ids;
        const QVariantMap catalog = resolvedData(data.value(u"exercises"_s)).toMap();
        for (const QVariant &e : catalog.value(u"exercises"_s).toList())
            ids.insert(e.toMap().value(u"id"_s).toString());
        // Every exercise with English instructions has them in Polish too.
        for (auto it = english.cbegin(); it != english.cend(); ++it)
            QVERIFY2(polish.contains(it.key()), qPrintable(it.key()));
        // Metric units only: no inches, feet, pounds, miles or yards.
        const QRegularExpression imperial(u"\\d\\s*(?:cal[ei]?|stóp|stopy|funt(?:y|ów)?|mil[ei]?|jard(?:y|ów)?)\\b"_s);
        int withDiacritics = 0;
        for (auto it = polish.cbegin(); it != polish.cend(); ++it) {
            const QString id = it.key();
            QVERIFY2(ids.contains(id), qPrintable(id));
            const QVariantList steps = it.value().toList();
            QVERIFY2(english.contains(id), qPrintable(id));
            QCOMPARE_NE(steps.size(), 0);
            QVERIFY2(steps.size() == english.value(id).toList().size(), qPrintable(id));
            const QVariantList original = english.value(id).toList();
            for (int i = 0; i < steps.size(); ++i) {
                const QString text = steps.at(i).toString();
                QVERIFY2(text.size() >= 5 && text == text.trimmed(), qPrintable(id + u": "_s + text));
                QVERIFY2(text != original.at(i).toString(), qPrintable(id));
                QVERIFY2(!text.contains(imperial), qPrintable(id + u": "_s + text));
            }
            if (steps.constFirst().toString().contains(QRegularExpression(u"[ąćęłńóśźż]"_s)))
                ++withDiacritics;
        }
        QVERIFY(withDiacritics >= polish.size() * 9 / 10);
    }

    // The exercise view and the page made from the catalog show the Polish instructions to a Polish
    // reader (every exercise has them: polishInstructionsMatchTheEnglish) and the English to an
    // English one.
    void polishInstructionsAreShown()
    {
        const QVariantMap polish =
                resolvedData(builtinExtension(u"silownia"_s).data.value(u"instructions-pl"_s)).toMap();
        const QVariantMap english = resolvedData(builtinExtension(u"silownia"_s).data.value(u"instructions"_s)).toMap();
        const auto first = [](const QVariantMap &steps, const QString &id) {
            return steps.value(id).toList().constFirst().toString();
        };
        const QString noteEnglish = u"po angielsku"_s;

        QVERIFY(open(sampleNotes()));
        auto shown = [this](const QString &id) {
            return QString::fromUtf8(QJsonDocument::fromVariant(render(u"exercise"_s, {{u"id"_s, id}})).toJson());
        };
        // Brulion's own exercise and one of free-exercise-db's
        for (const QString &id : {u"back-squat"_s, u"zottman-curl"_s}) {
            const QString view = shown(id);
            QVERIFY2(view.contains(first(polish, id).left(30)), qPrintable(id));
            QVERIFY2(!view.contains(first(english, id).left(30)) && !view.contains(noteEnglish), qPrintable(id));
        }
        // the page: the Polish steps
        Result r = m_engine->act(u"silownia"_s, u"exercise"_s, u"createPage"_s, {{u"id"_s, u"back-squat"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QString page = file(m_host->pathOf(u"Przysiad ze sztangą"_s));
        QVERIFY(page.contains(u"## Wykonanie\n\n1. "_s + first(polish, u"back-squat"_s) + u"\n"_s));
        r = m_engine->act(u"silownia"_s, u"exercise"_s, u"createPage"_s, {{u"id"_s, u"zottman-curl"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        page = file(m_host->pathOf(u"Uginanie Zottmana"_s));
        QVERIFY(page.contains(u"## Wykonanie\n\n1. "_s + first(polish, u"zottman-curl"_s) + u"\n"_s));
        QVERIFY(!page.contains(noteEnglish));

        // an English reader: the English steps
        QVERIFY(open(sampleNotes(), u"en"_s));
        QVERIFY(shown(u"back-squat"_s).contains(first(english, u"back-squat"_s).left(30)));
        QVERIFY(!shown(u"back-squat"_s).contains(first(polish, u"back-squat"_s).left(30)));
    }

    void catalogSearch_data()
    {
        QTest::addColumn<QVariantMap>("args");
        QTest::addColumn<QString>("first");
        QTest::addColumn<int>("atLeast");
        const auto q = [](const QString &query) { return QVariantMap{{u"query"_s, query}}; };
        // a name people write: Brulion's own exercise first
        QTest::newRow("przysiad") << q(u"przysiad"_s) << u"back-squat"_s << 10;
        QTest::newRow("case and diacritics") << q(u"WYCISKANIE LEZAC"_s) << u"bench-press"_s << 1;
        QTest::newRow("english name") << q(u"bench press"_s) << u"bench-press"_s << 5;
        QTest::newRow("words in any order") << q(u"skośnej hantli"_s) << u"incline-dumbbell-press"_s << 2;
        QTest::newRow("polish alias") << q(u"allahy"_s) << u"cable-crunch"_s << 1;
        QTest::newRow("translated upstream") << q(u"orbitrek"_s) << u"elliptical-trainer"_s << 1;
        // a name starting with the words first: the untranslated "Zottman Preacher Curl" before
        // "Uginanie Zottmana" (Zottman Curl) in a Polish interface
        QTest::newRow("english only") << q(u"zottman"_s) << u"zottman-preacher-curl"_s << 2;
        QTest::newRow("translated, by its english name") << q(u"zottman curl"_s) << u"zottman-curl"_s << 1;
        QTest::newRow("nothing") << q(u"zażółć gęślą jaźń"_s) << QString() << 0;
        QTest::newRow("muscle by Polish name, equipment")
                << QVariantMap{{u"muscle"_s, u"Klatka piersiowa"_s}, {u"equipment"_s, u"hantle"_s}} << u"dumbbell-fly"_s
                << 10; // Brulion's own first, by name: "Rozpiętki z hantlami"
        QTest::newRow("muscle by id, query")
                << QVariantMap{{u"muscle"_s, u"lats"_s}, {u"query"_s, u"podciąganie"_s}} << u"pull-up"_s << 3;
        QTest::newRow("muscle by synonym") << QVariantMap{{u"muscle"_s, u"przednie aktony barków"_s}}
                                           << u"rear-delt-fly"_s << 30; // "Odwrotne rozpiętki"
        QTest::newRow("kind") << QVariantMap{{u"category"_s, u"rozciąganie"_s}} << QString() << 100;
    }
    void catalogSearch()
    {
        QFETCH(QVariantMap, args);
        QFETCH(QString, first);
        QFETCH(int, atLeast);
        QVERIFY(open({}));
        args.insert(u"limit"_s, 2000);
        const QVariantMap found = search(args);
        QVERIFY2(!found.contains(u"error"_s), qPrintable(found.value(u"error"_s).toString()));
        const QStringList list = ids(found);
        QVERIFY2(list.size() >= atLeast, qPrintable(list.join(u", "_s)));
        QCOMPARE(found.value(u"total"_s).toInt(), list.size());
        if (!first.isEmpty())
            QCOMPARE(list.value(0), first);
        // every one passes the filters
        for (const QVariant &e : found.value(u"exercises"_s).toList()) {
            const QVariantMap x = e.toMap();
            if (args.value(u"muscle"_s).toString() == u"lats"_s)
                QVERIFY(x.value(u"primary"_s).toStringList().contains(u"lats"_s));
            if (args.contains(u"equipment"_s))
                QCOMPARE(x.value(u"equipment"_s).toString(), u"dumbbell"_s);
            if (args.contains(u"category"_s))
                QCOMPARE(x.value(u"category"_s).toString(), u"stretching"_s);
        }
    }

    void catalogSearchRefusesWhatItDoesNotKnow()
    {
        QVERIFY(open({}));
        QCOMPARE(search({{u"muscle"_s, u"skrzydła"_s}}).value(u"unknown"_s).toString(), u"muscle"_s);
        QCOMPARE(search({{u"equipment"_s, u"lina okrętowa"_s}}).value(u"unknown"_s).toString(), u"equipment"_s);
        QCOMPARE(search({{u"category"_s, u"szachy"_s}}).value(u"unknown"_s).toString(), u"category"_s);
        // the limit
        QCOMPARE(search({{u"query"_s, u"curl"_s}, {u"limit"_s, 3}}).value(u"exercises"_s).toList().size(), 3);
        // hostile queries end, without matches
        QCOMPARE(search({{u"query"_s, QString(20000, u'a')}}).value(u"total"_s).toInt(), 0);
        QCOMPARE(search({{u"query"_s, u"%d+.*[]("_s}}).value(u"total"_s).toInt(), 0);
    }

    void pickerAndExerciseViews()
    {
        QVERIFY(open(sampleNotes()));
        // the picker: a search narrows, a filter too; "+ today" adds the exercise to today's
        // workout (a new one: there is none today)
        QVariantMap view = render(u"catalog"_s);
        QVERIFY(QString::fromUtf8(QJsonDocument::fromVariant(view).toJson()).contains(u"879 ćwiczeń"_s));
        QVERIFY(m_engine->act(u"silownia"_s, u"catalog"_s, u"search"_s, {{u"value"_s, u"wiosłowanie"_s}}).ok());
        QVERIFY(m_engine->act(u"silownia"_s, u"catalog"_s, u"filter"_s,
                              {{u"key"_s, u"equipment"_s}, {u"value"_s, u"barbell"_s}})
                        .ok());
        view = render(u"catalog"_s);
        const QString shown = QString::fromUtf8(QJsonDocument::fromVariant(view).toJson());
        QVERIFY2(shown.contains(u"Wiosłowanie sztangą"_s), qPrintable(shown.left(3000)));
        QVERIFY(!shown.contains(u"Wiosłowanie hantlem"_s));
        Result r = m_engine->act(u"silownia"_s, u"catalog"_s, u"addToToday"_s, {{u"id"_s, u"barbell-row"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"„Wiosłowanie sztangą” w dzisiejszym treningu"_s);
        QCOMPARE(file(u"journals/2026-11-11.md"_s), u"- Trening #trening\n  - [ ] [[Wiosłowanie sztangą]] 3×10\n"_s);
        // a second one goes under the same workout; cardio gets a time
        r = m_engine->act(u"silownia"_s, u"catalog"_s, u"addToToday"_s, {{u"id"_s, u"running"_s}});
        QVERIFY(r.ok());
        QCOMPARE(file(u"journals/2026-11-11.md"_s),
                 u"- Trening #trening\n  - [ ] [[Wiosłowanie sztangą]] 3×10\n  - [ ] [[Bieg]] 20 min\n"_s);
        QVERIFY(m_engine->act(u"silownia"_s, u"catalog"_s, u"clear"_s, {}).ok());
        // the exercise: its muscles, its page made from the catalog, its instructions
        view = render(u"exercise"_s, {{u"id"_s, u"romanian-deadlift"_s}});
        const QString exercise = QString::fromUtf8(QJsonDocument::fromVariant(view).toJson());
        QVERIFY(exercise.contains(u"Martwy ciąg rumuński"_s) && exercise.contains(u"Mięśnie główne: dwugłowe uda"_s));
        QVERIFY(exercise.contains(u"Utwórz stronę ćwiczenia"_s));
        r = m_engine->act(u"silownia"_s, u"exercise"_s, u"createPage"_s, {{u"id"_s, u"romanian-deadlift"_s}});
        QCOMPARE(r.value.toMap().value(u"message"_s).toString(), u"Utworzono stronę „Martwy ciąg rumuński”"_s);
        QVERIFY(file(m_host->pathOf(u"Martwy ciąg rumuński"_s)).startsWith(u"katalog:: romanian-deadlift\n"_s));
        QVERIFY(!QString::fromUtf8(
                         QJsonDocument::fromVariant(render(u"exercise"_s, {{u"id"_s, u"romanian-deadlift"_s}}))
                                 .toJson())
                         .contains(u"Utwórz stronę ćwiczenia"_s));
        QCOMPARE(render(u"exercise"_s, {{u"id"_s, u"nie-ma"_s}}).value(u"type"_s).toString(), u"column"_s);
    }

    // The exercise view shows free-exercise-db's two pictures (start, finish) under the facts, at
    // the pinned commit's address; an exercise upstream has none of and one of Brulion's own
    // without an upstream exercise show none.
    void exercisePictures()
    {
        QVERIFY(open({}));
        const auto picturesOf = [this](const QString &id) {
            for (const QVariant &child : render(u"exercise"_s, {{u"id"_s, id}}).value(u"children"_s).toList()) {
                if (child.toMap().value(u"type"_s) == u"pictures"_s)
                    return child.toMap();
            }
            return QVariantMap();
        };
        const QVariantMap squat = picturesOf(u"back-squat"_s);
        QCOMPARE(squat.value(u"label"_s).toString(), u"Zdjęcia z free-exercise-db"_s);
        const QVariantList items = squat.value(u"items"_s).toList();
        QCOMPARE(items.size(), 2);
        const QString base = u"https://raw.githubusercontent.com/yuhonas/free-exercise-db/"
                             u"f00c92c7dcf1216a928a52c3706c7ce8e2f71ed5/exercises/"_s;
        QCOMPARE(items[0].toMap().value(u"src"_s).toString(), base + u"Barbell_Squat/0.jpg"_s);
        QCOMPARE(items[1].toMap().value(u"src"_s).toString(), base + u"Barbell_Squat/1.jpg"_s);
        QCOMPARE(items[0].toMap().value(u"caption"_s).toString(), u"Początek ruchu"_s);
        QCOMPARE(items[1].toMap().value(u"caption"_s).toString(), u"Koniec ruchu"_s);
        QCOMPARE(items[1].toMap().value(u"alt"_s).toString(), u"Przysiad ze sztangą: Koniec ruchu"_s);
        // the pictures come before the buttons, after the facts
        const QVariantList children = render(u"exercise"_s, {{u"id"_s, u"back-squat"_s}}).value(u"children"_s).toList();
        qsizetype pictures = -1, row = -1;
        for (qsizetype i = 0; i < children.size(); ++i) {
            const QString type = children[i].toMap().value(u"type"_s).toString();
            if (type == u"pictures"_s && pictures < 0)
                pictures = i;
            if (type == u"row"_s && row < 0)
                row = i;
        }
        QVERIFY(pictures > 0 && pictures < row);
        // an upstream exercise of the rest of the catalog: its own id
        QCOMPARE(picturesOf(u"alternate-heel-touchers"_s).value(u"items"_s).toList().value(0).toMap().value(u"src"_s),
                 QVariant(base + u"Alternate_Heel_Touchers/0.jpg"_s));
        // none upstream; none of Brulion's own without an upstream exercise (running)
        QVERIFY(picturesOf(u"kettlebell-halo"_s).isEmpty());
        QVERIFY(picturesOf(u"running"_s).isEmpty());
        QVERIFY(!render(u"exercise"_s, {{u"id"_s, u"kettlebell-halo"_s}}).isEmpty());
    }

    // The volume of every muscle and week with Polish names: the catalog's muscles of "Przysiad",
    // "Wyciskanie leżąc", "Podciąganie" (with the body weight of the log), a page's own muscles,
    // secondary muscles at half, planned and cancelled sets left out, pounds as kilograms.
    void weeklyVolume()
    {
        QVERIFY(open({
                {u"journals/2026-11-02.md"_s,
                 u"- Trening A #trening\n  - [x] [[Przysiad]] 100 kg 5×5\n  - [x] [[Wyciskanie leżąc]] 80 kg 3×5\n"
                 "  - [x] [[Podciąganie]] MC × 10, 8\n  - [ ] [[Martwy ciąg]] 140 kg 1×5\n"
                 "  - [-] [[Wiosłowanie sztangą]] 60 kg 3×5\n- 80 kg #waga\n"_s},
                {u"journals/2026-11-09.md"_s,
                 u"- Trening B #trening\n  - [x] [[Moje ćwiczenie]] 20 kg 3×10\n  - [x] [[Deska]] 3×60 s\n"
                 "  - [x] [[Wykroki]] 225 lb 2×10\n  - [x] Rzut oszczepem 10 kg 3×5\n  - [x] [[Stare ćwiczenie]] 50 kg 2×10\n"_s},
                {u"Moje ćwiczenie.md"_s, u"miesnie:: Klatka, przednie aktony barków\nmiesnie-pomocnicze:: triceps\n"_s},
                // a page the catalog wrote before 0.24: "mięśnie::" is no property (keys are ASCII)
                {u"Stare ćwiczenie.md"_s, u"kategoria:: nogi\nmięśnie:: łydki\nsprzęt:: maszyna\n"_s},
        }));
        const Result r =
                command(u"volume-report"_s, {{u"page"_s, QString()}, {u"args"_s, QVariantMap{{u"weeks"_s, 2}}}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QVariantMap report = r.value.toMap();
        QCOMPARE(report.value(u"unit"_s).toString(), u"kg"_s);
        QCOMPARE(report.value(u"weeks"_s).toStringList(), (QStringList{u"2026-11-02"_s, u"2026-11-09"_s}));
        const QVariantList totals = report.value(u"totals"_s).toList();
        QCOMPARE(totals.value(0).toDouble(), 5140.0); // 2500 + 1200 + 80 kg × 18
        QCOMPARE(totals.value(1).toDouble(), 3791.2); // 600 + 0 + 225 lb × 20 + 150 + 1000
        QCOMPARE(report.value(u"sets"_s).toList().value(0).toDouble(), 10.0);
        QCOMPARE(report.value(u"unassigned"_s).toStringList(), QStringList{u"Rzut oszczepem"_s});
        QHash<QString, QVariantMap> muscles;
        for (const QVariant &m : report.value(u"muscles"_s).toList())
            muscles.insert(m.toMap().value(u"id"_s).toString(), m.toMap());
        const auto volume = [&](const QString &id, int week) {
            return muscles.value(id).value(u"volume"_s).toList().value(week).toDouble();
        };
        const auto sets = [&](const QString &id, int week) {
            return muscles.value(id).value(u"sets"_s).toList().value(week).toDouble();
        };
        QCOMPARE(volume(u"quadriceps"_s, 0), 2500.0);
        QCOMPARE(sets(u"quadriceps"_s, 0), 5.0);
        QCOMPARE(volume(u"glutes"_s, 0), 1250.0); // the squat's secondary: half
        QCOMPARE(sets(u"glutes"_s, 0), 2.5);
        QCOMPARE(volume(u"chest"_s, 0), 1200.0);
        QCOMPARE(volume(u"triceps"_s, 0), 600.0);
        QCOMPARE(volume(u"lats"_s, 0), 1440.0); // bodyweight: 80 kg of the log × 18
        QCOMPARE(volume(u"biceps"_s, 0), 720.0);
        QCOMPARE(volume(u"hamstrings"_s, 0), 1250.0); // the squat's; the deadlift was only planned
        // the second week: the page's own muscles ("Klatka" = chest, front delts = shoulders)
        QCOMPARE(volume(u"chest"_s, 1), 600.0);
        QCOMPARE(volume(u"shoulders"_s, 1), 600.0);
        QCOMPARE(volume(u"triceps"_s, 1), 300.0);
        QCOMPARE(sets(u"triceps"_s, 1), 1.5);
        QCOMPARE(volume(u"abdominals"_s, 1), 0.0); // a plank: sets, no volume
        QCOMPARE(sets(u"abdominals"_s, 1), 3.0);
        QCOMPARE(volume(u"quadriceps"_s, 1), 2041.2);          // 225 lb × 20
        QCOMPARE(volume(u"calves"_s, 1), 1000.0 + 2041.2 / 2); // the old page's "mięśnie:: łydki", the lunge's half
        QCOMPARE(muscles.value(u"quadriceps"_s).value(u"current"_s).toDouble(), 2041.2);
        QCOMPARE(muscles.value(u"quadriceps"_s).value(u"average"_s).toDouble(), 2500.0);
        QCOMPARE(muscles.value(u"quadriceps"_s).value(u"name"_s).toString(), u"czworogłowe uda"_s);
        // the busiest first
        QCOMPARE(report.value(u"muscles"_s).toList().value(0).toMap().value(u"id"_s).toString(), u"quadriceps"_s);

        // In pounds: the same numbers in the setting's unit.
        QVERIFY(m_engine->evaluate(u"silownia"_s, u"b.setSetting(\"unit\", \"lb\")"_s, u"test"_s).ok());
        const QVariantMap pounds =
                command(u"volume-report"_s, {{u"page"_s, QString()}, {u"args"_s, QVariantMap{{u"weeks"_s, 2}}}})
                        .value.toMap();
        QCOMPARE(pounds.value(u"unit"_s).toString(), u"lb"_s);
        QCOMPARE(pounds.value(u"totals"_s).toList().value(0).toDouble(), 11331.8); // 5140 kg

        // The view: this week against the average, the heat map of every muscle, the table.
        QVERIFY(m_engine->evaluate(u"silownia"_s, u"b.setSetting(\"unit\", \"kg\")"_s, u"test"_s).ok());
        const QVariantMap view = render(u"volume"_s, {{u"weeks"_s, u"2"_s}});
        QVariantMap heat;
        for (const QVariant &c : view.value(u"children"_s).toList())
            if (c.toMap().value(u"type"_s) == u"heatmap"_s)
                heat = c.toMap();
        QCOMPARE(heat.value(u"columns"_s).toStringList(), (QStringList{u"2.11"_s, u"9.11"_s}));
        const QVariantMap first = heat.value(u"rows"_s).toList().value(0).toMap();
        QCOMPARE(first.value(u"label"_s).toString(), u"czworogłowe uda"_s);
        QCOMPARE(first.value(u"note"_s).toString(), u"2041 kg"_s);
        QCOMPARE(first.value(u"texts"_s).toList().value(0).toString(),
                 u"czworogłowe uda, tydzień od 2.11: 2500 kg, 5 serii"_s);
        const QString all = QString::fromUtf8(QJsonDocument::fromVariant(view).toJson());
        QVERIFY(all.contains(u"Bez przypisanych mięśni: Rzut oszczepem"_s));
        QVERIFY(all.contains(u"3 serie"_s)); // the plank's abs: sets, without volume
    }

    void volumeOfNoWorkouts()
    {
        QVERIFY(open({{u"Notatka.md"_s, u"- nic\n"_s}}));
        const QVariantMap report =
                command(u"volume-report"_s, {{u"page"_s, QString()}, {u"args"_s, QVariantMap{{u"weeks"_s, 500}}}})
                        .value.toMap();
        QCOMPARE(report.value(u"weeks"_s).toStringList().size(), 104); // clamped
        QVERIFY(report.value(u"muscles"_s).toList().isEmpty());
        const QString view = QString::fromUtf8(QJsonDocument::fromVariant(render(u"volume"_s)).toJson());
        QVERIFY(view.contains(u"Brak treningów w tym okresie"_s));
    }

    // The weigh-ins: what is read from a line (any order, comma or point, kg or lb, body fat,
    // notes), what is not a weigh-in.
    void weighIns_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<double>("weight");
        QTest::addColumn<QString>("unit");
        QTest::addColumn<double>("fat"); // -1: none
        QTest::addColumn<QString>("notes");
        QTest::newRow("plain") << u"- 82,5 kg #waga"_s << 82.5 << u"kg"_s << -1.0 << QString();
        QTest::newRow("all of it") << u"- 82,5 kg 18 % rano #waga"_s << 82.5 << u"kg"_s << 18.0 << u"rano"_s;
        QTest::newRow("glued, point") << u"- Waga: 82.5kg 18,5% po treningu #Waga"_s << 82.5 << u"kg"_s << 18.5
                                      << u"Waga: po treningu"_s;
        QTest::newRow("pounds, english tag") << u"- 181 lb #weight"_s << 181.0 << u"lb"_s << -1.0 << QString();
        QTest::newRow("lbs") << u"- 181 lbs morning #weight"_s << 181.0 << u"lb"_s << -1.0 << u"morning"_s;
        QTest::newRow("no unit: the setting's") << u"- #waga 80"_s << 80.0 << u"kg"_s << -1.0 << QString();
        QTest::newRow("fat first") << u"- 20 % 79 kg #waga"_s << 79.0 << u"kg"_s << 20.0 << QString();
        QTest::newRow("a done task, its date")
                << u"- [x] 79,9 kg #waga ✅ 2026-11-02"_s << 79.9 << u"kg"_s << -1.0 << QString();
        QTest::newRow("other tags stay notes")
                << u"- 80 kg #waga #zdrowie"_s << 80.0 << u"kg"_s << -1.0 << u"#zdrowie"_s;
        QTest::newRow("fat only") << u"- 18 % #waga"_s << 0.0 << QString() << -1.0 << QString();
        QTest::newRow("no number") << u"- waga bez liczby #waga"_s << 0.0 << QString() << -1.0 << QString();
        QTest::newRow("zero") << u"- 0 kg #waga"_s << 0.0 << QString() << -1.0 << QString();
        QTest::newRow("hostile digits") << u"- "_s + QString(100000, u'1') + u" kg #waga"_s << 0.0 << QString() << -1.0
                                        << QString();
        QTest::newRow("hostile percents")
                << u"- 80 kg "_s + u"5 % "_s.repeated(300) + u"#waga"_s << 80.0 << u"kg"_s << 5.0 << u"%"_s;
    }
    void weighIns()
    {
        QFETCH(QString, line);
        QFETCH(double, weight);
        QFETCH(QString, unit);
        QFETCH(double, fat);
        QFETCH(QString, notes);
        QVERIFY(open({{u"journals/2026-11-02.md"_s, line + u'\n'}}));
        test::CpuTimer clock;
        clock.start();
        const Result r = command(u"weights"_s, {{u"page"_s, QString()}, {u"args"_s, QVariantMap{}}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(clock.elapsed() < Limits{}.timeoutMs * BRULION_TEST_SLOWDOWN);
        const QVariantList entries = r.value.toMap().value(u"entries"_s).toList();
        if (unit.isEmpty()) {
            QVERIFY(entries.isEmpty());
            return;
        }
        QCOMPARE(entries.size(), 1);
        const QVariantMap e = entries.first().toMap();
        QCOMPARE(e.value(u"weight"_s).toDouble(), weight);
        QCOMPARE(e.value(u"unit"_s).toString(), unit);
        QCOMPARE(e.value(u"date"_s).toString(), u"2026-11-02"_s);
        if (fat < 0)
            QVERIFY(!e.contains(u"fat"_s));
        else
            QCOMPARE(e.value(u"fat"_s).toDouble(), fat);
        if (!notes.startsWith(u'%'))
            QCOMPARE(e.value(u"notes"_s).toString(), notes);
        QCOMPARE(e.value(u"kg"_s).toDouble(), unit == u"lb" ? std::round(weight * 0.45359237 * 100) / 100 : weight);
    }

    // Writing weigh-ins: the line in Brulion's spelling at the end of the day's note, every other
    // byte kept (CRLF line endings, no final line break, other blocks); the trend; the view's quick
    // entry, Undo and removal.
    void weighInsWrittenByteForByte()
    {
        const QString before = u"- Śniadanie: owsianka\r\n- [ ] Zadzwonić do Łukasza\r\n- 80 kg #waga"_s;
        QVERIFY(open(
                {{u"journals/2026-11-11.md"_s, before}, {u"journals/2026-11-04.md"_s, u"- 81 kg 20 % #waga\n"_s}}));
        Result r = command(u"weight"_s,
                           {{u"page"_s, QString()}, {u"args"_s, QVariantMap{{u"text"_s, u"82,5 kg 18 % rano"_s}}}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toMap().value(u"line"_s).toString(), u"82,5 kg 18 % rano #waga"_s);
        QCOMPARE(file(u"journals/2026-11-11.md"_s), before + u"\r\n- 82,5 kg 18 % rano #waga\r\n"_s);
        // with the parts as arguments, another day, pounds
        r = command(u"weight"_s, {{u"page"_s, QString()},
                                  {u"args"_s, QVariantMap{{u"weight"_s, 181},
                                                          {u"unit"_s, u"lb"_s},
                                                          {u"date"_s, u"2026-11-10"_s},
                                                          {u"notes"_s, u"po urlopie"_s}}}});
        QVERIFY(r.ok());
        QCOMPARE(file(u"journals/2026-11-10.md"_s), u"- 181 lb po urlopie #waga\n"_s);
        // nonsense is refused, nothing written
        r = command(u"weight"_s, {{u"page"_s, QString()}, {u"args"_s, QVariantMap{{u"text"_s, u"dużo"_s}}}});
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"warning"_s);
        r = command(u"weight"_s, {{u"page"_s, QString()},
                                  {u"args"_s, QVariantMap{{u"weight"_s, 80}, {u"date"_s, u"2026-02-30"_s}}}});
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"error"_s);
        QVERIFY(m_host->pathOf(u"2026-02-30"_s).isEmpty());

        // the log: a day's last weigh-in counts, the trend smoothed 10 % a day
        const QVariantMap log =
                command(u"weights"_s, {{u"page"_s, QString()}, {u"args"_s, QVariantMap{}}}).value.toMap();
        QCOMPARE(log.value(u"entries"_s).toList().size(), 4);
        QCOMPARE(log.value(u"latest"_s).toDouble(), 82.5);
        QCOMPARE(log.value(u"latestDate"_s).toString(), u"2026-11-11"_s);
        // 81 → 82,1 (6 days: 1 − 0,9⁶ of the way) → 82,5 (1 day: 10 %)
        const double t1 = 81 + (1 - std::pow(0.9, 6)) * (181 * 0.45359237 - 81);
        const double t2 = t1 + 0.1 * (82.5 - t1);
        QCOMPARE(log.value(u"trend"_s).toDouble(), std::round(t2 * 100) / 100);
        QCOMPARE(log.value(u"fat"_s).toDouble(), 18.0);
        QVERIFY(log.contains(u"change7"_s));

        // the view: its field writes today's weigh-in, Undo takes it back, a menu removes one
        const QString today = file(u"journals/2026-11-11.md"_s);
        r = m_engine->act(u"silownia"_s, u"weight"_s, u"add"_s, {{u"value"_s, u"82,4"_s}});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(file(u"journals/2026-11-11.md"_s), today + u"- 82,4 kg #waga\r\n"_s);
        const QVariantMap undo = r.value.toMap().value(u"button"_s).toMap();
        QCOMPARE(undo.value(u"label"_s).toString(), u"Cofnij"_s);
        QVERIFY(m_engine->act(u"silownia"_s, u"weight"_s, u"remove"_s, undo.value(u"args"_s).toMap()).ok());
        QCOMPARE(file(u"journals/2026-11-11.md"_s), today);
        r = m_engine->act(u"silownia"_s, u"weight"_s, u"add"_s, {{u"value"_s, u"nic"_s}});
        QCOMPARE(r.value.toMap().value(u"tone"_s).toString(), u"warning"_s);
        QCOMPARE(file(u"journals/2026-11-11.md"_s), today);
        const QString shown = QString::fromUtf8(QJsonDocument::fromVariant(render(u"weight"_s)).toJson());
        QVERIFY(shown.contains(u"82,5 kg"_s) && shown.contains(u"Trend"_s) && shown.contains(u"Tkanka tłuszczowa"_s));
        // the macro {{ext:silownia/waga}}: the latest weigh-in and the trend
        const Result macro =
                m_engine->runMacro(u"silownia"_s, u"waga"_s, {{u"args"_s, QVariantList{}}}, test::kRoomyMs);
        QVERIFY2(macro.ok(), qPrintable(macro.error));
        const QString value = QString::fromUtf8(QJsonDocument::fromVariant(macro.value).toJson());
        QVERIFY2(value.contains(u"82,5 kg"_s) && value.contains(u"trend"_s) && value.contains(u"2026-11-11"_s),
                 qPrintable(value));
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
        const QVariantMap row = cardOf(render(u"today"_s), u"Squat"_s);
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
        // Within the app's time limit (Limits::timeoutMs) of work: the CPU time of this thread,
        // where the engine runs, not a wall clock a stalled runner moves on.
        test::CpuTimer clock;
        clock.start();
        const QVariantMap view = render(u"today"_s);
        QVERIFY(!view.isEmpty());
        QVERIFY(!cardOf(view, u"Y"_s).isEmpty());
        // the capped line: 100 sets at most
        const QVariantMap capped = cardOf(view, u"X"_s);
        QVERIFY(!capped.isEmpty());
        QVERIFY(render(u"progress"_s).value(u"type"_s) == u"column"_s);
        QVERIFY2(clock.elapsed() < Limits{}.timeoutMs * BRULION_TEST_SLOWDOWN,
                 qPrintable(QString::number(clock.elapsed())));
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
        test::CpuTimer clock; // the work, not the runner's stalls
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
