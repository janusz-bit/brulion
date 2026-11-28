// The terminal panel's C++ (src/app/terminal): TerminalSession end to end on a real
// pseudo-terminal — input echoed by cat, a paste, the shell started in its folder printing pwd, the
// size the program sees, the title it sets, stop and restart, exit codes, whether a program runs in
// it, the marks of output and a bell while not shown, its current directory —, a thread's tabs and
// splits (TerminalThread, TerminalGroup) and every thread's terminals (TerminalRegistry: keys,
// rekeying, closing, the limits that never end a running program), and TerminalView in a window:
// keys into the program (Ctrl+C is ^C; Ctrl+Shift+C copies; the terminal's own Ctrl+Shift chords;
// the app's shortcuts left alone), the input method's text, selection by dragging and by double and
// triple clicks, copying, the scrollback and its keys and wheel, mouse reporting, links under Ctrl
// and Ctrl+click, the find bar's matches, Clear, the context menu's request, the block for the
// chat, painting at odd sizes and in both palettes. Windows (ConPTY): the user's shell — PowerShell
// or cmd.exe — in its folder, commands run through it with their exit codes (a native program's
// own, a failing cmdlet's 1), a tab's label over the console's title, stop and restart.
#include "shelllaunch.h"
#include "terminalgroup.h"
#include "terminalregistry.h"
#include "terminalsession.h"
#include "terminalthread.h"
#include "terminalview.h"

#include "pty.h"

#include "term/screen.h"

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "testtiming.h"

using namespace Qt::StringLiterals;
using brulion::terminal::TerminalGroup;
using brulion::terminal::TerminalRegistry;
using brulion::terminal::TerminalSession;
using brulion::terminal::TerminalThread;
using brulion::terminal::TerminalView;

namespace {

// A shell's start, output and exit: work the tests are not about — kRoomyMs (testtiming.h). On
// CI's Windows runner a missing command's PowerShell took 16.35 s to exit, past the 15 s this was.
constexpr int Timeout = brulion::test::kRoomyMs;

QString screenText(const TerminalSession &session)
{
    return session.terminal().screen().text();
}

/// Output for a session that runs no program (the view's tests).
void show(TerminalSession &session, const QByteArray &output)
{
    session.terminal().feed(output);
    Q_EMIT session.contentChanged();
}

/// A view of 640 × 400 in a shown window.
struct Window {
    QQuickWindow window;
    TerminalView *view = nullptr;

    Window()
    {
        window.resize(640, 400);
        view = new TerminalView(window.contentItem());
        QFont font(u"monospace"_s);
        font.setPixelSize(14);
        view->setFont(font);
        view->setSize(QSizeF(640, 400));
        window.show();
        if (!QTest::qWaitForWindowExposed(&window))
            qWarning("the window was not exposed");
        view->forceActiveFocus();
    }
    /// The middle of cell (column, row) of the view.
    QPointF cell(int column, int row) const
    {
        return QPointF(view->padding() + (column + 0.5) * view->cellWidth(),
                       view->padding() + (row + 0.5) * view->cellHeight());
    }
    /// The boundary left of cell (column, row), halfway down the row.
    QPointF edge(int column, int row) const
    {
        return QPointF(view->padding() + column * view->cellWidth(),
                       view->padding() + (row + 0.5) * view->cellHeight());
    }
    void mouse(QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons, ulong timestamp,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type, at, window.mapToGlobal(at), button, buttons, modifiers);
        event.setTimestamp(timestamp);
        QGuiApplication::sendEvent(&window, &event);
    }
    void click(QPointF at, ulong timestamp, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, timestamp, modifiers);
        mouse(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, timestamp + 1, modifiers);
    }
    void drag(QPointF from, QPointF to, ulong timestamp, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, timestamp, modifiers);
        for (int i = 1; i <= 4; ++i)
            mouse(QEvent::MouseMove, from + (to - from) * (i / 4.0), Qt::NoButton, Qt::LeftButton, timestamp + ulong(i),
                  modifiers);
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, timestamp + 5, modifiers);
    }
    void wheel(QPointF at, int angle, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QWheelEvent event(at, window.mapToGlobal(at), QPoint(), QPoint(0, angle), Qt::NoButton, modifiers,
                          Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&window, &event);
    }
};

} // namespace

class TestTerminal : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void availability();
    void echoThroughCat();
    void shellInFolder();
    void sizeOfThePty();
    void titleFromProgram();
    void stopAndRestart();
    void exitCodes();
    void busyWithAJob();
    void marksWhileNotShown();
    void currentDirectory();
    void tabsAndSplits();
    void registryKeys();
    void registryLimits();
    void registrySettle();
    void splitSizes();
    void splitSizesKept();
    void registryLayouts();
    void viewKeys();
    void viewTerminalKeys();
    void viewLinks();
    void viewFind();
    void viewClear();
    void viewReflow();
    void viewAttachesAfterOutput();
    void viewContextMenu();
    void chatBlock_data();
    void chatBlock();
    void viewSelection();
    void viewScrollback();
    void viewMouseReporting();
    void viewPaintsAtOddSizes();
    void viewColors();
    void viewLayout();
    void viewFlood();

    // Windows (ConPTY)
    void windowsShellInFolder();
    void windowsCommands();
    void windowsStopAndRestart();

private:
    bool m_posix = false;
    bool m_conpty = false;
    ulong m_time = 1000;
};

void TestTerminal::initTestCase()
{
    m_posix = QByteArrayView(BRULION_TEST_PTY_BACKEND) == "posix";
    m_conpty = QByteArrayView(BRULION_TEST_PTY_BACKEND) == "conpty";
    // A shell whose startup prints nothing of the user's own (Windows: not a path there, so
    // PowerShell or cmd.exe).
    qputenv("SHELL", "/bin/sh");
}

void TestTerminal::cleanup()
{
    // Sessions ended by a test are deleted later: now, before the next test (and no leaks).
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void TestTerminal::availability()
{
    QCOMPARE(TerminalSession::available(), m_posix || m_conpty);
    QCOMPARE(TerminalRegistry::available(), m_posix || m_conpty);
    if (!m_posix && !m_conpty) {
        TerminalSession session;
        QVERIFY(!session.start());
        QVERIFY(!session.errorString().isEmpty());
        QVERIFY(!session.isRunning());
    }
}

void TestTerminal::echoThroughCat()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalSession session;
    session.setCommand(u"cat"_s);
    QSignalSpy exited(&session, &TerminalSession::exited);
    QSignalSpy running(&session, &TerminalSession::runningChanged);
    QVERIFY(session.start());
    QVERIFY(session.isRunning());
    QVERIFY(!session.hasExited());
    QCOMPARE(session.exitCode(), -1);
    QCOMPARE(session.title(), u"cat"_s);
    QCOMPARE(session.commandLine(), (QStringList{u"/bin/sh"_s, u"-lc"_s, u"cat"_s}));
    session.write(u"zażółć\r"_s);
    // The line's echo, then cat's copy of it.
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"zażółć"_s), 2, Timeout);
    session.paste(u"gęślą\njaźń\n"_s);
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"jaźń"_s), 2, Timeout);
    QCOMPARE(screenText(session).count(u"gęślą"_s), 2);
    session.write(u"\x04"_s);
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(exited.at(0).at(0).toInt(), 0);
    QCOMPARE(session.exitCode(), 0);
    QVERIFY(session.hasExited());
    QVERIFY(!session.isRunning());
    QCOMPARE(running.size(), 2);
    // Input after the end goes nowhere.
    session.write(u"lost"_s);
}

void TestTerminal::shellInFolder()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(dir.path()).absoluteFilePath(u"projekt żółw"_s);
    QVERIFY(QDir().mkpath(folder));
    TerminalSession session;
    session.setFolder(folder);
    QSignalSpy exited(&session, &TerminalSession::exited);
    QVERIFY(session.start());
    QCOMPARE(session.title(), u"sh"_s);
    session.write(u"pwd\r"_s);
    QTRY_VERIFY_WITH_TIMEOUT(screenText(session).contains(folder + u"\n"_s), Timeout);
    session.write(u"exit 5\r"_s);
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(session.exitCode(), 5);
}

void TestTerminal::sizeOfThePty()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalSession session;
    QSignalSpy size(&session, &TerminalSession::sizeChanged);
    session.resize(100, 30);
    QCOMPARE(size.size(), 1);
    QCOMPARE(session.columns(), 100);
    QCOMPARE(session.rows(), 30);
    session.resize(0, 0); // ignored
    QCOMPARE(session.columns(), 100);
    session.setCommand(u"stty size; read x; stty size"_s);
    QVERIFY(session.start());
    QTRY_VERIFY_WITH_TIMEOUT(screenText(session).contains(u"30 100"_s), Timeout);
    session.resize(120, 40);
    session.write(u"\r"_s);
    QTRY_VERIFY_WITH_TIMEOUT(screenText(session).contains(u"40 120"_s), Timeout);
}

void TestTerminal::titleFromProgram()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalSession session;
    session.setCommand(u"printf '\\033]0;Mój tytuł\\007'"_s);
    session.setLabel(u"Skrypt"_s);
    QCOMPARE(session.title(), u"Skrypt"_s);
    QSignalSpy titles(&session, &TerminalSession::titleChanged);
    QSignalSpy exited(&session, &TerminalSession::exited);
    QVERIFY(session.start());
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(session.title(), u"Mój tytuł"_s);
    QVERIFY(titles.size() >= 1);
    // OSC 7: the directory the shell reports.
    TerminalSession cwd;
    cwd.setCommand(u"printf '\\033]7;file://host/tmp/kat%%C3%%B3w\\007'"_s);
    QSignalSpy cwdExited(&cwd, &TerminalSession::exited);
    QVERIFY(cwd.start());
    QTRY_COMPARE_WITH_TIMEOUT(cwdExited.size(), 1, Timeout);
    QCOMPARE(cwd.workingDirectory(), u"/tmp/katów"_s);
}

