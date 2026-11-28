// CodeFileTable — a CSV or TSV file of the viewer as a table (docs/CODE-FILES.md "Tables"; QML
// `CodeFileTable`): the rows of `document`'s text (io::parseCsv() with codefiles::tableSeparator()),
// at most MaxRows rows and MaxColumns columns, each cell at most MaxCellLength characters (one line:
// its line breaks shown as spaces). Read again whenever the document is.
//
//   CodeFileTable { id: table; document: doc }   // table.available, table.rowCount, table.columnCount
//   TableView { model: table; delegate: … required property string display }
#pragma once

#include "codefiledocument.h"

#include <QAbstractTableModel>
#include <QPointer>
#include <QStringList>

namespace brulion::codefiles {

class CodeFileTable : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(brulion::codefiles::CodeFileDocument *document READ document WRITE setDocument NOTIFY documentChanged FINAL)
    /// The document is a table file read whole (a .csv or .tsv: tableSeparator()).
    Q_PROPERTY(bool available READ available NOTIFY tableChanged FINAL)
    Q_PROPERTY(int rows READ rows NOTIFY tableChanged FINAL)
    Q_PROPERTY(int columns READ columns NOTIFY tableChanged FINAL)
    /// More rows or columns than shown.
    Q_PROPERTY(bool cut READ cut NOTIFY tableChanged FINAL)

public:
    static constexpr int MaxRows = 10000;
    static constexpr int MaxColumns = 200;
    static constexpr int MaxCellLength = 500;

    explicit CodeFileTable(QObject *parent = nullptr);

    CodeFileDocument *document() const { return m_document; }
    void setDocument(CodeFileDocument *document);
    bool available() const { return m_available; }
    int rows() const { return int(m_rows.size()); }
    int columns() const { return m_columns; }
    bool cut() const { return m_cut; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// The table of `text` of the file `path` (pure): its rows as shown, `cut` when some were left out.
    static QList<QStringList> tableOf(const QString &path, const QString &text, bool *cut);

Q_SIGNALS:
    void documentChanged();
    void tableChanged();

private:
    void read();

    QPointer<CodeFileDocument> m_document;
    QMetaObject::Connection m_loaded;
    QList<QStringList> m_rows;
    int m_columns = 0;
    bool m_available = false;
    bool m_cut = false;
};

} // namespace brulion::codefiles
