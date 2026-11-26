// A program on a pseudo-terminal (src/platform/pty.h) with this build's backend: output of
// /bin/sh, input through cat, `stty size` before and after a resize, the working directory and the
// environment the child gets, no descriptor of the parent's files in the child, exit codes and
// signals, programs that cannot be started, terminate() of a `sleep 100` with its background job
// and of one that ignores SIGHUP, a large paste into a slow reader, whether a job of the shell's is
// in the terminal's foreground, the foreground program's current directory — and every child reaped
// (no zombies), also when the Pty is deleted while its program runs.
//
// Windows (ConPTY): the same through pty_probe (tests/platform/pty_probe.cpp: a console program that
// prints what it sees) and cmd.exe, the output read as the screen ConPTY paints — through the
// emulator, which also answers ConPTY's queries — the command line's quoting and cmd.exe's native
// arguments, the environment block, no inherited handle, terminate() of a process tree (a child on
// the console, one without a console, one that holds the console's close up), a program that ends
// by itself leaving a child without a console running, deletion while running, a large paste and a
// flood of output.
#include "pty.h"

#include "term/terminal.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Qt::StringLiterals;
using brulion::platform::Pty;

namespace {

constexpr int Timeout = 15000 * BRULION_TEST_SLOWDOWN;
/// POSIX signal numbers (Linux, macOS and the BSDs agree), named where <csignal> may lack them.
constexpr int SigHup = 1;
constexpr int SigKill = 9;
constexpr int SigTerm = 15;

/// A Pty that collects its output and its end.
struct Run {
    Pty pty;
    QByteArray output;
    bool done = false;
    int exitCode = -2;
    bool crashed = false;

    Run()
    {
        QObject::connect(&pty, &Pty::dataReceived, &pty, [this](const QByteArray &data) { output += data; });
        QObject::connect(&pty, &Pty::finished, &pty, [this](int code, bool signal) {
            done = true;
            exitCode = code;
            crashed = signal;
        });
    }
    bool start(const QString &program, const QStringList &arguments, const QString &cwd = QString(),
               QStringList environment = Pty::defaultEnvironment(), int columns = 80, int rows = 24)
    {
        QString error;
        const bool ok = pty.start(program, arguments, cwd, environment, columns, rows, &error);
        if (!ok)
            qWarning("start failed: %s", qPrintable(error));
        return ok;
    }
    bool sh(const QString &script, const QString &cwd = QString(), QStringList environment = Pty::defaultEnvironment())
    {
        return start(u"/bin/sh"_s, {u"-c"_s, script}, cwd, environment);
    }
};

/// The process is gone and reaped: not even a zombie answers.
bool reaped(qint64 pid)
{
#ifdef Q_OS_UNIX
    return ::kill(pid_t(pid), 0) != 0 && errno == ESRCH;
#else
    Q_UNUSED(pid);
    return true;
#endif
}

#ifdef Q_OS_WIN
/// Whether the process `pid` has ended (waiting up to `ms` for it).
bool processEnded(qint64 pid, int ms = 0)
{
    const HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process)
        return true; // (no such process any more)
    const bool ended = ::WaitForSingleObject(process, DWORD(ms)) == WAIT_OBJECT_0;
    ::CloseHandle(process);
    return ended;
}

void killProcess(qint64 pid)
{
    if (const HANDLE process = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(pid))) {
        ::TerminateProcess(process, 1);
        ::WaitForSingleObject(process, 5000);
        ::CloseHandle(process);
    }
}
#endif

/// A Pty whose output goes through the emulator, as in a terminal session: ConPTY paints a screen
/// (its output is the sequences that draw it, not the program's bytes), and its queries are answered.
struct ScreenRun : Run {
    static constexpr int Columns = 200;
    static constexpr int Rows = 50;
    brulion::term::Terminal terminal{Columns, Rows, 2000};

