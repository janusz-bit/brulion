// OutlineModel — the headings of the page being edited (the side panel's "Konspekt"), live from
// the block editor's BlockListModel (any model with the roles `headingLevel`, `plainText` and
// optionally `view`, `line`): edits show up at once, without waiting for the save.
//
//   OutlineModel { source: page.blocks }
//   ListView { model: outline; delegate: … onClicked: editor.revealRow(model.row) }
//
// Roles: row (row in the source: BlockEditor.revealRow), level (1–6), depth (level relative to
// the shallowest heading: 0 …), text (plain), line (first line in the file, -1 when unknown).
#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>

namespace brulion::content {

class OutlineModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { RowRole = Qt::UserRole + 1, LevelRole, DepthRole, TextRole, LineRole };
    Q_ENUM(Role)

    explicit OutlineModel(QObject *parent = nullptr);

    QAbstractItemModel *source() const { return m_source.data(); }
    void setSource(QAbstractItemModel *source);
    int count() const { return int(m_rows.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Recompute now (tests; normally coalesced after source changes).
    Q_INVOKABLE void rebuild();
    /// Outline entry for a source row: the heading the row belongs to (the last heading at or
    /// above it), -1 before the first heading — to highlight where the caret is.
    Q_INVOKABLE int entryForSourceRow(int sourceRow) const;

Q_SIGNALS:
    void sourceChanged();
    void countChanged();

private:
    struct Entry {
        int row = -1;
        int level = 0;
        int depth = 0;
        QString text;
        int line = -1;

        friend bool operator==(const Entry &, const Entry &) = default;
    };
    void schedule();

    QPointer<QAbstractItemModel> m_source;
    QList<QMetaObject::Connection> m_connections;
    QList<Entry> m_rows;
    QTimer m_timer;
};

} // namespace brulion::content
