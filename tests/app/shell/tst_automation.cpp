// brulion:// addresses on AppController (src/app/shell/appautomation.cpp, docs/UI.md
// "Automation"): opening navigates at once; writing without the key waits for the confirmation
// dialog (pendingAutomation) and writes only on confirmAutomation(), never on dismissAutomation();
// with the notebook's key it writes at once; a wrong key, policy Key and policy Off refuse;
// addresses wait while a notebook opens (also from the launch's --uri and the --open files), go to
// another notebook named by `vault` (opened first), go back from the documentation to the user's
// notebook; a second launch's `uri` message and a brulion:// link clicked in a note arrive the
// same way; a new page is never written over an existing one; an extension command that is not
// approved never runs; the keys are per notebook; automationLink() builds addresses that read back;
// prompt links open a draft that is never sent.
#include "appcontroller.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "automation/policy.h"
#include "automation/uri.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;

namespace {

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

qsizetype requests(const QSignalSpy &spy, const QString &action)
{
    qsizetype n = 0;
    for (const QList<QVariant> &args : spy)
        n += args.value(0).toString() == action ? 1 : 0;
    return n;
}

QString lastToast(const QSignalSpy &spy)
{
    return spy.isEmpty() ? QString() : spy.last().value(0).toString();
}

} // namespace

class TestAutomation : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_counter = 0;

    std::unique_ptr<AppController> makeApp(const QStringList &args = {}, const QStringList &openFiles = {})
    {
        AppController::Options o;
        o.settingsFile = m_tmp.filePath(u"settings-%1.ini"_s.arg(++m_counter));
        o.dataDir = m_tmp.filePath(u"data-%1"_s.arg(m_counter));
        o.arguments = QStringList{u"brulion"_s} + args;
        o.openFiles = openFiles;
        auto app = std::make_unique<AppController>(o);
        app->setClock([] { return QDateTime(QDate(2026, 11, 20), QTime(10, 0)); });
        return app;
    }
    /// A notebook folder with a shopping page and a journal template.
    QString notebook(const QString &name)
    {
        const QString root = m_tmp.filePath(name);
        writeFile(root + u"/Zakupy.md"_s, "# Zakupy\n\n## Dziś\n- chleb\n\n## Jutro\n- ser\n");
        writeFile(root + u"/templates/journal.md"_s, "## Plan dnia\n- \n");
        return root;
    }
    std::unique_ptr<AppController> openApp(const QString &root, const QStringList &args = {})
    {
        auto app = makeApp(QStringList{u"--vault"_s, root} + args);
        app->start();
        [&] { QTRY_COMPARE(app->status(), u"ready"_s); }();
        return app;
    }
    static QString key(AppController &app) { return app.createAutomationKey(); }

