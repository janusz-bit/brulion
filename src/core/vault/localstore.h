// LocalStore — a small persistent JSON key/value store kept on this device (never synced, never
// inside the vault): trash metadata, sync bookkeeping, UI state, caches.
//
//   LocalStore store(appDataDir + "/store.json");   // loads the file now; "" = in memory only
//   store.set("sync/etag/Notatka.md", "\"abc\"");   // QJsonValue; Undefined removes the key
//   store.get(key, fallback), contains(key), remove(key), removePrefix(prefix), clear()
//   store.keys(prefix) (sorted), values(prefix) -> QJsonObject {key: value}
//   store.flush()                                   // write now (also done by the destructor)
//
// Changes are kept in memory and written after `flushDelay()` ms (debounced: one save per
// burst of changes) with QSaveFile, so the file is always either the old or the new version.
// Robust against concurrent writers of the same file (a second app instance): before saving,
// the store re-reads the file and, if somebody else changed it, applies only its own pending
// changes on top of that version. A file that is not valid JSON is kept as "<file>.corrupt"
// and the store starts empty (with a warning). Everything is synchronous on the owner's thread.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace brulion {

class LocalStore : public QObject {
    Q_OBJECT
public:
    /// Default debounce delay of saves, in milliseconds.
    static constexpr int kDefaultFlushDelay = 500;

    /// Opens (loads) the store at `filePath`; an empty path gives a store that lives in memory.
    explicit LocalStore(const QString &filePath, QObject *parent = nullptr);
    /// Writes pending changes.
    ~LocalStore() override;
    Q_DISABLE_COPY_MOVE(LocalStore)

    QString filePath() const;
    /// False for in-memory stores (empty file path).
    bool isPersistent() const;

    /// Value of `key`, or `defaultValue` when there is none.
    QJsonValue get(const QString &key, const QJsonValue &defaultValue = QJsonValue(QJsonValue::Undefined)) const;
    bool contains(const QString &key) const;
    /// Store `value` under `key`; an Undefined value removes the key.
    void set(const QString &key, const QJsonValue &value);
    /// Remove `key`; returns whether it existed.
    bool remove(const QString &key);
    /// Remove every key starting with `prefix`; returns how many were removed.
    int removePrefix(const QString &prefix);
    /// Remove every key.
    void clear();

    /// Keys starting with `prefix`, sorted.
    QStringList keys(const QString &prefix = {}) const;
    /// Keys starting with `prefix` with their values.
    QJsonObject values(const QString &prefix = {}) const;
    qsizetype size() const;

    /// Write pending changes now (merging concurrent changes made to the file by others).
    /// Returns false (and keeps the changes pending) when the file cannot be written.
    bool flush();
    /// Re-read the file; pending changes of this store are applied on top of its content.
    bool reload();
    bool hasPendingChanges() const;

    /// Debounce delay for saves in ms; 0 writes synchronously on every change.
    void setFlushDelay(int msec);
    int flushDelay() const;

    /// Description of the last error.
    QString errorString() const;

Q_SIGNALS:
    /// A debounced save failed; the changes stay pending and are retried with the next change.
    void saveFailed(const QString &error);

private:
    using Map = QMap<QString, QJsonValue>;
    /// Read the file into `raw` and its keys into `out`. When the bytes equal `known` (content
    /// this store parsed or wrote before), `out` stays empty: the caller already has them.
    bool readFile(Map &out, QByteArray &raw, const QByteArray *known = nullptr);
    Map withPendingApplied(const Map &disk) const;
    void touch(const QString &key, const std::optional<QJsonValue> &value);
    void scheduleFlush();
    void keepCorruptCopy();

    QString m_path;
    Map m_data;
    /// Changes not yet written: key -> new value, std::nullopt for removed keys.
    QHash<QString, std::optional<QJsonValue>> m_pending;
    bool m_cleared = false; ///< clear() pending: the file's keys are dropped on save
    QByteArray m_lastBytes; ///< file content as last read or written by this store
    QTimer m_timer;
    int m_flushDelay = kDefaultFlushDelay;
    QString m_error;
};

} // namespace brulion
