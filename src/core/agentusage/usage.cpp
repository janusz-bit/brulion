#include "agentusage/usage.h"

#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::agentusage {

namespace {

/// A token count of a log: a non-negative whole number (anything else: 0).
qint64 count(const QJsonValue &value)
{
    const double d = value.toDouble(0);
    if (!std::isfinite(d) || d <= 0)
        return 0;
    return d >= 9e15 ? qint64(9e15) : qint64(d);
}

/// An ISO time as the agents write it ("2026-11-24T10:00:00.123Z", microseconds too), in ms since
/// the epoch (-1: none).
qint64 isoMsecs(const QJsonValue &value)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text.size() > 64)
        return -1;
    QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!t.isValid()) {
        static const QRegularExpression fraction(u"(\\.\\d{3})\\d+"_s);
        QString shorter = text;
        shorter.replace(fraction, u"\\1"_s);
        t = QDateTime::fromString(shorter, Qt::ISODateWithMs);
    }
    // (A time without its zone is the computer's: Qt reads it as local time.)
    return t.isValid() ? t.toMSecsSinceEpoch() : -1;
}

/// A drive's path ("C:\…", "c:/…") or a Windows share ("\\server\…").
bool windowsPath(const QString &path)
{
    return (path.size() >= 3 && path.at(1) == u':' && path.at(0).isLetter()
            && (path.at(2) == u'\\' || path.at(2) == u'/'))
           || path.startsWith(u"\\\\");
}

/// A folder as the logs name it, cleaned ("\" as "/" on a Windows path; no trailing "/").
QString cleanFolder(const QString &path)
{
    QString p = path.trimmed();
    if (p.isEmpty() || p.size() > 4096)
        return {};
    if (windowsPath(p))
        p.replace(u'\\', u'/');
    return QDir::cleanPath(p);
}

QJsonObject objectOf(const QByteArray &line, bool *malformed)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (malformed)
            *malformed = true;
        return {};
    }
    return doc.object();
}

quint8 tierFlags(const QString &model, qint64 prompt)
{
    const std::optional<Price> price = priceOf(model);
    return price && price->longAbove > 0 && prompt > price->longAbove ? quint8(LongContext) : quint8(0);
}

/// A Codex usage object ({input_tokens, cached_input_tokens, output_tokens, reasoning_output_tokens,
/// total_tokens}) as raw counts: input with the cached part in it, cacheRead the cached part.
bool codexUsage(const QJsonValue &value, Tokens *tokens, qint64 *total)
{
    if (!value.isObject())
        return false;
    const QJsonObject o = value.toObject();
    if (!o.value(u"input_tokens").isDouble() && !o.value(u"output_tokens").isDouble()
        && !o.value(u"total_tokens").isDouble())
        return false;
    tokens->input = count(o.value(u"input_tokens"));
    tokens->cacheRead = std::min(count(o.value(u"cached_input_tokens")), tokens->input);
    tokens->output = count(o.value(u"output_tokens"));
    tokens->reasoning = std::min(count(o.value(u"reasoning_output_tokens")), tokens->output);
    *total = o.value(u"total_tokens").isDouble() ? count(o.value(u"total_tokens")) : tokens->input + tokens->output;
    return true;
}

qint64 minus(qint64 a, qint64 b)
{
    return a > b ? a - b : 0;
}

/// A Codex model name from an object's model / model_name ("" none).
QString codexModel(const QJsonObject &o)
{
    for (const QString &key : {u"model"_s, u"model_name"_s}) {
        const QString m = o.value(key).toString().trimmed();
        if (!m.isEmpty())
            return m.left(200);
    }
    return {};
}

/// The event of a Codex turn's (or response's) `turn` usage at the line's time (nullopt: no time, no
/// tokens).
std::optional<Event> codexTurn(const QJsonObject &line, const CodexState &state, const Tokens &turn, const QString &key)
{
    const qint64 msecs = isoMsecs(line.value(u"timestamp"));
    if (msecs < 0 || (turn.input == 0 && turn.output == 0))
        return std::nullopt;
    Event e;
    e.key = key;
    e.msecs = msecs;
    e.model = state.model;
    e.project = state.project;
    e.tokens.cacheRead = turn.cacheRead;
    e.tokens.input = minus(turn.input, turn.cacheRead);
    e.tokens.output = turn.output;
    e.tokens.reasoning = turn.reasoning;
    e.tokens.requests = 1;
    if (state.fast)
        e.flags |= Fast;
    e.flags |= tierFlags(e.model, turn.input);
    return e;
}

} // namespace

Tokens &Tokens::operator+=(const Tokens &other)
{
    input += other.input;
    output += other.output;
    cacheWrite5m += other.cacheWrite5m;
    cacheWrite1h += other.cacheWrite1h;
    cacheRead += other.cacheRead;
    reasoning += other.reasoning;
    requests += other.requests;
    return *this;
}

