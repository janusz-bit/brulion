// McpServerSupervisor (src/app/shell/mcpserversupervisor.h): the remote MCP server the desktop app
// runs in the background, with the real brulion-cli as its program and a private home. Its
// arguments, the delays between starts, off without a program, refused without a notebook or a
// password; started (running, its endpoint answering, its log lines), refused by the server itself
// (a public address that is no https, a port in use: failed, not started again), started again
// after it was killed and after a setting changed, and stopped (off, the app gone).
#include "mcpserversupervisor.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::shell::McpServerSupervisor;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;

bool alive(qint64 pid)
{
#ifdef Q_OS_UNIX
    return pid > 0 && ::kill(pid_t(pid), 0) == 0;
#elif defined(Q_OS_WIN)
    if (pid <= 0)
        return false;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

} // namespace

class TestMcpServer : public QObject {
    Q_OBJECT

    QTemporaryDir m_home;
    QString m_vault;
    QString m_password;

    McpServerSupervisor::Config config() const
    {
        McpServerSupervisor::Config c;
        c.enabled = true;
        c.vault = m_vault;
        c.port = 0; // (any free one)
        c.passwordFile = m_password;
        return c;
    }
    std::unique_ptr<McpServerSupervisor> supervisor() const
    {
        auto s = std::make_unique<McpServerSupervisor>();
        s->setProgram({QStringLiteral(BRULION_CLI)});
        s->setRestartDelayForTests(100);
        return s;
    }
    /// GET of `url`: its status (0 no answer).
    static int get(const QString &url)
    {
        QNetworkAccessManager manager;
        std::unique_ptr<QNetworkReply> reply(manager.get(QNetworkRequest(QUrl(url))));
        QSignalSpy done(reply.get(), &QNetworkReply::finished);
        if (!done.wait(kTimeoutMs))
            return 0;
        return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        // brulion-cli (and the server it runs) in a private home: its state file, the notebook's
        // device state.
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_home.filePath(u"config"_s).toUtf8());
        qputenv("XDG_DATA_HOME", m_home.filePath(u"data"_s).toUtf8());
        qputenv("XDG_STATE_HOME", m_home.filePath(u"state"_s).toUtf8());
        qputenv("XDG_CACHE_HOME", m_home.filePath(u"cache"_s).toUtf8());
        qunsetenv("BRULION_PROFILE");
        m_vault = m_home.filePath(u"Notatki"_s);
        QVERIFY(QDir().mkpath(m_vault));
        QFile page(m_vault + u"/Zakupy.md"_s);
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("- mleko\n");
        m_password = m_home.filePath(u"secrets/mcp-password"_s);
        QVERIFY(QDir().mkpath(m_home.filePath(u"secrets"_s)));
        QFile secret(m_password);
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.write(u"zażółć-gęślą"_s.toUtf8());
    }

    void arguments()
    {
        McpServerSupervisor::Config c;
        c.vault = u"/home/ala/Notatki"_s;
        c.port = 8780;
        QCOMPARE(McpServerSupervisor::arguments(c), (QStringList{u"--compact"_s, u"--vault"_s, u"/home/ala/Notatki"_s,
                                                                 u"mcp"_s, u"serve"_s, u"--port"_s, u"8780"_s}));
        c.passwordFile = u"/home/ala/.local/share/Brulion/Brulion/secrets/mcp-password"_s;
        c.publicUrl = u" https://ala.tail1234.ts.net "_s;
        c.readOnly = true;
        QCOMPARE(McpServerSupervisor::arguments(c),
                 (QStringList{u"--compact"_s, u"--vault"_s, u"/home/ala/Notatki"_s, u"mcp"_s, u"serve"_s, u"--port"_s,
                              u"8780"_s, u"--password-file"_s, c.passwordFile, u"--public-url"_s, u"https://ala.tail1234.ts.net"_s,
                              u"--read-only"_s}));
    }

    void delays()
    {
        QCOMPARE(McpServerSupervisor::restartDelayMs(0), 1000);
        QCOMPARE(McpServerSupervisor::restartDelayMs(1), 2000);
        QCOMPARE(McpServerSupervisor::restartDelayMs(2), 4000);
        QCOMPARE(McpServerSupervisor::restartDelayMs(5), 32000);
        QCOMPARE(McpServerSupervisor::restartDelayMs(6), 60000);
        QCOMPARE(McpServerSupervisor::restartDelayMs(1000), 60000);
    }

    void offAndNoPassword()
    {
        auto s = supervisor();
        QCOMPARE(s->state(), u"off"_s);
        McpServerSupervisor::Config c = config();
        c.enabled = false;
        s->setConfig(c);
        QCOMPARE(s->state(), u"off"_s);
        c.enabled = true;
        c.vault.clear(); // (no notebook open)
        s->setConfig(c);
        QCOMPARE(s->state(), u"failed"_s);
        QVERIFY(!s->error().isEmpty());
        c.vault = m_vault;
        c.passwordFile.clear();
        s->setConfig(c);
        QCOMPARE(s->state(), u"failed"_s);
        QVERIFY(!s->error().isEmpty());
        QCOMPARE(s->processId(), 0);
        // No program here (Android, the browser): off.
        s->setProgram({});
        s->setConfig(config());
        QCOMPARE(s->state(), u"off"_s);
    }

    void runsAndStops()
    {
        auto s = supervisor();
        QSignalSpy logged(s.get(), &McpServerSupervisor::logChanged);
        s->setConfig(config());
        QCOMPARE(s->state(), u"starting"_s);
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), u"running"_s, kTimeoutMs);
        QVERIFY(s->error().isEmpty());
        const QString endpoint = s->endpoint();
        QVERIFY2(endpoint.startsWith(u"http://127.0.0.1:"_s) && endpoint.endsWith(u"/mcp"_s), qPrintable(endpoint));
        QVERIFY(!s->reachable()); // (this computer's address only)
        const qint64 pid = s->processId();
        QVERIFY(alive(pid));
        // It answers: the sign-in's metadata.
        const QString base = endpoint.chopped(4);
        QCOMPARE(get(base + u"/.well-known/oauth-protected-resource"_s), 200);
        // Its log lines: one per request, without a query string or token.
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(s->log(),
                                                     [](const QString &l) {
                                                         return l.contains(u"oauth-protected-resource"_s);
                                                     }),
                                 kTimeoutMs);
        QVERIFY(logged.size() > 0);
        // Off: stopped, its process gone.
        McpServerSupervisor::Config c = config();
        c.enabled = false;
        s->setConfig(c);
        QCOMPARE(s->state(), u"off"_s);
        QCOMPARE(s->endpoint(), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!alive(pid), kTimeoutMs);
    }

    void refused_data()
    {
        QTest::addColumn<QString>("publicUrl");
        QTest::addColumn<bool>("portTaken");
        QTest::newRow("no https") << u"http://notatki.example.pl"_s << false;
        QTest::newRow("port in use") << QString() << true;
    }
    void refused()
    {
        QFETCH(QString, publicUrl);
        QFETCH(bool, portTaken);
        QTcpServer taken;
        McpServerSupervisor::Config c = config();
        c.publicUrl = publicUrl;
        if (portTaken) {
            QVERIFY(taken.listen(QHostAddress::LocalHost));
            c.port = taken.serverPort();
        }
        auto s = supervisor();
        s->setConfig(c);
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), u"failed"_s, kTimeoutMs);
        QVERIFY2(!s->error().isEmpty(), qPrintable(s->error()));
        if (portTaken)
            QVERIFY2(s->error().contains(QString::number(c.port)), qPrintable(s->error()));
        else
            QVERIFY2(s->error().contains(u"https"_s), qPrintable(s->error()));
        // Not started again: the same settings would fail the same way.
        QTest::qWait(500);
        QCOMPARE(s->state(), u"failed"_s);
        QCOMPARE(s->processId(), 0);
    }

    void startedAgain()
    {
#ifndef Q_OS_UNIX
        QSKIP("ends the server with a signal");
#else
        auto s = supervisor();
        QStringList states;
        QString waitedBecause;
        connect(s.get(), &McpServerSupervisor::changed, this, [&states, &waitedBecause, &s] {
            states << s->state();
            if (s->state() == u"waiting")
                waitedBecause = s->error();
        });
        s->setConfig(config());
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), u"running"_s, kTimeoutMs);
        const qint64 first = s->processId();
        // Killed: waiting, then running again in a new process.
        states.clear();
        QVERIFY(::kill(pid_t(first), SIGKILL) == 0);
        QTRY_COMPARE_WITH_TIMEOUT(states, (QStringList{u"waiting"_s, u"starting"_s, u"running"_s}), kTimeoutMs);
        QVERIFY(!waitedBecause.isEmpty());
        QVERIFY(s->error().isEmpty());
        const qint64 second = s->processId();
        QVERIFY(second != first);
        // A setting changed: started again with it.
        McpServerSupervisor::Config c = config();
        c.readOnly = true;
        s->setConfig(c);
        QTRY_COMPARE_WITH_TIMEOUT(s->state(), u"running"_s, kTimeoutMs);
        QVERIFY(s->processId() != second);
        QVERIFY(!alive(second));
        // The app goes: so does the server.
        const qint64 third = s->processId();
        s.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!alive(third), kTimeoutMs);
#endif
    }
};

QTEST_GUILESS_MAIN(TestMcpServer)
#include "tst_mcpserver.moc"
