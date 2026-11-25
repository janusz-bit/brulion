// The agents' usage history (core agentusage): Claude Code's and Codex's session log lines,
// the price table, the folders read with the incremental cache, the days in local time, the
// reports and the reader's rhythm off the calling thread. Every log here is synthetic (built in
// the test), in the shapes the agents write.

#include "agentusage/history.h"
#include "agentusage/reader.h"
#include "agentusage/usage.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace brulion;
using namespace brulion::agentusage;
using namespace Qt::StringLiterals;

namespace {

QByteArray jsonLine(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

struct ClaudeUse {
    QString id = u"msg_01"_s;
    QString request = u"req_01"_s;
    QString time = u"2026-11-24T10:00:00.000Z"_s;
    QString model = u"claude-opus-5-5"_s;
    qint64 input = 100;
    qint64 output = 50;
    qint64 cache5m = 0;
    qint64 cache1h = 0;
    qint64 cacheRead = 0;
    QString cwd = u"/home/ola/Notatki"_s;
};

QJsonObject claudeObject(const ClaudeUse &u)
{
    QJsonObject usage{{u"input_tokens"_s, double(u.input)},
                      {u"output_tokens"_s, double(u.output)},
                      {u"cache_creation_input_tokens"_s, double(u.cache5m + u.cache1h)},
                      {u"cache_read_input_tokens"_s, double(u.cacheRead)},
                      {u"cache_creation"_s, QJsonObject{{u"ephemeral_5m_input_tokens"_s, double(u.cache5m)},
                                                        {u"ephemeral_1h_input_tokens"_s, double(u.cache1h)}}},
                      {u"service_tier"_s, u"standard"_s}};
    return QJsonObject{
            {u"type"_s, u"assistant"_s},
            {u"timestamp"_s, u.time},
            {u"cwd"_s, u.cwd},
            {u"sessionId"_s, u"s1"_s},
            {u"requestId"_s, u.request},
            {u"isSidechain"_s, false},
            {u"message"_s,
             QJsonObject{{u"id"_s, u.id},
                         {u"type"_s, u"message"_s},
                         {u"role"_s, u"assistant"_s},
                         {u"model"_s, u.model},
                         {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"Cześć"_s}}}},
                         {u"usage"_s, usage}}}};
}

QByteArray claudeLine(const ClaudeUse &u)
{
    return jsonLine(claudeObject(u));
}

QByteArray userLine(const QString &text)
{
    return jsonLine(QJsonObject{{u"type"_s, u"user"_s},
                                {u"timestamp"_s, u"2026-11-24T09:59:00.000Z"_s},
                                {u"message"_s, QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, text}}}});
}

QJsonObject codexUsageObject(qint64 input, qint64 cached, qint64 output, qint64 reasoning)
{
    return QJsonObject{{u"input_tokens"_s, double(input)},
                       {u"cached_input_tokens"_s, double(cached)},
                       {u"output_tokens"_s, double(output)},
                       {u"reasoning_output_tokens"_s, double(reasoning)},
                       {u"total_tokens"_s, double(input + output)}};
}

QByteArray codexMeta(const QString &cwd, const QString &time = u"2026-11-24T08:00:00.000Z"_s)
{
    return jsonLine(QJsonObject{{u"timestamp"_s, time},
                                {u"type"_s, u"session_meta"_s},
                                {u"payload"_s, QJsonObject{{u"id"_s, u"0199"_s}, {u"cwd"_s, cwd}}}});
}

QByteArray codexTurn(const QString &model, const QString &time = u"2026-11-24T08:00:01.000Z"_s)
{
    return jsonLine(QJsonObject{{u"timestamp"_s, time},
                                {u"type"_s, u"turn_context"_s},
                                {u"payload"_s, QJsonObject{{u"model"_s, model}, {u"cwd"_s, u"/x"_s}}}});
}

/// A token_count with cumulative totals and (when `last` is given) the turn's.
QByteArray codexCount(const QString &time, const QJsonObject &total, const QJsonObject &last = {})
{
    QJsonObject info{{u"total_token_usage"_s, total}};
    if (!last.isEmpty())
        info.insert(u"last_token_usage"_s, last);
    return jsonLine(QJsonObject{{u"timestamp"_s, time},
                                {u"type"_s, u"event_msg"_s},
                                {u"payload"_s, QJsonObject{{u"type"_s, u"token_count"_s}, {u"info"_s, info}}}});
}

/// A token_usage_record: one API response's use (Codex 0.154 on), `response` its id ("" none).
QByteArray codexRecord(const QString &time, const QString &response, const QJsonObject &usage)
{
    QJsonObject payload{
            {u"thread_id"_s, u"0199"_s}, {u"turn_id"_s, u"1"_s}, {u"usage"_s, usage}, {u"thread_token_usage"_s, usage}};
    if (!response.isEmpty())
        payload.insert(u"response_id"_s, response);
    return jsonLine(QJsonObject{{u"timestamp"_s, time}, {u"type"_s, u"token_usage_record"_s}, {u"payload"_s, payload}});
}

void writeFile(const QString &path, const QByteArray &bytes, bool append = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(append ? QIODevice::Append : QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

/// A file's time moved on (the cache knows a file by its size and time).
void touch(const QString &path, int seconds)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTimeUtc().addSecs(seconds), QFileDevice::FileModificationTime));
}

Source claudeSource(const QString &root)
{
    return Source{Provider::Claude, {QDir(root).filePath(u"projects"_s)}};
}

Source codexSource(const QString &root)
{
    return Source{Provider::Codex, {QDir(root).filePath(u"sessions"_s), QDir(root).filePath(u"archived_sessions"_s)}};
}

ReadOptions options(const QString &cache, const QTimeZone &zone = QTimeZone::UTC)
{
    ReadOptions o;
    o.cacheFile = cache;
    o.timeZone = zone;
    o.now = QDateTime(QDate(2026, 11, 25), QTime(12, 0), QTimeZone::UTC);
    return o;
}

Totals totalOf(const History &h, const QString &folder = {})
{
    return report(h, Query{QDate(2025, 1, 1), QDate(2027, 1, 1), folder}).total;
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-9;
}

} // namespace

class TestAgentUsage : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void modelNames_data();
    void modelNames();
    void prices();
    void claudeLines();
    void claudeLinesMalformed_data();
    void claudeLinesMalformed();
    void codexLines();
    void codexWithoutTurnUsage();
    void codexFast();
    void folders();
    void projectFolders();
    void sources();
    void claudeHistory();
    void claudeResumedSessionCountsOnce();
    void truncatedLastLine();
    void hugeLine();
    void codexHistory();
    void codexForkCountsOnce();
    void codexResponseRecords();
    void codexRecordsHistory();
    void incrementalCache();
    void rewrittenFile();
    void goneFileKeepsItsUse();
    void malformedCache();
    void localDays();
    void quarterHourZones();
    void retention();
    void symlinkOutOfFolder();
    void reports();
    void readerRhythm();
    void readerCancelled();
};

