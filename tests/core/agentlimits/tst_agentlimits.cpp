// The subscription limits of an agent's account (agentlimits/limits.h, reader.h): which agents are
// Claude Code or Codex (their command lines, npx packages, what initialize says), where their
// sign-in files are (the command's own CLAUDE_CONFIG_DIR / CODEX_HOME, the environment's, the home
// folder's), the sign-in files read (tokens, expiry, plan, an API key's sign-in, malformed files),
// the usage answers of both providers as windows — recorded shapes: five hours, the week, a
// model's week from both of Claude's spellings, extra usage, Codex's primary and secondary windows
// with and without their lengths, Free's month, a model's own limits — and malformed ones; the
// requests (https, the providers' hosts, their headers) and the answers' failures; the reader: at
// most every five minutes per account, a forced refresh, no request without a sign-in or with an
// expired one, the token masked in the log while it is on its way and not kept afterwards.
#include "agentlimits/limits.h"
#include "agentlimits/reader.h"

#include "log/redact.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::agentlimits;

namespace {

QDateTime utc(int y, int m, int d, int h, int min)
{
    return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::UTC);
}

const QDateTime kChecked = utc(2026, 11, 22, 14, 30);

/// A window as a line: "id kind model percent reset" (reset ISO in UTC, "-" unknown).
QString line(const Window &w)
{
    const QString kind = w.kind == Kind::Session ? u"session"_s
                         : w.kind == Kind::Weekly ? u"weekly"_s
                         : w.kind == Kind::Model  ? u"model"_s
                                                  : u"monthly"_s;
    return u"%1 %2 %3 %4 %5 %6"_s.arg(w.id, kind, w.model.isEmpty() ? u"-"_s : w.model)
            .arg(w.usedPercent)
            .arg(w.minutes)
            .arg(w.resetsAt.isValid() ? w.resetsAt.toUTC().toString(Qt::ISODate) : u"-"_s);
}

QStringList lines(const Limits &limits)
{
    QStringList out;
    for (const Window &w : limits.windows)
        out.append(line(w));
    return out;
}

/// A JWT with `claims` (not signed: only its payload is read).
QString jwt(const QJsonObject &claims)
{
    const auto part = [](const QByteArray &bytes) {
        return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    };
    return part(R"({"alg":"none"})") + u'.' + part(QJsonDocument(claims).toJson(QJsonDocument::Compact)) + u'.'
           + part("podpis");
}

/// Claude's answer as an account on Max gets it (python's microseconds, a model's week, nulls).
const QByteArray kClaudeMax = R"({
  "five_hour": {"utilization": 12.0, "resets_at": "2026-11-22T18:00:00.413528+00:00"},
  "seven_day": {"utilization": 68.0, "resets_at": "2026-11-27T07:00:00.413548+00:00"},
  "seven_day_oauth_apps": null,
  "seven_day_opus": {"utilization": 91.0, "resets_at": "2026-11-27T07:00:00.413556+00:00"},
  "seven_day_sonnet": {"utilization": 3.5, "resets_at": null},
  "iguana_necktie": null,
  "extra_usage": {"is_enabled": false, "monthly_limit": null, "used_credits": null, "utilization": null}
})";

/// The newer shape: a model's week in `limits`, extra usage spent.
const QByteArray kClaudeScoped = R"({
  "five_hour": {"utilization": 100, "resets_at": "2026-11-22T16:00:00Z"},
  "seven_day": {"utilization": 50, "resets_at": "2026-11-29T09:30:00Z"},
  "seven_day_opus": null,
  "limits": [
    {"kind": "weekly_scoped", "percent": 80, "resets_at": "2026-11-29T09:30:00Z",
     "scope": {"model": {"display_name": "Fable"}}},
    {"kind": "weekly_scoped", "percent": 10, "resets_at": null, "scope": {"model": {"display_name": "fable"}}},
    {"kind": "weekly_scoped", "percent": null, "scope": {"model": {"display_name": "Haiku"}}},
    {"kind": "something_new", "percent": 30}
  ],
  "seven_day_fable": {"utilization": 99, "resets_at": null},
  "extra_usage": {"is_enabled": true, "monthly_limit": 5000, "used_credits": 1250, "utilization": 24}
})";