bool mayCarryUsage(Provider provider, const QByteArray &line)
{
    if (provider == Provider::Claude)
        return line.contains("\"usage\"");
    if (provider == Provider::Codex)
        return line.contains("token_count") || line.contains("token_usage_record") || line.contains("turn_context")
               || line.contains("session_meta") || line.contains("thread_settings_applied");
    return false;
}

std::optional<Event> claudeEvent(const QByteArray &line, const QString &fallbackProject, bool *malformed)
{
    if (malformed)
        *malformed = false;
    const QJsonObject o = objectOf(line, malformed);
    if (o.isEmpty() || o.value(u"type").toString() != u"assistant")
        return std::nullopt;
    const QJsonObject message = o.value(u"message").toObject();
    const QJsonValue usageValue = message.value(u"usage");
    if (!usageValue.isObject())
        return std::nullopt;
    const QJsonObject usage = usageValue.toObject();
    if (!usage.value(u"input_tokens").isDouble() && !usage.value(u"output_tokens").isDouble())
        return std::nullopt;
    const qint64 msecs = isoMsecs(o.value(u"timestamp"));
    if (msecs < 0)
        return std::nullopt;
    Event e;
    e.msecs = msecs;
    e.model = message.value(u"model").toString().trimmed().left(200);
    e.tokens.input = count(usage.value(u"input_tokens"));
    e.tokens.output = count(usage.value(u"output_tokens"));
    e.tokens.cacheRead = count(usage.value(u"cache_read_input_tokens"));
    const qint64 written = count(usage.value(u"cache_creation_input_tokens"));
    const QJsonObject creation = usage.value(u"cache_creation").toObject();
    const qint64 hour = count(creation.value(u"ephemeral_1h_input_tokens"));
    const qint64 minutes = count(creation.value(u"ephemeral_5m_input_tokens"));
    // (Without the split, or a split smaller than the whole: the rest is a five minutes' write.)
    e.tokens.cacheWrite1h = hour;
    e.tokens.cacheWrite5m = std::max(minutes, minus(written, hour));
    e.tokens.reasoning = std::min(count(usage.value(u"output_tokens_details").toObject().value(u"thinking_tokens")),
                                  e.tokens.output);
    e.tokens.requests = 1;
    const QString id = message.value(u"id").toString();
    const QString request = o.value(u"requestId").toString();
    if (!id.isEmpty() && !request.isEmpty())
        e.key = id.left(200) + u':' + request.left(200);
    e.project = cleanFolder(o.value(u"cwd").toString());
    if (e.project.isEmpty())
        e.project = fallbackProject;
    if (usage.value(u"speed").toString() == u"fast")
        e.flags |= Fast;
    e.flags |= tierFlags(e.model, promptTokens(e.tokens));
    const QJsonValue cost = o.value(u"costUSD");
    if (cost.isDouble() && std::isfinite(cost.toDouble()) && cost.toDouble() >= 0 && cost.toDouble() < 1e6) {
        e.flags |= LoggedCost;
        e.loggedCost = cost.toDouble();
    }
    return e;
}

std::optional<Event> codexEvent(const QByteArray &line, CodexState &state, bool *malformed)
{
    if (malformed)
        *malformed = false;
    const QJsonObject o = objectOf(line, malformed);
    if (o.isEmpty())
        return std::nullopt;
    const QString type = o.value(u"type").toString();
    const QJsonObject payload = o.value(u"payload").toObject();
    if (type == u"session_meta") {
        if (const QString cwd = cleanFolder(payload.value(u"cwd").toString()); !cwd.isEmpty())
            state.project = cwd;
        return std::nullopt;
    }
    if (type == u"turn_context") {
        if (const QString model = codexModel(payload); !model.isEmpty())
            state.model = model;
        if (const QString cwd = cleanFolder(payload.value(u"cwd").toString());
            !cwd.isEmpty() && state.project.isEmpty())
            state.project = cwd;
        return std::nullopt;
    }
    // One API response's use (Codex 0.154 on): counted once by its response id — a fork's copied
    // records too —, and the rollout's token_count events no longer (each repeats the records'
    // sum, without the responses of a turn's agents).
    if (type == u"token_usage_record") {
        state.records = true;
        if (const QString model = codexModel(payload); !model.isEmpty())
            state.model = model;
        Tokens use;
        qint64 total = 0;
        if (!codexUsage(payload.value(u"usage"), &use, &total))
            return std::nullopt;
        const QString response = payload.value(u"response_id").toString().left(200);
        return codexTurn(o, state, use, response.isEmpty() ? QString() : u"codex:response:"_s + response);
    }
    if (type != u"event_msg")
        return std::nullopt;
    const QString kind = payload.value(u"type").toString();
    if (kind == u"thread_settings_applied") {
        const QJsonObject settings = payload.value(u"thread_settings").toObject();
        if (settings.contains(u"service_tier")) {
            const QString tier = settings.value(u"service_tier").toString().toLower();
            state.fast = tier == u"priority" || tier == u"fast" || tier == u"ultrafast";
        }
        return std::nullopt;
    }
    if (kind != u"token_count")
        return std::nullopt;
    const QJsonObject info = payload.value(u"info").toObject();
    if (info.isEmpty())
        return std::nullopt;
    if (const QString model = codexModel(info); !model.isEmpty())
        state.model = model;
    Tokens totals;
    qint64 totalTokens = 0;
    const bool hasTotals = codexUsage(info.value(u"total_token_usage"), &totals, &totalTokens);
    // A repeat (Codex sends the count again with the rate limits): the totals did not move.
    if (hasTotals && state.hasTotals && totals == state.totals && totalTokens == state.totalTokens)
        return std::nullopt;
    Tokens turn;
    qint64 turnTotal = 0;
    if (!codexUsage(info.value(u"last_token_usage"), &turn, &turnTotal)) {
        if (!hasTotals)
            return std::nullopt;
        const Tokens before = state.hasTotals ? state.totals : Tokens{};
        turn.input = minus(totals.input, before.input);
        turn.cacheRead = std::min(minus(totals.cacheRead, before.cacheRead), turn.input);
        turn.output = minus(totals.output, before.output);
        turn.reasoning = std::min(minus(totals.reasoning, before.reasoning), turn.output);
    }
    QString key;
    if (hasTotals) {
        state.hasTotals = true;
        state.totals = totals;
        state.totalTokens = totalTokens;
        // A fork repeats its parent's counts with their cumulative totals: one key for both.
        key = u"codex:%1:%2:%3:%4:%5"_s.arg(totals.input)
                      .arg(totals.cacheRead)
                      .arg(totals.output)
                      .arg(totals.reasoning)
                      .arg(totalTokens);
    }
    // (A rollout with token_usage_record lines counts those: every record comes before its count.)
    if (state.records)
        return std::nullopt;
    return codexTurn(o, state, turn, key);
}