void TestTerminal::stopAndRestart()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalSession session;
    session.setCommand(u"echo started; sleep 100"_s);
    QSignalSpy exited(&session, &TerminalSession::exited);
    QVERIFY(session.start());
    QVERIFY(session.start()); // running already: nothing
    QTRY_VERIFY_WITH_TIMEOUT(screenText(session).contains(u"started"_s), Timeout);
    session.stop();
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(session.exitCode(), 128 + 1); // SIGHUP
    QVERIFY(session.hasExited());
    // Restart: a clean screen, the same command again.
    QVERIFY(session.start());
    QVERIFY(session.isRunning());
    QVERIFY(!session.hasExited());
    QTRY_VERIFY_WITH_TIMEOUT(screenText(session).contains(u"started"_s), Timeout);
    QCOMPARE(screenText(session).count(u"started"_s), 1);
    session.stop();
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 2, Timeout);
}

void TestTerminal::exitCodes()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalSession failing;
    failing.setCommand(u"exit 3"_s);
    QSignalSpy exited(&failing, &TerminalSession::exited);
    QVERIFY(failing.start());
    QVERIFY(failing.errorString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(failing.exitCode(), 3);
    TerminalSession missing;
    missing.setCommand(u"brulion-no-such-command-xyz"_s);
    QSignalSpy missingExited(&missing, &TerminalSession::exited);
    QVERIFY(missing.start());
    QTRY_COMPARE_WITH_TIMEOUT(missingExited.size(), 1, Timeout);
    QCOMPARE(missing.exitCode(), 127);
    // A folder that does not exist: the home folder.
    TerminalSession home;
    home.setFolder(u"/nonexistent/folder"_s);
    home.setCommand(u"pwd"_s);
    QSignalSpy homeExited(&home, &TerminalSession::exited);
    QVERIFY(home.start());
    QTRY_COMPARE_WITH_TIMEOUT(homeExited.size(), 1, Timeout);
    QVERIFY2(screenText(home).contains(QDir::homePath())
                     || screenText(home).contains(QDir(QDir::homePath()).canonicalPath()),
             qPrintable(screenText(home)));
}

void TestTerminal::busyWithAJob()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    // The interactive shell: a job of its own (job control) in the foreground.
    TerminalSession shell;
    QSignalSpy busy(&shell, &TerminalSession::busyChanged);
    QVERIFY(shell.start());
    QVERIFY(!shell.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(!screenText(shell).trimmed().isEmpty(), Timeout); // the prompt
    shell.write(u"sleep 100\r"_s);
    QTRY_VERIFY_WITH_TIMEOUT(shell.isBusy(), Timeout);
    QVERIFY(busy.size() >= 1);
    // ^C ends the job; the shell stays, idle.
    shell.write(u"\x03"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!shell.isBusy(), Timeout);
    QVERIFY(shell.isRunning());
    // A session's own command is its program: busy while it runs.
    TerminalSession command;
    command.setCommand(u"sleep 0.3"_s);
    QVERIFY(command.start());
    QVERIFY(command.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(command.hasExited(), Timeout);
    QVERIFY(!command.isBusy());
}

void TestTerminal::marksWhileNotShown()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalSession session;
    session.setCommand(u"cat"_s);
    QSignalSpy marks(&session, &TerminalSession::marksChanged);
    QVERIFY(session.start());
    // What it prints in its first half second (a shell's prompt) is no activity.
    session.write(u"start\r"_s);
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"start"_s), 2, Timeout);
    QVERIFY(!session.hasActivity() || BRULION_TEST_SLOWDOWN > 1);
    QTest::qWait(600);
    // Output while no view shows it: activity; a bell: rung.
    session.write(u"zażółć\r"_s);
    QTRY_VERIFY_WITH_TIMEOUT(session.hasActivity(), Timeout);
    QVERIFY(!session.bellRung());
    session.write(u"\x07\r"_s);
    QTRY_VERIFY_WITH_TIMEOUT(session.bellRung(), Timeout);
    // Shown: both go; output while shown leaves none.
    session.setShown(true);
    QVERIFY(session.isShown());
    QVERIFY(!session.hasActivity());
    QVERIFY(!session.bellRung());
    session.write(u"gęślą\r"_s);
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"gęślą"_s), 2, Timeout);
    QVERIFY(!session.hasActivity());
    session.setShown(false);
    QVERIFY(!session.isShown());

    // A view in a window shows it; hidden, it does not.
    Window w;
    w.view->setSession(&session);
    QVERIFY(session.isShown());
    w.view->setVisible(false);
    QVERIFY(!session.isShown());
    w.view->setVisible(true);
    QVERIFY(session.isShown());
    w.view->setSession(nullptr);
    QVERIFY(!session.isShown());
    session.stop();
    QTRY_VERIFY_WITH_TIMEOUT(session.hasExited(), Timeout);
}

void TestTerminal::currentDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(u"podkatalog"_s));
    TerminalSession session;
    // Before it runs: its folder; none (or a missing one): home.
    QCOMPARE(session.currentDirectory(), QDir::homePath());
    session.setFolder(dir.path() + u"/brak"_s);
    QCOMPARE(session.currentDirectory(), QDir::homePath());
    session.setFolder(dir.path());
    QCOMPARE(session.currentDirectory(), dir.path());
    if (!m_posix)
        return;
    QVERIFY(session.start());
    QTRY_VERIFY_WITH_TIMEOUT(!screenText(session).trimmed().isEmpty(), Timeout);
    session.write(u"cd podkatalog\r"_s);
#if defined(Q_OS_LINUX)
    // The shell's own directory, though it reported none (no OSC 7).
    QTRY_COMPARE_WITH_TIMEOUT(QDir(session.currentDirectory()).canonicalPath(),
                              QDir(dir.path()).canonicalPath() + u"/podkatalog"_s, Timeout);
#endif
    session.stop();
    QTRY_VERIFY_WITH_TIMEOUT(session.hasExited(), Timeout);
}

