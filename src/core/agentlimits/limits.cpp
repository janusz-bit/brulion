#include "agentlimits/limits.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::agentlimits {

namespace {

constexpr int SessionMinutes = 5 * 60;
constexpr int WeekMinutes = 7 * 24 * 60;
constexpr int MonthMinutes = 30 * 24 * 60;
constexpr qint64 MaxFileBytes = 1024 * 1024;

double clampPercent(double value)
{
    if (!std::isfinite(value))
        return 0;
    return std::clamp(value, 0.0, 100.0);
}

/// An ISO time as the providers write it ("2026-11-22T18:00:00.123456+00:00", "…Z"), in UTC.
QDateTime isoTime(const QJsonValue &value)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return {};
    QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!t.isValid()) {
        // More than milliseconds (Python's microseconds): Qt reads three digits.
        static const QRegularExpression fraction(u"(\\.\\d{3})\\d+"_s);
        QString shorter = text;
        shorter.replace(fraction, u"\\1"_s);
        t = QDateTime::fromString(shorter, Qt::ISODateWithMs);
    }
    return t.isValid() ? t.toUTC() : QDateTime();
}

/// A time given in Unix seconds (0 or none: invalid).
QDateTime unixTime(const QJsonValue &value)
{
    const double seconds = value.toDouble(0);
    if (!std::isfinite(seconds) || seconds <= 0)
        return {};
    return QDateTime::fromSecsSinceEpoch(qint64(seconds), QTimeZone::UTC);
}

/// "opus" → "Opus", "sonnet_4" → "Sonnet 4".
QString modelName(const QString &key)
{
    QString out = key;
    out.replace(u'_', u' ');
    if (!out.isEmpty())
        out[0] = out.at(0).toUpper();
    return out;
}

/// The claims of a JWT's payload ({} when it is none).
QJsonObject jwtClaims(const QString &token)
{
    const QStringList parts = token.split(u'.');
    if (parts.size() != 3)
        return {};
    const QByteArray payload = QByteArray::fromBase64(parts.at(1).toLatin1(),
                                                      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QJsonDocument::fromJson(payload).object();
}

Kind kindFor(int minutes)
{
    if (minutes >= MonthMinutes)
        return Kind::Monthly;
    if (minutes >= 24 * 60)
        return Kind::Weekly;
    return Kind::Session;
}

/// A Codex window ({used_percent, limit_window_seconds, reset_after_seconds, reset_at}); nullopt
/// when it is none.
std::optional<Window> codexWindow(const QJsonValue &value, const QString &id, int fallbackMinutes,
                                  const QDateTime &checkedAt)
{
    const QJsonObject o = value.toObject();
    if (o.isEmpty() || !o.value(u"used_percent").isDouble())
        return std::nullopt;
    Window w;
    w.id = id;
    const double seconds = o.value(u"limit_window_seconds").toDouble(0);
    w.minutes = seconds > 0 && std::isfinite(seconds) ? int(std::lround(seconds / 60.0)) : fallbackMinutes;
    w.kind = kindFor(w.minutes);
    w.usedPercent = clampPercent(o.value(u"used_percent").toDouble());
    w.resetsAt = unixTime(o.value(u"reset_at"));
    if (!w.resetsAt.isValid() && checkedAt.isValid()) {
        const double after = o.value(u"reset_after_seconds").toDouble(-1);
        if (after >= 0 && std::isfinite(after))
            w.resetsAt = checkedAt.toUTC().addSecs(qint64(after));
    }
    return w;
}

int kindOrder(Kind kind)
{
    switch (kind) {
    case Kind::Session: return 0;
    case Kind::Weekly: return 1;
    case Kind::Model: return 2;
    case Kind::Monthly: return 3;
    }
    return 4;
}

void sortWindows(QList<Window> &windows)
{
    std::ranges::stable_sort(windows, {}, [](const Window &w) { return kindOrder(w.kind); });
}

/// `name` as a word of a command: its file name without ".exe"/".cmd", lower-case.
QString programName(const QString &word)
{
    QString base = QFileInfo(word).fileName().toLower();
    for (const QString &suffix : {u".exe"_s, u".cmd"_s, u".bat"_s, u".js"_s}) {
        if (base.endsWith(suffix))
            base.chop(suffix.size());
    }
    return base;
}

} // namespace

