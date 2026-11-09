#include "io/tablecsv.h"

#include "io/csv.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::io {

using namespace Qt::StringLiterals;

namespace {

/// What detectCsvSeparator() reads: the records of about the first 64 kB, at most 20.
constexpr qsizetype SampleLength = 64 * 1024;
constexpr qsizetype SampleRecords = 20;

/// A cell's text for CSV: `<br>` a line break (the table's form of one).
QString csvCellText(const QString &cell)
{
    if (!cell.contains(u'<'))
        return cell;
    static const QRegularExpression br(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption);
    return QString(cell).replace(br, u"\n"_s);
}

/// The first records of `text` (SampleLength, SampleRecords) split by `separator`.
QList<QStringList> sample(QStringView text, QChar separator)
{
    QList<QStringList> rows = parseCsv(text.left(SampleLength), separator);
    if (text.size() > SampleLength && !rows.isEmpty())
        rows.removeLast(); // cut in the middle
    if (rows.size() > SampleRecords)
        rows.resize(SampleRecords);
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
    // Cell `c` of `row` (`r`: its index in model.rows, -1 for the header): a formula's result when
    // `eval` has one, else the text as written.
    const auto cellText = [&](const QStringList &row, qsizetype r, qsizetype c) -> QString {
        const QStringView trimmed = QStringView(row.at(c)).trimmed();
        const bool formula = r >= 0 && trimmed.startsWith(u'=') && !trimmed.mid(1).trimmed().isEmpty();
        if (formula && r < eval.values.size() && c < eval.values.at(r).size() && !eval.values.at(r).at(c).isNull())
            return eval.values.at(r).at(c);
        return csvCellText(row.at(c));
    };
    QString out;
    QStringList fields;
    fields.reserve(columns);
    const auto record = [&](const QStringList &row, qsizetype r) {
        fields.clear();
        for (qsizetype c = 0; c < columns; ++c)
            fields.append(csvSafeText(c < row.size() ? cellText(row, r, c) : QString()));
        out += csvRow(fields, separator) + u"\r\n"_s;
    };
    record(model.header, -1);
    for (qsizetype r = 0; r < model.rows.size(); ++r)
        record(model.rows.at(r), r);
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
        const qsizetype fields = rows.isEmpty() ? 0 : rows.first().size();
        if (fields < 2)
            continue;
        const qsizetype same = std::ranges::count_if(rows, [&](const QStringList &r) { return r.size() == fields; });
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
    if (const auto declared = declaredSeparator(text)) {
        out.separator = *declared;
        // The records after the "sep=" line and its line break (CRLF, LF or CR).
        const qsizetype lineEnd = text.sliced(5).startsWith(u"\r\n") ? 7 : 6;
        text = text.sliced(std::min(text.size(), lineEnd));
    } else {
        out.separator = detectCsvSeparator(text);
    }
    QList<QStringList> records = parseCsv(text, out.separator);
    // Records without any text (blank lines, ";;;" lines) are left out.
    records.removeIf([](const QStringList &r) {
        return std::ranges::all_of(r, [](const QString &f) { return QStringView(f).trimmed().isEmpty(); });
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
