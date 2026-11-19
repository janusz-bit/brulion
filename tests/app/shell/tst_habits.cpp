// The Habits view as the app draws it (docs/UI.md "Habits"): an ExtensionViewModel over the real
// built-in extension `nawyki` (src/core/extensions/builtin/nawyki.luau) switched on in a
// WorkspaceExtensions over a small notebook — a summary, a card per habit in the notebook's order
// (its row: today's check with its menu, the name, the schedule, the "⋯" menu; the streaks; the
// month grid), problems (a schedule not understood, a name defined twice) with a link to their
// definition, another month through the view's params; today's check and a past day ticked through
// the view (the line in the day's journal), the toast's Undo, a tick written in the journal itself
// drawn again in place; "Add habit"'s form (text fields; a schedule not understood asked again);
// the extension off: nothing drawn.

#include "workspaceextensions.h"

#include "extensionviewmodel.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;

namespace {

const QString kToday = u"journals/2026-11-04.md"_s;

QHash<QString, QString> notebookFiles()
{
    return {
            {u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Bieganie\n  habit:: pn, śr, pt\n"
                             "- Czytanie\n  habit:: 3 razy w tygodniu\n- Coś\n  habit:: kiedy mi się zachce\n"_s},
            {u"Zzz/Inne.md"_s, u"- medytacja\n  habit:: co 2 dni\n"_s},
            {u"journals/2026-11-02.md"_s, u"- [x] Medytacja #nawyk\n- [x] Bieganie #nawyk\n- [x] Czytanie #nawyk\n"_s},
            {u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk\n- [x] Czytanie #nawyk\n"_s},
            {kToday, u"- [ ] Medytacja #nawyk\n- [-] Bieganie #nawyk\n- notatka\n"_s},
    };
}

QString readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

class TestShellHabits : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<WorkspaceExtensions> m_ext;
    QStringList m_messages; ///< the toasts, "kind: text [button]"
    QList<int> m_buttons;   ///< their buttons' ids

    QString disk(const QString &path) const { return readFile(m_dir + u'/' + path); }

    /// The notebook opened as the app opens one (tst_extensions.cpp), habits switched on.
    void open()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
        m_messages.clear();
        m_buttons.clear();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        const QHash<QString, QString> files = notebookFiles();
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = m_dir + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(it.value().toUtf8());
        }
        m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        QVERIFY(m_vault->open());
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        m_ext = std::make_unique<WorkspaceExtensions>([] { return QDateTime(QDate(2026, 11, 4), QTime(10, 30)); });
        connect(m_ext.get(), &WorkspaceExtensions::message, this,
                [this](const QString &text, const QString &kind) { m_messages << kind + u": "_s + text; });
        connect(m_ext.get(), &WorkspaceExtensions::messageWithButton, this,
                [this](const QString &text, const QString &kind, const QString &label, int id) {
                    m_messages << kind + u": "_s + text + u" ["_s + label + u']';
                    m_buttons << id;
                });
        connect(m_vault.get(), &Vault::changed, this, [this](const QList<VaultChange> &changes) {
            QStringList changed;
            for (const VaultChange &c : changes) {
                if (c.type == ChangeType::Deleted) {
                    m_index->remove(c.path);
                } else if (paths::isMarkdown(c.path)) {
                    bool ok = false;
                    const QString content = m_vault->readText(c.path, &ok);
                    if (ok)
                        m_index->update(c.path, content);
                }
                changed << c.path;
            }
            m_ext->notesChanged(changed);
        });
        m_ext->attach(m_vault.get(), m_index.get());
        m_ext->setEnabled({u"nawyki"_s});
        m_ext->indexReady();
    }

    /// The view "month" of `nawyki`, drawn.
    void draw(ExtensionViewModel &model, const QString &month = QString())
    {
        model.setExtensions(m_ext.get());
        model.setExtension(u"nawyki"_s);
        model.setView(u"month"_s);
        model.setParamKey(u"month"_s);
        model.setParamValue(month);
        model.refresh();
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
    }

    static QVariant role(const ExtensionViewModel &model, int row, int role)
    {
        return model.data(model.index(row), role);
    }
    static ExtensionControlsModel *controlsOf(const ExtensionViewModel &model, int row)
    {
        return qobject_cast<ExtensionControlsModel *>(role(model, row, ExtensionViewModel::ControlsRole).value<QObject *>());
    }
    static QString control(const ExtensionControlsModel *controls, int i, int r)
    {
        return controls ? controls->data(controls->index(i), r).toString() : QString();
    }
    /// The row line of the habit `name` (its card's first line), -1 when none.
    static int rowOf(const ExtensionViewModel &model, const QString &name)
    {
        for (int i = 0; i < model.rowCount(); ++i) {
            const ExtensionControlsModel *c = controlsOf(model, i);
            if (c && c->rowCount() > 1 && control(c, 0, ExtensionControlsModel::LabelRole) == name)
                return i;
        }
        return -1;
    }
    /// The state of `date` in the month line `row`.
    static QString dayState(const ExtensionViewModel &model, int row, const QString &date)
    {
        for (const QVariant &d : role(model, row, ExtensionViewModel::DaysRole).toList())
            if (d.toMap().value(u"date"_s).toString() == date)
                return d.toMap().value(u"state"_s).toString();
        return {};
    }
    /// The texts of the lines of `type`.
    static QStringList texts(const ExtensionViewModel &model, const QString &type)
    {
        QStringList out;
        for (int i = 0; i < model.rowCount(); ++i)
            if (role(model, i, ExtensionViewModel::TypeRole).toString() == type)
                out << role(model, i, ExtensionViewModel::TextRole).toString();
        return out;
    }