QString providerName(Provider provider)
{
    switch (provider) {
    case Provider::Claude: return u"claude"_s;
    case Provider::Codex: return u"codex"_s;
    case Provider::None: break;
    }
    return {};
}

Provider providerOf(const QStringList &command, const QString &agentName)
{
    for (const QString &word : command) {
        const QString lower = word.toLower();
        const QString program = programName(word);
        if (lower.contains(u"claude-agent-acp") || lower.contains(u"claude-code-acp") || program == u"claude")
            return Provider::Claude;
        if (lower.contains(u"codex-acp") || program == u"codex")
            return Provider::Codex;
    }
    const QString name = agentName.toLower();
    if (name.contains(u"claude"))
        return Provider::Claude;
    if (name.contains(u"codex"))
        return Provider::Codex;
    return Provider::None;
}

QString credentialsFile(Provider provider, const QStringList &command, const QHash<QString, QString> &environment,
                        const QString &home)
{
    const QString variable = provider == Provider::Claude  ? u"CLAUDE_CONFIG_DIR"_s
                             : provider == Provider::Codex ? u"CODEX_HOME"_s
                                                           : QString();
    if (variable.isEmpty())
        return {};
    // The assignments in front of the program (`VAR=value prog`, `env [-flags] VAR=value prog`).
    QString dir;
    const bool env = !command.isEmpty() && programName(command.first()) == u"env";
    for (qsizetype i = env ? 1 : 0; i < command.size(); ++i) {
        const QString &word = command.at(i);
        if (env && word.startsWith(u'-')) {
            if (word == u"-u" || word == u"-C" || word == u"-S")
                ++i; // (its argument)
            continue;
        }
        const qsizetype eq = word.indexOf(u'=');
        if (eq <= 0)
            break;
        if (word.left(eq) == variable)
            dir = word.mid(eq + 1);
    }
    if (dir.isEmpty())
        dir = environment.value(variable);
    if (dir.startsWith(u"~/") || dir == u"~")
        dir = home + dir.mid(1);
    if (dir.trimmed().isEmpty())
        dir = QDir(home).filePath(provider == Provider::Claude ? u".claude"_s : u".codex"_s);
    return QDir::cleanPath(QDir(dir).filePath(provider == Provider::Claude ? u".credentials.json"_s : u"auth.json"_s));
}

QString failureCode(Failure failure)
{
    switch (failure) {
    case Failure::None: return {};
    case Failure::NoCredentials: return u"no_credentials"_s;
    case Failure::Unreadable: return u"unreadable"_s;
    case Failure::ApiKey: return u"api_key"_s;
    case Failure::Expired: return u"expired"_s;
    case Failure::SignedOut: return u"signed_out"_s;
    case Failure::RateLimited: return u"rate_limited"_s;
    case Failure::Offline: return u"offline"_s;
    case Failure::ServerError: return u"server_error"_s;
    case Failure::Malformed: return u"malformed"_s;
    }
    return {};
}

Credentials claudeCredentials(const QByteArray &json)
{
    Credentials out;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        out.failure = Failure::Unreadable;
        return out;
    }
    const QJsonObject oauth = doc.object().value(u"claudeAiOauth").toObject();
    out.token = oauth.value(u"accessToken").toString().trimmed();
    if (out.token.isEmpty()) {
        out.failure = Failure::NoCredentials;
        return out;
    }
    const double expires = oauth.value(u"expiresAt").toDouble(0);
    if (expires > 0 && std::isfinite(expires))
        out.expiresAt = QDateTime::fromMSecsSinceEpoch(qint64(expires), QTimeZone::UTC);
    out.plan = oauth.value(u"subscriptionType").toString().trimmed().left(40);
    return out;
}

Credentials codexCredentials(const QByteArray &json)
{
    Credentials out;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        out.failure = Failure::Unreadable;
        return out;
    }
    const QJsonObject root = doc.object();
    const QJsonObject tokens = root.value(u"tokens").toObject();
    out.token = tokens.value(u"access_token").toString().trimmed();
    if (out.token.isEmpty()) {
        // An API key's sign-in has no tokens: nothing to show.
        const bool apiKey = !root.value(u"OPENAI_API_KEY").toString().trimmed().isEmpty()
                            || root.value(u"auth_mode").toString().compare(u"apikey", Qt::CaseInsensitive) == 0;
        out.failure = apiKey ? Failure::ApiKey : Failure::NoCredentials;
        return out;
    }
    out.accountId = tokens.value(u"account_id").toString().trimmed();
    const QJsonObject idClaims = jwtClaims(tokens.value(u"id_token").toString());
    const QJsonObject auth = idClaims.value(u"https://api.openai.com/auth").toObject();
    out.plan = auth.value(u"chatgpt_plan_type").toString().trimmed().left(40);
    if (out.accountId.isEmpty())
        out.accountId = auth.value(u"chatgpt_account_id").toString().trimmed();
    const double exp = jwtClaims(out.token).value(u"exp").toDouble(0);
    if (exp > 0 && std::isfinite(exp))
        out.expiresAt = QDateTime::fromSecsSinceEpoch(qint64(exp), QTimeZone::UTC);
    return out;
}

