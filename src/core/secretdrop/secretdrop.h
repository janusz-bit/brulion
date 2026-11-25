// SecretDrop — a secret the user gives an AI agent of this computer, handed over as a file only its
// owner can read (docs/UI.md "AI chat" → "A secret the agent asks for"; the app's ChatSecrets asks
// and delivers): the value never goes into the conversation, the event log or the agent's context —
// the agent reads it only inside the command that uses it (`--token "$(cat <file>)"`).
//
//   <base>/                     0700 (the app's runtime folder, else its cache: AppController)
//     run-3f9a…/                0700, this run's: removed when the app ends
//       secret-7c1e…            0600, the value as UTF-8 (no line end)
//     run-3f9a….lock            QLockFile: held while this run lives
//
// A file lives until discard() of its owner (the conversation: its turn ended, its agent's process
// ended) or its lifetime is up (at most MaxLifetimeMs), whichever comes first; the destructor
// removes this run's folder. sweep() — at the next start — removes what a run that crashed left:
// every run folder whose lock no live process holds. While a value lives (written, or held()
// without a file: the value given to the agent itself) it is registered with the event log's
// redaction (logging::ScopedSecret), so it never shows in a log line.
//
// GUI-free, single-threaded (its timers: the thread it lives in).
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

class QLockFile;

namespace brulion::secretdrop {

/// The longest a value lives.
inline constexpr qint64 MaxLifetimeMs = 15 * 60 * 1000;

class Drop : public QObject {
    Q_OBJECT
public:
    /// `base`: the folder of every run's folder (made 0700 when first written; "" none: write() fails).
    explicit Drop(const QString &base, QObject *parent = nullptr);
    ~Drop() override;

    QString base() const { return m_base; }
    /// This run's folder ("" before the first write()).
    QString folder() const { return m_folder; }

    /// A new owner-only file holding `value` for `owner` (a conversation's path), deleted after
    /// `lifetimeMs` (at most MaxLifetimeMs) or by discard(): its absolute path; "" with `*error`
    /// (no base, a base that is a link or not this user's to keep private, a disk that refuses).
    QString write(const QString &owner, const QString &value, qint64 lifetimeMs = MaxLifetimeMs,
                  QString *error = nullptr);
    /// `value` registered with the log's redaction for `owner` without a file (it went to the agent
    /// itself), for `lifetimeMs` or until discard().
    void hold(const QString &owner, const QString &value, qint64 lifetimeMs = MaxLifetimeMs);
    /// Deletes the files of `owner` and ends its values' redaction: how many there were.
    int discard(const QString &owner);
    void discardAll();
    /// The files `owner` has now.
    QStringList files(const QString &owner) const;
    /// `owner` has a file or a held value.
    bool holds(const QString &owner) const;
    /// The owners with anything now.
    QStringList owners() const;

    /// Removes the folders that runs which ended left in `base` (their lock held by no live
    /// process; this run's own never): how many.
    static int sweep(const QString &base);

Q_SIGNALS:
    /// A value's time was up: its file (if any) is gone.
    void expired(const QString &owner, const QString &path);

private:
    struct Entry;
    bool prepare(QString *error);
    void add(const QString &owner, const QString &path, const QString &value, qint64 lifetimeMs);
    void remove(qsizetype index);
    /// Drops what is due, and waits for the next one.
    void tick();

    QString m_base;
    QString m_folder;
    std::unique_ptr<QLockFile> m_lock;
    QList<std::shared_ptr<Entry>> m_entries;
    QTimer m_timer; // (the next value's end)
};

} // namespace brulion::secretdrop
