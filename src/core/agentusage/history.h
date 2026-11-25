// The usage history of an agent's session logs (agentusage/usage.h), read from its folders with an
// incremental cache: per log file its size, time, how far it was read, the state a Codex rollout
// carries, the keys of the messages counted (a message counts once in all files) and its use per
// quarter of an hour (UTC: every time zone's day is whole quarters of it), so a refresh reads only
// what was appended. A file that shrank or whose first bytes changed is read again from its start;
// one that is gone (Claude Code deletes sessions after 30 days) keeps its use in the cache, so the
// history outlives the agent's own clean-up, for RetentionDays.
//
// Reading: line by line in chunks (never a whole file), at most MaxLineBytes per line (a longer
// one is skipped — a tool result's picture), only lines that may carry usage parsed, a truncated
// last line (still being written) left for the next time, malformed lines skipped and counted;
// no symlink is followed out of a log folder, at most MaxFiles files. The cache is JSON, versioned
// (with the price table's date: a newer table reads everything again), written atomically and for
// the owner only, rebuilt when it is malformed. Blocking: run it off the GUI thread (reader.h).
#pragma once

#include "agentusage/usage.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <atomic>

namespace brulion::agentusage {

/// Where an agent's logs are.
struct Source {
    Provider provider = Provider::None;
    /// Claude: <config>/projects (CLAUDE_CONFIG_DIR's, else ~/.claude's and ~/.config/claude's);
    /// Codex: <CODEX_HOME>/sessions and archived_sessions.
    QStringList folders;
    /// "claude:/home/ola/.claude/projects|/home/ola/.config/claude/projects".
    QString key() const { return agentlimits::providerName(provider) + u':' + folders.join(u'|'); }
    bool valid() const { return provider != Provider::None && !folders.isEmpty(); }
    friend bool operator==(const Source &, const Source &) = default;
};

/// The log folders of `provider` as an agent's command and the environment set them (as
/// agentlimits::credentialsFile(): `VAR=… prog`, `env VAR=… prog`, else `environment`, else under
/// `home`).
Source sourceOf(Provider provider, const QStringList &command, const QHash<QString, QString> &environment,
                const QString &home);

inline constexpr int RetentionDays = 400;
inline constexpr qint64 MaxLineBytes = 4 * 1024 * 1024;
inline constexpr int MaxFiles = 50000;

struct ReadOptions {
    QString cacheFile;                         ///< "" none: every file read whole each time
    QTimeZone timeZone = QTimeZone::LocalTime; ///< whose days the history has
    QDateTime now;                             ///< default: now
    const std::atomic_bool *cancel = nullptr;  ///< set: stop soon, the cache left as it was
};

/// What a reading did (tests, the log).
struct ReadStats {
    int filesRead = 0;     ///< files opened because they changed
    qint64 bytesRead = 0;  ///< bytes read from them
    qint64 events = 0;     ///< requests counted this time
    qint64 duplicates = 0; ///< repeated messages left out
    bool cacheRebuilt = false;
    bool cancelled = false;
};

/// Reads the logs of `source` (blocking): only what changed since the cache last read them, then
/// writes the cache. Cancelled: History.error "cancelled", nothing written.
History readHistory(const Source &source, const ReadOptions &options, ReadStats *stats = nullptr);

/// The cache file of `source` in the app's cache folder: "<cacheDir>/agent-usage/claude-<hash>.json".
QString cacheFileFor(const QString &cacheDir, const Source &source);

} // namespace brulion::agentusage