    ScreenRun()
    {
        QObject::connect(&pty, &Pty::dataReceived, &pty, [this](const QByteArray &data) { terminal.feed(data); });
        QObject::connect(&terminal, &brulion::term::Terminal::reply, &pty,
                         [this](const QByteArray &data) { pty.write(data); });
    }
    /// The scrollback and the screen, a line each, trailing spaces dropped.
    QString text() const
    {
        const brulion::term::Screen &screen = terminal.screen();
        QStringList lines;
        for (int i = -screen.historySize(); i < screen.rows(); ++i)
            lines.append(screen.lineText(i));
        return lines.join(u'\n');
    }
    bool probe(const QStringList &arguments, const QString &cwd = QString(),
               const QStringList &environment = Pty::defaultEnvironment())
    {
#ifdef BRULION_TEST_PTY_PROBE
        return start(QString::fromUtf8(BRULION_TEST_PTY_PROBE), arguments, cwd, environment, Columns, Rows);
#else
        Q_UNUSED(arguments)
        Q_UNUSED(cwd)
        Q_UNUSED(environment)
        return false;
#endif
    }
    /// The number after "<word> " in the text ("child 1234"), 0 when there is none yet.
    qint64 numberAfter(const QString &word) const
    {
        const QString all = text();
        const qsizetype at = all.indexOf(word + u' ');
        if (at < 0)
            return 0;
        const QString rest = all.mid(at + word.size() + 1);
        qsizetype end = 0;
        while (end < rest.size() && rest.at(end).isDigit())
            ++end;
        return end > 0 && end < rest.size() ? rest.left(end).toLongLong() : 0;
    }
};

} // namespace

class TestPty : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void availability();
    void output();
    void sizeAndResize();
    void inputThroughCat();
    void workingDirectory();
    void environment();
    void noInheritedDescriptors();
    void exitBySignal();
    void cannotStart();
    void terminateSleep();
    void terminateIgnoringHangup();
    void deletedWhileRunning();
    void largePaste();
    void foregroundJob();
    void currentDirectory();
    void notRunning();

    // Windows (ConPTY)
    void conptyOutput();
    void conptyExitCodes();
    void conptyArguments();
    void conptySizeAndResize();
    void conptyInput();
    void conptyWorkingDirectory();
    void conptyEnvironment();
    void conptyNoInheritedHandles();
    void conptyCannotStart();
    void conptyTerminateTree();
    void conptyTerminateIgnoringClose();
    void conptyChildWithoutConsoleStays();
    void conptyDeletedWhileRunning();
    void conptyLargePaste();
    void conptyFlood();

private:
    bool m_posix = false;
    bool m_conpty = false;
};

void TestPty::initTestCase()
{
    m_posix = QByteArrayView(BRULION_TEST_PTY_BACKEND) == "posix";
    m_conpty = QByteArrayView(BRULION_TEST_PTY_BACKEND) == "conpty";
}

void TestPty::availability()
{
    QCOMPARE(Pty::available(), m_posix || m_conpty);
    if (!m_posix && !m_conpty) {
        Pty pty;
        QString error;
        QVERIFY(!pty.start(u"/bin/sh"_s, {}, {}, {}, 80, 24, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(Pty::userShell().isEmpty());
        return;
    }
    QVERIFY(QFileInfo(Pty::userShell()).isExecutable());
    QVERIFY(!Pty::defaultEnvironment().isEmpty());
}

void TestPty::output()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(run.sh(u"printf 'zażółć gęślą jaźń\\n'; exit 3"_s));
    QVERIFY(run.pty.isRunning());
    const qint64 pid = run.pty.processId();
    QVERIFY(pid > 0);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    // The line discipline's ONLCR: LF arrives as CR LF.
    QVERIFY2(run.output.contains(u"zażółć gęślą jaźń\r\n"_s.toUtf8()), run.output.constData());
    QCOMPARE(run.exitCode, 3);
    QVERIFY(!run.crashed);
    QVERIFY(!run.pty.isRunning());
    QCOMPARE(run.pty.processId(), 0);
    QVERIFY(reaped(pid));
}

void TestPty::sizeAndResize()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(run.start(u"/bin/sh"_s, {u"-c"_s, u"stty size; read x; stty size"_s}, {}, Pty::defaultEnvironment(), 80,
                      24));
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("24 80"), Timeout);
    QVERIFY(run.pty.resize(132, 43));
    QVERIFY(run.pty.write("\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("43 132"), Timeout);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QCOMPARE(run.exitCode, 0);
}

void TestPty::inputThroughCat()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(run.start(u"cat"_s, {}));
    QVERIFY(run.pty.write(u"zażółć\r"_s.toUtf8()));
    // The echo of the line, then cat's copy of it.
    QTRY_VERIFY_WITH_TIMEOUT(run.output.count(u"zażółć\r\n"_s.toUtf8()) == 2, Timeout);
    // Ctrl+D at the start of a line: end of input.
    QVERIFY(run.pty.write("\x04"));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QCOMPARE(run.exitCode, 0);
    QVERIFY(!run.crashed);
}

