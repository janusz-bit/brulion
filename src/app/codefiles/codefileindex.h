// The project file index of the app (docs/CODE-FILES.md "The index"): FileIndexCache keeps one
// core codefiles::FileIndex per folder, built off the GUI thread and shared; CodeFileIndex (QML
// `CodeFileIndex`) is one view of it — a folder, a query, the ranked rows — for the file picker
// and a thread's @-mentions.
//
//   CodeFileIndex { folder: root.folder; query: field.text; limit: 50; kinds: "files"; live: popup.visible }
//   … rows: [{path, name, folder, isFolder, title, nameMarkup, folderMarkup, icon}]
//
// Building: `git -C <folder> ls-files -co --exclude-standard -z` (through gitcli::Client: no shell,
// no prompts, read-only), its output parsed on a pool thread; no git, or no repository: a walk of
// the folder on a pool thread (FileIndex::walk). At most 200 000 entries. While a build runs the
// last index answers. refresh() builds again (a build already running is followed by one more);
// a `live` view refreshes every 15 s; the Code view refreshes at each turn's end. At most 8
// folders are kept (the least recently asked for goes). Each index follows the disk by itself: a
// QFileSystemWatcher on the folder and its indexed folders (shallow first, at most MaxWatched)
// builds it again WatchDelayMs after a file is made, deleted or renamed in one of them (not on
// Windows: a watched folder cannot be deleted there).
//
// Queries: over up to 20 000 entries at once, on the GUI thread (well under a millisecond a
// thousand entries); bigger indexes on a pool thread, the newest query's result kept (rows change
// a moment later).
#pragma once

#include "codefiles/fileindex.h"

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <memory>

namespace brulion::gitcli {
class Client;
}

namespace brulion::codefiles {

class FileIndexCache : public QObject {
    Q_OBJECT
public:
    /// The app's one cache.
    static FileIndexCache *instance();
    explicit FileIndexCache(QObject *parent = nullptr);
    ~FileIndexCache() override;

    /// The index of `folder` as it is now (nullptr: none yet); asks for a build when there is none.
    std::shared_ptr<const FileIndex> index(const QString &folder);
    /// A build of `folder`'s index runs.
    bool building(const QString &folder) const;
    /// Where the index came from: "git", "folder" (a walk) or "" none yet.
    QString source(const QString &folder) const;
    /// Build `folder`'s index again (the old one answers meanwhile).
    void refresh(const QString &folder);
    /// The folders kept at most.
    static constexpr int MaxFolders = 8;
    /// The folders of one index watched at most (inotify's watches are the system's) and the wait
    /// after a change before the index is built again.
    static constexpr int MaxWatched = 2000;
    static constexpr int WatchDelayMs = 400;
    /// How many folders of `folder`'s index are watched (tests).
    int watchedCount(const QString &folder) const;
    /// git's output read at most (64 MiB).
    static constexpr qsizetype MaxGitOutput = 64 * 1024 * 1024;
    /// Tests: git as this program ("" none: every folder is walked).
    void setGitProgram(const QString &program);

Q_SIGNALS:
    /// `folder`'s index or its building state changed.
    void updated(const QString &folder);

private:
    struct Slot {
        std::shared_ptr<const FileIndex> index;
        QString source;
        bool building = false;
        bool again = false;
        quint64 used = 0;
        int generation = 0;
        /// Its folders watched (owned by the cache; null until the first build).
        QFileSystemWatcher *watcher = nullptr;
    };
    /// `folder`'s watcher set to the index's folders.
    void watch(const QString &folder, Slot &slot);
    void changedOnDisk(const QString &folder);
    void forget(Slot &slot);
    void build(const QString &folder);
    void finish(const QString &folder, int generation, std::shared_ptr<const FileIndex> index, const QString &source);
    void trim();

