// The subscription limits of an AI agent's account (docs/UI.md "AI agents (ACP)", "Its limits"):
// Claude Code's and Codex's — how much of each window (five hours, a week, a model's own week) the
// signed-in account has used and when it resets — read the way the agents' own programs read them:
//
//   Claude  GET https://api.anthropic.com/api/oauth/usage with the OAuth access token of
//           `${CLAUDE_CONFIG_DIR:-~/.claude}/.credentials.json` (claudeAiOauth.accessToken),
//           `anthropic-beta: oauth-2025-04-20` and Claude Code's user agent (the endpoint may answer
//           429 to another one): {five_hour, seven_day, seven_day_<model>, limits [weekly_scoped
//           with a model], extra_usage} — utilization in percent, resets_at an ISO time.
//   Codex   GET https://chatgpt.com/backend-api/wham/usage with the ChatGPT access token of
//           `${CODEX_HOME:-~/.codex}/auth.json` (tokens.access_token) and its workspace
//           (`Chatgpt-Account-Id`: tokens.account_id): {plan_type, rate_limit {primary_window,
//           secondary_window: used_percent, limit_window_seconds, reset_at (Unix seconds)},
//           additional_rate_limits [{limit_name, rate_limit}]}.
//
// Everything here is pure (JSON in, windows out; a request built from credentials) except
// readCredentials(), which reads a file. The token is never stored by this module beyond the
// Credentials value a caller holds for one request, and never written anywhere: the reader
// (agentlimits/reader.h) keeps it only while the request is on its way, registered with the
// event log's redaction. A token is never refreshed here (that would rotate the agent's own
// sign-in): an expired one says so, and the agent's program renews it when it runs.
#pragma once

#include "sync/http.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace brulion::agentlimits {

/// Whose limits.
enum class Provider {
    None,
    Claude, ///< Claude Code (claude-agent-acp, claude-code-acp, `claude`)
    Codex,  ///< Codex (codex-acp, `codex`)
};
/// "claude", "codex", "" for None.
QString providerName(Provider provider);

/// The provider an agent is: by its command line's words (the program and its arguments; npx's
/// package names count — "@agentclientprotocol/claude-agent-acp", "@zed-industries/codex-acp"),
/// else by what its initialize said it is (`agentName`: "claude-agent-acp", "codex-acp"). None
/// for anything else (Gemini CLI, OpenCode, an agent of the user's own).
Provider providerOf(const QStringList &command, const QString &agentName = {});

/// The file with the account's sign-in: Claude's `.credentials.json` in CLAUDE_CONFIG_DIR, Codex's
/// `auth.json` in CODEX_HOME — as the agent's command sets them (`VAR=value program …`, `env
/// VAR=value …`), else as `environment` does, else under `home` (~/.claude, ~/.codex). "" for None.
QString credentialsFile(Provider provider, const QStringList &command, const QHash<QString, QString> &environment,
                        const QString &home);

/// Why no limits can be shown.
enum class Failure {
    None,
    NoCredentials, ///< no sign-in file (not signed in, or the sign-in is in the system's keyring)
    Unreadable,    ///< the file cannot be read, or is no JSON with a token
    ApiKey,        ///< signed in with an API key: no subscription, no limits
    Expired,       ///< the token's own expiry is past (the agent renews it when it runs)
    SignedOut,     ///< the provider refused the token (401, 403)
    RateLimited,   ///< the provider asks to wait (429)
    Offline,       ///< no answer (no network, a timeout)
    ServerError,   ///< another answer (5xx, …)
    Malformed,     ///< an answer that is not the usage JSON
};
/// "no_credentials", "unreadable", "api_key", "expired", "signed_out", "rate_limited", "offline",
/// "server_error", "malformed"; "" for None.
QString failureCode(Failure failure);