void TestTerminal::tabsAndSplits()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    TerminalRegistry registry;
    const QString key = u"AI/2026-11-26 Wątek.md"_s;
    TerminalThread *thread = registry.thread(key, dir.path());
    QVERIFY(thread);
    QCOMPARE(thread->key(), key);
    QCOMPARE(thread->folder(), dir.path());
    QCOMPARE(registry.thread(key, QString()), thread);
    QCOMPARE(thread->count(), 0);
    QCOMPARE(thread->currentIndex(), -1);
    QCOMPARE(thread->currentSession(), nullptr);
    QVERIFY(thread->autoOpen());

    // A tab: one shell, shown, started, in the thread's folder.
    TerminalSession *first = thread->openTab(QString(), u"Powłoka"_s);
    QVERIFY(first);
    QVERIFY(first->isRunning());
    QCOMPARE(first->folder(), dir.path());
    QCOMPARE(thread->count(), 1);
    QCOMPARE(thread->currentIndex(), 0);
    QCOMPARE(thread->currentSession(), first);
    TerminalGroup *group = thread->current();
    QCOMPARE(group->count(), 1);
    QCOMPARE(group->focusSession(), first);
    QCOMPARE(group->title(), u"Powłoka"_s);
    QCOMPARE(thread->data(thread->index(0), TerminalThread::TitleRole).toString(), u"Powłoka"_s);

    // Splits: beside, then under (the tab's direction is the last split's), focused, up to four.
    TerminalSession *right = thread->split(Qt::Horizontal);
    QVERIFY(right);
    QCOMPARE(group->count(), 2);
    QCOMPARE(group->orientation(), Qt::Horizontal);
    QCOMPARE(group->focusIndex(), 1);
    QCOMPARE(thread->currentSession(), right);
    QCOMPARE(thread->data(thread->index(0), TerminalThread::CountRole).toInt(), 2);
    TerminalSession *down = thread->split(Qt::Vertical);
    QVERIFY(down);
    QCOMPARE(group->orientation(), Qt::Vertical);
    QCOMPARE(group->indexOf(down), 2);
    QVERIFY(thread->split(Qt::Vertical));
    QCOMPARE(group->count(), TerminalGroup::MaxSessions);
    QVERIFY(!group->canSplit());
    QCOMPARE(thread->split(Qt::Horizontal), nullptr);
    QVERIFY(!thread->error().isEmpty());
    QCOMPARE(thread->sessionCount(), 4);

    // A second tab after it, with a command; it ends: its exit code in the tab's roles.
    TerminalSession *echo = thread->openTab(u"echo hej; exit 3"_s, QString());
    QVERIFY(echo);
    QVERIFY(thread->error().isEmpty());
    QCOMPARE(thread->count(), 2);
    QCOMPARE(thread->currentIndex(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(thread->data(thread->index(1), TerminalThread::HasExitedRole).toBool(), Timeout);
    QCOMPARE(thread->data(thread->index(1), TerminalThread::ExitCodeRole).toInt(), 3);

    // Through every terminal: the last tab's one, then the first tab's four, around.
    thread->focusNext(1);
    QCOMPARE(thread->currentIndex(), 0);
    QCOMPARE(group->focusIndex(), 0);
    thread->focusNext(1);
    QCOMPARE(group->focusIndex(), 1);
    thread->focusNext(-2);
    QCOMPARE(thread->currentIndex(), 1);
    thread->setCurrentIndex(0);
    group->setFocusIndex(2);
    QCOMPARE(thread->currentSession(), down);

    // Closing one: the others stay, the one beside it is focused; a tab left empty goes.
    QPointer<TerminalSession> gone = down;
    thread->closeSession(down, true);
    QCOMPARE(group->count(), 3);
    QCOMPARE(group->focusIndex(), 2);
    QTRY_VERIFY(gone.isNull());
    thread->closeTab(1, true);
    QCOMPARE(thread->count(), 1);
    QCOMPARE(thread->currentIndex(), 0);
    QVERIFY(thread->autoOpen());
    // The user closed the last one: no shell opens by itself here any more.
    thread->closeTab(0, true);
    QCOMPARE(thread->count(), 0);
    QCOMPARE(thread->sessionCount(), 0);
    QCOMPARE(thread->currentIndex(), -1);
    QVERIFY(!thread->autoOpen());
    // New terminals start where the thread works now.
    thread->setFolder(QDir::tempPath());
    TerminalSession *again = thread->openTab(QString(), QString());
    QCOMPARE(again->folder(), QDir::tempPath());
    thread->closeAll();
    QCOMPARE(thread->count(), 0);
}

void TestTerminal::registryKeys()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    TerminalRegistry registry;
    QCOMPARE(registry.thread(QString(), dir.path()), nullptr);
    // A new thread's terminals, keyed by its folder.
    TerminalThread *thread = registry.thread(dir.path(), dir.path());
    registry.shown(dir.path());
    QVERIFY(thread->openTab(QString(), QString()));
    QCOMPARE(registry.keys(), QStringList{dir.path()});
    QCOMPARE(registry.sessionCount(), 1);
    // Its first message made its page.
    const QString page = u"AI/2026-11-26 Napraw błąd.md"_s;
    QVERIFY(registry.rekey(dir.path(), page));
    QCOMPARE(registry.find(dir.path()), nullptr);
    QCOMPARE(registry.find(page), thread);
    QCOMPARE(thread->key(), page);
    QCOMPARE(registry.keys(), QStringList{page});
    QVERIFY(!registry.rekey(u"AI/none.md"_s, page));
    // Another thread's terminals are not taken over.
    TerminalThread *other = registry.thread(u"AI/inny.md"_s, dir.path());
    QVERIFY(other->openTab(QString(), QString()));
    QVERIFY(!registry.rekey(u"AI/inny.md"_s, page));
    // A rename of the page: the terminals follow.
    const QString renamed = u"AI/2026-11-26 Błąd naprawiony.md"_s;
    registry.pathRenamed(page, renamed);
    QCOMPARE(registry.find(renamed), thread);
    QCOMPARE(registry.find(page), nullptr);

    // A program running: busy, and what an archive leaves.
    QSignalSpy busy(&registry, &TerminalRegistry::busyChanged);
    QVERIFY(thread->openTab(u"sleep 100"_s, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(registry.isBusy(renamed), Timeout);
    QCOMPARE(registry.busyKeys(), QStringList{renamed});
    QCOMPARE(busy.last().at(0).toString(), renamed);
    QCOMPARE(busy.last().at(1).toBool(), true);
    QCOMPARE(thread->sessionCount(), 2);
    registry.archived(renamed, true);
    QCOMPARE(thread->sessionCount(), 1); // the idle shell ended, the running sleep stayed
    QVERIFY(thread->sessions().first()->isBusy());
    registry.archived(renamed, false); // brought back: nothing ends
    QCOMPARE(thread->sessionCount(), 1);
    // Deleted: everything ends, the thread is forgotten (it was not shown).
    registry.shown(u"AI/inny.md"_s);
    QPointer<TerminalThread> watched = thread;
    registry.pathRemoved(renamed);
    QCOMPARE(registry.find(renamed), nullptr);
    QVERIFY(!registry.isBusy(renamed));
    QCOMPARE(busy.last().at(0).toString(), renamed);
    QCOMPARE(busy.last().at(1).toBool(), false);
    QTRY_VERIFY(watched.isNull());
    QCOMPARE(registry.keys(), QStringList{u"AI/inny.md"_s});
    // The shown one deleted: its terminals end, the object stays for the panel.
    registry.close(u"AI/inny.md"_s);
    QCOMPARE(registry.find(u"AI/inny.md"_s), other);
    QCOMPARE(other->sessionCount(), 0);
    QCOMPARE(registry.sessionCount(), 0);
    // Quitting.
    QVERIFY(other->openTab(QString(), QString()));
    registry.closeAll();
    QCOMPARE(registry.sessionCount(), 0);
}

// A thread settled (T3 Code's settling): its stopOnSettle scripts' terminals end, its idle ones too,
// a busy one of another command stays; its runOnSettle scripts run one at a time across threads in
// its worktree — a tab that ended with 0 closes, a failed one stays —; without a worktree none run.
void TestTerminal::registrySettle()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir, worktree;
    TerminalRegistry registry;
    const QString page = u"AI/2026-11-28 Rabat żółwia.md"_s;
    TerminalThread *thread = registry.thread(page, dir.path());
    QVERIFY(thread->openTab(QString(), QString()));
    TerminalSession *dev = thread->openTab(u"sleep 100"_s, u"Dev"_s);
    TerminalSession *build = thread->openTab(u"sleep 200"_s, u"Build"_s);
    QVERIFY(dev && build);
    QTRY_VERIFY_WITH_TIMEOUT(dev->isBusy() && build->isBusy(), Timeout);
    QPointer<TerminalSession> devWatched = dev;

    QSignalSpy ended(&registry, &TerminalRegistry::settleScriptEnded);
    registry.settle(page, {u"sleep 100"_s}, worktree.path(),
                    {{u"pwd > settled.txt"_s, u"Czyść"_s}, {u"exit 3"_s, u"Zły"_s}});
    // (One at a time: the first runs, the second waits — before the event loop lets either end.)
    QVERIFY(registry.settleRunning());
    QCOMPARE(registry.settleWaiting(), 1);
    // Another thread settled meanwhile waits for its turn.
    registry.settle(u"AI/inny.md"_s, {}, worktree.path(), {{u"true"_s, u"Inny"_s}});
    QCOMPARE(registry.settleWaiting(), 2);
    QTRY_VERIFY(devWatched.isNull());
    // (The idle shell and the stopped dev server gone; the build there.)
    QVERIFY(thread->sessions().contains(build));
    QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 3, Timeout);
    QCOMPARE(ended.at(0), (QVariantList{page, u"Czyść"_s, 0}));
    QCOMPARE(ended.at(1), (QVariantList{page, u"Zły"_s, 3}));
    QCOMPARE(ended.at(2), (QVariantList{u"AI/inny.md"_s, u"Inny"_s, 0}));
    // In the worktree.
    QFile out(worktree.filePath(u"settled.txt"_s));
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(QFileInfo(QString::fromUtf8(out.readAll()).trimmed()).canonicalFilePath(),
             QFileInfo(worktree.path()).canonicalFilePath());
    // The one that ended well closed, the failed one kept with the build.
    QTRY_COMPARE(thread->sessionCount(), 2);
    QVERIFY(thread->sessions().contains(build));
    QCOMPARE(registry.find(u"AI/inny.md"_s)->sessionCount(), 0);
    QVERIFY(!registry.settleRunning());
    // No worktree: nothing runs (the project's own folder is left alone).
    registry.settle(page, {}, QString(), {{u"true"_s, u"Nic"_s}});
    QCOMPARE(registry.settleWaiting(), 0);
    QVERIFY(!registry.settleRunning());
    QCOMPARE(ended.size(), 3);
    registry.closeAll();
}

// Resizable splits (docs/CODE-TERMINAL.md "Threads, tabs and splits"): an edge dragged between two
// panes moves only them, never below MinShare; equal again; a pane added or taken shares out again.
void TestTerminal::splitSizes()
{
    using brulion::terminal::movedEdge;
    const auto near = [](const QList<qreal> &a, const QList<qreal> &b) {
        if (a.size() != b.size())
            return false;
        for (qsizetype i = 0; i < a.size(); ++i)
            if (std::abs(a.at(i) - b.at(i)) > 1e-9)
                return false;
        return true;
    };
    QVERIFY(near(movedEdge({0.5, 0.5}, 1, 0.3), {0.3, 0.7}));
    QVERIFY(near(movedEdge({0.25, 0.25, 0.5}, 2, 0.6), {0.25, 0.35, 0.4}));
    // Clamped: neither pane under the minimum.
    QVERIFY(near(movedEdge({0.5, 0.5}, 1, 0.01), {TerminalGroup::MinShare, 1 - TerminalGroup::MinShare}));
    QVERIFY(near(movedEdge({0.5, 0.5}, 1, 2.0), {1 - TerminalGroup::MinShare, TerminalGroup::MinShare}));
    QVERIFY(near(movedEdge({0.2, 0.3, 0.5}, 1, 0.9), {0.2 + 0.3 - TerminalGroup::MinShare, TerminalGroup::MinShare, 0.5}));
    // No such edge: as it was.
    QVERIFY(near(movedEdge({0.5, 0.5}, 0, 0.3), {0.5, 0.5}));
    QVERIFY(near(movedEdge({0.5, 0.5}, 2, 0.3), {0.5, 0.5}));
    QVERIFY(near(movedEdge({1.0}, 1, 0.3), {1.0}));

    if (!m_posix)
        return;
    QTemporaryDir dir;
    TerminalRegistry registry;
    TerminalThread *thread = registry.thread(u"AI/podział.md"_s, dir.path());
    QVERIFY(thread->openTab(QString(), QString()));
    TerminalGroup *group = thread->current();
    QCOMPARE(group->share(0), 1.0);
    QVERIFY(thread->split(Qt::Horizontal));
    QCOMPARE(group->share(0), 0.5);
    QCOMPARE(group->start(1), 0.5);
    QSignalSpy sized(group, &TerminalGroup::sharesChanged);
    group->moveEdge(1, 0.25);
    QCOMPARE(sized.size(), 1);
    QCOMPARE(group->share(0), 0.25);
    QCOMPARE(group->start(1), 0.25);
    QCOMPARE(group->share(1), 0.75);
    QCOMPARE(group->share(5), 0.0);
    // A third pane: equal shares again; one taken: the rest share it.
    QVERIFY(thread->split(Qt::Horizontal));
    QVERIFY(std::abs(group->share(2) - 1.0 / 3) < 1e-9);
    group->moveEdge(1, 0.5);
    thread->closeSession(group->at(2), true);
    QCOMPARE(group->count(), 2);
    QVERIFY(std::abs(group->share(0) + group->share(1) - 1.0) < 1e-9);
    QVERIFY(group->share(0) > group->share(1));
    group->equalize();
    QCOMPARE(group->share(1), 0.5);
    registry.closeAll();
}