    QHash<QString, Slot> m_slots;
    quint64 m_clock = 0;
    gitcli::Client *m_git = nullptr;
    /// Folders changed on disk, built again when m_watchTimer fires.
    QStringList m_changed;
    QTimer m_watchTimer;
};

class CodeFileIndex : public QObject {
    Q_OBJECT
    /// The folder (absolute; "" none: no rows).
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    /// What is typed (fuzzy, fuzzy.h); "" the shallowest entries.
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    /// No rows when false (a mention not being written): nothing is computed.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    /// "all" (default), "files", "folders".
    Q_PROPERTY(QString kinds READ kinds WRITE setKinds NOTIFY kindsChanged FINAL)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged FINAL)
    /// Refreshed every 15 s while true (the picker shown).
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged FINAL)
    /// The colour of the matched letters in the rows' markup.
    Q_PROPERTY(QColor markColor READ markColor WRITE setMarkColor NOTIFY markColorChanged FINAL)
    /// Paths listed first for an empty query (recently opened files), when the index has them.
    Q_PROPERTY(QStringList recent READ recent WRITE setRecent NOTIFY recentChanged FINAL)
    /// [{path, name, folder (the part before the name, "" at the top), isFolder, title (a folder's
    ///   path with a "/"), nameMarkup, folderMarkup (StyledText, escaped, matched letters bold in
    ///   markColor), iconName ("folder" / "file-code" / "file-text" / "image")}]
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool building READ building NOTIFY stateChanged FINAL)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged FINAL)
    /// "git", "folder", "".
    Q_PROPERTY(QString source READ source NOTIFY stateChanged FINAL)

public:
    explicit CodeFileIndex(QObject *parent = nullptr);
    ~CodeFileIndex() override;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool active() const { return m_active; }
    void setActive(bool active);
    QString kinds() const { return m_kinds; }
    void setKinds(const QString &kinds);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    bool live() const { return m_live; }
    void setLive(bool live);
    QColor markColor() const { return m_markColor; }
    void setMarkColor(const QColor &color);
    QStringList recent() const { return m_recent; }
    void setRecent(const QStringList &recent);
    QVariantList rows() const { return m_rows; }
    bool ready() const { return m_index != nullptr; }
    bool building() const;
    int fileCount() const { return m_index ? m_index->fileCount() : 0; }
    bool truncated() const { return m_index && m_index->truncated(); }
    QString source() const;

    /// Build the folder's index again.
    Q_INVOKABLE void refresh();
    /// The index has this relative path (a file or a folder).
    Q_INVOKABLE bool contains(const QString &path) const;
    Q_INVOKABLE bool isFolder(const QString &path) const;
    /// The path of row `row` of `rows` ("" out of range).
    Q_INVOKABLE QString pathAt(int row) const;

    /// Queries bigger indexes than this go to a pool thread.
    static constexpr int SyncQueryLimit = 20000;
    /// Tests: always on the GUI thread.
    static void setAlwaysSync(bool on);

Q_SIGNALS:
    void folderChanged();
    void queryChanged();
    void activeChanged();
    void kindsChanged();
    void limitChanged();
    void liveChanged();
    void markColorChanged();
    void recentChanged();
    void rowsChanged();
    void stateChanged();

private:
    void attach();
    void update();
    void setRows(const QVariantList &rows);

    QString m_folder;
    QString m_query;
    bool m_active = true;
    QString m_kinds = QStringLiteral("all");
    int m_limit = 50;
    bool m_live = false;
    QColor m_markColor = QColor(0x25, 0x63, 0xeb);
    QStringList m_recent;
    QVariantList m_rows;
    std::shared_ptr<const FileIndex> m_index;
    int m_generation = 0;
    QTimer m_liveTimer;
};

/// The rows of a CodeFileIndex for `hits` of `index` (pure; `recent` first for an empty query).
QVariantList fileRows(const FileIndex &index, const QList<FileIndex::Hit> &hits, const QColor &mark);
/// A file's icon by its name: "image", "file-code", "file-text"; a folder "folder".
QString fileIcon(QStringView path, bool folder);

} // namespace brulion::codefiles