void TestAgentUsage::modelNames_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("normalized");
    QTest::addColumn<bool>("priced");
    QTest::newRow("opus 5.5") << u"claude-opus-5-5"_s << u"claude-opus-5-5"_s << true;
    QTest::newRow("dated") << u"claude-opus-4-1-20250805"_s << u"claude-opus-4-1"_s << true;
    QTest::newRow("sonnet 4") << u"claude-sonnet-4-20250514"_s << u"claude-sonnet-4"_s << true;
    QTest::newRow("1m") << u"claude-sonnet-4-5[1m]"_s << u"claude-sonnet-4-5"_s << true;
    QTest::newRow("bedrock") << u"us.anthropic.claude-opus-4-1-20250805-v1:0"_s << u"claude-opus-4-1"_s << true;
    QTest::newRow("vertex") << u"claude-opus-4-5@20251101"_s << u"claude-opus-4-5"_s << true;
    QTest::newRow("upper case") << u" Claude-Haiku-4-5 "_s << u"claude-haiku-4-5"_s << true;
    QTest::newRow("3.5 latest") << u"claude-3-5-sonnet-latest"_s << u"claude-3-5-sonnet"_s << true;
    QTest::newRow("gpt dated") << u"gpt-5-2025-08-07"_s << u"gpt-5"_s << true;
    QTest::newRow("openai prefix") << u"openai/gpt-5.3-codex"_s << u"gpt-5.3-codex"_s << true;
    QTest::newRow("codex mini latest") << u"codex-mini-latest"_s << u"codex-mini-latest"_s << true;
    QTest::newRow("gpt 6") << u"gpt-6.2-sol"_s << u"gpt-6.2-sol"_s << true;
    QTest::newRow("synthetic") << u"<synthetic>"_s << u"<synthetic>"_s << true;
    QTest::newRow("unknown") << u"llama-4-maverick"_s << u"llama-4-maverick"_s << false;
    QTest::newRow("spark") << u"gpt-5.3-codex-spark"_s << u"gpt-5.3-codex-spark"_s << false;
    QTest::newRow("empty") << QString() << QString() << false;
}

void TestAgentUsage::modelNames()
{
    QFETCH(QString, model);
    QFETCH(QString, normalized);
    QFETCH(bool, priced);
    QCOMPARE(normalizedModel(model), normalized);
    QCOMPARE(priceOf(model).has_value(), priced);
}

void TestAgentUsage::prices()
{
    // Opus 5.5: 4 / 20 per million, cache writes 1.25× and 2×, reads 0.20.
    const Price opus = *priceOf(u"claude-opus-5-5"_s);
    Tokens t;
    t.input = 1'000'000;
    t.output = 1'000'000;
    t.cacheWrite5m = 1'000'000;
    t.cacheWrite1h = 1'000'000;
    t.cacheRead = 1'000'000;
    QVERIFY(near(costOf(t, opus, 0), 4 + 20 + 5 + 8 + 0.20));
    QVERIFY(near(costOf(t, opus, Fast), 2 * (4 + 20 + 5 + 8 + 0.20)));
    // Sonnet 4.5 above 200 K prompt tokens: 6 / 22.50.
    const Price sonnet = *priceOf(u"claude-sonnet-4-5"_s);
    Tokens one;
    one.input = 1'000'000;
    QVERIFY(near(costOf(one, sonnet, 0), 3));
    QVERIFY(near(costOf(one, sonnet, LongContext), 6));
    // No long tier: the flag changes nothing.
    QVERIFY(near(costOf(one, opus, LongContext), 4));
    // Codex: cached input at its own price.
    const Price codex = *priceOf(u"gpt-5.3-codex"_s);
    Tokens c;
    c.input = 1'000'000;
    c.cacheRead = 1'000'000;
    c.output = 1'000'000;
    QVERIFY(near(costOf(c, codex, 0), 1.75 + 0.175 + 14));
    // Claude Code's own messages cost nothing.
    QVERIFY(near(costOf(t, *priceOf(u"<synthetic>"_s), 0), 0));
    QCOMPARE(QString::fromLatin1(PricesAsOf).size(), 10);
}

void TestAgentUsage::claudeLines()
{
    ClaudeUse u;
    u.input = 12;
    u.output = 345;
    u.cache5m = 1000;
    u.cache1h = 2000;
    u.cacheRead = 50000;
    u.cwd = u"/home/ola/Zażółć gęślą"_s;
    QJsonObject o = claudeObject(u);
    QJsonObject message = o.value(u"message").toObject();
    QJsonObject usage = message.value(u"usage").toObject();
    usage.insert(u"output_tokens_details"_s, QJsonObject{{u"thinking_tokens"_s, 100}});
    message.insert(u"usage"_s, usage);
    o.insert(u"message"_s, message);
    bool malformed = true;
    const std::optional<Event> e = claudeEvent(jsonLine(o), {}, &malformed);
    QVERIFY(e);
    QVERIFY(!malformed);
    QCOMPARE(e->key, u"msg_01:req_01"_s);
    QCOMPARE(e->model, u"claude-opus-5-5"_s);
    QCOMPARE(e->project, u"/home/ola/Zażółć gęślą"_s);
    QCOMPARE(e->msecs, QDateTime(QDate(2026, 11, 24), QTime(10, 0), QTimeZone::UTC).toMSecsSinceEpoch());
    QCOMPARE(e->tokens.input, 12);
    QCOMPARE(e->tokens.output, 345);
    QCOMPARE(e->tokens.cacheWrite5m, 1000);
    QCOMPARE(e->tokens.cacheWrite1h, 2000);
    QCOMPARE(e->tokens.cacheRead, 50000);
    QCOMPARE(e->tokens.reasoning, 100);
    QCOMPARE(e->tokens.requests, 1);
    QCOMPARE(e->flags, quint8(0));

    // Without the TTL split: a five minutes' write. Fast mode, a cost of its own, microseconds.
    usage.remove(u"cache_creation"_s);
    usage.insert(u"speed"_s, u"fast"_s);
    message.insert(u"usage"_s, usage);
    o.insert(u"message"_s, message);
    o.insert(u"costUSD"_s, 0.25);
    o.insert(u"timestamp"_s, u"2026-11-24T10:00:00.123456Z"_s);
    const std::optional<Event> f = claudeEvent(jsonLine(o));
    QVERIFY(f);
    QCOMPARE(f->tokens.cacheWrite5m, 3000);
    QCOMPARE(f->tokens.cacheWrite1h, 0);
    QCOMPARE(f->flags, quint8(Fast | LoggedCost));
    QCOMPARE(f->loggedCost, 0.25);
    QCOMPARE(f->msecs % 1000, 123);

    // No cwd: the file's last one. No request id: never deduplicated.
    o.remove(u"cwd"_s);
    o.remove(u"requestId"_s);
    const std::optional<Event> g = claudeEvent(jsonLine(o), u"/home/ola/Notatki"_s);
    QVERIFY(g);
    QCOMPARE(g->project, u"/home/ola/Notatki"_s);
    QVERIFY(g->key.isEmpty());

    // A long prompt of Sonnet 4.5.
    ClaudeUse big;
    big.model = u"claude-sonnet-4-5-20250929"_s;
    big.cacheRead = 250'000;
    QCOMPARE(claudeEvent(claudeLine(big))->flags, quint8(LongContext));
    // A Windows path, cleaned.
    big.cwd = u"C:\\Users\\Ola\\Notatki\\"_s;
    QCOMPARE(claudeEvent(claudeLine(big))->project, u"C:/Users/Ola/Notatki"_s);
}

