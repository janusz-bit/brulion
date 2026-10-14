#include "index/querymessages_p.h"

#include "index/query.h"

#include <QCoreApplication>

namespace brulion {

namespace index_detail::messages {

QString unknownKey(const QString &key, const QString &allowed)
{
    return QCoreApplication::translate("brulion::Query",
                                       "Unknown filter “%1:”. Available: %2. Put text containing a colon in quotes.")
            .arg(key, allowed);
}

QString emptyValue(const QString &key, const QString &example)
{
    return QCoreApplication::translate("brulion::Query", "Filter “%1:” needs a value, e.g. %2.").arg(key, example);
}

QString badValue(const QString &key, const QString &value, const QString &allowed)
{
    return QCoreApplication::translate("brulion::Query", "Invalid value “%1” in “%2:”. Allowed: %3.")
            .arg(value, key, allowed);
}

QString badDate(const QString &key, const QString &value)
{
    return QCoreApplication::translate("brulion::Query",
                                       "Cannot understand the date “%1” in “%2:”. Examples: "
                                       "today, tomorrow, 2026-10-15, +7d, -2w, this-week, next-month, "
                                       "overdue, none, any.")
            .arg(value, key);
}

QString operatorWithKeyword(const QString &key, const QString &op, const QString &value)
{
    return QCoreApplication::translate("brulion::Query", "Operator “%1” cannot be combined with “%2” in “%3:”.")
            .arg(op, value, key);
}

QString badLimit(const QString &value, int max)
{
    return QCoreApplication::translate("brulion::Query", "Limit must be a number from 1 to %1 (got “%2”).")
            .arg(max)
            .arg(value);
}

QString unclosedQuote()
{
    return QCoreApplication::translate("brulion::Query", "Missing closing quote.");
}

QString notNegatable(const QString &key)
{
    return QCoreApplication::translate("brulion::Query", "Filter “%1:” cannot be negated with a minus.").arg(key);
}

QString typeConflict(const QString &key, const QString &value, const QString &type)
{
    return QCoreApplication::translate("brulion::Query", "“%1:%2” does not work with type:%3.").arg(key, value, type);
}

QString duplicate(const QString &key)
{
    return QCoreApplication::translate("brulion::Query", "Filter “%1:” may appear only once.").arg(key);
}

QString noDate()
{
    return QCoreApplication::translate("brulion::Query", "No date");
}

QString noTag()
{
    return QCoreApplication::translate("brulion::Query", "No tag");
}

QString noStatus()
{
    return QCoreApplication::translate("brulion::Query", "No status");
}

QString noPriority()
{
    return QCoreApplication::translate("brulion::Query", "No priority");
}

} // namespace index_detail::messages

QString queryStatusLabel(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return QCoreApplication::translate("brulion::Query", "To do");
    case TaskStatus::Doing: return QCoreApplication::translate("brulion::Query", "In progress");
    case TaskStatus::Done: return QCoreApplication::translate("brulion::Query", "Done");
    case TaskStatus::Cancelled: return QCoreApplication::translate("brulion::Query", "Cancelled");
    case TaskStatus::Deferred: return QCoreApplication::translate("brulion::Query", "Deferred");
    }
    return {};
}

QString queryPriorityLabel(Priority priority)
{
    switch (priority) {
    case Priority::Highest: return QCoreApplication::translate("brulion::Query", "Highest");
    case Priority::High: return QCoreApplication::translate("brulion::Query", "High");
    case Priority::Medium: return QCoreApplication::translate("brulion::Query", "Medium");
    case Priority::None: return QCoreApplication::translate("brulion::Query", "Normal");
    case Priority::Low: return QCoreApplication::translate("brulion::Query", "Low");
    case Priority::Lowest: return QCoreApplication::translate("brulion::Query", "Lowest");
    }
    return {};
}

} // namespace brulion
