#include "ioselftest.h"

#include "appcontroller.h"
#include "workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <qtestsupport_gui.h>

#include <cstdio>
#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
// QtGui's entry point for synthetic mouse input (what QtTest's mouse functions call).
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);
QT_END_NAMESPACE

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

/// Sanitizer builds run many times slower: ctest passes BRULION_TEST_SLOWDOWN (tests/CMakeLists.txt).
int slowdown()
{
    static const int factor = qMax(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN"));
    return factor;
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > qint64(timeoutMs) * slowdown())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

/// Let input, bindings, transitions and queued calls run for `ms` (not scaled).
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

/// A tap with one finger at `scenePos`, as a touch screen sends it.
void touchTap(QQuickWindow *window, QPointF scenePos)
{
    // QTouchEventSequence's constructor is protected; QtTest's touchEvent() is a subclass too.
    struct Sequence : QTest::QTouchEventSequence {
        Sequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) { }
    };
    static QPointingDevice *device = QTest::createTouchDevice();
    Sequence sequence(window, device);
    sequence.press(0, scenePos.toPoint(), window).commit();
    settle(110);
    sequence.release(0, scenePos.toPoint(), window).commit();
    settle(150);
}

/// A left click at `scenePos`, as a real mouse sends it.
void mouseClick(QQuickWindow *window, QPointF scenePos)
{
    static int timestamp = 0;
    timestamp += 1000; // never a double click
    const QPointF global = window->mapToGlobal(scenePos);
    qt_handleMouseEvent(window, scenePos, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier,
                        timestamp);
    qt_handleMouseEvent(window, scenePos, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress,
                        Qt::NoModifier, timestamp += 20);
    qt_handleMouseEvent(window, scenePos, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease,
                        Qt::NoModifier, timestamp += 80);
    settle(150);
}

/// Shown: the item is visible (isVisible() includes its ancestors) and has a size.
bool shown(const QQuickItem *item)
{
    return item && item->isVisible() && item->width() > 0 && item->height() > 0;
}

/// Records a QML dialog's selectedFile when it becomes visible — inside open(), when a native
/// dialog (Android's "Save as") has just taken it — before anything that runs after open().
class OpenWatcher : public QObject {
    Q_OBJECT

public:
    explicit OpenWatcher(QObject *dialog) : m_dialog(dialog)
    {
        connect(dialog, SIGNAL(visibleChanged()), this, SLOT(visibleChanged()));
    }
    bool opened() const { return m_opened; }
    QUrl fileAtOpen() const { return m_fileAtOpen; }

private Q_SLOTS:
    void visibleChanged()
    {
        if (m_opened || !m_dialog->property("visible").toBool())
            return;
        m_opened = true;
        m_fileAtOpen = m_dialog->property("selectedFile").toUrl();
    }

private:
    QObject *m_dialog;
    bool m_opened = false;
    QUrl m_fileAtOpen;
};

/// Scrolls the nearest Flickable around `item` (if any) so that the item's middle is in its
/// viewport — away from the phone's bottom bar and the floating button where possible.
void ensureVisible(QQuickItem *item)
{
    for (QQuickItem *p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable"))
            continue;
        const qreal mid = item->mapToItem(p, QPointF(item->width() / 2, item->height() / 2)).y();
        if (mid >= p->height() * 0.15 && mid <= p->height() * 0.7)
            return;
        auto *content = p->property("contentItem").value<QQuickItem *>();
        if (!content)
            return;
        // ListViews start above 0 (their header): contentY runs from originY.
        const qreal originY = p->property("originY").toReal();
        const qreal y = item->mapToItem(content, QPointF(0, item->height() / 2)).y();
        const qreal maxY = originY + qMax<qreal>(0, p->property("contentHeight").toReal() - p->height());
        p->setProperty("contentY", qBound<qreal>(originY, y - p->height() / 2, maxY));
        settle(150);
        return;
    }
}

/// The QTemporaryDir of the run: alive until after the app closed the notebook in it.
QTemporaryDir &scratch()
{
    static QTemporaryDir dir;
    return dir;
}

class SelfTest {
public:
    SelfTest(QQmlApplicationEngine *engine, shell::AppController *app)
        : m_engine(engine), m_app(app), m_touch(QCoreApplication::arguments().contains(u"--touch"_s))
    { }

