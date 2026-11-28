// The canvas view's lists (docs/UI.md "Canvas view"): the cards and the edges in or near the view,
// in z-order — only those are made into QML items (a canvas of a thousand cards shows the few
// dozen on screen). CanvasEditor updates them after every change of the canvas, the view or the
// selection: a card or edge that stays is changed in place (dataChanged), one that comes into view
// is inserted and one that leaves removed, so delegates are kept while the view moves.
#pragma once

#include "canvas/canvas.h"

#include <QAbstractListModel>
#include <QHash>
#include <QRectF>
#include <QSet>

#include <functional>

namespace brulion::canvasview {

/// What a file card shows: its kind — "note", "canvas", "picture", "pdf", "drawing", "file" (any
/// other), "missing" (no such file in the notebook) — and the vault path it resolves to.
struct FileKind {
    QString kind;
    QString path;
};
using FileKindOf = std::function<FileKind(const QString &path)>;

class CanvasNodeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        NodeIdRole = Qt::UserRole + 1,
        KindRole,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        ColorRole,
        TextRole,
        FileRole,
        SubpathRole,
        UrlRole,
        LabelRole,
        BackgroundRole,
        BackgroundStyleRole,
        ShapeRole,  ///< nodeStyle().shape (Advanced Canvas)
        BorderRole, ///< nodeStyle().border
        SelectedRole,
        FileKindRole,
        FilePathRole,
        TitleRole,
        OrderRole,
    };

    explicit CanvasNodeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    /// The nodes of `c` whose rectangles meet `area`, in z-order, with `selected` marked (the view
    /// draws groups below every card, as Obsidian does, and edges between them).
    void sync(const canvas::Canvas &c, const QRectF &area, const QSet<QString> &selected, const FileKindOf &kindOf);
    /// The row of node `id`, -1 when it is not listed.
    Q_INVOKABLE int rowOf(const QString &id) const;
    /// The file kinds are asked again (a file appeared, the index changed).
    void forgetKinds() { m_kinds.clear(); }

Q_SIGNALS:
    void countChanged();

private:
    struct Row {
        canvas::Node node;
        bool selected = false;
        QString fileKind;
        QString filePath;
        QString title;
        int order = 0;
        bool sameAs(const Row &o) const
        {
            return node.sameAs(o.node) && selected == o.selected && fileKind == o.fileKind && filePath == o.filePath
                   && title == o.title && order == o.order;
        }
    };
    QList<Row> m_rows;
    QHash<QString, FileKind> m_kinds;
};

class CanvasEdgeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        EdgeIdRole = Qt::UserRole + 1,
        PathRole,
        ArrowsRole,
        ColorRole,
        LabelRole,
        LabelXRole,
        LabelYRole,
        SelectedRole,
        FromNodeRole,
        ToNodeRole,
    };

    explicit CanvasEdgeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    /// The edges of `c` whose curves meet `area`, in their order.
    void sync(const canvas::Canvas &c, const QRectF &area, const QSet<QString> &selected);

Q_SIGNALS:
    void countChanged();

private:
    struct Row {
        QString id;
        QString path;
        QString arrows;
        QString color;
        QString label;
        QPointF labelPoint;
        bool selected = false;
        QString from;
        QString to;
        bool operator==(const Row &) const = default;
    };
    QList<Row> m_rows;
};

/// An edge's arrowheads as one SVG path ("M x y L … Z" per arrowhead; "" for none).
QString arrowsPath(const QList<QPointF> &start, const QList<QPointF> &end);

} // namespace brulion::canvasview
