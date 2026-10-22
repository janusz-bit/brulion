// Navigation of the shell: routes (normalisation, equality), the back/forward history and
// AppController's navigation (side panel, Android back, journals, notifications, renamed and
// deleted pages), its vault lifecycle and the command palette's commands.
#include "appcontroller.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "demovault.h"
#include "navigation.h"
#include "olddocs.h"
#include "workspace.h"

#include "reminders/notifications.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

/// The platform's notifier as Android has it: the armed set rings while the app is closed.
class FakeSink : public brulion::NotificationSink {
public:
    QList<QList<brulion::ScheduledNotification>> scheduled;
    bool firesWhenClosed() const override { return true; }
    void schedule(const QList<brulion::ScheduledNotification> &list) override { scheduled.append(list); }
    void showNow(const brulion::ScheduledNotification &) override { }
    /// Titles of what is armed now.
    QStringList armed() const
    {
        QStringList out;
        if (!scheduled.isEmpty()) {
            for (const brulion::ScheduledNotification &n : scheduled.last())
                out.append(n.title);
        }
        return out;
    }
};

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
        QCOMPARE(app->settings()->recentVaults().size(), 1); // the documentation is not a recent notebook
        // It is the user's copy: edits and deletions are theirs.
        QVERIFY(app->workspace()->write(u"Zadania.md"_s, u"- [ ] moja wersja zadań\n"_s));
        QVERIFY(!app->workspace()->deletePage(u"Obliczenia.md"_s).isEmpty());

        // Back: the notebook used before, on its usual home route.
        app->toggleDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(mine).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!app->inDocsVault());
        QVERIFY(!app->workspace()->exists(u"Spis treści.md"_s));
        QCOMPARE(title(), toDocs);

        // The second visit brings the documentation up to date, but never over the user's edits
        // and without bringing back what the user deleted.
        app->toggleDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(docs).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->workspace()->exists(u"Spis treści.md"_s));
        QCOMPARE(app->workspace()->readFile(u"Zadania.md"_s), u"- [ ] moja wersja zadań\n"_s);
        QVERIFY(!app->workspace()->exists(u"Obliczenia.md"_s));
    }

    void documentationNotebookFromAnOlderVersion()
    {
        // Opened in 0.3.0–0.3.2, before "Wzory matematyczne" existed (the user has kept only the
        // table of contents and changed "Zadania"): the new page arrives, the unchanged table of
        // contents is replaced by this version's, the changed page stays.
        auto app = openApp(u"aktualizacja"_s);
        const QString docs = app->docsVaultPath();
        QVERIFY(writeFile(QDir(docs).filePath(docsWelcomePage()), olddocs::kTableOfContents030.toUtf8()));
        QVERIFY(writeFile(QDir(docs).filePath(u"Zadania.md"_s), "- [ ] moje zadania\n"));
        app->openDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(docs).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->workspace()->exists(u"Wzory matematyczne.md"_s));
        QCOMPARE(app->currentRoute().value(u"path"_s).toString(), docsWelcomePage());
        for (const DemoFile &f : docsVaultFiles()) {
            if (f.path == docsWelcomePage())
                QCOMPARE(app->workspace()->readFile(f.path), f.content);
        }
        QCOMPARE(app->workspace()->readFile(u"Zadania.md"_s), u"- [ ] moje zadania\n"_s);
        QVERIFY(!app->workspace()->exists(u"Obliczenia.md"_s)); // deleted back then: stays deleted
    }

    void switchingNotebooksDoesNotCarrySyncOver()
    {
        // Syncing belongs to one notebook: switching to the documentation (or another notebook)
        // must not point the same server folder at it — notes would flow between notebooks.
        auto app = openApp(u"synced"_s);
        const QString mine = QDir::cleanPath(m_tmp.filePath(u"synced"_s));
        AppSettings *settings = app->settings();
        settings->setNextcloudUrl(u"https://chmura.example.pl"_s);
        settings->setNextcloudUser(u"Łucja"_s);
        QVERIFY(settings->setNextcloudPassword(u"hasło-aplikacji"_s));
        settings->setNextcloudFolder(u"Notatki"_s);
        settings->setNextcloudEnabled(true);
        QVERIFY(app->workspace()->sync()->active());

        app->openDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(app->docsVaultPath()).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!settings->nextcloudEnabled());
        QVERIFY(!app->workspace()->sync()->active());

        app->toggleDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(mine).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(settings->nextcloudEnabled());
        QCOMPARE(settings->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(app->workspace()->sync()->active());

        // Another notebook of the user starts without sync as well.
        app->openVault(m_tmp.filePath(u"other"_s));
        QTRY_COMPARE(app->workspace()->rootPath(), QDir(m_tmp.filePath(u"other"_s)).absolutePath());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!settings->nextcloudEnabled());
        QVERIFY(!app->workspace()->sync()->active());
    }

    void documentationNotebookWithoutOtherNotebook()
    {
        // Started in the documentation with no other notebook known: "back" shows the start screen.
        auto app = makeApp();
        app->start();
        app->openDocsVault();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->inDocsVault());
        // Quick capture has nowhere to go but the documentation: it says so and writes nothing.
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->quickCapture();
        QCOMPARE(toasts.size(), 1);
        QCOMPARE(ui.size(), 0);
        QVERIFY(app->inDocsVault());
        QCOMPARE(app->status(), u"ready"_s);
        app->toggleDocsVault();
        QCOMPARE(app->status(), u"onboarding"_s);
        QVERIFY(!app->inDocsVault());
    }

    void documentationKeepsTheArmedReminders()
    {
        // Reading the documentation must not disarm the user's alarms: it has no reminders of its
        // own, and its empty set would replace the armed one (on Android every alarm is cancelled).
        FakeSink sink;
        auto app = makeApp();
        Workspace *ws = app->workspace();
        ws->sync()->setSyncthingDiscovery(false);
        ws->reminders()->setSink(&sink);
        const QString mine = QDir(m_tmp.filePath(u"budzik"_s)).absolutePath();
        QVERIFY(writeFile(mine + u"/Plan.md"_s, "- Pobudka na pociąg ⏰! 2026-10-15 05:45\n"));
        app->start();
        app->openVault(mine);
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_VERIFY(ws->reminders()->isRunning());
        QCOMPARE(sink.armed(), QStringList{u"Pobudka na pociąg"_s});

        const qsizetype calls = sink.scheduled.size();
        app->toggleDocsVault(); // Ctrl+Shift+B
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QTRY_VERIFY(ws->ready());
        QVERIFY(!ws->reminders()->isRunning());
        QCOMPARE(sink.scheduled.size(), calls); // the armed set stays as it was

        app->toggleDocsVault();
        QTRY_COMPARE(ws->rootPath(), mine);
        QTRY_VERIFY(ws->reminders()->isRunning());
        QCOMPARE(sink.armed(), QStringList{u"Pobudka na pociąg"_s});

        // Another notebook of the user arms its own reminders instead (the guide says so).
        const QString other = QDir(m_tmp.filePath(u"budzik-inny"_s)).absolutePath();
        app->openVault(other);
        QTRY_COMPARE(ws->rootPath(), other);
        QTRY_VERIFY(ws->reminders()->isRunning());
        QVERIFY(sink.armed().isEmpty());
    }

    void restartLeavesTheDocumentation()
    {
        // Quit while reading the documentation: the next start opens the user's notebook, so quick
        // capture (the launcher, Ctrl+Shift+N) and the journal write there.
        auto app = openApp(u"po restarcie"_s);
        const QString mine = QDir(m_tmp.filePath(u"po restarcie"_s)).absolutePath();
        const QString docs = app->docsVaultPath();
        app->toggleDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        for (const QVariant &v : app->settings()->recentVaults())
            QVERIFY(v.toMap().value(u"path"_s).toString() != docs);
        AppController::Options o;
        o.settingsFile = app->settings()->fileName();
        o.dataDir = app->dataDir();
        app.reset();

        AppController again(o);
        again.setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 0)); });
        again.start();
        QTRY_COMPARE(again.status(), u"ready"_s);
        QVERIFY(!again.inDocsVault());
        QCOMPARE(again.workspace()->rootPath(), mine);
        QVERIFY(again.capture(u"Zadzwonić do mamy jutro o 18"_s));
        QVERIFY(QFile::exists(mine + u"/journals/2026-10-14.md"_s));
        QVERIFY(!QFile::exists(docs + u"/journals/2026-10-14.md"_s));
    }

    void startAfterOlderVersionLeftInTheDocumentation()
    {
        // 0.3.0–0.3.5 stored the documentation as the current and most recent notebook.
        auto app = makeApp();
        const QString mine = QDir(m_tmp.filePath(u"żółw"_s)).absolutePath();
        QVERIFY(QDir().mkpath(mine));
        app->settings()->addRecentVault(mine);
        app->settings()->addRecentVault(app->docsVaultPath());
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(app->workspace()->rootPath(), mine);
        QVERIFY(!app->inDocsVault());

        // Only the documentation known: the start screen, with nothing set to reopen it.
        auto alone = makeApp();
        alone->settings()->addRecentVault(alone->docsVaultPath());
        alone->start();
        QCOMPARE(alone->status(), u"onboarding"_s);
        QVERIFY(alone->settings()->currentVault().isEmpty());
    }

    void startForgetsTheDocumentationAmongRecents()
    {
        // 0.3.0–0.3.5 listed the documentation among the recent notebooks; the start screen and
        // the switcher offered it until it was opened again. Starting drops it, whichever notebook
        // opens, and opening the documentation never lists it again.
        const auto listed = [](AppController &a, const QString &path) {
            for (const QVariant &v : a.settings()->recentVaults()) {
                if (v.toMap().value(u"path"_s).toString() == path)
                    return true;
            }
            return false;
        };
        auto app = makeApp();
        const QString mine = QDir(m_tmp.filePath(u"zeszyt żółwia"_s)).absolutePath();
        const QString other = QDir(m_tmp.filePath(u"inny zeszyt"_s)).absolutePath();
        QVERIFY(QDir().mkpath(mine));
        QVERIFY(QDir().mkpath(other));
        app->settings()->addRecentVault(other);
        app->settings()->addRecentVault(app->docsVaultPath());
        app->settings()->addRecentVault(mine);
        QVERIFY(listed(*app, app->docsVaultPath()));
        app->start();
        QTRY_COMPARE(app->status(), u"ready"_s);
        QCOMPARE(app->workspace()->rootPath(), mine);
        QVERIFY(!listed(*app, app->docsVaultPath()));
        QVERIFY(listed(*app, mine));
        QVERIFY(listed(*app, other));

        app->toggleDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!listed(*app, app->docsVaultPath()));
        // Its folder opened by another spelling is the documentation too.
        app->toggleDocsVault();
        QTRY_COMPARE(app->workspace()->rootPath(), mine);
        app->openVault(app->docsVaultPath() + u"/"_s);
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!listed(*app, app->docsVaultPath()));
        QVERIFY(listed(*app, mine));

        // Only the documentation listed: the start screen, with an empty list.
        auto alone = makeApp();
        alone->settings()->addRecentVault(alone->docsVaultPath());
        alone->start();
        QCOMPARE(alone->status(), u"onboarding"_s);
        QVERIFY(alone->settings()->recentVaults().isEmpty());
    }

    void quickCaptureLeavesTheDocumentation()
    {
        // Ctrl+Shift+N, the "+" button or the launcher while reading the documentation: back to
        // the user's notebook, and the dialog opens there. Nothing is written into the docs.
        auto app = openApp(u"szybka notatka"_s);
        const QString mine = QDir(m_tmp.filePath(u"szybka notatka"_s)).absolutePath();
        app->toggleDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(!app->capture(u"Kupić mleko"_s));
        QVERIFY(!QFile::exists(app->docsVaultPath() + u"/journals/2026-10-14.md"_s));

        QSignalSpy ui(app.get(), &AppController::uiActionRequested);
        app->quickCapture();
        QTRY_COMPARE(app->workspace()->rootPath(), mine);
        QTRY_COMPARE(ui.size(), 1);
        QCOMPARE(ui.first().first().toString(), u"capture"_s);
        QVERIFY(!app->inDocsVault());
        QVERIFY(app->capture(u"Kupić mleko"_s));
        QVERIFY(QFile::exists(mine + u"/journals/2026-10-14.md"_s));
    }

    void backToAMissingNotebookCreatesNothing()
    {
        // The user's folder is deleted (moved, on an unplugged card) while the documentation is
        // open: "back" must not create an empty notebook there, as start() never does.
        auto app = openApp(u"USB/Notatki"_s);
        const QString mine = QDir(m_tmp.filePath(u"USB/Notatki"_s)).absolutePath();
        app->toggleDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(QDir(m_tmp.filePath(u"USB"_s)).removeRecursively());
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->toggleDocsVault(); // Ctrl+Shift+B
        QCOMPARE(app->status(), u"ready"_s);
        QTest::qWait(50);
        QCOMPARE(app->status(), u"ready"_s);
        QVERIFY(app->inDocsVault());
        QVERIFY(!QDir(mine).exists());
        QCOMPARE(toasts.size(), 1);
        QVERIFY(toasts.first().first().toString().contains(QDir::toNativeSeparators(mine)));
        QVERIFY(app->lastUserVault().isEmpty()); // dropped from the list

        // The same for the switcher in the sidebar and the start screen's list.
        auto other = makeApp();
        const QString gone = QDir(m_tmp.filePath(u"usunięty notatnik"_s)).absolutePath();
        const QString kept = QDir(m_tmp.filePath(u"istniejący notatnik"_s)).absolutePath();
        QVERIFY(QDir().mkpath(kept));
        other->settings()->addRecentVault(kept);
        other->settings()->addRecentVault(gone);
        QVERIFY(!other->openRecentVault(gone));
        QVERIFY(!QFileInfo::exists(gone));
        QCOMPARE(other->settings()->recentVaults().size(), 1);
        QCOMPARE(other->status(), u"boot"_s);
        QVERIFY(other->openRecentVault(kept));
        QTRY_COMPARE(other->status(), u"ready"_s);
        QCOMPARE(other->workspace()->rootPath(), kept);
    }

    void reminderTapLeavesTheDocumentation()
    {
        // Android rings the user's alarms while the documentation is open: tapping one opens its
        // note in the user's notebook instead of "the note no longer exists".
        auto app = openApp(u"dotknięcie"_s);
        const QString mine = QDir(m_tmp.filePath(u"dotknięcie"_s)).absolutePath();
        QVERIFY(app->workspace()->write(u"Plan.md"_s, u"- Pobudka ⏰! 2026-10-15 05:45\n"_s));
        app->toggleDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QSignalSpy toasts(app.get(), &AppController::toastRequested);
        app->openNotification(u"Plan.md"_s, 0);
        QTRY_COMPARE(app->workspace()->rootPath(), mine);
        QTRY_COMPARE(app->currentRoute().value(u"path"_s).toString(), u"Plan.md"_s);
        QCOMPARE(app->currentView(), u"page"_s);
        QCOMPARE(toasts.size(), 0);
    }

    void reminderTapOnAPageTheDocumentationAlsoHas()
    {
        // The user's own "Zadania.md" (the documentation has a page of that name too): a tap
        // while reading the documentation opens the user's page, never the documentation's.
        auto app = openApp(u"moje zadania"_s);
        const QString mine = QDir(m_tmp.filePath(u"moje zadania"_s)).absolutePath();
        const QString text = u"- [ ] Oddać książkę do biblioteki ⏰ 2026-10-15 09:00\n"_s;
        QVERIFY(app->workspace()->write(u"Zadania.md"_s, text));
        app->toggleDocsVault();
        QTRY_VERIFY(app->inDocsVault());
        QTRY_COMPARE(app->status(), u"ready"_s);
        QVERIFY(app->workspace()->exists(u"Zadania.md"_s)); // the documentation's page
        app->openNotification(u"Zadania.md"_s, 0);
        QTRY_COMPARE(app->workspace()->rootPath(), mine);
        QTRY_COMPARE(app->currentRoute().value(u"path"_s).toString(), u"Zadania.md"_s);
        QVERIFY(!app->inDocsVault());
        QCOMPARE(app->workspace()->readFile(u"Zadania.md"_s), text);

        // No notebook of the user to go back to: the documentation's page never opens instead.
        auto alone = makeApp();
        alone->start();
        alone->openDocsVault();
        QTRY_VERIFY(alone->inDocsVault());
        QTRY_COMPARE(alone->status(), u"ready"_s);
        QSignalSpy toasts(alone.get(), &AppController::toastRequested);
        const QVariantMap before = alone->currentRoute();
        alone->openNotification(u"Zadania.md"_s, 3);
        QCOMPARE(alone->currentRoute(), before);
        QCOMPARE(toasts.size(), 1);
        QVERIFY(alone->inDocsVault());
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
