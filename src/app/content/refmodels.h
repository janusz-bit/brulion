// Block references grouped by page — the backlinks of a page (PageView's "Odnośniki", the side
// panel) and the blocks carrying a tag (TagsView's tag detail).
//
//   BacklinksModel { workspace: Workspace; path: page.path; palette: rendererPalette }
//   BacklinksModel { workspace: Workspace; path: page.path; mode: "unlinked" }
//   TagBlocksModel { workspace: Workspace; tag: "praca"; palette: rendererPalette }
//
// Rows are page headers and the blocks under them (rowType "page" | "ref"), journals first
// (newest first), then other pages:
//   page rows  path, title, folder, journalDate ("" for pages), count (refs in the group),
//              collapsed, pageLevel (TagBlocksModel: the page itself carries the tag)
//   ref rows   path, line (first line of the block, for opening it there), html (the block's own
//              text rendered by the core InlineRenderer — escaped, links page:/tag:/block:/ext:,
//              the task checkbox as task:toggle), text (plain, one line), blockId, breadcrumb
//              ("Projekt › Zadania": the parent blocks), taskStatus, depth (0)
// Groups listed in `collapsedPaths` keep their header (with the count) and hide their refs.
// Unlinked references (mode "unlinked") are plain-text mentions: html is the escaped excerpt
// with the mention highlighted; linkMention(row) turns it into a [[link]] in that file (through
// the Workspace, so open editors reload) and linkAll() does it for every listed mention.
#pragma once

#include "workspacemodel.h"

#include "markdown/inlinerenderer.h"

#include <QDate>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace brulion::markdown {
class BlockDocument;
}

namespace brulion::content {

class GroupedRefsModel : public WorkspaceListModel {
    Q_OBJECT
    /// Renderer colours (see BlockEditor.rendererPalette).
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    /// "pl" (default) | "en": dates in chips.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// "YYYY-MM-DD" for "dziś"/"jutro" and overdue chips; "" = the workspace clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// Paths of the groups shown collapsed (header only).
    Q_PROPERTY(QStringList collapsedPaths READ collapsedPaths WRITE setCollapsedPaths NOTIFY collapsedPathsChanged FINAL)
    /// Most ref rows listed (0 = all): long lists show the first ones and a "show more" button.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged FINAL)
    /// References left out by `limit` (not counting collapsed groups).
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY totalsChanged FINAL)
    /// References (ref rows, also of collapsed groups) and pages (groups).
    Q_PROPERTY(int refCount READ refCount NOTIFY totalsChanged FINAL)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY totalsChanged FINAL)

