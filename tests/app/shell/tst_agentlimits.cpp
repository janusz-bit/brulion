// The subscription limits as the chat and Settings show them (src/app/shell/agentlimits.h,
// Workspace.chats.limits): which agents of this device have them (Claude Code's and Codex's
// presets, a command of the user's own its program names, never one on another computer), their
// accounts' sign-in files, the texts — "5 h: 12 % · week: 68 %", each window with its reset in local
// time, the warning above 80 % and the danger at 100 %, what to do when the sign-in cannot be used
// — and reading them when shown (once in five minutes) or refreshed, over a fake transport.
#include "agentlimits.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "sync/http.h"

#include <QLocale>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;
using namespace brulion::agentlimits;

namespace {

class FakeTransport : public sync::HttpTransport {
public:
    QList<std::pair<sync::HttpRequest, sync::HttpCallback>> pending;
    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        pending.append({request, std::move(done)});
    }
    void answer(int status, const QByteArray &body)
    {
        auto [request, done] = pending.takeFirst();
        sync::HttpResponse r;
        r.status = status;
        r.body = body;
        QTimer::singleShot(0, [done, r] { done(r); });
    }
};

const QDateTime kNow(QDate(2026, 11, 23), QTime(14, 30)); // a Monday, local time

Window window(const QString &id, Kind kind, double percent, const QDateTime &reset, const QString &model = {},
              int minutes = 0)
{
    Window w;
    w.id = id;
    w.kind = kind;
    w.usedPercent = percent;
    w.resetsAt = reset.toUTC();
    w.model = model;
    w.minutes = minutes > 0 ? minutes : kind == Kind::Session ? 300 : 10080;
    return w;
}

} // namespace