private Q_SLOTS:
    void init() { open(); }

    void cleanup()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
    }

    void cardsInTheNotebooksOrder()
    {
        ExtensionViewModel model;
        draw(model);
        // The summary first; then a card per definition: valid habits with a row, the streaks and
        // the month; problems with their name, the problem and a link to the definition.
        QCOMPARE(role(model, 0, ExtensionViewModel::TypeRole).toString(), u"text"_s);
        QVERIFY(role(model, 0, ExtensionViewModel::MutedRole).toBool());
        const QString summary = role(model, 0, ExtensionViewModel::TextRole).toString();
        QVERIFY2(summary.contains(u'0') && summary.contains(u'2'), qPrintable(summary)); // 0 of 2 done today
        const int med = rowOf(model, u"Medytacja"_s);
        const int run = rowOf(model, u"Bieganie"_s);
        const int read = rowOf(model, u"Czytanie"_s);
        QVERIFY(med > 0 && run > med && read > run);
        for (const int row : {med, run, read}) {
            QCOMPARE(role(model, row, ExtensionViewModel::CardRole).toString(), u"first"_s);
            QCOMPARE(role(model, row + 1, ExtensionViewModel::TypeRole).toString(), u"text"_s);
            QCOMPARE(role(model, row + 1, ExtensionViewModel::IconRole).toString(), u"flame"_s);
            QCOMPARE(role(model, row + 2, ExtensionViewModel::TypeRole).toString(), u"month"_s);
            QCOMPARE(role(model, row + 2, ExtensionViewModel::CardRole).toString(), u"last"_s);
            QCOMPARE(role(model, row + 2, ExtensionViewModel::MonthRole).toString(), u"2026-11"_s);
        }
        // The row: today's check (with its menu and the habit's name), the name, the schedule in
        // words, the "⋯" menu.
        ExtensionControlsModel *controls = controlsOf(model, med);
        QStringList kinds;
        for (int i = 0; i < controls->rowCount(); ++i)
            kinds << control(controls, i, ExtensionControlsModel::TypeRole);
        QCOMPARE(kinds, (QStringList{u"check"_s, u"text"_s, u"text"_s, u"menu"_s}));
        QCOMPARE(control(controls, 0, ExtensionControlsModel::StateRole), u"open"_s);
        QVERIFY(controls->data(controls->index(0), ExtensionControlsModel::HasMenuRole).toBool());
        QCOMPARE(control(controls, 1, ExtensionControlsModel::TextRole), u"Medytacja"_s);
        QCOMPARE(control(controls, 2, ExtensionControlsModel::TextRole), u"codziennie"_s);
        QCOMPARE(model.menuLabels(med, 3, QString()).size(), 5);
        QCOMPARE(model.menuLabels(med, 0, QString()).size(), 5);
        // Bieganie was skipped today.
        QCOMPARE(control(controlsOf(model, run), 0, ExtensionControlsModel::StateRole), u"skipped"_s);
        // The streaks: Medytacja done the two days before today.
        const QString streak = role(model, med + 1, ExtensionViewModel::TextRole).toString();
        QVERIFY2(streak.contains(u'2') && streak.contains(u"100%"_s), qPrintable(streak));
        QVERIFY2(role(model, read + 1, ExtensionViewModel::TextRole).toString().contains(u"2 z 3"_s)
                         || role(model, read + 1, ExtensionViewModel::TextRole).toString().contains(u"2 of 3"_s),
                 qPrintable(role(model, read + 1, ExtensionViewModel::TextRole).toString()));
        // The month: done, due today (tappable), future (not), outside before the first check.
        const int grid = med + 2;
        QCOMPARE(role(model, grid, ExtensionViewModel::DaysRole).toList().size(), 30);
        QCOMPARE(dayState(model, grid, u"2026-11-02"_s), u"done"_s);
        QCOMPARE(dayState(model, grid, u"2026-11-04"_s), u"due"_s);
        QCOMPARE(dayState(model, grid, u"2026-11-05"_s), u"future"_s);
        QCOMPARE(dayState(model, grid, u"2026-11-01"_s), u"outside"_s);
        const QVariantMap future = role(model, grid, ExtensionViewModel::DaysRole).toList().at(4).toMap();
        QVERIFY(!future.value(u"tappable"_s).toBool());
        const QVariantMap todayCell = role(model, grid, ExtensionViewModel::DaysRole).toList().at(3).toMap();
        QVERIFY(todayCell.value(u"tappable"_s).toBool() && todayCell.value(u"menu"_s).toBool());
        // The problems: a schedule not understood, a second definition of a name — a link each to
        // the definition (at its line).
        QCOMPARE(texts(model, u"link"_s).size(), 2);
        QSignalSpy pages(&model, &ExtensionViewModel::pageRequested);
        for (int i = 0; i < model.rowCount(); ++i)
            if (role(model, i, ExtensionViewModel::TypeRole).toString() == u"link")
                model.activate(i);
        QCOMPARE(pages.count(), 2);
        QCOMPARE(pages.at(0).at(0).toString(), u"Nawyki.md"_s);
        QCOMPARE(pages.at(0).at(1).toInt(), 6); // "- Coś" is the 7th line
        QCOMPARE(pages.at(1).at(0).toString(), u"Zzz/Inne.md"_s);
        QCOMPARE(pages.at(1).at(1).toInt(), 0);
        QStringList dangers;
        for (int i = 0; i < model.rowCount(); ++i)
            if (role(model, i, ExtensionViewModel::ToneRole).toString() == u"danger")
                dangers << role(model, i, ExtensionViewModel::TextRole).toString();
        QCOMPARE(dangers.size(), 2);
        // A menu's journal and definition.
        QSignalSpy journals(&model, &ExtensionViewModel::journalRequested);
        const QStringList labels = model.menuLabels(med, 3, QString());
        for (int i = 0; i < labels.size(); ++i)
            if (model.menuIcons(med, 3, QString()).at(i) == u"book-open")
                model.activateMenuItem(med, 3, QString(), i);
        QCOMPARE(journals.count(), 1);
        QCOMPARE(journals.first().at(0).toString(), u"2026-11-04"_s);
    }

    void anotherMonth()
    {
        ExtensionViewModel model;
        draw(model, u"2026-10"_s);
        const int med = rowOf(model, u"Medytacja"_s);
        QVERIFY(med > 0);
        QCOMPARE(role(model, med + 2, ExtensionViewModel::MonthRole).toString(), u"2026-10"_s);
        QCOMPARE(role(model, med + 2, ExtensionViewModel::DaysRole).toList().size(), 31);
        QCOMPARE(dayState(model, med + 2, u"2026-10-31"_s), u"outside"_s);
        // Across the year; a month that is no month: this one.
        model.setParamValue(u"2027-01"_s);
        model.refresh();
        QCOMPARE(role(model, rowOf(model, u"Medytacja"_s) + 2, ExtensionViewModel::DaysRole).toList().size(), 31);
        QCOMPARE(dayState(model, rowOf(model, u"Medytacja"_s) + 2, u"2027-01-01"_s), u"future"_s);
        model.setParamValue(u"bzdura"_s);
        model.refresh();
        QCOMPARE(role(model, rowOf(model, u"Medytacja"_s) + 2, ExtensionViewModel::MonthRole).toString(), u"2026-11"_s);
    }

    void tickedThroughTheView()
    {
        ExtensionViewModel model;
        draw(model);
        int med = rowOf(model, u"Medytacja"_s);
        ExtensionControlsModel *controls = controlsOf(model, med);
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        // Today's check: the day's check ticked in today's journal; in place at once.
        model.activateControl(med, 0);
        QVERIFY2(disk(kToday).startsWith(u"- [x] Medytacja #nawyk ✅ 2026-11-04\n"_s), qPrintable(disk(kToday)));
        QCOMPARE(control(controls, 0, ExtensionControlsModel::StateRole), u"done"_s);
        QCOMPARE(dayState(model, med + 2, u"2026-11-04"_s), u"done"_s);
        QCOMPARE(m_messages.size(), 1);
        QVERIFY2(m_messages.first().startsWith(u"success: "_s) && m_messages.first().endsWith(u"[Cofnij]"_s),
                 qPrintable(m_messages.first()));
        // Undo in the toast: the state before (open) written back.
        m_ext->pressButton(m_buttons.last());
        QTRY_COMPARE(control(controls, 0, ExtensionControlsModel::StateRole), u"open"_s);
        QVERIFY2(disk(kToday).startsWith(u"- [ ] Medytacja #nawyk\n"_s), qPrintable(disk(kToday)));
        // Again, then a second tap opens it (the line stays).
        model.activateControl(med, 0);
        model.activateControl(med, 0);
        QVERIFY(disk(kToday).startsWith(u"- [ ] Medytacja #nawyk\n"_s));
        QCOMPARE(control(controls, 0, ExtensionControlsModel::StateRole), u"open"_s);
        // A past day of the grid; a day's menu: skipped.
        model.activateDay(med + 2, u"2026-11-01"_s);
        QVERIFY2(disk(u"journals/2026-11-01.md"_s).contains(u"- [x] Medytacja #nawyk"_s),
                 qPrintable(disk(u"journals/2026-11-01.md"_s)));
        QCOMPARE(dayState(model, med + 2, u"2026-11-01"_s), u"done"_s);
        const QStringList dayMenu = model.menuLabels(med + 2, -1, u"2026-11-03"_s);
        QCOMPARE(dayMenu.size(), 5);
        model.activateMenuItem(med + 2, -1, u"2026-11-03"_s, 1); // skipped
        QVERIFY2(disk(u"journals/2026-11-03.md"_s).startsWith(u"- [-] Medytacja #nawyk ❌ 2026-11-04\n"_s),
                 qPrintable(disk(u"journals/2026-11-03.md"_s)));
        QCOMPARE(dayState(model, med + 2, u"2026-11-03"_s), u"skipped"_s);
        // The future: nothing to tap.
        const QString before = disk(u"journals/2026-11-05.md"_s);
        model.activateDay(med + 2, u"2026-11-05"_s);
        QCOMPARE(disk(u"journals/2026-11-05.md"_s), before);
        // A tick written in the journal itself: drawn again (in place, no reset).
        QVERIFY(m_vault->writeText(kToday, u"- [x] Medytacja #nawyk\n- [-] Bieganie #nawyk\n- notatka\n"_s,
                                   ChangeSource::External));
        QTRY_COMPARE(control(controls, 0, ExtensionControlsModel::StateRole), u"done"_s);
        QCOMPARE(controlsOf(model, med), controls);
        QCOMPARE(resets.count(), 0);
        // A page habit: the line links to its page.
        QVERIFY(m_vault->writeText(u"Joga.md"_s, u"habit:: codziennie\n\nRozciąganie.\n"_s, ChangeSource::External));
        QTRY_VERIFY(rowOf(model, u"Joga"_s) > 0);
        model.activateControl(rowOf(model, u"Joga"_s), 0);
        QVERIFY2(disk(kToday).contains(u"- [x] [[Joga]] #nawyk ✅ 2026-11-04\n"_s), qPrintable(disk(kToday)));
    }

    void addHabitForm()
    {
        // "Add habit": a form with two text fields; a schedule not understood and a name in use are
        // asked again; the definition goes to the page of the first definition.
        m_ext->runCommand(u"nawyki"_s, u"add"_s);
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        ExtensionFormModel *fields = m_ext->questionFields();
        QCOMPARE(fields->rowCount(), 2);
        QCOMPARE(fields->data(fields->index(0), ExtensionFormModel::KindRole).toString(), u"text"_s);
        QCOMPARE(fields->data(fields->index(1), ExtensionFormModel::KindRole).toString(), u"text"_s);
        QVERIFY(!fields->data(fields->index(1), ExtensionFormModel::PlaceholderRole).toString().isEmpty());
        QCOMPARE(fields->firstTextField(), 0);
        fields->setText(0, u"Rozciąganie"_s);
        fields->setText(1, u"co drugi wtorek"_s);
        int serial = m_ext->questionSerial();
        m_ext->answerForm();
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        QVERIFY(m_ext->questionSerial() > serial);
        QVERIFY(m_ext->questionTitle().contains(u'\n')); // why, under the title
        QCOMPARE(fields->data(fields->index(0), ExtensionFormModel::TextRole).toString(), u"Rozciąganie"_s);
        QCOMPARE(fields->data(fields->index(1), ExtensionFormModel::TextRole).toString(), u"co drugi wtorek"_s);
        fields->setText(0, u"MEDYTACJA"_s);
        fields->setText(1, QString());
        serial = m_ext->questionSerial();
        m_ext->answerForm();
        QVERIFY(m_ext->questionSerial() > serial); // a name in use
        fields->setText(0, u"Rozciąganie"_s);
        m_ext->answerForm();
        QVERIFY(m_ext->questionKind().isEmpty());
        QVERIFY2(disk(u"Nawyki.md"_s).endsWith(u"- Rozciąganie\n  habit:: codziennie\n"_s), qPrintable(disk(u"Nawyki.md"_s)));
        QCOMPARE(m_messages.size(), 1);
        QVERIFY2(m_messages.first().startsWith(u"success: "_s) && m_messages.first().contains(u"Nawyki"_s),
                 qPrintable(m_messages.first()));
        // Its button opens the page.
        QSignalSpy pages(m_ext.get(), &WorkspaceExtensions::pageRequested);
        m_ext->pressButton(m_buttons.last());
        QCOMPARE(pages.count(), 1);
        QCOMPARE(pages.first().at(0).toString(), u"Nawyki"_s);
        // Cancelled: nothing written.
        const QString page = disk(u"Nawyki.md"_s);
        m_ext->runCommand(u"nawyki"_s, u"add"_s);
        m_ext->cancelQuestion();
        QCOMPARE(disk(u"Nawyki.md"_s), page);
    }

    void switchedOff()
    {
        ExtensionViewModel model;
        draw(model);
        QVERIFY(model.rowCount() > 0);
        QVERIFY(m_ext->habitsEnabled());
        m_ext->setEnabled({});
        QVERIFY(!m_ext->habitsEnabled());
        QTRY_COMPARE(model.rowCount(), 0);
        QVERIFY(model.error().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestShellHabits)
#include "tst_habits.moc"