// The shares are part of a tab's layout over a restart.
void TestTerminal::splitSizesKept()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    QJsonObject layouts;
    {
        TerminalRegistry registry;
        TerminalThread *thread = registry.thread(u"AI/podział.md"_s, dir.path());
        registry.shown(u"AI/podział.md"_s);
        QVERIFY(thread->openTab(QString(), QString()));
        QVERIFY(thread->split(Qt::Vertical));
        thread->current()->moveEdge(1, 0.7);
        layouts = registry.layouts();
        registry.closeAll();
    }
    const QJsonArray sizes = layouts.value(u"threads"_s).toArray().at(0).toObject().value(u"tabs"_s).toArray().at(0)
                                     .toObject().value(u"sizes"_s).toArray();
    QCOMPARE(sizes.size(), 2);
    QVERIFY(std::abs(sizes.at(0).toDouble() - 0.7) < 1e-9);
    TerminalRegistry again;
    again.setLayouts(layouts);
    TerminalThread *thread = again.thread(u"AI/podział.md"_s, dir.path());
    QTRY_COMPARE(thread->count(), 1);
    QCOMPARE(thread->current()->count(), 2);
    QVERIFY(std::abs(thread->current()->share(0) - 0.7) < 1e-9);
    QCOMPARE(thread->current()->orientation(), Qt::Vertical);
    again.closeAll();
}

void TestTerminal::registryLimits()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    TerminalRegistry registry;
    registry.setLimits(2, 4);
    auto open = [&registry](const QString &key, int tabs) {
        TerminalThread *thread = registry.thread(key, QDir::tempPath());
        registry.shown(key);
        for (int i = 0; i < tabs; ++i) {
            if (!thread->openTab(QString(), QString()))
                return thread;
        }
        return thread;
    };
    TerminalThread *a = open(u"A.md"_s, 1);
    TerminalThread *b = open(u"B.md"_s, 2);
    QCOMPARE(registry.sessionCount(), 3);
    QCOMPARE(registry.keys(), (QStringList{u"B.md"_s, u"A.md"_s}));
    // A third thread shown: the least recently shown one's idle terminals end.
    QPointer<TerminalThread> watchedA = a;
    TerminalThread *c = open(u"C.md"_s, 1);
    QCOMPARE(c->sessionCount(), 1);
    registry.shown(u"C.md"_s);
    QCOMPARE(registry.keys(), (QStringList{u"C.md"_s, u"B.md"_s}));
    QTRY_VERIFY(watchedA.isNull()); // nothing left of it to remember
    QCOMPARE(registry.sessionCount(), 3);
    // Beyond the terminals' limit: one of another thread's idle ones makes room.
    QVERIFY(c->openTab(QString(), QString()));
    QCOMPARE(registry.sessionCount(), 4);
    QVERIFY(c->openTab(QString(), QString()));
    QCOMPARE(registry.sessionCount(), 4);
    QCOMPARE(b->sessionCount(), 1);
    QCOMPARE(c->sessionCount(), 3);

    // A running program is never ended to make room: with only those left, no new terminal.
    TerminalRegistry busy;
    busy.setLimits(1, 2);
    TerminalThread *server = busy.thread(u"Serwer.md"_s, QDir::tempPath());
    busy.shown(u"Serwer.md"_s);
    QVERIFY(server->openTab(u"sleep 100"_s, u"Dev server"_s));
    TerminalThread *next = busy.thread(u"Inny.md"_s, QDir::tempPath());
    busy.shown(u"Inny.md"_s);
    QVERIFY(next->openTab(QString(), QString()));
    busy.shown(u"Inny.md"_s); // two threads with terminals, the limit one: the busy one stays
    QCOMPARE(server->sessionCount(), 1);
    QCOMPARE(next->openTab(QString(), QString()), nullptr);
    QVERIFY(!next->error().isEmpty());
    QCOMPARE(busy.sessionCount(), 2);
}

void TestTerminal::registryLayouts()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath(u"zażółć"_s));
    const QString inner = dir.path() + u"/zażółć"_s;
    QByteArray stored;
    int saves = 0;
    {
        TerminalRegistry before;
        before.setLayoutStorage([] { return QByteArray(); },
                                [&stored, &saves](const QByteArray &json) {
                                    stored = json;
                                    ++saves;
                                });
        TerminalThread *thread = before.thread(u"AI/Wątek.md"_s, dir.path());
        before.shown(u"AI/Wątek.md"_s);
        QVERIFY(thread->openTab(QString(), QString()));
        QVERIFY(thread->split(Qt::Vertical));
        QVERIFY(thread->openTab(u"echo gotowe-$((40+2)); sleep 100"_s, u"Serwer"_s));
        thread->setCurrentIndex(0);
        // A moment after the change, written.
        QTRY_VERIFY_WITH_TIMEOUT(saves > 0, Timeout);
        thread->setFolder(inner);
        TerminalThread *other = before.thread(u"AI/Inny.md"_s, inner);
        QVERIFY(other->openTab(QString(), QString()));
        before.shown(u"AI/Wątek.md"_s);
        before.saveLayouts();
        const QJsonObject layouts = QJsonDocument::fromJson(stored).object();
        const QJsonArray threads = layouts.value(u"threads"_s).toArray();
        QCOMPARE(threads.size(), 2);
        // The most recently shown first.
        QCOMPARE(threads.at(0).toObject().value(u"key"_s).toString(), u"AI/Wątek.md"_s);
        const QJsonArray tabs = threads.at(0).toObject().value(u"tabs"_s).toArray();
        QCOMPARE(tabs.size(), 2);
        QCOMPARE(tabs.at(0).toObject().value(u"split"_s).toString(), u"down"_s);
        QCOMPARE(tabs.at(0).toObject().value(u"terminals"_s).toArray().size(), 2);
        QCOMPARE(tabs.at(1).toObject().value(u"terminals"_s).toArray().at(0).toObject().value(u"label"_s).toString(),
                 u"Serwer"_s);
    }
    // After a restart: the thread's tabs and splits come back when its terminals are asked for.
    TerminalRegistry after;
    after.setLayoutStorage([&stored] { return stored; }, {});
    QCOMPARE(after.savedCount(), 2);
    QCOMPARE(after.sessionCount(), 0); // nothing runs before it is asked for
    TerminalThread *thread = after.thread(u"AI/Wątek.md"_s, dir.path());
    QCOMPARE(after.savedCount(), 1);
    QCOMPARE(thread->count(), 2);
    QCOMPARE(thread->currentIndex(), 0);
    QCOMPARE(thread->group(0)->count(), 2);
    QCOMPARE(thread->group(0)->orientation(), Qt::Vertical);
    TerminalSession *server = thread->group(1)->at(0);
    QCOMPARE(server->label(), u"Serwer"_s);
    // The command typed into its shell, not run: Enter runs it again.
    QVERIFY(server->command().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(screenText(*server).contains(u"echo gotowe-$((40+2)); sleep 100"_s), Timeout);
    QVERIFY(!screenText(*server).contains(u"gotowe-42"_s));
    QVERIFY(!server->isBusy());
    server->write(u"\x03"_s);
    // Saved again: still that command's tab.
    const QJsonArray again = after.layouts().value(u"threads"_s).toArray();
    QCOMPARE(again.at(0).toObject().value(u"tabs"_s).toArray().at(1).toObject().value(u"terminals"_s).toArray().at(0)
                     .toObject().value(u"command"_s).toString(),
             u"echo gotowe-$((40+2)); sleep 100"_s);
    // Kept while not asked for: the other one is still in the layouts.
    const QJsonArray threads = after.layouts().value(u"threads"_s).toArray();
    QCOMPARE(threads.size(), 2);
    QCOMPARE(threads.at(1).toObject().value(u"key"_s).toString(), u"AI/Inny.md"_s);
    // Renamed and deleted threads: their layouts follow, and go.
    after.pathRenamed(u"AI/Inny.md"_s, u"AI/Nowa nazwa.md"_s);
    QCOMPARE(after.layouts().value(u"threads"_s).toArray().at(1).toObject().value(u"key"_s).toString(),
             u"AI/Nowa nazwa.md"_s);
    after.pathRemoved(u"AI/Nowa nazwa.md"_s);
    QCOMPARE(after.savedCount(), 0);
    // Malformed settings: skipped, nothing breaks.
    TerminalRegistry broken;
    broken.setLayouts(QJsonDocument::fromJson(
                              R"({"threads": [1, {"key": ""}, {"key": "a", "tabs": "x"}, {"key": "b", "tabs": [{"terminals": [{"folder": 5}]}]}]})")
                              .object());
    QCOMPARE(broken.savedCount(), 1);
    QVERIFY(broken.thread(u"b"_s, QDir::tempPath())->count() == 1);
    broken.setLayouts(QJsonObject());
    QCOMPARE(broken.savedCount(), 0);
    // A tab of four within a limit of three: three come back; a command of several lines is not
    // typed (its line breaks would run it), and kept.
    TerminalRegistry limited;
    limited.setLimits(10, 3);
    const QString tmp = QDir::tempPath();
    QJsonArray four;
    four.append(QJsonObject{{u"folder"_s, tmp}, {u"label"_s, QString()}, {u"command"_s, u"echo raz\necho dwa"_s}});
    for (int i = 0; i < 3; ++i)
        four.append(QJsonObject{{u"folder"_s, tmp}, {u"label"_s, QString()}, {u"command"_s, QString()}});
    limited.setLayouts(QJsonObject{
            {u"threads"_s, QJsonArray{QJsonObject{
                                   {u"key"_s, u"c"_s},
                                   {u"tabs"_s, QJsonArray{QJsonObject{{u"split"_s, u"right"_s}, {u"terminals"_s, four}}}}}}}});
    TerminalThread *three = limited.thread(u"c"_s, tmp);
    QCOMPARE(limited.sessionCount(), 3);
    QCOMPARE(three->group(0)->count(), 3);
    TerminalSession *script = three->group(0)->at(0);
    QCOMPARE(script->label(), u"echo raz echo dwa"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!screenText(*script).trimmed().isEmpty(), Timeout);
    QTest::qWait(200);
    QVERIFY(!screenText(*script).contains(u"echo raz"_s));
    QCOMPARE(script->typedCommand(), u"echo raz\necho dwa"_s);
}