void TestPty::workingDirectory()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath(u"projekt żółw"_s);
    QVERIFY(QDir().mkpath(folder));
    Run run;
    QVERIFY(run.sh(u"pwd; echo \"PWD=$PWD\""_s, folder));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    const QString out = QString::fromUtf8(run.output);
    QVERIFY2(out.contains(QDir(folder).absolutePath() + u"\r\n"_s)
                     || out.contains(QDir(folder).canonicalPath() + u"\r\n"_s),
             qPrintable(out));
    QVERIFY2(out.contains(u"PWD="_s + QDir(folder).absolutePath()), qPrintable(out));
    // A missing folder: the home folder.
    Run home;
    QVERIFY(home.sh(u"pwd"_s, dir.filePath(u"missing"_s)));
    QTRY_VERIFY_WITH_TIMEOUT(home.done, Timeout);
    QVERIFY2(home.output.contains(QFile::encodeName(QDir::homePath()))
                     || home.output.contains(QFile::encodeName(QDir(QDir::homePath()).canonicalPath())),
             home.output.constData());
}

void TestPty::environment()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QStringList env = Pty::defaultEnvironment();
    env.append(u"COLUMNS=5"_s);
    env.append(u"LINES=5"_s);
    env.append(u"TERM=dumb"_s);
    env.append(u"BRULION_TEST_VAR=ąę"_s);
    Run run;
    QVERIFY(run.sh(u"echo \"T=$TERM C=$COLORTERM COLS=${COLUMNS-unset} LINES=${LINES-unset} X=$BRULION_TEST_VAR\""_s,
                   {}, env));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY2(run.output.contains(u"T=xterm-256color C=truecolor COLS=unset LINES=unset X=ąę"_s.toUtf8()),
             run.output.constData());

    // No locale at all: a UTF-8 one is named.
    Run bare;
    QVERIFY(bare.sh(u"echo \"L=$LC_CTYPE\""_s, {}, {u"PATH=/run/current-system/sw/bin:/usr/bin:/bin"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(bare.done, Timeout);
    QVERIFY2(bare.output.contains("L=C.UTF-8") || bare.output.contains("L=UTF-8"), bare.output.constData());
}

void TestPty::noInheritedDescriptors()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    if (!QFileInfo::exists(u"/proc/self/fd"_s))
        QSKIP("no /proc");
    QTemporaryFile marker(QDir::tempPath() + u"/brulion-pty-marker-XXXXXX"_s);
    QVERIFY(marker.open()); // an open descriptor of the parent's
    const QString name = QFileInfo(marker.fileName()).fileName();
    Run run;
    QVERIFY(run.sh(u"for f in /proc/$$/fd/*; do echo \"$f -> $(readlink \"$f\")\"; done"_s));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    const QString out = QString::fromUtf8(run.output);
    QVERIFY2(out.contains(u"/fd/0 -> /dev/pts/"_s), qPrintable(out));
    QVERIFY2(!out.contains(name), qPrintable(out));
    QVERIFY2(!out.contains(u"/dev/ptmx"_s), qPrintable(out));
}

void TestPty::exitBySignal()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(run.sh(u"kill -TERM $$"_s));
    const qint64 pid = run.pty.processId();
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY(run.crashed);
    QCOMPARE(run.exitCode, SigTerm);
    QVERIFY(reaped(pid));
}

void TestPty::cannotStart()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Pty pty;
    QSignalSpy finished(&pty, &Pty::finished);
    QString error;
    QVERIFY(!pty.start(u"/nonexistent/program"_s, {}, {}, Pty::defaultEnvironment(), 80, 24, &error));
    QVERIFY(error.contains(u"/nonexistent/program"_s));
    error.clear();
    QVERIFY(!pty.start(u"brulion-no-such-program-xyz"_s, {}, {}, Pty::defaultEnvironment(), 80, 24, &error));
    QVERIFY(!error.isEmpty());
    // Executable but no program: exec() itself refuses it, and says why.
    QTemporaryDir dir;
    const QString junk = dir.filePath(u"junk"_s);
    QFile file(junk);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\x7f"
               "ELF garbage that is not a program",
               30);
    file.close();
    QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    error.clear();
    QVERIFY(!pty.start(junk, {}, {}, Pty::defaultEnvironment(), 80, 24, &error));
    QVERIFY2(error.contains(u"junk"_s), qPrintable(error));
    QVERIFY(!pty.isRunning());
    QCOMPARE(finished.size(), 0);
    // The Pty still works afterwards.
    QVERIFY(pty.start(u"/bin/sh"_s, {u"-c"_s, u"exit 7"_s}, {}, Pty::defaultEnvironment(), 80, 24, &error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, Timeout);
    QCOMPARE(finished.at(0).at(0).toInt(), 7);
    // And starts again after its program ended.
    QVERIFY(pty.start(u"/bin/sh"_s, {u"-c"_s, u"exit 8"_s}, {}, Pty::defaultEnvironment(), 80, 24, &error));
    QVERIFY(!pty.start(u"/bin/sh"_s, {}, {}, Pty::defaultEnvironment(), 80, 24, &error)); // one at a time
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, Timeout);
    QCOMPARE(finished.at(1).at(0).toInt(), 8);
}

