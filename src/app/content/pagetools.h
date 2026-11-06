// PageTools — page-level helpers for PageView's header and menu: what a rename will touch, the
// properties summary (aliases, page tags, other page properties, folder breadcrumb), the page's
// wiki link for "Copy link", title validation, and exporting one page (Markdown or a
// self-contained HTML document) to the clipboard or a file.
//
//   PageTools { id: tools; workspace: Workspace }
//   tools.renameImpact(page.path)   // { notes: 3, links: 5, tags: 2, queries: 1 }
//
// With a `path` it also keeps that page's summary() for the page header as typed properties
// (folderParts, aliasChips, tagChips, headerTag, subTags), again whenever the notebook changes
// (Workspace revision, indexing) — read compiled in QML, unlike members of a returned map:
//
//   PageTools { id: headerTools; workspace: Workspace; path: page.path }
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
    /// The page whose summary the properties below hold ("" none: they are empty).
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// summary(path).folderParts: [{ title, path }] of the page's folders, outermost first.
    Q_PROPERTY(QVariantList folderParts READ folderParts NOTIFY summaryChanged FINAL)
    /// The page's aliases from the index as chips: [{ name, key: "", editable: false }].
    Q_PROPERTY(QVariantList aliasChips READ aliasChips NOTIFY summaryChanged FINAL)
    /// The page's own tags from the index as chips: [{ name (display), key (tag), editable: false }].
    Q_PROPERTY(QVariantList tagChips READ tagChips NOTIFY summaryChanged FINAL)
    /// summary(path).tag: { key, display, text, references, subTags } — {} when there is none.
    Q_PROPERTY(QVariantMap headerTag READ headerTag NOTIFY summaryChanged FINAL)
    /// That tag's subTags: [{ tag, display, count }].
    Q_PROPERTY(QVariantList subTags READ subTags NOTIFY summaryChanged FINAL)

public:
    explicit PageTools(QObject *parent = nullptr);

    QObject *workspaceObject() const;
    void setWorkspaceObject(QObject *workspace);
    void setWorkspace(shell::Workspace *workspace);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QVariantList folderParts() const;
    QVariantList aliasChips() const;
    QVariantList tagChips() const;
    QVariantMap headerTag() const;
    QVariantList subTags() const;

    /// What renaming `path` rewrites: { notes (other notes linking to it by name or path, or
    /// carrying a tag that names it, or a ```query naming it), links (those links, and the query
    /// filters `ref:` / `[[…]]` / `page:` / `path:` naming the page), tags (those tags: `#old`,
    /// `#[[old]]`, `tags::` and frontmatter items — docs/SYNTAX.md §3 —, and `tag:` filters),
    /// queries (the query filters among them) }.
    Q_INVOKABLE QVariantMap renameImpact(const QString &path) const;
    /// The tag that names the page ("#praca", "#[[Jan Kowalski]]", "#Projekty/Dom" for a duplicate
    /// base name; markdown::tagSyntax()); "" when no tag can name it (an attachment).
    Q_INVOKABLE QString tagFor(const QString &path) const;
    /// "" when `title` is a good new title for `path`, else a translated reason (empty, invalid
    /// characters, a page with that title exists in the folder).
    Q_INVOKABLE QString validateTitle(const QString &path, const QString &title) const;
    /// { exists, title, folder, folderParts: [{ title, path }], journalDate, aliases: [..],
    ///   tags: [{ tag, display }] (the page's own tags), properties: [{ key, value }] (other page
    ///   properties), words, backlinks (pages linking here or tagging it),
    ///   tag: { key, display, text ("#praca" / "#[[Jan Kowalski]]"), references (blocks and pages
    ///   tagged with it), subTags: [{ tag, display, count }] (praca/… under praca) } — the tag that
    ///   names this page (docs/SYNTAX.md §3), also for a page not written yet (the one in use that
    ///   names it most exactly: #praca/raporty for praca/raporty.md); key "" for an attachment }.
    Q_INVOKABLE QVariantMap summary(const QString &path) const;
    /// The page menu's and the folder tree's "Copy link": "[[Dom]]", "[[Projekty/Dom]]" when the
    /// title alone names another page (the shortest path that tells them apart), a daily note by
    /// its file name — notebook::pageLinkTarget(), the page part of every copied link; an
    /// attachment by the shortest name that reaches it ("[[plan.png]]").
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

    /// The side panel's calendar (ContentMiniCalendar) — BDatePicker marks for the days `from` …
    /// `to`: { "YYYY-MM-DD": [kinds] }, "journal" for a day with a journal, then the kinds of its
    /// other calendar entries in their order, each once, at most three kinds a day: "overdue" (a
    /// due task before `today` that is not done), "alarm" / "reminder", else the entry's kind.
    Q_INVOKABLE QVariantMap calendarMarks(const QString &from, const QString &to, const QString &today) const;
    /// The calendar entries of the day `date` but its journal (Workspace::calendarItems() rows).
    Q_INVOKABLE QVariantList calendarEntries(const QString &date) const;
    /// The page header's tag field (BSuggestField rows): PageController::suggestPropertyValues()
    /// rows { value, count } as { title: "#value", value, subtitle: the count ("" for 0), icon: "" }.
    Q_INVOKABLE QVariantList tagSuggestions(const QVariantList &values) const;

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
    void pathChanged();
    void summaryChanged();

private:
    /// summary()'s `tag` of `path`.
    static QVariantMap tagInfo(const VaultIndex *index, const QString &path, int references);

    QByteArray exportData(const QString &path, const QString &format) const;

    /// summary(m_path) again (nothing for no path); summaryChanged() when it differs.
    void refreshSummary();

    QPointer<shell::Workspace> m_workspace;
    QString m_path;
    QVariantMap m_summary;
};

} // namespace brulion::content
