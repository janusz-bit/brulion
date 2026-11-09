// Reading a ```chart block (docs/SYNTAX.md §9 "Charts") and the module's messages.
#include "chart/chart.h"
#include "chart/messages_p.h"

#include "text/text.h"

#include <QHash>

using namespace Qt::StringLiterals;

namespace brulion::chart {

namespace {

/// A line's text cut for an error message.
QString excerpt(QStringView text)
{
    const QStringView t = text.trimmed();
    return t.size() > 40 ? detail::cutAt(t, 39).toString() + u'…' : t.toString();
}

/// The line without a trailing `// comment` (one after a blank: `http://` stays).
QStringView withoutComment(QStringView line)
{
    for (qsizetype i = 1; i + 1 < line.size(); ++i) {
        if (line.at(i) == u'/' && line.at(i + 1) == u'/' && line.at(i - 1).isSpace())
            return line.left(i).trimmed();
    }
    return line.trimmed();
}

enum class Key { None, Type, Table, Calc, Query, X, Y, Title };

Key keyOf(QStringView word)
{
    static const QHash<QString, Key> keys = {
            {u"type"_s, Key::Type},    {u"typ"_s, Key::Type},
            {u"rodzaj"_s, Key::Type},  {u"table"_s, Key::Table},
            {u"tabela"_s, Key::Table}, {u"tabelka"_s, Key::Table},
            {u"calc"_s, Key::Calc},    {u"obliczenia"_s, Key::Calc},
            {u"query"_s, Key::Query},  {u"zapytanie"_s, Key::Query},
            {u"x"_s, Key::X},          {u"labels"_s, Key::X},
            {u"label"_s, Key::X},      {u"etykiety"_s, Key::X},
            {u"etykieta"_s, Key::X},   {u"y"_s, Key::Y},
            {u"values"_s, Key::Y},     {u"value"_s, Key::Y},
            {u"series"_s, Key::Y},     {u"wartosci"_s, Key::Y},
            {u"wartosc"_s, Key::Y},    {u"serie"_s, Key::Y},
            {u"title"_s, Key::Title},  {u"tytul"_s, Key::Title},
    };
    return keys.value(text::foldKey(word), Key::None);
}

std::optional<Type> typeOf(QStringView value)
{
    static const QHash<QString, Type> types = {
            {u"bar"_s, Type::Bar},       {u"bars"_s, Type::Bar},     {u"column"_s, Type::Bar},
            {u"columns"_s, Type::Bar},   {u"slupkowy"_s, Type::Bar}, {u"slupki"_s, Type::Bar},
            {u"kolumnowy"_s, Type::Bar}, {u"line"_s, Type::Line},    {u"lines"_s, Type::Line},
            {u"liniowy"_s, Type::Line},  {u"linia"_s, Type::Line},   {u"linie"_s, Type::Line},
            {u"pie"_s, Type::Pie},       {u"kolowy"_s, Type::Pie},   {u"kolo"_s, Type::Pie},
    };
    const auto it = types.constFind(text::foldKey(value));
    return it == types.constEnd() ? std::nullopt : std::optional(*it);
}

/// "above" (0) or a number from 1.
std::optional<int> sourceIndexOf(QStringView value)
{
    const QString v = text::foldKey(value);
    if (v.isEmpty() || v == u"above" || v == u"powyzej" || v == u"previous" || v == u"poprzednia")
        return 0;
    bool ok = false;
    const int n = v.toInt(&ok);
    if (ok && n >= 1 && n <= 100000)
        return n;
    return std::nullopt;
}

/// A column name as written: quotes or brackets around it dropped.
QString columnName(QStringView name)
{
    QStringView n = name.trimmed();
    if (n.size() >= 2
        && ((n.front() == u'"' && n.back() == u'"') || (n.front() == u'[' && n.back() == u']')
            || (n.front() == u'„' && (n.back() == u'”' || n.back() == u'"'))))
        n = n.mid(1, n.size() - 2).trimmed();
    return n.toString();
}

/// `Przychód, "Koszt, brutto"; [Zysk]` → the names (separators outside quotes and brackets).
QStringList columnList(QStringView value)
{
    QStringList out;
    QChar close;
    qsizetype start = 0;
    for (qsizetype i = 0; i <= value.size(); ++i) {
        const QChar c = i < value.size() ? value.at(i) : QChar();
        if (!close.isNull() && i < value.size()) { // an unclosed quote ends with the value
            if (c == close || (close == u'”' && c == u'"'))
                close = QChar();
            continue;
        }
        if (c == u'"')
            close = u'"';
        else if (c == u'„')
            close = u'”';
        else if (c == u'[')
            close = u']';
        else if (i == value.size() || c == u',' || c == u';') {
            const QString name = columnName(value.mid(start, i - start));
            if (!name.isEmpty())
                out.append(name);
            start = i + 1;
        }
    }
    return out;
}

} // namespace

SpecResult parseSpec(QStringView source)
{
    SpecResult out;
    const auto fail = [&](Error::Code code, int line, QString detail = {}) {
        out.error = Error{code, line, std::move(detail)};
        return out;
    };
    if (source.size() > limits::MaxSpecLength)
        return fail(Error::Code::TooLarge, 0, u"source"_s);
    bool sourceGiven = false;
    const QList<text::Line> lines = text::splitLines(source);
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const int lineNo = int(i) + 1;
        const QStringView raw = QStringView(lines.at(i).text).trimmed();
        if (raw.isEmpty() || raw.startsWith(u"//") || raw.startsWith(u'#') || raw.startsWith(u"%%"))
            continue;
        const QStringView line = withoutComment(raw);
        const qsizetype colon = line.indexOf(u':');
        if (colon <= 0)
            return fail(Error::Code::Syntax, lineNo, excerpt(raw));
        const QStringView word = line.left(colon).trimmed();
        const QStringView value = line.mid(colon + 1).trimmed();
        const Key key = keyOf(word);
        if (key == Key::Table || key == Key::Calc || key == Key::Query) { // the one data source
            if (std::exchange(sourceGiven, true))
                return fail(Error::Code::SeveralSources, lineNo);
            out.spec.sourceLine = lineNo;
        }
        switch (key) {
        case Key::None: return fail(Error::Code::UnknownKey, lineNo, excerpt(word));
        case Key::Type: {
            const std::optional<Type> type = typeOf(value);
            if (!type)
                return fail(Error::Code::InvalidType, lineNo, excerpt(value));
            out.spec.type = *type;
            break;
        }
        case Key::Table:
        case Key::Calc: {
            out.spec.source = key == Key::Table ? SourceKind::Table : SourceKind::Calc;
            const std::optional<int> index = sourceIndexOf(value);
            if (!index)
                return fail(Error::Code::InvalidSource, lineNo, excerpt(value));
            out.spec.sourceIndex = *index;
            break;
        }
        case Key::Query:
            out.spec.source = SourceKind::Query;
            if (value.isEmpty())
                return fail(Error::Code::EmptyQuery, lineNo);
            out.spec.query = value.toString();
            break;
        case Key::X:
            out.spec.x = columnName(value);
            out.spec.xLine = lineNo;
            break;
        case Key::Y:
            out.spec.y = columnList(value);
            out.spec.yLine = lineNo;
            break;
        case Key::Title: out.spec.title = detail::cutAt(value, limits::MaxLabelLength).toString(); break;
        }
    }
    return out;
}