void TestTerminal::viewTerminalKeys()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Window w;
    TerminalSession session;
    session.setCommand(u"cat"_s);
    w.view->setSession(&session);
    QVERIFY(session.start());
    QTRY_VERIFY(w.view->hasActiveFocus());
    QByteArray lines;
    for (int i = 0; i < 100; ++i)
        lines += "wiersz " + QByteArray::number(i) + "\r\n";
    show(session, lines);
    QVERIFY(w.view->historySize() > 0);

    QSignalSpy find(w.view, &TerminalView::findRequested);
    QTest::keyClick(&w.window, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(w.view->hasSelection());
    QVERIFY(w.view->selectedText().startsWith(u"wiersz 0\n"_s));
    QTest::keyClick(&w.window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(find.size(), 1);
    QTest::keyClick(&w.window, Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(w.view->scrollOffset(), w.view->historySize());
    QTest::keyClick(&w.window, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(w.view->scrollOffset(), 0);

    // The app's shortcuts are left to it; other Ctrl chords are the program's.
    w.view->setAppShortcuts({u"Ctrl+Alt+]"_s, u"Ctrl+`"_s, u"nie skrót"_s});
    auto wanted = [&w](Qt::Key key, Qt::KeyboardModifiers mods) {
        QKeyEvent over(QEvent::ShortcutOverride, key, mods);
        over.ignore();
        QGuiApplication::sendEvent(w.view, &over);
        return over.isAccepted();
    };
    QVERIFY(!wanted(Qt::Key_BracketRight, Qt::ControlModifier | Qt::AltModifier));
    QVERIFY(!wanted(Qt::Key_QuoteLeft, Qt::ControlModifier));
    QVERIFY(wanted(Qt::Key_BracketLeft, Qt::ControlModifier | Qt::AltModifier));
    QVERIFY(wanted(Qt::Key_R, Qt::ControlModifier));
    // The terminal's own Ctrl+Shift chords are its own; other ones the app's.
    QVERIFY(wanted(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier));
    QVERIFY(!wanted(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier));
    w.view->setAppShortcuts({});
    QVERIFY(wanted(Qt::Key_BracketRight, Qt::ControlModifier | Qt::AltModifier));
}

void TestTerminal::viewLinks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(u"src"_s));
    {
        QFile file(dir.path() + u"/src/main.cpp"_s);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    const QString main = QDir::cleanPath(dir.path() + u"/src/main.cpp"_s);
    Window w;
    TerminalSession session;
    session.setFolder(dir.path());
    w.view->setSession(&session);
    show(session, "error: src/main.cpp:12:5: boom\r\nsee https://example.com/x.\r\nmissing/file.cpp:3\r\n");
    QSignalSpy files(w.view, &TerminalView::fileActivated);
    QSignalSpy links(w.view, &TerminalView::linkActivated);
    QSignalSpy hovered(w.view, &TerminalView::hoveredLinkChanged);

    // Without Ctrl nothing is looked for.
    w.mouse(QEvent::MouseMove, w.cell(10, 0), Qt::NoButton, Qt::NoButton, m_time += 10);
    QCOMPARE(w.view->hoveredLink(), QString());
    // With Ctrl: the file under the pointer.
    w.mouse(QEvent::MouseMove, w.cell(11, 0), Qt::NoButton, Qt::NoButton, m_time += 10, Qt::ControlModifier);
    QCOMPARE(w.view->hoveredLink(), main);
    QCOMPARE(hovered.size(), 1);
    // Along the same link: nothing looked for again.
    w.mouse(QEvent::MouseMove, w.cell(15, 0), Qt::NoButton, Qt::NoButton, m_time += 10, Qt::ControlModifier);
    QCOMPARE(hovered.size(), 1);
    // Ctrl+click opens it at its line and column, selects nothing.
    w.click(w.cell(11, 0), m_time += 1000, Qt::ControlModifier);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.at(0).at(0).toString(), main);
    QCOMPARE(files.at(0).at(1).toInt(), 12);
    QCOMPARE(files.at(0).at(2).toInt(), 5);
    QVERIFY(!w.view->hasSelection());
    // Ctrl let go (the pointer moves without it): no link.
    w.mouse(QEvent::MouseMove, w.cell(12, 0), Qt::NoButton, Qt::NoButton, m_time += 10);
    QCOMPARE(w.view->hoveredLink(), QString());
    // A web address, without the full stop after it.
    w.click(w.cell(8, 1), m_time += 1000, Qt::ControlModifier);
    QCOMPARE(links.size(), 1);
    QCOMPARE(links.at(0).at(0).toString(), u"https://example.com/x"_s);
    // A path of no file: no link; the click is an ordinary one.
    w.mouse(QEvent::MouseMove, w.cell(3, 2), Qt::NoButton, Qt::NoButton, m_time += 10, Qt::ControlModifier);
    QCOMPARE(w.view->hoveredLink(), QString());
    w.click(w.cell(3, 2), m_time += 1000, Qt::ControlModifier);
    QCOMPARE(files.size(), 1);
    // New output over the hovered line ends the link.
    w.mouse(QEvent::MouseMove, w.cell(11, 0), Qt::NoButton, Qt::NoButton, m_time += 10, Qt::ControlModifier);
    QCOMPARE(w.view->hoveredLink(), main);
    show(session, "\x1b[1;1Hnothing here anymore\x1b[K");
    QCOMPARE(w.view->hoveredLink(), QString());
    // Painted with a link underlined: no crash, the view is drawn.
    QVERIFY(!w.window.grabWindow().isNull());
}

void TestTerminal::viewFind()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    QByteArray out;
    for (int i = 0; i < 200; ++i)
        out += "wiersz " + QByteArray::number(i) + (i % 50 == 0 ? " Zażółć gęślą" : "") + "\r\n";
    show(session, out);
    QSignalSpy changed(w.view, &TerminalView::findChanged);
    QCOMPARE(w.view->find(u"zażółć"_s, false), 4);
    QCOMPARE(w.view->findText(), u"zażółć"_s);
    QCOMPARE(w.view->findCount(), 4);
    // The newest first, scrolled to.
    QCOMPARE(w.view->findIndex(), 3);
    QVERIFY(changed.size() >= 1);
    QVERIFY(w.view->visibleText().contains(u"wiersz 150 Zażółć"_s));
    // Older ones: up through the scrollback, around.
    w.view->findOlder();
    w.view->findOlder();
    w.view->findOlder();
    QCOMPARE(w.view->findIndex(), 0);
    QVERIFY(w.view->visibleText().contains(u"wiersz 0 Zażółć"_s));
    w.view->findOlder();
    QCOMPARE(w.view->findIndex(), 3);
    w.view->findNewer();
    QCOMPARE(w.view->findIndex(), 0);
    // Letter case.
    QCOMPARE(w.view->find(u"zażółć"_s, true), 0);
    QCOMPARE(w.view->findIndex(), -1);
    QCOMPARE(w.view->find(u"Zażółć"_s, true), 4);
    // New output: found again a moment later, the current match kept.
    w.view->findOlder();
    QCOMPARE(w.view->findIndex(), 2);
    show(session, "nowy Zażółć\r\n");
    QTRY_COMPARE(w.view->findCount(), 5);
    QCOMPARE(w.view->findIndex(), 2);
    QVERIFY(!w.window.grabWindow().isNull());
    // Over.
    w.view->clearFind();
    QCOMPARE(w.view->findCount(), 0);
    QCOMPARE(w.view->findText(), QString());
}

