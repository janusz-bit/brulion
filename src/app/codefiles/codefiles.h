// CodeFiles (QML singleton `CodeFiles`) — the codefiles area's helpers for QML (docs/CODE-FILES.md):
// links of agents' answers to a thread's files, @-mentions of a thread's composer, review comments,
// recently opened files, the clipboard, the index's refresh.
//
//   const target = CodeFiles.linkTarget(folder, href)   // {path, line, endLine} of a file inside folder, {} else
//   CodeFiles.mentionStart(text, caret) >= 0            // an @-mention is being written
//   input.text = CodeFiles.withMention(text, caret, "src/main.cpp")
//   chat.addToComposer(CodeFiles.reviewComment(path, from, to, lines, comment))
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace brulion::codefiles {

class CodeFiles : public QObject {
    Q_OBJECT
public:
    explicit CodeFiles(QObject *parent = nullptr);

    /// A link a thread's message or tool call has (the chat's href: "page:<target>", a bare path,
    /// a file: URL) as a file or folder inside `folder` that exists: {path (relative), line, column,
    /// endLine, isFolder}; {} for anything else (pages of the notebook, web addresses, files outside
    /// it, ones that are not there).
    Q_INVOKABLE QVariantMap linkTarget(const QString &folder, const QString &href) const;
    /// linkTarget()'s path ("" none) and line (0 none) alone.
    Q_INVOKABLE QString linkPath(const QString &folder, const QString &href) const;
    Q_INVOKABLE int linkLine(const QString &folder, const QString &href) const;
    /// `path` (relative to `folder`, or absolute inside it) as an absolute path; "" when it leaves it.
    Q_INVOKABLE QString absolutePath(const QString &folder, const QString &path) const;
    /// The relative path of `path` inside `folder` ("" outside).
    Q_INVOKABLE QString relativePath(const QString &folder, const QString &path) const;
    /// relativePath() of each of `paths`, those outside left out, each once.
    Q_INVOKABLE QStringList relativePaths(const QString &folder, const QStringList &paths) const;

    /// Where an @-mention being written at `caret` starts (-1: none) and what it is so far.
    Q_INVOKABLE int mentionStart(const QString &text, int caret) const;
    Q_INVOKABLE QString mentionQuery(const QString &text, int caret) const;
    /// `text` with that mention replaced by "@<path> " ("" none), and where the caret goes then.
    Q_INVOKABLE QString withMention(const QString &text, int caret, const QString &path) const;
    Q_INVOKABLE int mentionEnd(const QString &text, int caret, const QString &path) const;

    /// The composer's text of a review comment on lines `from` … `to` of `path` (viewer.h).
    Q_INVOKABLE QString reviewComment(const QString &path, int from, int to, const QString &lines,
                                      const QString &comment) const;
    /// The language of a file ("cpp", "python"… "" none).
    Q_INVOKABLE QString languageOf(const QString &path) const;
    /// A file's icon: "folder", "image", "code", "file-text".
    Q_INVOKABLE QString iconOf(const QString &path, bool folder) const;

    /// `recent` with `path` first, each once, at most `max`.
    Q_INVOKABLE QStringList withRecent(const QStringList &recent, const QString &path, int max) const;
    /// The settings key of a folder's recent files ("code/recentFiles/<hash of the folder>").
    Q_INVOKABLE QString recentKey(const QString &folder) const;

    /// Build the folder's file index again (a turn ended: its files changed).
    Q_INVOKABLE void refresh(const QString &folder) const;
    /// Plain text on the clipboard.
    Q_INVOKABLE bool copyText(const QString &text) const;
    /// "1.2 MB", "340 KB", "12 B".
    Q_INVOKABLE QString sizeText(qint64 bytes) const;
};

} // namespace brulion::codefiles