public:
    enum Role {
        RowTypeRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        FolderRole,
        JournalDateRole,
        CountRole,
        CollapsedRole,
        PageLevelRole,
        LineRole,
        HtmlRole,
        TextRole,
        BlockIdRole,
        BreadcrumbRole,
        TaskStatusRole,
        DepthRole,
    };
    Q_ENUM(Role)

    explicit GroupedRefsModel(QObject *parent = nullptr);

    QVariantMap palette() const { return m_paletteMap; }
    void setPalette(const QVariantMap &palette);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString today() const { return m_today; }
    void setToday(const QString &today);
    QStringList collapsedPaths() const { return m_collapsed; }
    void setCollapsedPaths(const QStringList &paths);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int hiddenCount() const { return m_hidden; }
    int refCount() const { return m_refCount; }
    int pageCount() const { return m_pageCount; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Collapse / expand the group of `path`.
    Q_INVOKABLE void toggleCollapsed(const QString &path);
    /// All roles of a row as a map (empty for a bad row).
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Check / uncheck the task of a ref row (task:toggle in its text): done <-> todo.
    Q_INVOKABLE bool toggleTask(int row);

Q_SIGNALS:
    void paletteChanged();
    void localeChanged();
    void todayChanged();
    void collapsedPathsChanged();
    void limitChanged();
    void totalsChanged();

protected:
    struct Ref {
        int line = 0;
        QString html;
        QString text;
        QString blockId;
        QString breadcrumb;
        QString taskStatus;

        friend bool operator==(const Ref &, const Ref &) = default;
    };
    struct Group {
        QString path;
        QString title;
        QString folder;
        QString journalDate;
        bool pageLevel = false;
        QList<Ref> refs;
    };
    /// The groups to show, in order (subclasses).
    virtual QList<Group> collect() = 0;

    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

    QDate effectiveToday() const;
    dates::UiLocale uiLocaleValue() const;
    const markdown::Palette &rendererPalette() const { return m_palette; }
    /// A group for `path` with title, folder and journal date filled in.
    Group groupFor(const QString &path) const;
    /// A ref row for block `blockIndex` of the indexed note `path` (its own text rendered the
    /// way the editor shows it).
    Ref refForBlock(const QString &path, int blockIndex) const;
    /// A ref row for line `line` of `path` that is not inside a block (e.g. frontmatter).
    Ref refForLine(const QString &path, int line) const;

private:
    struct Row {
        bool header = false;
        QString path;
        QString title;
        QString folder;
        QString journalDate;
        int count = 0;
        bool collapsed = false;
        bool pageLevel = false;
        Ref ref;
        QString key;

        friend bool operator==(const Row &, const Row &) = default;
    };
    QList<Row> m_rows;
    /// Parsed documents of the notes rendered during one refresh.
    mutable QHash<QString, std::shared_ptr<markdown::BlockDocument>> m_docs;
    QVariantMap m_paletteMap;
    markdown::Palette m_palette;
    QString m_locale = QStringLiteral("pl");
    QString m_today;
    QStringList m_collapsed;
    int m_refCount = 0;
    int m_pageCount = 0;
    int m_limit = 0;
    int m_hidden = 0;
};

class BacklinksModel : public GroupedRefsModel {
    Q_OBJECT
    /// The page whose references are listed.
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// "linked" (default: links, embeds, block references) | "unlinked" (plain-text mentions).
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged FINAL)

public:
    explicit BacklinksModel(QObject *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);

    /// Unlinked mode: turn the mention of ref row `row` into a [[link]] to `path` in its file.
    /// False when the mention is no longer there (the file changed) or the write failed.
    Q_INVOKABLE bool linkMention(int row);
    /// Unlinked mode: link every listed mention; returns how many were linked.
    Q_INVOKABLE int linkAll();
    /// The page's names searched for mentions: its title and aliases.
    Q_INVOKABLE QStringList names() const;

Q_SIGNALS:
    void pathChanged();
    void modeChanged();

protected:
    QList<Group> collect() override;

private:
    QList<Group> collectLinked();
    QList<Group> collectUnlinked();
    /// Link the first mention on `line` of `content` (from `source`). Returns the new content
    /// ("" when nothing was found).
    QString linkedContent(const QString &source, const QString &content, int line) const;

    QString m_path;
    QString m_mode = QStringLiteral("linked");
};

class TagBlocksModel : public GroupedRefsModel {
    Q_OBJECT
    /// The tag (without '#'; any case); sub-tags ("praca/projekt" under "praca") are included.
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY tagChanged FINAL)
    /// Tag display spelling as first written ("Praca").
    Q_PROPERTY(QString display READ display NOTIFY totalsChanged FINAL)
    /// [{ tag, display, count }] of the direct and deeper sub-tags, most used first.
    Q_PROPERTY(QVariantList subTags READ subTags NOTIFY totalsChanged FINAL)
    /// The parent tag ("praca" for "praca/projekt"; "" at the top).
    Q_PROPERTY(QString parentTag READ parentTag NOTIFY tagChanged FINAL)

public:
    explicit TagBlocksModel(QObject *parent = nullptr);

    QString tag() const { return m_tag; }
    void setTag(const QString &tag);
    QString display() const { return m_display; }
    QVariantList subTags() const { return m_subTags; }
    QString parentTag() const;

Q_SIGNALS:
    void tagChanged();

protected:
    QList<Group> collect() override;

private:
    QString m_tag;
    QString m_display;
    QVariantList m_subTags;
};

} // namespace brulion::content
