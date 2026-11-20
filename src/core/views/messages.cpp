#include "views/messages_p.h"

#include <QCoreApplication>

namespace brulion::views::messages {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("brulion::Views", text);
}

} // namespace

QString columnLabel(QueryColumnKind kind)
{
    switch (kind) {
    case QueryColumnKind::Title: return tr("Title");
    case QueryColumnKind::Path: return tr("Path");
    case QueryColumnKind::Folder: return tr("Folder");
    case QueryColumnKind::Tags: return tr("Tags");
    case QueryColumnKind::Modified: return tr("Modified");
    case QueryColumnKind::Size: return tr("Size");
    case QueryColumnKind::Date: return tr("Date");
    case QueryColumnKind::Text: return tr("Text");
    case QueryColumnKind::Status: return tr("Status");
    case QueryColumnKind::Priority: return tr("Priority");
    case QueryColumnKind::Due: return tr("Due");
    case QueryColumnKind::Scheduled: return tr("Scheduled");
    case QueryColumnKind::Start: return tr("Start");
    case QueryColumnKind::Done: return tr("Done");
    case QueryColumnKind::Created: return tr("Created");
    case QueryColumnKind::Property:
    case QueryColumnKind::Formula: break;
    }
    return {};
}

QString summaryName(const QString &function)
{
    if (function == u"sum")
        return tr("Sum");
    if (function == u"average")
        return tr("Average");
    if (function == u"median")
        return tr("Median");
    if (function == u"min")
        return tr("Min");
    if (function == u"max")
        return tr("Max");
    if (function == u"range")
        return tr("Range");
    if (function == u"stddev")
        return tr("Std. deviation");
    if (function == u"count")
        return tr("Count");
    if (function == u"empty")
        return tr("Empty");
    if (function == u"filled")
        return tr("Filled");
    if (function == u"unique")
        return tr("Unique");
    if (function == u"checked")
        return tr("Checked");
    if (function == u"unchecked")
        return tr("Unchecked");
    if (function == u"earliest")
        return tr("Earliest");
    if (function == u"latest")
        return tr("Latest");
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
    return tr("Map views are shown as a table: Brulion has no maps.");
}

QString unknownViewType(const QString &type)
{
    return QCoreApplication::translate("brulion::Views", "The view type “%1” is not supported: shown as a table.")
            .arg(type);
}

QString boardNeedsGroup()
{
    return tr("A board needs a group: its columns are the values of a property (group:status, groupBy). Shown as a "
              "table.");
}

QString boardWithoutDrops()
{
    return tr("Cards cannot be moved between these columns: nothing in the notes holds their value.");
}

QString notYaml()
{
    return tr("This base is not a YAML mapping (filters, formulas, properties, views).");
}

QString noViews()
{
    return tr("This base has no views.");
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
    return tr("This cell cannot be changed here.");
}

QString invalidValue(const QString &type)
{
    return QCoreApplication::translate("brulion::Views", "That is no valid value for a %1 property.").arg(type);
}

QString yamlNotEditable()
{
    return tr("The base's YAML cannot be changed safely here (a flow mapping or an unusual layout): edit the file as "
              "text.");
}

QString noValue()
{
    return tr("No value");
}

QString fileName()
{
    return tr("Name");
}

} // namespace brulion::views::messages