void TestAgentUsage::claudeLinesMalformed_data()
{
    QTest::addColumn<QByteArray>("line");
    QTest::addColumn<bool>("malformed");
    QTest::newRow("not json") << QByteArray("{\"type\":\"assistant\",\"message\":{\"usage\"") << true;
    QTest::newRow("array") << QByteArray("[1,2,3]") << true;
    QTest::newRow("user") << userLine(u"usage?"_s) << false;
    QTest::newRow("no usage") << QByteArray(
            R"({"type":"assistant","timestamp":"2026-11-24T10:00:00Z","message":{"model":"claude-opus-5-5"}})")
                              << false;
    QTest::newRow("usage not object") << QByteArray(
            R"({"type":"assistant","timestamp":"2026-11-24T10:00:00Z","message":{"usage":7}})")
                                      << false;
    QTest::newRow("no counts") << QByteArray(
            R"({"type":"assistant","timestamp":"2026-11-24T10:00:00Z","message":{"usage":{"x":1}}})")
                               << false;
    QTest::newRow("no time") << QByteArray(
            R"({"type":"assistant","message":{"usage":{"input_tokens":1,"output_tokens":2}}})")
                             << false;
    QTest::newRow("bad time") << QByteArray(
            R"({"type":"assistant","timestamp":"wczoraj","message":{"usage":{"input_tokens":1}}})")
                              << false;
}

void TestAgentUsage::claudeLinesMalformed()
{
    QFETCH(QByteArray, line);
    QFETCH(bool, malformed);
    bool was = !malformed;
    QVERIFY(!claudeEvent(line, {}, &was));
    QCOMPARE(was, malformed);
    // Negative and absurd counts are none.
    const QByteArray odd =
            R"({"type":"assistant","timestamp":"2026-11-24T10:00:00Z","message":{"usage":{"input_tokens":-5,"output_tokens":1e300}}})";
    const std::optional<Event> e = claudeEvent(odd);
    QVERIFY(e);
    QCOMPARE(e->tokens.input, 0);
    QVERIFY(e->tokens.output > 0);
}

void TestAgentUsage::codexLines()
{
    CodexState state;
    QVERIFY(!codexEvent(codexMeta(u"/home/ola/Księgowość"_s), state));
    QCOMPARE(state.project, u"/home/ola/Księgowość"_s);
    QVERIFY(!codexEvent(codexTurn(u"gpt-5.3-codex"_s), state));
    QCOMPARE(state.model, u"gpt-5.3-codex"_s);

    // First turn: 1000 in (800 cached), 200 out (50 reasoning).
    std::optional<Event> a = codexEvent(codexCount(u"2026-11-24T08:01:00Z"_s, codexUsageObject(1000, 800, 200, 50),
                                                   codexUsageObject(1000, 800, 200, 50)),
                                        state);
    QVERIFY(a);
    QCOMPARE(a->tokens.input, 200);
    QCOMPARE(a->tokens.cacheRead, 800);
    QCOMPARE(a->tokens.output, 200);
    QCOMPARE(a->tokens.reasoning, 50);
    QCOMPARE(a->model, u"gpt-5.3-codex"_s);
    QCOMPARE(a->project, u"/home/ola/Księgowość"_s);
    QVERIFY(!a->key.isEmpty());
    // The same totals again (sent with the rate limits): a repeat.
    QVERIFY(!codexEvent(codexCount(u"2026-11-24T08:01:01Z"_s, codexUsageObject(1000, 800, 200, 50),
                                   codexUsageObject(1000, 800, 200, 50)),
                        state));
    // The second turn: cumulative 3000/2500/500/100, the turn's 2000/1700/300/50.
    std::optional<Event> b = codexEvent(codexCount(u"2026-11-24T08:02:00Z"_s, codexUsageObject(3000, 2500, 500, 100),
                                                   codexUsageObject(2000, 1700, 300, 50)),
                                        state);
    QVERIFY(b);
    QCOMPARE(b->tokens.input, 300);
    QCOMPARE(b->tokens.cacheRead, 1700);
    QCOMPARE(b->tokens.output, 300);
    QVERIFY(a->key != b->key);
    QCOMPARE(state.totals.input, 3000);
    // A count without info (rate limits only), other events, malformed lines: nothing.
    QVERIFY(!codexEvent(jsonLine(QJsonObject{{u"type"_s, u"event_msg"_s},
                                             {u"payload"_s, QJsonObject{{u"type"_s, u"token_count"_s},
                                                                        {u"info"_s, QJsonValue::Null}}}}),
                        state));
    QVERIFY(!codexEvent(jsonLine(QJsonObject{{u"type"_s, u"response_item"_s}, {u"payload"_s, QJsonObject{}}}), state));
    bool malformed = false;
    QVERIFY(!codexEvent("{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_cou", state, &malformed));
    QVERIFY(malformed);
    QCOMPARE(state.totals.input, 3000);
}

void TestAgentUsage::codexWithoutTurnUsage()
{
    // Older rollouts: only the cumulative totals — the differences count.
    CodexState state;
    codexEvent(codexTurn(u"gpt-5"_s), state);
    std::optional<Event> a =
            codexEvent(codexCount(u"2026-11-24T08:01:00Z"_s, codexUsageObject(1000, 0, 100, 0)), state);
    std::optional<Event> b =
            codexEvent(codexCount(u"2026-11-24T08:02:00Z"_s, codexUsageObject(1500, 400, 160, 10)), state);
    QVERIFY(a && b);
    QCOMPARE(a->tokens.input, 1000);
    QCOMPARE(b->tokens.input + b->tokens.cacheRead, 500);
    QCOMPARE(b->tokens.cacheRead, 400);
    QCOMPARE(b->tokens.output, 60);
    QCOMPARE(b->tokens.reasoning, 10);
    // A count without a time moves the totals on, but counts nothing.
    QVERIFY(!codexEvent(codexCount(u""_s, codexUsageObject(2000, 400, 200, 10)), state));
    std::optional<Event> c =
            codexEvent(codexCount(u"2026-11-24T08:04:00Z"_s, codexUsageObject(2100, 400, 210, 10)), state);
    QVERIFY(c);
    QCOMPARE(c->tokens.input, 100);
    QCOMPARE(c->tokens.output, 10);
}