void TestTerminal::viewClear()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    QByteArray out;
    for (int i = 0; i < 100; ++i)
        out += "wiersz " + QByteArray::number(i) + "\r\n";
    show(session, out + "$ ls -la");
    w.view->scrollLines(5);
    w.view->selectAll();
    QVERIFY(w.view->historySize() > 0);
    QSignalSpy cleared(&session, &TerminalSession::cleared);
    w.view->clear();
    QCOMPARE(cleared.size(), 1);
    QCOMPARE(w.view->historySize(), 0);
    QCOMPARE(w.view->scrollOffset(), 0);
    QVERIFY(!w.view->hasSelection());
    QVERIFY(w.view->visibleText().startsWith(u"$ ls -la"_s));
    QCOMPARE(w.view->visibleText().trimmed(), u"$ ls -la"_s);
}

void TestTerminal::viewReflow()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    const int columns = w.view->columns();
    show(session, QByteArray(columns + 10, 'x') + "\r\n$ ");
    w.view->selectAll();
    QVERIFY(w.view->hasSelection());
    // Narrower: the long line wraps again (nothing of it cut); the selection's lines are others now.
    w.view->setWidth(w.view->width() / 2);
    QTRY_VERIFY(w.view->columns() < columns);
    QVERIFY(!w.view->hasSelection());
    QCOMPARE(session.terminal().screen().text().count(u'x'), columns + 10);
    QCOMPARE(session.terminal().screen().line(session.terminal().screen().cursorRow()).trimmedText(), u"$"_s);
    // Wider again: one row of it.
    w.view->setWidth(w.window.width());
    QTRY_COMPARE(w.view->columns(), columns);
    QCOMPARE(session.terminal().screen().line(1).trimmedText(), QString(10, u'x'));
}

void TestTerminal::viewAttachesAfterOutput()
{
    // Output while no view shows the session (another tab, another thread): a view shown then and
    // scrolled back keeps its lines when the next one comes.
    TerminalSession session;
    Window w;
    // The session already at the view's size: attaching resizes nothing (a resize pulls lines back
    // out of the history, and how many depends on the font the platform gives the view — Nix's
    // aarch64 sandbox gave another one).
    QTRY_VERIFY(w.view->columns() > 0 && w.view->rows() > 0);
    session.resize(w.view->columns(), w.view->rows());
    QByteArray lines;
    for (int i = 0; i < 2000; ++i)
        lines += "wiersz " + QByteArray::number(i) + "\r\n";
    show(session, lines);
    w.view->setSession(&session);
    w.view->scrollLines(5);
    const QString before = w.view->visibleText();
    show(session, "jeszcze jeden\r\n");
    QCOMPARE(w.view->scrollOffset(), 6);
    QCOMPARE(w.view->visibleText(), before);
    // A selection made now survives the next line.
    w.view->selectAll();
    show(session, "i jeszcze\r\n");
    QVERIFY(w.view->hasSelection());
}

void TestTerminal::viewContextMenu()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    show(session, "tekst do menu\r\n");
    QSignalSpy menu(w.view, &TerminalView::contextMenuRequested);
    const QPointF at = w.cell(3, 0);
    w.mouse(QEvent::MouseButtonPress, at, Qt::RightButton, Qt::RightButton, m_time += 1000);
    w.mouse(QEvent::MouseButtonRelease, at, Qt::RightButton, Qt::NoButton, m_time += 10);
    QCOMPARE(menu.size(), 1);
    QCOMPARE(menu.at(0).at(0).toReal(), at.x());
    QCOMPARE(menu.at(0).at(1).toReal(), at.y());
    // A program that asked for the mouse gets the right button; Shift bypasses it.
    show(session, "\x1b[?1000h");
    w.mouse(QEvent::MouseButtonPress, at, Qt::RightButton, Qt::RightButton, m_time += 1000);
    w.mouse(QEvent::MouseButtonRelease, at, Qt::RightButton, Qt::NoButton, m_time += 10);
    QCOMPARE(menu.size(), 1);
    w.mouse(QEvent::MouseButtonPress, at, Qt::RightButton, Qt::RightButton, m_time += 1000, Qt::ShiftModifier);
    w.mouse(QEvent::MouseButtonRelease, at, Qt::RightButton, Qt::NoButton, m_time += 10, Qt::ShiftModifier);
    QCOMPARE(menu.size(), 2);
}

void TestTerminal::chatBlock_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("folder");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("block");
    const QString home = QDir::homePath();
    QTest::newRow("in home") << u"zsh"_s << home + u"/projekt"_s << u"\n$ ls   \nzażółć.txt\n\n\n"_s
                             << u"Terminal output (zsh, in ~/projekt):\n```console\n$ ls\nzażółć.txt\n```\n"_s;
    QTest::newRow("home itself") << u"bash"_s << home << u"x"_s
                                 << u"Terminal output (bash, in ~):\n```console\nx\n```\n"_s;
    QTest::newRow("elsewhere") << u"npm run dev"_s << u"/srv/app"_s << u"ready"_s
                               << u"Terminal output (npm run dev, in /srv/app):\n```console\nready\n```\n"_s;
    QTest::newRow("no folder") << u"sh"_s << QString() << u"ok"_s << u"Terminal output (sh):\n```console\nok\n```\n"_s;
    QTest::newRow("backticks inside") << u"sh"_s << QString() << u"a ``` b ```` c"_s
                                      << u"Terminal output (sh):\n`````console\na ``` b ```` c\n`````\n"_s;
    QTest::newRow("blank") << u"sh"_s << home << u" \n\n  \n"_s << QString();
    QTest::newRow("empty") << u"sh"_s << home << QString() << QString();
}

void TestTerminal::chatBlock()
{
    QFETCH(QString, title);
    QFETCH(QString, folder);
    QFETCH(QString, text);
    QFETCH(QString, block);
    QCOMPARE(TerminalView::chatBlock(title, folder, text), block);
}

