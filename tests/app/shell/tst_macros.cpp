// Macros, template variables, view blocks and panels as the app runs them (workspacemacros.cpp,
// extensionviewmodel.h; docs/EXTENSIONS.md "Macros", "View blocks and panels"): what a macro shows
// before, while and after it is computed — never by running Luau while drawing —, the placeholder
// of an extension that waits for approval and the value once it runs, a value computed again when
// a note it read changes (and only then), a macro that times out given up on, the export's texts;
// a template's variables; a ```view block's source and the note's params, a view that is not live,
// the new controls (text field, select, slider, date, switch, progress bar) with their actions;
// the side panel's sections and the views a block can show.

#include "workspaceextensions.h"

#include "extensionviewmodel.h"
#include "index/vaultindex.h"
#include "io/files.h"
#include "util/paths.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;
using markdown::MacroView;

namespace {

const QString kPage = u"Rozszerzenia/Pomiary.md"_s;
const QString kJournal = u"journals/2026-11-20.md"_s;

/// The extension "Pomiary" (id "pomiary"): macros over a page read by name and over a query, a
/// macro that loops, a template variable, a view of every new control (its actions note what
/// they got) and two panels.
const QString kExtension = u"---\ntags: [rozszerzenie]\nuprawnienia: [zapis]\n---\n```luau\n"
        "local got = {}\n"
        "b.macro(\"waga\", function(ctx)\n"
        "    local text = b.read(\"Waga\") or \"\"\n"
        "    local last = text:match(\"(%d+[,.]?%d*) kg%s*$\") or \"?\"\n"
        "    return { b.ui.heading(last .. \" kg\"), \" \", b.ui.link(\"Waga\") }\n"
        "end, { description = \"Ostatni pomiar wagi\", example = \"\" })\n"
        "b.macro(\"zadania\", function(ctx)\n"
        "    return b.ui.text(`{b.query(\"type:tasks status:open\", 1).total} otwartych`, { tone = \"danger\" })\n"
        "end, { description = \"Otwarte zadania\" })\n"
        "b.macro(\"petla\", function(ctx) while true do end end)\n"
        "b.macro(\"echo\", function(ctx) return `{ctx.page}|{ctx.date or \"-\"}|{table.concat(ctx.args, \";\")}` end)\n"
        "b.variable(\"dzien\", function(ctx) return `- Pomiar z {ctx.date}\\n- {ctx.page}` end)\n"
        "b.view(\"kontrolki\", function(params)\n"
        "    return b.ui.column({\n"
        "        b.ui.text(`{params.page or \"-\"} {params.date or \"-\"} {params.kolor or \"-\"}`),\n"
        "        b.ui.input({ placeholder = \"Pomiar\", action = \"zapisz\", label = \"Waga\", submit = \"Zapisz\" }),\n"
        "        b.ui.row({\n"
        "            b.ui.select({ options = { \"mały\", { label = \"Duży\", value = \"d\" } }, value = \"d\", action = \"wybierz\" }),\n"
        "            b.ui.date({ value = \"2026-11-21\", action = \"dzien\" }),\n"
        "            b.ui.toggle(true, \"wlacz\", { id = 7 }, { label = \"Włączone\" }),\n"
        "        }),\n"
        "        b.ui.slider({ value = 3, min = 1, max = 5, step = 0.5, unit = \"pkt\", action = \"ocena\" }),\n"
        "        b.ui.progress({ value = 3, max = 4, label = \"3 z 4\", tone = \"success\" }),\n"
        "        b.ui.text(table.concat(got, \"|\")),\n"
        "    })\n"
        "end, {\n"
        "    zapisz = function(args) table.insert(got, \"zapisz=\" .. args.value) end,\n"
        "    wybierz = function(args) table.insert(got, \"wybierz=\" .. args.value) end,\n"
        "    dzien = function(args) table.insert(got, \"dzien=\" .. args.value) end,\n"
        "    wlacz = function(args) table.insert(got, `wlacz={args.value}/{args.id}`) end,\n"
        "    ocena = function(args) table.insert(got, \"ocena=\" .. args.value) end,\n"
        "}, { label = \"Kontrolki\", icon = \"sliders-horizontal\", panel = \"journal\" })\n"
        "b.view(\"bok\", function(params) return b.ui.text(params.path or \"-\") end, nil, { panel = true })\n"
        "b.view(\"lewy\", function(params) return b.ui.text(\"po lewej\") end, nil, { panel = \"sidebar\" })\n"
        "```\n"_s;

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

class TestShellMacros : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<WorkspaceExtensions> m_ext;
    QStringList m_messages;

