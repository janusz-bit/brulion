// The terminal panel's C++ (src/app/terminal): TerminalSession end to end on a real
// pseudo-terminal — input echoed by cat, a paste, the shell started in its folder printing pwd, the
// size the program sees, the title it sets, stop and restart, exit codes —, the tabs' model, and
// TerminalView in a window: keys into the program (Ctrl+C is ^C; Ctrl+Shift+C copies), the input
// method's text, selection by dragging and by double and triple clicks, copying, the scrollback
// and its keys and wheel, mouse reporting, painting at odd sizes and in both palettes.
#include "terminalsession.h"
#include "terminalsessionmodel.h"
#include "terminalview.h"

#include "term/screen.h"

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::terminal::TerminalSession;
using brulion::terminal::TerminalSessionModel;
using brulion::terminal::TerminalView;

namespace {

constexpr int Timeout = 15000 * BRULION_TEST_SLOWDOWN;

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
    void availability();
    void echoThroughCat();
    void shellInFolder();
    void sizeOfThePty();
    void titleFromProgram();
    void stopAndRestart();
    void exitCodes();
    void sessionModel();
    void viewKeys();
    void viewSelection();
    void viewScrollback();
    void viewMouseReporting();
    void viewPaintsAtOddSizes();
    void viewColors();
    void viewLayout();
    void viewFlood();

private:
    bool m_posix = false;
    ulong m_time = 1000;
};

void TestTerminal::initTestCase()
{
    m_posix = QByteArrayView(BRULION_TEST_PTY_BACKEND) == "posix";
    // A shell whose startup prints nothing of the user's own.
    qputenv("SHELL", "/bin/sh");
}

void TestTerminal::availability()
{
    QCOMPARE(TerminalSession::available(), m_posix);
    QCOMPARE(TerminalSessionModel::available(), m_posix);
    if (!m_posix) {
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

void TestTerminal::sessionModel()
{
    TerminalSessionModel model;
    QSignalSpy count(&model, &TerminalSessionModel::countChanged);
    QCOMPARE(model.add(u"/tmp"_s, QString(), u"Powłoka"_s), 0);
    QCOMPARE(model.add(u"/tmp"_s, u"echo hi"_s, QString()), 1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(count.size(), 2);
    QCOMPARE(model.data(model.index(0), TerminalSessionModel::TitleRole).toString(), u"Powłoka"_s);
    QCOMPARE(model.data(model.index(1), TerminalSessionModel::TitleRole).toString(), u"echo hi"_s);
    QCOMPARE(model.at(0)->folder(), u"/tmp"_s);
    QCOMPARE(model.at(1)->command(), u"echo hi"_s);
    QCOMPARE(model.at(2), nullptr);
    QCOMPARE(model.at(-1), nullptr);
    QCOMPARE(model.indexOf(model.at(1)), 1);
    QVERIFY(model.roleNames().values().contains("session"));
    QCOMPARE(model.data(model.index(0), TerminalSessionModel::SessionRole).value<TerminalSession *>(), model.at(0));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.at(1)->setLabel(u"Echo"_s);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(model.data(model.index(1), TerminalSessionModel::TitleRole).toString(), u"Echo"_s);
    if (m_posix) {
        QVERIFY(model.at(1)->start());
        QTRY_VERIFY_WITH_TIMEOUT(model.data(model.index(1), TerminalSessionModel::HasExitedRole).toBool(), Timeout);
        QCOMPARE(model.data(model.index(1), TerminalSessionModel::ExitCodeRole).toInt(), 0);
    }

    QPointer<TerminalSession> first = model.at(0);
    model.remove(0);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.at(0)->command(), u"echo hi"_s);
    QTRY_VERIFY(first.isNull()); // deleted once the event loop is back
    model.remove(5);
    QCOMPARE(model.count(), 1);
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

QTEST_MAIN(TestTerminal)
#include "tst_terminal.moc"