Credentials readCredentials(Provider provider, const QString &file)
{
    Credentials out;
    if (provider == Provider::None || file.isEmpty()) {
        out.failure = Failure::NoCredentials;
        return out;
    }
    QFile f(file);
    if (!f.exists()) {
        out.failure = Failure::NoCredentials;
        return out;
    }
    if (f.size() > MaxFileBytes || !f.open(QIODevice::ReadOnly)) {
        out.failure = Failure::Unreadable;
        return out;
    }
    const QByteArray json = f.read(MaxFileBytes);
    return provider == Provider::Claude ? claudeCredentials(json) : codexCredentials(json);
}

double Limits::highest() const
{
    double out = 0;
    for (const Window &w : windows)
        out = std::max(out, w.usedPercent);
    return out;
}

Level levelOf(double usedPercent)
{
    if (usedPercent >= 100.0)
        return Level::Danger;
    if (usedPercent > WarningPercent)
        return Level::Warning;
    return Level::Normal;
}

Limits claudeUsage(const QByteArray &json, const QDateTime &checkedAt)
{
    Limits out;
    out.provider = Provider::Claude;
    out.checkedAt = checkedAt;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        out.failure = Failure::Malformed;
        return out;
    }
    const QJsonObject root = doc.object();
    // A window may come without its utilization (not started): left out.
    const auto window = [](const QJsonValue &value, const QString &id, Kind kind, int minutes,
                           const QString &model) -> std::optional<Window> {
        const QJsonObject o = value.toObject();
        if (!o.value(u"utilization").isDouble())
            return std::nullopt;
        return Window{id, kind, model, minutes, clampPercent(o.value(u"utilization").toDouble()),
                      isoTime(o.value(u"resets_at"))};
    };
    if (auto w = window(root.value(u"five_hour"), u"five_hour"_s, Kind::Session, SessionMinutes, {}))
        out.windows.append(*w);
    if (auto w = window(root.value(u"seven_day"), u"seven_day"_s, Kind::Weekly, WeekMinutes, {}))
        out.windows.append(*w);
    // A model's own week: the newer list (weekly_scoped with a model), then seven_day_<model>.
    QStringList models;
    for (const QJsonValue &v : root.value(u"limits").toArray()) {
        const QJsonObject limit = v.toObject();
        const QString name = limit.value(u"scope").toObject().value(u"model").toObject().value(u"display_name").toString().trimmed();
        if (limit.value(u"kind").toString() != u"weekly_scoped" || name.isEmpty() || !limit.value(u"percent").isDouble()
            || models.contains(name.toLower()))
            continue;
        models.append(name.toLower());
        static const QRegularExpression separators(u"[^a-z0-9]+"_s);
        QString key = name.toLower();
        key.replace(separators, u"_"_s);
        out.windows.append(Window{u"seven_day_"_s + key, Kind::Model, name.left(80), WeekMinutes,
                                  clampPercent(limit.value(u"percent").toDouble()), isoTime(limit.value(u"resets_at"))});
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString key = it.key();
        if (!key.startsWith(u"seven_day_") || key == u"seven_day_oauth_apps")
            continue;
        const QString name = modelName(key.mid(10));
        if (name.isEmpty() || models.contains(name.toLower()))
            continue;
        if (auto w = window(it.value(), key, Kind::Model, WeekMinutes, name.left(80))) {
            models.append(name.toLower());
            out.windows.append(*w);
        }
    }
    // Extra usage, paid for beyond the plan: how much of its monthly limit is spent.
    const QJsonObject extra = root.value(u"extra_usage").toObject();
    if (extra.value(u"is_enabled").toBool()) {
        const double limit = extra.value(u"monthly_limit").toDouble(0);
        const double used = extra.value(u"used_credits").toDouble(-1);
        double percent = -1;
        if (limit > 0 && used >= 0)
            percent = used / limit * 100.0;
        else if (extra.value(u"utilization").isDouble())
            percent = extra.value(u"utilization").toDouble();
        if (percent >= 0 && std::isfinite(percent))
            out.windows.append(Window{u"extra_usage"_s, Kind::Monthly, {}, MonthMinutes, clampPercent(percent), {}});
    }
    if (out.windows.isEmpty() && !root.contains(u"five_hour") && !root.contains(u"seven_day"))
        out.failure = Failure::Malformed;
    sortWindows(out.windows);
    return out;
}