void TestPty::terminateSleep()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    // A background job of the shell (no job control: the same process group) and the shell waiting.
    QVERIFY(run.sh(u"sleep 100 & echo \"job=$!\"; wait"_s));
    const qint64 pid = run.pty.processId();
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("job="), Timeout);
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("\r\n"), Timeout);
    const QByteArray line = run.output.mid(run.output.indexOf("job=") + 4);
    const qint64 job = line.left(line.indexOf('\r')).toLongLong();
    QVERIFY(job > 0);
    QElapsedTimer timer;
    timer.start();
    run.pty.terminate(5000);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY(run.crashed);
    QCOMPARE(run.exitCode, SigHup);
    QVERIFY2(timer.elapsed() < 4000 * BRULION_TEST_SLOWDOWN, "SIGHUP ended it, not the SIGKILL after the grace period");
    QVERIFY(reaped(pid));
    // The job got the hangup too (init reaps it).
    QTRY_VERIFY_WITH_TIMEOUT(reaped(job), Timeout);
}

void TestPty::terminateIgnoringHangup()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(run.sh(u"trap '' HUP; echo ready; sleep 100"_s));
    const qint64 pid = run.pty.processId();
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("ready"), Timeout);
    run.pty.terminate(300);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY(run.crashed);
    QCOMPARE(run.exitCode, SigKill);
    QVERIFY(reaped(pid));
}

void TestPty::deletedWhileRunning()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    auto *pty = new Pty;
    QVERIFY(pty->start(u"sleep"_s, {u"100"_s}, {}, Pty::defaultEnvironment(), 80, 24));
    const qint64 pid = pty->processId();
    QVERIFY(pid > 0);
    delete pty;
    // The destructor waited for it.
    QVERIFY(reaped(pid));

    // One that ignores SIGHUP is killed.
    auto *stubborn = new Pty;
    QByteArray out;
    connect(stubborn, &Pty::dataReceived, this, [&out](const QByteArray &data) { out += data; });
    QVERIFY(stubborn->start(u"/bin/sh"_s, {u"-c"_s, u"trap '' HUP; echo ready; sleep 100"_s}, {},
                            Pty::defaultEnvironment(), 80, 24));
    const qint64 stubbornPid = stubborn->processId();
    QTRY_VERIFY_WITH_TIMEOUT(out.contains("ready"), Timeout);
    delete stubborn;
    QVERIFY(reaped(stubbornPid));
}

void TestPty::largePaste()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(run.sh(u"stty -echo; echo ready; wc -c; echo done"_s));
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("ready"), Timeout);
    // 1 MB in lines of 100: far more than the pty's buffer, queued while it is full.
    QByteArray paste;
    for (int i = 0; i < 10000; ++i)
        paste += QByteArray(99, 'a') + '\r';
    QVERIFY(run.pty.write(paste));
    QVERIFY(run.pty.write("\x04"));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout * 2);
    QVERIFY2(run.output.contains("1000000"), run.output.right(200).constData());
    QVERIFY(run.output.contains("done"));
    QCOMPARE(run.exitCode, 0);
}