private Q_SLOTS:
    void opens()
    {
        const QString root = notebook(u"opens"_s);
        auto app = openApp(root);
        app->receiveUri(u"brulion://open?page=zakupy&heading=Jutro"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(app->currentRoute().value(u"heading"_s).toString(), u"Jutro"_s);
        app->receiveUri(u"brulion://daily?date=wczoraj"_s);
        QCOMPARE(app->currentRoute().value(u"view"_s).toString(), u"journal"_s);
        QCOMPARE(app->currentRoute().value(u"date"_s).toString(), u"2026-11-19"_s);
        app->receiveUri(u"web+brulion://search?q=%C5%BC%C3%B3%C5%82w"_s, u"web"_s);
        QCOMPARE(app->currentRoute().value(u"view"_s).toString(), u"search"_s);
        QCOMPARE(app->currentRoute().value(u"query"_s).toString(), u"żółw"_s);
        app->receiveUri(u"brulion://view?name=calendar"_s);
        QCOMPARE(app->currentRoute().value(u"view"_s).toString(), u"calendar"_s);
        // Opening a day never creates its note.
        QVERIFY(!QFile::exists(root + u"/journals/2026-11-19.md"_s));
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->receiveUri(u"brulion://capture"_s);
        QCOMPARE(requests(ui, u"capture"_s), 1);
    }

    void writingAsksFirst()
    {
        const QString root = notebook(u"asks"_s);
        auto app = openApp(root);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->receiveUri(u"brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=Mleko&x-source=Strona"_s);
        QCOMPARE(requests(ui, u"automation"_s), 1);
        const QVariantMap pending = app->pendingAutomation();
        QCOMPARE(pending.value(u"action"_s).toString(), u"append"_s);
        QCOMPARE(pending.value(u"target"_s).toString(), u"Zakupy"_s);
        QCOMPARE(pending.value(u"heading"_s).toString(), u"Dziś"_s);
        QCOMPARE(pending.value(u"block"_s).toString(), u"- Mleko"_s);
        QCOMPARE(pending.value(u"source"_s).toString(), u"Strona"_s);
        const QByteArray before = readRaw(root + u"/Zakupy.md"_s);
        // "Cancel": nothing written.
        app->dismissAutomation();
        QVERIFY(app->pendingAutomation().isEmpty());
        QCOMPARE(readRaw(root + u"/Zakupy.md"_s), before);
        // "Write".
        app->receiveUri(u"brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=Mleko"_s);
        const QVariantMap result = app->confirmAutomation();
        QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
        QCOMPARE(readRaw(root + u"/Zakupy.md"_s), QByteArray("# Zakupy\n\n## Dziś\n- chleb\n- Mleko\n\n## Jutro\n- ser\n"));
        QVERIFY(app->pendingAutomation().isEmpty());
        QVERIFY(!app->confirmAutomation().value(u"ok"_s).toBool()); // nothing waits any more
    }

    void keyWritesAtOnce()
    {
        const QString root = notebook(u"keyed"_s);
        auto app = openApp(root);
        const QString k = key(*app);
        QCOMPARE(k.size(), 32);
        QCOMPARE(app->automationKey(), k);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QSignalSpy toasts(app.get(), &AppController::toastWithActionRequested);
        app->receiveUri(u"brulion://task?text=Zadzwoni%C4%87+do+mamy&remind=jutro+o+18&key="_s + k);
        QCOMPARE(requests(ui, u"automation"_s), 0);
        QCOMPARE(QString::fromUtf8(readRaw(root + u"/journals/2026-11-20.md"_s)),
                 u"## Plan dnia\n- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00\n"_s);
        QCOMPARE(toasts.size(), 1);
        QCOMPARE(toasts.first().value(1).toString(), u"Reminder added to today's journal"_s);
        // A new page with the key: opened (new pages open by default).
        app->receiveUri(u"brulion://new?title=Pomys%C5%82y&text=pierwszy&key="_s + k.toUpper());
        QCOMPARE(readRaw(root + u"/Pomysły.md"_s), QByteArray("pierwszy\n"));
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Pomysły.md"_s);
        // Never over an existing page: that page opens, nothing is written.
        app->receiveUri(u"brulion://new?title=zakupy&text=inne&key="_s + k);
        QCOMPARE(readRaw(root + u"/Zakupy.md"_s), QByteArray("# Zakupy\n\n## Dziś\n- chleb\n\n## Jutro\n- ser\n"));
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
    }

    void refusals()
    {
        const QString root = notebook(u"refusals"_s);
        auto app = openApp(root);
        const QString k = key(*app);
        const QByteArray before = readRaw(root + u"/Zakupy.md"_s);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->receiveUri(u"brulion://append?page=Zakupy&text=x&key=zly"_s);
        QVERIFY(lastToast(toasts).contains(u"key"_s));
        // Policy Key: no dialog, no write without the key; with it, writing works.
        app->setAutomationPolicy(u"key"_s);
        QCOMPARE(app->automationPolicy(), u"key"_s);
        app->receiveUri(u"brulion://append?page=Zakupy&text=x"_s);
        QCOMPARE(requests(ui, u"automation"_s), 0);
        QCOMPARE(readRaw(root + u"/Zakupy.md"_s), before);
        app->receiveUri(u"brulion://open?page=Zakupy%23Jutro"_s); // opening still works
        QCOMPARE(app->currentRoute().value(u"heading"_s).toString(), u"Jutro"_s);
        // Policy Off: nothing, not even with the key.
        app->setAutomationPolicy(u"off"_s);
        app->receiveUri(u"brulion://append?page=Zakupy&text=x&key="_s + k);
        app->receiveUri(u"brulion://daily"_s);
        QCOMPARE(app->currentRoute().value(u"heading"_s).toString(), u"Jutro"_s);
        QCOMPARE(readRaw(root + u"/Zakupy.md"_s), before);
        QVERIFY(lastToast(toasts).contains(u"Settings"_s));
        app->setAutomationPolicy(u"whatever"_s); // back to the default
        QCOMPARE(app->automationPolicy(), u"ask"_s);
        // Malformed addresses and unknown parts are told, never guessed.
        const qsizetype n = toasts.size();
        app->receiveUri(u"brulion://append?text=50%"_s);
        app->receiveUri(u"brulion://append?page=Zakupy&heding=Dzi%C5%9B&text=x&key="_s + k);
        app->receiveUri(u"brulion://delete?page=Zakupy"_s);
        QCOMPARE(toasts.size(), n + 3);
        QCOMPARE(readRaw(root + u"/Zakupy.md"_s), before);
        // An extension command that no approved extension has never runs.
        app->receiveUri(u"brulion://run?extension=obca&command=zrob&key="_s + k);
        QVERIFY(lastToast(toasts).contains(u"obca/zrob"_s));
    }

    void keysPerNotebook()
    {
        const QString first = notebook(u"keysA"_s);
        auto app = openApp(first);
        const QString k = key(*app);
        QCOMPARE(app->createAutomationKey(), k); // kept
        const QString renewed = app->renewAutomationKey();
        QVERIFY(!renewed.isEmpty() && renewed != k);
        app->receiveUri(u"brulion://append?page=Zakupy&text=stary&key="_s + k);
        QVERIFY(!readRaw(first + u"/Zakupy.md"_s).contains("stary"));
        app->removeAutomationKey();
        QCOMPARE(app->automationKey(), QString());
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->receiveUri(u"brulion://append?page=Zakupy&text=bez&key="_s + renewed);
        QCOMPARE(requests(ui, u"automation"_s), 0); // a key the notebook has no more: refused, not asked
        QVERIFY(!readRaw(first + u"/Zakupy.md"_s).contains("bez"));
    }

    void waitsForTheNotebook()
    {
        // The launch's address (--uri, and one among the --open files): run once the notebook is
        // ready.
        const QString root = notebook(u"launch"_s);
        auto app = makeApp({u"--vault"_s, root, u"--uri"_s, u"brulion://open?page=Zakupy"_s},
                           {u"brulion://view?name=tasks"_s});
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(app->currentRoute().value(u"view"_s).toString(), u"tasks"_s);
        // (The page opened first, then the view.)
        QVERIFY(app->canGoBack());
    }

    void otherNotebook()
    {
        const QString home = notebook(u"Dom"_s);
        const QString work = notebook(u"Praca"_s);
        writeFile(work + u"/Projekt.md"_s, "- start\n");
        auto app = openApp(work);
        app->openVault(home);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(app->workspace()->rootPath(), home);
        // Named by its folder's name: opened, then the address runs there.
        app->receiveUri(u"brulion://open?vault=praca&page=Projekt"_s);
        QTRY_COMPARE(app->workspace()->rootPath(), work);
        QTRY_COMPARE(app->currentRoute().value(u"path"_s).toString(), u"Projekt.md"_s);
        // A notebook the device does not know: told, nothing opens.
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->receiveUri(u"brulion://open?vault=Nieznany&page=Projekt"_s);
        QVERIFY(lastToast(toasts).contains(u"Nieznany"_s));
        QCOMPARE(app->workspace()->rootPath(), work);
    }

    void fromTheDocumentation()
    {
        const QString root = notebook(u"docs-back"_s);
        auto app = openApp(root);
        app->openDocsVault();
        QTRY_VERIFY(app->inDocsVault() && app->status() == u"ready");
        app->receiveUri(u"brulion://open?page=Zakupy"_s);
        QTRY_COMPARE(app->workspace()->rootPath(), root);
        QTRY_COMPARE(app->currentRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
    }

    void instanceMessageAndNoteLinks()
    {
        const QString root = notebook(u"messages"_s);
        auto app = openApp(root);
        QVERIFY(app->handleInstanceMessage("uri", QJsonObject{{u"uri"_s, u"brulion://open?page=Zakupy"_s}}));
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
        // A link in a note: its address is checked like any other (a write asks).
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->workspace()->openUrl(QUrl(u"brulion://capture?text=Z+notatki"_s));
        QCOMPARE(requests(ui, u"automation"_s), 1);
        QCOMPARE(app->pendingAutomation().value(u"block"_s).toString(), u"- Z notatki"_s);
        app->dismissAutomation();
    }

    void links()
    {
        const QString root = notebook(u"Mój notatnik"_s);
        auto app = openApp(root);
        const QString k = key(*app);
        const QString link = app->automationLink(u"append"_s, {{u"page"_s, u"Zakupy"_s}, {u"text"_s, u"Mleko 2%"_s}}, true);
        const automation::ParsedUri parsed = automation::parseUri(link);
        QVERIFY(parsed.request);
        QCOMPARE(parsed.request->vault, u"Mój notatnik"_s);
        QCOMPARE(parsed.request->key, k);
        QCOMPARE(parsed.request->value({u"text"}), u"Mleko 2%"_s);
        QVERIFY(!app->automationLink(u"append"_s, {{u"text"_s, u"x"_s}}, false).contains(u"key="_s));
        QVERIFY(app->automationLink(u"nonsense"_s, {}, false).isEmpty());
        QCOMPARE(app->pageAutomationLink(u"Zakupy.md"_s, u"Jutro"_s),
                 u"brulion://open?page=Zakupy&heading=Jutro&vault=M%C3%B3j%20notatnik"_s);
    }

    // Prompt links (brulion://chat): a new conversation's draft, never sent — the partner only when
    // it is set up here (by its id or name), the page only when the notebook has it, what nobody
    // sees taken out; nothing is written.
    void promptLinks()
    {
        const QString root = notebook(u"prompty"_s);
        auto app = openApp(root);
        app->workspace()->chats()->setAgents({AgentConfig{u"claude"_s, u"Claude Code"_s, u"claude-agent-acp"_s}});
        app->receiveUri(u"brulion://chat?prompt=Co%20mam%20kupi%C4%87%3F%E2%80%8B&page=zakupy&partner=acp%3Aclaude"_s);
        QVariantMap route = app->currentRoute();
        QCOMPARE(route.value(u"view"_s).toString(), u"chat"_s);
        QVERIFY(route.value(u"new"_s).toBool());
        QVERIFY(route.value(u"fromLink"_s).toBool());
        QCOMPARE(route.value(u"draft"_s).toString(), u"Co mam kupić?"_s);
        QCOMPARE(route.value(u"hidden"_s).toInt(), 1);
        QCOMPARE(route.value(u"page"_s).toString(), u"Zakupy"_s);
        QCOMPARE(route.value(u"partner"_s).toString(), u"acp:claude"_s);
        QVERIFY(!route.contains(u"prompt"_s)); // (`prompt` would send it at once)
        // By its name too; one not set up here: the default, said.
        app->receiveUri(u"brulion://chat?prompt=Hej&partner=claude%20code"_s);
        QCOMPARE(app->currentRoute().value(u"partner"_s).toString(), u"acp:claude"_s);
        app->receiveUri(u"brulion://chat?prompt=Hej&partner=acp%3Agemini&page=Brak"_s);
        route = app->currentRoute();
        QVERIFY(!route.contains(u"partner"_s));
        QCOMPARE(route.value(u"partnerMissing"_s).toString(), u"acp:gemini"_s);
        QCOMPARE(route.value(u"pageMissing"_s).toString(), u"Brak"_s);
        // Nothing written: no conversation.
        QVERIFY(!QFileInfo::exists(root + u"/AI"_s));
        // Built for a message or a draft: no notebook named, so it opens in anyone's.
        const QString link = app->chatPromptLink(u"Podsumuj „Zakupy”"_s, u"acp:claude"_s, u"Zakupy"_s);
        QCOMPARE(link, u"brulion://chat?prompt=Podsumuj%20%E2%80%9EZakupy%E2%80%9D&partner=acp%3Aclaude&page=Zakupy"_s);
        QVERIFY(!link.contains(u"vault="_s));
    }
};

QTEST_MAIN(TestAutomation)
#include "tst_automation.moc"
