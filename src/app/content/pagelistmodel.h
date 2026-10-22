// PageListModel — every page of the vault for AllPagesView, sortable and filterable, built from
// the index and the vault's file list in one pass (a vault with thousands of notes stays fast:
// backlink counts come from one walk over all resolved links).
//
//   PageListModel { workspace: Workspace; sortKey: "modified"; filter: field.text }
//
// sortKey: "title" (default, Polish collation) | "folder" | "modified" | "backlinks" | "tags";
// descending reverses it (ties are always by title). filter: words that must all occur in the
// title, an alias or the folder (case- and diacritic-insensitive). folder: only pages in this
// folder and below. includeJournals: daily notes too (default false).
//
// Roles: path, title, folder, aliases (list), modified (ms since epoch), modifiedLabel ("dziś
// 09:12"), size, backlinks (pages linking here or carrying a tag that names the page), tags
// (display spellings), tagCount,
// journalDate ("" for pages), titleMatches (positions of the filter words in the title).
#pragma once

#include "workspacemodel.h"

#include <QDate>
#include <QStringList>
#include <QVariantList>

namespace brulion::content {

class PageListModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY sortChanged FINAL)
    Q_PROPERTY(bool includeJournals READ includeJournals WRITE setIncludeJournals NOTIFY includeJournalsChanged FINAL)
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// "YYYY-MM-DD" for "dziś"/"wczoraj"; "" = the workspace clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// Pages in scope (folder, journals) before the text filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged FINAL)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        FolderRole,
        AliasesRole,
        ModifiedRole,
        ModifiedLabelRole,
        SizeRole,
        BacklinksRole,
        TagsRole,
        TagCountRole,
        JournalDateRole,
        TitleMatchesRole,
    };
    Q_ENUM(Role)

    explicit PageListModel(QObject *parent = nullptr);

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString sortKey() const { return m_sortKey; }
    void setSortKey(const QString &key);
    bool descending() const { return m_descending; }
    void setDescending(bool on);
    bool includeJournals() const { return m_includeJournals; }
    void setIncludeJournals(bool on);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString today() const { return m_today; }
    void setToday(const QString &today);
    int totalCount() const { return m_total; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Row of `path` (-1 when not listed).
    Q_INVOKABLE int rowOf(const QString &path) const;

Q_SIGNALS:
    void filterChanged();
    void folderChanged();
    void sortChanged();
    void includeJournalsChanged();
    void localeChanged();
    void todayChanged();
    void totalCountChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Page {
        QString path;
        QString title;
        QString folder;
        QStringList aliases;
        qint64 modified = 0;
        qint64 size = 0;
        int backlinks = 0;
        QStringList tags;
        QString journalDate;
        QVariantList titleMatches;

        friend bool operator==(const Page &, const Page &) = default;
    };
    QDate effectiveToday() const;

    QList<Page> m_rows;
    QString m_filter;
    QString m_folder;
    QString m_sortKey = QStringLiteral("title");
    bool m_descending = false;
    bool m_includeJournals = false;
    QString m_locale = QStringLiteral("pl");
    QString m_today;
    int m_total = 0;
    /// A sort change reorders everything: reset instead of diffing.
    bool m_resetNext = false;
};

} // namespace brulion::content