void TestPty::foregroundJob()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    Run run;
    QVERIFY(!run.pty.hasForegroundJob());
    // Job control on: the shell runs `sleep` in a process group of its own, in the foreground.
    QVERIFY(run.sh(u"set -m; echo start; sleep %1; echo back; read answer; echo \"got $answer\""_s.arg(
            3 * BRULION_TEST_SLOWDOWN)));
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("start"), Timeout);
    QTRY_VERIFY_WITH_TIMEOUT(run.pty.hasForegroundJob(), Timeout);
    // The job ended; the shell reads a line itself: nothing but the shell in the foreground.
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("back"), Timeout);
    QTRY_VERIFY_WITH_TIMEOUT(!run.pty.hasForegroundJob(), Timeout);
    QVERIFY(run.pty.write("tak\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY(run.output.contains("got tak"));
    QVERIFY(!run.pty.hasForegroundJob());
    QVERIFY(run.pty.currentDirectory().isEmpty());
}

void TestPty::currentDirectory()
{
    if (!m_posix)
        QSKIP("no pseudo-terminal in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(u"zażółć/src"_s));
    const QString inner = QDir(dir.path()).canonicalPath() + u"/zażółć/src"_s;
    Run run;
    QVERIFY(run.sh(u"echo ready; read a; cd src; echo moved; read b"_s, dir.path() + u"/zażółć"_s));
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("ready"), Timeout);
#if defined(Q_OS_LINUX)
    QCOMPARE(QDir(run.pty.currentDirectory()).canonicalPath(), QDir(dir.path()).canonicalPath() + u"/zażółć"_s);
    QVERIFY(run.pty.write("\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.output.contains("moved"), Timeout);
    // The shell went into src: the directory follows it.
    QTRY_COMPARE_WITH_TIMEOUT(QDir(run.pty.currentDirectory()).canonicalPath(), inner, Timeout);
#else
    // Where the system does not say: nothing.
    QVERIFY(run.pty.currentDirectory().isEmpty());
    QVERIFY(run.pty.write("\r"));
#endif
    QVERIFY(run.pty.write("\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
}

void TestPty::notRunning()
{
    Pty pty;
    QVERIFY(!pty.isRunning());
    QVERIFY(!pty.write("x"));
    QVERIFY(!pty.resize(80, 24));
    pty.terminate(); // nothing to do
    QCOMPARE(pty.processId(), 0);
}

// ---- Windows (ConPTY) ----------------------------------------------------------------------------

void TestPty::conptyOutput()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun run;
    QVERIFY(run.probe({u"print"_s, u"zażółć gęślą jaźń"_s, u"ZAŻÓŁĆ GĘŚLĄ"_s}));
    QVERIFY(run.pty.isRunning());
    const qint64 pid = run.pty.processId();
    QVERIFY(pid > 0);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY2(run.text().contains(u"zażółć gęślą jaźń\nZAŻÓŁĆ GĘŚLĄ"_s), qPrintable(run.text()));
    QCOMPARE(run.exitCode, 0);
    QVERIFY(!run.crashed);
    QVERIFY(!run.pty.isRunning());
    QCOMPARE(run.pty.processId(), 0);
#ifdef Q_OS_WIN
    QVERIFY(processEnded(pid));
#endif
}

void TestPty::conptyExitCodes()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun three;
    QVERIFY(three.probe({u"exit"_s, u"3"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(three.done, Timeout);
    QCOMPARE(three.exitCode, 3);
    QVERIFY(!three.crashed);
    // An NTSTATUS: a crash, the code as it is.
    ScreenRun crash;
    QVERIFY(crash.probe({u"crash"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(crash.done, Timeout);
    QCOMPARE(crash.exitCode, int(0xC0000005u));
    QVERIFY(crash.crashed);
    // cmd.exe found on PATH, its argument quoted.
    ScreenRun cmd;
    QVERIFY(cmd.start(u"cmd.exe"_s, {u"/d"_s, u"/c"_s, u"exit 7"_s}, {}, Pty::defaultEnvironment(), ScreenRun::Columns,
                      ScreenRun::Rows));
    QTRY_VERIFY_WITH_TIMEOUT(cmd.done, Timeout);
    QCOMPARE(cmd.exitCode, 7);
    QVERIFY(!cmd.crashed);
}

void TestPty::conptyArguments()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    // Read back by the C runtime as they were given: spaces, quotes, backslashes before quotes and
    // at the end, an empty one, cmd.exe's characters (nothing here is a shell).
    const QStringList arguments = {u"zażółć"_s,
                                   u"dwa słowa"_s,
                                   u"\"cytat\""_s,
                                   u"a\\b"_s,
                                   u"koniec\\"_s,
                                   u"spacja i koniec\\"_s,
                                   QString(),
                                   u"100%"_s,
                                   u"a&b|c<d>e^f"_s,
                                   u"\\\\serwer\\udział\\"_s,
                                   u"\"\\\"\""_s,
                                   u"%PATH%"_s};
    ScreenRun run;
    QVERIFY(run.probe(QStringList{u"args"_s} + arguments));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QString expected;
    for (const QString &argument : arguments)
        expected += u'[' + argument + u"]\n"_s;
    expected += u"args done"_s;
    QVERIFY2(run.text().contains(expected), qPrintable(run.text()));

    // cmd.exe's /c takes the rest of the line by its own rules: native arguments, as they are.
    ScreenRun cmd;
    cmd.pty.setNativeArguments(u"/d /s /c \"echo \"zażółć\" a^&b & exit 4\""_s);
    QCOMPARE(cmd.pty.nativeArguments(), u"/d /s /c \"echo \"zażółć\" a^&b & exit 4\""_s);
    QVERIFY(cmd.start(u"cmd.exe"_s, {}, {}, Pty::defaultEnvironment(), ScreenRun::Columns, ScreenRun::Rows));
    QTRY_VERIFY_WITH_TIMEOUT(cmd.done, Timeout);
    QVERIFY2(cmd.text().contains(u"\"zażółć\" a&b"_s), qPrintable(cmd.text()));
    QCOMPARE(cmd.exitCode, 4);
}

void TestPty::conptySizeAndResize()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun run;
    QVERIFY(run.probe({u"size"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(run.text().contains(u"size 50 200"_s), Timeout);
    const qsizetype before = run.output.size();
    run.terminal.resize(132, 43);
    QVERIFY(run.pty.resize(132, 43));
    // (The size goes to conhost on a pipe of its own: input sent at once could come first. ConPTY
    // paints the new screen once it has it.)
    (void)QTest::qWaitFor([&] { return run.output.size() > before; }, 3000);
    QVERIFY(run.pty.write("\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.text().contains(u"size 43 132"_s), Timeout);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QCOMPARE(run.exitCode, 0);
}

void TestPty::conptyInput()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun run;
    QVERIFY(run.probe({u"echo"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(run.text().contains(u"ready"_s), Timeout);
    QVERIFY(run.pty.write(u"zażółć gęślą\r"_s.toUtf8()));
    QTRY_VERIFY_WITH_TIMEOUT(run.text().contains(u"got: zażółć gęślą"_s), Timeout);
    // The console's echo of the line above the program's copy of it.
    QVERIFY2(run.text().contains(u"zażółć gęślą\ngot: zażółć gęślą"_s), qPrintable(run.text()));
    QVERIFY(run.pty.write("quit\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QCOMPARE(run.exitCode, 0);
    QVERIFY(!run.pty.write("lost"));
}

void TestPty::conptyWorkingDirectory()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.filePath(u"projekt żółw"_s);
    QVERIFY(QDir().mkpath(folder));
    ScreenRun run;
    QVERIFY(run.probe({u"cwd"_s}, folder));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    const QString expected = u"cwd "_s + QDir::toNativeSeparators(QDir(folder).absolutePath());
    QVERIFY2(run.text().contains(expected, Qt::CaseInsensitive), qPrintable(run.text() + u" / "_s + expected));
    // A missing folder: the home folder.
    ScreenRun home;
    QVERIFY(home.probe({u"cwd"_s}, dir.filePath(u"missing"_s)));
    QTRY_VERIFY_WITH_TIMEOUT(home.done, Timeout);
    QVERIFY2(home.text().contains(u"cwd "_s + QDir::toNativeSeparators(QDir::homePath()), Qt::CaseInsensitive),
             qPrintable(home.text()));
}

void TestPty::conptyEnvironment()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    QStringList env = Pty::defaultEnvironment();
    env << u"COLUMNS=5"_s << u"Lines=5"_s << u"TERM=dumb"_s << u"WT_SESSION=abc"_s << u"COLORTERM=no"_s
        << u"BRULION_TEST_VAR=ąęż żółw"_s << u"brulion_dup=1"_s << u"BRULION_DUP=2"_s;
    ScreenRun run;
    QVERIFY(run.probe({u"env"_s, u"TERM"_s, u"COLORTERM"_s, u"COLUMNS"_s, u"LINES"_s, u"WT_SESSION"_s,
                       u"BRULION_TEST_VAR"_s, u"BRULION_DUP"_s, u"SystemRoot"_s},
                      {}, env));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    const QString text = run.text();
    for (const QString &line : {u"TERM unset"_s, u"COLORTERM=truecolor"_s, u"COLUMNS unset"_s, u"LINES unset"_s,
                                u"WT_SESSION unset"_s, u"BRULION_TEST_VAR=ąęż żółw"_s, u"BRULION_DUP=2"_s})
        QVERIFY2(text.contains(line), qPrintable(line + u" in:\n"_s + text));
    // The rest of the app's environment is there.
    QVERIFY2(text.contains(u"SystemRoot="_s + qEnvironmentVariable("SystemRoot"), Qt::CaseInsensitive),
             qPrintable(text));
}

void TestPty::conptyNoInheritedHandles()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    const QString marker = dir.filePath(u"brulion-pty-marker.txt"_s);
    {
        QFile file(marker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("marker");
    }
    // An inheritable handle of the app's: the program must not get it.
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(marker).utf16()),
                                        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(handle != INVALID_HANDLE_VALUE);
    ScreenRun run;
    QVERIFY(run.probe({u"handle"_s, QString::number(quintptr(handle), 16), u"brulion-pty-marker"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    ::CloseHandle(handle);
    QVERIFY2(run.text().contains(u"handle not inherited"_s), qPrintable(run.text()));
#endif
}

void TestPty::conptyCannotStart()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    Pty pty;
    QSignalSpy finished(&pty, &Pty::finished);
    QString error;
    const QStringList env = Pty::defaultEnvironment();
    QVERIFY(!pty.start(u"C:\\nonexistent\\program.exe"_s, {}, {}, env, 80, 24, &error));
    QVERIFY2(error.contains(u"program.exe"_s), qPrintable(error));
    error.clear();
    QVERIFY(!pty.start(u"brulion-no-such-program-xyz"_s, {}, {}, env, 80, 24, &error));
    QVERIFY(!error.isEmpty());
    // Not a program: CreateProcess says why.
    QTemporaryDir dir;
    const QString junk = dir.filePath(u"junk.exe"_s);
    {
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("MZ garbage that is not a program");
    }
    error.clear();
    QVERIFY(!pty.start(junk, {}, {}, env, 80, 24, &error));
    QVERIFY2(error.contains(u"junk"_s), qPrintable(error));
    // A batch file is cmd.exe's to run.
    const QString batch = dir.filePath(u"skrypt.cmd"_s);
    {
        QFile file(batch);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("@echo off\r\nexit 1\r\n");
    }
    error.clear();
    QVERIFY(!pty.start(batch, {}, {}, env, 80, 24, &error));
    QVERIFY2(error.contains(u"cmd.exe"_s), qPrintable(error));
    QVERIFY(!pty.isRunning());
    QCOMPARE(finished.size(), 0);
#ifdef BRULION_TEST_PTY_PROBE
    // The Pty still works afterwards, and again after its program ended; one program at a time.
    const QString probe = QString::fromUtf8(BRULION_TEST_PTY_PROBE);
    QVERIFY(pty.start(probe, {u"exit"_s, u"7"_s}, {}, env, 80, 24, &error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, Timeout);
    QCOMPARE(finished.at(0).at(0).toInt(), 7);
    QVERIFY(pty.start(probe, {u"exit"_s, u"8"_s}, {}, env, 80, 24, &error));
    QVERIFY(!pty.start(probe, {}, {}, env, 80, 24, &error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, Timeout);
    QCOMPARE(finished.at(1).at(0).toInt(), 8);
#endif
}

void TestPty::conptyTerminateTree()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
#ifdef Q_OS_WIN
    // A child on the console: the console's close (CTRL_CLOSE_EVENT) ends both, long before the
    // grace period is over.
    ScreenRun run;
    QVERIFY(run.probe({u"spawn"_s, u"console"_s}));
    const qint64 pid = run.pty.processId();
    QTRY_VERIFY_WITH_TIMEOUT(run.numberAfter(u"child"_s) > 0, Timeout);
    const qint64 child = run.numberAfter(u"child"_s);
    QElapsedTimer timer;
    timer.start();
    run.pty.terminate(8000);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    // (STATUS_CONTROL_C_EXIT where the console's close came as CTRL_CLOSE_EVENT; Windows versions
    // differ in how the close ends a program.)
    QVERIFY2(run.exitCode != 0, qPrintable(QString::number(quint32(run.exitCode), 16)));
    QVERIFY2(timer.elapsed() < 6000 * BRULION_TEST_SLOWDOWN, "the console's close ended it, not the job");
    QVERIFY(processEnded(pid));
    QVERIFY(processEnded(child, Timeout));

    // A child without a console gets no close: the job ends it.
    ScreenRun detached;
    QVERIFY(detached.probe({u"spawn"_s, u"detached"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(detached.numberAfter(u"child"_s) > 0, Timeout);
    const qint64 orphan = detached.numberAfter(u"child"_s);
    QVERIFY(!processEnded(orphan));
    detached.pty.terminate(8000);
    QTRY_VERIFY_WITH_TIMEOUT(detached.done, Timeout);
    QVERIFY(processEnded(orphan, Timeout));
#endif
}

void TestPty::conptyTerminateIgnoringClose()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun run;
    QVERIFY(run.probe({u"ignore-close"_s}));
    const qint64 pid = run.pty.processId();
    QTRY_VERIFY_WITH_TIMEOUT(run.text().contains(u"ready"_s), Timeout);
    QElapsedTimer timer;
    timer.start();
    run.pty.terminate(300);
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QVERIFY2(run.exitCode != 0, qPrintable(QString::number(quint32(run.exitCode), 16)));
    // (Windows itself gives a close handler 5 s.)
    QVERIFY2(timer.elapsed() < 4500 * BRULION_TEST_SLOWDOWN, "the job ended it after the grace period");
#ifdef Q_OS_WIN
    QVERIFY(processEnded(pid));
#else
    Q_UNUSED(pid)
#endif
}

void TestPty::conptyChildWithoutConsoleStays()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
#ifdef Q_OS_WIN
    // The program ends by itself: what it started without a console (an editor, a browser) stays…
    ScreenRun run;
    QVERIFY(run.probe({u"spawn"_s, u"detached"_s, u"exit"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout);
    QCOMPARE(run.exitCode, 0);
    const qint64 child = run.numberAfter(u"child"_s);
    QVERIFY2(child > 0, qPrintable(run.text()));
    QVERIFY(!processEnded(child, 500));
    killProcess(child);
    // … what it left on the console gets the console's close.
    ScreenRun console;
    QVERIFY(console.probe({u"spawn"_s, u"console"_s, u"exit"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(console.done, Timeout);
    const qint64 attached = console.numberAfter(u"child"_s);
    QVERIFY2(attached > 0, qPrintable(console.text()));
    const bool ended = processEnded(attached, Timeout);
    if (!ended)
        killProcess(attached);
    QVERIFY(ended);
#endif
}

void TestPty::conptyDeletedWhileRunning()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
#if defined(Q_OS_WIN) && defined(BRULION_TEST_PTY_PROBE)
    const QString probe = QString::fromUtf8(BRULION_TEST_PTY_PROBE);
    auto *pty = new Pty;
    QVERIFY(pty->start(probe, {u"sleep"_s}, {}, Pty::defaultEnvironment(), 80, 24));
    const qint64 pid = pty->processId();
    QVERIFY(pid > 0);
    delete pty;
    QVERIFY(processEnded(pid)); // the destructor waited for it

    // One that holds the console's close up: the job ends it.
    auto *stubborn = new Pty;
    QByteArray out;
    connect(stubborn, &Pty::dataReceived, this, [&out](const QByteArray &data) { out += data; });
    QVERIFY(stubborn->start(probe, {u"ignore-close"_s}, {}, Pty::defaultEnvironment(), 80, 24));
    const qint64 stubbornPid = stubborn->processId();
    QTRY_VERIFY_WITH_TIMEOUT(out.contains("ready"), Timeout);
    QElapsedTimer timer;
    timer.start();
    delete stubborn;
    QVERIFY(processEnded(stubbornPid));
    QVERIFY(timer.elapsed() < 3000 * BRULION_TEST_SLOWDOWN);
#endif
}

void TestPty::conptyLargePaste()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun run;
    QVERIFY(run.probe({u"count"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(run.text().contains(u"ready"_s), Timeout);
    // 200 KB in lines of 100: more than the pipe holds, written by the writer thread as conhost
    // takes it.
    QByteArray paste;
    for (int i = 0; i < 2000; ++i)
        paste += QByteArray(99, 'a') + '\r';
    QVERIFY(run.pty.write(paste));
    QVERIFY(run.pty.write("end\r"));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout * 4);
    QVERIFY2(run.text().contains(u"count 198000"_s), qPrintable(run.text().right(400)));
    QCOMPARE(run.exitCode, 0);
}

void TestPty::conptyFlood()
{
    if (!m_conpty)
        QSKIP("ConPTY: Windows only");
    ScreenRun run;
    int reads = 0;
    connect(&run.pty, &Pty::dataReceived, this, [&reads] { ++reads; });
    QElapsedTimer timer;
    timer.start();
    QVERIFY(run.probe({u"flood"_s, u"200000"_s}));
    QTRY_VERIFY_WITH_TIMEOUT(run.done, Timeout * 4);
    qInfo("200000 lines through ConPTY: %lld ms, %d deliveries, %lld bytes", timer.elapsed(), reads,
          qint64(run.output.size()));
    QVERIFY2(run.text().contains(u"y\nflood done"_s), qPrintable(run.text().right(400)));
    QCOMPARE(run.exitCode, 0);
}

QTEST_GUILESS_MAIN(TestPty)
#include "tst_pty.moc"
