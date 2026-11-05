// BlockRowData — typed access to the roles of a BlockListModel row for QML compiled ahead of
// time (docs/UI.md "Compiled QML"). A delegate's `model` object is untyped (`var`): reading its
// roles (`model.html`) cannot be compiled to C++. BlockDelegate takes the roles every block reads
// as typed `required property`s (read once when the block is created); the roles only some blocks
// or views read (the rendered text, a table's data, …) come from this object, as typed FINAL
// properties read on demand: a role costs nothing until a binding reads it, as with
// `model.html`, so hidden rows and other views never render a text they do not show.
//
// It reads the delegate's own model object — the `model` property of the object it is declared
// in (its QObject parent), which the delegate model sets before any binding runs — so it never
// shows another row, also while the delegate is being created; each property reads that object's
// role and announces what the object announces for it (the delegate model's dataChanged), exactly
// what `model.<role>` does.
//
//   BlockDelegate { required property var model
//                   readonly property BlockRowData rowData: BlockRowData {} }
//   Text { text: block.rowData.html }      TableView { data: block.rowData.tableData.rows }
#pragma once

#include <QList>
#include <QMetaProperty>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::editor {

class BlockRowData : public QObject {
    Q_OBJECT
    // The roles (BlockListModel's role names, docs in blocklistmodel.h).
    Q_PROPERTY(QString html READ html NOTIFY htmlChanged FINAL)
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

    QString html() const;
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
    void htmlChanged();
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
    enum Role {
        Html,
        EditText,
        Lang,
        CalloutType,
        IsSelected,
        CalcResults,
        TableData,
        ChartData,
        Query,
        Properties,
        PropertiesInfo,
        RoleCount
    };
    /// The value of `role` on the delegate's model object (invalid without one); the first read
    /// of a role connects the object's change signal for it to ours.
    QVariant value(Role role, const char *name, void (BlockRowData::*changed)()) const;
    /// The parent's `model` object (looked up until it is found).
    QObject *source() const;

    mutable QPointer<QObject> m_source;
    mutable bool m_sourceLooked = false; ///< found (looked up again until it is)
    mutable int m_connected = 0;         ///< bit per Role: its change signal is connected
};

} // namespace brulion::editor
