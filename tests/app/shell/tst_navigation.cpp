// Navigation of the shell: routes (normalisation, equality), the back/forward history and
// AppController's navigation (side panel, Android back, journals, notifications, renamed and
// deleted pages), its vault lifecycle and the command palette's commands.
#include "appcontroller.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "navigation.h"
#include "workspace.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::shell;

namespace {

QVariantMap page(const QString &path)
{
    return { { u"view"_s, u"page"_s }, { u"path"_s, path } };
}

QVariantMap view(const QString &name)
{
    return { { u"view"_s, name } };
}

} // namespace

class TestNavigation : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    std::unique_ptr<AppController> makeApp(const QStringList &args = {})
    {
        AppController::Options o;
        o.settingsFile = m_tmp.filePath(u"settings-%1.ini"_s.arg(++m_counter));
        o.dataDir = m_tmp.filePath(u"data-%1"_s.arg(m_counter));
        o.arguments = args;
        auto app = std::make_unique<AppController>(o);
        app->setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 0)); });
        return app;
    }
    /// An app with an open vault (folder `name` in the temp dir).
    std::unique_ptr<AppController> openApp(const QString &name, const QStringList &args = {})
    {
        auto app = makeApp(args);
        app->start();
        if (app->status() == u"onboarding")
            app->openVault(m_tmp.filePath(name));
        [&] { QTRY_COMPARE(app->status(), u"ready"_s); }();
        return app;
    }
    int m_counter = 0;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        // No desktop notification service in these tests (whatever the machine has): the
        // reminders integration then falls back to the tray, deterministically.
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/brulion-test-bus");
    }

    // ---- routes ---------------------------------------------------------------------------

    void normalizeRoute_data()
    {
        QTest::addColumn<QVariantMap>("input");
        QTest::addColumn<QVariantMap>("expected");
        QTest::newRow("unknown view") << view(u"nope"_s) << QVariantMap();
        QTest::newRow("no view") << QVariantMap{ { u"path"_s, u"a.md"_s } } << QVariantMap();
        QTest::newRow("home alias") << view(u"home"_s) << view(u"journals"_s);
        QTest::newRow("page without path") << view(u"page"_s) << QVariantMap();
        QTest::newRow("empty values dropped")
            << QVariantMap{ { u"view"_s, u"page"_s }, { u"path"_s, u"Żółw.md"_s }, { u"blockId"_s, QString() } }
            << page(u"Żółw.md"_s);
        QTest::newRow("line as int")
            << QVariantMap{ { u"view"_s, u"page"_s }, { u"path"_s, u"a.md"_s }, { u"line"_s, 4.0 } }
            << QVariantMap{ { u"view"_s, u"page"_s }, { u"path"_s, u"a.md"_s }, { u"line"_s, 4 } };
        QTest::newRow("negative line dropped")
            << QVariantMap{ { u"view"_s, u"page"_s }, { u"path"_s, u"a.md"_s }, { u"line"_s, -1 } } << page(u"a.md"_s);
        QTest::newRow("journal date")
            << QVariantMap{ { u"view"_s, u"journal"_s }, { u"date"_s, u"2026-10-14"_s } }
            << QVariantMap{ { u"view"_s, u"journal"_s }, { u"date"_s, u"2026-10-14"_s } };
        QTest::newRow("journal bad date") << QVariantMap{ { u"view"_s, u"journal"_s }, { u"date"_s, u"14.10.2026"_s } }
                                          << QVariantMap();
        QTest::newRow("tag without tag") << view(u"tag"_s) << view(u"tags"_s);
        QTest::newRow("extra keys kept")
            << QVariantMap{ { u"view"_s, u"settings"_s }, { u"section"_s, u"editor"_s } }
            << QVariantMap{ { u"view"_s, u"settings"_s }, { u"section"_s, u"editor"_s } };
    }
    void normalizeRoute()
    {
        QFETCH(QVariantMap, input);
        QFETCH(QVariantMap, expected);
        QCOMPARE(brulion::shell::normalizeRoute(input), expected);
    }

    void sameRoute()
    {
        QVERIFY(brulion::shell::sameRoute(page(u"a.md"_s), page(u"a.md"_s)));
        QVERIFY(!brulion::shell::sameRoute(page(u"a.md"_s), page(u"b.md"_s)));
        // QML passes numbers as double.
        QVariantMap a = page(u"a.md"_s), b = page(u"a.md"_s);
        a.insert(u"line"_s, 3);
        b.insert(u"line"_s, 3.0);
        QVERIFY(brulion::shell::sameRoute(a, b));
        b.insert(u"line"_s, 4);
        QVERIFY(!brulion::shell::sameRoute(a, b));
    }

    void historyBackForward()
    {
        RouteHistory h;
        QCOMPARE(h.current(), view(u"journals"_s));
        QVERIFY(!h.canGoBack());
        QVERIFY(h.navigate(page(u"a.md"_s)));
        QVERIFY(h.navigate(page(u"b.md"_s)));
        QVERIFY(!h.navigate(page(u"b.md"_s))); // same route: no new entry
        QVERIFY(!h.navigate(view(u"bogus"_s)));
        QCOMPARE(h.backDepth(), 2);
        QVERIFY(h.back());
        QCOMPARE(h.current(), page(u"a.md"_s));
        QVERIFY(h.canGoForward());
        QVERIFY(h.back());
        QCOMPARE(h.current(), view(u"journals"_s));
        QVERIFY(!h.back());
        QVERIFY(h.forward());
        QVERIFY(h.forward());
        QCOMPARE(h.current(), page(u"b.md"_s));
        QVERIFY(!h.forward());
        // Navigating after going back drops the forward entries.
        h.back();
        QVERIFY(h.navigate(view(u"tasks"_s)));
        QVERIFY(!h.canGoForward());
        QCOMPARE(h.backDepth(), 2);
    }

    void historyReplaceAndReset()
    {
        RouteHistory h;
        h.navigate(page(u"a.md"_s));
        QVariantMap withLine = page(u"a.md"_s);
        withLine.insert(u"line"_s, 7);
        QVERIFY(h.replace(withLine));
        QCOMPARE(h.current().value(u"line"_s).toInt(), 7);
        QCOMPARE(h.backDepth(), 1);
        QVERIFY(!h.replace(view(u"x"_s)));
        h.reset();
        QCOMPARE(h.current(), view(u"journals"_s));
        QCOMPARE(h.backDepth(), 0);
    }

    void historyDepthIsBounded()
    {
        RouteHistory h;
        for (int i = 0; i < RouteHistory::MaxDepth + 30; ++i)
            h.navigate(page(u"p%1.md"_s.arg(i)));
        QCOMPARE(h.backDepth(), RouteHistory::MaxDepth);
    }

    void historyRemoveIf()
    {
        RouteHistory h;
        h.navigate(page(u"a.md"_s));
        h.navigate(page(u"gone.md"_s));
        h.navigate(page(u"a.md"_s));
        h.navigate(page(u"gone.md"_s));
        const bool changed = h.removeIf([](const QVariantMap &r) { return r.value(u"path"_s).toString() == u"gone.md"; });
        QVERIFY(changed);
        // The previous page became current; the duplicates "a.md, a.md" collapsed.
        QCOMPARE(h.current(), page(u"a.md"_s));
        QCOMPARE(h.backDepth(), 1);
        QVERIFY(h.back());
        QCOMPARE(h.current(), view(u"journals"_s));
        // Nothing left: home.
        RouteHistory single;
        single.navigate(page(u"gone.md"_s));
        single.back();
        single.forward();
        QVERIFY(single.removeIf([](const QVariantMap &r) { return r.value(u"path"_s).toString() == u"gone.md"; }));
        QCOMPARE(single.current(), view(u"journals"_s));
        QCOMPARE(single.backDepth(), 0);
        QVERIFY(!single.removeIf([](const QVariantMap &) { return false; }));
    }

    // ---- AppController ----------------------------------------------------------------------

    void bootWithoutVaultShowsOnboarding()
    {
        auto app = makeApp();
        QCOMPARE(app->status(), u"boot"_s);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);
        app->start(); // only once
        QCOMPARE(app->status(), u"onboarding"_s);
    }

    void openVaultRemembersIt()
    {
        auto app = makeApp();
        app->start();
        QSignalSpy status(app.get(), &AppController::statusChanged);
        const QString path = m_tmp.filePath(u"Mój notatnik"_s);
        app->openVault(path);
        QCOMPARE(app->status(), u"loading"_s);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->workspace()->isOpen());
        QCOMPARE(app->workspace()->rootPath(), QDir(path).absolutePath());
        QCOMPARE(app->settings()->currentVault(), QDir::cleanPath(path));
        QCOMPARE(app->settings()->recentVaults().first().toMap().value(u"name"_s).toString(), u"Mój notatnik"_s);
        QCOMPARE(app->currentView(), u"journals"_s);
        QVERIFY(app->atHome());
        QVERIFY(app->deviceStatePath(path).startsWith(app->dataDir() + u"/vaults/"_s));

        // A second controller on the same settings reopens it at start().
        AppController::Options o;
        o.settingsFile = app->settings()->fileName();
        o.dataDir = app->dataDir();
        app.reset();
        AppController again(o);
        again.start();
        QCOMPARE(again.status(), u"loading"_s);
        QTRY_COMPARE(again.status(), u"ready"_s);
        QCOMPARE(again.workspace()->name(), u"Mój notatnik"_s);
    }

    void missingVaultFolderIsNotRecreated()
    {
        auto app = makeApp();
        const QString path = m_tmp.filePath(u"usunięty"_s);
        app->settings()->addRecentVault(path); // current, but the folder does not exist
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);
        QCOMPARE(toasts.size(), 1);
        QVERIFY(!QFileInfo::exists(path));
    }

    void openVaultOnAFileFails()
    {
        auto app = makeApp();
        QFile file(m_tmp.filePath(u"plik.txt"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        app->openVault(file.fileName());
        QTRY_COMPARE(app->status(), u"error"_s);
        QVERIFY(!app->errorString().isEmpty());
        app->showOnboarding();
        QCOMPARE(app->status(), u"onboarding"_s);
    }

    void navigateBackForward()
    {
        auto app = openApp(u"nav"_s);
        QSignalSpy routeSpy(app.get(), &AppController::currentRouteChanged);
        app->navigate(view(u"tasks"_s));
        QCOMPARE(app->currentView(), u"tasks"_s);
        QVERIFY(app->canGoBack());
        QVERIFY(!app->atHome());
        app->navigate(view(u"tasks"_s)); // no duplicate
        app->navigate(view(u"nonsense"_s)); // ignored
        QCOMPARE(routeSpy.size(), 1);
        app->openTag(u"#projekt/dom"_s);
        QCOMPARE(app->currentRoute().value(u"tag"_s).toString(), u"projekt/dom"_s);
        app->search(u"żółw"_s);
        QCOMPARE(app->currentRoute().value(u"query"_s).toString(), u"żółw"_s);
        QVERIFY(app->back());
        QCOMPARE(app->currentView(), u"tag"_s);
        QVERIFY(app->forward());
        QCOMPARE(app->currentView(), u"search"_s);
        app->goHome();
        QVERIFY(app->atHome());
        QVERIFY(app->canGoBack());
    }

    void replaceRouteKeepsHistory()
    {
        auto app = openApp(u"replace"_s);
        app->openPage(u"a.md"_s);
        QVariantMap r = page(u"a.md"_s);
        r.insert(u"line"_s, 12);
        app->replaceRoute(r);
        QCOMPARE(app->currentRoute().value(u"line"_s).toInt(), 12);
        QVERIFY(app->back());
        QVERIFY(app->atHome());
    }

    void androidBackKey()
    {
        auto app = openApp(u"back"_s);
        QVERIFY(!app->handleBackKey()); // home: the app may close
        app->navigate(view(u"calendar"_s));
        QVERIFY(app->handleBackKey());
        QVERIFY(app->atHome());
        // Not home but nothing to go back to (e.g. the start route was not home).
        auto other = openApp(u"back2"_s, { u"--route"_s, u"settings"_s });
        QCOMPARE(other->currentView(), u"settings"_s);
        QVERIFY(other->handleBackKey());
        QVERIFY(other->atHome());
    }

    void sidePanel()
    {
        auto app = openApp(u"side"_s);
        QSignalSpy side(app.get(), &AppController::sidePanelRouteChanged);
        app->setSidePanelAllowed(true);
        app->openPage(u"b.md"_s, -1, true);
        QCOMPARE(app->sidePanelRoute(), page(u"b.md"_s));
        QVERIFY(app->sidePanelVisible());
        QVERIFY(app->atHome()); // main view untouched
        QCOMPARE(side.size(), 1);
        app->clearSidePanelRoute();
        QVERIFY(app->sidePanelRoute().isEmpty());
        // A narrow window has no side panel: the route opens in the main view.
        app->setSidePanelAllowed(false);
        app->openPage(u"c.md"_s, 3, true);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"c.md"_s);
        QCOMPARE(app->currentRoute().value(u"line"_s).toInt(), 3);
    }

    void journalsAndRecentPages()
    {
        auto app = openApp(u"journals"_s);
        app->openToday();
        QCOMPARE(app->currentView(), u"journal"_s);
        QCOMPARE(app->currentRoute().value(u"date"_s).toString(), u"2026-10-14"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        // Opening a day never creates its note (the editor does, with the first change).
        QVERIFY(!app->workspace()->exists(u"journals/2026-10-14.md"_s));
        QCOMPARE(app->today(-1), u"2026-10-13"_s);
        app->openJournal(u"bad"_s);
        QCOMPARE(app->currentRoute().value(u"date"_s).toString(), u"2026-10-14"_s);
        app->openJournal(u"2026-10-10"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"journals/2026-10-10.md"_s);
        QVERIFY(!app->workspace()->exists(u"journals/2026-10-10.md"_s));
        // A day with a note under another name opens that note.
        QVERIFY(app->workspace()->writeFile(u"journals/2026_10_09.md"_s, u"- z Logseqa\n"_s));
        app->openJournal(u"2026-10-09"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"journals/2026_10_09.md"_s);

        const QString p = app->workspace()->createPage(u"Notatka"_s);
        app->openPage(p);
        QCOMPARE(app->workspace()->recentPages().first().toMap().value(u"path"_s).toString(), p);
    }

    void followsRenamedAndDeletedPages()
    {
        auto app = openApp(u"rename"_s);
        Workspace *ws = app->workspace();
        const QString a = ws->createPage(u"Stara nazwa"_s);
        const QString b = ws->createPage(u"Inna"_s);
        app->openPage(b);
        app->openPage(a);
        app->openPage(a, -1, true); // also pinned in the side panel
        const QString renamed = ws->renamePage(a, u"Nowa nazwa"_s);
        QCOMPARE(renamed, u"Nowa nazwa.md"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), renamed);
        QCOMPARE(app->sidePanelRoute().value(u"path"_s).toString(), renamed);

        QVERIFY(!ws->deletePage(renamed).isEmpty());
        // The deleted page left the history: back at the previous page.
        QCOMPARE(app->currentRoute(), page(b));
        QVERIFY(app->sidePanelRoute().isEmpty());
        QVERIFY(app->back());
        QVERIFY(app->atHome());
        QVERIFY(!app->canGoBack());
    }

    void notificationBeforeReady()
    {
        const QString vault = m_tmp.filePath(u"notify"_s);
        QDir().mkpath(vault);
        QFile f(vault + u"/Przypomnienia.md"_s);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("- Zadzwonić ⏰ 2026-10-15 09:00\n");
        f.close();
        auto app = makeApp({ u"--vault"_s, vault });
        QSignalSpy show(app.get(), &AppController::showWindowRequested);
        app->openNotification(u"Przypomnienia.md"_s, 0);
        QCOMPARE(show.size(), 1);
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Przypomnienia.md"_s);
        QCOMPARE(app->currentRoute().value(u"line"_s).toInt(), 0);
        // A note that disappeared: a warning instead of an empty page.
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->openNotification(u"Nie ma.md"_s, 2);
        QCOMPARE(toasts.size(), 1);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Przypomnienia.md"_s);
    }

    void demoVaultOpensTheGuide()
    {
        auto app = makeApp({ u"--demo"_s });
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(app->workspace()->rootPath(), app->demoVaultPath());
        QCOMPARE(app->currentRoute(), page(u"Witaj w Brulionie.md"_s));
        QVERIFY(app->workspace()->exists(u"journals/2026-10-14.md"_s));
        QVERIFY(app->workspace()->exists(u"Projekty/Remont mieszkania.md"_s));
    }

    void closeVault()
    {
        auto app = openApp(u"close"_s);
        app->navigate(view(u"tasks"_s));
        app->closeVault();
        QCOMPARE(app->status(), u"onboarding"_s);
        QTRY_VERIFY(!app->workspace()->isOpen());
        QVERIFY(app->settings()->currentVault().isEmpty());
        QVERIFY(app->atHome());
        QVERIFY(!app->canGoBack());
    }

    void closeVaultLetsEditorsSaveFirst()
    {
        // The shell (and the editors in it) is unloaded when the status changes; QML deletes it
        // later. An editor saves its last keystrokes when it is destroyed — into the vault that
        // must still be open then.
        auto app = openApp(u"close-save"_s);
        Workspace *ws = app->workspace();
        const QString root = ws->rootPath();
        auto *editor = new QObject;
        QObject::connect(editor, &QObject::destroyed, ws,
                         [ws] { ws->write(u"Niezapisane.md"_s, u"- ostatnie słowa, zażółć\n"_s); });
        QObject::connect(app.get(), &AppController::statusChanged, editor, [&app, editor] {
            if (app->status() != u"ready")
                editor->deleteLater();
        });
        // As from a click (the "Open another…" button lives in the shell being unloaded).
        QTimer::singleShot(0, app.get(), [&app] { app->closeVault(); });
        QTRY_COMPARE(app->status(), u"onboarding"_s);
        QTRY_VERIFY(!ws->isOpen());
        QFile saved(root + u"/Niezapisane.md"_s);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(saved.readAll()), u"- ostatnie słowa, zażółć\n"_s);

        // Opening another vault right away still ends with that vault open.
        app->openVault(m_tmp.filePath(u"close-save-2"_s));
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTest::qWait(10);
        QVERIFY(ws->isOpen());
        QCOMPARE(ws->name(), u"close-save-2"_s);
    }

    void closeThenOpenImmediately()
    {
        auto app = openApp(u"close-open"_s);
        app->closeVault();
        app->openVault(m_tmp.filePath(u"close-open-2"_s));
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTest::qWait(10);
        QVERIFY(app->workspace()->isOpen());
        QCOMPARE(app->workspace()->name(), u"close-open-2"_s);
    }

    void retryOpensTheFailedVault()
    {
        // "Try again" on the error screen retries the notebook that failed, not the last good one.
        auto app = openApp(u"good"_s);
        const QString good = app->workspace()->rootPath();
        const QString path = m_tmp.filePath(u"na razie plik"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        app->closeVault();
        app->openVault(path);
        QTRY_COMPARE(app->status(), u"error"_s);
        QCOMPARE(app->failedVaultPath(), QDir::cleanPath(path));
        QVERIFY(QFile::remove(path)); // the user fixed it (e.g. mounted the card)
        app->retryOpenVault();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(app->workspace()->rootPath(), QDir(path).absolutePath());
        QVERIFY(app->workspace()->rootPath() != good);
        QVERIFY(app->failedVaultPath().isEmpty());
    }

    void startRouteIsSetBeforeReady()
    {
        // The shell is built when the status becomes "ready": it must see the first route then,
        // not build the home view and replace it a moment later.
        auto app = makeApp({ u"--demo"_s });
        QVariantMap routeWhenReady;
        QObject::connect(app.get(), &AppController::statusChanged, app.get(), [&] {
            if (app->status() == u"ready")
                routeWhenReady = app->currentRoute();
        });
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(routeWhenReady, page(u"Witaj w Brulionie.md"_s));
    }

    void testNotificationHandler()
    {
        auto app = makeApp();
        // The reminders integration delivers it through the platform's notifier; without a
        // notification service the desktop shows it in the tray, and without a tray it says
        // why nothing appears.
        QSignalSpy shown(app.get(), &AppController::notificationRequested);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->sendTestNotification();
        QCOMPARE(shown.size(), 0);
        QCOMPARE(toasts.size(), 1);
        QCOMPARE(toasts.first().at(1).toString(), u"warning"_s);
        app->setTrayAvailable(true);
        app->sendTestNotification();
        QTRY_COMPARE(shown.size(), 1);
        QCOMPARE(shown.first().at(1).toString(), u"Reminders work. This is how they will look."_s);
        // With one, the integration delivers it (on Android it must add it to the armed set,
        // which a separate scheduleReminders() call would replace).
        QStringList titles;
        app->setTestNotificationHandler([&](const QString &title, const QString &body) {
            QVERIFY(!body.isEmpty());
            titles.append(title);
        });
        app->sendTestNotification();
        QCOMPARE(titles.size(), 1);
        QCOMPARE(shown.size(), 1);
    }

    void captureAndSync()
    {
        auto app = openApp(u"capture"_s);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->quickCapture();
        QCOMPARE(ui.size(), 1);
        QCOMPARE(ui.first().first().toString(), u"capture"_s);
        QVERIFY(app->capture(u"Kupić mleko"_s));
        QVERIFY(app->workspace()->readFile(u"journals/2026-10-14.md"_s).contains(u"- Kupić mleko\n"_s));
        QVERIFY(!app->capture(u"   "_s));

        QSignalSpy sync(app.get(), &AppController::syncRequested);
        app->requestSync(); // nothing configured
        QCOMPARE(sync.size(), 0);
        app->setSyncAvailable(true);
        app->requestSync();
        QCOMPARE(sync.size(), 1);
    }

    void launcherActionsWaitForTheVault()
    {
        // A second launch with --capture / --today while the notebook is still opening: both
        // happen once it is ready instead of being lost ("open a notebook first").
        const QString vault = m_tmp.filePath(u"wait-ready"_s);
        QDir().mkpath(vault);
        auto app = makeApp({ u"--vault"_s, vault });
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->start();
        QCOMPARE(app->status(), u"loading"_s);
        app->quickCapture();
        app->openToday();
        QCOMPARE(ui.size(), 0);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(app->currentView(), u"journal"_s);
        QCOMPARE(app->currentRoute().value(u"date"_s).toString(), u"2026-10-14"_s);
        QTRY_COMPARE(ui.size(), 1);
        QCOMPARE(ui.first().first().toString(), u"capture"_s);
    }

    void quitAndTray()
    {
        auto app = makeApp({ u"--hidden"_s });
        QVERIFY(!app->startHidden()); // no tray: never start invisible
        app->setTrayAvailable(true);
        QVERIFY(app->startHidden());
        // Closing into the tray explains itself once (the hint survives restarts).
        QSignalSpy hint(app.get(), &AppController::notificationRequested);
        app->hiddenToTray();
        app->hiddenToTray();
        QCOMPARE(hint.size(), 1);
        QSignalSpy quit(app.get(), &AppController::quitRequested);
        app->quit();
        QVERIFY(app->quitting());
        QCOMPARE(quit.size(), 1);
    }

    // ---- commands ---------------------------------------------------------------------------

    void documentationNotebook()
    {
        // "Dokumentacja Brulionu": opened from the switcher / Ctrl+Shift+B, seeded on first
        // use, starts on its table of contents, and the toggle goes back to the user's notebook.
        auto app = openApp(u"moje notatki"_s);
        const QString mine = QDir::cleanPath(m_tmp.filePath(u"moje notatki"_s));
        const QString docs = app->docsVaultPath();
        QVERIFY(docs.endsWith(u"/Dokumentacja Brulionu"_s));
        QVERIFY(!app->inDocsVault());

        CommandModel *model = app->commands();
        QVERIFY(model->ids().contains(u"vault.docs"_s));
        const auto title = [&] {
            for (const QVariant &v : model->commands()) {
                if (v.toMap().value(u"id"_s).toString() == u"vault.docs")
                    return v.toMap().value(u"title"_s).toString();
            }
            return QString();
        };
        const QString toDocs = title();
        QVERIFY(!toDocs.isEmpty());

        QVERIFY(model->run(u"vault.docs"_s));
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(docs).absolutePath());
        QVERIFY(app->inDocsVault());
        QCOMPARE(app->currentView(), u"page"_s);
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), u"Spis treści.md"_s);
        QVERIFY(app->workspace()->exists(u"Zadania.md"_s));
        QVERIFY(app->workspace()->exists(u"Format plików.md"_s));
        QVERIFY(title() != toDocs); // "Back to my notebook"
        QCOMPARE(app->settings()->recentVaults().size(), 2);

        // Back: the notebook used before, on its usual home route.
        app->toggleDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(mine).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!app->inDocsVault());
        QVERIFY(!app->workspace()->exists(u"Spis treści.md"_s));
        QCOMPARE(title(), toDocs);

        // The second visit does not seed again over the user's edits.
        app->toggleDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(docs).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->workspace()->exists(u"Spis treści.md"_s));
    }

    void documentationNotebookWithoutOtherNotebook()
    {
        // Started in the documentation with no other notebook known: "back" shows the start screen.
        auto app = makeApp();
        app->start();
        app->openDocsVault();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->inDocsVault());
        app->toggleDocsVault();
        QCOMPARE(app->status(), u"onboarding"_s);
        QVERIFY(!app->inDocsVault());
    }

    void commands()
    {
        auto app = openApp(u"commands"_s);
        CommandModel *model = app->commands();
        const QStringList ids = model->ids();
        QCOMPARE(QSet<QString>(ids.cbegin(), ids.cend()).size(), ids.size()); // unique
        for (const QVariant &v : model->commands()) {
            const QVariantMap c = v.toMap();
            QVERIFY(!c.value(u"title"_s).toString().isEmpty());
            QVERIFY(!c.value(u"icon"_s).toString().isEmpty());
        }
        QVERIFY(model->run(u"go.tasks"_s));
        QCOMPARE(app->currentView(), u"tasks"_s);
        QVERIFY(model->run(u"nav.back"_s));
        QVERIFY(app->atHome());
        QVERIFY(!model->run(u"page.history"_s)); // not on a page
        QSignalSpy sourceUi(app.get(), &AppController::uiActionRequested);
        QVERIFY(model->run(u"page.source"_s)); // today's journal is shown in the editor at home
        QCOMPARE(sourceUi.size(), 1);
        QCOMPARE(sourceUi.first().first().toString(), u"toggleSourceMode"_s);
        // The note mode of the page in the editor (document / outline): the editor switches it.
        QVERIFY(model->run(u"page.mode"_s));
        QCOMPARE(sourceUi.size(), 2);
        QCOMPARE(sourceUi.last().first().toString(), u"toggleNoteMode"_s);
        QVERIFY(model->run(u"go.tasks"_s));
        QVERIFY(!model->run(u"page.source"_s)); // no editor there
        QVERIFY(!model->run(u"page.mode"_s));
        QVERIFY(model->run(u"nav.back"_s));
        QVERIFY(!model->run(u"sync.now"_s));     // no sync configured
        QVERIFY(!model->run(u"no.such"_s));
        QVERIFY(model->run(u"journal.yesterday"_s));
        QCOMPARE(app->currentRoute().value(u"date"_s).toString(), u"2026-10-13"_s);

        const QString p = app->workspace()->createPage(u"Strona"_s);
        app->openPage(p);
        QVERIFY(model->run(u"page.favorite"_s));
        QVERIFY(app->workspace()->isFavorite(p));
        QVERIFY(model->run(u"page.history"_s));
        QCOMPARE(app->currentRoute(), (QVariantMap{ { u"view"_s, u"history"_s }, { u"path"_s, p } }));

        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        QVERIFY(model->run(u"page.new"_s));
        QVERIFY(model->run(u"theme.toggle"_s));
        QCOMPARE(ui.size(), 2);
        QCOMPARE(ui.at(1).first().toString(), u"toggleTheme"_s);

        QSignalSpy changed(model, &CommandModel::commandsChanged);
        QSignalSpy language(app.get(), &AppController::languageChanged);
        const QString before = app->settings()->language();
        QVERIFY(model->run(u"language.switch"_s));
        QVERIFY(app->settings()->language() != before);
        QCOMPARE(language.size(), 1);
        QVERIFY(changed.size() >= 1);
    }
};

QTEST_GUILESS_MAIN(TestNavigation)
#include "tst_navigation.moc"