/// Codex on Plus, with a model's own limits.
const QByteArray kCodexPlus = R"({
  "plan_type": "plus",
  "rate_limit": {
    "allowed": true,
    "limit_reached": false,
    "primary_window": {"used_percent": 23, "limit_window_seconds": 18000, "reset_after_seconds": 12600, "reset_at": 1795370400},
    "secondary_window": {"used_percent": 41.5, "limit_window_seconds": 604800, "reset_after_seconds": 404000, "reset_at": 1795762800}
  },
  "credits": {"has_credits": false, "unlimited": false, "balance": "0"},
  "additional_rate_limits": [
    {"limit_name": "GPT-5.3-Codex-Spark", "metered_feature": "codex_bengalfox",
     "rate_limit": {"allowed": true, "limit_reached": false,
                    "primary_window": {"used_percent": 5, "limit_window_seconds": 18000, "reset_at": 1795370400},
                    "secondary_window": null}},
    {"limit_name": "", "rate_limit": {"primary_window": {"used_percent": 1}}}
  ]
})";

/// Codex on Free: a month, no lengths given, a reset given only as seconds from now.
const QByteArray kCodexFree = R"({
  "plan_type": "free",
  "rate_limit": {"primary_window": {"used_percent": 130, "reset_after_seconds": 3600}, "secondary_window": null}
})";

class FakeTransport : public sync::HttpTransport {
public:
    struct Pending {
        sync::HttpRequest request;
        sync::HttpCallback done;
    };
    QList<Pending> pending;
    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        pending.append({request, std::move(done)});
    }
    /// Answers the oldest request (from the event loop, as a real transport does).
    void answer(int status, const QByteArray &body)
    {
        QVERIFY(!pending.isEmpty());
        Pending p = pending.takeFirst();
        sync::HttpResponse r;
        r.status = status;
        r.body = body;
        if (status == 0)
            r.networkError = u"Host not found"_s;
        QTimer::singleShot(0, [done = std::move(p.done), r]() mutable {
            done(r);
            done = nullptr; // (what it holds goes now, as a real transport's reply does)
        });
    }
};

} // namespace

