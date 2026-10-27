#include "widgetactions.h"

#include "planningutil.h"

#include "notebook/journals.h"
#include "types.h"

#include <QCoreApplication>
#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::widgets {

std::optional<WidgetAction> widgetActionFromJson(const QJsonObject &json)
{
    WidgetAction action;
    action.id = json.value(u"id"_s).toString();
    if (action.id.isEmpty())
        return std::nullopt;
    const QString kind = json.value(u"kind"_s).toString(u"task"_s);
    if (kind == u"task")
        action.kind = WidgetAction::Kind::Task;
    else if (kind == u"note")
        action.kind = WidgetAction::Kind::Note;
    else
        return std::nullopt;
    action.text = json.value(u"text"_s).toString();
    action.date = planning::parseIsoDate(json.value(u"date"_s).toString());
    const QTime time = QTime::fromString(json.value(u"time"_s).toString(), u"HH:mm"_s);
    if (time.isValid() && action.date.isValid())
        action.time = time;
    action.alarm = json.value(u"alarm"_s).toBool() && action.time.has_value();
    action.vault = json.value(u"vault"_s).toString();
    return action;
}

QList<WidgetAction> widgetActions(const QJsonArray &array)
{
    QList<WidgetAction> out;
    QSet<QString> seen;
    for (const QJsonValue &value : array) {
        const auto action = widgetActionFromJson(value.toObject());
        if (!action || seen.contains(action->id))
            continue;
        seen.insert(action->id);
        out.append(*action);
    }
    return out;
}

QString widgetActionBlock(const WidgetAction &action)
{
    if (action.text.trimmed().isEmpty())
        return {};
    if (action.kind == WidgetAction::Kind::Note)
        return notebook::blockFromText(action.text);
    if (!action.date.isValid())
        return planning::taskLine(action.text);
    if (action.time)
        return planning::taskLine(action.text, std::nullopt, DateSpec{action.date, action.time}, action.alarm);
    return planning::taskLine(action.text, DateSpec{action.date, std::nullopt});
}

QString widgetActionsAddedMessage(int count)
{
    return QCoreApplication::translate("brulion::widgets::WidgetActions",
                                       "%n entry(ies) added from the widget to today's journal.", nullptr, count);
}

QString widgetActionFailedMessage(const QString &text)
{
    return QCoreApplication::translate("brulion::widgets::WidgetActions",
                                       "Could not add “%1” from the widget to today's journal.")
            .arg(text.simplified());
}

} // namespace brulion::widgets