    int run()
    {
        if (!waitFor([this] { return m_app->status() == u"onboarding"; }, 20000)) {
            check(false, "the app starts on onboarding (--onboarding)");
            return m_failures;
        }
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        if (!m_window || !scratch().isValid()) {
            check(false, "the main window and a temporary folder exist");
            return m_failures;
        }
        m_window->requestActivate();
        waitFor([this] { return m_window->isActive(); }, 2000);
        settle(300);

        fromOnboarding();
        if (m_app->status() != u"ready")
            return m_failures;
        fromEmptyJournal();
        fromDrawer();
        fromMoreMenu();
        phoneSteps();
        importTypedPath();
        exportSaveDialog();
        return m_failures;
    }

private:
    void check(bool ok, const char *what)
    {
        std::fprintf(stderr, "selftest: %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++m_failures;
    }

    /// The shown item called `name` (a view left a moment ago may not be deleted yet), else the
    /// first one.
    QQuickItem *item(const char *name) const
    {
        const QList<QQuickItem *> found = m_window->findChildren<QQuickItem *>(QString::fromLatin1(name));
        for (QQuickItem *i : found) {
            if (shown(i))
                return i;
        }
        return found.value(0);
    }

    /// The controller of the import view on screen (a new one with every visit of the view).
    QObject *importController() const
    {
        const QList<QObject *> found = m_window->findChildren<QObject *>(u"importController"_s);
        for (QObject *c : found) {
            if (shown(qobject_cast<QQuickItem *>(c->parent())))
                return c;
        }
        return nullptr;
    }

    void tap(QQuickItem *target)
    {
        // The page may still be settling (a summary arriving, rows of tiles wrapping): aim once
        // the target stays put, else the tap lands where it was.
        QPointF at;
        for (int i = 0; i < 20; ++i) {
            ensureVisible(target);
            const QPointF now = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
            if (i > 0 && now == at)
                break;
            at = now;
            settle(100);
        }
        if (m_touch)
            touchTap(m_window, at);
        else
            mouseClick(m_window, at);
    }

    bool onImportTab() const
    {
        return m_app->currentView() == u"importExport" && m_app->currentRoute().value(u"tab"_s).toString() == u"import";
    }

    bool backHome()
    {
        m_app->goHome();
        const bool home = waitFor([this] { return m_app->currentView() == u"journals"; });
        settle(300);
        return home;
    }

    // 1a. Phones: the folder field low on the screen gets the focus with the on-screen keyboard
    //     up (a simulated one): it is scrolled above the keyboard (the window is never panned to
    //     it, docs/UI.md "On-screen keyboard").
    void keyboardOverField(QQuickItem *field)
    {
        auto *insets = m_engine->singletonInstance<QObject *>("Brulion", "KeyboardInsets");
        QQuickItem *flick = field->parentItem();
        while (flick && !flick->inherits("QQuickFlickable"))
            flick = flick->parentItem();
        if (!insets || !flick) {
            check(false, "onboarding: the keyboard and the folder field's Flickable exist");
            return;
        }
        const qreal keyboard = 336;
        const qreal top = m_window->height() - keyboard;
        const auto bottom = [field] { return field->mapToScene(QPointF(0, field->height())).y(); };
        // Low on the screen, where the keyboard comes (as on a phone in portrait).
        const qreal low = m_window->height() - 120;
        flick->setProperty("contentY", qMax<qreal>(0, flick->property("contentY").toReal() + bottom() - low));
        settle(100);
        const qreal before = bottom();
        insets->setProperty("simulated", keyboard);
        field->forceActiveFocus();
        check(before > top && waitFor([&] {
                  return field->hasActiveFocus() && bottom() <= top && bottom() > field->height();
              }),
              "onboarding: the folder field that gets the focus is scrolled above the keyboard");
        std::fprintf(stderr, "selftest: the folder field ends at %.0f, then %.0f (the keyboard's top %.0f)\n", before,
                     bottom(), top);
        m_window->contentItem()->forceActiveFocus();
        insets->setProperty("simulated", -1);
        settle(200);
    }

    // 1. Onboarding: a new notebook folder, "Import notes".
    void fromOnboarding()
    {
        QQuickItem *path = item("onboardingVaultPath");
        QQuickItem *button = item("onboardingImport");
        check(shown(path) && shown(button), "onboarding shows “Import notes” next to the notebook folder");
        if (!path || !button)
            return;
        if (m_touch)
            keyboardOverField(path);
        m_vault = scratch().filePath(u"Nowy notatnik"_s);
        path->setProperty("text", m_vault);
        settle(100);
        tap(button);
        check(waitFor([this] { return m_app->status() == u"ready"; }, 20000)
                      && waitFor([this] { return onImportTab(); }),
              "onboarding’s “Import notes” opens the new notebook on the import tab");
        check(QDir(m_vault).exists() && QDir(m_app->workspace()->rootPath()) == QDir(m_vault),
              "the notebook is the folder typed on onboarding (created)");
        settle(300);
    }

    // 2. The empty journal's button.
    void fromEmptyJournal()
    {
        if (!backHome()) {
            check(false, "home is the journal");
            return;
        }
        QQuickItem *hint = item("journalsEmptyHint");
        QQuickItem *button = nullptr;
        if (hint) {
            const QList<QQuickItem *> children = hint->findChildren<QQuickItem *>();
            for (QQuickItem *c : children) {
                if (c->inherits("QQuickAbstractButton") && shown(c)) {
                    button = c;
                    break;
                }
            }
        }
        check(shown(hint) && button, "the empty journal offers “Import notes”");
        if (!button)
            return;
        tap(button);
        check(waitFor([this] { return onImportTab(); }), "the empty journal’s “Import notes” opens the import tab");
    }

    // 3. Phones: the drawer.
    void fromDrawer()
    {
        if (m_window->width() >= 900)
            return; // the sidebar is no drawer there (the entry is phone-only)
        if (!backHome()) {
            check(false, "home is the journal");
            return;
        }
        QObject *drawer = m_window->findChild<QObject *>(u"shellDrawer"_s);
        if (!drawer) {
            check(false, "the drawer exists");
            return;
        }
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor([drawer] { return drawer->property("opened").toBool(); }), "the drawer opens");
        QQuickItem *entry = item("drawerImportNotes");
        check(shown(entry), "the drawer lists “Import notes”");
        if (!shown(entry))
            return;
        tap(entry);
        check(waitFor([this] { return onImportTab(); }), "the drawer’s “Import notes” opens the import tab");
        check(waitFor([drawer] { return !drawer->property("visible").toBool(); }), "the drawer closes");
    }

