// What AI agents of this computer used — tokens, requests and what they would cost on the API —
// as their own session logs tell it (docs/UI.md "AI agents (ACP)", "Its usage"; T3 Code and
// ccusage read the same files). Pure: a log line in, an event out; events in, totals out; the
// price table. Reading the folders and the incremental cache: agentusage/history.h; off the GUI
// thread: agentusage/reader.h.
//
//   Claude Code  ${CLAUDE_CONFIG_DIR:-~/.claude}/projects/<folder>/*.jsonl (and the subagents'
//                files below them): an assistant line carries timestamp, cwd, requestId,
//                message.id, message.model and message.usage (input_tokens, output_tokens,
//                cache_creation_input_tokens split by cache_creation.ephemeral_5m/1h_input_tokens,
//                cache_read_input_tokens, output_tokens_details.thinking_tokens, speed), sometimes
//                costUSD. One message is often logged several times while it streams (and again
//                when a session is resumed into another file): it counts once, by message.id +
//                requestId, as ccusage counts it.
//   Codex        ${CODEX_HOME:-~/.codex}/sessions/YYYY/MM/DD/rollout-*.jsonl and archived_sessions
//                (also compressed by Codex, rollout-*.jsonl.zst: history.h reads them):
//                session_meta (cwd), turn_context (model), event_msg token_count with
//                info.total_token_usage (cumulative for the session) and info.last_token_usage
//                (the turn's) — input_tokens (cached_input_tokens included), output_tokens
//                (reasoning_output_tokens included); thread_settings_applied's service_tier
//                (priority: fast). Since Codex 0.154 a token_usage_record line per API response
//                (payload.usage, payload.response_id): a rollout that has them counts each response
//                once by its id (a fork's copies too) and its token_count events not — they leave
//                out the responses of a turn's own agents. Before: a token_count whose cumulative
//                totals did not move is a repeat and counts nothing; otherwise the turn's usage
//                counts (the difference of the totals when the event has no last_token_usage). A
//                fork's copied history repeats its parent's cumulative totals: those count once.
//
// The cost is an estimate of what the same tokens would cost on the provider's API (the price
// table below, dated) — a subscription pays nothing more for them. A costUSD in the log wins over
// the table. A model the table does not know: its tokens count, its cost is "not known".
#pragma once

#include "agentlimits/limits.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <optional>

namespace brulion::agentusage {

using agentlimits::Provider;

/// Tokens of some requests, by kind.
struct Tokens {
    qint64 input = 0;        ///< input not read from a cache (Codex: input_tokens − cached_input_tokens)
    qint64 output = 0;       ///< output, reasoning included
    qint64 cacheWrite5m = 0; ///< written to the prompt cache for five minutes (Claude)
    qint64 cacheWrite1h = 0; ///< written to it for an hour (Claude)
    qint64 cacheRead = 0;    ///< read from the cache (Claude's cache reads, Codex's cached input)
    qint64 reasoning = 0;    ///< of `output`: thinking / reasoning (shown apart, priced as output)
    qint64 requests = 0;

