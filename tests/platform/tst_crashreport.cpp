// crashreport.h: a child process installs the report and crashes (a null write on the main thread
// and on a named thread, abort()); its stderr names the signal and the thread and holds a stack,
// and it still dies of the same signal. Linux with glibc only (skipped elsewhere and in sanitizer
// builds, where the sanitizers keep their own handlers).
#include "crashreport.h"

#include <QTest>

#include <cstdint>
#include <cstdlib>
#include <thread>

#ifdef Q_OS_LINUX
#include <pthread.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace {

#ifdef Q_OS_LINUX
volatile std::uintptr_t g_nowhere = 0;

void writeToNull()
{
    *reinterpret_cast<volatile int *>(g_nowhere) = 1;
}

/// Runs `how` in a child with the report installed; its stderr and how it ended.
QByteArray crashChild(const QByteArray &how, int *status)
{
    int fds[2];
    if (pipe(fds) != 0)
        return {};
    const pid_t pid = fork();
    if (pid == 0) {
        ::close(fds[0]);
        dup2(fds[1], STDERR_FILENO);
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0); // no core dump of the test's crashes
        brulion::platform::installCrashReport();
        brulion::platform::installCrashReport(); // a second call changes nothing
        if (how == "main") {
            prctl(PR_SET_NAME, "główny", 0, 0, 0);
            writeToNull();
        } else if (how == "thread") {
            std::thread worker([] {
                pthread_setname_np(pthread_self(), "wątek-awaria");
                writeToNull();
            });
            worker.join();
        } else if (how == "abort") {
            std::abort();
        }
        _exit(0);
    }
    ::close(fds[1]);
    QByteArray out;
    char buffer[4096];
    for (ssize_t n; (n = ::read(fds[0], buffer, sizeof(buffer))) > 0;)
        out.append(buffer, n);
    ::close(fds[0]);
    waitpid(pid, status, 0);
    return out;
}
#endif

} // namespace

class TestCrashReport : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        if (!brulion::platform::crashReportAvailable())
            QSKIP("no crash report in this build (not Linux with glibc, or a sanitizer build)");
    }

    void report_data()
    {
        QTest::addColumn<QByteArray>("how");
        QTest::addColumn<int>("signal");
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<QByteArray>("thread");
#ifdef Q_OS_LINUX
        QTest::newRow("main thread") << "main"_ba << SIGSEGV << "brulion: fatal SIGSEGV (11, code "_ba
                                     << "in thread 'główny'"_ba;
        QTest::newRow("named thread") << "thread"_ba << SIGSEGV << "brulion: fatal SIGSEGV (11, code "_ba
                                      << "in thread 'wątek-awaria'"_ba;
        QTest::newRow("abort") << "abort"_ba << SIGABRT << "brulion: fatal SIGABRT (6, code "_ba << "in thread '"_ba;
#endif
    }

    void report()
    {
#ifdef Q_OS_LINUX
        QFETCH(QByteArray, how);
        QFETCH(int, signal);
        QFETCH(QByteArray, header);
        QFETCH(QByteArray, thread);
        int status = 0;
        const QByteArray out = crashChild(how, &status);
        QVERIFY2(WIFSIGNALED(status), out.constData());
        QCOMPARE(WTERMSIG(status), signal);
        QVERIFY2(out.contains(header), out.constData());
        QVERIFY2(out.contains(thread), out.constData());
        if (signal == SIGSEGV)
            QVERIFY2(out.contains(", address 0x0)"), out.constData());
        QVERIFY2(out.contains("); its stack:\n"), out.constData());
        QVERIFY2(out.contains("brulion: end of the crash report\n"), out.constData());
        // Frames between the two lines: at least the handler's and the crashing function's.
        const qsizetype from = out.indexOf("its stack:\n") + 11;
        const qsizetype to = out.indexOf("brulion: end of the crash report");
        QVERIFY2(out.mid(from, to - from).count('\n') >= 2, out.constData());
#endif
    }
};

QTEST_GUILESS_MAIN(TestCrashReport)
#include "tst_crashreport.moc"