    // 4. The top bar's "More" menu.
    void fromMoreMenu()
    {
        if (!backHome()) {
            check(false, "home is the journal");
            return;
        }
        const QString moreLabel = QCoreApplication::translate("ShellTopBar", "More");
        QQuickItem *more = nullptr;
        const QList<QQuickItem *> items = m_window->findChildren<QQuickItem *>();
        for (QQuickItem *i : items) {
            if (i->property("label").toString() == moreLabel && shown(i)) {
                more = i;
                break;
            }
        }
        check(more != nullptr, "the top bar has “More”");
        if (!more)
            return;
        tap(more);
        QQuickItem *entry = item("menuImportNotes");
        check(waitFor([entry] { return shown(entry); }), "the “More” menu lists “Import notes”");
        if (!shown(entry))
            return;
        tap(entry);
        check(waitFor([this] { return onImportTab(); }), "the menu’s “Import notes” opens the import tab");
    }

    // 5. The chosen app's steps on a phone.
    void phoneSteps()
    {
        QObject *controller = nullptr;
        waitFor([&] { return (controller = importController()) != nullptr; });
        if (!controller) {
            check(false, "the import view has its controller");
            return;
        }
        controller->setProperty("importerId", u"obsidian"_s);
        settle(200);
        QQuickItem *steps = item("importPhoneHowTo");
        if (m_touch)
            check(shown(steps), "with Obsidian chosen the phone’s steps show");
        else
            check(steps && !steps->isVisible(), "the desktop shows no phone steps");
        controller->setProperty("importerId", QString());
        settle(100);
    }

