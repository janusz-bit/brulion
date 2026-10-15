#include "io/csv.h"

namespace brulion::io {

using namespace Qt::StringLiterals;

QList<QStringList> parseCsv(QStringView text, QChar separator)
{
    QList<QStringList> rows;
    if (text.startsWith(QChar(0xFEFF)))
        text = text.sliced(1);
    QStringList row;
    QString field;
    bool quoted = false;     // inside a quoted field
    bool wasQuoted = false;  // the current field started with a quote
    bool rowHasData = false; // the current record has at least one character or separator
    const auto endField = [&] {
        row.append(field);
        field.clear();
        wasQuoted = false;
    };
    const auto endRow = [&] {
        endField();
        rows.append(row);
        row.clear();
        rowHasData = false;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == u'"') {
                if (i + 1 < text.size() && text[i + 1] == u'"') {
                    field += u'"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == separator) {
            endField();
            rowHasData = true;
        } else if (c == u'\r' || c == u'\n') {
            if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            endRow();
        } else if (c == u'"' && field.isEmpty() && !wasQuoted) {
            quoted = true;
            wasQuoted = true;
            rowHasData = true;
        } else {
            field += c;
            rowHasData = true;
        }
    }
    if (rowHasData || !field.isEmpty() || !row.isEmpty())
        endRow();
    return rows;
}

QString csvField(QStringView value, QChar separator)
{
    const bool needsQuotes = value.contains(separator) || value.contains(u'"') || value.contains(u'\n')
                             || value.contains(u'\r')
                             || (!value.isEmpty() && (value.front().isSpace() || value.back().isSpace()));
    if (!needsQuotes)
        return value.toString();
    QString out = value.toString();
    out.replace(u"\""_s, u"\"\""_s);
    return u'"' + out + u'"';
}

QString csvRow(const QStringList &fields, QChar separator)
{
    QStringList out;
    out.reserve(fields.size());
    for (const QString &f : fields)
        out.append(csvField(f, separator));
    return out.join(separator);
}

QString csvSafeText(const QString &value)
{
    if (value.isEmpty())
        return value;
    const QChar c = value.front();
    if (c != u'=' && c != u'+' && c != u'-' && c != u'@' && c != u'\t' && c != u'\r')
        return value;
    bool isNumber = false;
    value.toDouble(&isNumber);
    return isNumber ? value : u'\'' + value;
}

} // namespace brulion::io
