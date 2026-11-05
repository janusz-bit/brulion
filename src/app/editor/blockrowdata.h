// BlockRowData — typed access to one row of a BlockListModel for QML compiled ahead of time
// (docs/UI.md "Compiled QML"). A delegate's `model` object is untyped (`var`): reading its roles
// (`model.html`) cannot be compiled to C++. BlockDelegate takes the roles every block reads as
// typed `required property`s (read once when the block is created); the roles only some blocks
// or views read (the rendered text, a table's data, …) come from this object, as typed FINAL
// properties read on demand: a role costs nothing until a binding reads it, as with the model
// object (`model.html`), so hidden rows and other views never render a text they do not show.
// A property announces a change (its NOTIFY) when the model's dataChanged covers the row and
// names its role or no role — what the delegate model does for `model.<role>`. Like the model
// object it does not re-announce anything when only the row number changes (`row` follows the
// delegate's index when rows are inserted or removed above it: the same block, the same data).
//
//   BlockDelegate { required property int index
//                   readonly property BlockRowData rowData: BlockRowData { model: …blocks; row: block.index } }
//   Text { text: block.rowData.html }      TableView { data: block.rowData.tableData.rows }
#pragma once

#include "blocklistmodel.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::editor {

class BlockRowData : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::editor::BlockListModel *model READ model WRITE setModel NOTIFY modelChanged FINAL)
    Q_PROPERTY(int row READ row WRITE setRow NOTIFY rowChanged FINAL)
    // The roles (BlockListModel's role names, docs in blocklistmodel.h).
    Q_PROPERTY(QString html READ html NOTIFY htmlChanged FINAL)
    Q_PROPERTY(QString plainText READ plainText NOTIFY plainTextChanged FINAL)
    Q_PROPERTY(QString editText READ editText NOTIFY editTextChanged FINAL)
    Q_PROPERTY(QString lang READ lang NOTIFY langChanged FINAL)
    Q_PROPERTY(QString calloutType READ calloutType NOTIFY calloutTypeChanged FINAL)
    Q_PROPERTY(bool isSelected READ isSelected NOTIFY isSelectedChanged FINAL)
    Q_PROPERTY(QVariantList calcResults READ calcResults NOTIFY calcResultsChanged FINAL)
    Q_PROPERTY(QVariantMap tableData READ tableData NOTIFY tableDataChanged FINAL)
    Q_PROPERTY(QVariantMap chartData READ chartData NOTIFY chartDataChanged FINAL)
    Q_PROPERTY(QVariantMap query READ query NOTIFY queryChanged FINAL)
    Q_PROPERTY(QVariantList properties READ properties NOTIFY propertiesChanged FINAL)
    Q_PROPERTY(QVariantMap propertiesInfo READ propertiesInfo NOTIFY propertiesInfoChanged FINAL)

public:
    explicit BlockRowData(QObject *parent = nullptr);

    BlockListModel *model() const;
    void setModel(BlockListModel *model);
    int row() const { return m_row; }
    void setRow(int row);

    QString html() const;
    QString plainText() const;
    QString editText() const;
    QString lang() const;
    QString calloutType() const;
    bool isSelected() const;
    QVariantList calcResults() const;
    QVariantMap tableData() const;
    QVariantMap chartData() const;
    QVariantMap query() const;
    QVariantList properties() const;
    QVariantMap propertiesInfo() const;

Q_SIGNALS:
    void modelChanged();
    void rowChanged();
    void htmlChanged();
    void plainTextChanged();
    void editTextChanged();
    void langChanged();
    void calloutTypeChanged();
    void isSelectedChanged();
    void calcResultsChanged();
    void tableDataChanged();
    void chartDataChanged();
    void queryChanged();
    void propertiesChanged();
    void propertiesInfoChanged();

private:
    QVariant value(int role) const;
    void dataChanged(int first, int last, const QList<int> &roles);
    void announceAll();

    QPointer<BlockListModel> m_model;
    int m_row = -1;
    /// Connected to the model's dataChanged on the first read (an unread object costs nothing).
    mutable QMetaObject::Connection m_connection;
};

} // namespace brulion::editor
