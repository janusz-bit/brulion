// A program on a pseudo-terminal (src/platform/pty.h) with this build's backend: output of
// /bin/sh, input through cat, `stty size` before and after a resize, the working directory and the
// environment the child gets, no descriptor of the parent's files in the child, exit codes and
// signals, programs that cannot be started, terminate() of a `sleep 100` with its background job
// and of one that ignores SIGHUP, a large paste into a slow reader — and every child reaped (no
// zombies), also when the Pty is deleted while its program runs.
#include "pty.h"

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
    void notRunning();

private:
    bool m_posix = false;
};

void TestPty::initTestCase()
{
    m_posix = QByteArrayView(BRULION_TEST_PTY_BACKEND) == "posix";
}

void TestPty::availability()
{
    QCOMPARE(Pty::available(), m_posix);
    if (!m_posix) {
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

void TestPty::notRunning()
{
    Pty pty;
    QVERIFY(!pty.isRunning());
    QVERIFY(!pty.write("x"));
    QVERIFY(!pty.resize(80, 24));
    pty.terminate(); // nothing to do
    QCOMPARE(pty.processId(), 0);
}

QTEST_GUILESS_MAIN(TestPty)
#include "tst_pty.moc"
