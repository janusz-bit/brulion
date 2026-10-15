// SearchResultModel — full-text search for SearchView (VaultIndex::search: folded words, prefix
// for the last word, fuzzy, title x3, aliases x2), best first, with the positions of the matched
// words for highlighting (TextHelper.markMatches) and simple filters.
//
//   SearchResultModel { workspace: Workspace; query: field.text; scope: "pages" }
//
// scope: "all" (default) | "pages" (no daily notes) | "journals" (only daily notes);
// folder: only results inside this folder (and below). Results follow vault changes (re-run
// 300 ms after the index changed while a query is set).
//
// Roles: path, title, folder, line (0-based first matching body line; -1 when only the title or
// an alias matched), excerpt (plain text around the match), excerptMatches / titleMatches
// (UTF-16 positions of matched characters), journalDate ("YYYY-MM-DD" for daily notes, else ""),
// score.
#pragma once

#include "workspacemodel.h"

#include <QStringList>
#include <QVariantList>

namespace brulion::content {

class SearchResultModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged FINAL)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    /// Most results listed (default 100).
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged FINAL)
    /// Results before the scope / folder filters (for "3 of 12" and empty-filter hints).
    Q_PROPERTY(int totalCount READ totalCount NOTIFY resultsChanged FINAL)
    /// Folded words of the query that are highlighted.
    Q_PROPERTY(QStringList terms READ terms NOTIFY resultsChanged FINAL)
    /// [{ folder, count }] of the results' top-level folders ("" = the vault root), most first.
    Q_PROPERTY(QVariantList folders READ folders NOTIFY resultsChanged FINAL)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        FolderRole,
        LineRole,
        ExcerptRole,
        ExcerptMatchesRole,
        TitleMatchesRole,
        JournalDateRole,
        ScoreRole,
    };
    Q_ENUM(Role)

    explicit SearchResultModel(QObject *parent = nullptr);

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int totalCount() const { return m_total; }
    QStringList terms() const { return m_terms; }
    QVariantList folders() const { return m_folders; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void queryChanged();
    void scopeChanged();
    void folderChanged();
    void limitChanged();
    void resultsChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Hit {
        QString path;
        QString title;
        QString folder;
        int line = -1;
        QString excerpt;
        QVariantList excerptMatches;
        QVariantList titleMatches;
        QString journalDate;
        double score = 0;

        friend bool operator==(const Hit &, const Hit &) = default;
    };
    QList<Hit> m_rows;
    QString m_query;
    QString m_scope = QStringLiteral("all");
    QString m_folder;
    int m_limit = 100;
    int m_total = 0;
    QStringList m_terms;
    QVariantList m_folders;
};

} // namespace brulion::content