    // 6. A vault typed as a path, the preview, the import, the report.
    void importTypedPath()
    {
        const QString source = scratch().filePath(u"Sejf Łucji"_s);
        const auto write = [](const QString &file, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
        };
        const bool written =
                write(source + u"/.obsidian/app.json"_s, "{}")
                && write(source + u"/Zażółć gęślą jaźń.md"_s,
                         QStringLiteral("# Zażółć\n\nLink do [[Łódź]].\n").toUtf8())
                         && write(source + u"/Miasta/Łódź.md"_s, QStringLiteral("- Piotrkowska\n").toUtf8())
                                  && write(source + u"/załączniki/zdjęcie.png"_s, QByteArray("\x89PNG\r\n\x1a\n", 8));
        QObject *controller = importController();
        QQuickItem *field = item("importPathField");
        QQuickItem *go = item("importPathButton");
        check(written && controller && shown(field) && go, "the import tab has the path field");
        if (!controller || !field || !go)
            return;
        field->setProperty("text", source);
        settle(100);
        tap(go);
        check(waitFor([controller] { return controller->property("state").toString() == u"ready"; }, 15000),
              "the typed path is read and checked");
        const QVariantMap preview = controller->property("preview").toMap();
        check(controller->property("effectiveId").toString() == u"obsidian" && preview.value(u"pages"_s).toInt() == 2
                      && preview.value(u"attachments"_s).toInt() == 1,
              "the preview counts the vault: Obsidian, 2 pages, 1 attachment");
        check(!m_app->workspace()->exists(u"Import/Obsidian/Zażółć gęślą jaźń.md"_s),
              "nothing is written before Import");
        QQuickItem *importButton = item("importStartButton");
        check(shown(importButton), "the preview offers “Import”");
        if (!shown(importButton))
            return;
        tap(importButton);
        check(waitFor([controller] { return controller->property("state").toString() == u"done"; }, 15000),
              "the import finishes");
        const QVariantMap report = controller->property("report").toMap();
        check(report.value(u"pages"_s).toInt() == 2 && report.value(u"failed"_s).toList().isEmpty(),
              "the report: 2 pages, nothing failed");
        check(m_app->workspace()->exists(u"Import/Obsidian/Zażółć gęślą jaźń.md"_s)
                      && m_app->workspace()->exists(u"Import/Obsidian/Miasta/Łódź.md"_s),
              "the pages are in the notebook");
        QQuickItem *openFolder = item("importOpenFolder");
        check(shown(openFolder), "the report offers “Open the imported folder”");
        if (!shown(openFolder))
            return;
        tap(openFolder);
        check(waitFor([this] {
                  return m_app->currentView() == u"allPages"
                         && m_app->currentRoute().value(u"path"_s).toString() == u"Import/Obsidian";
              }),
              "“Open the imported folder” shows Import/Obsidian in All pages");
    }

    // 7. The export's save dialog opens with the produced file's name.
    void exportSaveDialog()
    {
        m_app->navigate({{u"view"_s, u"importExport"_s}, {u"tab"_s, u"export"_s}});
        QObject *controller = nullptr;
        QObject *dialog = nullptr;
        QQuickItem *button = nullptr;
        waitFor([&] {
            controller = nullptr;
            for (QObject *c : m_window->findChildren<QObject *>(u"exportController"_s)) {
                if (shown(qobject_cast<QQuickItem *>(c->parent())))
                    controller = c;
            }
            dialog = controller ? controller->parent()->findChild<QObject *>(u"exportSaveDialog"_s) : nullptr;
            button = item("exportStartButton");
            return controller && dialog && shown(button) && button->isEnabled();
        });
        check(controller && dialog && shown(button) && button->isEnabled(), "the export tab offers “Export…”");
        if (!controller || !dialog || !button)
            return;
        OpenWatcher watcher(dialog);
        tap(button);
        const bool opened = waitFor([&watcher] { return watcher.opened(); }, 15000);
        check(opened, "“Export…” produces the file and opens the save dialog");
        if (!opened)
            return;
        const QString name = controller->property("fileName").toString();
        std::fprintf(stderr, "selftest: export file “%s”, the dialog opened with “%s”\n", qPrintable(name),
                     qPrintable(watcher.fileAtOpen().toString()));
        check(!name.isEmpty() && name.endsWith(u".zip"_s) && watcher.fileAtOpen().fileName() == name,
              "the save dialog opens with the export’s name");
        QMetaObject::invokeMethod(dialog, "reject");
        check(waitFor([dialog] { return !dialog->property("visible").toBool(); }), "the save dialog closes");
        QMetaObject::invokeMethod(controller, "discard");
        settle(200);
    }

    QQmlApplicationEngine *m_engine;
    shell::AppController *m_app;
    bool m_touch;
    QQuickWindow *m_window = nullptr;
    QString m_vault;
    int m_failures = 0;
};

} // namespace

void startImportSelfTest(QQmlApplicationEngine *engine, shell::AppController *app)
{
    QTimer::singleShot(0, app, [engine, app] {
        int failures = 0;
        {
            SelfTest test(engine, app);
            failures = test.run();
        }
        std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::ioviews

#include "ioselftest.moc"
