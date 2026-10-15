// Shared machinery of the importers: path claims (unique, case-insensitive), the ImportContext
// collecting output files, the SourceTree mapping source files to vault paths with link
// rewriting, text decoding and small formatting helpers. Internal to the io module.
#pragma once

#include "io/importer.h"
#include "io/links_p.h"
#include "types.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace brulion::io::detail {

/// Case- and normalisation-insensitive key of a vault path.
QString pathKey(const QString &path);

/// Paths taken by this import or by the vault (ImportOptions::exists).
class PathClaims {
public:
    explicit PathClaims(std::function<bool(const VaultPath &)> exists = {});
    /// `path` or a free variant ("Page 2.md", "photo 2.png"), now taken.
    VaultPath claim(const VaultPath &path);
    /// Mark `path` as taken as it is (a daily note appended to an existing file).
    void reserve(const VaultPath &path);
    bool isTaken(const VaultPath &path) const;

private:
    QSet<QString> m_keys;
    QHash<QString, int> m_nextSuffix; ///< path key -> first suffix number not tried yet
    std::function<bool(const VaultPath &)> m_exists;
};

/// One path segment for a page or folder name: paths::sanitizeFileName() without trailing dots
/// / spaces (Windows), at most 200 bytes of UTF-8 (file systems allow 255 per name; room is left
/// for the extension and a " 2" suffix) — "Untitled" when nothing is left.
QString pageSegment(const QString &name);
/// `title` as frontmatter `aliases` when pageSegment() had to change it (characters that file
/// names cannot hold, length), so links and search still find the note by its real title.
void addTitleAlias(QVariantMap &frontmatter, const QString &title);
/// A file name for an attachment: the stem sanitised, the extension kept (lower-cased).
QString attachmentFileName(const QString &name);
/// Each segment of a relative path made safe (pages: pageSegment, the last one of an attachment
/// path: attachmentFileName).
QString safeRelativePath(const QString &relative, bool attachment);

/// Collects the output of one importer run.
class ImportContext {
public:
    ImportContext(const ImportOptions &options, const QString &importerName);

    const ImportOptions &options() const { return m_options; }
    const VaultConfig &config() const { return m_options.config; }

    /// Vault path for a page at `relative` (e.g. "Folder/Title") under the target folder:
    /// segments made safe, ".md" added. Not claimed.
    VaultPath pagePath(const QString &relative) const;
    /// pagePath(), made unique and claimed.
    VaultPath claimPage(const QString &relative);
    /// `<attachmentsFolder>/<subfolder>/<relative>` made safe, unique and claimed.
    VaultPath claimAttachment(const QString &relative);
    /// The folder receiving attachments.
    QString attachmentFolder() const { return m_attachmentFolder; }
    /// Daily-note path of `date` (VaultConfig journal settings).
    VaultPath journalPath(QDate date) const;
    /// Wiki-link target of the daily note of `date`: its title ("2026-10-14"), or its path without
    /// ".md" when the title alone names no single day (a format with folders, "yyyy/MM/dd").
    QString journalLinkTarget(QDate date) const;

    /// Add a page (UTF-8, "\n" line endings as given).
    void addPage(const VaultPath &path, const QString &content);
    /// Add (or extend) the daily note of `date`; returns its path.
    VaultPath addJournal(QDate date, const QString &content);
    /// Add an attachment under the attachment folder; identical data under the same requested
    /// name is stored once. Returns its vault path.
    VaultPath addAttachment(const QString &relative, const QByteArray &data);
    /// Add a file at a claimed path.
    void addFile(const VaultPath &path, const QByteArray &data);

    void warn(const QString &message);
    ImportResult take();

private:
    ImportOptions m_options;
    QString m_attachmentFolder;
    PathClaims m_claims;
    ImportResult m_result;
    QHash<QString, qsizetype> m_journalIndex;                              ///< path key -> index in files
    QHash<QString, QList<std::pair<QByteArray, VaultPath>>> m_attachments; ///< requested -> stored
};

/// Maps the files of a tree-shaped source (a vault, an export folder) to their vault paths and
/// rewrites path-based links of pages moved by the import.
class SourceTree {
public:
    enum class PageLinks {
        Keep, ///< link style kept (wiki stays wiki, Markdown stays Markdown)
        Wiki, ///< Markdown links to pages become wiki links `[[Page|label]]`
    };

    void add(const QString &source, const VaultPath &destination);
    bool contains(const QString &source) const;
    VaultPath destination(const QString &source) const;