Limits codexUsage(const QByteArray &json, const QDateTime &checkedAt)
{
    Limits out;
    out.provider = Provider::Codex;
    out.checkedAt = checkedAt;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        out.failure = Failure::Malformed;
        return out;
    }
    const QJsonObject root = doc.object();
    out.plan = root.value(u"plan_type").toString().trimmed().left(40);
    // Free and Go plans have a month where the others have five hours.
    const bool monthly = out.plan == u"free" || out.plan == u"go";
    const QJsonObject rate = root.value(u"rate_limit").toObject();
    if (auto w = codexWindow(rate.value(u"primary_window"), u"primary"_s, monthly ? MonthMinutes : SessionMinutes,
                             checkedAt))
        out.windows.append(*w);
    if (auto w = codexWindow(rate.value(u"secondary_window"), u"secondary"_s, WeekMinutes, checkedAt))
        out.windows.append(*w);
    // A model's own limits (GPT-5.3-Codex-Spark …).
    for (const QJsonValue &v : root.value(u"additional_rate_limits").toArray()) {
        const QJsonObject limit = v.toObject();
        const QString name = limit.value(u"limit_name").toString().trimmed().left(80);
        if (name.isEmpty())
            continue;
        const QJsonObject r = limit.value(u"rate_limit").toObject();
        for (const auto &[position, fallback] : {std::pair{u"primary_window"_s, SessionMinutes},
                                                 std::pair{u"secondary_window"_s, WeekMinutes}}) {
            if (auto w = codexWindow(r.value(position), u"model:"_s + name + u':' + position.left(position.indexOf(u'_')),
                                     fallback, checkedAt)) {
                w->kind = Kind::Model;
                w->model = name;
                out.windows.append(*w);
            }
        }
    }
    if (out.windows.isEmpty() && !root.contains(u"rate_limit"))
        out.failure = Failure::Malformed;
    sortWindows(out.windows);
    return out;
}

sync::HttpRequest usageRequest(Provider provider, const Credentials &credentials)
{
    sync::HttpRequest r;
    r.method = "GET";
    r.timeoutMs = 15000;
    r.maxBodyBytes = MaxFileBytes;
    const auto header = [&r](const QByteArray &name, const QByteArray &value) { r.headers.append({name, value}); };
    header("Accept", "application/json");
    header("Authorization", "Bearer " + credentials.token.toUtf8());
    if (provider == Provider::Claude) {
        r.url = QUrl(QString::fromLatin1(ClaudeUsageUrl));
        header("anthropic-beta", "oauth-2025-04-20");
        header("User-Agent", ClaudeUserAgent);
    } else if (provider == Provider::Codex) {
        r.url = QUrl(QString::fromLatin1(CodexUsageUrl));
        header("OpenAI-Beta", "codex-1");
        header("Originator", "Codex Desktop");
        if (!credentials.accountId.isEmpty())
            header("Chatgpt-Account-Id", credentials.accountId.toUtf8());
    }
    return r;
}

Limits usageFrom(Provider provider, const sync::HttpResponse &response, const QDateTime &checkedAt)
{
    if (response.status >= 200 && response.status < 300) {
        return provider == Provider::Claude ? claudeUsage(response.body, checkedAt)
                                            : codexUsage(response.body, checkedAt);
    }
    Limits out;
    out.provider = provider;
    out.checkedAt = checkedAt;
    out.status = response.status;
    if (response.status == 0)
        out.failure = Failure::Offline;
    else if (response.status == 401 || response.status == 403)
        out.failure = Failure::SignedOut;
    else if (response.status == 429)
        out.failure = Failure::RateLimited;
    else
        out.failure = Failure::ServerError;
    return out;
}

} // namespace brulion::agentlimits