void TestAgentUsage::codexFast()
{
    CodexState state;
    const auto settings = [](const QJsonObject &s) {
        return jsonLine(QJsonObject{
                {u"type"_s, u"event_msg"_s},
                {u"timestamp"_s, u"2026-11-24T08:00:00Z"_s},
                {u"payload"_s, QJsonObject{{u"type"_s, u"thread_settings_applied"_s}, {u"thread_settings"_s, s}}}});
    };
    codexEvent(codexTurn(u"gpt-5.4"_s), state);
    codexEvent(settings({{u"service_tier"_s, u"priority"_s}}), state);
    QVERIFY(state.fast);
    // A settings event without the tier leaves it.
    codexEvent(settings({{u"model"_s, u"gpt-5.4"_s}}), state);
    QVERIFY(state.fast);
    std::optional<Event> e =
            codexEvent(codexCount(u"2026-11-24T08:01:00Z"_s, codexUsageObject(300000, 0, 10, 0)), state);
    QVERIFY(e);
    QCOMPARE(e->flags, quint8(Fast | LongContext));
    codexEvent(settings({{u"service_tier"_s, u"default"_s}}), state);
    QVERIFY(!state.fast);
}

void TestAgentUsage::folders()
{
    QVERIFY(inFolder(u"/home/ola/Notatki"_s, u"/home/ola/Notatki"_s));
    QVERIFY(inFolder(u"/home/ola/Notatki/projekt/"_s, u"/home/ola/Notatki/"_s));
    QVERIFY(inFolder(u"/home/ola/Zażółć/a"_s, u"/home/ola/Zażółć"_s));
    QVERIFY(!inFolder(u"/home/ola/Notatki2"_s, u"/home/ola/Notatki"_s));
    QVERIFY(!inFolder(u"/home/ola"_s, u"/home/ola/Notatki"_s));
    QVERIFY(!inFolder(u"/home/ola/notatki"_s, u"/home/ola/Notatki"_s));
    QVERIFY(inFolder(u"c:/users/ola/notatki/x"_s, u"C:\\Users\\Ola\\Notatki"_s));
    QVERIFY(!inFolder(QString(), u"/home/ola"_s));
    QVERIFY(!inFolder(u"/home/ola"_s, QString()));
}

void TestAgentUsage::projectFolders()
{
    QCOMPARE(claudeProjectFolder(u"/var/lib/t3code/.t3/worktrees/brulion"_s),
             u"-var-lib-t3code--t3-worktrees-brulion"_s);
    QCOMPARE(claudeProjectFolder(u"/home/ola/Zażółć"_s), u"-home-ola-Za----"_s);
}

void TestAgentUsage::sources()
{
    const QHash<QString, QString> none;
    Source claude =
            sourceOf(Provider::Claude, {u"npx"_s, u"@agentclientprotocol/claude-agent-acp"_s}, none, u"/home/ola"_s);
    QCOMPARE(claude.folders, QStringList({u"/home/ola/.claude/projects"_s, u"/home/ola/.config/claude/projects"_s}));
    claude = sourceOf(Provider::Claude, {u"CLAUDE_CONFIG_DIR=~/praca"_s, u"claude-agent-acp"_s}, none, u"/home/ola"_s);
    QCOMPARE(claude.folders, QStringList({u"/home/ola/praca/projects"_s}));
    QVERIFY(claude.key().startsWith(u"claude:"_s));
    const Source codex =
            sourceOf(Provider::Codex, {u"codex-acp"_s}, {{u"CODEX_HOME"_s, u"/srv/codex"_s}}, u"/home/ola"_s);
    QCOMPARE(codex.folders, QStringList({u"/srv/codex/sessions"_s, u"/srv/codex/archived_sessions"_s}));
    QVERIFY(!sourceOf(Provider::None, {}, none, u"/home/ola"_s).valid());
    QVERIFY(cacheFileFor(u"/c"_s, codex).startsWith(u"/c/agent-usage/codex-"_s));
    QVERIFY(cacheFileFor(u"/c"_s, codex) != cacheFileFor(u"/c"_s, claude));
}

void TestAgentUsage::claudeHistory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString project = dir.filePath(u"projects/-home-ola-Za----"_s);
    ClaudeUse u;
    u.cwd = u"/home/ola/Zażółć"_s;
    // One message logged three times while it streams.
    QByteArray log = userLine(u"Napisz \"usage\""_s) + claudeLine(u) + claudeLine(u) + claudeLine(u);
    ClaudeUse second = u;
    second.id = u"msg_02"_s;
    second.request = u"req_02"_s;
    second.model = u"some-new-model"_s; // (unknown: tokens, no cost)
    second.input = 7;
    second.output = 3;
    log += claudeLine(second);
    log += "{\"type\":\"assistant\",\"message\":{\"usage\":{broken\n";
    writeFile(project + u"/s1.jsonl"_s, log);
    // A subagent's file below the session.
    ClaudeUse sub = u;
    sub.id = u"msg_03"_s;
    sub.request = u"req_03"_s;
    sub.model = u"claude-haiku-5-5"_s;
    writeFile(project + u"/s1/subagents/agent-a1.jsonl"_s, claudeLine(sub));
    // Not a log.
    writeFile(project + u"/s1/tool-results/x.txt"_s, claudeLine(sub));

    ReadStats stats;
    const History h = readHistory(claudeSource(dir.path()), options(dir.filePath(u"cache.json"_s)), &stats);
    QVERIFY(h.found);
    QVERIFY(h.error.isEmpty());
    QCOMPARE(h.files, 2);
    QCOMPARE(h.skippedLines, 1);
    QCOMPARE(stats.duplicates, 2);
    const Totals t = totalOf(h);
    QCOMPARE(t.tokens.requests, 3);
    QCOMPARE(t.tokens.input, 100 + 7 + 100);
    QCOMPARE(t.tokens.output, 50 + 3 + 50);
    QCOMPARE(t.unpricedRequests, 1);
    QCOMPARE(t.unpricedTokens, 10);
    QVERIFY(!t.costKnown());
    // 100 in + 50 out of Opus 5.5 (4 / 20) and of Haiku 5.5 (0.10 / 0.50).
    QVERIFY(near(t.cost, (100 * 4.0 + 50 * 20.0 + 100 * 0.10 + 50 * 0.50) / 1e6));
    const Report r = report(h, Query{QDate(2026, 11, 24), QDate(2026, 11, 24), u"/home/ola/Zażółć"_s});
    QCOMPARE(r.total.tokens.requests, 3);
    QCOMPARE(r.projects.size(), 1);
    QCOMPARE(r.projects.first().name, u"/home/ola/Zażółć"_s);
    QCOMPARE(report(h, Query{QDate(2026, 11, 24), QDate(2026, 11, 24), u"/home/ola/Inne"_s}).total.tokens.requests, 0);
}

void TestAgentUsage::claudeResumedSessionCountsOnce()
{
    // A resumed session copies the conversation into a file of its own: each message counts once.
    QTemporaryDir dir;
    const QString project = dir.filePath(u"projects/-home-ola-Notatki"_s);
    ClaudeUse a;
    ClaudeUse b = a;
    b.id = u"msg_02"_s;
    b.request = u"req_02"_s;
    writeFile(project + u"/first.jsonl"_s, claudeLine(a) + claudeLine(b));
    ClaudeUse c = a;
    c.id = u"msg_03"_s;
    c.request = u"req_03"_s;
    writeFile(project + u"/resumed.jsonl"_s, claudeLine(a) + claudeLine(b) + claudeLine(c));
    const History h = readHistory(claudeSource(dir.path()), options(dir.filePath(u"cache.json"_s)));
    QCOMPARE(totalOf(h).tokens.requests, 3);
    // And again from the cache, with a new copy appended to the second.
    writeFile(project + u"/resumed.jsonl"_s, claudeLine(c), true);
    touch(project + u"/resumed.jsonl"_s, 5);
    const History again = readHistory(claudeSource(dir.path()), options(dir.filePath(u"cache.json"_s)));
    QCOMPARE(totalOf(again).tokens.requests, 3);
}