class TestAgentLimitsShell : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom)); }

    void accounts_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("programName");
        QTest::addColumn<bool>("remote");
        QTest::addColumn<QString>("key");
        QTest::newRow("Claude Code") << u"npx -y @agentclientprotocol/claude-agent-acp"_s << QString() << false
                                     << u"claude:/home/łucja/.claude/.credentials.json"_s;
        QTest::newRow("Codex") << u"npx -y @agentclientprotocol/codex-acp"_s << QString() << false
                               << u"codex:/home/łucja/.codex/auth.json"_s;
        QTest::newRow("its own config folder")
                << u"env CLAUDE_CONFIG_DIR=\"/home/łucja/praca claude\" npx claude-agent-acp"_s << QString() << false
                << u"claude:/home/łucja/praca claude/.credentials.json"_s;
        QTest::newRow("Gemini CLI") << u"npx -y @google/gemini-cli --acp"_s << QString() << false << u":"_s;
        QTest::newRow("own command, Codex inside") << u"/opt/agent/run.sh"_s << u"codex-acp"_s << false
                                                   << u"codex:/home/łucja/.codex/auth.json"_s;
        QTest::newRow("on another computer") << u"npx -y @agentclientprotocol/claude-agent-acp"_s << QString() << true
                                             << u":"_s;
    }

    void accounts()
    {
        QFETCH(QString, command);
        QFETCH(QString, programName);
        QFETCH(bool, remote);
        QFETCH(QString, key);
        AgentConfig agent{u"a"_s, u"Agent"_s, command};
        if (remote)
            agent.url = u"https://nixos.example.ts.net"_s;
        QCOMPARE(AgentLimits::accountOf(agent, programName, {}, u"/home/łucja"_s).key(), key);
    }

    void texts()
    {
        Limits limits;
        limits.provider = Provider::Claude;
        limits.plan = u"max"_s;
        limits.checkedAt = kNow.addSecs(-60);
        limits.windows = {window(u"five_hour"_s, Kind::Session, 12.4, kNow.addSecs(4 * 3600 + 600)),
                          window(u"seven_day"_s, Kind::Weekly, 67.6, QDateTime(QDate(2026, 11, 27), QTime(8, 0))),
                          window(u"seven_day_opus"_s, Kind::Model, 91, QDateTime(QDate(2026, 12, 2), QTime(9, 0)), u"Opus"_s),
                          window(u"seven_day_sonnet"_s, Kind::Model, 3, QDateTime(), u"Sonnet"_s)};
        const QVariantMap d = AgentLimits::describe(limits, kNow);
        QCOMPARE(d.value(u"summary"_s).toString(), u"5 h: 12 % · week: 68 %"_s);
        QCOMPARE(d.value(u"level"_s).toString(), u"warning"_s);
        QCOMPARE(d.value(u"plan"_s).toString(), u"Plan: Max"_s);
        QCOMPARE(d.value(u"checked"_s).toString(), u"Checked at 14:29"_s);
        QCOMPARE(d.value(u"failure"_s).toString(), QString());
        const QVariantList rows = d.value(u"windows"_s).toList();
        QCOMPARE(rows.size(), 4);
        const auto row = [&rows](int i, const QString &key) { return rows.at(i).toMap().value(key).toString(); };
        QCOMPARE(row(0, u"label"_s), u"5-hour window"_s);
        QCOMPARE(row(0, u"text"_s), u"12 % · resets at 18:40"_s);
        QCOMPARE(row(0, u"level"_s), QString());
        QCOMPARE(row(1, u"label"_s), u"Week, all models"_s);
        QCOMPARE(row(1, u"text"_s), u"68 % · resets Fri 08:00"_s);
        QCOMPARE(row(2, u"label"_s), u"Week, Opus"_s);
        QCOMPARE(row(2, u"text"_s), u"91 % · resets 2 Dec 09:00"_s);
        QCOMPARE(row(2, u"level"_s), u"warning"_s);
        QCOMPARE(row(3, u"text"_s), u"3 %"_s);
        const QString tip = d.value(u"tooltip"_s).toString();
        QVERIFY(tip.startsWith(u"Claude Code · Plan: Max\n5-hour window: 12 % · resets at 18:40\n"_s));
        QVERIFY(tip.endsWith(u"\nChecked at 14:29"_s));

        // At 100 % anywhere: the danger colour; Codex Free's month where it has no five hours.
        limits.windows[2].usedPercent = 100;
        QCOMPARE(AgentLimits::describe(limits, kNow).value(u"level"_s).toString(), u"danger"_s);
        Limits free;
        free.provider = Provider::Codex;
        free.windows = {window(u"primary"_s, Kind::Monthly, 40, QDateTime(), {}, 43200)};
        QCOMPARE(AgentLimits::describe(free, kNow).value(u"summary"_s).toString(), u"month: 40 %"_s);
        QCOMPARE(AgentLimits::describe(free, kNow).value(u"level"_s).toString(), QString());
        Limits spark;
        spark.provider = Provider::Codex;
        spark.windows = {window(u"model:Spark:primary"_s, Kind::Model, 5, QDateTime(), u"GPT-5.3-Codex-Spark"_s, 300)};
        QCOMPARE(AgentLimits::describe(spark, kNow).value(u"windows"_s).toList().first().toMap().value(u"label"_s).toString(),
                 u"GPT-5.3-Codex-Spark, 5-hour window"_s);
    }

    void failures_data()
    {
        QTest::addColumn<int>("provider");
        QTest::addColumn<int>("failure");
        QTest::addColumn<QString>("contains");
        QTest::addColumn<QString>("chip");
        QTest::newRow("Claude, not signed in") << int(Provider::Claude) << int(Failure::NoCredentials) << u"/login"_s
                                               << u"Limits: not signed in"_s;
        QTest::newRow("Codex, not signed in") << int(Provider::Codex) << int(Failure::NoCredentials) << u"codex login"_s
                                              << u"Limits: not signed in"_s;
        QTest::newRow("API key") << int(Provider::Codex) << int(Failure::ApiKey) << u"API key"_s
                                 << u"Limits: not signed in"_s;
        QTest::newRow("expired") << int(Provider::Claude) << int(Failure::Expired) << u"use Claude Code once"_s
                                 << u"Limits: sign in again"_s;
        QTest::newRow("refused") << int(Provider::Codex) << int(Failure::SignedOut) << u"codex login"_s
                                 << u"Limits: sign in again"_s;
        QTest::newRow("offline") << int(Provider::Claude) << int(Failure::Offline) << u"No connection"_s
                                 << u"Limits: offline"_s;
        QTest::newRow("too often") << int(Provider::Claude) << int(Failure::RateLimited) << u"few minutes"_s
                                   << u"Limits: not available"_s;
        QTest::newRow("server") << int(Provider::Claude) << int(Failure::ServerError) << u"HTTP 503"_s
                                << u"Limits: not available"_s;
        QTest::newRow("unreadable") << int(Provider::Claude) << int(Failure::Unreadable) << u".credentials.json"_s
                                    << u"Limits: not available"_s;
    }

    void failures()
    {
        QFETCH(int, provider);
        QFETCH(int, failure);
        QFETCH(QString, contains);
        QFETCH(QString, chip);
        Limits limits;
        limits.provider = Provider(provider);
        limits.failure = Failure(failure);
        limits.status = limits.failure == Failure::ServerError ? 503 : 0;
        limits.checkedAt = kNow;
        const QVariantMap d = AgentLimits::describe(limits, kNow, u"/home/łucja/.claude/.credentials.json"_s);
        QVERIFY2(d.value(u"failure"_s).toString().contains(contains), qPrintable(d.value(u"failure"_s).toString()));
        QCOMPARE(d.value(u"failureShort"_s).toString(), chip);
        QCOMPARE(d.value(u"summary"_s).toString(), QString());
        QVERIFY(d.value(u"tooltip"_s).toString().contains(d.value(u"failure"_s).toString()));
    }

    void resets()
    {
        QCOMPARE(AgentLimits::resetText(QDateTime(), kNow), QString());
        QCOMPARE(AgentLimits::resetText(QDateTime(QDate(2026, 11, 23), QTime(23, 59)), kNow), u"resets at 23:59"_s);
        QCOMPARE(AgentLimits::resetText(QDateTime(QDate(2026, 11, 24), QTime(0, 5)), kNow), u"resets Tue 00:05"_s);
        QCOMPARE(AgentLimits::resetText(QDateTime(QDate(2026, 11, 30), QTime(7, 0)), kNow), u"resets 30 Nov 07:00"_s);
        // In local time, whatever zone it came in.
        const QDateTime utc = QDateTime(QDate(2026, 11, 23), QTime(18, 0)).toUTC();
        QCOMPARE(AgentLimits::resetText(utc, kNow), u"resets at 18:00"_s);
    }

    void readsWhenShown()
    {
        Workspace ws;
        ws.chats()->setAgents({AgentConfig{u"claude"_s, u"Claude Code"_s, u"npx -y @agentclientprotocol/claude-agent-acp"_s},
                               AgentConfig{u"gemini"_s, u"Gemini CLI"_s, u"npx -y @google/gemini-cli --acp"_s}});
        AgentLimits *limits = ws.chats()->limits();
        FakeTransport transport;
        QDateTime now = kNow;
        int reads = 0;
        limits->setTransport(&transport);
        limits->setClock([&now] { return now; });
        limits->setEnvironment({}, u"/home/łucja"_s);
        limits->setCredentialsReader([&reads](Provider provider, const QString &file) {
            ++reads;
            Credentials c;
            if (provider == Provider::Claude && file == u"/home/łucja/.claude/.credentials.json")
                c.token = u"sk-ant-oat01-fake"_s;
            else
                c.failure = Failure::NoCredentials;
            return c;
        });
        if (!WorkspaceAgents::available())
            QSKIP("programs do not run here: no limits");
        QVERIFY(limits->applies(u"acp:claude"_s));
        QVERIFY(!limits->applies(u"acp:gemini"_s));
        QVERIFY(!limits->applies(QString()));
        QVERIFY(!limits->applies(u"acp:nieznany"_s));
        QVERIFY(!limits->state(u"acp:gemini"_s).value(u"applies"_s).toBool());

        QSignalSpy changed(limits, &AgentLimits::changed);
        QVariantMap state = limits->state(u"acp:claude"_s);
        QVERIFY(state.value(u"applies"_s).toBool());
        QVERIFY(!state.value(u"read"_s).toBool());
        limits->show(u"acp:claude"_s);
        QCOMPARE(transport.pending.size(), 1);
        QCOMPARE(transport.pending.first().first.url.host(), u"api.anthropic.com"_s);
        QVERIFY(limits->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        transport.answer(200, R"({"five_hour":{"utilization":81,"resets_at":null},"seven_day":{"utilization":20,"resets_at":null}})");
        QTRY_VERIFY(!limits->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        state = limits->state(u"acp:claude"_s);
        QCOMPARE(state.value(u"summary"_s).toString(), u"5 h: 81 % · week: 20 %"_s);
        QCOMPARE(state.value(u"level"_s).toString(), u"warning"_s);
        QVERIFY(changed.size() >= 2);
        // Shown again within five minutes: nothing read; Refresh reads now.
        now = kNow.addSecs(120);
        limits->show(u"acp:claude"_s);
        QCOMPARE(transport.pending.size(), 0);
        QCOMPARE(reads, 1);
        limits->refresh(u"acp:claude"_s);
        QCOMPARE(transport.pending.size(), 1);
        transport.answer(401, "{}");
        QTRY_VERIFY(!limits->state(u"acp:claude"_s).value(u"busy"_s).toBool());
        QCOMPARE(limits->state(u"acp:claude"_s).value(u"failureShort"_s).toString(), u"Limits: sign in again"_s);
        // An agent not Claude Code nor Codex is never read.
        limits->show(u"acp:gemini"_s);
        limits->refresh(u"acp:gemini"_s);
        QCOMPARE(transport.pending.size(), 0);
        // The agents changed: a binding asks again.
        const int serial = limits->serial();
        ws.chats()->setAgents({AgentConfig{u"codex"_s, u"Codex"_s, u"npx -y @agentclientprotocol/codex-acp"_s}});
        QVERIFY(limits->serial() > serial);
        QVERIFY(!limits->applies(u"acp:claude"_s));
        limits->show(u"acp:codex"_s);
        QCOMPARE(limits->state(u"acp:codex"_s).value(u"failureShort"_s).toString(), u"Limits: not signed in"_s);
        QCOMPARE(transport.pending.size(), 0);
    }
};

QTEST_MAIN(TestAgentLimitsShell)
#include "tst_agentlimits.moc"