class TestAgentLimits : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void providers_data()
    {
        QTest::addColumn<QStringList>("command");
        QTest::addColumn<QString>("agentName");
        QTest::addColumn<QString>("provider");
        QTest::newRow("Claude Code preset") << QStringList{u"npx"_s, u"-y"_s, u"@agentclientprotocol/claude-agent-acp"_s}
                                            << QString() << u"claude"_s;
        QTest::newRow("Codex preset") << QStringList{u"npx"_s, u"-y"_s, u"@agentclientprotocol/codex-acp"_s} << QString()
                                      << u"codex"_s;
        QTest::newRow("Zed's adapter") << QStringList{u"npx"_s, u"@zed-industries/claude-code-acp@latest"_s} << QString()
                                       << u"claude"_s;
        QTest::newRow("Zed's Codex") << QStringList{u"npx"_s, u"-y"_s, u"@zed-industries/codex-acp"_s} << QString()
                                     << u"codex"_s;
        QTest::newRow("a Nix store path")
                << QStringList{u"/nix/store/abc-claude-agent-acp-0.109.0/bin/claude-agent-acp"_s} << QString()
                << u"claude"_s;
        QTest::newRow("with the environment")
                << QStringList{u"env"_s, u"CODEX_HOME=/home/ola/praca"_s, u"codex-acp"_s} << QString() << u"codex"_s;
        QTest::newRow("Gemini CLI") << QStringList{u"npx"_s, u"-y"_s, u"@google/gemini-cli"_s, u"--acp"_s} << QString()
                                    << QString();
        QTest::newRow("OpenCode") << QStringList{u"npx"_s, u"-y"_s, u"opencode-ai"_s, u"acp"_s} << QString() << QString();
        QTest::newRow("own agent, initialize says Claude")
                << QStringList{u"/home/ola/bin/mój-agent"_s} << u"claude-agent-acp"_s << u"claude"_s;
        QTest::newRow("own agent, initialize says Codex") << QStringList{u"agent"_s} << u"codex-acp"_s << u"codex"_s;
        QTest::newRow("own agent") << QStringList{u"python3"_s, u"agent.py"_s} << u"Mój agent"_s << QString();
        QTest::newRow("nothing") << QStringList() << QString() << QString();
    }

    void providers()
    {
        QFETCH(QStringList, command);
        QFETCH(QString, agentName);
        QFETCH(QString, provider);
        QCOMPARE(providerName(providerOf(command, agentName)), provider);
    }

    void files_data()
    {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QStringList>("command");
        QTest::addColumn<QString>("environment"); // "VAR=value" or ""
        QTest::addColumn<QString>("file");
        const QStringList claude{u"npx"_s, u"-y"_s, u"@agentclientprotocol/claude-agent-acp"_s};
        QTest::newRow("Claude, the home folder") << u"claude"_s << claude << QString()
                                                 << u"/home/żaneta/.claude/.credentials.json"_s;
        QTest::newRow("Claude, the environment")
                << u"claude"_s << claude << u"CLAUDE_CONFIG_DIR=/srv/claude-praca"_s
                << u"/srv/claude-praca/.credentials.json"_s;
        QTest::newRow("Claude, the command's own")
                << u"claude"_s
                << QStringList{u"CLAUDE_CONFIG_DIR=/home/żaneta/.claude-druga"_s, u"npx"_s, u"claude-agent-acp"_s}
                << u"CLAUDE_CONFIG_DIR=/srv/inny"_s << u"/home/żaneta/.claude-druga/.credentials.json"_s;
        QTest::newRow("Claude, env with a tilde")
                << u"claude"_s << QStringList{u"env"_s, u"-u"_s, u"X"_s, u"CLAUDE_CONFIG_DIR=~/konta/firma"_s, u"claude-agent-acp"_s}
                << QString() << u"/home/żaneta/konta/firma/.credentials.json"_s;
        QTest::newRow("Claude, an assignment after the program is an argument")
                << u"claude"_s << QStringList{u"claude-agent-acp"_s, u"CLAUDE_CONFIG_DIR=/x"_s} << QString()
                << u"/home/żaneta/.claude/.credentials.json"_s;
        QTest::newRow("Codex, the home folder") << u"codex"_s << QStringList{u"codex-acp"_s} << QString()
                                                << u"/home/żaneta/.codex/auth.json"_s;
        QTest::newRow("Codex, CODEX_HOME") << u"codex"_s << QStringList{u"codex-acp"_s} << u"CODEX_HOME=/opt/codex/"_s
                                           << u"/opt/codex/auth.json"_s;
        QTest::newRow("neither") << QString() << QStringList{u"gemini"_s} << QString() << QString();
    }

    void files()
    {
        QFETCH(QString, provider);
        QFETCH(QStringList, command);
        QFETCH(QString, environment);
        QFETCH(QString, file);
        QHash<QString, QString> env;
        if (!environment.isEmpty())
            env.insert(environment.section(u'=', 0, 0), environment.section(u'=', 1));
        const Provider p = provider == u"claude" ? Provider::Claude : provider == u"codex" ? Provider::Codex : Provider::None;
        QCOMPARE(credentialsFile(p, command, env, u"/home/żaneta"_s), file);
    }

    void claudeCredentialsRead_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("failure");
        QTest::addColumn<QString>("token");
        QTest::addColumn<QString>("plan");
        QTest::addColumn<qint64>("expires");
        QTest::newRow("signed in")
                << QByteArray(R"({"claudeAiOauth":{"accessToken":" sk-ant-oat01-fake ","refreshToken":"sk-ant-ort01-fake",)"
                              R"("expiresAt":1795370400000,"scopes":["user:inference","user:profile"],)"
                              R"("subscriptionType":"max","rateLimitTier":"default_claude_max_20x"}})")
                << QString() << u"sk-ant-oat01-fake"_s << u"max"_s << qint64(1795370400000);
        QTest::newRow("no expiry") << QByteArray(R"({"claudeAiOauth":{"accessToken":"t0k3n"}})") << QString() << u"t0k3n"_s
                                   << QString() << qint64(0);
        QTest::newRow("no OAuth sign-in") << QByteArray(R"({"mcpOAuth":{}})") << u"no_credentials"_s << QString()
                                          << QString() << qint64(0);
        QTest::newRow("empty token") << QByteArray(R"({"claudeAiOauth":{"accessToken":""}})") << u"no_credentials"_s
                                     << QString() << QString() << qint64(0);
        QTest::newRow("a token that is no text") << QByteArray(R"({"claudeAiOauth":{"accessToken":12}})")
                                                 << u"no_credentials"_s << QString() << QString() << qint64(0);
        QTest::newRow("not JSON") << QByteArray("claudeAiOauth=abc") << u"unreadable"_s << QString() << QString()
                                  << qint64(0);
        QTest::newRow("an array") << QByteArray("[1,2]") << u"unreadable"_s << QString() << QString() << qint64(0);
        QTest::newRow("empty file") << QByteArray() << u"unreadable"_s << QString() << QString() << qint64(0);
    }

    void claudeCredentialsRead()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, failure);
        QFETCH(QString, token);
        QFETCH(QString, plan);
        QFETCH(qint64, expires);
        const Credentials c = claudeCredentials(json);
        QCOMPARE(failureCode(c.failure), failure);
        QCOMPARE(c.token, token);
        QCOMPARE(c.plan, plan);
        QCOMPARE(c.expiresAt.isValid() ? c.expiresAt.toMSecsSinceEpoch() : qint64(0), expires);
    }

    void codexCredentialsRead()
    {
        const QString id = jwt(QJsonObject{{u"https://api.openai.com/auth"_s,
                                            QJsonObject{{u"chatgpt_plan_type"_s, u"plus"_s},
                                                        {u"chatgpt_account_id"_s, u"konto-żółw"_s}}},
                                           {u"email"_s, u"ola@example.org"_s}});
        const QString access = jwt(QJsonObject{{u"exp"_s, 1795370400}});
        QJsonObject tokens{{u"id_token"_s, id}, {u"access_token"_s, access}, {u"refresh_token"_s, u"rt-fake"_s},
                           {u"account_id"_s, u"ws-1234"_s}};
        QJsonObject file{{u"OPENAI_API_KEY"_s, QJsonValue::Null}, {u"tokens"_s, tokens},
                         {u"last_refresh"_s, u"2026-11-20T10:00:00Z"_s}};
        Credentials c = codexCredentials(QJsonDocument(file).toJson());
        QVERIFY(c.ok());
        QCOMPARE(c.token, access);
        QCOMPARE(c.accountId, u"ws-1234"_s);
        QCOMPARE(c.plan, u"plus"_s);
        QCOMPARE(c.expiresAt, utc(2026, 11, 22, 18, 0));

        // Without account_id: the id token's workspace; an opaque access token has no expiry.
        tokens.remove(u"account_id"_s);
        tokens.insert(u"access_token"_s, u"opaque-token"_s);
        file.insert(u"tokens"_s, tokens);
        c = codexCredentials(QJsonDocument(file).toJson());
        QVERIFY(c.ok());
        QCOMPARE(c.accountId, u"konto-żółw"_s);
        QVERIFY(!c.expiresAt.isValid());

        // An API key's sign-in: no tokens, no subscription.
        c = codexCredentials(R"({"OPENAI_API_KEY":"sk-proj-fake"})");
        QCOMPARE(c.failure, Failure::ApiKey);
        c = codexCredentials(R"({"auth_mode":"apikey","tokens":null})");
        QCOMPARE(c.failure, Failure::ApiKey);
        c = codexCredentials(R"({"OPENAI_API_KEY":null})");
        QCOMPARE(c.failure, Failure::NoCredentials);
        c = codexCredentials(R"({"tokens":{"id_token":"zepsuty.token"}})");
        QCOMPARE(c.failure, Failure::NoCredentials);
        c = codexCredentials("not json");
        QCOMPARE(c.failure, Failure::Unreadable);
        // A malformed id token leaves the plan unknown, nothing else.
        c = codexCredentials(R"({"tokens":{"access_token":"a.b","id_token":"x.!!!.y","account_id":"w"}})");
        QVERIFY(c.ok());
        QVERIFY(c.plan.isEmpty());
    }

    void readFiles()
    {
        QCOMPARE(readCredentials(Provider::Claude, u"/nonexistent/brulion/.credentials.json"_s).failure,
                 Failure::NoCredentials);
        QCOMPARE(readCredentials(Provider::None, u"/etc/hostname"_s).failure, Failure::NoCredentials);
        QTemporaryDir dir;
        QFile f(dir.filePath(u".credentials.json"_s));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"claudeAiOauth":{"accessToken":"z-pliku"}})");
        f.close();
        QCOMPARE(readCredentials(Provider::Claude, f.fileName()).token, u"z-pliku"_s);
        // A folder where the file should be.
        QCOMPARE(readCredentials(Provider::Codex, dir.path()).failure, Failure::Unreadable);
    }

    void claudeAnswers_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("failure");
        QTest::addColumn<QStringList>("windows");
        QTest::newRow("Max") << kClaudeMax << QString()
                             << QStringList{u"five_hour session - 12 300 2026-11-22T18:00:00Z"_s,
                                            u"seven_day weekly - 68 10080 2026-11-27T07:00:00Z"_s,
                                            u"seven_day_opus model Opus 91 10080 2026-11-27T07:00:00Z"_s,
                                            u"seven_day_sonnet model Sonnet 3.5 10080 -"_s};
        QTest::newRow("a model's week in limits, extra usage")
                << kClaudeScoped << QString()
                << QStringList{u"five_hour session - 100 300 2026-11-22T16:00:00Z"_s,
                               u"seven_day weekly - 50 10080 2026-11-29T09:30:00Z"_s,
                               u"seven_day_fable model Fable 80 10080 2026-11-29T09:30:00Z"_s,
                               u"extra_usage monthly - 25 43200 -"_s};
        QTest::newRow("over 100 and under 0 clamped")
                << QByteArray(R"({"five_hour":{"utilization":130.2,"resets_at":"2026-11-22T18:00:00Z"},)"
                              R"("seven_day":{"utilization":-4,"resets_at":"zła data"}})")
                << QString()
                << QStringList{u"five_hour session - 100 300 2026-11-22T18:00:00Z"_s, u"seven_day weekly - 0 10080 -"_s};
        QTest::newRow("a window not started") << QByteArray(R"({"five_hour":{"utilization":null,"resets_at":null},)"
                                                            R"("seven_day":{"utilization":7,"resets_at":null}})")
                                              << QString() << QStringList{u"seven_day weekly - 7 10080 -"_s};
        QTest::newRow("windows as text") << QByteArray(R"({"five_hour":"12%"})") << QString() << QStringList();
        QTest::newRow("extra usage by utilization")
                << QByteArray(R"({"five_hour":null,"seven_day":null,"extra_usage":{"is_enabled":true,"monthly_limit":null,)"
                              R"("used_credits":null,"utilization":64}})")
                << QString() << QStringList{u"extra_usage monthly - 64 43200 -"_s};
        QTest::newRow("an error object") << QByteArray(R"({"type":"error","error":{"type":"authentication_error"}})")
                                         << u"malformed"_s << QStringList();
        QTest::newRow("HTML") << QByteArray("<html><body>Bad gateway</body></html>") << u"malformed"_s << QStringList();
        QTest::newRow("empty") << QByteArray() << u"malformed"_s << QStringList();
        QTest::newRow("null") << QByteArray("null") << u"malformed"_s << QStringList();
        QTest::newRow("cut off") << kClaudeMax.left(60) << u"malformed"_s << QStringList();
    }

    void claudeAnswers()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, failure);
        QFETCH(QStringList, windows);
        const Limits limits = claudeUsage(json, kChecked);
        QCOMPARE(failureCode(limits.failure), failure);
        QCOMPARE(lines(limits), windows);
        QCOMPARE(limits.provider, Provider::Claude);
        QCOMPARE(limits.checkedAt, kChecked);
    }

    void codexAnswers_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("failure");
        QTest::addColumn<QString>("plan");
        QTest::addColumn<QStringList>("windows");
        QTest::newRow("Plus") << kCodexPlus << QString() << u"plus"_s
                              << QStringList{u"primary session - 23 300 2026-11-22T18:00:00Z"_s,
                                             u"secondary weekly - 41.5 10080 2026-11-27T07:00:00Z"_s,
                                             u"model:GPT-5.3-Codex-Spark:primary model GPT-5.3-Codex-Spark 5 300 2026-11-22T18:00:00Z"_s};
        QTest::newRow("Free: a month") << kCodexFree << QString() << u"free"_s
                                       << QStringList{u"primary monthly - 100 43200 2026-11-22T15:30:00Z"_s};
        QTest::newRow("no lengths on Pro") << QByteArray(R"({"plan_type":"pro","rate_limit":{"primary_window":{"used_percent":2},)"
                                                         R"("secondary_window":{"used_percent":9,"reset_at":1796083200}}})")
                                           << QString() << u"pro"_s
                                           << QStringList{u"primary session - 2 300 -"_s,
                                                          u"secondary weekly - 9 10080 2026-12-01T00:00:00Z"_s};
        QTest::newRow("no limits on the plan") << QByteArray(R"({"plan_type":"team","rate_limit":null})") << QString()
                                               << u"team"_s << QStringList();
        QTest::newRow("percent as text") << QByteArray(R"({"rate_limit":{"primary_window":{"used_percent":"5"}}})")
                                         << QString() << QString() << QStringList();
        QTest::newRow("a detail object") << QByteArray(R"({"detail":"Unauthorized"})") << u"malformed"_s << QString()
                                         << QStringList();
        QTest::newRow("not JSON") << QByteArray("{plan_type: plus}") << u"malformed"_s << QString() << QStringList();
    }

    void codexAnswers()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, failure);
        QFETCH(QString, plan);
        QFETCH(QStringList, windows);
        const Limits limits = codexUsage(json, kChecked);
        QCOMPARE(failureCode(limits.failure), failure);
        QCOMPARE(limits.plan, plan);
        QCOMPARE(lines(limits), windows);
    }

    void levels()
    {
        QCOMPARE(levelOf(0), Level::Normal);
        QCOMPARE(levelOf(80), Level::Normal);
        QCOMPARE(levelOf(80.5), Level::Warning);
        QCOMPARE(levelOf(99.9), Level::Warning);
        QCOMPARE(levelOf(100), Level::Danger);
        QCOMPARE(claudeUsage(kClaudeMax, kChecked).highest(), 91.0);
        QCOMPARE(Limits().highest(), 0.0);
    }

    void requests()
    {
        Credentials c;
        c.token = u"sk-ant-oat01-fake"_s;
        sync::HttpRequest r = usageRequest(Provider::Claude, c);
        QCOMPARE(r.method, QByteArray("GET"));
        QCOMPARE(r.url, QUrl(u"https://api.anthropic.com/api/oauth/usage"_s));
        QCOMPARE(r.header("authorization"), QByteArray("Bearer sk-ant-oat01-fake"));
        QCOMPARE(r.header("anthropic-beta"), QByteArray("oauth-2025-04-20"));
        QVERIFY(r.header("user-agent").startsWith("claude-code/"));
        QVERIFY(!r.hasHeader("chatgpt-account-id"));
        QVERIFY(r.timeoutMs > 0);
        QCOMPARE(r.bodyLimit(), qint64(1024 * 1024));

        c.token = u"eyJ.fake.token"_s;
        c.accountId = u"ws-1234"_s;
        r = usageRequest(Provider::Codex, c);
        QCOMPARE(r.url, QUrl(u"https://chatgpt.com/backend-api/wham/usage"_s));
        QCOMPARE(r.url.scheme(), u"https"_s);
        QCOMPARE(r.header("authorization"), QByteArray("Bearer eyJ.fake.token"));
        QCOMPARE(r.header("chatgpt-account-id"), QByteArray("ws-1234"));
        QVERIFY(!r.hasHeader("anthropic-beta"));
    }

    void answers_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("failure");
        QTest::newRow("ok") << 200 << QString();
        QTest::newRow("no network") << 0 << u"offline"_s;
        QTest::newRow("expired token") << 401 << u"signed_out"_s;
        QTest::newRow("forbidden") << 403 << u"signed_out"_s;
        QTest::newRow("too many") << 429 << u"rate_limited"_s;
        QTest::newRow("server") << 503 << u"server_error"_s;
        QTest::newRow("not found") << 404 << u"server_error"_s;
    }

    void answers()
    {
        QFETCH(int, status);
        QFETCH(QString, failure);
        sync::HttpResponse r;
        r.status = status;
        r.body = kClaudeMax;
        const Limits limits = usageFrom(Provider::Claude, r, kChecked);
        QCOMPARE(failureCode(limits.failure), failure);
        QCOMPARE(limits.status, status == 200 ? 0 : status);
        QCOMPARE(limits.windows.isEmpty(), status != 200);
    }

    void reader()
    {
        FakeTransport transport;
        QDateTime now = kChecked;
        QHash<QString, Credentials> files;
        Credentials claude;
        claude.token = u"sk-ant-oat01-tajny-token"_s;
        claude.expiresAt = utc(2026, 11, 23, 0, 0);
        files.insert(u"/home/ola/.claude/.credentials.json"_s, claude);
        int reads = 0;
        auto reader = std::make_unique<LimitsReader>(&transport);
        reader->setClock([&now] { return now; });
        reader->setCredentialsReader([&files, &reads](Provider, const QString &file) {
            ++reads;
            if (!files.contains(file)) {
                Credentials none;
                none.failure = Failure::NoCredentials;
                return none;
            }
            return files.value(file);
        });
        QSignalSpy changed(reader.get(), &LimitsReader::changed);
        const Account account{Provider::Claude, u"/home/ola/.claude/.credentials.json"_s};
        QVERIFY(!reader->limits(account).checkedAt.isValid());

        QVERIFY(reader->refresh(account));
        QCOMPARE(transport.pending.size(), 1);
        QVERIFY(reader->busy(account));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().first().toString(), account.key());
        // While it is on its way the token is masked in the log.
        QCOMPARE(logging::redact(u"w logu: sk-ant-oat01-tajny-token"_s), u"w logu: ***"_s);
        // One at a time, even forced.
        QVERIFY(!reader->refresh(account, true));
        QCOMPARE(transport.pending.size(), 1);

        transport.answer(200, kClaudeMax);
        QTRY_VERIFY(!reader->busy(account));
        QVERIFY(reader->limits(account).ok());
        QCOMPARE(reader->limits(account).windows.size(), 4);
        QCOMPARE(reader->limits(account).checkedAt, kChecked);
        // The token is not kept: no longer a secret of the log's.
        QCOMPARE(logging::redact(u"w logu: sk-ant-oat01-tajny-token"_s), u"w logu: sk-ant-oat01-tajny-token"_s);

        // Not again within five minutes …
        now = kChecked.addSecs(4 * 60);
        QVERIFY(!reader->refresh(account));
        QCOMPARE(transport.pending.size(), 0);
        QCOMPARE(reads, 1);
        // … but forced (a manual refresh), and after them.
        QVERIFY(reader->refresh(account, true));
        transport.answer(401, R"({"error":"invalid token"})");
        QTRY_VERIFY(!reader->busy(account));
        QCOMPARE(reader->limits(account).failure, Failure::SignedOut);
        QCOMPARE(reader->limits(account).status, 401);
        QVERIFY(!reader->refresh(account));
        now = now.addSecs(5 * 60);
        QVERIFY(reader->refresh(account));
        transport.answer(0, {});
        QTRY_VERIFY(!reader->busy(account));
        QCOMPARE(reader->limits(account).failure, Failure::Offline);

        // An expired token: nothing goes out, and the next look tries again at once.
        now = utc(2026, 11, 23, 1, 0);
        QVERIFY(!reader->refresh(account));
        QCOMPARE(transport.pending.size(), 0);
        QCOMPARE(reader->limits(account).failure, Failure::Expired);
        const int before = reads;
        QVERIFY(!reader->refresh(account));
        QCOMPARE(reads, before + 1);

        // Another account (no sign-in file) is its own.
        const Account other{Provider::Codex, u"/home/ola/.codex/auth.json"_s};
        QVERIFY(!reader->refresh(other));
        QCOMPARE(reader->limits(other).failure, Failure::NoCredentials);
        QCOMPARE(reader->limits(other).provider, Provider::Codex);
        QCOMPARE(reader->limits(account).failure, Failure::Expired);
        QVERIFY(!reader->refresh(Account{}));

        // Destroyed with a request on its way: its answer goes nowhere.
        claude.expiresAt = QDateTime();
        files.insert(account.file, claude);
        QVERIFY(reader->refresh(account, true));
        reader.reset();
        transport.answer(200, kClaudeMax);
        QTest::qWait(10);
    }
};

QTEST_GUILESS_MAIN(TestAgentLimits)
#include "tst_agentlimits.moc"
