#include "views/messages_p.h"

#include <QCoreApplication>

namespace brulion::views::messages {

QString columnLabel(QueryColumnKind kind)
{
    switch (kind) {
    case QueryColumnKind::Title: return QCoreApplication::translate("brulion::Views", "Title");
    case QueryColumnKind::Path: return QCoreApplication::translate("brulion::Views", "Path");
    case QueryColumnKind::Folder: return QCoreApplication::translate("brulion::Views", "Folder");
    case QueryColumnKind::Tags: return QCoreApplication::translate("brulion::Views", "Tags");
    case QueryColumnKind::Modified: return QCoreApplication::translate("brulion::Views", "Modified");
    case QueryColumnKind::Size: return QCoreApplication::translate("brulion::Views", "Size");
    case QueryColumnKind::Date: return QCoreApplication::translate("brulion::Views", "Date");
    case QueryColumnKind::Text: return QCoreApplication::translate("brulion::Views", "Text");
    case QueryColumnKind::Status: return QCoreApplication::translate("brulion::Views", "Status");
    case QueryColumnKind::Priority: return QCoreApplication::translate("brulion::Views", "Priority");
    case QueryColumnKind::Due: return QCoreApplication::translate("brulion::Views", "Due");
    case QueryColumnKind::Scheduled: return QCoreApplication::translate("brulion::Views", "Scheduled");
    case QueryColumnKind::Start: return QCoreApplication::translate("brulion::Views", "Start");
    case QueryColumnKind::Done: return QCoreApplication::translate("brulion::Views", "Done");
    case QueryColumnKind::Created: return QCoreApplication::translate("brulion::Views", "Created");
    case QueryColumnKind::Property:
    case QueryColumnKind::Formula: break;
    }
    return {};
}

QString summaryName(const QString &function)
{
    if (function == u"sum")
        return QCoreApplication::translate("brulion::Views", "Sum");
    if (function == u"average")
        return QCoreApplication::translate("brulion::Views", "Average");
    if (function == u"median")
        return QCoreApplication::translate("brulion::Views", "Median");
    if (function == u"min")
        return QCoreApplication::translate("brulion::Views", "Min");
    if (function == u"max")
        return QCoreApplication::translate("brulion::Views", "Max");
    if (function == u"range")
        return QCoreApplication::translate("brulion::Views", "Range");
    if (function == u"stddev")
        return QCoreApplication::translate("brulion::Views", "Std. deviation");
    if (function == u"count")
        return QCoreApplication::translate("brulion::Views", "Count");
    if (function == u"empty")
        return QCoreApplication::translate("brulion::Views", "Empty");
    if (function == u"filled")
        return QCoreApplication::translate("brulion::Views", "Filled");
    if (function == u"unique")
        return QCoreApplication::translate("brulion::Views", "Unique");
    if (function == u"checked")
        return QCoreApplication::translate("brulion::Views", "Checked");
    if (function == u"unchecked")
        return QCoreApplication::translate("brulion::Views", "Unchecked");
    if (function == u"earliest")
        return QCoreApplication::translate("brulion::Views", "Earliest");
    if (function == u"latest")
        return QCoreApplication::translate("brulion::Views", "Latest");
    return function;
}

QString summaryWithoutColumn(const QString &column)
{
    return QCoreApplication::translate("brulion::Views",
                                       "The summary of “%1” names a column the view does not show: add it to columns:.")
            .arg(column);
}

QString formulaError(const QString &column, const QString &message)
{
    return QCoreApplication::translate("brulion::Views", "Column “%1”: %2").arg(column, message);
}

QString days(int n)
{
    return QCoreApplication::translate("brulion::Views", "%n day(s)", "", n);
}

QString mapAsTable()
{
    return QCoreApplication::translate("brulion::Views", "Map views are shown as a table: Brulion has no maps.");
}

QString unknownViewType(const QString &type)
{
    return QCoreApplication::translate("brulion::Views", "The view type “%1” is not supported: shown as a table.")
            .arg(type);
}

QString boardNeedsGroup()
{
    return QCoreApplication::translate(
            "brulion::Views",
            "A board needs a group: its columns are the values of a property (group:status, groupBy). Shown as a "
            "table.");
}

QString boardWithoutDrops()
{
    return QCoreApplication::translate(
            "brulion::Views", "Cards cannot be moved between these columns: nothing in the notes holds their value.");
}

QString notYaml()
{
    return QCoreApplication::translate("brulion::Views",
                                       "This base is not a YAML mapping (filters, formulas, properties, views).");
}

QString noViews()
{
    return QCoreApplication::translate("brulion::Views", "This base has no views.");
}

QString badView(int number, const QString &message)
{
    return QCoreApplication::translate("brulion::Views", "View %1: %2").arg(number).arg(message);
}

QString badFilter(const QString &where, const QString &message)
{
    return QCoreApplication::translate("brulion::Views", "Filter “%1”: %2").arg(where, message);
}

QString badFormula(const QString &name, const QString &message)
{
    return QCoreApplication::translate("brulion::Views", "Formula “%1”: %2").arg(name, message);
}

QString circularFormula(const QString &name)
{
    return QCoreApplication::translate("brulion::Views", "Formula “%1” refers to itself (through other formulas).")
            .arg(name);
}

QString unknownSummary(const QString &name)
{
    return QCoreApplication::translate("brulion::Views", "Unknown summary “%1”.").arg(name);
}

QString unsupportedKey(const QString &key)
{
    return QCoreApplication::translate("brulion::Views", "“%1” is not supported and is left out.").arg(key);
}

QString blockKeyNotAscii(const QString &key)
{
    return QCoreApplication::translate("brulion::Views",
                                       "“%1” cannot be a block property: a key:: line uses the letters A–Z, digits, - "
                                       "and _.")
            .arg(key);
}

QString notEditable()
{
    return QCoreApplication::translate("brulion::Views", "This cell cannot be changed here.");
}

QString invalidValue(const QString &type)
{
    return QCoreApplication::translate("brulion::Views", "That is no valid value for a %1 property.").arg(type);
}

QString yamlNotEditable()
{
    return QCoreApplication::translate(
            "brulion::Views",
            "The base's YAML cannot be changed safely here (a flow mapping or an unusual layout): edit the file as "
            "text.");
}

QString noValue()
{
    return QCoreApplication::translate("brulion::Views", "No value");
}

QString fileName()
{
    return QCoreApplication::translate("brulion::Views", "Name");
}

} // namespace brulion::views::messages
