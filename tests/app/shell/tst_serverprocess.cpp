// ServerProcess (src/app/shell/serverprocess.h), a brulion-cli server run while the app runs, with
// a server of the test's own (fakeserver.cpp): Running once its first line says so (its "result",
// its log lines without their time), stopped by the end of its standard input (and terminated when
// it ignores that), started again after a crash with the waits growing and a long run forgetting
// them, a refusal (exit 2) or a program that does not start left Failed with the server's words, an
// end with exit 5 tried again, start() with new arguments, and the destructor stopping it.
#include "serverprocess.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::ServerProcess;

namespace {

constexpr int kTimeoutMs = 20000 * BRULION_TEST_SLOWDOWN;

QString fileText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

class TestServerProcess : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void runsAndStops()
    {
        const QString mark = m_tmp.filePath(u"stopped-1"_s);
        ServerProcess server(u"Serwer testowy"_s);
        QSignalSpy lines(&server, &ServerProcess::logLine);
        QSignalSpy announced(&server, &ServerProcess::announced);
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://żółw.example:8790"_s, u"--mark"_s, mark});
        QCOMPARE(server.state(), ServerProcess::State::Stopped);
        server.start();
        QCOMPARE(server.state(), ServerProcess::State::Starting);
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Running, kTimeoutMs);
        QCOMPARE(announced.size(), 1);
        QCOMPARE(server.announcement().value(u"url"_s).toString(), u"http://żółw.example:8790"_s);
        QVERIFY(server.processId() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(lines.size() >= 2, kTimeoutMs);
        QCOMPARE(lines.at(0).at(0).toString(), u"starting the fake server"_s); // (its time left out)
        // Stopped: its input ends and it stops by itself.
        server.stop();
        QCOMPARE(server.state(), ServerProcess::State::Stopped);
        QTRY_COMPARE_WITH_TIMEOUT(fileText(mark), u"stopped"_s, kTimeoutMs);
        QVERIFY(server.announcement().isEmpty());
    }

    void terminatedWhenItIgnoresTheEnd()
    {
#ifdef Q_OS_LINUX
        ServerProcess server(u"Uparty"_s);
        server.setStopTimeoutMs(200);
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://x"_s, u"--ignore-eof"_s});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Running, kTimeoutMs);
        const qint64 pid = server.processId();
        QVERIFY(pid > 0);
        QVERIFY(QFile::exists(u"/proc/%1"_s.arg(pid)));
        // Its input ends, it goes on: terminated after the stop timeout (and reaped).
        server.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(u"/proc/%1"_s.arg(pid)), kTimeoutMs);
#else
        QSKIP("the process is looked for in /proc");
#endif
    }

    void restartedAfterACrash()
    {
        ServerProcess server(u"Kruchy"_s);
        server.setRetryDelays({100, 300});
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://x"_s, u"--crash-after"_s, u"100"_s});
        QSignalSpy announced(&server, &ServerProcess::announced);
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Waiting, kTimeoutMs);
        QCOMPARE(server.failures(), 1);
        QVERIFY(!server.error().isEmpty());
        QVERIFY(server.retryIn() <= 100);
        // Started again by itself, and again.
        QTRY_VERIFY_WITH_TIMEOUT(announced.size() >= 2, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(server.failures() >= 2, kTimeoutMs);
        server.stop();
        QCOMPARE(server.state(), ServerProcess::State::Stopped);
        QCOMPARE(server.failures(), 0);
        const qsizetype seen = announced.size();
        QTest::qWait(500);
        QCOMPARE(announced.size(), seen); // (stopped: not started again)
    }

    void longRunForgetsFailures()
    {
        ServerProcess server(u"Kruchy"_s);
        server.setRetryDelays({50});
        server.setStableMs(0); // (every run counts as long)
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://x"_s, u"--crash-after"_s, u"50"_s});
        QSignalSpy announced(&server, &ServerProcess::announced);
        server.start();
        QTRY_VERIFY_WITH_TIMEOUT(announced.size() >= 3, kTimeoutMs);
        QCOMPARE(server.failures(), 1);
        server.stop();
    }

    void refusalIsFinal()
    {
        ServerProcess server(u"Odmowa"_s);
        server.setRetryDelays({50});
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--refuse"_s, u"2"_s});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Failed, kTimeoutMs);
        QCOMPARE(server.error(), u"Nie można słuchać na tym adresie"_s);
        QCOMPARE(server.errorCode(), u"bad_listen"_s);
        QTest::qWait(300);
        QCOMPARE(server.state(), ServerProcess::State::Failed);
        // A refusal over several lines (an older brulion-cli without --compact): its words too.
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--refuse"_s, u"2"_s, u"--indented"_s});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Failed, kTimeoutMs);
        QCOMPARE(server.error(), u"Nieznana opcja: --stop-on-eof"_s);
        QCOMPARE(server.errorCode(), u"unknown_option"_s);
        // Exit 5 (unavailable: it may work later): tried again.
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--refuse"_s, u"5"_s});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Waiting, kTimeoutMs);
        QCOMPARE(server.error(), u"Nie można słuchać na tym adresie"_s);
        server.stop();
    }

    void programMissing()
    {
        ServerProcess server(u"Brak"_s);
        server.setProgram(m_tmp.filePath(u"nie-ma-takiego-programu"_s), {});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Failed, kTimeoutMs);
        QVERIFY(!server.error().isEmpty());
        ServerProcess none(u"Pusty"_s);
        none.start();
        QCOMPARE(none.state(), ServerProcess::State::Failed);
    }

    void startedAnewWithNewArguments()
    {
        const QString mark = m_tmp.filePath(u"stopped-2"_s);
        ServerProcess server(u"Zmienny"_s);
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://pierwszy"_s, u"--mark"_s, mark});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Running, kTimeoutMs);
        server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://drugi"_s});
        server.start();
        QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Running, kTimeoutMs);
        QCOMPARE(server.announcement().value(u"url"_s).toString(), u"http://drugi"_s);
        QTRY_COMPARE_WITH_TIMEOUT(fileText(mark), u"stopped"_s, kTimeoutMs); // (the first one stopped)
    }

    void destructorStops()
    {
        const QString mark = m_tmp.filePath(u"stopped-3"_s);
        {
            ServerProcess server(u"Ulotny"_s);
            server.setProgram(QStringLiteral(FAKE_SERVER), {u"--announce"_s, u"http://x"_s, u"--mark"_s, mark});
            server.start();
            QTRY_COMPARE_WITH_TIMEOUT(server.state(), ServerProcess::State::Running, kTimeoutMs);
        }
        // (The destructor waited for it.)
        QCOMPARE(fileText(mark), u"stopped"_s);
    }
};

QTEST_GUILESS_MAIN(TestServerProcess)
#include "tst_serverprocess.moc"