void TestAgentUsage::truncatedLastLine()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(u"projects/p/s.jsonl"_s);
    ClaudeUse a;
    ClaudeUse b = a;
    b.id = u"msg_02"_s;
    b.request = u"req_02"_s;
    const QByteArray second = claudeLine(b);
    // The second line is still being written.
    writeFile(file, claudeLine(a) + second.left(40));
    const QString cache = dir.filePath(u"cache.json"_s);
    History h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 1);
    QCOMPARE(h.skippedLines, 0);
    writeFile(file, second.mid(40), true);
    touch(file, 5);
    h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 2);
    QCOMPARE(h.skippedLines, 0);
}

void TestAgentUsage::hugeLine()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(u"projects/p/s.jsonl"_s);
    ClaudeUse a;
    QJsonObject huge = claudeObject(a);
    huge.insert(u"padding"_s, QString(int(MaxLineBytes) + 10, u'x'));
    ClaudeUse b = a;
    b.id = u"msg_02"_s;
    b.request = u"req_02"_s;
    // A too long line between two ordinary ones (a picture in a tool result).
    ClaudeUse c = a;
    c.id = u"msg_03"_s;
    c.request = u"req_03"_s;
    writeFile(file, claudeLine(b) + jsonLine(huge) + claudeLine(c));
    const QString cache = dir.filePath(u"cache.json"_s);
    History h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 2);
    QCOMPARE(h.skippedLines, 1);
    // A huge line still being written at the end: skipped on, also across readings.
    QByteArray tail = jsonLine(huge);
    tail.chop(100);
    writeFile(file, tail, true);
    touch(file, 5);
    h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 2);
    ClaudeUse d = a;
    d.id = u"msg_04"_s;
    d.request = u"req_04"_s;
    writeFile(file, jsonLine(huge).right(100) + claudeLine(d), true);
    touch(file, 10);
    h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 3);
    QCOMPARE(h.skippedLines, 2);
}

void TestAgentUsage::codexHistory()
{
    QTemporaryDir dir;
    const QString name = u"rollout-2026-11-24T08-00-00-0199.jsonl"_s;
    const QString file = dir.filePath(u"sessions/2026/11/24/"_s + name);
    QByteArray log = codexMeta(u"/home/ola/Notatki"_s) + codexTurn(u"gpt-5.3-codex"_s);
    log += codexCount(u"2026-11-24T08:01:00Z"_s, codexUsageObject(1000, 800, 200, 50),
                      codexUsageObject(1000, 800, 200, 50));
    log += codexCount(u"2026-11-24T08:01:01Z"_s, codexUsageObject(1000, 800, 200, 50),
                      codexUsageObject(1000, 800, 200, 50));
    log += codexCount(u"2026-11-24T08:02:00Z"_s, codexUsageObject(3000, 2500, 500, 100),
                      codexUsageObject(2000, 1700, 300, 50));
    writeFile(file, log);
    // The same rollout archived: read once (sessions/ wins).
    writeFile(dir.filePath(u"archived_sessions/"_s + name), log);
    // Not a rollout.
    writeFile(dir.filePath(u"sessions/2026/11/24/notes.jsonl"_s), log);
    const History h = readHistory(codexSource(dir.path()), options(dir.filePath(u"cache.json"_s)));
    QCOMPARE(h.files, 1);
    const Totals t = totalOf(h);
    QCOMPARE(t.tokens.requests, 2);
    QCOMPARE(t.tokens.input + t.tokens.cacheRead, 3000);
    QCOMPARE(t.tokens.cacheRead, 2500);
    QCOMPARE(t.tokens.output, 500);
    QCOMPARE(t.tokens.reasoning, 100);
    QVERIFY(t.costKnown());
    QVERIFY(near(t.cost, (500 * 1.75 + 2500 * 0.175 + 500 * 14.0) / 1e6));
}

void TestAgentUsage::codexForkCountsOnce()
{
    QTemporaryDir dir;
    QByteArray parent = codexMeta(u"/home/ola/Notatki"_s) + codexTurn(u"gpt-5.3-codex"_s);
    parent +=
            codexCount(u"2026-11-24T08:01:00Z"_s, codexUsageObject(1000, 0, 100, 0), codexUsageObject(1000, 0, 100, 0));
    writeFile(dir.filePath(u"sessions/2026/11/24/rollout-2026-11-24T08-00-00-a.jsonl"_s), parent);
    // The fork repeats the parent's count (its cumulative totals), then goes on.
    QByteArray fork = codexMeta(u"/home/ola/Notatki"_s, u"2026-11-24T09:00:00Z"_s) + codexTurn(u"gpt-5.3-codex"_s);
    fork += codexCount(u"2026-11-24T09:00:00Z"_s, codexUsageObject(1000, 0, 100, 0), codexUsageObject(1000, 0, 100, 0));
    fork += codexCount(u"2026-11-24T09:01:00Z"_s, codexUsageObject(1500, 0, 150, 0), codexUsageObject(500, 0, 50, 0));
    writeFile(dir.filePath(u"sessions/2026/11/24/rollout-2026-11-24T09-00-00-b.jsonl"_s), fork);
    const History h = readHistory(codexSource(dir.path()), options(dir.filePath(u"cache.json"_s)));
    const Totals t = totalOf(h);
    QCOMPARE(t.tokens.requests, 2);
    QCOMPARE(t.tokens.input, 1500);
    QCOMPARE(t.tokens.output, 150);
}