/// The sign-in of an account, as read from its file. Held only for a request.
struct Credentials {
    QString token;          ///< the OAuth / ChatGPT access token ("" none)
    QString accountId;      ///< Codex: the ChatGPT workspace (tokens.account_id)
    QDateTime expiresAt;    ///< the token's own expiry (UTC; invalid: not known)
    QString plan;           ///< what the file says of the plan ("max", "pro", "plus"; "" not known)
    Failure failure = Failure::None;
    bool ok() const { return failure == Failure::None && !token.isEmpty(); }
};
/// Claude's `.credentials.json`: {"claudeAiOauth": {"accessToken", "expiresAt" (ms), "subscriptionType"}}.
Credentials claudeCredentials(const QByteArray &json);
/// Codex's `auth.json`: {"OPENAI_API_KEY", "tokens": {"access_token", "account_id", "id_token"}} —
/// the plan from the id token's claims, the expiry from the access token's when it is a JWT.
Credentials codexCredentials(const QByteArray &json);
/// The credentials of `provider` in `file` (at most 1 MiB is read): NoCredentials when it is not
/// there, Unreadable when it cannot be read.
Credentials readCredentials(Provider provider, const QString &file);

/// The kind of a window.
enum class Kind {
    Session, ///< the five hours (Claude's five_hour, Codex's primary window)
    Weekly,  ///< the week, all models
    Model,   ///< a model's own weekly allowance ("Opus", "GPT-5.3-Codex-Spark")
    Monthly, ///< a month (Codex Free's, Claude's extra usage spending)
};

/// One limit window of an account.
struct Window {
    QString id;          ///< stable per provider: "five_hour", "seven_day", "seven_day_opus", "primary", …
    Kind kind = Kind::Session;
    QString model;       ///< a Model window's model as the provider names it ("" others)
    int minutes = 0;     ///< how long the window is (300, 10080; 0 not known)
    double usedPercent = 0; ///< 0–100
    QDateTime resetsAt;  ///< UTC; invalid: not known (a window not started)
    friend bool operator==(const Window &, const Window &) = default;
};

/// The limits of one account at one time.
struct Limits {
    Provider provider = Provider::None;
    QString plan;           ///< "max", "pro", "plus", "team" … as the provider gives it ("" not known)
    QList<Window> windows;  ///< sessions first, then the week, the models' weeks, the months
    QDateTime checkedAt;    ///< when they were read (invalid: never)
    Failure failure = Failure::None;
    int status = 0;         ///< the HTTP status of a failed answer (0 none)
    bool ok() const { return failure == Failure::None; }
    /// The most used window's percentage (0 without windows).
    double highest() const;
};

/// How a percentage is shown: "warning" above 80 %, "danger" at 100 %.
enum class Level { Normal, Warning, Danger };
Level levelOf(double usedPercent);
inline constexpr double WarningPercent = 80.0;

/// Claude's usage answer (api/oauth/usage) as windows. Malformed when it is no JSON object or has
/// no window at all.
Limits claudeUsage(const QByteArray &json, const QDateTime &checkedAt);
/// Codex's usage answer (backend-api/wham/usage) as windows.
Limits codexUsage(const QByteArray &json, const QDateTime &checkedAt);

/// The usage endpoints (https only, the providers' own hosts).
inline constexpr char ClaudeUsageUrl[] = "https://api.anthropic.com/api/oauth/usage";
inline constexpr char CodexUsageUrl[] = "https://chatgpt.com/backend-api/wham/usage";
/// The user agent Claude's endpoint answers (Claude Code's).
inline constexpr char ClaudeUserAgent[] = "claude-code/2.1.273";

/// The request that reads `provider`'s limits with `credentials` (GET, 15 s, at most 1 MiB).
sync::HttpRequest usageRequest(Provider provider, const Credentials &credentials);
/// `response` (of usageRequest()) as limits: the windows of a 200, else the failure (401/403
/// SignedOut, 429 RateLimited, no answer Offline, …).
Limits usageFrom(Provider provider, const sync::HttpResponse &response, const QDateTime &checkedAt);

} // namespace brulion::agentlimits
