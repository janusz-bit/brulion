#include "dataview/messages_p.h"

#include <QCoreApplication>

namespace brulion::dataview::messages {

QString atLine(int line, const QString &message)
{
    return QCoreApplication::translate("brulion::Dataview", "Line %1: %2").arg(line).arg(message);
}

QString emptyQuery()
{
    return QCoreApplication::translate("brulion::Dataview", "Write a query, e.g. LIST FROM #projekt or TASK WHERE !completed.");
}

QString expectedQueryType(const QString &found)
{
    return QCoreApplication::translate("brulion::Dataview", "A Dataview query starts with LIST, TABLE or TASK, not “%1”.").arg(found);
}

QString calendarNotSupported()
{
    return QCoreApplication::translate("brulion::Dataview", "CALENDAR views are not supported: use LIST, TABLE or TASK.");
}

QString unknownCommand(const QString &found)
{
    return QCoreApplication::translate("brulion::Dataview", "Unknown command “%1”. Available: FROM, WHERE, SORT, GROUP BY, FLATTEN, LIMIT.").arg(found);
}

QString fromNotFirst()
{
    return QCoreApplication::translate("brulion::Dataview", "FROM comes once, right after LIST, TABLE or TASK.");
}

QString badSource(const QString &found)
{
    return QCoreApplication::translate("brulion::Dataview", "Cannot read the source “%1”. Examples: #tag, \"Folder\", [[Page]], outgoing([[Page]]).").arg(found);
}

QString csvNotSupported()
{
    return QCoreApplication::translate("brulion::Dataview", "csv() sources are not supported.");
}

QString badLimit(const QString &found, int max)
{
    return QCoreApplication::translate("brulion::Dataview", "LIMIT needs a whole number from 1 to %1, not “%2”.").arg(max).arg(found);
}

QString unclosedString()
{
    return QCoreApplication::translate("brulion::Dataview", "A string is not closed with \".");
}

QString unclosedLink()
{
    return QCoreApplication::translate("brulion::Dataview", "A link is not closed with ]].");
}

QString expectedExpression(const QString &found)
{
    return found.isEmpty() ? QCoreApplication::translate("brulion::Dataview", "An expression is missing at the end.")
                           : QCoreApplication::translate("brulion::Dataview", "Expected a value or a field, found “%1”.").arg(found);
}

QString expectedClosing(const QString &closing, const QString &found)
{
    return found.isEmpty() ? QCoreApplication::translate("brulion::Dataview", "“%1” is missing at the end.").arg(closing)
                           : QCoreApplication::translate("brulion::Dataview", "Expected “%1”, found “%2”.").arg(closing, found);
}

QString expectedName(const QString &after)
{
    return QCoreApplication::translate("brulion::Dataview", "A name is missing after %1.").arg(after);
}

QString unexpected(const QString &found)
{
    return QCoreApplication::translate("brulion::Dataview", "Unexpected “%1”.").arg(found);
}

QString unknownFunction(const QString &name)
{
    return QCoreApplication::translate("brulion::Dataview", "Unknown function “%1”.").arg(name);
}

QString unsupportedFunction(const QString &name)
{
    return QCoreApplication::translate("brulion::Dataview", "The function “%1” is not supported: Brulion does not run regular expressions or scripts from notes.")
            .arg(name);
}

QString wrongArgumentCount(const QString &name)
{
    return QCoreApplication::translate("brulion::Dataview", "Wrong number of arguments for “%1”.").arg(name);
}

QString tooDeep()
{
    return QCoreApplication::translate("brulion::Dataview", "The expression is nested too deeply.");
}

QString tooLong(int max)
{
    return QCoreApplication::translate("brulion::Dataview",
                                       "The expression is too long: at most %1 operators may follow one another.")
            .arg(max);
}

QString valueTooDeep(int max)
{
    return QCoreApplication::translate("brulion::Dataview",
                                       "A list, object or lambda is nested more than %1 levels deep.")
            .arg(max);
}

QString valueTooLarge(qint64 items, qint64 chars)
{
    return QCoreApplication::translate("brulion::Dataview",
                                       "A value is too large: more than %1 items or %2 characters.")
            .arg(items)
            .arg(chars);
}

QString badDate(const QString &text)
{
    return QCoreApplication::translate("brulion::Dataview", "Cannot read the date “%1”. Examples: date(today), date(2026-10-15), date(\"2026-10-15 09:30\").")
            .arg(text);
}

QString badDuration(const QString &text)
{
    return QCoreApplication::translate("brulion::Dataview", "Cannot read the duration “%1”. Examples: dur(3 days), dur(\"1h 30m\").").arg(text);
}

QString tooCostly()
{
    return QCoreApplication::translate("brulion::Dataview", "The query takes too long to evaluate. Narrow it down with FROM or WHERE.");
}

QString tooManyRows(int max)
{
    return QCoreApplication::translate("brulion::Dataview", "The query makes more than %1 rows.").arg(max);
}

QString notAFunction(const QString &name)
{
    return QCoreApplication::translate("brulion::Dataview", "“%1” is not a function.").arg(name);
}

QString fileHeader()
{
    return QCoreApplication::translate("brulion::Dataview", "File");
}

QString groupHeader()
{
    return QCoreApplication::translate("brulion::Dataview", "Group");
}

QString years(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n year(s)", nullptr, n);
}

QString months(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n month(s)", nullptr, n);
}

QString weeks(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n week(s)", nullptr, n);
}

QString days(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n day(s)", nullptr, n);
}

QString hours(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n hour(s)", nullptr, n);
}

QString minutes(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n minute(s)", nullptr, n);
}

QString seconds(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n second(s)", nullptr, n);
}

QString milliseconds(int n)
{
    return QCoreApplication::translate("brulion::Dataview", "%n millisecond(s)", nullptr, n);
}

} // namespace brulion::dataview::messages
