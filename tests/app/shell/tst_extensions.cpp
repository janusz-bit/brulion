// The extensions as the app runs them (src/app/shell/workspaceextensions.h), on the sample
// extensions and their notebook (tests/core/extensions/samples): pages that arrive wait for
// approval and the approved ones run; commands ask through the question the app shows; views draw
// and their actions write; "save" and "change" hooks as the Workspace delivers them — what an
// extension writes never comes back to the hooks, what the user changes always does; code an
// extension writes waits for approval; the editor's own save of an approved page keeps it
// approved, a save that does not load keeps the version before it running. A view as the app draws
// it (extensionviewmodel.h, on the gym's "today"): numbers typed into its steppers, the view drawn
// again in place at once after an action, a change of kinds after the timer.
//
// The Workspace's part is played as Workspace::onVaultChanged and Workspace::write play it: the
// vault reports a change while it is written (the index first, then notesChanged()), and the
// editor's save says savedByEditor() first.

#include "workspaceextensions.h"

#include "ai/local.h"
#include "extensionviewmodel.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include "testtiming.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;

namespace {

const QString kSamples = QStringLiteral(BRULION_SOURCE_DIR) + u"/tests/core/extensions/samples"_s;
const QString kFolder = u"Rozszerzenia"_s;
const QString kJournal = u"journals/2026-11-14.md"_s;

/// An extension that counts the "change" events it is told about in a page it writes itself, and
/// has a command that writes another page.
const QString kCounter = u"---\ntags: [extension]\npermissions: [write]\n---\n```luau\n"
                         "local n = 0\n"
                         "b.on(\"change\", function(e)\n"
                         "    n += 1\n"
                         "    b.write(\"Licznik\", `zmiany: {n} ({table.concat(e.pages, \", \")})\\n`)\n"
                         "end)\n"
                         "b.command(\"pisz\", function(ctx)\n"
                         "    b.write(\"Z polecenia\", \"pisze polecenie\\n\")\n"
                         "    b.appendToJournal(b.today(), \"- wpis polecenia\")\n"
                         "    return \"napisane\"\n"
                         "end)\n```\n"_s;

/// An extension (page "Węzły", id "wezly") with the newer parts of the API the app draws: ctx.args,
/// results as tables with buttons, a form's text fields, and a view of every newer node — a text
/// with an icon and a tone, a link at a line, a card, a menu, a check that is skipped and has a
/// menu, a month grid, an empty state with an icon. What its actions set lives in `marks`.
const QString kNodes = u"---\ntags: [extension]\n---\n```luau\n"
                       "local marks = { [\"dziś\"] = \"skipped\" }\n"
                       "b.command(\"echo\", function(ctx)\n"
                       "    local n = 0\n"
                       "    for _ in ctx.args do n += 1 end\n"
                       "    return { message = `słowo: {ctx.args.word or \"-\"} ({n})`, tone = \"success\",\n"
                       "             button = { label = \"Jeszcze\", command = \"echo\", args = { word = \"żółw\" } } }\n"
                       "end)\n"
                       "b.command(\"strona\", function(ctx)\n"
                       "    return { message = \"Gotowe\", tone = \"info\", button = { label = \"Otwórz\", page = \"Żółwie\" } }\n"
                       "end)\n"
                       "b.command(\"blad\", function(ctx) return { message = \"Źle\", tone = \"error\" } end)\n"
                       "b.command(\"cisza\", function(ctx) return { tone = \"error\" } end)\n"
                       "b.command(\"formularz\", function(ctx)\n"
                       "    local a = b.form(\"Nowy nawyk\", {\n"
                       "        { key = \"ile\", label = \"Ile\", value = 2 },\n"
                       "        { key = \"nazwa\", label = \"Nazwa\", kind = \"text\", value = \"Żółć\", placeholder = \"np. Czytanie\" },\n"
                       "    })\n"
                       "    if a == nil then return \"anulowano\" end\n"
                       "    return `{a.nazwa}/{a.ile}`\n"
                       "end)\n"
                       "local function menu(day)\n"
                       "    return {\n"
                       "        { label = \"Zrobione\", action = \"ustaw\", args = { day = day, state = \"done\" }, icon = \"check\" },\n"
                       "        nil,\n"
                       "        { label = \"Bez etykiety\" },\n"
                       "        { label = \"Dziennik\", journal = \"2026-11-03\" },\n"
                       "        { label = \"Definicja\", page = \"Nawyki\", line = 3 },\n"
                       "    }\n"
                       "end\n"
                       "b.view(\"widok\", function(params)\n"
                       "    if params.pusto then return b.ui.empty(\"Pusto\", \"Nic tu nie ma\", \"flame\") end\n"
                       "    local state = marks[\"dziś\"]\n"
                       "    local checked = if state == \"done\" then true elseif state == \"skipped\" then \"skipped\" else false\n"
                       "    return b.ui.column({\n"
                       "        b.ui.text(\"Seria: 3 dni\", { icon = \"flame\", tone = \"success\" }),\n"
                       "        b.ui.card({\n"
                       "            b.ui.row({\n"
                       "                b.ui.check(checked, \"ustaw\", { day = \"dziś\", state = \"done\" }, { menu = menu(\"dziś\"), label = \"Medytacja\" }),\n"
                       "                b.ui.text(\"Medytacja\"),\n"
                       "                b.ui.text(\"źle\", { tone = \"danger\" }),\n"
                       "                b.ui.menu(menu(\"dziś\"), \"Więcej\"),\n"
                       "            }),\n"
                       "            b.ui.link(\"Nawyki\", \"Definicja\", 3),\n"
                       "            b.ui.month({ month = \"2026-11\", label = \"listopad 2026\", days = {\n"
                       "                [\"2026-11-02\"] = { state = marks[\"2026-11-02\"] or \"missed\", action = \"ustaw\",\n"
                       "                                     args = { day = \"2026-11-02\", state = \"done\" }, menu = menu(\"2026-11-02\") },\n"
                       "                [\"2026-11-14\"] = { state = \"due\", action = \"ustaw\", args = { day = \"2026-11-14\", state = \"done\" } },\n"
                       "                [\"2026-11-20\"] = { state = \"future\" },\n"
                       "                [\"2026-11-21\"] = { state = \"cokolwiek\" },\n"
                       "            } }),\n"
                       "        }),\n"
                       "        b.ui.card({ b.ui.text(\"sama\") }),\n"
                       "        b.ui.month({ month = \"bzdura\", days = {} }),\n"
                       "        b.ui.menu(menu(\"dziś\")),\n"
                       "    })\n"
                       "end, {\n"
                       "    ustaw = function(args)\n"
                       "        local before = marks[args.day] or \"open\"\n"
                       "        if before == args.state then return nil end\n"
                       "        marks[args.day] = args.state\n"
                       "        return { message = `{args.day}: {args.state}`, tone = \"success\",\n"
                       "                 button = { label = \"Cofnij\", action = \"ustaw\", args = { day = args.day, state = before } } }\n"
                       "    end,\n"
                       "})\n```\n"_s;

QString readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList filesUnder(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

/// The nodes of a view's tree of `type`, in document order.
void collect(const QVariant &node, const QString &type, QList<QVariantMap> *out)
{
    const QVariantMap map = node.toMap();
    if (map.value(u"type"_s).toString() == type)
        out->append(map);
    for (const QVariant &child : map.value(u"children"_s).toList())
        collect(child, type, out);
}

QStringList textsOf(const QVariantMap &tree, const QString &type)
{
    QList<QVariantMap> nodes;
    collect(tree, type, &nodes);
    QStringList out;
    for (const QVariantMap &node : std::as_const(nodes))
        out << node.value(u"text"_s).toString();
    return out;
}

/// The kinds of a view model's lines (lineType).
QStringList lineKinds(const ExtensionViewModel &model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out << model.data(model.index(i), ExtensionViewModel::TypeRole).toString();
    return out;
}

/// The controls of a view model's row line.
ExtensionControlsModel *controlsOf(const ExtensionViewModel &model, int row)
{
    return qobject_cast<ExtensionControlsModel *>(model.data(model.index(row), ExtensionViewModel::ControlsRole).value<QObject *>());
}

QStringList controlKinds(const ExtensionControlsModel *controls)
{
    QStringList out;
    for (int i = 0; controls && i < controls->rowCount(); ++i)
        out << controls->data(controls->index(i), ExtensionControlsModel::TypeRole).toString();
    return out;
}

QString controlRole(const ExtensionControlsModel *controls, int i, int role)
{
    return controls ? controls->data(controls->index(i), role).toString() : QString();
}

/// The rows of a model's dataChanged signals, sorted.
QList<int> changedRows(const QSignalSpy &spy)
{
    QList<int> rows;
    for (const QList<QVariant> &args : spy)
        rows << args.at(0).value<QModelIndex>().row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

} // namespace

class TestShellExtensions : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<WorkspaceExtensions> m_ext;
    QStringList m_messages; ///< the toasts, "kind: text"
    QDateTime m_now;        ///< the app's clock

    QString disk(const QString &path) const { return readFile(m_dir + u'/' + path); }
    QString page(const QString &title) const { return kFolder + u'/' + title + u".md"_s; }

    QString lineWith(const QString &path, const QString &needle) const
    {
        for (const QString &line : disk(path).split(u'\n'))
            if (line.contains(needle))
                return line;
        return {};
    }

    int lineOf(const QString &path, const QString &needle) const
    {
        const QStringList lines = disk(path).split(u'\n');
        for (int i = 0; i < lines.size(); ++i)
            if (lines[i].contains(needle))
                return i;
        return -1;
    }

    /// The console's lines that hold `text`.
    int consoleLines(const QString &text) const
    {
        return int(m_ext->console()->text().split(u'\n').filter(text).size());
    }

    /// The sample notebook with the ten sample extensions and the counter, opened as the app opens
    /// a notebook: every extension page is there, none was approved on this device.
    void open()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
        m_messages.clear();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QHash<QString, QString> files{{page(u"Licznik zmian"_s), kCounter}};
        for (const QString &file : filesUnder(kSamples + u"/notebook"_s))
            files.insert(file, readFile(kSamples + u"/notebook/"_s + file));
        for (const QString &file : filesUnder(kSamples + u"/extensions"_s))
            files.insert(kFolder + u'/' + file, readFile(kSamples + u"/extensions/"_s + file));
        QVERIFY(files.size() > 20);
        // Link check's scan stops after a second of os.clock(), a wall clock as well.
        QString &linkCheck = files[page(u"Link check"_s)];
        QVERIFY(linkCheck.contains(u"local BUDGET_SECONDS = 1\n"_s));
        linkCheck.replace(u"local BUDGET_SECONDS = 1\n"_s,
                          u"local BUDGET_SECONDS = %1\n"_s.arg(brulion::test::kRoomyMs / 1000));
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
        m_now = QDateTime(QDate(2026, 11, 14), QTime(10, 30));
        m_ext = std::make_unique<WorkspaceExtensions>([this] { return m_now; });
        // The time limit is a wall clock that a stalled runner runs out in a call of a millisecond:
        // roomy here, as none of these tests is about it.
        m_ext->setLimits(extensions::Limits{brulion::test::kRoomyMs, extensions::Limits{}.memoryBytes});
        connect(m_ext.get(), &WorkspaceExtensions::message, this,
                [this](const QString &text, const QString &kind) { m_messages << kind + u": "_s + text; });
        // Workspace::onVaultChanged: the index first, then the extensions are told — inside the
        // vault's write.
        connect(m_vault.get(), &Vault::changed, this, [this](const QList<VaultChange> &changes) {
            m_ext->notesChanging(changes); // (before the index follows: where they come from)
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
        m_ext->indexReady();
    }

    void approveAll()
    {
        approveShared();
        for (const QString &file : filesUnder(m_dir + u'/' + kFolder))
            m_ext->approve(kFolder + u'/' + file);
    }

    /// The code the samples share (b.require("Wspólne funkcje")): approved first, as their user
    /// would, else they wait with it.
    void approveShared() { m_ext->approve(page(u"Wspólne funkcje"_s)); }

    /// A sample approved with the code it shares.
    void approveSample(const QString &title)
    {
        approveShared();
        m_ext->approve(page(title));
    }

    /// Workspace::writeIfUnchanged: this device's editor saves a page over the text it has
    /// (`base`; by default what is in the file, as for an editor that follows it).
    bool editorSaves(const QString &path, const QString &content, const std::optional<QString> &base = std::nullopt)
    {
        const QString expected = base ? *base : disk(path);
        m_ext->savedByEditor(path, content, expected);
        return m_vault->writeTextIfUnchanged(path, content, expected, ChangeSource::User) == WriteOutcome::Written;
    }

    /// Another program or a sync changes a page.
    bool syncBrings(const QString &path, const QString &content)
    {
        return m_vault->writeText(path, content, ChangeSource::External);
    }

    /// The page "Węzły" (kNodes) written as this device's user writes and approved: running.
    void addNodesExtension()
    {
        QVERIFY(m_vault->writeText(page(u"Węzły"_s), kNodes, ChangeSource::User));
        QTRY_VERIFY(!m_ext->pageState(page(u"Węzły"_s)).isEmpty()); // (read when the burst is over)
        m_ext->approve(page(u"Węzły"_s));
        QVERIFY2(m_ext->pageState(page(u"Węzły"_s)) == u"running",
                 qPrintable(m_ext->pageMessage(page(u"Węzły"_s))));
    }

private Q_SLOTS:
    void init() { open(); }

    void cleanup()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
    }

    void pagesWaitForApproval()
    {
        // Code that is in the notebook when it opens (a sync brought it, an import) runs nowhere
        // until it is approved on this device.
        const QStringList pages = filesUnder(m_dir + u'/' + kFolder);
        QCOMPARE(pages.size(), 12);
        QCOMPARE(m_ext->waitingApproval(), 12);
        for (const QString &file : pages)
            QCOMPARE(m_ext->pageState(kFolder + u'/' + file), u"approval"_s);
        QVERIFY(m_ext->commands().isEmpty());
        QVERIFY(m_ext->views().isEmpty());
        // One approved: it waits still, for the code it shares (b.require) — approved too, it runs;
        // the others still wait.
        m_ext->approve(page(u"Licznik słów"_s));
        QCOMPARE(m_ext->pageState(page(u"Licznik słów"_s)), u"error"_s);
        QVERIFY2(m_ext->pageMessage(page(u"Licznik słów"_s)).contains(u"“Wspólne funkcje” waits for approval"_s),
                 qPrintable(m_ext->pageMessage(page(u"Licznik słów"_s))));
        approveShared();
        QCOMPARE(m_ext->pageState(page(u"Licznik słów"_s)), u"running"_s);
        QCOMPARE(m_ext->commands().size(), 3);
        QCOMPARE(m_ext->waitingApproval(), 10);
        // All of them: every sample loads in the app's engine, in a Polish interface.
        approveAll();
        for (const QString &file : pages)
            QVERIFY2(m_ext->pageState(kFolder + u'/' + file) == u"running",
                     qPrintable(file + u": "_s + m_ext->pageMessage(kFolder + u'/' + file)));
        QCOMPARE(m_ext->waitingApproval(), 0);
        QCOMPARE(m_ext->commands().size(), 16);
        QCOMPARE(m_ext->views().size(), 8);
        QCOMPARE(m_ext->title(u"licznik-slow"_s), u"Licznik słów"_s);
        QCOMPARE(m_ext->viewTitle(u"wydatki"_s, u"miesiac"_s), u"Wydatki"_s);
        // An English interface loads them again: the bilingual one speaks English.
        m_ext->setLanguage(u"en"_s);
        QCOMPARE(m_ext->viewTitle(u"wydatki"_s, u"miesiac"_s), u"Expenses"_s);
        QCOMPARE(m_ext->commands().size(), 16);
    }

    void commandsAndTheirQuestions()
    {
        approveAll();
        // A command's text is a message.
        m_ext->runCommand(u"licznik-slow"_s, u"policz"_s, u"Lalka.md"_s);
        QCOMPARE(m_messages, QStringList{u"info: „Lalka”: 19 słów, 133 znaki, ok. 1 min czytania"_s});
        // A command on the block under the caret asks for a line of text.
        m_messages.clear();
        m_ext->runCommand(u"przeglad-zadan"_s, u"termin"_s, u"Zakupy.md"_s, lineOf(u"Zakupy.md"_s, u"mleko"_s));
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        QVERIFY(m_ext->isWaiting());
        m_ext->answerText(u"jutro"_s);
        QVERIFY(!m_ext->isWaiting());
        QCOMPARE(m_messages, QStringList{u"info: Termin: 2026-11-15"_s});
        QCOMPARE(lineWith(u"Zakupy.md"_s, u"mleko"_s), u"- [ ] mleko 📅 2026-11-15"_s);
        // A form, then a question that is cancelled: the command goes on without the note.
        m_messages.clear();
        m_ext->runCommand(u"nastroj"_s, u"zapisz"_s);
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        QCOMPARE(m_ext->questionTitle(), u"Jak się dziś czujesz?"_s);
        QCOMPARE(m_ext->questionFields()->rowCount(), 2);
        m_ext->questionFields()->setNumber(0, 5);
        m_ext->answerForm();
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        m_ext->cancelQuestion();
        QCOMPARE(m_messages, QStringList{u"info: Zapisano: nastrój 5/5, energia 3/5"_s});
        QVERIFY(disk(kJournal).endsWith(u"- Samopoczucie #nastrój\n  nastroj:: 5\n  energia:: 3\n"_s));
        // A form, a choice among the categories, a text.
        m_messages.clear();
        m_ext->runCommand(u"wydatki"_s, u"dodaj"_s);
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        m_ext->questionFields()->setNumber(0, 12.5);
        m_ext->answerForm();
        QCOMPARE(m_ext->questionKind(), u"choose"_s);
        QCOMPARE(m_ext->questionOptions().first(), u"jedzenie"_s);
        m_ext->answerChoice(u"jedzenie"_s);
        m_ext->answerText(u"kebab"_s);
        QCOMPARE(m_messages, QStringList{u"info: Dopisano: 12,5 zł, jedzenie"_s});
        QVERIFY(disk(kJournal).endsWith(u"- 12,5 zł jedzenie: kebab #wydatek\n"_s));
        // A timer of its own (not the gym's rest: that stays as it is).
        m_ext->runCommand(u"pomodoro"_s, u"break"_s);
        QCOMPARE(m_ext->timers().size(), 1);
        const QVariantMap timer = m_ext->timers().first().toMap();
        QCOMPARE(timer.value(u"extension"_s).toString(), u"pomodoro"_s);
        QCOMPARE(timer.value(u"label"_s).toString(), u"Break"_s);
        QVERIFY(timer.value(u"remaining"_s).toInt() > 290 && timer.value(u"remaining"_s).toInt() <= 300);
        QCOMPARE(m_ext->restRemaining(), 0);
        m_ext->stopTimer(u"pomodoro"_s);
        QVERIFY(m_ext->timers().isEmpty());
        // A model without a key (nothing leaves the device): the command says so.
        m_messages.clear();
        m_ext->runCommand(u"action-items"_s, u"extract"_s, u"Podróże/Peru.md"_s);
        QTRY_VERIFY2(m_messages.join(u'|').contains(u"AI is not available: "_s), qPrintable(m_messages.join(u'|')));
        QCOMPARE(m_ext->aiPending(), 0);
    }

    void aModelOnTheDeviceAndPictures()
    {
        // b.ai with the provider "local": the request goes to the app's model on the device, a
        // picture it names is loaded by the app first.
        struct Device : ai::LocalBackend {
            QList<ai::Request> requests;
            QList<ai::Settings> settings;
            void send(const ai::Settings &s, const ai::Request &request,
                      std::function<void(const ai::Reply &)> done, ai::PartialCallback,
                      std::shared_ptr<std::atomic<bool>>) override
            {
                settings << s;
                requests << request;
                ai::Reply reply;
                reply.ok = true;
                reply.text = u"Na zdjęciu jest paragon."_s;
                reply.model = u"lfm3-vl-2b"_s;
                QTimer::singleShot(0, [done, reply] { done(reply); });
            }
        } device;
        QStringList asked;
        m_ext->setAiPictures([&](const QString &name, QString *why) -> std::optional<ai::Image> {
            asked << name;
            if (name == u"paragon.jpg")
                return ai::Image{"JPEG of the receipt", u"image/jpeg"_s};
            *why = u"no such picture in the notebook"_s;
            return std::nullopt;
        });
        ai::Settings local;
        local.provider = u"local"_s;
        m_ext->setAi(local);
        m_ext->setLocalAi(&device);

        m_ext->evaluate(u"return b.ai({ prompt = \"Co to?\", images = { \"paragon.jpg\" } })"_s);
        QTRY_COMPARE(consoleLines(u"Na zdjęciu jest paragon."_s), 1);
        QCOMPARE(m_ext->aiPending(), 0);
        QCOMPARE(asked, QStringList{u"paragon.jpg"_s});
        QCOMPARE(device.requests.size(), 1);
        QCOMPARE(device.settings[0].provider, u"local"_s);
        QCOMPARE(device.requests[0].messages.size(), 1);
        QCOMPARE(device.requests[0].messages[0].content, u"Co to?"_s);
        QCOMPARE(device.requests[0].messages[0].images, (QList<ai::Image>{{"JPEG of the receipt", u"image/jpeg"_s}}));
        // A picture the notebook does not have: the script gets the error, nothing is asked.
        m_ext->evaluate(u"return b.ai({ prompt = \"A to?\", images = { \"brak.png\" } })"_s);
        QTRY_COMPARE(consoleLines(u"the picture \"brak.png\": no such picture in the notebook"_s), 1);
        QCOMPARE(device.requests.size(), 1);
        // "Check" in the settings asks the same model.
        m_messages.clear();
        m_ext->testAi();
        QTRY_COMPARE(m_messages.size(), 1);
        QVERIFY2(m_messages[0].startsWith(u"success: "_s) && m_messages[0].contains(u"lfm3-vl-2b"_s)
                         && m_messages[0].contains(u"Na zdjęciu jest paragon."_s),
                 qPrintable(m_messages[0]));
        QCOMPARE(device.requests.size(), 2);
        QCOMPARE(device.requests[1].effort, u"low"_s); // no thinking over "Say OK."
        // Without a model runner in the app (a build without one): the request is refused.
        m_ext->setLocalAi(nullptr);
        m_ext->evaluate(u"return b.ai(\"x\")"_s);
        QTRY_COMPARE(consoleLines(u"runs no model on the device"_s), 1);
    }

    void viewsAndTheirActions()
    {
        approveAll();
        QString error;
        QVariantMap tree = m_ext->render(u"przeglad-zadan"_s, u"przeglad"_s, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(tree.value(u"type"_s).toString(), u"column"_s);
        QCOMPARE(textsOf(tree, u"heading"_s),
                 (QStringList{u"Zaległe (2)"_s, u"Na dziś (1)"_s, u"Najbliższe dni: 7 (2)"_s}));
        QList<QVariantMap> checks;
        collect(tree, u"check"_s, &checks);
        QCOMPARE(checks.size(), 5);
        // A tap: the action writes the note, a text it returns is a message.
        QVERIFY(m_ext->act(u"przeglad-zadan"_s, u"przeglad"_s, u"zrobione"_s, checks[0].value(u"args"_s).toMap()));
        QCOMPARE(lineWith(u"Dom.md"_s, u"czynsz"_s), u"- [x] Zapłacić czynsz 📅 2026-11-10 ⏫ ✅ 2026-11-14"_s);
        // The same control tapped again (the view not drawn in between): the line changed, nothing
        // is written, and the app says so.
        m_messages.clear();
        QVERIFY(!m_ext->act(u"przeglad-zadan"_s, u"przeglad"_s, u"zrobione"_s, checks[0].value(u"args"_s).toMap()));
        QCOMPARE(m_messages.size(), 1);
        QVERIFY2(m_messages[0].startsWith(u"info: "_s) && !m_messages[0].contains(u"stale"_s),
                 qPrintable(m_messages[0]));
        tree = m_ext->render(u"przeglad-zadan"_s, u"przeglad"_s, {}, &error);
        QCOMPARE(textsOf(tree, u"heading"_s).first(), u"Zaległe (1)"_s);
        // A view with params, as a b.ui.open node gives them.
        tree = m_ext->render(u"wydatki"_s, u"miesiac"_s, {{u"month"_s, u"2026-10"_s}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(textsOf(tree, u"heading"_s), QStringList{u"Wydatki: październik 2026"_s});
        QCOMPARE(textsOf(tree, u"text"_s), QStringList{u"Razem: 155,5 zł"_s});
    }

    void ownWritesAreNoChange()
    {
        // "change" hooks hear what changed in the notebook — but never what an extension wrote
        // itself (no echoes, no loops), and always what the user changed, also in a page an
        // extension wrote before.
        m_ext->approve(page(u"Licznik zmian"_s));
        QCOMPARE(m_ext->pageState(page(u"Licznik zmian"_s)), u"running"_s);
        // The user edits a page: one event, with that page. The hook's own write of its page is
        // none (it would be the second event).
        QVERIFY(editorSaves(u"Dom.md"_s, disk(u"Dom.md"_s) + u"- [ ] Podlać kwiaty\n"_s));
        QTRY_COMPARE(disk(u"Licznik.md"_s), u"zmiany: 1 (Dom)\n"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), u"zmiany: 1 (Dom)\n"_s);
        // The user edits the page the extension wrote: an event like any other.
        QVERIFY(editorSaves(u"Licznik.md"_s, u"ręcznie\n"_s));
        QTRY_COMPARE(disk(u"Licznik.md"_s), u"zmiany: 2 (Licznik)\n"_s);
        // A command of the extension writes a page and today's note: no event.
        m_ext->runCommand(u"licznik-zmian"_s, u"pisz"_s);
        QCOMPARE(m_messages, QStringList{u"info: napisane"_s});
        QCOMPARE(disk(u"Z polecenia.md"_s), u"pisze polecenie\n"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), u"zmiany: 2 (Licznik)\n"_s);
        // The user edits what the command wrote, a sync changes another page: one event each
        // burst, with the pages of the burst.
        QVERIFY(editorSaves(u"Z polecenia.md"_s, u"ręcznie\n"_s));
        QVERIFY(syncBrings(u"Zakupy.md"_s, disk(u"Zakupy.md"_s) + u"- [ ] jajka\n"_s));
        QTRY_COMPARE(disk(u"Licznik.md"_s), u"zmiany: 3 (Z polecenia, Zakupy)\n"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), u"zmiany: 3 (Z polecenia, Zakupy)\n"_s);
        QCOMPARE(consoleLines(u"error"_s), 0);
    }

    void aPageKeptWrittenByAHook()
    {
        // The project index: its "change" hook writes the page "Projekty" when a project changes —
        // once, with nothing to do for its own write —, its "save" hook notes the save.
        approveSample(u"Indeks projektów"_s);
        QString dom = disk(u"Dom.md"_s);
        dom.replace(u"- [ ] Zapłacić czynsz"_s, u"- [x] Zapłacić czynsz"_s);
        QVERIFY(editorSaves(u"Dom.md"_s, dom));
        QTRY_VERIFY(
                disk(u"Projekty.md"_s).contains(u"- [[Dom]] — otwarte zadania: 2, najbliższy termin: 2026-11-14\n"_s));
        QTRY_COMPARE(consoleLines(u"Zapisano projekt: Dom (Dom.md)"_s), 1);
        QTest::qWait(1500);
        QCOMPARE(consoleLines(u"Indeks projektów przepisany"_s), 1);
        // Somebody edits the written page: the next event puts it back.
        const QString written = disk(u"Projekty.md"_s);
        QVERIFY(editorSaves(u"Projekty.md"_s, written + u"- dopisek\n"_s));
        QTRY_COMPARE(disk(u"Projekty.md"_s), written);
        QTRY_COMPARE(consoleLines(u"Indeks projektów przepisany"_s), 2);
    }

    void aSaveOverATextThatArrivedWaits()
    {
        // Another program changes an approved extension a moment before the editor saves its own
        // change of the page: the vault finds the other text, the editor merges it in and saves
        // the result at once (PageController::save). The code that arrived is in that save and
        // nobody here has read it: the page waits for approval, as if it had arrived alone.
        const QString path = page(u"Link check"_s);
        m_ext->approve(path);
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        const QString approved = disk(path);
        QString typed = approved; // what somebody is typing here: the page's text, not its code
        typed.replace(u"It only reads: no permissions."_s, u"Tylko czyta: żadnych uprawnień."_s);
        QString arrived = approved; // what arrives: other code
        arrived.replace(u"b.ui.heading(`Broken links: "_s, u"b.ui.heading(`Stolen: "_s);
        QString merged = arrived;
        merged.replace(u"It only reads: no permissions."_s, u"Tylko czyta: żadnych uprawnień."_s);
        QVERIFY(typed != approved && arrived != approved && merged != arrived);
        // The file changes behind the vault's back (Syncthing, an agent's command line) …
        QFile file(m_dir + u'/' + path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(arrived.toUtf8());
        file.close();
        // … the editor's save finds it, and the merged text is saved over what arrived, before
        // any event is handled.
        QVERIFY(!editorSaves(path, typed, approved));
        QVERIFY(editorSaves(path, merged, arrived));
        QCOMPARE(disk(path), merged);
        QTRY_COMPARE(m_ext->pageState(path), u"approval"_s);
        QString error;
        QVERIFY(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error).isEmpty());
        // Read and approved, it runs.
        m_ext->approve(path);
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Stolen: 4"_s});
    }

    void trustFollowsTheTextEditedHere()
    {
        const QString path = page(u"Link check"_s);
        m_ext->approve(path);
        QString error;
        // The page's text changes here, its code does not: nothing to approve, and the code edited
        // next is edited over that text.
        QString text = disk(path);
        text.replace(u"It only reads: no permissions."_s, u"Tylko czyta: żadnych uprawnień."_s);
        QVERIFY(editorSaves(path, text));
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // (Saved again before the first save was looked at: one edit after the other, both here.)
        QString code = text;
        code.replace(u"b.ui.heading(`Broken links: "_s, u"b.ui.heading(`Zerwane: "_s);
        QVERIFY(editorSaves(path, code));
        QTRY_COMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                     QStringList{u"Zerwane: 4"_s});
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // The same in two steps, each looked at before the next: text, then code.
        text = code;
        text.replace(u"Tylko czyta: żadnych uprawnień."_s, u"Tylko czyta: nie ma uprawnień."_s);
        QVERIFY(editorSaves(path, text));
        QTest::qWait(20);
        code = text;
        code.replace(u"b.ui.heading(`Zerwane: "_s, u"b.ui.heading(`Urwane: "_s);
        QVERIFY(editorSaves(path, code));
        QTRY_COMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                     QStringList{u"Urwane: 4"_s});
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        code.replace(u"b.ui.heading(`Urwane: "_s, u"b.ui.heading(`Zerwane: "_s);
        text = code;
        text.replace(u"Tylko czyta: nie ma uprawnień."_s, u"Tylko czyta: żadnych uprawnień."_s);
        QVERIFY(editorSaves(path, text));
        code = text;
        QTRY_COMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                     QStringList{u"Zerwane: 4"_s});
        // Text that arrives with the approved code in it changes no trust either: the code edited
        // over it here stays approved.
        QString synced = code;
        synced.replace(u"Tylko czyta: żadnych uprawnień."_s, u"Czyta."_s);
        QVERIFY(syncBrings(path, synced));
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // (Saved before the text that arrived was looked at, as a save that finds it does.)
        QString again = synced;
        again.replace(u"b.ui.heading(`Zerwane: "_s, u"b.ui.heading(`Brak stron: "_s);
        QVERIFY(editorSaves(path, again));
        QTRY_COMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                     QStringList{u"Brak stron: 4"_s});
        // A save that does not say over which text it was made (Workspace::write: a task ticked
        // in the agenda) changes no code; when it does, it waits.
        QString blind = again;
        blind.replace(u"b.ui.heading(`Brak stron: "_s, u"b.ui.heading(`Na ślepo: "_s);
        m_ext->savedByEditor(path, blind);
        QVERIFY(m_vault->writeText(path, blind, ChangeSource::User));
        QTRY_COMPARE(m_ext->pageState(path), u"approval"_s);
    }

    void codeAnExtensionWritesWaits()
    {
        // "Quick entries" writes an extension page. It is code that did not come from this
        // device's editor: it waits for approval like code from a sync, then runs.
        approveSample(u"Quick entries"_s);
        m_ext->runCommand(u"quick-entries"_s, u"new"_s);
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        m_ext->answerText(u"Kawa"_s);
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        m_ext->answerText(u"☕ {time} Kawa #kawa"_s);
        QVERIFY2(m_messages.value(0).startsWith(u"info: Written: \"Quick entry Kawa\""_s),
                 qPrintable(m_messages.join(u'|')));
        const QString made = page(u"Quick entry Kawa"_s);
        QVERIFY(disk(made).contains(u"local LINE = \"☕ {time} Kawa #kawa\"\n"_s));
        QTRY_COMPARE(m_ext->pageState(made), u"approval"_s);
        QVERIFY(m_ext->commands().size() == 1); // the new page's command is not there
        m_ext->approve(made);
        QCOMPARE(m_ext->pageState(made), u"running"_s);
        m_messages.clear();
        m_ext->runCommand(u"quick-entry-kawa"_s, u"add"_s);
        QVERIFY2(m_messages.value(0).startsWith(u"info: Added: ☕ "_s), qPrintable(m_messages.join(u'|')));
        static const QRegularExpression line(u"^- ☕ \\d\\d:\\d\\d Kawa #kawa$"_s);
        QVERIFY2(line.match(disk(kJournal).trimmed().section(u'\n', -1)).hasMatch(), qPrintable(disk(kJournal)));
    }

    void editingAnApprovedPage()
    {
        const QString path = page(u"Link check"_s);
        m_ext->approve(path);
        QString error;
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Broken links: 4"_s});
        // This device's editor changes the code: it stays approved and runs at once.
        QString code = disk(path);
        QVERIFY(code.contains(u"b.ui.heading(`Broken links: "_s));
        code.replace(u"b.ui.heading(`Broken links: "_s, u"b.ui.heading(`Missing pages: "_s);
        QVERIFY(editorSaves(path, code));
        QTRY_COMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                     QStringList{u"Missing pages: 4"_s});
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // A save that does not load: the page says where, and the version before it keeps running.
        QString broken = code;
        broken.replace(u"local MAX_SHOWN = 50"_s, u"local MAX_SHOWN = = 50"_s);
        QVERIFY(broken != code);
        QVERIFY(editorSaves(path, broken));
        QTRY_COMPARE(m_ext->pageState(path), u"error"_s);
        QCOMPARE(m_ext->pageErrorLine(path), int(broken.left(broken.indexOf(u"local MAX_SHOWN"_s)).count(u'\n')));
        QVERIFY2(m_ext->pageMessage(path).startsWith(u"Link check:"_s), qPrintable(m_ext->pageMessage(path)));
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Missing pages: 4"_s});
        // The same change arriving from elsewhere (a sync): it waits, and nothing of the page runs.
        code.replace(u"Missing pages: "_s, u"Gone: "_s);
        QVERIFY(syncBrings(path, code));
        QTRY_COMPARE(m_ext->pageState(path), u"approval"_s);
        QVERIFY(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error).isEmpty());
        m_ext->approve(path);
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Gone: 4"_s});
    }

    void typedNumbers_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("unit");
        QTest::addColumn<double>("value"); // NaN: not a number to write

        const double no = std::numeric_limits<double>::quiet_NaN();
        QTest::newRow("comma") << u"82,5"_s << u"kg"_s << 82.5;
        QTest::newRow("point") << u"82.5"_s << u"kg"_s << 82.5;
        QTest::newRow("whole") << u"100"_s << u"kg"_s << 100.0;
        QTest::newRow("zero") << u"0"_s << QString() << 0.0;
        QTest::newRow("spaces around") << u"  12\t"_s << QString() << 12.0;
        QTest::newRow("thousands space") << u"1 000"_s << QString() << 1000.0;
        QTest::newRow("no whole part") << u",5"_s << u"kg"_s << 0.5;
        QTest::newRow("separator last") << u"100."_s << u"kg"_s << 100.0;
        QTest::newRow("with its unit") << u"82,5 kg"_s << u"kg"_s << 82.5;
        QTest::newRow("unit, no space, capitals") << u"82,5KG"_s << u"kg"_s << 82.5;
        QTest::newRow("empty") << QString() << u"kg"_s << no;
        QTest::newRow("only spaces") << u"   "_s << QString() << no;
        QTest::newRow("the unit alone") << u"kg"_s << u"kg"_s << no;
        QTest::newRow("another unit") << u"8 lb"_s << u"kg"_s << no;
        QTest::newRow("a unit where none is shown") << u"8 kg"_s << QString() << no;
        QTest::newRow("a word") << u"dużo"_s << QString() << no;
        QTest::newRow("minus") << u"-5"_s << u"kg"_s << no;
        QTest::newRow("plus") << u"+5"_s << u"kg"_s << no;
        QTest::newRow("exponent") << u"1e3"_s << QString() << no;
        QTest::newRow("inf") << u"inf"_s << QString() << no;
        QTest::newRow("nan") << u"nan"_s << QString() << no;
        QTest::newRow("two separators") << u"1,2.5"_s << QString() << no;
        QTest::newRow("a separator alone") << u","_s << QString() << no;
        QTest::newRow("fullwidth digit") << u"５"_s << QString() << no;
        QTest::newRow("Arabic-Indic digit") << u"٣"_s << QString() << no;
        QTest::newRow("too large") << QString(400, u'9') << QString() << no;
    }

    void typedNumbers()
    {
        // What a stepper's field (and a form's) takes as a number.
        QFETCH(QString, text);
        QFETCH(QString, unit);
        QFETCH(double, value);
        const std::optional<double> parsed = parseExtensionNumber(text, unit);
        QCOMPARE(parsed.has_value(), !std::isnan(value));
        if (parsed)
            QCOMPARE(*parsed, value);
    }

    void stepsOntoTheGrid_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<double>("step");
        QTest::addColumn<int>("direction");
        QTest::addColumn<double>("expected");

        QTest::newRow("on the grid, up") << 30.0 << 2.5 << 1 << 32.5;
        QTest::newRow("on the grid, down") << 30.0 << 2.5 << -1 << 27.5;
        QTest::newRow("typed between, up") << 31.0 << 2.5 << 1 << 32.5;
        QTest::newRow("typed between, down: the grid value below") << 31.0 << 2.5 << -1 << 30.0;
        QTest::newRow("just under a grid value, down") << 32.4 << 2.5 << -1 << 30.0;
        QTest::newRow("lb") << 102.5 << 5.0 << -1 << 100.0;
        QTest::newRow("never below 0") << 0.0 << 2.5 << -1 << 0.0;
        QTest::newRow("from a fraction to 0") << 1.0 << 2.5 << -1 << 0.0;
        QTest::newRow("binary fractions on the grid") << 0.3 << 0.1 << 1 << 0.4;
        QTest::newRow("binary fractions, down") << 0.3 << 0.1 << -1 << 0.2;
        QTest::newRow("reps") << 8.0 << 1.0 << 1 << 9.0;
        QTest::newRow("step 0 is 1") << 7.0 << 0.0 << 1 << 8.0;
        QTest::newRow("negative step is 1") << 7.0 << -2.0 << -1 << 6.0;
        QTest::newRow("NaN value is 0") << std::numeric_limits<double>::quiet_NaN() << 1.0 << 1 << 1.0;
    }

    void stepsOntoTheGrid()
    {
        QFETCH(double, value);
        QFETCH(double, step);
        QFETCH(int, direction);
        QFETCH(double, expected);
        const double stepped = stepExtensionNumber(value, step, direction);
        QVERIFY2(std::abs(stepped - expected) < 1e-9, qPrintable(QString::number(stepped, 'g', 17)));
    }

    void stepperNumbersTypedIn()
    {
        // The gym's "today" as the app draws it (ExtensionViewModel): a weight and reps typed into
        // its steppers are written by their actions; the view is drawn again at once and in place —
        // the same row models, no reset, a dataChanged for what changed only —, so the next tap
        // (a field is left for "+ set") acts on the line as it is now.
        m_ext->setEnabled({u"silownia"_s});
        QVERIFY(m_vault->writeText(kJournal,
                                   disk(kJournal) + u"- Trening A #trening\n  - [ ] [[Przysiad]] 100 kg × 5\n"
                                                    "  - [ ] [[Podciąganie]] MC × 8\n"_s,
                                   ChangeSource::User));
        const auto squat = [this] { return lineWith(kJournal, u"[[Przysiad]]"_s); };
        ExtensionViewModel model;
        model.setExtensions(m_ext.get());
        model.setExtension(u"silownia"_s);
        model.setView(u"today"_s);
        model.refresh();
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        // Every exercise is a card of the same three lines: the exercise, the last set's steppers
        // (a bodyweight line: reps only), the set buttons.
        const QString row = u"row"_s;
        QCOMPARE(lineKinds(model), (QStringList{u"heading"_s, row, row, row, row, row, row}));
        QStringList parts;
        for (int i = 0; i < model.rowCount(); ++i)
            parts << model.data(model.index(i), ExtensionViewModel::CardRole).toString();
        QCOMPARE(parts, (QStringList{QString(), u"first"_s, u"middle"_s, u"last"_s, u"first"_s, u"middle"_s, u"last"_s}));
        const int exercise = 1;
        const int steppers = 2;
        const int buttons = 3;
        ExtensionControlsModel *exerciseControls = controlsOf(model, exercise);
        ExtensionControlsModel *controls = controlsOf(model, steppers);
        ExtensionControlsModel *buttonControls = controlsOf(model, buttons);
        QVERIFY(exerciseControls && controls && buttonControls);
        QCOMPARE(controlKinds(exerciseControls), (QStringList{u"check"_s, u"link"_s, u"text"_s}));
        const QStringList kinds{u"stepper"_s, u"stepper"_s};
        QCOMPARE(controlKinds(controls), kinds);
        QCOMPARE(controlKinds(buttonControls), QStringList{u"button"_s});
        QCOMPARE(controlKinds(controlsOf(model, 5)), QStringList{u"stepper"_s}); // bodyweight: reps only
        const int weight = 0;
        const int reps = 1;
        const int addSet = 0;
        QCOMPARE(controlRole(controls, weight, ExtensionControlsModel::NumberRole), u"100"_s);
        QCOMPARE(controlRole(controls, weight, ExtensionControlsModel::DisplayRole), u"100 kg"_s);
        QCOMPARE(controlRole(controls, weight, ExtensionControlsModel::UnitRole), u"kg"_s);
        QCOMPARE(controlRole(controls, reps, ExtensionControlsModel::NumberRole), u"5"_s);

        QSignalSpy lineResets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy controlResets(controls, &QAbstractItemModel::modelReset);
        QSignalSpy controlChanges(controls, &QAbstractItemModel::dataChanged);
        QSignalSpy exerciseChanges(exerciseControls, &QAbstractItemModel::dataChanged);
        // A weight typed with a decimal comma: written, shown at once, in place.
        QVERIFY(model.enterValue(steppers, weight, u"102,5"_s));
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 102,5 kg × 5"_s);
        QCOMPARE(controlsOf(model, steppers), controls);
        QCOMPARE(controlRole(controls, weight, ExtensionControlsModel::NumberRole), u"102,5"_s);
        QCOMPARE(controlRole(exerciseControls, 2, ExtensionControlsModel::TextRole), u"102,5 kg × 5"_s);
        QCOMPARE(changedRows(exerciseChanges), QList<int>{2}); // the sets' text
        QCOMPARE(changedRows(controlChanges), QList<int>{weight}); // and the stepper
        QCOMPARE(lineResets.count(), 0);
        QCOMPARE(controlResets.count(), 0);
        // The next control right away (no event loop in between): it acts on the new line.
        model.activateControl(steppers, reps, 6);
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 102,5 kg × 6"_s);
        QVERIFY2(m_messages.isEmpty(), qPrintable(m_messages.join(u'|')));

        // What is no new number writes nothing: the value shown, other text, other controls.
        for (const QString &text : {u"102.50"_s, u"102,5 kg"_s, u" 102,5"_s, u""_s, u"abc"_s, u"-5"_s, u"1e3"_s, u"5 lb"_s})
            QVERIFY2(!model.enterValue(steppers, weight, text), qPrintable(text));
        QVERIFY(!model.enterValue(buttons, addSet, u"3"_s)); // a button
        QVERIFY(!model.enterValue(exercise, 2, u"3"_s)); // a text
        QVERIFY(!model.enterValue(steppers, 99, u"3"_s));
        QVERIFY(!model.enterValue(0, 0, u"3"_s)); // the heading
        QVERIFY(!model.enterValue(-1, weight, u"3"_s));
        QVERIFY(!model.enterValue(7, weight, u"3"_s));
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 102,5 kg × 6"_s);
        // Its unit typed along, a point; reps; the bodyweight line's reps.
        QVERIFY(model.enterValue(steppers, weight, u"105 kg"_s));
        QVERIFY(model.enterValue(steppers, reps, u"8"_s));
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 105 kg × 8"_s);
        QVERIFY(model.enterValue(5, 0, u"10"_s));
        QCOMPARE(lineWith(kJournal, u"[[Podciąganie]]"_s), u"  - [ ] [[Podciąganie]] MC × 10"_s);
        // A weight off the step's grid: − and + go to the grid's values next to it.
        QVERIFY(model.enterValue(steppers, weight, u"31"_s));
        QCOMPARE(model.stepped(steppers, weight, -1), 30.0);
        QCOMPARE(model.stepped(steppers, weight, 1), 32.5);
        QCOMPARE(lineResets.count(), 0);
        QCOMPARE(controlResets.count(), 0);

        // "+ set" brings "− set": controls of other kinds wait for the timer (the delegate whose
        // handler acted stays until then), then only the buttons' line is reset — the steppers,
        // a line of their own, keep their delegates (a field being typed into stays).
        QSignalSpy buttonResets(buttonControls, &QAbstractItemModel::modelReset);
        model.activateControl(buttons, addSet);
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 31 kg 2×8"_s);
        QCOMPARE(controlKinds(buttonControls), QStringList{u"button"_s});
        QTRY_COMPARE(controlKinds(buttonControls), (QStringList{u"button"_s, u"button"_s}));
        QCOMPARE(controlsOf(model, buttons), buttonControls);
        QCOMPARE(buttonResets.count(), 1);
        QCOMPARE(controlResets.count(), 0);
        QCOMPARE(lineResets.count(), 0);
        m_ext->stopRest();

        // While the lines change nothing is written: a field that loses the focus then (its
        // delegate going) would act on lines half changed.
        QList<bool> duringReset;
        connect(buttonControls, &QAbstractItemModel::modelAboutToBeReset, this,
                [&] { duringReset << model.enterValue(steppers, weight, u"50"_s); });
        connect(buttonControls, &QAbstractItemModel::modelReset, this,
                [&] { duringReset << model.enterValue(steppers, weight, u"50"_s); });
        model.activateControl(buttons, addSet + 1); // "− set"
        QTRY_COMPARE(controlKinds(buttonControls), QStringList{u"button"_s});
        QCOMPARE(duringReset, (QList<bool>{false, false}));
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 31 kg × 8"_s);
        disconnect(buttonControls, nullptr, this, nullptr);

        // A note changed elsewhere is drawn again in place too: nothing of the rows changed here.
        controlChanges.clear();
        const int revision = model.revision();
        QVERIFY(syncBrings(u"Zakupy.md"_s, disk(u"Zakupy.md"_s) + u"- [ ] jajka\n"_s));
        QTRY_VERIFY(model.revision() > revision);
        QCOMPARE(controlChanges.count(), 0);
        QCOMPARE(lineResets.count(), 0);
        // Another view starts afresh.
        model.setView(u"progress"_s);
        QTRY_COMPARE(lineResets.count(), 1);
        QVERIFY(!lineKinds(model).contains(u"row"_s));

        // The add-set form (b.form) takes the same numbers and steps onto the grid as well.
        m_ext->runCommand(u"silownia"_s, u"add-set"_s, kJournal, lineOf(kJournal, u"[[Przysiad]]"_s));
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        ExtensionFormModel *fields = m_ext->questionFields();
        const auto field = [fields](int row) { return fields->data(fields->index(row), ExtensionFormModel::ValueRole).toDouble(); };
        QCOMPARE(field(0), 31.0);
        fields->setNumberText(0, u"82,5 kg"_s);
        QCOMPARE(field(0), 82.5);
        fields->setNumberText(0, u"-5"_s);
        fields->setNumberText(0, u"1e3"_s);
        QCOMPARE(field(0), 82.5);
        fields->setNumberText(0, u"31"_s);
        fields->step(0, -1);
        QCOMPARE(field(0), 30.0);
        fields->setNumberText(1, u"4"_s);
        m_ext->answerForm();
        QCOMPARE(squat(), u"  - [ ] [[Przysiad]] 31 kg × 8; 30 kg × 4"_s);
        m_ext->stopRest();
    }

    void argsAndTableResults()
    {
        // A command the app runs with arguments gets them as ctx.args ({} without any); a result
        // { message, tone, button } is a message with that tone and a button, which runs the
        // command (with its args), opens a page, or — of a view's action — runs that action again.
        addNodesExtension();
        QList<int> buttons;
        connect(m_ext.get(), &WorkspaceExtensions::messageWithButton, this,
                [&](const QString &text, const QString &kind, const QString &label, int id) {
                    m_messages << kind + u": "_s + text + u" ["_s + label + u']';
                    buttons << id;
                });
        QStringList pages;
        connect(m_ext.get(), &WorkspaceExtensions::pageRequested, this, [&](const QString &p) { pages << p; });
        m_ext->runCommand(u"wezly"_s, u"echo"_s);
        m_ext->runCommand(u"wezly"_s, u"echo"_s, QString(), -1, {{u"word"_s, u"ćma"_s}, {u"inne"_s, 1}});
        QCOMPARE(m_messages, (QStringList{u"success: słowo: - (0) [Jeszcze]"_s, u"success: słowo: ćma (2) [Jeszcze]"_s}));
        QCOMPARE(buttons.size(), 2);
        // Its button: the command again with the button's args; a button works once.
        m_messages.clear();
        m_ext->pressButton(buttons[0]);
        m_ext->pressButton(buttons[0]);
        QCOMPARE(m_messages, QStringList{u"success: słowo: żółw (1) [Jeszcze]"_s});
        // A page button; a forgotten button does nothing.
        m_messages.clear();
        m_ext->runCommand(u"wezly"_s, u"strona"_s);
        QCOMPARE(m_messages, QStringList{u"info: Gotowe [Otwórz]"_s});
        m_ext->forgetButton(buttons.last());
        m_ext->pressButton(buttons.last());
        QVERIFY(pages.isEmpty());
        m_ext->runCommand(u"wezly"_s, u"strona"_s);
        m_ext->pressButton(buttons.last());
        QCOMPARE(pages, QStringList{u"Żółwie"_s});
        // No button: a plain message with the tone; no message: silent.
        m_messages.clear();
        m_ext->runCommand(u"wezly"_s, u"blad"_s);
        m_ext->runCommand(u"wezly"_s, u"cisza"_s);
        QCOMPARE(m_messages, QStringList{u"error: Źle"_s});

        // A view action's result: Undo runs the same view's action with the args before.
        m_messages.clear();
        buttons.clear();
        QVERIFY(m_ext->act(u"wezly"_s, u"widok"_s, u"ustaw"_s, {{u"day"_s, u"2026-11-02"_s}, {u"state"_s, u"done"_s}}));
        QCOMPARE(m_messages, QStringList{u"success: 2026-11-02: done [Cofnij]"_s});
        m_ext->pressButton(buttons.last());
        QCOMPARE(m_messages.last(), u"success: 2026-11-02: open [Cofnij]"_s);
        QString error;
        QList<QVariantMap> months;
        collect(m_ext->render(u"wezly"_s, u"widok"_s, {}, &error), u"month"_s, &months);
        QCOMPARE(months.first().value(u"days"_s).toMap().value(u"2026-11-02"_s).toMap().value(u"state"_s).toString(),
                 u"open"_s);
        // Nothing changed (nil): no message.
        m_messages.clear();
        QVERIFY(m_ext->act(u"wezly"_s, u"widok"_s, u"ustaw"_s, {{u"day"_s, u"2026-11-02"_s}, {u"state"_s, u"open"_s}}));
        QVERIFY(m_messages.isEmpty());
    }

    void formTextFields()
    {
        // b.form's text fields: their text (the value given first), a placeholder; the answer is
        // the text typed; numbers as before.
        addNodesExtension();
        m_ext->runCommand(u"wezly"_s, u"formularz"_s);
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        ExtensionFormModel *fields = m_ext->questionFields();
        QCOMPARE(fields->rowCount(), 2);
        const auto role = [fields](int row, int role) { return fields->data(fields->index(row), role).toString(); };
        QCOMPARE(role(0, ExtensionFormModel::KindRole), u"number"_s);
        QCOMPARE(role(1, ExtensionFormModel::KindRole), u"text"_s);
        QCOMPARE(role(1, ExtensionFormModel::TextRole), u"Żółć"_s);
        QCOMPARE(role(1, ExtensionFormModel::PlaceholderRole), u"np. Czytanie"_s);
        QCOMPARE(role(0, ExtensionFormModel::PlaceholderRole), QString());
        QCOMPARE(fields->firstTextField(), 1);
        fields->setText(1, u"Gęślą jaźń"_s);
        fields->setNumber(0, 3);
        m_ext->answerForm();
        QCOMPARE(m_messages, QStringList{u"info: Gęślą jaźń/3"_s});
        // Cancelled: nil.
        m_messages.clear();
        m_ext->runCommand(u"wezly"_s, u"formularz"_s);
        m_ext->cancelQuestion();
        QCOMPARE(m_messages, QStringList{u"info: anulowano"_s});
    }

    void newNodesInTheViewModel()
    {
        // Every newer node as the app draws it: their roles, a card's lines with their parts of
        // it, a month's cells, menus and what their items do, a day's tap; drawn again in place.
        addNodesExtension();
        ExtensionViewModel model;
        model.setExtensions(m_ext.get());
        model.setExtension(u"wezly"_s);
        model.setView(u"widok"_s);
        model.refresh();
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        // (A month that is no month is left out.)
        QCOMPARE(lineKinds(model), (QStringList{u"text"_s, u"row"_s, u"link"_s, u"month"_s, u"text"_s, u"menu"_s}));
        const auto line = [&model](int row, int role) { return model.data(model.index(row), role); };
        QStringList parts;
        for (int i = 0; i < model.rowCount(); ++i)
            parts << line(i, ExtensionViewModel::CardRole).toString();
        QCOMPARE(parts, (QStringList{QString(), u"first"_s, u"middle"_s, u"last"_s, u"only"_s, QString()}));
        // A text with an icon and a tone.
        QCOMPARE(line(0, ExtensionViewModel::IconRole).toString(), u"flame"_s);
        QCOMPARE(line(0, ExtensionViewModel::ToneRole).toString(), u"success"_s);
        QCOMPARE(line(4, ExtensionViewModel::ToneRole).toString(), QString());
        // The row: a skipped check with a menu and a name, texts, a menu.
        ExtensionControlsModel *controls = controlsOf(model, 1);
        QCOMPARE(controlKinds(controls), (QStringList{u"check"_s, u"text"_s, u"text"_s, u"menu"_s}));
        QCOMPARE(controlRole(controls, 0, ExtensionControlsModel::StateRole), u"skipped"_s);
        QCOMPARE(controlRole(controls, 0, ExtensionControlsModel::LabelRole), u"Medytacja"_s);
        QVERIFY(controls->data(controls->index(0), ExtensionControlsModel::HasMenuRole).toBool());
        QVERIFY(!controls->data(controls->index(0), ExtensionControlsModel::CheckedRole).toBool());
        QCOMPARE(controlRole(controls, 2, ExtensionControlsModel::ToneRole), u"danger"_s);
        QCOMPARE(controlRole(controls, 3, ExtensionControlsModel::LabelRole), u"Więcej"_s);
        QVERIFY(controls->data(controls->index(3), ExtensionControlsModel::HasMenuRole).toBool());
        QVERIFY(!controls->data(controls->index(1), ExtensionControlsModel::HasMenuRole).toBool());
        // Menus: the items that can be chosen (no nil, none without anything to do), their icons.
        const QStringList labels{u"Zrobione"_s, u"Dziennik"_s, u"Definicja"_s};
        QCOMPARE(model.menuLabels(1, 0, QString()), labels);
        QCOMPARE(model.menuLabels(1, 3, QString()), labels);
        QCOMPARE(model.menuIcons(1, 3, QString()), (QStringList{u"check"_s, u"book-open"_s, u"file-text"_s}));
        QCOMPARE(model.menuLabels(5, -1, QString()), labels);
        QCOMPARE(model.menuLabels(3, -1, u"2026-11-02"_s), labels);
        QVERIFY(model.menuLabels(3, -1, u"2026-11-14"_s).isEmpty());
        QVERIFY(model.menuLabels(1, 1, QString()).isEmpty());
        QVERIFY(model.menuLabels(0, -1, QString()).isEmpty());
        QVERIFY(model.menuLabels(99, 0, QString()).isEmpty());
        QSignalSpy pageSpy(&model, &ExtensionViewModel::pageRequested);
        QSignalSpy journalSpy(&model, &ExtensionViewModel::journalRequested);
        model.activateMenuItem(1, 3, QString(), 1);
        QCOMPARE(journalSpy.count(), 1);
        QCOMPARE(journalSpy.first().at(0).toString(), u"2026-11-03"_s);
        model.activateMenuItem(5, -1, QString(), 2);
        QCOMPARE(pageSpy.count(), 1);
        QCOMPARE(pageSpy.first().at(0).toString(), u"Nawyki"_s);
        QCOMPARE(pageSpy.first().at(1).toInt(), 2); // the 1-based line 3, 0-based
        // The link at its line.
        QCOMPARE(line(2, ExtensionViewModel::TextRole).toString(), u"Definicja"_s);
        model.activate(2);
        QCOMPARE(pageSpy.count(), 2);
        QCOMPARE(pageSpy.last().at(1).toInt(), 2);

        // The month: a cell per day, the states it gave (else "rest"), which days act and have menus.
        QCOMPARE(line(3, ExtensionViewModel::MonthRole).toString(), u"2026-11"_s);
        QCOMPARE(line(3, ExtensionViewModel::TitleRole).toString(), u"listopad 2026"_s);
        const auto days = [&] { return line(3, ExtensionViewModel::DaysRole).toList(); };
        QCOMPARE(days().size(), 30);
        const auto day = [&](int d) { return days().at(d - 1).toMap(); };
        QCOMPARE(day(1).value(u"date"_s).toString(), u"2026-11-01"_s);
        QCOMPARE(day(1).value(u"day"_s).toInt(), 1);
        QCOMPARE(day(1).value(u"state"_s).toString(), u"rest"_s);
        QCOMPARE(day(2).value(u"state"_s).toString(), u"missed"_s);
        QVERIFY(day(2).value(u"tappable"_s).toBool() && day(2).value(u"menu"_s).toBool());
        QCOMPARE(day(14).value(u"state"_s).toString(), u"due"_s);
        QVERIFY(day(14).value(u"tappable"_s).toBool() && !day(14).value(u"menu"_s).toBool());
        QCOMPARE(day(20).value(u"state"_s).toString(), u"future"_s);
        QVERIFY(!day(20).value(u"tappable"_s).toBool());
        QCOMPARE(day(21).value(u"state"_s).toString(), u"rest"_s); // a state it does not know

        // A day tapped: its action; drawn again at once and in place (only that line's days).
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy controlResets(controls, &QAbstractItemModel::modelReset);
        QSignalSpy controlChanges(controls, &QAbstractItemModel::dataChanged);
        model.activateDay(3, u"2026-11-02"_s);
        QCOMPARE(day(2).value(u"state"_s).toString(), u"done"_s);
        QCOMPARE(changedRows(changes), QList<int>{3});
        QCOMPARE(changes.first().at(2).value<QList<int>>(), QList<int>{ExtensionViewModel::DaysRole});
        model.activateDay(3, u"2026-11-20"_s); // no action: nothing
        model.activateDay(2, u"2026-11-02"_s); // no month line
        QCOMPARE(changes.count(), 1);
        // A menu's action: the check's state changes in place, the row's model stays.
        model.activateMenuItem(1, 0, QString(), 0);
        QCOMPARE(controlsOf(model, 1), controls);
        QCOMPARE(controlRole(controls, 0, ExtensionControlsModel::StateRole), u"done"_s);
        QVERIFY(controls->data(controls->index(0), ExtensionControlsModel::CheckedRole).toBool());
        QCOMPARE(changedRows(controlChanges), QList<int>{0});
        QCOMPARE(resets.count(), 0);
        QCOMPARE(controlResets.count(), 0);
        // The check tapped: its action (done again: nothing changes, nothing said).
        m_messages.clear();
        model.activateControl(1, 0);
        QVERIFY(m_messages.isEmpty());

        // An empty state with its icon; another view's params start afresh.
        model.setParam(u"pusto"_s, u"tak"_s);
        QTRY_COMPARE(resets.count(), 1);
        QCOMPARE(lineKinds(model), QStringList{u"empty"_s});
        QCOMPARE(line(0, ExtensionViewModel::IconRole).toString(), u"flame"_s);
        QCOMPARE(line(0, ExtensionViewModel::TitleRole).toString(), u"Pusto"_s);
        QCOMPARE(line(0, ExtensionViewModel::CardRole).toString(), QString());
    }

    // ---- what the samples met as walls, and the extension's own corner ----------------------------

    /// An extension page `title` with `code` (its permissions `permissions`), written as this
    /// device's user writes and approved: running.
    void addExtension(const QString &title, const QString &code, const QString &permissions = u"write"_s)
    {
        const QString text =
                u"---\ntags: [extension]\npermissions: [%1]\n---\n```luau\n%2\n```\n"_s.arg(permissions, code);
        QVERIFY(m_vault->writeText(page(title), text, ChangeSource::User));
        QTRY_VERIFY(!m_ext->pageState(page(title)).isEmpty());
        m_ext->approve(page(title));
        QVERIFY2(m_ext->pageState(page(title)) == u"running", qPrintable(m_ext->pageMessage(page(title))));
    }

    void hooksHearOtherExtensions()
    {
        // A task ticked in the review's view reaches the page the project index keeps, and the
        // counter hears both writes — never its own.
        approveSample(u"Przegląd zadań"_s);
        approveSample(u"Indeks projektów"_s);
        m_ext->approve(page(u"Licznik zmian"_s));
        QString error;
        const QVariantMap tree = m_ext->render(u"przeglad-zadan"_s, u"przeglad"_s, {}, &error);
        QList<QVariantMap> checks;
        collect(tree, u"check"_s, &checks);
        QVERIFY(!checks.isEmpty());
        QVERIFY(m_ext->act(u"przeglad-zadan"_s, u"przeglad"_s, u"zrobione"_s, checks[0].value(u"args"_s).toMap()));
        QTRY_VERIFY2(disk(u"Projekty.md"_s).contains(u"- [[Dom]] — otwarte zadania: 2"_s),
                     qPrintable(disk(u"Projekty.md"_s)));
        QTRY_VERIFY2(disk(u"Licznik.md"_s).startsWith(u"zmiany: "_s), qPrintable(disk(u"Licznik.md"_s)));
        QTRY_VERIFY2(disk(u"Licznik.md"_s).contains(u"Projekty"_s), qPrintable(disk(u"Licznik.md"_s)));
        QTest::qWait(1500);
        // The counter's own page is no change for it: it settled.
        const QString settled = disk(u"Licznik.md"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), settled);
        QCOMPARE(consoleLines(u"error"_s), 0);
    }

    void lineWritesAreHeardAsTheExtensions()
    {
        // The flashcards' line functions and b.setProperties write as the extension that called
        // them: another extension's hooks hear them with its name, its own hooks never do.
        addExtension(u"Linie"_s,
                     u"b.on(\"change\", function(e)\n"
                     "    if table.find(e.pages, \"Zakupy\") then b.appendToPage(\"Echo linii\", \"- echo\") end\n"
                     "end)\n"
                     "b.command(\"pisz\", function()\n"
                     "    local lines = b.lines(\"Zakupy\")\n"
                     "    local ref = b.insertLine(lines[#lines].ref, \"- [ ] masło\")\n"
                     "    ref = b.replaceLine(ref, \"- [x] masło\")\n"
                     "    ref = b.setProperties(ref, { { \"sklep\", \"Żabka\" }, { \"ile\", 2 } })\n"
                     "    local extra = b.insertLine(ref, \"- do usunięcia\")\n"
                     "    b.removeLine(extra)\n"
                     "end)"_s);
        addExtension(u"Słuchacz linii"_s, u"b.on(\"change\", function(e)\n"
                                          "    if table.find(e.pages, \"Zakupy\") then\n"
                                          "        b.appendToPage(\"Słyszane linie\", `- change {table.concat(e.by, "
                                          "\",\")} {table.concat(e.sources, \",\")}`)\n"
                                          "    end\n"
                                          "end)"_s);
        QTest::qWait(1500); // (the pages written above are heard and done with first)
        const QString echoBefore = disk(u"Echo linii.md"_s);
        m_ext->runCommand(u"linie"_s, u"pisz"_s);
        QTRY_VERIFY2(disk(u"Zakupy.md"_s).contains(u"- [x] masło"_s), qPrintable(disk(u"Zakupy.md"_s)));
        QVERIFY2(disk(u"Zakupy.md"_s).contains(u"sklep:: Żabka"_s), qPrintable(disk(u"Zakupy.md"_s)));
        QVERIFY(!disk(u"Zakupy.md"_s).contains(u"do usunięcia"_s));
        QTRY_VERIFY2(disk(u"Słyszane linie.md"_s).contains(u"- change linie extension"_s),
                     qPrintable(disk(u"Słyszane linie.md"_s)));
        QTest::qWait(1500);
        // Its own writes are no change for its own hook.
        QCOMPARE(disk(u"Echo linii.md"_s), echoBefore);
        QCOMPARE(consoleLines(u"error"_s), 0);
    }

    void chainsOfHooksEnd()
    {
        // Two extensions that write whenever the other one wrote: some rounds, then the chain ends
        // (ChangeBatch::kMaxDepth) and the console says so.
        addExtension(
                u"Ping"_s,
                u"local n = 0\nb.on(\"change\", function(e)\n    n += 1\n    b.write(\"Ping wynik\", \"ping \" .. n .. \"\\n\")\nend)"_s);
        addExtension(
                u"Pong"_s,
                u"local n = 0\nb.on(\"change\", function(e)\n    n += 1\n    b.write(\"Pong wynik\", \"pong \" .. n .. \"\\n\")\nend)"_s);
        QVERIFY(editorSaves(u"Zakupy.md"_s, disk(u"Zakupy.md"_s) + u"- [ ] chleb\n"_s));
        QTRY_VERIFY_WITH_TIMEOUT(consoleLines(u"stopped after"_s) == 1, 20000);
        const QString ping = disk(u"Ping wynik.md"_s), pong = disk(u"Pong wynik.md"_s);
        QTest::qWait(2000);
        QCOMPARE(disk(u"Ping wynik.md"_s), ping);
        QCOMPARE(disk(u"Pong wynik.md"_s), pong);
        // Each wrote a few times, not forever.
        QVERIFY2(ping.mid(5).trimmed().toInt() <= extensions::ChangeBatch::kMaxDepth + 1, qPrintable(ping));
        QVERIFY2(pong.mid(5).trimmed().toInt() >= 2, qPrintable(pong));
    }

    void taskJournalOpenDayAndTimerEvents()
    {
        addExtension(
                u"Zdarzenia"_s,
                u"b.on(\"task\", function(e) b.appendToPage(\"Dziennik zdarzeń\", `- task {e.plain} "
                u"[{e.previous}]→[{e.task}] {e.source} {e.by}`) end)\n"
                "b.on(\"journal\", function(e) b.appendToPage(\"Dziennik zdarzeń\", `- journal {e.date} {e.source}`) "
                "end)\n"
                "b.on(\"day\", function(e) b.appendToPage(\"Dziennik zdarzeń\", `- day {e.previous}→{e.date}`) end)\n"
                "b.on(\"timer\", function(e) b.appendToPage(\"Dziennik zdarzeń\", `- timer {e.label} {e.seconds}`) "
                "end)\n"
                "b.on(\"open\", function(e) b.appendToPage(\"Dziennik zdarzeń\", `- open {e.date}`) end)\n"
                "b.command(\"herbata\", function() b.timer(1, { label = \"Herbata\", message = \"Herbata gotowa.\" }) end)"_s);
        // The editor ticks a task: one "task" event with its statuses.
        QString dom = disk(u"Dom.md"_s);
        dom.replace(u"- [ ] Wymienić żarówkę w łazience"_s, u"- [x] Wymienić żarówkę w łazience"_s);
        QVERIFY(editorSaves(u"Dom.md"_s, dom));
        QTRY_VERIFY2(disk(u"Dziennik zdarzeń.md"_s).contains(u"- task Wymienić żarówkę w łazience [ ]→[x] app"_s),
                     qPrintable(disk(u"Dziennik zdarzeń.md"_s)));
        // Another extension's tick is heard too, with its name.
        approveSample(u"Przegląd zadań"_s);
        QString error;
        const QVariantMap tree = m_ext->render(u"przeglad-zadan"_s, u"przeglad"_s, {}, &error);
        QList<QVariantMap> checks;
        collect(tree, u"check"_s, &checks);
        QVERIFY(m_ext->act(u"przeglad-zadan"_s, u"przeglad"_s, u"zrobione"_s, checks[0].value(u"args"_s).toMap()));
        QTRY_VERIFY2(disk(u"Dziennik zdarzeń.md"_s).contains(u"→[x] extension przeglad-zadan"_s),
                     qPrintable(disk(u"Dziennik zdarzeń.md"_s)));
        // A daily note made (here by a sync).
        QVERIFY(m_vault->writeText(u"journals/2026-11-15.md"_s, u"- jutro\n"_s, ChangeSource::Sync));
        QTRY_VERIFY(disk(u"Dziennik zdarzeń.md"_s).contains(u"- journal 2026-11-15 sync"_s));
        // A new day while the app runs.
        m_now = m_now.addDays(1);
        QTRY_VERIFY_WITH_TIMEOUT(disk(u"Dziennik zdarzeń.md"_s).contains(u"- day 2026-11-14→2026-11-15"_s), 20000);
        // Its own timer: its words in the notification and the message, then its "timer" hook.
        QStringList notified;
        m_ext->setNotifier([&](const QString &title, const QString &body) { notified << title + u" | "_s + body; });
        m_messages.clear();
        m_ext->runCommand(u"zdarzenia"_s, u"herbata"_s);
        QCOMPARE(m_ext->timers().size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(notified, QStringList{u"Herbata is over | Herbata gotowa."_s}, 10000);
        QVERIFY(m_messages.contains(u"info: Herbata gotowa."_s));
        QTRY_VERIFY(disk(u"Dziennik zdarzeń.md"_s).contains(u"- timer Herbata 1"_s));
        QVERIFY(m_ext->timers().isEmpty());
        QCOMPARE(m_ext->restRemaining(), 0); // (the gym's rest is another timer)
        m_ext->setNotifier({});
        // "open": once, when the notebook opens with the extension approved.
        QVERIFY(!disk(u"Dziennik zdarzeń.md"_s).contains(u"- open"_s));
        m_ext->detach();
        m_ext->attach(m_vault.get(), m_index.get());
        m_ext->indexReady();
        QTRY_COMPARE(disk(u"Dziennik zdarzeń.md"_s).count(u"- open 2026-11-15"_s), 1);
        QCOMPARE(consoleLines(u"error"_s), 0);
    }

    void settingsOfAnExtension()
    {
        addExtension(
                u"Waluta"_s,
                u"b.settings({\n"
                "    { key = \"waluta\", label = \"Waluta\", value = \"zł\" },\n"
                "    { key = \"limit\", label = \"Limit miesięczny\", value = 1000, step = 50, unit = \"zł\" },\n"
                "    { key = \"grosze\", label = \"Z groszami\", value = false },\n"
                "    { key = \"widok\", label = \"Widok\", options = { \"lista\", \"tabela\" } },\n"
                "})\n"
                "b.command(\"pokaz\", function() return `{b.setting(\"limit\")} {b.setting(\"waluta\")} {tostring(b.setting(\"grosze\"))} {b.setting(\"widok\")}` end)"_s);
        const QVariantList fields = m_ext->settingsOf(u"waluta"_s);
        QCOMPARE(fields.size(), 4);
        QCOMPARE(m_ext->settingsCount(u"waluta"_s), 4);
        QCOMPARE(fields[1].toMap().value(u"display"_s).toString(), u"1000"_s);
        QCOMPARE(fields[3].toMap().value(u"options"_s).toStringList(), (QStringList{u"lista"_s, u"tabela"_s}));
        // As the settings dialog sets them: a number typed with a comma and its unit, a switch, a choice.
        QVERIFY(m_ext->setSetting(u"waluta"_s, u"limit"_s, u"1 250,5 zł"_s)
                || m_ext->setSetting(u"waluta"_s, u"limit"_s, u"1250,5 zł"_s));
        QVERIFY(m_ext->setSetting(u"waluta"_s, u"grosze"_s, true));
        QVERIFY(m_ext->setSetting(u"waluta"_s, u"widok"_s, u"tabela"_s));
        QVERIFY(!m_ext->setSetting(u"waluta"_s, u"widok"_s, u"wykres"_s));
        QVERIFY(!m_ext->setSetting(u"waluta"_s, u"limit"_s, u"dużo"_s));
        QVERIFY(!m_ext->setSetting(u"waluta"_s, u"nie-ma"_s, 1));
        m_messages.clear();
        m_ext->runCommand(u"waluta"_s, u"pokaz"_s);
        QCOMPARE(m_messages, QStringList{u"info: 1250.5 zł true tabela"_s});
        // Kept on this device: a new start reads them.
        m_ext->detach();
        m_ext->attach(m_vault.get(), m_index.get());
        m_ext->indexReady();
        m_messages.clear();
        m_ext->runCommand(u"waluta"_s, u"pokaz"_s);
        QCOMPARE(m_messages, QStringList{u"info: 1250.5 zł true tabela"_s});
        m_ext->resetSettings(u"waluta"_s);
        m_messages.clear();
        m_ext->runCommand(u"waluta"_s, u"pokaz"_s);
        QCOMPARE(m_messages, QStringList{u"info: 1000 zł false lista"_s});
        QCOMPARE(m_ext->list()->rows().size() > 0, true);
    }

    void approvingTheVersionRead()
    {
        approveShared(); // (the code it shares)
        const QString path = page(u"Licznik słów"_s);
        // New on this device: the review shows all of the code, nothing to compare with.
        QVariantMap review = m_ext->approvalReview(path);
        QCOMPARE(review.value(u"title"_s).toString(), u"Licznik słów"_s);
        QCOMPARE(review.value(u"before"_s).toBool(), false);
        QVERIFY(review.value(u"added"_s).toInt() > 10);
        QCOMPARE(review.value(u"removed"_s).toInt(), 0);
        QVERIFY(review.value(u"oldCode"_s).toString().isEmpty());
        const QString first = review.value(u"hash"_s).toString();
        QCOMPARE(m_ext->approveVersion(path, first), QString());
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // A sync changes a line of its code and asks to write: it waits, and the review shows what
        // changed against the version approved here, and the permission it asks for anew.
        QString text = disk(path);
        QVERIFY(text.contains(u"uprawnienia"_s) || text.contains(u"permissions"_s) || true);
        text.replace(u"---\ntags: [rozszerzenie]\n"_s, u"---\ntags: [rozszerzenie]\nuprawnienia: [zapis]\n"_s);
        const qsizetype code = text.indexOf(u"```luau\n"_s);
        QVERIFY(code > 0);
        text.insert(code + 8, u"local ZMIENIONE_PRZEZ_SYNC = true\n"_s);
        QVERIFY(syncBrings(path, text));
        QTRY_COMPARE(m_ext->pageState(path), u"approval"_s);
        review = m_ext->approvalReview(path);
        QCOMPARE(review.value(u"before"_s).toBool(), true);
        QCOMPARE(review.value(u"added"_s).toInt(), 1);
        QCOMPARE(review.value(u"removed"_s).toInt(), 0);
        QVERIFY(review.value(u"code"_s).toString().contains(u"ZMIENIONE_PRZEZ_SYNC"_s));
        QVERIFY(!review.value(u"oldCode"_s).toString().contains(u"ZMIENIONE_PRZEZ_SYNC"_s));
        QCOMPARE(review.value(u"newPermissions"_s).toStringList(), QStringList{u"change notes"_s});
        bool added = false;
        for (const QVariant &l : review.value(u"lines"_s).toList())
            added = added
                    || (l.toMap().value(u"kind"_s) == u"added"_s
                        && l.toMap().value(u"text"_s).toString().contains(u"ZMIENIONE"_s));
        QVERIFY(added);
        const QString shown = review.value(u"hash"_s).toString();
        // While it is read, another sync replaces the code again: the version shown is not approved.
        text.replace(u"local ZMIENIONE_PRZEZ_SYNC = true\n"_s, u"local ZMIENIONE_ZNOWU = true\n"_s);
        QVERIFY(syncBrings(path, text));
        QTRY_VERIFY(m_ext->pageHash(path) != shown);
        QCOMPARE(m_ext->approveVersion(path, shown), u"changed"_s);
        QCOMPARE(m_ext->pageState(path), u"approval"_s);
        QCOMPARE(m_ext->approveVersion(page(u"Nie ma"_s), shown), u"gone"_s);
        // The version read now: approved, it runs.
        QCOMPARE(m_ext->approveVersion(path, m_ext->approvalReview(path).value(u"hash"_s).toString()), QString());
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // What was approved is kept for the next review (the device state, not the notebook).
        QVERIFY(m_vault->store()
                        ->get(extensions::approvedCodeStoreKey(u"licznik-slow"_s))
                        .toObject()
                        .value(u"code"_s)
                        .toString()
                        .contains(u"ZMIENIONE_ZNOWU"_s));
    }

    void sharedCode()
    {
        const QString library =
                u"local M = {}\nfunction M.powitanie(imie) return \"Cześć, \" .. imie .. \"!\" end\nreturn M"_s;
        addExtension(u"Wspólne"_s, library, QString());
        addExtension(
                u"Powitanie"_s,
                u"local w = b.require(\"Wspólne\")\nb.command(\"witaj\", function() return w.powitanie(\"Żaneta\") end)"_s,
                QString());
        m_messages.clear();
        m_ext->runCommand(u"powitanie"_s, u"witaj"_s);
        QCOMPARE(m_messages, QStringList{u"info: Cześć, Żaneta!"_s});
        // The library changes on another device: it waits, and its user cannot run its code meanwhile.
        QString text = disk(page(u"Wspólne"_s));
        text.replace(u"Cześć, "_s, u"Dzień dobry, "_s);
        QVERIFY(syncBrings(page(u"Wspólne"_s), text));
        QTRY_COMPARE(m_ext->pageState(page(u"Wspólne"_s)), u"approval"_s);
        QTRY_COMPARE(m_ext->pageState(page(u"Powitanie"_s)), u"error"_s);
        QVERIFY2(m_ext->pageMessage(page(u"Powitanie"_s)).contains(u"waits for approval"_s),
                 qPrintable(m_ext->pageMessage(page(u"Powitanie"_s))));
        // Approved: its user loads again with the new code.
        m_ext->approve(page(u"Wspólne"_s));
        QTRY_COMPARE(m_ext->pageState(page(u"Powitanie"_s)), u"running"_s);
        m_messages.clear();
        m_ext->runCommand(u"powitanie"_s, u"witaj"_s);
        QCOMPARE(m_messages, QStringList{u"info: Dzień dobry, Żaneta!"_s});
        // Settings → Extensions names what it uses.
        bool listed = false;
        for (const ExtensionRow &row : m_ext->list()->rows())
            listed = listed || (row.id == u"powitanie" && row.modules == QStringList{u"Wspólne"_s});
        QVERIFY(listed);
    }

    void selectionOfTheEditor()
    {
        addExtension(u"Zaznaczenie"_s, u"b.command(\"pokaz\", function(ctx) return `[{ctx.selection}]` end)"_s,
                     QString());
        m_messages.clear();
        m_ext->offerSelection(u"Dom.md"_s, u"Zapłacić czynsz"_s);
        m_ext->runCommand(u"zaznaczenie"_s, u"pokaz"_s, u"Dom.md"_s);
        QCOMPARE(m_messages, QStringList{u"info: [Zapłacić czynsz]"_s});
        // Offered for another page, or not at all: none; an offer is used once.
        m_messages.clear();
        m_ext->offerSelection(u"Zakupy.md"_s, u"mleko"_s);
        m_ext->runCommand(u"zaznaczenie"_s, u"pokaz"_s, u"Dom.md"_s);
        m_ext->runCommand(u"zaznaczenie"_s, u"pokaz"_s, u"Dom.md"_s);
        QCOMPARE(m_messages, (QStringList{u"info: []"_s, u"info: []"_s}));
        // On a block (the editor's command with block = true): the selection comes with it.
        m_messages.clear();
        m_ext->runBlockCommand(u"zaznaczenie/pokaz"_s, u"Dom.md"_s, 5, u"żarówkę"_s);
        QCOMPARE(m_messages, QStringList{u"info: [żarówkę]"_s});
        // A selection of a whole book: its beginning.
        m_messages.clear();
        m_ext->offerSelection(u"Dom.md"_s, QString(300000, u'ż'));
        m_ext->runCommand(u"zaznaczenie"_s, u"pokaz"_s, u"Dom.md"_s);
        QCOMPARE(m_messages.size(), 1);
        QCOMPARE(m_messages[0].size(), 200000 + 8);
    }

    void copiedToAnotherNotebook()
    {
        // The word count and the library it requires go to another notebook, approved there.
        addExtension(u"Wspólne"_s, u"return { zero = 0 }"_s, QString());
        QString counter = disk(page(u"Licznik słów"_s));
        counter.replace(u"```luau\n"_s, u"```luau\nlocal wspolne = b.require(\"Wspólne\")\n"_s);
        QVERIFY(m_vault->writeText(page(u"Licznik słów"_s), counter, ChangeSource::User));
        QTRY_VERIFY(m_ext->approvalReview(page(u"Licznik słów"_s)).value(u"code"_s).toString().contains(u"wspolne"_s));
        approveSample(u"Licznik słów"_s);
        QCOMPARE(m_ext->pageState(page(u"Licznik słów"_s)), u"running"_s);
        const QString other = m_tmp.filePath(u"inny%1"_s.arg(m_n));
        QDir().mkpath(other);
        const QString otherState = m_tmp.filePath(u"inny-stan%1"_s.arg(m_n));
        QCOMPARE(m_ext->copyToNotebook(page(u"Licznik słów"_s), other), u"Extensions cannot be copied here."_s);
        m_ext->setDeviceStates([&](const QString &) { return otherState; });
        QCOMPARE(m_ext->copyToNotebook(page(u"Licznik słów"_s), m_dir), u"Choose another notebook."_s);
        m_messages.clear();
        QCOMPARE(m_ext->copyToNotebook(page(u"Licznik słów"_s), other), QString());
        QCOMPARE(readFile(other + u'/' + page(u"Licznik słów"_s)), counter);
        QCOMPARE(readFile(other + u'/' + page(u"Wspólne"_s)), disk(page(u"Wspólne"_s)));
        QCOMPARE(readFile(other + u'/' + page(u"Wspólne funkcje"_s)), disk(page(u"Wspólne funkcje"_s)));
        QCOMPARE(m_messages.size(), 1);
        {
            LocalStore store(otherState + u"/store.json"_s);
            QCOMPARE(store.get(extensions::approvedStoreKey()).toArray().size(), 3);
            QVERIFY(!store.get(extensions::approvedCodeStoreKey(u"licznik-slow"_s)).toObject().isEmpty());
        }
        // Again: nothing to do. Changed there: a conflict, replaced only when asked.
        QCOMPARE(m_ext->copyToNotebook(page(u"Licznik słów"_s), other), QString());
        QFile changed(other + u'/' + page(u"Wspólne"_s));
        QVERIFY(changed.open(QIODevice::WriteOnly));
        changed.write("zmieniona tam\n");
        changed.close();
        QCOMPARE(m_ext->copyToNotebook(page(u"Licznik słów"_s), other), u"conflict"_s);
        QCOMPARE(readFile(other + u'/' + page(u"Wspólne"_s)), u"zmieniona tam\n"_s);
        QCOMPARE(m_ext->copyToNotebook(page(u"Licznik słów"_s), other, true), QString());
        QCOMPARE(readFile(other + u'/' + page(u"Wspólne"_s)), disk(page(u"Wspólne"_s)));
        QCOMPARE(m_ext->copyToNotebook(u"Dom.md"_s, other), u"This page is no extension."_s);
    }
};

QTEST_GUILESS_MAIN(TestShellExtensions)
#include "tst_extensions.moc"
