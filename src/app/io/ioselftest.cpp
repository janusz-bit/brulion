#include "ioselftest.h"

#include "appcontroller.h"
#include "exportcontroller.h"
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

#include <algorithm>
#include <cstdio>
#include <functional>

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
    static const int factor = std::max(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN"));
    return factor;
}

/// Run the event loop until `condition` holds or `timeoutMs` (scaled) passed; a stall of the thread
/// counts as 250 ms of it, as in uidriver::waitFor().
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    const qint64 maxRound = qint64(250) * slowdown();
    qint64 waited = 0;
    QElapsedTimer round;
    round.start();
    while (!condition()) {
        waited += std::min(round.restart(), maxRound);
        if (waited > qint64(timeoutMs) * slowdown())
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
    struct Sequence final : QTest::QTouchEventSequence {
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

/// A view's object (a controller, a dialog) whose view is on screen.
bool parentShown(const QObject *object)
{
    return shown(qobject_cast<QQuickItem *>(object->parent()));
}

/// The first of `objects` that `accept` takes, or nullptr.
template<typename T, typename Pred>
T *firstOf(const QList<T *> &objects, Pred accept)
{
    const auto it = std::ranges::find_if(objects, accept);
    return it != objects.cend() ? *it : nullptr;
}

/// The last of `objects` that `accept` takes, or nullptr.
template<typename T, typename Pred>
T *lastOf(const QList<T *> &objects, Pred accept)
{
    const auto it = std::ranges::find_if(objects.crbegin(), objects.crend(), accept);
    return it != objects.crend() ? *it : nullptr;
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
        const qreal maxY = originY + std::max<qreal>(0, p->property("contentHeight").toReal() - p->height());
        p->setProperty("contentY", std::clamp<qreal>(y - p->height() / 2, originY, maxY));
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
        csvMapping();
        exportSaveDialog();
        pageExport();
        publishDryRun();
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
        QQuickItem *visible = firstOf(found, shown);
        return visible ? visible : found.value(0);
    }

    /// The controller of the import view on screen (a new one with every visit of the view).
    QObject *importController() const
    {
        return firstOf(m_window->findChildren<QObject *>(u"importController"_s), parentShown);
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
        const auto bottom = [field] { return field->mapToScene(QPointF(0, field->height())).y(); };
        // Low on the screen, where the keyboard comes (as on a phone in portrait).
        const qreal low = m_window->height() - 120;
        flick->setProperty("contentY", std::max<qreal>(0, flick->property("contentY").toReal() + bottom() - low));
        settle(100);
        const qreal before = bottom();
        // A keyboard that covers it: 336 px, or taller where the start screen shows the field high
        // up (its first card) — as a bigger keyboard or a smaller phone would.
        const qreal keyboard = std::max<qreal>(336, m_window->height() - before + 40);
        const qreal top = m_window->height() - keyboard;
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
        const auto isButton = [](const QQuickItem *c) { return c->inherits("QQuickAbstractButton") && shown(c); };
        QQuickItem *button = hint ? firstOf(hint->findChildren<QQuickItem *>(), isButton) : nullptr;
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
        QQuickItem *more = firstOf(m_window->findChildren<QQuickItem *>(), [&moreLabel](const QQuickItem *i) {
            return i->property("label").toString() == moreLabel && shown(i);
        });
        check(more != nullptr, "the top bar has “More”");
        if (!more)
            return;
        tap(more);
        // In the menu's "Notebook" submenu.
        const QString notebookTitle = QCoreApplication::translate("ShellTopBar", "Notebook");
        QQuickItem *notebook = nullptr;
        waitFor([&] {
            notebook = firstOf(m_window->findChildren<QQuickItem *>(), [&notebookTitle](const QQuickItem *i) {
                return i->inherits("QQuickMenuItem") && i->property("text").toString() == notebookTitle && shown(i);
            });
            return notebook != nullptr;
        });
        check(notebook != nullptr, "the “More” menu has the “Notebook” submenu");
        if (!notebook)
            return;
        tap(notebook);
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

    /// The first shown item of the visual tree (the window's items and its overlay: popups,
    /// menus) that `accept` takes — also the items of Repeaters and Instantiators, which
    /// findChildren() does not see.
    QQuickItem *visualItem(const std::function<bool(const QQuickItem *)> &accept) const
    {
        QQuickItem *root =
                m_window->contentItem()->parentItem() ? m_window->contentItem()->parentItem() : m_window->contentItem();
        QList<QQuickItem *> stack{root};
        while (!stack.isEmpty()) {
            QQuickItem *i = stack.takeLast();
            if (!i->isVisible())
                continue;
            if (shown(i) && accept(i))
                return i;
            const QList<QQuickItem *> children = i->childItems();
            for (qsizetype k = children.size() - 1; k >= 0; --k)
                stack.append(children[k]);
        }
        return nullptr;
    }

    /// A shown menu item (an open BMenu's) with this text.
    QQuickItem *menuItem(const QString &text) const
    {
        return visualItem([&text](const QQuickItem *i) {
            return i->inherits("QQuickMenuItem") && i->property("text").toString() == text;
        });
    }

    /// The shown segment of a BSegmented (`control`) with this text.
    QQuickItem *segment(QQuickItem *control, const QString &text) const
    {
        if (!control)
            return nullptr;
        return visualItem([control, &text](const QQuickItem *i) {
            return i->inherits("QQuickAbstractButton") && i->property("text").toString() == text
                   && control->isAncestorOf(i);
        });
    }

    // 6b. A CSV typed as a path: the mapping card; a column's type changed in its menu, and the
    //     sample page follows; one table page and back; the import writes the pages and the base.
    void csvMapping()
    {
        const QString csv = scratch().filePath(u"Książki.csv"_s);
        {
            QFile f(csv);
            // Windows-1250, ";" — as Excel on Polish Windows saves it.
            const QByteArray bytes = "Tytu\xb3;Autor;Rok\r\nPan Tadeusz;Adam Mickiewicz;1834\r\nLalka;Boles\xb3"
                                     "aw Prus;1890\r\n";
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
                check(false, "the CSV file is written");
                return;
            }
        }
        m_app->navigate({{u"view"_s, u"importExport"_s}, {u"tab"_s, u"import"_s}});
        QObject *controller = nullptr;
        QQuickItem *field = nullptr;
        QQuickItem *go = nullptr;
        waitFor([&] {
            controller = importController();
            field = item("importPathField");
            go = item("importPathButton");
            return controller && shown(field) && go && controller->property("state").toString() == u"empty";
        });
        check(controller && shown(field) && go, "the import tab is back for the CSV file");
        if (!controller || !field || !go)
            return;
        field->setProperty("text", csv);
        settle(100);
        tap(go);
        check(waitFor([controller] { return controller->property("state").toString() == u"ready"; }, 15000),
              "the CSV file is read and checked");
        QQuickItem *card = nullptr;
        waitFor([&] { return shown(card = item("importCsvMapping")); });
        check(controller->property("effectiveId").toString() == u"csv" && shown(card),
              "CSV is recognised and the mapping card shows");
        const auto sample = [controller] {
            return controller->property("csvMapping")
                    .toMap()
                    .value(u"file"_s)
                    .toMap()
                    .value(u"samplePage"_s)
                    .toString();
        };
        check(sample().contains(u"Rok: 1834"_s) && sample().contains(u"Autor: Adam Mickiewicz"_s),
              "the sample page: Windows-1250 read, the year a number");
        // (A Repeater's row: in the visual tree, not among findChildren().)
        QQuickItem *type = nullptr;
        waitFor(
                [&] {
                    type = visualItem([](const QQuickItem *i) { return i->objectName() == u"importCsvType_2"; });
                    return type != nullptr;
                },
                15000);
        check(shown(type), "the column “Rok” has its type menu");
        if (!shown(type))
            return;
        tap(type);
        const QString textLabel = QCoreApplication::translate("brulion::ioviews::ImportController", "Text");
        QQuickItem *entry = nullptr;
        waitFor([&] { return (entry = menuItem(textLabel)) != nullptr; });
        check(entry != nullptr, "the type menu lists “Text”");
        if (!entry)
            return;
        tap(entry);
        check(waitFor([&] { return sample().contains(u"Rok: \"1834\""_s); }, 15000),
              "the year chosen as text: the sample page follows");
        QQuickItem *mode = item("importCsvMode");
        QQuickItem *table = segment(mode, QCoreApplication::translate("ImportCsvMapping", "One page with the table"));
        check(table != nullptr, "the mapping offers one page with the table");
        if (!table)
            return;
        tap(table);
        const auto pages = [controller] { return controller->property("preview").toMap().value(u"pages"_s).toInt(); };
        check(waitFor([&] { return pages() == 1 && controller->property("state").toString() == u"ready"; }, 15000),
              "one page with the table: the preview counts one page");
        QQuickItem *rows = segment(mode, QCoreApplication::translate("ImportCsvMapping", "A page per row"));
        if (rows)
            tap(rows);
        check(waitFor([&] { return pages() == 2 && controller->property("state").toString() == u"ready"; }, 15000),
              "a page per row again: two pages");
        QQuickItem *importButton = item("importStartButton");
        if (!shown(importButton)) {
            check(false, "the preview offers “Import”");
            return;
        }
        tap(importButton);
        check(waitFor([controller] { return controller->property("state").toString() == u"done"; }, 15000),
              "the CSV import finishes");
        check(m_app->workspace()->exists(u"Import/CSV/Pan Tadeusz.md"_s)
                      && m_app->workspace()->exists(u"Import/CSV/Lalka.md"_s)
                      && m_app->workspace()->exists(u"Import/CSV/Książki.base"_s),
              "the rows are pages, with a base");
    }

    // 7. The export's save dialog opens with the produced file's name.
    void exportSaveDialog()
    {
        m_app->navigate({{u"view"_s, u"importExport"_s}, {u"tab"_s, u"export"_s}});
        QObject *controller = nullptr;
        QObject *dialog = nullptr;
        QQuickItem *button = nullptr;
        waitFor([&] {
            controller = lastOf(m_window->findChildren<QObject *>(u"exportController"_s), parentShown);
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

    // 8. One page (docs/UI.md "Exporting and printing a page"): "Export page" of the imported
    //    page → PDF → "Save as file…" makes the PDF and opens the save dialog with its name;
    //    "Print…" (into a PDF file instead of the system's print dialog) prints the page.
    void pageExport()
    {
        const QString pagePath = u"Import/Obsidian/Zażółć gęślą jaźń.md"_s;
        if (!m_app->workspace()->exists(pagePath)) {
            check(false, "the imported page is there to export");
            return;
        }
        m_app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, pagePath}, {u"open"_s, u"export"_s}});
        QObject *controller = nullptr;
        QQuickItem *pdf = nullptr;
        QQuickItem *save = nullptr;
        const auto onPage = [&pagePath](const QObject *c) { return c->property("page").toString() == pagePath; };
        const auto isPdf = [](const QQuickItem *tile) { return tile->objectName() == u"pageExportFormat_pdf"; };
        waitFor(
                [&] {
                    controller = lastOf(m_window->findChildren<QObject *>(u"pageExportController"_s), onPage);
                    // The tiles of a Repeater: items of the grid, not its QObject children.
                    const QQuickItem *grid = item("pageExportFormats");
                    pdf = grid ? lastOf(grid->childItems(), isPdf) : nullptr;
                    save = item("pageExportSave");
                    return controller && shown(pdf) && shown(save) && save->isEnabled();
                },
                10000);
        std::fprintf(stderr, "selftest: page view %s, controller %s, PDF tile %s, save button %s\n",
                     qPrintable(m_app->currentView()), controller ? "found" : "missing",
                     pdf ? (shown(pdf) ? "shown" : "hidden") : "missing",
                     save ? (shown(save) ? (save->isEnabled() ? "enabled" : "disabled") : "hidden") : "missing");
        check(controller && shown(pdf) && shown(save), "“Export page” shows the formats of one page");
        if (!controller || !shown(pdf) || !shown(save))
            return;
        tap(pdf);
        check(waitFor([controller] { return controller->property("format").toString() == u"pdf"; }),
              "the PDF tile chooses PDF");
        QObject *dialog =
                controller->parent() ? controller->parent()->findChild<QObject *>(u"pageExportSaveDialog"_s) : nullptr;
        if (!dialog) {
            check(false, "the page's save dialog exists");
            return;
        }
        {
            OpenWatcher watcher(dialog);
            tap(save);
            const bool opened = waitFor([&watcher] { return watcher.opened(); }, 20000);
            check(opened, "“Save as file…” makes the PDF and opens the save dialog");
            const QString name = controller->property("fileName").toString();
            std::fprintf(stderr, "selftest: page export “%s”, the dialog opened with “%s”\n", qPrintable(name),
                         qPrintable(watcher.fileAtOpen().toString()));
            check(name == u"Zażółć gęślą jaźń-2026-10-14.pdf"_s && watcher.fileAtOpen().fileName() == name,
                  "the save dialog opens with the PDF's name");
            QMetaObject::invokeMethod(dialog, "reject");
            check(waitFor([dialog] { return !dialog->property("visible").toBool(); }), "the save dialog closes");
        }
        pageExportWithPassword(qobject_cast<ioviews::ExportController *>(controller), dialog);
        QQuickItem *print = item("pageExportPrint");
        check(shown(print), "the page's export offers “Print…”");
        if (!shown(print))
            return;
        const QString printed = scratch().filePath(u"wydruk.pdf"_s);
        QMetaObject::invokeMethod(controller, "setPrintToFile", Q_ARG(QString, printed));
        tap(print);
        check(waitFor([controller] { return controller->property("state").toString() == u"printed"; }, 20000),
              "“Print…” prints the page");
        QFile file(printed);
        check(file.open(QIODevice::ReadOnly) && file.read(5) == "%PDF-", "the printed pages are there");
        settle(300);
    }

    // 8b. HTML with a password: the HTML tile shows "Protect with a password"; on, "Save as
    //     file…" waits for the password typed twice, then makes the lock page (no word of the
    //     page in it) and opens the save dialog; off again for the steps after it.
    void pageExportWithPassword(ioviews::ExportController *controller, QObject *saveDialog)
    {
        const QQuickItem *grid = item("pageExportFormats");
        QQuickItem *html = grid ? lastOf(grid->childItems(),
                                         [](const QQuickItem *tile) {
                                             return tile->objectName() == u"pageExportFormat_html-single"_s;
                                         })
                                : nullptr;
        if (!controller || !shown(html)) {
            check(false, "the page's export offers HTML");
            return;
        }
        controller->setProtectIterations(1000);
        tap(html);
        QQuickItem *protect = nullptr;
        check(waitFor([&] {
                  protect = item("exportProtect");
                  return controller->format() == u"html-single" && shown(protect);
              }),
              "HTML offers “Protect with a password”");
        if (!shown(protect))
            return;
        tap(protect);
        QQuickItem *password = item("exportPassword");
        QQuickItem *confirm = item("exportPasswordConfirm");
        QQuickItem *save = item("pageExportSave");
        check(waitFor([&] { return controller->protect() && shown(password) && shown(confirm); }),
              "the switch asks for the password twice");
        check(save && !save->isEnabled(), "“Save as file…” waits for the password");
        if (!shown(password) || !shown(confirm) || !save)
            return;
        password->setProperty("text", u"Źdźbło trawy na łące"_s);
        confirm->setProperty("text", u"Źdźbło trawy"_s);
        settle(100);
        check(!save->isEnabled() && !controller->hasPassword(), "passwords that differ are not taken");
        confirm->setProperty("text", u"Źdźbło trawy na łące"_s);
        check(waitFor([&] { return save->isEnabled() && controller->hasPassword(); }), "the same password twice is");
        {
            OpenWatcher watcher(saveDialog);
            tap(save);
            check(waitFor([&watcher] { return watcher.opened(); }, 20000),
                  "“Save as file…” makes the protected HTML and opens the save dialog");
            const QString page = controller->result().isEmpty() ? QString()
                                                                : QString::fromUtf8(controller->result().first().data);
            check(page.contains(u"id=\"brulion-sealed\""_s) && !page.contains(u"Zażółć"_s),
                  "the file is the lock page, nothing of the page readable");
            QMetaObject::invokeMethod(saveDialog, "reject");
            check(waitFor([saveDialog] { return !saveDialog->property("visible").toBool(); }), "the save dialog closes");
        }
        tap(protect);
        check(waitFor([controller] { return !controller->protect(); }), "the switch turns protection off");
    }

    // 9. Publishing (docs/UI.md "Publishing"): the page's "Publish…" with no target yet asks for
    //    one — a folder on this computer, named in the dialog —, the page goes to it and the dry
    //    run lists its web page, writing nothing; "Publish" then puts the site into the folder.
    void publishDryRun()
    {
        const QString pagePath = u"Import/Obsidian/Zażółć gęślą jaźń.md"_s;
        const QString folder = scratch().filePath(u"strona łąka"_s);
        m_app->navigate({{u"view"_s, u"importExport"_s},
                         {u"tab"_s, u"publish"_s},
                         {u"add"_s, u"page"_s},
                         {u"path"_s, pagePath}});
        QObject *controller = nullptr;
        QObject *dialog = nullptr;
        QQuickItem *name = nullptr;
        QQuickItem *kind = nullptr;
        waitFor(
                [&] {
                    controller = lastOf(m_window->findChildren<QObject *>(u"publishController"_s), parentShown);
                    dialog = controller && controller->parent()
                                     ? controller->parent()->findChild<QObject *>(u"publishTargetDialog"_s)
                                     : nullptr;
                    name = item("publishTargetName");
                    // The tiles of a Repeater: items of the grid, not its QObject children.
                    const QQuickItem *kinds = item("publishKinds");
                    kind = kinds ? lastOf(kinds->childItems(),
                                          [](const QQuickItem *tile) {
                                              return tile->objectName() == u"publishKind_folder";
                                          })
                                 : nullptr;
                    return controller && dialog && dialog->property("opened").toBool() && shown(name) && shown(kind);
                },
                10000);
        std::fprintf(stderr,
                     "selftest: view %s (%s), publish controller %s, target dialog %s, name field %s, folder tile %s\n",
                     qPrintable(m_app->currentView()), qPrintable(m_app->currentRoute().value(u"tab"_s).toString()),
                     controller ? "found" : "missing",
                     dialog ? (dialog->property("opened").toBool() ? "open" : "closed") : "missing",
                     name ? (shown(name) ? "shown" : "hidden") : "missing",
                     kind ? (shown(kind) ? "shown" : "hidden") : "missing");
        check(controller && dialog && shown(name) && shown(kind),
              "“Publish…” of a page without a target asks for a new target");
        if (!controller || !dialog || !shown(name) || !shown(kind))
            return;
        tap(kind);
        QQuickItem *path = nullptr;
        check(waitFor([&] {
                  path = item("publishFolderPath");
                  return shown(path);
              }),
              "the folder target asks for its folder");
        if (!shown(path))
            return;
        name->setProperty("text", u"Strona łąkowa"_s);
        path->setProperty("text", folder);
        check(waitFor([dialog] { return dialog->property("acceptEnabled").toBool(); }), "the target can be saved");
        QMetaObject::invokeMethod(dialog, "accept");
        // The page is added to the new target and previewed: a dry run.
        check(waitFor([controller] { return controller->property("state").toString() == u"previewed"; }, 20000),
              "the page is added to the target and previewed");
        const QVariantMap plan = controller->property("plan").toMap();
        const QVariantList files = plan.value(u"files"_s).toList();
        const bool listsPage = std::ranges::any_of(files, [](const QVariant &f) {
            const QVariantMap row = f.toMap();
            return row.value(u"path"_s).toString() == u"Import/Obsidian/Zażółć gęślą jaźń.html"
                   && row.value(u"action"_s).toString() == u"upload";
        });
        std::fprintf(stderr, "selftest: the dry run lists %lld file(s): %s\n", qint64(files.size()),
                     qPrintable(plan.value(u"summary"_s).toString()));
        check(listsPage && plan.value(u"remoteChecked"_s).toBool(), "the dry run lists the page's web page as new");
        check(!QFileInfo::exists(folder), "the dry run writes nothing");
        QQuickItem *planCard = item("publishPlan");
        QQuickItem *publish = item("publishFromPreview");
        check(shown(planCard) && shown(publish), "the dry run is shown with “Publish”");
        if (!shown(publish))
            return;
        tap(publish);
        check(waitFor([controller] { return controller->property("state").toString() == u"published"; }, 20000),
              "“Publish” publishes the page");
        check(QFileInfo::exists(folder + u"/Import/Obsidian/Zażółć gęślą jaźń.html"_s)
                      && QFileInfo::exists(folder + u"/index.html"_s) && QFileInfo::exists(folder + u"/search.js"_s),
              "the site is in the folder");
        check(shown(item("publishUrl")), "the published site's address is shown");
        QMetaObject::invokeMethod(controller, "dismiss");
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
        const int failures = SelfTest(engine, app).run();
        std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::ioviews

#include "ioselftest.moc"