void TestAgentUsage::codexResponseRecords()
{
    // A rollout of Codex 0.154 on: a record per API response, then the turn's token_count (its
    // totals the records' sum — without the responses of the turn's own agents).
    CodexState state;
    QVERIFY(!codexEvent(codexMeta(u"/home/ola/Księgowość"_s), state));
    QVERIFY(!codexEvent(codexTurn(u"gpt-6-astra"_s), state));
    QVERIFY(mayCarryUsage(Provider::Codex, codexRecord(u"2026-11-24T08:01:00Z"_s, u"resp_1"_s, {})));
    std::optional<Event> a = codexEvent(
            codexRecord(u"2026-11-24T08:01:00Z"_s, u"resp_1"_s, codexUsageObject(1000, 800, 200, 50)), state);
    QVERIFY(a);
    QVERIFY(state.records);
    QCOMPARE(a->key, u"codex:response:resp_1"_s);
    QCOMPARE(a->tokens.input, 200);
    QCOMPARE(a->tokens.cacheRead, 800);
    QCOMPARE(a->tokens.output, 200);
    QCOMPARE(a->tokens.reasoning, 50);
    QCOMPARE(a->tokens.requests, 1);
    QCOMPARE(a->model, u"gpt-6-astra"_s);
    QCOMPARE(a->project, u"/home/ola/Księgowość"_s);
    // A subagent's response in the same turn: a record of its own.
    std::optional<Event> b =
            codexEvent(codexRecord(u"2026-11-24T08:01:30Z"_s, u"resp_2"_s, codexUsageObject(500, 0, 40, 0)), state);
    QVERIFY(b);
    QCOMPARE(b->tokens.input, 500);
    // The turn's count: counted by the records already; its totals still kept.
    QVERIFY(!codexEvent(codexCount(u"2026-11-24T08:02:00Z"_s, codexUsageObject(1000, 800, 200, 50),
                                   codexUsageObject(1000, 800, 200, 50)),
                        state));
    QCOMPARE(state.totals.input, 1000);
    // A record without an id counts without a key; one without usage, or malformed, not at all.
    std::optional<Event> c =
            codexEvent(codexRecord(u"2026-11-24T08:03:00Z"_s, QString(), codexUsageObject(10, 0, 1, 0)), state);
    QVERIFY(c);
    QVERIFY(c->key.isEmpty());
    QVERIFY(!codexEvent(codexRecord(u"2026-11-24T08:03:00Z"_s, u"resp_3"_s, {}), state));
    QVERIFY(!codexEvent(codexRecord(u"2026-11-24T08:03:00Z"_s, u"resp_4"_s, codexUsageObject(0, 0, 0, 0)), state));
    bool malformed = false;
    QVERIFY(!codexEvent("{\"type\":\"token_usage_record\",\"payload\":{\"usa", state, &malformed));
    QVERIFY(malformed);
    // A record without a time: nothing.
    QVERIFY(!codexEvent(codexRecord(QString(), u"resp_5"_s, codexUsageObject(10, 0, 1, 0)), state));
}

void TestAgentUsage::codexRecordsHistory()
{
    QTemporaryDir dir;
    const QString name = u"sessions/2026/11/24/rollout-2026-11-24T08-00-00-ż.jsonl"_s;
    QByteArray log = codexMeta(u"/home/ola/Notatki"_s) + codexTurn(u"gpt-6-astra"_s);
    log += codexRecord(u"2026-11-24T08:01:00Z"_s, u"resp_1"_s, codexUsageObject(1000, 800, 200, 50));
    log += codexRecord(u"2026-11-24T08:01:30Z"_s, u"resp_2"_s, codexUsageObject(500, 0, 40, 0));
    log += codexCount(u"2026-11-24T08:02:00Z"_s, codexUsageObject(1000, 800, 200, 50),
                      codexUsageObject(1000, 800, 200, 50));
    writeFile(dir.filePath(name), log);
    // A fork copies its parent's records (the same response ids), then goes on.
    QByteArray fork = codexMeta(u"/home/ola/Notatki"_s, u"2026-11-24T09:00:00Z"_s) + codexTurn(u"gpt-6-astra"_s);
    fork += codexRecord(u"2026-11-24T08:01:00Z"_s, u"resp_1"_s, codexUsageObject(1000, 800, 200, 50));
    fork += codexRecord(u"2026-11-24T09:01:00Z"_s, u"resp_9"_s, codexUsageObject(3000, 0, 300, 0));
    writeFile(dir.filePath(u"sessions/2026/11/24/rollout-2026-11-24T09-00-00-b.jsonl"_s), fork);
    const QString cache = dir.filePath(u"cache.json"_s);
    ReadStats stats;
    History h = readHistory(codexSource(dir.path()), options(cache), &stats);
    Totals t = totalOf(h);
    QCOMPARE(t.tokens.requests, 3);
    QCOMPARE(t.tokens.input + t.tokens.cacheRead, 4500);
    QCOMPARE(t.tokens.output, 540);
    QCOMPARE(stats.duplicates, 1);
    // The rollout goes on — the state read back from the cache: its next count still counts nothing,
    // its next record counts.
    const QByteArray more =
            codexCount(u"2026-11-24T08:05:00Z"_s, codexUsageObject(1700, 800, 270, 50), codexUsageObject(700, 0, 70, 0))
            + codexRecord(u"2026-11-24T08:05:00Z"_s, u"resp_3"_s, codexUsageObject(700, 0, 70, 0));
    writeFile(dir.filePath(name), more, true);
    touch(dir.filePath(name), 5);
    h = readHistory(codexSource(dir.path()), options(cache), &stats);
    QCOMPARE(stats.filesRead, 1);
    QCOMPARE(stats.bytesRead, more.size());
    t = totalOf(h);
    QCOMPARE(t.tokens.requests, 4);
    QCOMPARE(t.tokens.input + t.tokens.cacheRead, 5200);
    QCOMPARE(t.tokens.output, 610);
}

void TestAgentUsage::incrementalCache()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(u"projects/p/s.jsonl"_s);
    const QString other = dir.filePath(u"projects/p/t.jsonl"_s);
    ClaudeUse a;
    writeFile(file, claudeLine(a));
    ClaudeUse b = a;
    b.id = u"msg_09"_s;
    b.request = u"req_09"_s;
    writeFile(other, claudeLine(b));
    const QString cache = dir.filePath(u"cache.json"_s);
    ReadStats stats;
    History h = readHistory(claudeSource(dir.path()), options(cache), &stats);
    QCOMPARE(stats.filesRead, 2);
    QCOMPARE(totalOf(h).tokens.requests, 2);
    QVERIFY(QFile::exists(cache));
#ifndef Q_OS_WIN // (Windows: the profile's ACLs keep it the user's; Qt reports no such bits there)
    QVERIFY(!(QFile::permissions(cache) & (QFileDevice::ReadOther | QFileDevice::ReadGroup)));
#endif

    // Nothing changed: no file opened.
    h = readHistory(claudeSource(dir.path()), options(cache), &stats);
    QCOMPARE(stats.filesRead, 0);
    QCOMPARE(stats.bytesRead, 0);
    QCOMPARE(totalOf(h).tokens.requests, 2);

    // Two lines appended to one: only they are read.
    ClaudeUse c = a;
    c.id = u"msg_02"_s;
    c.request = u"req_02"_s;
    c.input = 1;
    ClaudeUse d = c;
    d.id = u"msg_03"_s;
    d.request = u"req_03"_s;
    const QByteArray appended = claudeLine(c) + claudeLine(d);
    writeFile(file, appended, true);
    touch(file, 5);
    h = readHistory(claudeSource(dir.path()), options(cache), &stats);
    QCOMPARE(stats.filesRead, 1);
    QCOMPARE(stats.bytesRead, appended.size());
    QCOMPARE(stats.events, 2);
    QCOMPARE(totalOf(h).tokens.requests, 4);
    QCOMPARE(totalOf(h).tokens.input, 100 + 100 + 1 + 1);

    // Without a cache: everything again, the same totals.
    ReadOptions noCache = options(QString());
    const History whole = readHistory(claudeSource(dir.path()), noCache, &stats);
    QCOMPARE(stats.filesRead, 2);
    QVERIFY(totalOf(whole).tokens == totalOf(h).tokens);
}

