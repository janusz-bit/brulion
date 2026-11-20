#include "onboardingselftest.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "recentnotebooks.h"
#include "uidriver.h"
#include "workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

class OnboardingSelfTest {
public:
    OnboardingSelfTest(QQmlApplicationEngine *engine, AppController *app) : m_engine(engine), m_app(app) { }

    int run()
    {
        m_window = window();
        m_touch = m_app->touch() || QCoreApplication::arguments().contains(u"--touch"_s);
        if (!m_window || !uidriver::waitFor([this] { return m_app->status() == u"onboarding"; }, 20000)) {
            check(false, "the app starts on the start screen (--onboarding)");
            return m_failures;
        }
        QObject::connect(m_app, &AppController::toastRequested, m_app,
                         [this](const QString &, const QString &kind) {
                             if (kind == u"warning")
                                 ++m_warnings;
                         });
        m_root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(u"selftest"_s);
        QDir(m_root).removeRecursively();
        recentNotebooks();
        layout();
        missingNotebook();
        removeFromList();
        manyNotebooks();
        primaryAction();
        return m_failures;
    }

private:
    QQmlApplicationEngine *m_engine;
    AppController *m_app;
    QQuickWindow *m_window = nullptr;
    bool m_touch = false;
    int m_failures = 0;
    int m_warnings = 0;
    QString m_root;
    QString m_missing;

    QQuickWindow *window() const
    {
        const QList<QObject *> roots = m_engine->rootObjects();
        return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
    }