    qint64 cacheWrite() const { return cacheWrite5m + cacheWrite1h; }
    /// Every token (reasoning is part of output).
    qint64 total() const { return input + output + cacheWrite5m + cacheWrite1h + cacheRead; }
    bool isEmpty() const { return total() == 0 && requests == 0; }
    Tokens &operator+=(const Tokens &other);
    friend bool operator==(const Tokens &, const Tokens &) = default;
};

/// How a request was priced: fast mode (Claude's speed "fast", Codex's priority tier), a long
/// prompt (above the model's threshold: Sonnet 4.5's 200 K, the GPT-5.4+ 272 K), a cost the log
/// gave itself (costUSD).
enum Flag : quint8 {
    Fast = 1,
    LongContext = 2,
    LoggedCost = 4,
};

/// One request's use, as a log tells it.
struct Event {
    QString key;           ///< counted once by it ("" never deduplicated)
    qint64 msecs = 0;      ///< when (ms since the epoch)
    QString model;         ///< as the log names it ("claude-opus-5-5", "gpt-5.3-codex"; "" not known)
    QString project;       ///< the folder the agent ran in (the session's cwd, cleaned; "" not known)
    quint8 flags = 0;      ///< Flag
    Tokens tokens;         ///< requests = 1
    double loggedCost = 0; ///< USD, with LoggedCost
};

// ---- Prices -----------------------------------------------------------------------------------

/// USD per million tokens.
struct Rates {
    double input = 0;
    double output = 0;
    double cacheWrite5m = 0;
    double cacheWrite1h = 0;
    double cacheRead = 0;
};

/// A model's API price.
struct Price {
    Rates rates;
    qint64 longAbove = 0; ///< prompt tokens above which `longRates` apply (0: no such tier)
    Rates longRates;
    double fastFactor = 1; ///< fast mode / priority processing: the rates times this
};

/// The day the price table was last checked (the providers' pricing pages): an estimate as of it.
inline constexpr char PricesAsOf[] = "2026-11-24";

/// A model's name as the table knows it: lower case, without a provider prefix
/// ("anthropic.", "us.anthropic.", "openai/"), a date ("-20251101", "@20251101", "-2025-08-07") or
/// a context suffix ("[1m]").
QString normalizedModel(const QString &model);
/// The API price of `model` (nullopt: not known). `<synthetic>` (Claude Code's own messages)
/// costs nothing.
std::optional<Price> priceOf(const QString &model);
/// What `tokens` cost at `price`, priced as `flags` say (Fast, LongContext; LoggedCost is the
/// caller's).
double costOf(const Tokens &tokens, const Price &price, quint8 flags);
/// The prompt's size in tokens that decides the long-context tier (every input token, cached or not).
qint64 promptTokens(const Tokens &tokens);

// ---- Log lines --------------------------------------------------------------------------------

/// A Claude Code log line as an event (nullopt: no usage in it — a user line, a tool result, a
/// malformed one: `malformed` set). Prefers the line's cwd; else `fallbackProject`.
std::optional<Event> claudeEvent(const QByteArray &line, const QString &fallbackProject = {},
                                 bool *malformed = nullptr);

/// What a Codex rollout carries from line to line.
struct CodexState {
    bool hasTotals = false;
    Tokens totals;          ///< the last cumulative total_token_usage (input as input_tokens: cached included)
    qint64 totalTokens = 0; ///< its total_tokens
    QString model;          ///< the last turn_context's model
    QString project;        ///< session_meta's cwd
    bool fast = false;      ///< the last service tier is priority
    bool records = false;   ///< it has token_usage_record lines: they count, its token_count events not
    friend bool operator==(const CodexState &, const CodexState &) = default;
};
/// A Codex rollout line: updates `state` and gives the event of a token_count that counts
/// (nullopt: anything else, a repeated count, a malformed line: `malformed` set).
std::optional<Event> codexEvent(const QByteArray &line, CodexState &state, bool *malformed = nullptr);

/// The bytes a line must have to be parsed at all (the rest is skipped unread: huge tool results).
bool mayCarryUsage(Provider provider, const QByteArray &line);

/// Claude Code's folder name for a project ("/home/ola/Notatki" → "-home-ola-Notatki": every
/// character that is no ASCII letter or digit becomes "-").
QString claudeProjectFolder(const QString &path);

/// `project` is `folder` or inside it (paths compared cleaned, "\" as "/", a drive's letters in
/// any case).
bool inFolder(const QString &project, const QString &folder);

// ---- Totals -----------------------------------------------------------------------------------

/// Tokens and cost of some use.
struct Totals {
    Tokens tokens;
    double cost = 0;             ///< USD, of the priced part
    qint64 unpricedRequests = 0; ///< requests of models without a price
    qint64 unpricedTokens = 0;   ///< their tokens
    bool costKnown() const { return unpricedRequests == 0; }
    void add(const Tokens &tokens, quint8 flags, double loggedCost, const QString &model);
    Totals &operator+=(const Totals &other);
};

/// One day's use of one model in one project (the history the cache keeps, by local date).
struct DayUse {
    QDate date;
    QString model;
    QString project;
    quint8 flags = 0;
    Tokens tokens;
    double loggedCost = 0;
};

/// What the logs of one agent's folders hold: every day's use (in the time zone it was read in).
struct History {
    Provider provider = Provider::None;
    QList<DayUse> days;      ///< oldest first
    QDateTime readAt;        ///< when (invalid: never read)
    bool found = false;      ///< a log folder exists
    int files = 0;           ///< log files read (or known from the cache)
    qint64 skippedLines = 0; ///< malformed or too long lines left out
    QString error;           ///< why it could not be read ("" none)
};

/// A period and a filter of a report.
struct Query {
    QDate from;     ///< first day (local), inclusive
    QDate to;       ///< last day, inclusive
    QString folder; ///< only this project folder (and below); "" all
};
/// `days` days up to `today` (7 → today and the six before).
Query lastDays(const QDate &today, int days, const QString &folder = {});

struct DayTotals {
    QDate date;
    Totals totals;
};
struct NamedTotals {
    QString name; ///< a model, a project folder
    Totals totals;
};

/// The use of a period.
struct Report {
    Query query;
    Totals total;
    QList<DayTotals> days;       ///< every day of the period, oldest first (empty ones too)
    QList<NamedTotals> models;   ///< the most expensive first (then the most tokens)
    QList<NamedTotals> projects; ///< the most tokens first
};
Report report(const History &history, const Query &query);

} // namespace brulion::agentusage
