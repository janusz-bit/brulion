// RecentNotebooks — the recently used notebooks (AppSettings.recentVaults) as a list model for
// the start screen (OnboardingView, `AppController.recentNotebooks`): each with its name, its
// folder as people read it ("~/Notatki" on Linux, "C:\Users\…" on Windows), when it
// was last opened ("Today 18:30", "Yesterday 09:12", "Fri, Nov 13", "Oct 17, 2025") and whether
// its folder is there.
//
// The folders are checked off the GUI thread (a network share or a drive that hangs can take long
// to answer, or never), from the first refresh() on (the start screen shown; nothing before, so
// a run that never shows it checks nothing — and setAssumeAvailable() comes first): every
// entry's availability is "" until then, then "checking" (both shown as available), then
//   "available"   — a folder is there;
//   "missing"     — nothing there, or not a folder (deleted, moved, a drive not plugged in);
//   "unreachable" — no answer within timeout() (a network share that hangs), or a folder it may
//                   not read.
// refresh() checks them all again (the start screen shown, the window activated). In the browser
// every entry is "available" (its notebook lives in IndexedDB). A notebook the platform cannot
// see yet — on Android a folder on shared storage before "All files access" — is "available" as
// well (setAssumeAvailable(): opening it asks for the permission).
//
// Temporary folders: a notebook under the system's temporary folder (a test run's, an archive
// opened from a file manager) that is gone never comes back — it leaves the list instead of being
// marked (dropVanishedTemporary() at the start, and whenever a check finds one missing). Other
// missing notebooks stay, marked, until removed (remove(), "Remove from list"): a drive plugged
// in again brings its notebook back.
#pragma once

#include "dates/names.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace brulion::shell {

class AppSettings;

class RecentNotebooks : public QAbstractListModel {
    Q_OBJECT
    /// How many notebooks are listed.
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// How many of them are "missing" or "unreachable".
    Q_PROPERTY(int unavailableCount READ unavailableCount NOTIFY statesChanged FINAL)
    /// A folder was not checked yet, or its check is still waiting for an answer.
    Q_PROPERTY(bool checking READ checking NOTIFY statesChanged FINAL)

public:
    enum Role {
        PathRole = Qt::UserRole + 1, ///< the folder as stored (forward slashes)
        NameRole,                    ///< the notebook's name (its folder's name)
        DisplayPathRole,             ///< displayPath()
        LastOpenedRole,              ///< ISO local date-time, "" when unknown
        LastOpenedTextRole,          ///< lastOpenedText()
        AvailabilityRole,            ///< "" | "checking" | "available" | "missing" | "unreachable"
        TemporaryRole,               ///< under the system's temporary folder
    };

    explicit RecentNotebooks(AppSettings *settings, QObject *parent = nullptr);
    ~RecentNotebooks() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    int unavailableCount() const;
    bool checking() const;

    /// The app's clock (`--now`): "today" of the last-opened texts. Default: the system's.
    void setClock(std::function<QDateTime()> clock);
    /// The UI language of the last-opened texts ("pl" | "en").
    void setLanguage(const QString &language);
    /// The home folder shown as "~" in the paths ("" = none: Windows, Android — where "~" would
    /// be the app's private folder, not the user's).
    void setHome(const QString &home);
    /// Folders not to check: "available" at once (Android's shared storage without the permission).
    void setAssumeAvailable(std::function<bool(const QString &)> assume);
    /// What a check runs on its thread (default: probe()); tests block it to see a timeout.
    void setProbe(std::function<QString(const QString &)> probe);
    /// How long a check may take before its folder counts as "unreachable" (default 4 s).
    void setTimeout(int milliseconds);
    int timeout() const { return m_timeoutMs; }
    /// The folders that count as temporary (default: defaultTemporaryRoots()).
    void setTemporaryRoots(const QStringList &roots);

    /// Check every folder again (the ones already known keep their state until answered).
    Q_INVOKABLE void refresh();
    /// "Remove from list": the notebook leaves the recent notebooks (AppSettings).
    Q_INVOKABLE void remove(const QString &path);
    /// The availability of `path`'s folder ("" when it is not listed).
    Q_INVOKABLE QString availability(const QString &path) const;
    /// Row of `path`, -1 when not listed.
    Q_INVOKABLE int indexOf(const QString &path) const;
    /// A notebook found missing when it was opened (AppController::openRecentVault): a temporary
    /// one leaves the list, another is marked "missing" at once.
    void markMissing(const QString &path);
    /// Drop the temporary notebooks whose folders are gone (synchronous: local folders only);
    /// how many left the list.
    int dropVanishedTemporary();
    /// `path` is under one of the temporary folders.
    bool isTemporary(const QString &path) const;

    // ---- Pure helpers (tests) ---------------------------------------------------------------
    /// `path` is one of `roots` or inside one (cleaned, forward slashes; case-insensitive on
    /// Windows' drive letters and names).
    static bool isUnder(const QString &path, const QStringList &roots);
    /// The system's temporary folders: QDir::tempPath() (its canonical path too) and, outside
    /// Windows, /tmp and /var/tmp.
    static QStringList defaultTemporaryRoots();
    /// `path` as shown: inside `home` (when not empty) starting with "~", with the platform's
    /// separators ("C:\\Users\\…" on Windows).
    static QString displayPath(const QString &path, const QString &home);
    /// When a notebook was last opened, relative to `now`: "Today 18:30", "Yesterday 09:12" (with
    /// the time), "Fri, Nov 13" (this year), "Oct 17, 2025"; in Polish "Dziś 18:30", "Wczoraj
    /// 09:12", "Pt 13 lis", "17 paź 2025". "" for an invalid time.
    static QString lastOpenedText(const QDateTime &when, const QDateTime &now, dates::UiLocale locale);
    /// What is at `path`: "available" (a folder it may read), "unreachable" (a folder it may not
    /// read) or "missing". Blocks as long as the file system does.
    static QString probe(const QString &path);

Q_SIGNALS:
    void countChanged();
    void statesChanged();

private:
    struct Row {
        QString path;
        QString name;
        QString lastOpened;
        QString state;
        bool temporary = false;
    };

    void reload();
    void check(const QStringList &paths);
    /// The answer of check `generation` of `path` (0: one given at once, not by a thread).
    void answer(const QString &path, quint64 generation, const QString &state);
    void setState(const QString &path, const QString &state);
    void timedOut();
    QDateTime now() const;
    dates::UiLocale locale() const;

    AppSettings *m_settings;
    QList<Row> m_rows;
    /// The checks waiting for their folder's answer: path → the check's number.
    QHash<QString, quint64> m_running;
    quint64 m_nextGeneration = 0;
    std::function<QDateTime()> m_clock;
    std::function<bool(const QString &)> m_assumeAvailable;
    std::function<QString(const QString &)> m_probe;
    QString m_language = QStringLiteral("en");
    QString m_home;
    QStringList m_temporaryRoots;
    int m_timeoutMs = 4000;
    QTimer *m_timeout = nullptr;
    /// refresh() was called: the folders are checked (and the ones added later at once).
    bool m_checksStarted = false;
};

} // namespace brulion::shell
