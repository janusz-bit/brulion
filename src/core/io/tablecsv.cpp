#include "io/tablecsv.h"

#include "io/csv.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::io {

using namespace Qt::StringLiterals;

namespace {

/// A cell's text for CSV: `<br>` a line break (the table's form of one).
QString csvCellText(const QString &cell)
{
    if (!cell.contains(u'<'))
        return cell;
    static const QRegularExpression br(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption);
    QString out = cell;
    return out.replace(br, u"\n"_s);
}

/// The first records of `text` (about 64 kB, at most 20 records) split by `separator`.
QList<QStringList> sample(QStringView text, QChar separator)
{
    QList<QStringList> rows = parseCsv(text.left(64 * 1024), separator);
    if (text.size() > 64 * 1024 && !rows.isEmpty())
        rows.removeLast(); // cut in the middle
    if (rows.size() > 20)
        rows.resize(20);
    return rows;
}

/// Excel's "sep=;" line at the start: the separator it names.
std::optional<QChar> declaredSeparator(QStringView text)
{
    if (text.startsWith(QChar(0xFEFF)))
        text = text.sliced(1);
    if (text.size() >= 5 && text.startsWith(u"sep=", Qt::CaseInsensitive)
        && (text.size() == 5 || text.at(5) == u'\n' || text.at(5) == u'\r'))
        return text.at(4);
    return std::nullopt;
}

} // namespace

QString tableToCsv(const calc::TableModel &model, const calc::TableEval &eval, QChar separator)
{
    qsizetype columns = model.header.size();
    for (const QStringList &row : model.rows)
        columns = std::max(columns, row.size());
    const auto record = [&](const QStringList &cells) {
        QStringList fields;
        fields.reserve(columns);
        for (qsizetype c = 0; c < columns; ++c)
            fields.append(csvSafeText(c < cells.size() ? cells.at(c) : QString()));
        return csvRow(fields, separator) + u"\r\n"_s;
    };
    QStringList header;
    for (const QString &h : model.header)
        header.append(csvCellText(h));
    QString out = record(header);
    for (qsizetype r = 0; r < model.rows.size(); ++r) {
        QStringList cells;
        const QStringList &row = model.rows.at(r);
        for (qsizetype c = 0; c < row.size(); ++c) {
            const QString &cell = row.at(c);
            const QStringView trimmed = QStringView(cell).trimmed();
            const bool formula = trimmed.startsWith(u'=') && !trimmed.mid(1).trimmed().isEmpty();
            if (formula && r < eval.values.size() && c < eval.values.at(r).size() && !eval.values.at(r).at(c).isNull())
                cells.append(eval.values.at(r).at(c));
            else
                cells.append(csvCellText(cell));
        }
        out += record(cells);
    }
    return out;
}

QChar detectCsvSeparator(QStringView text)
{
    if (const auto declared = declaredSeparator(text))
        return *declared;
    QChar best = u',';
    qsizetype bestScore = 0;
    for (const QChar candidate : {u'\t', u';', u','}) {
        const QList<QStringList> rows = sample(text, candidate);
        if (rows.isEmpty())
            continue;
        const qsizetype fields = rows.first().size();
        if (fields < 2)
            continue;
        const qsizetype same =
                std::count_if(rows.cbegin(), rows.cend(), [&](const QStringList &r) { return r.size() == fields; });
        const qsizetype score = same * 1000 + std::min<qsizetype>(fields, 999);
        if (score > bestScore) { // ties: tab, then semicolon (decimal commas), then comma
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

std::optional<CsvTable> tableFromCsv(QStringView text)
{
    if (text.size() > MaxCsvTextLength)
        return std::nullopt;
    if (text.startsWith(QChar(0xFEFF)))
        text = text.sliced(1);
    CsvTable out;
    out.separator = detectCsvSeparator(text);
    if (declaredSeparator(text)) {
        const qsizetype eol = text.indexOf(u'\n');
        text = eol < 0 ? QStringView() : text.sliced(eol + 1);
    }
    QList<QStringList> records = parseCsv(text, out.separator);
    // Records without any text (blank lines, ";;;" lines) are left out.
    records.removeIf([](const QStringList &r) {
        return std::all_of(r.cbegin(), r.cend(), [](const QString &f) { return f.trimmed().isEmpty(); });
    });
    if (records.isEmpty())
        return std::nullopt;
    if (records.size() > MaxCsvRows + 1) {
        records.resize(MaxCsvRows + 1);
        out.truncated = true;
    }
    qsizetype columns = 0;
    for (const QStringList &r : std::as_const(records))
        columns = std::max(columns, r.size());
    if (columns > MaxCsvColumns) {
        columns = MaxCsvColumns;
        out.truncated = true;
    }
    const auto cell = [&](const QString &field) {
        QString v = field.trimmed();
        if (v.size() > MaxCsvCellLength) {
            v = v.left(MaxCsvCellLength - 1) + u'…';
            out.truncated = true;
        }
        if (v.startsWith(u'='))
            v.prepend(u'\\'); // text, not a formula
        return v;
    };
    const auto row = [&](const QStringList &record) {
        QStringList cells;
        cells.reserve(columns);
        for (qsizetype c = 0; c < columns; ++c)
            cells.append(c < record.size() ? cell(record.at(c)) : QString());
        return cells;
    };
    out.model.header = row(records.first());
    out.model.aligns = QList<calc::TableAlign>(columns, calc::TableAlign::None);
    out.model.rows.reserve(records.size() - 1);
    for (qsizetype r = 1; r < records.size(); ++r)
        out.model.rows.append(row(records.at(r)));
    return out;
}

} // namespace brulion::io