void TestTerminal::viewKeys()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Window w;
    TerminalSession session;
    session.setCommand(u"cat"_s);
    w.view->setSession(&session);
    QCOMPARE(session.columns(), w.view->columns());
    QCOMPARE(session.rows(), w.view->rows());
    QVERIFY(session.start());
    QTRY_VERIFY(w.view->hasActiveFocus());

    QTest::keyClick(&w.window, 'a');
    QTest::keyClick(&w.window, 'b');
    QTest::keyClick(&w.window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"ab"_s), 2, Timeout);
    // Tab goes to the program; the focus stays.
    QTest::keyClick(&w.window, 'x');
    QTest::keyClick(&w.window, Qt::Key_Tab);
    QTest::keyClick(&w.window, 'y');
    QTest::keyClick(&w.window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"x       y"_s), 2, Timeout);
    QVERIFY(w.view->hasActiveFocus());

    // Polish letters as a layout types them, and an input method's committed text.
    QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, u"ż"_s);
    QGuiApplication::sendEvent(&w.window, &press);
    QInputMethodEvent compose(u"ę"_s, {});
    QGuiApplication::sendEvent(w.view, &compose);
    QVERIFY(w.view->inputMethodQuery(Qt::ImEnabled).toBool());
    QInputMethodEvent commit;
    commit.setCommitString(u"ę"_s);
    QGuiApplication::sendEvent(w.view, &commit);
    QTest::keyClick(&w.window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(screenText(session).count(u"żę"_s), 2, Timeout);

    // Ctrl+Shift+C copies and sends nothing; Ctrl+C is ^C.
    w.view->selectAll();
    QVERIFY(w.view->hasSelection());
    QTest::keyClick(&w.window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(QGuiApplication::clipboard()->text().contains(u"ab\nab"_s));
    QVERIFY(session.isRunning());
    QSignalSpy exited(&session, &TerminalSession::exited);
    QTest::keyClick(&w.window, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(session.exitCode(), 128 + 2); // SIGINT
}

void TestTerminal::viewSelection()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    show(session, "hello world-foo/bar baz\r\nzażółć gęślą jaźń\r\n");
    const int columns = w.view->columns();
    QVERIFY(columns >= 40);

    // Dragging over "hello".
    w.drag(w.edge(0, 0), w.edge(5, 0), m_time += 1000);
    QVERIFY(w.view->hasSelection());
    QCOMPARE(w.view->selectedText(), u"hello"_s);
    // A double click: the word, with what paths are made of.
    w.click(w.cell(8, 0), m_time += 1000);
    w.click(w.cell(8, 0), m_time += 20);
    QCOMPARE(w.view->selectedText(), u"world-foo/bar"_s);
    // A triple click: the line.
    w.click(w.cell(8, 0), m_time += 1000);
    w.click(w.cell(8, 0), m_time += 20);
    w.click(w.cell(8, 0), m_time += 20);
    QCOMPARE(w.view->selectedText(), u"hello world-foo/bar baz"_s);
    // Polish words.
    w.click(w.cell(9, 1), m_time += 1000);
    w.click(w.cell(9, 1), m_time += 20);
    QCOMPARE(w.view->selectedText(), u"gęślą"_s);
    // Copy.
    w.view->copy();
    QCOMPARE(QGuiApplication::clipboard()->text(), u"gęślą"_s);
    // A click clears it.
    w.click(w.cell(2, 3), m_time += 1000);
    QVERIFY(!w.view->hasSelection());

    // A line wrapped at the right margin is one line for the triple click and for copying.
    show(session, QByteArray(columns + 5, 'x') + "\r\n");
    const int row = session.terminal().screen().cursorRow() - 2;
    w.click(w.cell(1, row), m_time += 1000);
    w.click(w.cell(1, row), m_time += 20);
    w.click(w.cell(1, row), m_time += 20);
    QCOMPARE(w.view->selectedText(), QString(columns + 5, u'x'));
    // Across lines: joined with a line break, trailing blanks dropped.
    w.drag(w.edge(6, 0), w.edge(3, 1), m_time += 1000);
    QCOMPARE(w.view->selectedText(), u"world-foo/bar baz\nzaż"_s);
    // New output over the selection ends it.
    show(session, "\x1b[2;1Hnowy");
    QVERIFY(!w.view->hasSelection());
    // Select all, clear.
    w.view->selectAll();
    QVERIFY(w.view->selectedText().startsWith(u"hello world"_s));
    w.view->clearSelection();
    QVERIFY(!w.view->hasSelection());
    QCOMPARE(w.view->selectedText(), QString());
}

void TestTerminal::viewScrollback()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    QByteArray lines;
    for (int i = 0; i < 200; ++i)
        lines += "line " + QByteArray::number(i) + "\r\n";
    show(session, lines);
    const int rows = w.view->rows();
    QVERIFY(rows > 5);
    QCOMPARE(w.view->historySize(), 200 - rows + 1);
    QCOMPARE(w.view->scrollOffset(), 0);
    QVERIFY(w.view->visibleText().startsWith(u"line "_s + QString::number(200 - rows + 1)));

    w.view->scrollLines(10);
    QCOMPARE(w.view->scrollOffset(), 10);
    const QString firstLine = u"line "_s + QString::number(200 - rows + 1 - 10);
    QVERIFY(w.view->visibleText().startsWith(firstLine + u"\n"_s));
    // New output while scrolled back: the same lines stay in view.
    show(session, "more\r\nmore\r\n");
    QCOMPARE(w.view->scrollOffset(), 12);
    QVERIFY(w.view->visibleText().startsWith(firstLine + u"\n"_s));
    // Shift+Page Up / Page Down, Shift+Home / End.
    QTest::keyClick(&w.window, Qt::Key_PageUp, Qt::ShiftModifier);
    QCOMPARE(w.view->scrollOffset(), 12 + rows - 1);
    QTest::keyClick(&w.window, Qt::Key_PageDown, Qt::ShiftModifier);
    QCOMPARE(w.view->scrollOffset(), 12);
    QTest::keyClick(&w.window, Qt::Key_Home, Qt::ShiftModifier);
    QCOMPARE(w.view->scrollOffset(), w.view->historySize());
    QVERIFY(w.view->visibleText().startsWith(u"line 0\n"_s));
    QTest::keyClick(&w.window, Qt::Key_End, Qt::ShiftModifier);
    QCOMPARE(w.view->scrollOffset(), 0);
    // The wheel: three lines a notch.
    w.wheel(w.cell(5, 5), 120);
    QCOMPARE(w.view->scrollOffset(), 3);
    w.wheel(w.cell(5, 5), -240);
    QCOMPARE(w.view->scrollOffset(), 0);
    // Out of range is clamped.
    w.view->setScrollOffset(100000);
    QCOMPARE(w.view->scrollOffset(), w.view->historySize());
    w.view->setScrollOffset(-5);
    QCOMPARE(w.view->scrollOffset(), 0);
    // The alternate screen has no scrollback: the wheel goes to the program as arrow keys.
    show(session, "\x1b[?1049h");
    QCOMPARE(w.view->historySize(), 0);
    w.wheel(w.cell(5, 5), 120);
    QCOMPARE(w.view->scrollOffset(), 0);
    show(session, "\x1b[?1049l");
    QVERIFY(w.view->historySize() > 0);
}

void TestTerminal::viewMouseReporting()
{
    Window w;
    TerminalSession session;
    w.view->setSession(&session);
    show(session, "clickable text\r\n");
    QVERIFY(!w.view->mouseReporting());
    QSignalSpy modes(w.view, &TerminalView::modesChanged);
    show(session, "\x1b[?1002;1006h");
    QVERIFY(w.view->mouseReporting());
    QVERIFY(modes.size() >= 1);
    // A drag goes to the program, not to the selection …
    w.drag(w.cell(0, 0), w.cell(5, 0), m_time += 1000);
    QVERIFY(!w.view->hasSelection());
    // … unless Shift is held.
    w.drag(w.edge(0, 0), w.edge(6, 0), m_time += 1000, Qt::ShiftModifier);
    QCOMPARE(w.view->selectedText(), u"clicka"_s);
    show(session, "\x1b[?1002l");
    QVERIFY(!w.view->mouseReporting());
}