void TestAgentUsage::rewrittenFile()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(u"projects/p/s.jsonl"_s);
    ClaudeUse a;
    ClaudeUse b = a;
    b.id = u"msg_02"_s;
    b.request = u"req_02"_s;
    writeFile(file, claudeLine(a) + claudeLine(b));
    const QString cache = dir.filePath(u"cache.json"_s);
    QCOMPARE(totalOf(readHistory(claudeSource(dir.path()), options(cache))).tokens.requests, 2);
    // Shorter: read again from its start.
    ClaudeUse c = a;
    c.id = u"msg_05"_s;
    c.request = u"req_05"_s;
    c.input = 9;
    writeFile(file, claudeLine(c));
    touch(file, 5);
    History h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 1);
    QCOMPARE(totalOf(h).tokens.input, 9);
    // As long as before but other first bytes: read again too.
    ClaudeUse e = c;
    e.id = u"msg_06"_s; // (the same length as msg_05)
    e.input = 8;
    writeFile(file, claudeLine(e));
    touch(file, 10);
    h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(totalOf(h).tokens.requests, 1);
    QCOMPARE(totalOf(h).tokens.input, 8);
}

void TestAgentUsage::goneFileKeepsItsUse()
{
    // Claude Code deletes old sessions: what was read of them stays in the history.
    QTemporaryDir dir;
    const QString file = dir.filePath(u"projects/p/old.jsonl"_s);
    ClaudeUse a;
    writeFile(file, claudeLine(a));
    const QString cache = dir.filePath(u"cache.json"_s);
    QCOMPARE(totalOf(readHistory(claudeSource(dir.path()), options(cache))).tokens.requests, 1);
    QVERIFY(QFile::remove(file));
    History h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(h.files, 0);
    QCOMPARE(totalOf(h).tokens.requests, 1);
    // Back again: read anew, not twice.
    writeFile(file, claudeLine(a));
    h = readHistory(claudeSource(dir.path()), options(cache));
    QCOMPARE(h.files, 1);
    QCOMPARE(totalOf(h).tokens.requests, 1);
}

