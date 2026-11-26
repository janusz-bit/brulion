// FileIndex — the files and folders of a project's folder (docs/CODE-FILES.md "The index"), for
// the file picker, a thread's @-mentions and Brulion's own search. Part of the codefiles module.
//
//   FileIndex index = FileIndex::fromNulSeparated(gitLsFilesOutput);   // git ls-files -co --exclude-standard -z
//   FileIndex index = FileIndex::walk(folder);                          // no git: the folder itself
//   for (const FileIndex::Hit &h : index.search(u"mainqml", 50, FileIndex::Kinds::Files)) …
//
// Paths are relative to the folder, "/"-separated, never empty, absolute or with "." / ".."
// parts (such input is skipped). Folders are derived from the files' paths (every parent of a
// file is one; a folder holds a file or a folder) and kept without a trailing "/". Entries are
// sorted by path (UTF-16 order) and held in one buffer — about 2 × the paths' characters plus 12
// bytes an entry. At most `maxEntries` (files and folders together, default 200 000): what does
// not fit is left out (`truncated`), files in the order given.
//
// walk(): the folder's tree without following symbolic links, at most 32 levels deep, skipping
// hidden folders (".git", ".cache" …), node_modules and build output (build, build-*,
// cmake-build-*, dist, out, target, __pycache__, venv, result …); hidden files are listed.
// It stops early when `cancel` becomes true.
//
// search(): the entries matching a fuzzy query (fuzzy.h), best first (ties: the shorter path, then
// path order), at most `limit`; an empty query gives the entries shallow first (fewer "/", then
// path order). Thread-safe for reading: an index is built once and then only read (the app shares
// one per folder between threads).
#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <atomic>

namespace brulion::codefiles {

class FileIndex {
public:
    static constexpr int DefaultMaxEntries = 200000;
    static constexpr int MaxWalkDepth = 32;

    enum class Kinds { All, Files, Folders };

    struct Hit {
        int entry = -1;
        int score = 0;
        /// The matched UTF-16 offsets in the entry's path.
        QList<int> positions;
    };

    FileIndex() = default;

    /// The index of these files (relative paths).
    static FileIndex fromPaths(const QStringList &files, int maxEntries = DefaultMaxEntries);
    /// The index of NUL-separated relative paths in UTF-8 (`git ls-files -z`); a last path without
    /// its NUL counts.
    static FileIndex fromNulSeparated(QByteArrayView output, int maxEntries = DefaultMaxEntries);
    /// The index of the folder's tree on disk.
    static FileIndex walk(const QString &root, int maxEntries = DefaultMaxEntries,
                          const std::atomic<bool> *cancel = nullptr);
    /// A folder name walk() skips.
    static bool skippedFolder(QStringView name);

    int size() const { return int(m_entries.size()); }
    bool isEmpty() const { return m_entries.isEmpty(); }
    int fileCount() const { return m_files; }
    int folderCount() const { return int(m_entries.size()) - m_files; }
    /// Some files or folders were left out (more than maxEntries).
    bool truncated() const { return m_truncated; }

    QStringView path(int entry) const;
    /// The part after the last "/".
    QStringView name(int entry) const;
    /// Where name() starts in path().
    int nameStart(int entry) const;
    bool isFolder(int entry) const;
    /// The entry of `path` (-1: none), a binary search.
    int find(QStringView path) const;
    bool contains(QStringView path) const { return find(path) >= 0; }
    /// Every file's path, in order.
    QStringList files() const;

    QList<Hit> search(QStringView query, int limit, Kinds kinds = Kinds::All) const;

private:
    struct Entry {
        int offset = 0;
        int length = 0;
        int nameStart = 0;
        bool folder = false;
    };
    static FileIndex build(QStringList files, bool truncated, int maxEntries);

    QString m_text;
    QList<Entry> m_entries;
    /// The entries shallow first (an empty query's order).
    QList<int> m_shallow;
    int m_files = 0;
    bool m_truncated = false;
};

/// A relative path as an index keeps it ("/"-separated, no "./"), "" for one it refuses (empty,
/// absolute, with ".." or "." parts, a NUL or a line break).
QString cleanRelativePath(QStringView path);

} // namespace brulion::codefiles
