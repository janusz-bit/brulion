#include "shellselftest.h"

#include "appcontroller.h"
#include "workspace.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

/// Press and release `key` in the window. True when an item handled it (accepted the press or
/// the release) — an unhandled back key is what lets Android close the app.
bool sendKey(QQuickWindow *window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             const QString &text = QString())
{
    if (qEnvironmentVariableIsSet("BRULION_SELFTEST_DEBUG")) {
        const QQuickItem *focus = window->activeFocusItem();
        const QString name = focus ? QString::fromLatin1(focus->metaObject()->className()) + u' ' + focus->objectName()
                                   : u"none"_s;
        std::fprintf(stderr, "selftest: key %d, window active %d, focus on %s\n", key, window->isActive(),
                     qPrintable(name));
    }
    // Straight to the focused item (window Shortcut objects are not involved).
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    press.ignore();
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    release.ignore();
    QCoreApplication::sendEvent(window, &release);
    waitFor([] { return false; }, 50); // let bindings, transitions and queued calls settle
    return press.isAccepted() || release.isAccepted();
}

int runChecks(QQmlApplicationEngine *engine, AppController *app)
{
    int failures = 0;
    const auto check = [&failures](bool ok, const char *what) {
        std::fprintf(stderr, "selftest: %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++failures;
    };
    const auto view = [app] { return app->currentView(); };

    if (!waitFor([app] { return app->status() == u"ready"; }, 20000)) {
        check(false, "the notebook opens");
        return failures;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
    if (!window) {
        check(false, "the main window exists");
        return failures;
    }
    window->requestActivate();
    waitFor([window] { return window->isActive(); }, 2000);
    const auto find = [window](const char *name) { return window->findChild<QObject *>(QString::fromLatin1(name)); };
    QObject *drawer = find("shellDrawer");
    QObject *palette = find("shellPalette");
    QObject *capture = find("shellCapture");
    if (!drawer || !palette || !capture) {
        check(false, "the drawer, the command palette and quick capture exist");
        return failures;
    }
    const auto isOpen = [](QObject *popup) { return popup->property("visible").toBool(); };

    // 0. Started with --capture (a launcher action, or the tray while the window was hidden):
    //    quick capture opens by itself; Escape closes it and the keys reach the shell again.
    if (QCoreApplication::arguments().contains(u"--capture"_s)) {
        check(waitFor([&] { return capture->property("opened").toBool(); }), "--capture opens quick capture");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return !isOpen(capture); }), "Escape closes quick capture");
    }

    // 1. The back key walks the history and is handled; at home it is left to the system.
    app->navigate({ { u"view"_s, u"tasks"_s } });
    bool handled = sendKey(window, Qt::Key_Back);
    check(handled && view() == u"journals", "back goes back through the history");
    handled = sendKey(window, Qt::Key_Back);
    check(!handled && view() == u"journals", "back at home is not handled (Android may close the app)");

    // 2. + 3. The drawer (narrow windows; wide ones have the sidebar instead).
    if (window->width() < 900) {
        // Escape closes the drawer — and nothing else happens.
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor([&] { return drawer->property("opened").toBool(); }), "the drawer opens");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return !isOpen(drawer); }) && view() == u"journals",
              "Escape closes the drawer and stays on the view");

        // An open drawer owns the keys: back/Escape never act on the view underneath it.
        app->navigate({ { u"view"_s, u"tasks"_s } });
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor([&] { return drawer->property("opened").toBool(); }), "the drawer opens again");
        sendKey(window, Qt::Key_Back);
        check(view() == u"tasks", "back with the drawer open does not navigate underneath it");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return !isOpen(drawer); }) && view() == u"tasks", "Escape closes the drawer");
        handled = sendKey(window, Qt::Key_Back);
        check(handled && view() == u"journals", "after the drawer closed, back reaches the shell again");
    }

    // 4. The command palette: Escape closes it; the focus returns to the shell.
    app->navigate({ { u"view"_s, u"tasks"_s } });
    app->requestUiAction(u"palette"_s);
    check(waitFor([&] { return palette->property("opened").toBool(); }), "the command palette opens");
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return !isOpen(palette); }) && view() == u"tasks", "Escape closes the palette");
    handled = sendKey(window, Qt::Key_Back);
    check(handled && view() == u"journals", "after the palette closed, back reaches the shell again");

    // 5. The block editor in the shell: real keys reach the edited block and are saved, Ctrl+E
    //    and the "Markdown source" command switch source mode, a link to a missing page opens
    //    that page ready to type (and typing creates it).
    {
        const QString guide = u"Witaj w Brulionie.md"_s;
        const auto controllerFor = [window](const QString &path) -> QObject * {
            const QList<QObject *> found = window->findChildren<QObject *>(u"pageController"_s);
            for (QObject *c : found) {
                if (c->property("path").toString() == path)
                    return c;
            }
            return nullptr;
        };
        const auto editorOf = [window](QObject *controller) -> QQuickItem * {
            const QList<QQuickItem *> found = window->findChildren<QQuickItem *>(u"pageEditor"_s);
            for (QQuickItem *e : found) {
                if (e->property("controller").value<QObject *>() == controller)
                    return e;
            }
            return nullptr;
        };
        const auto source = [](QObject *c) { return c ? c->property("sourceText").toString() : QString(); };
        // The block's text editor has the keyboard (its delegate is created a frame later).
        const auto editorFocused = [window] {
            return waitFor([window] {
                const QQuickItem *f = window->activeFocusItem();
                return f && QByteArray(f->metaObject()->className()).contains("InlineEditor");
            });
        };
        const auto typeText = [window](const QString &text) {
            for (const QChar ch : text)
                sendKey(window, 0, Qt::NoModifier, QString(ch));
        };

        app->openPage(guide);
        QObject *page = nullptr;
        check(waitFor([&] {
                  page = controllerFor(guide);
                  return page && page->property("loaded").toBool();
              }),
              "the guide page opens in the editor");
        QQuickItem *editor = page ? editorOf(page) : nullptr;
        if (page && editor) {
            const QStringList lines = app->workspace()->readFile(guide).split(u'\n');
            const qsizetype line = lines.indexOf(u"- Każdy punkt listy to **blok**. Kliknij go, żeby edytować."_s);
            int row = -1;
            QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(row), int(line));
            check(line >= 0 && row >= 0, "the guide has its first outline block");
            QMetaObject::invokeMethod(editor, "startEditing", QVariant(row), QVariant(-1));
            check(waitFor([&] { return page->property("editingRow").toInt() == row; }) && editorFocused(),
                  "a block starts editing and has the keyboard");
            typeText(u" Żółw ąę"_s);
            check(source(page).contains(u"żeby edytować. Żółw ąę"_s), "typed keys reach the block (Polish letters)");
            // Ctrl+K is the command palette, but Qt binds it to "delete to the end of the line" in
            // text editors on Linux: the edited block must leave it to the shell's shortcut (the
            // shortcut map asks the focused item with a ShortcutOverride event first).
            if (QQuickItem *focused = window->activeFocusItem()) {
                QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_K, Qt::ControlModifier);
                override.ignore();
                QCoreApplication::sendEvent(focused, &override);
                check(!override.isAccepted(), "a block being edited leaves Ctrl+K to the command palette");
            }
            sendKey(window, Qt::Key_E, Qt::ControlModifier);
            check(page->property("sourceMode").toBool(), "Ctrl+E in a block switches to Markdown source");
            app->requestUiAction(u"toggleSourceMode"_s);
            check(waitFor([&] { return !page->property("sourceMode").toBool(); }),
                  "the \"Markdown source\" command switches back");
            bool saved = false;
            QMetaObject::invokeMethod(page, "save", qReturnArg(saved));
            check(saved && app->workspace()->readFile(guide).contains(u"żeby edytować. Żółw ąę"_s), "the edit is saved to the file");

            // Phones: a block at the bottom of the screen that starts editing is scrolled above
            // the editing toolbar (which sits on the keyboard) instead of staying under it.
            if (window->width() < 900) {
                const qsizetype lowLine = lines.indexOf(u"- Ctrl+. zwija i rozwija dzieci bloku."_s);
                int low = -1;
                QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(low), int(lowLine));
                auto *list = editor->property("listView").value<QQuickItem *>();
                if (list && low >= 0) {
                    QMetaObject::invokeMethod(page, "stopEditing");
                    QMetaObject::invokeMethod(list, "positionViewAtIndex", low, 2 /* ListView.End */);
                    waitFor([] { return false; }, 100);
                    QMetaObject::invokeMethod(editor, "startEditing", QVariant(low), QVariant(-1));
                    const auto caretBottom = [editor]() -> qreal {
                        auto *edit = editor->property("activeEditor").value<QQuickItem *>();
                        if (!edit)
                            return 1e9;
                        const QRectF r = edit->property("cursorRectangle").toRectF();
                        return edit->mapToItem(editor, r.bottomLeft()).y();
                    };
                    check(editorFocused() && waitFor([&] { return caretBottom() <= editor->property("toolbarTop").toReal() + 0.5; }),
                          "a block edited at the bottom of a phone screen is not hidden under the toolbar");
                    QMetaObject::invokeMethod(page, "stopEditing");
                } else {
                    check(false, "the editor's list and the low block exist");
                }
            }

            // [[Pomysły na prezenty]] on the guide: the page does not exist yet.
            const QString missing = u"Pomysły na prezenty.md"_s;
            QVariantMap result;
            QMetaObject::invokeMethod(page, "activateLink", qReturnArg(result), row, u"page:Pomys%C5%82y%20na%20prezenty"_s, false);
            QObject *newPage = nullptr;
            check(waitFor([&] {
                      newPage = controllerFor(missing);
                      return app->currentRoute().value(u"path"_s).toString() == missing && newPage
                             && newPage->property("editingRow").toInt() >= 0;
                  }),
                  "a link to a missing page opens it with a block to type into");
            check(editorFocused(), "the new page's block has the keyboard");
            check(!app->workspace()->exists(missing), "opening a missing page does not create a file");
            if (newPage) {
                typeText(u"Rower dla Oli"_s);
                QMetaObject::invokeMethod(newPage, "save", qReturnArg(saved));
                check(app->workspace()->readFile(missing).contains(u"Rower dla Oli"_s), "typing creates the new page");

                // Renamed with unsaved typing (F2 right after typing): the editor follows the
                // file, the old name never comes back as a duplicate and nothing typed is lost.
                typeText(u" i Kuby"_s);
                const QString renamed = app->workspace()->renamePage(missing, u"Prezenty dla dzieci"_s);
                check(renamed == u"Prezenty dla dzieci.md"_s, "the open page is renamed");
                check(waitFor([&] {
                          return newPage->property("path").toString() == renamed
                                 && app->currentRoute().value(u"path"_s).toString() == renamed;
                      }),
                      "the editor and the route follow the rename");
                QMetaObject::invokeMethod(newPage, "save", qReturnArg(saved));
                waitFor([] { return false; }, 100);
                check(!app->workspace()->exists(missing), "the old name does not come back");
                check(app->workspace()->readFile(renamed).contains(u"Rower dla Oli i Kuby"_s),
                      "the text typed before the rename is saved under the new name");
            }
        } else {
            check(false, "the page view has an editor");
        }
        app->goHome();
    }

    // 6. Closing the notebook from the UI (as a click would: from inside an event).
    QTimer::singleShot(0, app, [app] { app->closeVault(); });
    check(waitFor([app] { return app->status() == u"onboarding" && !app->workspace()->isOpen(); }),
          "closing the notebook shows onboarding and closes the vault");
    return failures;
}

} // namespace

void startShellSelfTest(QQmlApplicationEngine *engine, AppController *app)
{
    QTimer::singleShot(0, app, [engine, app] {
        const int failures = runChecks(engine, app);
        std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::shell