QString typeName(Type type)
{
    switch (type) {
    case Type::Bar: return detail::ChartMessages::tr("Bar chart");
    case Type::Line: return detail::ChartMessages::tr("Line chart");
    case Type::Pie: return detail::ChartMessages::tr("Pie chart");
    }
    return detail::ChartMessages::tr("Chart");
}

QString Error::message() const
{
    using detail::ChartMessages;
    QString text;
    switch (code) {
    case Code::Syntax: text = ChartMessages::tr("cannot read “%1”: write “key: value”.").arg(detail); break;
    case Code::UnknownKey:
        text = ChartMessages::tr("unknown key “%1” (type, table, calc, query, x, y, title).").arg(detail);
        break;
    case Code::InvalidType: text = ChartMessages::tr("“%1” is no chart type (bar, line, pie).").arg(detail); break;
    case Code::InvalidSource:
        text = ChartMessages::tr("“%1” names no table or calc block (“above” or its number on the page).").arg(detail);
        break;
    case Code::SeveralSources:
        text = ChartMessages::tr("a chart takes its data from one table, calc block or query.");
        break;
    case Code::NoTable:
        text = detail.isEmpty() ? ChartMessages::tr("there is no table above the chart.")
                                : ChartMessages::tr("the page has no table number %1.").arg(detail);
        break;
    case Code::NoCalc:
        text = detail.isEmpty() ? ChartMessages::tr("there is no calc block above the chart.")
                                : ChartMessages::tr("the page has no calc block number %1.").arg(detail);
        break;
    case Code::UnknownColumn: text = ChartMessages::tr("the table has no column “%1”.").arg(detail); break;
    case Code::NoValues: text = ChartMessages::tr("there are no numbers to draw."); break;
    case Code::NoPositiveValues: text = ChartMessages::tr("a pie chart needs values above zero."); break;
    case Code::TooLarge:
        if (detail == u"points")
            text = ChartMessages::tr("too many points to draw (more than %1).").arg(limits::MaxPoints);
        else if (detail == u"series")
            text = ChartMessages::tr("too many series to draw (more than %1).").arg(limits::MaxSeries);
        else if (detail == u"slices")
            text = ChartMessages::tr("too many slices to draw (more than %1).").arg(limits::MaxSlices);
        else
            text = ChartMessages::tr("the chart's text is too long (more than %1 kB).")
                           .arg(limits::MaxSpecLength / 1024);
        break;
    case Code::QueryFailed: text = ChartMessages::tr("the query cannot run: %1").arg(detail); break;
    case Code::QueryUnavailable: text = ChartMessages::tr("charts of query results are drawn in the app only."); break;
    case Code::EmptyQuery: text = ChartMessages::tr("write the query after “query:”."); break;
    }
    if (line > 0)
        return ChartMessages::tr("Line %1: %2").arg(QString::number(line), text);
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

} // namespace brulion::chart