    /// The source file a link `target` written in `fromSource` points to: relative to the
    /// linking file, from the source root, then (wiki links only) by unique base name — shortest
    /// path first —, with or without ".md".
    std::optional<QString> resolve(const QString &target, const QString &fromSource, bool wiki) const;

    /// Rewrite the links of a page that moved from `source` to destination(source). Bare wiki
    /// names keep working by base name and stay as written unless the file was renamed.
    QString rewrite(const QString &content, const QString &source, PageLinks style = PageLinks::Keep) const;

    /// Wiki-link target of a page destination: its title when no other page of the tree has
    /// the same title, else the vault path without ".md".
    QString wikiTarget(const VaultPath &destination) const;

private:
    QHash<QString, QSet<QString>> m_pagesByTitle; ///< title key -> destination keys
    QHash<QString, QString> m_destinations;       ///< source key -> vault path
    QHash<QString, QString> m_sources;            ///< source key -> source path as given
    QHash<QString, QStringList> m_byBaseName;     ///< base name key (with and without .md) -> sources
    /// resolve() of bare names, cleared by add(). A SourceTree belongs to one importer run (one
    /// thread), so the cache needs no locking.
    mutable QHash<QString, std::optional<QString>> m_bestByBaseName;
};

/// Text of a file: UTF-8 (BOM skipped), UTF-16 with BOM, else 8-bit Windows text — windows-1250
/// when Polish letters dominate (old Notepad files on Polish Windows), otherwise windows-1252.
QString decodeText(const QByteArray &data);

/// "yyyy-MM-dd HH:mm" wall-clock time of `dateTime` in `zone` (empty for invalid input).
QString wallClock(const QDateTime &dateTime, const QTimeZone &zone);

/// A note: frontmatter (keys of `order` first; nothing when `data` is empty) + body; ends with
/// a line break.
QString noteWithFrontmatter(const QVariantMap &data, const QStringList &order, const QString &body);

/// Normalise line endings to "\n" and trim trailing blank lines.
QString normalizeNewlines(const QString &text);

/// Markdown linking to an imported attachment: an embed `![[path]]` for images, audio, video
/// and PDF, else a link `[[path|label]]` (label = file name when empty).
QString attachmentLink(const VaultPath &path, const QString &label = {});

/// MIME type -> file extension with dot (".png"), empty when unknown.
QString extensionForMime(const QString &mime);

/// A tag name from free text (labels, categories): whitespace -> '-', characters not allowed
/// in tags removed; empty when nothing usable is left (or only digits).
QString tagName(const QString &text);

/// A page title from free text: first non-empty line, Markdown heading marks removed,
/// shortened to `maxLength` characters (at a word boundary).
QString titleFromText(const QString &text, int maxLength = 80);

/// Does `files` contain a path for which `pred` holds?
bool anyPath(const QList<InputFile> &files, const std::function<bool(const QString &)> &pred);

/// Remove a folder shared by all paths ("Export/a.md", "Export/b/c.md" -> "a.md", "b/c.md")
/// unless `keep` says the folder is meaningful (e.g. a ".textbundle").
QList<InputFile> stripCommonRoot(const QList<InputFile> &files,
                                 const std::function<bool(const QString &folder)> &keep = {});

/// Apply `fn(line, masked)` to every line outside fenced code (`masked` = the line with inline
/// code and comments blanked, same length) and join the results with the original line endings.
QString mapLinesOutsideCode(const QString &content,
                            const std::function<QString(const QString &line, const QString &masked)> &fn);

/// Import of a tree of Markdown pages and attachments (a vault, an export folder).
struct TreeImport {
    /// Is a (kept) source file a page? Default: .md / .markdown.
    std::function<bool(const QString &path)> isPage;
    /// Source files to leave out (ignored folders such as .obsidian/ are always left out).
    std::function<bool(const QString &path)> skip;
    /// Page path (relative to the target folder, without extension) for a page source.
    /// Default: the source path without its extension.
    std::function<QString(const QString &path)> pageRelative;
    /// Attachment path (relative to the attachment folder) for an attachment source. Default:
    /// the source path.
    std::function<QString(const QString &path)> attachmentRelative;
    /// Content conversion before links are rewritten (default: none).
    std::function<QString(const QString &content, const QString &source)> convert;
    SourceTree::PageLinks links = SourceTree::PageLinks::Keep;
};

/// Add the pages (links rewritten for their new places) and attachments of `files` to `ctx`.
/// Returns the mapping used (source -> vault path).
SourceTree importTree(const QList<InputFile> &files, ImportContext &ctx, const TreeImport &options);

} // namespace brulion::io::detail