QString claudeProjectFolder(const QString &path)
{
    QString out = path;
    for (QChar &c : out) {
        const char16_t u = c.unicode();
        const bool keep = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9');
        if (!keep)
            c = u'-';
    }
    return out;
}

bool inFolder(const QString &project, const QString &folder)
{
    const QString p = cleanFolder(project);
    const QString f = cleanFolder(folder);
    if (p.isEmpty() || f.isEmpty())
        return false;
    const Qt::CaseSensitivity cs = windowsPath(f) ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (p.compare(f, cs) == 0)
        return true;
    const QString prefix = f.endsWith(u'/') ? f : f + u'/';
    return p.startsWith(prefix, cs);
}

void Totals::add(const Tokens &t, quint8 flags, double loggedCost, const QString &model)
{
    tokens += t;
    if (flags & LoggedCost) {
        cost += loggedCost;
        return;
    }
    if (const std::optional<Price> price = priceOf(model)) {
        cost += costOf(t, *price, flags);
        return;
    }
    unpricedRequests += t.requests;
    unpricedTokens += t.total();
}

Totals &Totals::operator+=(const Totals &other)
{
    tokens += other.tokens;
    cost += other.cost;
    unpricedRequests += other.unpricedRequests;
    unpricedTokens += other.unpricedTokens;
    return *this;
}

Query lastDays(const QDate &today, int days, const QString &folder)
{
    Query q;
    q.to = today;
    q.from = today.addDays(-(std::max(days, 1) - 1));
    q.folder = folder;
    return q;
}

Report report(const History &history, const Query &query)
{
    Report out;
    out.query = query;
    if (!query.from.isValid() || !query.to.isValid() || query.from > query.to)
        return out;
    QMap<QDate, Totals> byDay;
    QHash<QString, Totals> byModel;
    QHash<QString, Totals> byProject;
    for (const DayUse &d : history.days) {
        if (d.date < query.from || d.date > query.to)
            continue;
        if (!query.folder.isEmpty() && !inFolder(d.project, query.folder))
            continue;
        Totals t;
        t.add(d.tokens, d.flags, d.loggedCost, d.model);
        out.total += t;
        byDay[d.date] += t;
        byModel[d.model] += t;
        byProject[d.project] += t;
    }
    for (QDate day = query.from; day <= query.to; day = day.addDays(1))
        out.days.append(DayTotals{day, byDay.value(day)});
    for (auto it = byModel.cbegin(); it != byModel.cend(); ++it)
        out.models.append(NamedTotals{it.key(), it.value()});
    for (auto it = byProject.cbegin(); it != byProject.cend(); ++it)
        out.projects.append(NamedTotals{it.key(), it.value()});
    std::ranges::sort(out.models, [](const NamedTotals &a, const NamedTotals &b) {
        if (a.totals.cost != b.totals.cost)
            return a.totals.cost > b.totals.cost;
        if (a.totals.tokens.total() != b.totals.tokens.total())
            return a.totals.tokens.total() > b.totals.tokens.total();
        return a.name < b.name;
    });
    std::ranges::sort(out.projects, [](const NamedTotals &a, const NamedTotals &b) {
        if (a.totals.tokens.total() != b.totals.tokens.total())
            return a.totals.tokens.total() > b.totals.tokens.total();
        return a.name < b.name;
    });
    return out;
}

} // namespace brulion::agentusage
