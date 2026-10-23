// TagListModel — every tag of the vault for TagsView, with usage counts.
//
//   TagListModel { workspace: Workspace; filter: field.text; sortKey: "name" }
//
// sortKey: "count" (default: most used first) | "name" (Polish collation; nested tags right
// after their parent, so "praca/projekt" follows "praca"). filter: words that must all occur in
// the tag (case- and diacritic-insensitive).
//
// Roles: tag (canonical key, lower case), display (as first written: "Praca"), count (tagged
// blocks, plus one per page tagged only at page level), pageCount, depth (nesting: 0 for "praca",
// 1 for "praca/projekt"), leaf ("projekt"), parent ("praca"), matches (positions of the filter
// words in `display`), page (the note the tag names — docs/SYNTAX.md §3 — "" when it names none
// yet), pageTitle (its title).
#pragma once

#include "workspacemodel.h"

#include <QVariantList>

namespace brulion::content {

class TagListModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged FINAL)
    /// Tags before the filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged FINAL)

public:
    enum Role {
        TagRole = Qt::UserRole + 1,
        DisplayRole,
        CountRole,
        PageCountRole,
        DepthRole,
        LeafRole,
        ParentRole,
        MatchesRole,
        PageRole,
        PageTitleRole,
    };
    Q_ENUM(Role)

    explicit TagListModel(QObject *parent = nullptr);

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString sortKey() const { return m_sortKey; }
    void setSortKey(const QString &key);
    int totalCount() const { return m_total; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void filterChanged();
    void sortKeyChanged();
    void totalCountChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Tag {
        QString tag;
        QString display;
        int count = 0;
        int pageCount = 0;
        QVariantList matches;
        QString page;

        friend bool operator==(const Tag &, const Tag &) = default;
    };
    QList<Tag> m_rows;
    QString m_filter;
    QString m_sortKey = QStringLiteral("count");
    int m_total = 0;
    bool m_resetNext = false;
};

} // namespace brulion::content
