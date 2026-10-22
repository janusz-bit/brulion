// PageTools — page-level helpers for PageView's header and menu: what a rename will touch, the
// properties summary (aliases, page tags, other page properties, folder breadcrumb), the page's
// wiki link for "Kopiuj odnośnik", title validation, and exporting one page (Markdown or a
// self-contained HTML document) to the clipboard or a file.
//
//   PageTools { id: tools; workspace: Workspace }
//   tools.renameImpact(page.path)   // { notes: 3, links: 5, tags: 2 }
#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace brulion {
class VaultIndex;
}

namespace brulion::shell {
class Workspace;
}

namespace brulion::content {

class PageTools : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *workspace READ workspaceObject WRITE setWorkspaceObject NOTIFY workspaceChanged FINAL)

public:
    explicit PageTools(QObject *parent = nullptr);

    QObject *workspaceObject() const;
    void setWorkspaceObject(QObject *workspace);
    void setWorkspace(shell::Workspace *workspace);

    /// What renaming `path` rewrites: { notes (other notes linking to it by name or path, or
    /// carrying a tag that names it), links (those links), tags (those tags: `#old`, `#[[old]]`,
    /// `tags::` and frontmatter items — docs/SYNTAX.md §3) }.
    Q_INVOKABLE QVariantMap renameImpact(const QString &path) const;
    /// The tag that names the page ("#praca", "#[[Jan Kowalski]]", "#Projekty/Dom" for a duplicate
    /// base name; markdown::tagSyntax()); "" when no tag can name it.
    Q_INVOKABLE QString tagFor(const QString &path) const;
    /// "" when `title` is a good new title for `path`, else a translated reason (empty, invalid
    /// characters, a page with that title exists in the folder).
    Q_INVOKABLE QString validateTitle(const QString &path, const QString &title) const;
    /// { exists, title, folder, folderParts: [{ title, path }], journalDate, aliases: [..],
    ///   tags: [{ tag, display }] (the page's own tags), properties: [{ key, value }] (other page
    ///   properties), words, backlinks (pages linking here or tagging it),
    ///   tag: { key, display, text ("#praca" / "#[[Jan Kowalski]]"), references (blocks and pages
    ///   tagged with it), subTags: [{ tag, display, count }] (praca/… under praca) } — the tag that
    ///   names this page (docs/SYNTAX.md §3), also for a page not written yet }.
    Q_INVOKABLE QVariantMap summary(const QString &path) const;
    /// "[[Dom]]", or "[[Projekty/Dom]]" when the title alone would not reach this page.
    Q_INVOKABLE QString wikiLink(const QString &path) const;
    /// Follow a link of rendered note text (InlineRenderer URLs: page:, tag:, block:, ext:) found
    /// in the note `fromPath` — like the editor does: links resolve from that note, a link to a
    /// missing page opens it ready to be written. Returns false for links it does not handle
    /// (task:toggle, reminder:…), which the caller acts on.
    Q_INVOKABLE bool openLink(const QString &url, const QString &fromPath, bool newPane = false) const;
    /// The daily note of "YYYY-MM-DD": the existing one (any file name the journal folder accepts,
    /// e.g. Logseq's "2026_10_14.md"; the configured name preferred), else the path it will get.
    /// Never creates a file. "" for an invalid date.
    Q_INVOKABLE QString journalPathFor(const QString &date) const;
    /// Put plain text on the clipboard.
    Q_INVOKABLE bool copyText(const QString &text) const;

    /// [{ id: "markdown" | "html", name, extension }].
    Q_INVOKABLE QVariantList exportFormats() const;
    /// Suggested file name ("Dom.md", "Dom.html").
    Q_INVOKABLE QString exportFileName(const QString &path, const QString &format) const;
    /// The exported page as text ("" when the page cannot be read).
    Q_INVOKABLE QString exportText(const QString &path, const QString &format) const;
    /// Copy the export to the clipboard (HTML also as rich text, with the Markdown as plain text).
    Q_INVOKABLE bool copyExport(const QString &path, const QString &format) const;
    /// Write the export to a file chosen in a file dialog (file:// or, on Android, content://).
    Q_INVOKABLE bool saveExport(const QString &path, const QString &format, const QUrl &target) const;

Q_SIGNALS:
    void workspaceChanged();

private:
    /// summary()'s `tag` of `path`.
    static QVariantMap tagInfo(const VaultIndex *index, const QString &path, int references);

    QByteArray exportData(const QString &path, const QString &format) const;

    QPointer<shell::Workspace> m_workspace;
};

} // namespace brulion::content
