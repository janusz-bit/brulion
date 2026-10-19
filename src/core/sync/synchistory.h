// SyncHistory — the last runs of the sync engine for the UI ("Historia synchronizacji",
// "Szczegóły") and the exported log. Device-local, never synced; kept in a small LocalStore of
// its own (the app: <vault device state>/sync-history.json) — not in the sync bookkeeping store,
// whose flush rewrites every merge base.
#pragma once

#include "sync/synctypes.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>

#include <optional>

namespace brulion {
class LocalStore;
}

namespace brulion::sync {

/// How a run ended, from the user's point of view.
enum class SyncOutcome { Ok, Conflicts, FileErrors, NeedsConfirmation, Refused, Failed, Cancelled };
/// "ok" "conflicts" "files" "needs-confirmation" "refused" "failed" "cancelled"
QString syncOutcomeName(SyncOutcome outcome);
std::optional<SyncOutcome> syncOutcomeFromName(QStringView name);
/// cancelled > refused (syncError) > failed (remoteError) > needs-confirmation > files >
/// conflicts > ok. `stalled`: stopped by the scheduler's watchdog -> Failed.
SyncOutcome outcomeOf(const SyncReport &report, bool stalled = false);
/// The user has to act: auth, forbidden, not-found, parse, insufficient-storage, local-empty,
/// remote-empty, vault-unavailable (refused / failed with these codes), needs-confirmation, new
/// conflicts. Not: network, timeout, server, stalled, cancelled, per-file errors (Brulion retries
/// by itself).
bool needsUser(SyncOutcome outcome, const QString &errorCode);

/// One run as kept in the history (all texts technical and redacted).
struct SyncRunRecord {
    /// Caps of the lists kept per run.
    static constexpr int MaxErrors = 20;
    static constexpr int MaxConflicts = 20;

    int number = 0;
    SyncTrigger trigger = SyncTrigger::Unknown;
    QDateTime started; ///< local time
    QDateTime finished;
    qint64 durationMs = 0;
    SyncOutcome outcome = SyncOutcome::Ok;
    /// SyncReport::failureCode() ("auth", "timeout", "local-empty", …), "stalled", "files",
    /// "needs-confirmation", "conflicts"; "" for ok / cancelled.
    QString errorCode;
    QString errorMessage; ///< technical, redacted ("" for ok)
    int errorStatus = 0;  ///< HTTP status of the failure, when there was one
    int uploaded = 0;
    int downloaded = 0;
    int merged = 0;
    int deletedLocal = 0;
    int deletedRemote = 0;
    int conflicts = 0;
    int fileErrors = 0;
    int skipped = 0;
    int pendingDeletions = 0;
    QList<SyncPhaseTime> phases;
    SyncStats stats;
    QList<SyncFileError> errors;    ///< at most MaxErrors, redacted
    QList<VaultPath> conflictPaths; ///< at most MaxConflicts
    int repeats = 1;                ///< quiet runs folded into this record
    QDateTime firstStarted;         ///< start of the first folded run (repeats > 1)

    static SyncRunRecord fromReport(const SyncReport &report, bool stalled = false);
    /// Ok and nothing changed, skipped or failed.
    bool isQuiet() const;
    int changeCount() const;
    QJsonObject toJson() const;
    /// std::nullopt for malformed records (they are skipped when loading).
    static std::optional<SyncRunRecord> fromJson(const QJsonObject &json);
    friend bool operator==(const SyncRunRecord &, const SyncRunRecord &) = default;
};

QJsonObject toJson(const SyncPhaseTime &phase);
std::optional<SyncPhaseTime> phaseTimeFromJson(const QJsonObject &json);
QJsonObject toJson(const SyncStats &stats);
SyncStats statsFromJson(const QJsonObject &json);
QJsonObject toJson(const SyncFileError &error);
std::optional<SyncFileError> fileErrorFromJson(const QJsonObject &json);

/// The last runs, newest first, persisted in a LocalStore under the key "runs" (a JSON array).
class SyncHistory : public QObject {
    Q_OBJECT
public:
    static constexpr int DefaultCapacity = 20;
    /// `store` is not owned (nullptr: in memory only); loads the runs now.
    explicit SyncHistory(LocalStore *store, int capacity = DefaultCapacity, QObject *parent = nullptr);

    QList<SyncRunRecord> runs() const { return m_runs; } ///< newest first
    std::optional<SyncRunRecord> run(int number) const;
    /// Newest number + 1; 1 when empty.
    int nextNumber() const;
    /// Add a finished run. A quiet run after a quiet newest record folds into it: repeats++,
    /// number / finished / trigger / phases / stats of the newest run, durationMs the longest,
    /// firstStarted kept. Written to the store (its own debounce).
    void add(const SyncReport &report, bool stalled = false);
    void add(const SyncRunRecord &record);
    void clear();

Q_SIGNALS:
    void changed();

private:
    void save();

    LocalStore *m_store;
    int m_capacity;
    QList<SyncRunRecord> m_runs;
};

} // namespace brulion::sync