    markdown::MacroSpan span(const QString &name, const QString &arguments = QString(), const QString &ext = u"pomiary"_s)
    {
        markdown::MacroSpan s;
        s.extension = ext;
        s.name = name;
        s.arguments = arguments;
        return s;
    }
    /// What the macro shows once it is computed (the app's event loop runs the calls).
    MacroView settled(const markdown::MacroSpan &macro, const QString &path = kJournal)
    {
        MacroView view = m_ext->macroView(macro, path);
        for (int i = 0; i < 400 && view.state == MacroView::State::Pending; ++i) {
            QTest::qWait(5);
            view = m_ext->macroView(macro, path);
        }
        return view;
    }

    void open()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
        m_messages.clear();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        const QHash<QString, QString> files{
                {kPage, kExtension},
                {u"Waga.md"_s, u"- 2026-11-18: 82,5 kg\n- 2026-11-19: 82 kg\n"_s},
                {kJournal, u"- Waga: {{ext:pomiary/waga}}\n- [ ] Zadanie\n"_s},
                {u"Zadania.md"_s, u"- [ ] Pierwsze\n- [x] Zrobione\n"_s},
        };
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
        m_ext = std::make_unique<WorkspaceExtensions>([] { return QDateTime(QDate(2026, 11, 20), QTime(18, 30)); });
        connect(m_ext.get(), &WorkspaceExtensions::message, this,
                [this](const QString &text, const QString &kind) { m_messages << kind + u": "_s + text; });
        connect(m_vault.get(), &Vault::changed, this, [this](const QList<VaultChange> &changes) {
            QStringList changed;
            for (const VaultChange &c : changes) {
                bool ok = false;
                const QString content = m_vault->readText(c.path, &ok);
                if (ok)
                    m_index->update(c.path, content);
                changed << c.path;
            }
            m_ext->notesChanged(changed);
        });
        m_ext->attach(m_vault.get(), m_index.get());
        m_ext->indexReady();
    }

