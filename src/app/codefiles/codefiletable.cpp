// CodeFileTable (codefiletable.h): a CSV or TSV file as a table.
#include "codefiletable.h"

#include "codefiles/viewer.h"
#include "io/csv.h"

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

CodeFileTable::CodeFileTable(QObject *parent) : QAbstractTableModel(parent) {}

void CodeFileTable::setDocument(CodeFileDocument *document)
{
    if (document == m_document)
        return;
    disconnect(m_loaded);
    m_document = document;
    if (document)
        m_loaded = connect(document, &CodeFileDocument::loaded, this, &CodeFileTable::read);
    Q_EMIT documentChanged();
    read();
}

QList<QStringList> CodeFileTable::tableOf(const QString &path, const QString &text, bool *cut)
{
    *cut = false;
    const qsizetype lineEnd = text.indexOf(u'\n');
    const QChar separator = tableSeparator(path, QStringView(text).first(lineEnd < 0 ? text.size() : lineEnd));
    if (separator.isNull())
        return {};
    QList<QStringList> rows = io::parseCsv(text, separator);
    if (rows.size() > MaxRows) {
        rows.resize(MaxRows);
        *cut = true;
    }
    for (QStringList &row : rows) {
        if (row.size() > MaxColumns) {
            row.resize(MaxColumns);
            *cut = true;
        }
        for (QString &cell : row) {
            cell.replace(u'\r', u' ').replace(u'\n', u' ');
            if (cell.size() > MaxCellLength)
                cell = cell.left(MaxCellLength) + u'…';
        }
    }
    return rows;
}

void CodeFileTable::read()
{
    beginResetModel();
    m_rows.clear();
    m_columns = 0;
    m_cut = false;
    const bool text = m_document && m_document->kind() == u"text" && !m_document->truncated();
    if (text) {
        // (Its lines joined: the viewer's lines are the file's, line breaks inside quotes too.)
        QStringList lines;
        for (int line = 1; line <= m_document->lineCount(); ++line)
            lines << m_document->lineText(line);
        m_rows = tableOf(m_document->path(), lines.join(u'\n'), &m_cut);
    }
    for (const QStringList &row : std::as_const(m_rows))
        m_columns = std::max(m_columns, int(row.size()));
    m_available = text && !tableSeparator(m_document->path(), u"").isNull();
    endResetModel();
    Q_EMIT tableChanged();
}

int CodeFileTable::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

int CodeFileTable::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_columns; }

QVariant CodeFileTable::data(const QModelIndex &index, int role) const
{
    if (role != Qt::DisplayRole || !index.isValid() || index.row() >= m_rows.size())
        return {};
    return m_rows.at(index.row()).value(index.column());
}

QHash<int, QByteArray> CodeFileTable::roleNames() const { return {{Qt::DisplayRole, "display"}}; }

} // namespace brulion::codefiles