    void check(bool ok, const char *what)
    {
        std::fprintf(stderr, "selftest: %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++m_failures;
    }

    QQuickItem *named(const QString &name) const { return uidriver::shownNamed(m_window->contentItem(), name); }

    /// The rows of recent notebooks shown (the phone hides those past five until "Show all").
    QList<QQuickItem *> rows() const
    {
        QList<QQuickItem *> out;
        const QList<QQuickItem *> items = uidriver::allItems(m_window->contentItem());
        for (QQuickItem *item : items) {
            if (item->objectName() == u"onboardingRecent" && item->isVisible())
                out.append(item);
        }
        return out;
    }
    QQuickItem *row(const QString &path) const
    {
        const QList<QQuickItem *> shown = rows();
        for (QQuickItem *r : shown) {
            if (r->property("path").toString() == path)
                return r;
        }
        return nullptr;
    }
    /// The item called `name` inside `parent` that is visible.
    static QQuickItem *inside(QQuickItem *parent, const QString &name)
    {
        return parent ? uidriver::shownNamed(parent, name) : nullptr;
    }

    QString folder(const QString &name) const
    {
        const QString path = QDir::cleanPath(QDir(m_root).filePath(name));
        QDir().mkpath(path);
        return path;
    }

    /// The item `find` gives once it stays put (the same item at the same point two looks 100 ms
    /// apart, a press at its centre reaching it): a row the list has just laid out takes a tap
    /// beside it otherwise.
    QQuickItem *steady(const std::function<QQuickItem *()> &find)
    {
        QPointer<QQuickItem> item;
        QPointF where(-1, -1);
        const auto reaches = [this](QQuickItem *target) {
            for (QQuickItem *p = uidriver::pressTargetAt(m_window, uidriver::pointIn(target)); p; p = p->parentItem()) {
                if (p == target)
                    return true;
            }
            return false;
        };
        const bool still = uidriver::waitFor(
                [&] {
                    QQuickItem *found = find();
                    const QPointF now = found ? uidriver::pointIn(found) : QPointF(-1, -1);
                    const bool same = found && found == item && now == where && reaches(found);
                    item = found;
                    where = now;
                    if (!same)
                        uidriver::settle(100);
                    return same;
                },
                5000);
        return still ? item.data() : nullptr;
    }

    void press(QQuickItem *item)
    {
        if (!item)
            return;
        // (The pointer away first: a tooltip shown from an earlier hover would take the press.)
        if (!m_touch)
            uidriver::mouseMoveNow(m_window, QPointF(2, 2));
        uidriver::tapOrClickNow(m_window, uidriver::pointIn(item), m_touch);
    }

    // 1. Two notebooks that exist, one on a drive that is not there, one of a vanished temporary
    //    folder (oldest first: the list shows the newest first).
    void recentNotebooks()
    {
        const QString vanished = QDir::cleanPath(
                QDir::tempPath() + u"/brulion-selftest-%1/scratchpad/vault"_s.arg(QCoreApplication::applicationPid()));
        m_missing = u"/brulion-selftest-missing-%1/PENDRIVE/Zeszyt na wyjazd"_s.arg(QCoreApplication::applicationPid());
        AppSettings *settings = m_app->settings();
        settings->addRecentVault(vanished);
        settings->addRecentVault(m_missing);
        settings->addRecentVault(folder(u"Praca/Notatki zespołu"_s));
        settings->addRecentVault(folder(u"Dokumenty/Brulion"_s));
        RecentNotebooks *recent = m_app->recentNotebooks();
        check(uidriver::waitFor([&] { return rows().size() == 3 && !recent->checking(); }, 10000),
              "three recent notebooks are listed (the vanished temporary one is dropped)");
        check(settings->recentVaults().size() == 3 && recent->indexOf(vanished) < 0,
              "the vanished temporary notebook leaves the list for good");
        const QList<QQuickItem *> shown = rows();
        check(shown.size() == 3 && shown.at(0)->property("name").toString() == u"Brulion"_s
                      && shown.at(1)->property("name").toString() == u"Notatki zespołu"_s,
              "the most recent notebook comes first");
        QQuickItem *missing = row(m_missing);
        QQuickItem *reason = inside(missing, u"onboardingRecentReason"_s);
        const QString notFound = QCoreApplication::translate("OnboardingView", "Folder not found");
        check(missing && missing->property("availability").toString() == u"missing"_s && reason
                      && uidriver::labelOf(reason) == notFound,
              "the notebook on a missing drive is marked “Folder not found”");
        QQuickItem *remove = inside(missing, m_touch ? u"onboardingRecentForget"_s : u"onboardingRecentRemove"_s);
        check(remove && uidriver::shown(remove), "the missing notebook offers “Remove from list”");
        QQuickItem *available = shown.isEmpty() ? nullptr : shown.first();
        QQuickItem *when = inside(available, u"onboardingRecentWhen"_s);
        check(when && !uidriver::labelOf(when).isEmpty() && !inside(available, u"onboardingRecentReason"_s),
              "an available notebook says when it was last opened");
    }

    // 2. The layout.
    void layout()
    {
        QQuickItem *recents = named(u"onboardingRecents"_s);
        QQuickItem *start = named(u"onboardingStart"_s);
        QQuickItem *page = named(u"onboardingPage"_s);
        QQuickItem *flick = page ? uidriver::enclosingFlickable(page) : nullptr;
        if (!recents || !start || !flick) {
            check(false, "the start screen has its recent notebooks, start card and page");
            return;
        }
        const QPointF r = recents->mapToScene(QPointF(0, 0));
        const QPointF s = start->mapToScene(QPointF(0, 0));
        if (m_window->width() >= 900) {
            check(r.x() + recents->width() <= s.x() && qAbs(r.y() - s.y()) < 2,
                  "wide: the recent notebooks left of the start card, side by side");
            check(flick->property("contentHeight").toReal() <= flick->height() + 1,
                  "wide: the start screen fits the window, nothing to scroll");
        } else {
            check(r.y() + recents->height() <= s.y() && qAbs(r.x() - s.x()) < 2,
                  "narrow: one column, the recent notebooks above the start card");
        }
        QQuickItem *open = named(u"onboardingOpen"_s);
        check(open && open->property("variant").toString() == u"primary"_s && open->isEnabled()
                      && uidriver::labelOf(open) == QCoreApplication::translate("OnboardingView", "Open notebook"),
              "“Open notebook” is the one primary action");
        int primaries = 0;
        const QList<QQuickItem *> items = uidriver::allItems(page);
        for (QQuickItem *item : items) {
            if (uidriver::isA(item, "BButton") && item->isVisible() && item->property("variant").toString() == u"primary")
                ++primaries;
        }
        check(primaries == 1, "no other primary button competes with it");
        for (const char *name : {"onboardingImport", "onboardingGitHub", "onboardingSample"})
            check(named(QString::fromLatin1(name)) != nullptr, "the other ways to start are listed");
        // (A first run's "what Brulion is" panel makes way for the recent notebooks.)
        check(!named(u"onboardingAbout"_s), "a returning user sees no introduction");
    }

    // 3. The missing notebook clicked: a warning; nothing created; still listed, marked.
    void missingNotebook()
    {
        const int warnings = m_warnings;
        // On the row's name (not on its buttons).
        QQuickItem *target = steady([this] { return row(m_missing); });
        if (!target) {
            check(false, "the missing notebook's row stays put");
            return;
        }
        if (!m_touch)
            uidriver::mouseMoveNow(m_window, QPointF(2, 2));
        uidriver::tapOrClickNow(m_window, uidriver::pointIn(target, 0.3, 0.5), m_touch);
        check(uidriver::waitFor([&] { return m_warnings > warnings; }), "opening a missing notebook warns");
        uidriver::settle(200);
        check(m_app->status() == u"onboarding"_s && !QFileInfo::exists(m_missing),
              "nothing is created where the drive was; the start screen stays");
        check(row(m_missing) && m_app->recentNotebooks()->availability(m_missing) == u"missing"_s,
              "the missing notebook stays on the list, marked");
    }

    // 4. "Remove from list".
    void removeFromList()
    {
        const QString name = m_touch ? u"onboardingRecentForget"_s : u"onboardingRecentRemove"_s;
        QQuickItem *remove = steady([&] { return inside(row(m_missing), name); });
        press(remove);
        check(remove && uidriver::waitFor([this] { return rows().size() == 2 && !row(m_missing); }),
              "“Remove from list” takes the missing notebook off the list");
        check(m_app->settings()->recentVaults().size() == 2 && m_app->recentNotebooks()->indexOf(m_missing) < 0,
              "… and out of the settings, the others kept");
    }

    // 5. Seven notebooks.
    void manyNotebooks()
    {
        for (const QString &name : {u"Studia/Semestr zimowy"_s, u"Syncthing/Przepisy"_s, u"Projekty/Remont"_s,
                                    u"Syncthing/Ogród"_s, u"Archiwum/Brulion 2025"_s})
            m_app->settings()->addRecentVault(folder(name));
        QQuickItem *showAll = nullptr;
        if (m_window->width() < 880) {
            check(uidriver::waitFor([&] {
                      showAll = named(u"onboardingRecentShowAll"_s);
                      return showAll && rows().size() == 5;
                  }),
                  "narrow: five recent notebooks and “Show all (7)”");
            check(showAll && uidriver::labelOf(showAll).contains(u"7"_s), "the button says how many there are");
            press(steady([this] { return named(u"onboardingRecentShowAll"_s); }));
            check(uidriver::waitFor([this] { return rows().size() == 7 && !named(u"onboardingRecentShowAll"_s); }),
                  "“Show all” shows the rest");
        } else {
            check(uidriver::waitFor([this] { return rows().size() == 7; }) && !named(u"onboardingRecentShowAll"_s),
                  "wide: every recent notebook is listed");
        }
    }

    // 6. The primary action with a new folder; back on the start screen it is first.
    void primaryAction()
    {
        const QString created = QDir::cleanPath(QDir(m_root).filePath(u"Nowy zeszyt żółwia"_s));
        QQuickItem *field = named(u"onboardingVaultPath"_s);
        if (!field) {
            check(false, "the folder field is there");
            return;
        }
        field->setProperty("text", created);
        QQuickItem *open = steady([this] { return named(u"onboardingOpen"_s); });
        if (open && !uidriver::onScreen(m_window, open))
            uidriver::scrollIntoView(m_window, open);
        open = steady([this] { return named(u"onboardingOpen"_s); });
        press(open);
        check(open && uidriver::waitFor([this] { return m_app->status() == u"ready"; }, 20000)
                      && QDir(m_app->workspace()->rootPath()) == QDir(created) && QFileInfo(created).isDir(),
              "“Open notebook” creates and opens the typed folder");
        // Closing it (as the switcher's "Open another notebook…" does, from inside an event).
        QTimer::singleShot(0, m_app, [app = m_app] { app->closeVault(); });
        check(uidriver::waitFor([this] { return m_app->status() == u"onboarding" && !rows().isEmpty(); }, 10000)
                      && rows().first()->property("path").toString() == created,
              "back on the start screen the new notebook is first");
        if (m_touch)
            return;
        QQuickItem *first = nullptr;
        check(uidriver::waitFor([&] {
                  const QList<QQuickItem *> shown = rows();
                  first = shown.isEmpty() ? nullptr : shown.first();
                  return first && first->hasActiveFocus();
              }),
              "the keyboard starts on the most recent notebook");
        uidriver::sendKey(m_window, Qt::Key_Down);
        QQuickItem *second = rows().value(1);
        check(second && uidriver::waitFor([&] { return second->hasActiveFocus(); }), "Down moves to the next notebook");
        const QString secondPath = second ? second->property("path").toString() : QString();
        const QString thirdPath = rows().value(2) ? rows().value(2)->property("path").toString() : QString();
        uidriver::sendKey(m_window, Qt::Key_Delete);
        check(uidriver::waitFor([&] { return !secondPath.isEmpty() && !row(secondPath); })
                      && m_app->recentNotebooks()->indexOf(secondPath) < 0,
              "Delete takes the focused notebook off the list");
        check(uidriver::waitFor([&] {
                  QQuickItem *next = row(thirdPath);
                  return next && next->hasActiveFocus();
              }),
              "… and the focus moves on to the next notebook");
        // The list is one tab stop: Tab leaves it for the folder field.
        uidriver::sendKey(m_window, Qt::Key_Tab);
        QQuickItem *pathField = named(u"onboardingVaultPath"_s);
        check(pathField && uidriver::waitFor([&] {
                  for (QQuickItem *p = m_window->activeFocusItem(); p; p = p->parentItem()) {
                      if (p == pathField)
                          return true;
                  }
                  return false;
              }),
              "Tab goes from the list straight to the folder field");
    }
};

} // namespace

void startOnboardingSelfTest(QQmlApplicationEngine *engine, AppController *app)
{
    QTimer::singleShot(0, app, [engine, app] {
        const int failures = OnboardingSelfTest(engine, app).run();
        std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::shell