private Q_SLOTS:
    void init() { open(); }
    void cleanup()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
    }

    void placeholderUntilApproved()
    {
        // A page that came with the notebook waits for approval: its macros say so, without a call.
        QCOMPARE(m_ext->pageState(kPage), u"approval"_s);
        QCOMPARE(m_ext->macroView(span(u"waga"_s), kJournal).state, MacroView::State::Unapproved);
        QCOMPARE(m_ext->macroExtension(u"pomiary"_s).value(u"state"_s).toString(), u"approval"_s);
        QCOMPARE(m_ext->macroExtension(u"pomiary"_s).value(u"path"_s).toString(), kPage);
        QCOMPARE(m_ext->macroView(span(u"x"_s, {}, u"nikt"_s), kJournal).state, MacroView::State::Unknown);
        QCOMPARE(m_ext->macroExtension(u"nikt"_s).value(u"state"_s).toString(), u"unknown"_s);
        // A built-in that is off.
        QCOMPARE(m_ext->macroView(span(u"rekord"_s, u"Przysiad"_s, u"silownia"_s), kJournal).state, MacroView::State::Off);
        QSignalSpy changed(m_ext.get(), &WorkspaceExtensions::macrosChanged);
        m_ext->approve(kPage);
        QCOMPARE(m_ext->pageState(kPage), u"running"_s);
        QTRY_VERIFY(changed.count() > 0); // the placeholders become values
        // First "…", then the value, without anything else asking.
        const MacroView first = m_ext->macroView(span(u"waga"_s), kJournal);
        QCOMPARE(first.state, MacroView::State::Pending);
        const MacroView view = settled(span(u"waga"_s));
        QCOMPARE(markdown::macroViewText(view), u"82 kg [[Waga]]"_s);
        QCOMPARE(m_ext->macroView(span(u"nie-ma"_s), kJournal).state, MacroView::State::Unknown);
        // Switched off on this device: off.
        m_ext->setPageOn(kPage, false);
        QCOMPARE(m_ext->macroView(span(u"waga"_s), kJournal).state, MacroView::State::Off);
    }

    void computedAgainWhenWhatItReadChanges()
    {
        m_ext->approve(kPage);
        QCOMPARE(markdown::macroViewText(settled(span(u"waga"_s))), u"82 kg [[Waga]]"_s);
        QCOMPARE(markdown::macroViewText(settled(span(u"zadania"_s))), u"2 otwartych"_s);
        // Another note: the macro that read "Waga" by name stays as it is, the query's goes again.
        QSignalSpy changed(m_ext.get(), &WorkspaceExtensions::macrosChanged);
        QVERIFY(m_vault->writeText(u"Zadania.md"_s, u"- [x] Pierwsze\n- [x] Zrobione\n"_s, ChangeSource::External));
        QTRY_COMPARE(markdown::macroViewText(m_ext->macroView(span(u"zadania"_s), kJournal)), u"1 otwartych"_s);
        QCOMPARE(markdown::macroViewText(m_ext->macroView(span(u"waga"_s), kJournal)), u"82 kg [[Waga]]"_s);
        QTRY_VERIFY(changed.count() > 0); // (coalesced: after the value)
        // The page it read: its new value, the old one shown meanwhile.
        QVERIFY(m_vault->writeText(u"Waga.md"_s, u"- 2026-11-20: 81,5 kg\n"_s, ChangeSource::External));
        QCOMPARE(markdown::macroViewText(m_ext->macroView(span(u"waga"_s), kJournal)), u"82 kg [[Waga]]"_s);
        QTRY_COMPARE(markdown::macroViewText(m_ext->macroView(span(u"waga"_s), kJournal)), u"81,5 kg [[Waga]]"_s);
        // The note's context.
        QCOMPARE(markdown::macroViewText(settled(span(u"echo"_s, u"a, \"b, c\""_s))), u"2026-11-20|2026-11-20|a;b, c"_s);
        QCOMPARE(markdown::macroViewText(settled(span(u"echo"_s), u"Waga.md"_s)), u"Waga|-|"_s);
    }

    void aLoopIsGivenUpOn()
    {
        m_ext->approve(kPage);
        QElapsedTimer clock;
        clock.start();
        const MacroView view = settled(span(u"petla"_s));
        QCOMPARE(view.state, MacroView::State::Error);
        QVERIFY2(view.message.contains(u"250"_s), qPrintable(view.message));
        // In the console, once.
        QCOMPARE(m_ext->console()->text().count(u"{{ext:pomiary/petla}}"_s), 1);
        // A change makes it try again — once more, then never until the extension loads again.
        QVERIFY(m_vault->writeText(u"Zadania.md"_s, u"- [ ] x\n"_s, ChangeSource::External));
        QTest::qWait(400 * BRULION_TEST_SLOWDOWN);
        QVERIFY(m_vault->writeText(u"Zadania.md"_s, u"- [ ] y\n"_s, ChangeSource::External));
        QTest::qWait(400 * BRULION_TEST_SLOWDOWN);
        QCOMPARE(m_ext->macroView(span(u"petla"_s), kJournal).state, MacroView::State::Error);
        // The other macros of the page still work.
        QCOMPARE(markdown::macroViewText(settled(span(u"zadania"_s))), u"2 otwartych"_s);
    }

    void templateVariablesAndExports()
    {
        m_ext->approve(kPage);
        const QVariantMap context{{u"page"_s, u"2026-11-21"_s}, {u"path"_s, u"journals/2026-11-21.md"_s},
                                  {u"date"_s, u"2026-11-21"_s}, {u"template"_s, u"templates/journal.md"_s}};
        QCOMPARE(m_ext->templateVariable(u"pomiary"_s, u"dzien"_s, QString(), context),
                 std::optional<QString>(u"- Pomiar z 2026-11-21\n- 2026-11-21"_s));
        QCOMPARE(m_ext->templateVariable(u"pomiary"_s, u"brak"_s, QString(), context), std::nullopt);
        QCOMPARE(m_ext->templateVariable(u"nikt"_s, u"dzien"_s, QString(), context), std::nullopt);
        QVERIFY(m_messages.isEmpty()); // (an unknown one says nothing)
        // An export's documents get what the macros show, computed now when they were not yet.
        const QList<io::FileEntry> files{
                {kJournal, readFile(m_dir + u'/' + kJournal).toUtf8()},
                {u"Waga.md"_s, readFile(m_dir + u"/Waga.md"_s).toUtf8()},
        };
        const auto texts = m_ext->macroTexts(files);
        QCOMPARE(texts.size(), 1);
        QCOMPARE(texts.value(kJournal).value(u"{{ext:pomiary/waga}}"_s), u"**82 kg** [[Waga]]"_s);
    }

    void viewBlockAndControls()
    {
        m_ext->approve(kPage);
        ExtensionViewModel model;
        model.setExtensions(m_ext.get());
        // A ```view block's text: the view, its params; the note's page and date join them.
        model.setSource(u"# a comment\npomiary/kontrolki\nkolor: zielony\n"_s);
        QCOMPARE(model.extension(), u"pomiary"_s);
        QCOMPARE(model.view(), u"kontrolki"_s);
        QCOMPARE(model.params(), (QVariantMap{{u"kolor"_s, u"zielony"_s}}));
        QVERIFY(model.sourceProblem().isEmpty());
        model.setNotePath(kJournal);
        model.refresh();
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QCOMPARE(model.data(model.index(0), ExtensionViewModel::TextRole).toString(), u"2026-11-20 2026-11-20 zielony"_s);
        // A lone input is a bare row; the row of three keeps its card.
        QStringList kinds;
        QList<bool> bare;
        for (int i = 0; i < model.rowCount(); ++i) {
            kinds << model.data(model.index(i), ExtensionViewModel::TypeRole).toString();
            bare << model.data(model.index(i), ExtensionViewModel::BareRole).toBool();
        }
        QCOMPARE(kinds, (QStringList{u"text"_s, u"row"_s, u"row"_s, u"row"_s, u"row"_s, u"text"_s}));
        QCOMPARE(bare, (QList<bool>{false, true, false, true, true, false}));
        const auto controls = [&](int row) {
            return qobject_cast<ExtensionControlsModel *>(model.data(model.index(row), ExtensionViewModel::ControlsRole).value<QObject *>());
        };
        const auto role = [&](int row, int control, int r) { return controls(row)->data(controls(row)->index(control), r); };
        QCOMPARE(role(1, 0, ExtensionControlsModel::TypeRole).toString(), u"input"_s);
        QCOMPARE(role(1, 0, ExtensionControlsModel::PlaceholderRole).toString(), u"Pomiar"_s);
        QCOMPARE(role(1, 0, ExtensionControlsModel::SubmitRole).toString(), u"Zapisz"_s);
        QCOMPARE(role(2, 0, ExtensionControlsModel::TextRole).toString(), u"Duży"_s); // the chosen option
        QVERIFY(role(2, 0, ExtensionControlsModel::HasMenuRole).toBool());
        QCOMPARE(role(2, 1, ExtensionControlsModel::TextRole).toString(), u"2026-11-21"_s);
        QVERIFY(role(2, 2, ExtensionControlsModel::CheckedRole).toBool());
        QCOMPARE(role(3, 0, ExtensionControlsModel::MaxRole).toDouble(), 5.0);
        QCOMPARE(role(3, 0, ExtensionControlsModel::StepRole).toDouble(), 0.5);
        QCOMPARE(role(4, 0, ExtensionControlsModel::MaxRole).toDouble(), 4.0);
        QCOMPARE(role(4, 0, ExtensionControlsModel::ToneRole).toString(), u"success"_s);
        // Their actions get what was typed or chosen.
        QVERIFY(model.submitText(1, 0, u"81,5 kg — żółw"_s));
        QCOMPARE(model.menuLabels(2, 0, QString()), (QStringList{u"mały"_s, u"Duży"_s}));
        QCOMPARE(model.menuIcons(2, 0, QString()), (QStringList{QString(), u"check"_s}));
        model.activateMenuItem(2, 0, QString(), 0);
        QVERIFY(model.submitText(2, 1, u"2026-12-01"_s));
        QVERIFY(model.submitText(2, 1, QString())); // "No date"
        QVERIFY(!model.submitText(2, 1, u"2026-02-30"_s)); // no such day: nothing
        QVERIFY(!model.submitText(2, 2, u"tekst"_s)); // a switch takes no text
        model.activateControl(2, 2, 0);
        QCOMPARE(model.snapped(3, 0, 3.7), 3.5);
        QCOMPARE(model.snapped(3, 0, 99), 5.0);
        QCOMPARE(model.snapped(3, 0, -4), 1.0);
        model.activateControl(3, 0, model.snapped(3, 0, 4.2));
        model.refresh();
        QCOMPARE(model.data(model.index(5), ExtensionViewModel::TextRole).toString(),
                 u"zapisz=81,5 kg — żółw|wybierz=mały|dzien=2026-12-01|dzien=|wlacz=false/7|ocena=4"_s);
        QCOMPARE(model.formatNumber(3.5), u"3,5"_s);
    }

    void notLiveAndExtensionStates()
    {
        ExtensionViewModel model;
        model.setExtensions(m_ext.get());
        model.setSource(u"pomiary/bok"_s);
        model.setNotePath(kJournal);
        model.refresh();
        // Not approved yet: nothing drawn, its extension's state.
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.extensionState(), u"approval"_s);
        model.setLive(false);
        m_ext->approve(kPage);
        model.refresh(); // not live: nothing drawn, stale
        QCOMPARE(model.rowCount(), 0);
        model.setLive(true);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.extensionState(), QString());
        QCOMPARE(model.data(model.index(0), ExtensionViewModel::TextRole).toString(), kJournal);
        // A block that names no view.
        ExtensionViewModel broken;
        broken.setExtensions(m_ext.get());
        broken.setSource(u"bez ukośnika"_s);
        QVERIFY(broken.sourceProblem().contains(u"<extension>/<view>"_s));
        QCOMPARE(broken.extension(), QString());
    }

    void panelsAndEmbeddableViews()
    {
        QVERIFY(m_ext->panels(u"journal"_s).isEmpty());
        m_ext->approve(kPage);
        QStringList journal, page;
        for (const QVariant &v : m_ext->panels(u"journal"_s))
            journal << v.toMap().value(u"view"_s).toString() + u':' + v.toMap().value(u"title"_s).toString();
        for (const QVariant &v : m_ext->panels(u"page"_s))
            page << v.toMap().value(u"view"_s).toString();
        QCOMPARE(journal, (QStringList{u"kontrolki:Kontrolki"_s, u"bok:Pomiary"_s}));
        QCOMPARE(page, QStringList{u"bok"_s});
        // The pages panel at the left: only what asks for it.
        QStringList left;
        for (const QVariant &v : m_ext->panels(u"sidebar"_s))
            left << v.toMap().value(u"view"_s).toString();
        QCOMPARE(left, QStringList{u"lewy"_s});
        // A block can show the views with a label.
        QStringList sources;
        for (const QVariant &v : m_ext->embeddableViews())
            sources << v.toMap().value(u"source"_s).toString();
        QCOMPARE(sources, QStringList{u"pomiary/kontrolki"_s});
        // The editor's {{ completion.
        const QVariantList all = m_ext->macroSuggestions(QString(), 20);
        QCOMPARE(all.size(), 4);
        const QVariantList found = m_ext->macroSuggestions(u"wag"_s, 20);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].toMap().value(u"title"_s).toString(), u"pomiary/waga"_s);
        QCOMPARE(found[0].toMap().value(u"insertText"_s).toString(), u"ext:pomiary/waga"_s);
        QVERIFY(found[0].toMap().value(u"subtitle"_s).toString().startsWith(u"Ostatni pomiar wagi"_s));
        QCOMPARE(m_ext->macroSuggestions(u"otwarte"_s, 20).size(), 1); // by its description
    }
};

QTEST_MAIN(TestShellMacros)
#include "tst_macros.moc"