void TestTerminal::viewPaintsAtOddSizes()
{
    TerminalSession session;
    show(session,
         "\x1b[1;31;44mred on blue\x1b[0m \x1b[4;3;1munderlined\x1b[0m \x1b[7minverse\x1b[0m \x1b[9mstrike\x1b[0m\r\n"
         "\x1b[38;2;10;200;30mtrue colour\x1b[38;5;200m 256\x1b[0m \xe4\xb8\xad\xe6\x96\x87 e\xcc\x81 "
         "\xf0\x9f\x98\x80\r\n"
         "\x1b]8;;https://example.com\x07link\x1b]8;;\x07 \x1b(0lqqk\x1b(B \x1b[2mdim\x1b[8mhidden\x1b[0m\r\n"
         "zażółć gęślą jaźń\x1b[5 q");
    const QList<QSizeF> sizes = {QSizeF(0, 0),   QSizeF(1, 1),     QSizeF(2, 3),     QSizeF(7, 5),
                                 QSizeF(33, 17), QSizeF(640, 400), QSizeF(3000, 20), QSizeF(20, 3000)};
    for (const bool dark : {true, false}) {
        TerminalView view;
        view.setDark(dark);
        view.setSession(&session);
        for (const QSizeF &size : sizes) {
            view.setSize(size);
            QImage image(std::max(1, int(size.width())), std::max(1, int(size.height())),
                         QImage::Format_ARGB32_Premultiplied);
            image.fill(view.fillColor());
            QPainter painter(&image);
            view.paint(&painter);
            painter.end();
        }
        view.selectAll();
        view.scrollLines(1);
        QImage image(640, 400, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        view.paint(&painter);
    }
    // In a window too, through the scene graph.
    Window w;
    w.view->setSession(&session);
    for (const QSizeF &size : sizes) {
        w.view->setSize(size);
        QCoreApplication::processEvents();
        const QImage grabbed = w.window.grabWindow();
        QVERIFY(!grabbed.isNull());
    }
    // Without a session.
    TerminalView empty;
    empty.setSize(QSizeF(100, 100));
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    empty.paint(&painter);
    QCOMPARE(empty.visibleText(), QString());
    QCOMPARE(empty.selectedText(), QString());
}

void TestTerminal::viewColors()
{
    TerminalSession session;
    show(session, "\x1b[41m  \x1b[0m\x1b[48;2;1;2;3m  \x1b[0m\x1b[48;5;196m  \x1b[0m");
    for (const bool dark : {true, false}) {
        TerminalView view;
        view.setDark(dark);
        view.setSession(&session);
        view.setSize(QSizeF(400, 200));
        QCOMPARE(view.ansiColors().size(), 16);
        QCOMPARE(view.ansiColors(), TerminalView::defaultAnsiColors(dark));
        QImage image(400, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(view.fillColor());
        QPainter painter(&image);
        view.paint(&painter);
        painter.end();
        auto at = [&](int column) {
            return image.pixelColor(int(view.padding() + (column + 0.5) * view.cellWidth()),
                                    int(view.padding() + view.cellHeight() / 2));
        };
        QCOMPARE(at(0), view.ansiColors().at(1));
        QCOMPARE(at(2), QColor(1, 2, 3));
        QCOMPARE(at(4), TerminalView::xtermColor(196));
        QCOMPARE(image.pixelColor(399, 199), view.background());
        QCOMPARE(view.fillColor(), view.background());
    }
    // Colours set, then reset.
    TerminalView view;
    view.setBackground(QColor(u"#102030"_s));
    QCOMPARE(view.background(), QColor(u"#102030"_s));
    QCOMPARE(view.fillColor(), QColor(u"#102030"_s));
    view.resetBackground();
    QCOMPARE(view.background(), TerminalView().background());
    QList<QColor> palette;
    for (int i = 0; i < 16; ++i)
        palette.append(QColor(i * 10, 0, 0));
    view.setAnsiColors(palette);
    QCOMPARE(view.ansiColors(), palette);
    view.setAnsiColors({QColor(1, 1, 1)}); // too few: the default palette
    QCOMPARE(view.ansiColors(), TerminalView::defaultAnsiColors(true));
    QCOMPARE(TerminalView::xtermColor(16), QColor(0, 0, 0));
    QCOMPARE(TerminalView::xtermColor(231), QColor(255, 255, 255));
    QCOMPARE(TerminalView::xtermColor(232), QColor(8, 8, 8));
    QCOMPARE(TerminalView::xtermColor(255), QColor(238, 238, 238));
}

void TestTerminal::viewLayout()
{
    TerminalView view;
    QFont font(u"monospace"_s);
    font.setPixelSize(14);
    view.setFont(font);
    QSignalSpy layout(&view, &TerminalView::layoutChanged);
    view.setSize(QSizeF(800, 600));
    QVERIFY(view.cellWidth() >= 1);
    QVERIFY(view.cellHeight() >= 1);
    QCOMPARE(view.columns(), int(std::floor((800 - 2 * view.padding()) / view.cellWidth())));
    QCOMPARE(view.rows(), int(std::floor((600 - 2 * view.padding()) / view.cellHeight())));
    QVERIFY(view.columns() > 40);
    QVERIFY(layout.size() >= 1);
    TerminalSession session;
    view.setSession(&session);
    QCOMPARE(session.columns(), view.columns());
    QCOMPARE(session.rows(), view.rows());
    // Too small for a cell: the program keeps its size.
    const int columns = view.columns();
    view.setSize(QSizeF(0, 0));
    QCOMPARE(view.columns(), columns);
    QCOMPARE(session.columns(), columns);
    view.setSize(QSizeF(400, 300));
    QVERIFY(view.columns() < columns);
    QCOMPARE(session.columns(), view.columns());
    view.setPadding(20);
    QCOMPARE(view.columns(), int(std::floor((400 - 40) / view.cellWidth())));
    // A bigger font: fewer cells.
    font.setPixelSize(28);
    const int before = view.columns();
    view.setFont(font);
    QVERIFY(view.columns() < before);
    QCOMPARE(session.columns(), view.columns());
}

namespace {

/// A view that counts its paints.
class CountingView : public TerminalView {
public:
    using TerminalView::TerminalView;
    int paints = 0;
    void paint(QPainter *painter) override
    {
        ++paints;
        TerminalView::paint(painter);
    }
};

} // namespace

void TestTerminal::viewFlood()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    // `yes | head -n 100000` in a shown view: every read changes the screen, the view paints once a
    // frame whatever piled up — no more paints than reads — and keeps up.
    QQuickWindow window;
    window.resize(800, 500);
    auto *view = new CountingView(window.contentItem());
    view->setSize(QSizeF(800, 500));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    TerminalSession session;
    view->setSession(&session);
    session.setCommand(u"yes | head -n 100000; echo done"_s);
    int reads = 0;
    connect(&session, &TerminalSession::contentChanged, this, [&reads] { ++reads; });
    QSignalSpy exited(&session, &TerminalSession::exited);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(session.start());
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout * 2);
    const qint64 elapsed = timer.elapsed();
    QCoreApplication::processEvents();
    qInfo("100000 lines: %lld ms, %d reads, %d paints", elapsed, reads, view->paints);
    QCOMPARE(session.exitCode(), 0);
    QVERIFY(screenText(session).contains(u"y\ndone"_s));
    QCOMPARE(session.terminal().screen().historySize(), 5000);
    QVERIFY(view->paints > 0);
    // At most a paint a read (a frame takes whatever reads came before it), never one a line. On a
    // fast machine far fewer (7 paints for 15 reads); a slow one reads in bigger chunks.
    QVERIFY2(view->paints <= reads + 2, "paints are coalesced to frames");

    // What a whole screen of colourful text costs to paint (htop's full redraws).
    TerminalSession full;
    QByteArray screen = "\x1b[H";
    for (int row = 0; row < 60; ++row) {
        for (int column = 0; column < 200; column += 10)
            screen += "\x1b[3" + QByteArray::number((row + column) % 8) + ";4" + QByteArray::number(column % 8)
                      + (column % 20 == 0 ? ";1" : "") + "m" + "zażółć ab ";
        if (row < 59)
            screen += "\r\n";
    }
    TerminalView big;
    big.setSession(&full);
    big.setSize(QSizeF(200 * big.cellWidth() + 2 * big.padding(), 60 * big.cellHeight() + 2 * big.padding()));
    show(full, screen);
    QCOMPARE(full.columns(), 200);
    QImage image(big.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    QElapsedTimer paintTimer;
    paintTimer.start();
    for (int i = 0; i < 10; ++i) {
        image.fill(big.fillColor());
        QPainter painter(&image);
        big.paint(&painter);
    }
    qInfo("a 200 x 60 screen of colourful text: %.1f ms a paint", paintTimer.nsecsElapsed() / 1e7);
}

// ---- Windows (ConPTY) ----------------------------------------------------------------------------

namespace {

using brulion::terminal::ShellKind;

ShellKind userShellKind()
{
    return brulion::terminal::shellKind(brulion::platform::Pty::userShell());
}

/// The screen and the scrollback, as one text.
QString allText(const TerminalSession &session)
{
    const brulion::term::Screen &screen = session.terminal().screen();
    QStringList lines;
    for (int i = -screen.historySize(); i < screen.rows(); ++i)
        lines.append(screen.lineText(i));
    return lines.join(u'\n');
}

} // namespace

void TestTerminal::windowsShellInFolder()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(dir.path()).absoluteFilePath(u"projekt żółw"_s);
    QVERIFY(QDir().mkpath(folder));
    const ShellKind kind = userShellKind();
    QVERIFY(kind != ShellKind::Posix); // (SHELL is not a path of Windows: PowerShell or cmd.exe)
    TerminalSession session;
    session.setFolder(folder);
    QSignalSpy exited(&session, &TerminalSession::exited);
    QVERIFY(session.start());
    QVERIFY(!session.title().isEmpty());
    QCOMPARE(session.commandLine().value(0), brulion::platform::Pty::userShell());
    if (kind == ShellKind::PowerShell)
        QCOMPARE(session.commandLine().mid(1), QStringList{u"-NoLogo"_s});
    // The folder on a line of its own (the prompt names it too).
    session.write(kind == ShellKind::PowerShell ? u"(Get-Location).Path\r"_s : u"cd\r"_s);
    const QString line = u'\n' + QDir::toNativeSeparators(folder) + u'\n';
    QTRY_VERIFY2_WITH_TIMEOUT(allText(session).contains(line, Qt::CaseInsensitive), qPrintable(allText(session)),
                              Timeout);
    session.write(u"exit 5\r"_s);
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QCOMPARE(session.exitCode(), 5);
}

void TestTerminal::windowsCommands()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    const bool powerShell = userShellKind() == ShellKind::PowerShell;
    // Output and an exit of its own; the tab's label over the console's title.
    TerminalSession echo;
    echo.setCommand(powerShell ? u"echo 'zażółć gęślą'; exit 3"_s : u"echo zażółć gęślą& exit 3"_s);
    echo.setLabel(u"Skrypt"_s);
    QSignalSpy exited(&echo, &TerminalSession::exited);
    QVERIFY(echo.start());
    if (powerShell)
        QCOMPARE(echo.commandLine().value(2), u"-EncodedCommand"_s);
    else
        QCOMPARE(echo.commandLine().last(), u"/d /s /c \""_s + echo.command() + u'"');
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QVERIFY2(allText(echo).contains(u"zażółć gęślą"_s), qPrintable(allText(echo)));
    QCOMPARE(echo.exitCode(), 3);
    QCOMPARE(echo.title(), u"Skrypt"_s);

    // A native program's exit code comes through the shell as it is.
    TerminalSession native;
    native.setCommand(u"cmd /d /c exit 5"_s);
    QSignalSpy nativeExited(&native, &TerminalSession::exited);
    QVERIFY(native.start());
    QTRY_COMPARE_WITH_TIMEOUT(nativeExited.size(), 1, Timeout);
    QCOMPARE(native.exitCode(), 5);

    // A command that is not there fails; one that works ends with 0.
    TerminalSession missing;
    missing.setCommand(u"brulion-no-such-command-xyz"_s);
    QSignalSpy missingExited(&missing, &TerminalSession::exited);
    QVERIFY(missing.start());
    QTRY_COMPARE_WITH_TIMEOUT(missingExited.size(), 1, Timeout);
    QVERIFY(missing.exitCode() != 0);
    if (powerShell)
        QCOMPARE(missing.exitCode(), 1);
    TerminalSession fine;
    fine.setCommand(u"echo gotowe"_s);
    QSignalSpy fineExited(&fine, &TerminalSession::exited);
    QVERIFY(fine.start());
    QTRY_COMPARE_WITH_TIMEOUT(fineExited.size(), 1, Timeout);
    QCOMPARE(fine.exitCode(), 0);
}

void TestTerminal::windowsStopAndRestart()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    const bool powerShell = userShellKind() == ShellKind::PowerShell;
    TerminalSession session;
    session.setCommand(powerShell ? u"echo started; Start-Sleep 100"_s : u"echo started& ping -n 100 127.0.0.1 >NUL"_s);
    QSignalSpy exited(&session, &TerminalSession::exited);
    QVERIFY(session.start());
    QTRY_VERIFY_WITH_TIMEOUT(allText(session).contains(u"started"_s), Timeout);
    QElapsedTimer timer;
    timer.start();
    session.stop();
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, Timeout);
    QVERIFY(timer.elapsed() < 6000 * BRULION_TEST_SLOWDOWN);
    QVERIFY(session.hasExited());
    qInfo("stopped: exit code 0x%x", quint32(session.exitCode()));
    // Restart: a clean screen, the same command again.
    QVERIFY(session.start());
    QTRY_VERIFY_WITH_TIMEOUT(allText(session).contains(u"started"_s), Timeout);
    QCOMPARE(allText(session).count(u"started"_s), 1);
    session.stop();
    QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 2, Timeout);
}

QTEST_MAIN(TestTerminal)
#include "tst_terminal.moc"