void TestAgentUsage::malformedCache()
{
    QTemporaryDir dir;
    ClaudeUse a;
    writeFile(dir.filePath(u"projects/p/s.jsonl"_s), claudeLine(a));
    const QString cache = dir.filePath(u"cache.json"_s);
    const QList<QByteArray> broken = {
            QByteArray("{nie json"),
            QByteArray("[]"),
            QByteArray(R"({"version":99,"files":[]})"),
            QByteArray(R"({"version":1,"prices":"1999-01-01","files":[]})"),
            QByteArray(R"({"version":1,"prices":")") + PricesAsOf + R"(","source":"x","files":[]})",
    };
    for (const QByteArray &bytes : broken) {
        writeFile(cache, bytes);
        ReadStats stats;
        const History h = readHistory(claudeSource(dir.path()), options(cache), &stats);
        QVERIFY2(stats.cacheRebuilt, bytes.constData());
        QCOMPARE(totalOf(h).tokens.requests, 1);
    }
    // A file entry that is no file state: rebuilt.
    QFile f(cache);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QJsonArray files = root.value(u"files").toArray();
    QJsonObject first = files.first().toObject();
    first.insert(u"buckets"_s, QJsonArray{QJsonArray{1, 99, 99, 0}});
    files.replace(0, first);
    root.insert(u"files"_s, files);
    writeFile(cache, QJsonDocument(root).toJson());
    ReadStats stats;
    const History h = readHistory(claudeSource(dir.path()), options(cache), &stats);
    QVERIFY(stats.cacheRebuilt);
    QCOMPARE(totalOf(h).tokens.requests, 1);
}

void TestAgentUsage::localDays()
{
    const QTimeZone warsaw("Europe/Warsaw");
    if (!warsaw.isValid())
        QSKIP("No time zone data here");
    QTemporaryDir dir;
    QByteArray log;
    int n = 0;
    const auto at = [&](const QString &time) {
        ClaudeUse u;
        u.id = u"msg_%1"_s.arg(++n);
        u.request = u"req_%1"_s.arg(n);
        u.time = time;
        log += claudeLine(u);
    };
    // Autumn: 23:59 CEST on the 24th, 00:00 CEST on the 25th; the 25th is 25 hours long (until
    // 23:00 UTC).
    at(u"2026-10-24T21:59:00Z"_s);
    at(u"2026-10-24T22:00:00Z"_s);
    at(u"2026-10-25T00:30:00Z"_s);
    at(u"2026-10-25T22:59:00Z"_s);
    at(u"2026-10-25T23:00:00Z"_s);
    // Spring: 23:59 CET on 28 March, 00:00 CET on the 29th; the 29th ends at 22:00 UTC.
    at(u"2026-03-28T22:59:00Z"_s);
    at(u"2026-03-28T23:00:00Z"_s);
    at(u"2026-03-29T21:59:00Z"_s);
    at(u"2026-03-29T22:00:00Z"_s);
    writeFile(dir.filePath(u"projects/p/s.jsonl"_s), log);
    ReadOptions o = options(dir.filePath(u"cache.json"_s), warsaw);
    const History h = readHistory(claudeSource(dir.path()), o);
    const auto requests = [&h](const QDate &day) { return report(h, Query{day, day, {}}).total.tokens.requests; };
    QCOMPARE(requests(QDate(2026, 10, 24)), 1);
    QCOMPARE(requests(QDate(2026, 10, 25)), 3);
    QCOMPARE(requests(QDate(2026, 10, 26)), 1);
    QCOMPARE(requests(QDate(2026, 3, 28)), 1);
    QCOMPARE(requests(QDate(2026, 3, 29)), 2);
    QCOMPARE(requests(QDate(2026, 3, 30)), 1);
    // The same cache in UTC: other days.
    const History utc = readHistory(claudeSource(dir.path()), options(dir.filePath(u"cache.json"_s)));
    QCOMPARE(report(utc, Query{QDate(2026, 10, 24), QDate(2026, 10, 24), {}}).total.tokens.requests, 2);
}

void TestAgentUsage::quarterHourZones()
{
    const QTimeZone kathmandu("Asia/Kathmandu"); // +05:45
    if (!kathmandu.isValid())
        QSKIP("No time zone data here");
    QTemporaryDir dir;
    ClaudeUse a;
    a.time = u"2026-11-24T18:14:59Z"_s; // 23:59:59
    ClaudeUse b = a;
    b.id = u"msg_02"_s;
    b.request = u"req_02"_s;
    b.time = u"2026-11-24T18:15:00Z"_s; // 00:00 on the 25th
    writeFile(dir.filePath(u"projects/p/s.jsonl"_s), claudeLine(a) + claudeLine(b));
    const History h = readHistory(claudeSource(dir.path()), options(dir.filePath(u"cache.json"_s), kathmandu));
    QCOMPARE(report(h, Query{QDate(2026, 11, 24), QDate(2026, 11, 24), {}}).total.tokens.requests, 1);
    QCOMPARE(report(h, Query{QDate(2026, 11, 25), QDate(2026, 11, 25), {}}).total.tokens.requests, 1);
}

void TestAgentUsage::retention()
{
    QTemporaryDir dir;
    ClaudeUse old;
    old.time = u"2025-01-01T10:00:00Z"_s; // older than RetentionDays before "now" (2026-11-25)
    ClaudeUse recent;
    recent.id = u"msg_02"_s;
    recent.request = u"req_02"_s;
    writeFile(dir.filePath(u"projects/p/s.jsonl"_s), claudeLine(old) + claudeLine(recent));
    const History h = readHistory(claudeSource(dir.path()), options(dir.filePath(u"cache.json"_s)));
    QCOMPARE(totalOf(h).tokens.requests, 1);
    QCOMPARE(h.days.first().date, QDate(2026, 11, 24));
}

void TestAgentUsage::symlinkOutOfFolder()
{
    QTemporaryDir dir;
    ClaudeUse a;
    writeFile(dir.filePath(u"elsewhere/secret.jsonl"_s), claudeLine(a));
    writeFile(dir.filePath(u"projects/p/real.jsonl"_s), QByteArray());
    QDir().mkpath(dir.filePath(u"projects/q"_s));
    if (!QFile::link(dir.filePath(u"elsewhere/secret.jsonl"_s), dir.filePath(u"projects/q/link.jsonl"_s))
        || !QFile::link(dir.filePath(u"elsewhere"_s), dir.filePath(u"projects/linkdir"_s)))
        QSKIP("No symlinks here");
    // (Windows: QFile::link makes a shortcut, a file of its own that is no link.)
    if (!QFileInfo(dir.filePath(u"projects/q/link.jsonl"_s)).isSymbolicLink())
        QSKIP("QFile::link makes no symbolic links here");
    const History h = readHistory(claudeSource(dir.path()), options(QString()));
    QCOMPARE(totalOf(h).tokens.requests, 0);
    QCOMPARE(h.files, 1);
}

void TestAgentUsage::reports()
{
    History h;
    h.provider = Provider::Claude;
    const auto use = [](const QDate &d, const QString &model, const QString &project, qint64 input, quint8 flags = 0,
                        double logged = 0) {
        DayUse u;
        u.date = d;
        u.model = model;
        u.project = project;
        u.flags = flags;
        u.tokens.input = input;
        u.tokens.requests = 1;
        u.loggedCost = logged;
        return u;
    };
    h.days = {use(QDate(2026, 11, 20), u"claude-opus-5-5"_s, u"/home/ola/Notatki"_s, 1'000'000),
              use(QDate(2026, 11, 24), u"claude-haiku-5-5"_s, u"/home/ola/Notatki/projekt"_s, 2'000'000),
              use(QDate(2026, 11, 24), u"claude-sonnet-5-5"_s, u"/home/ola/Inne"_s, 500'000, LoggedCost, 1.5),
              use(QDate(2026, 10, 1), u"claude-opus-5-5"_s, u"/home/ola/Notatki"_s, 1'000'000)};
    const Report week = report(h, lastDays(QDate(2026, 11, 24), 7));
    QCOMPARE(week.query.from, QDate(2026, 11, 18));
    QCOMPARE(week.days.size(), 7);
    QCOMPARE(week.days.at(2).date, QDate(2026, 11, 20));
    QCOMPARE(week.days.at(2).totals.tokens.input, 1'000'000);
    QCOMPARE(week.days.at(3).totals.tokens.requests, 0);
    QCOMPARE(week.total.tokens.requests, 3);
    QVERIFY(near(week.total.cost, 4 + 0.2 + 1.5));
    // The most expensive model first.
    QCOMPARE(week.models.size(), 3);
    QCOMPARE(week.models.at(0).name, u"claude-opus-5-5"_s);
    QCOMPARE(week.models.at(1).name, u"claude-sonnet-5-5"_s);
    QCOMPARE(week.projects.at(0).name, u"/home/ola/Notatki/projekt"_s);
    const Report notebook = report(h, lastDays(QDate(2026, 11, 24), 7, u"/home/ola/Notatki"_s));
    QCOMPARE(notebook.total.tokens.requests, 2);
    QCOMPARE(report(h, lastDays(QDate(2026, 11, 24), 90)).total.tokens.requests, 4);
    QCOMPARE(lastDays(QDate(2026, 11, 24), 0).from, QDate(2026, 11, 24));
    QVERIFY(report(h, Query{QDate(2026, 11, 24), QDate(2026, 11, 1), {}}).days.isEmpty());
}

void TestAgentUsage::readerRhythm()
{
    QTemporaryDir dir;
    ClaudeUse a;
    writeFile(dir.filePath(u"projects/p/s.jsonl"_s), claudeLine(a));
    UsageReader reader(dir.filePath(u"cache"_s));
    QDateTime now(QDate(2026, 11, 25), QTime(12, 0), QTimeZone::UTC);
    reader.setClock([&now] { return now; });
    reader.setTimeZone(QTimeZone::UTC);
    const Source source = claudeSource(dir.path());
    QSignalSpy changed(&reader, &UsageReader::changed);
    QVERIFY(!reader.history(source).readAt.isValid());
    QVERIFY(reader.refresh(source));
    QVERIFY(reader.busy(source));
    QVERIFY(!reader.refresh(source, true)); // (one on its way)
    QTRY_VERIFY(!reader.busy(source));
    QCOMPARE(totalOf(reader.history(source)).tokens.requests, 1);
    QVERIFY(changed.count() >= 2);
    QCOMPARE(changed.first().first().toString(), source.key());
    QVERIFY(QFile::exists(cacheFileFor(dir.filePath(u"cache"_s), source)));
    // Within five minutes: not again, unless forced.
    now = now.addSecs(60);
    QVERIFY(!reader.refresh(source));
    QVERIFY(reader.refresh(source, true));
    QTRY_VERIFY(!reader.busy(source));
    now = now.addSecs(5 * 60);
    QVERIFY(reader.refresh(source));
    reader.waitForDone();
    QTRY_VERIFY(!reader.busy(source));
    QVERIFY(!reader.refresh(Source{}));
}

void TestAgentUsage::readerCancelled()
{
    // A reader destroyed while it reads cancels and waits; a cancelled reading writes no cache.
    QTemporaryDir dir;
    QByteArray log;
    for (int i = 0; i < 1000; ++i) {
        ClaudeUse u;
        u.id = u"msg_%1"_s.arg(i);
        u.request = u"req_%1"_s.arg(i);
        log += claudeLine(u);
    }
    for (int f = 0; f < 10; ++f)
        writeFile(dir.filePath(u"projects/p/s%1.jsonl"_s.arg(f)), log);
    {
        UsageReader reader(dir.filePath(u"cache"_s));
        QVERIFY(reader.refresh(claudeSource(dir.path())));
    }
    std::atomic_bool cancel{true};
    ReadOptions o = options(dir.filePath(u"other.json"_s));
    o.cancel = &cancel;
    ReadStats stats;
    const History h = readHistory(claudeSource(dir.path()), o, &stats);
    QCOMPARE(h.error, u"cancelled"_s);
    QVERIFY(stats.cancelled);
    QVERIFY(!QFile::exists(dir.filePath(u"other.json"_s)));
}

QTEST_GUILESS_MAIN(TestAgentUsage)
#include "tst_agentusage.moc"
