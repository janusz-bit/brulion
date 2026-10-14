#include "markdown/templates.h"

#include <QLocale>
#include <QRegularExpression>

namespace brulion::markdown {

using namespace Qt::StringLiterals;

namespace {

QLocale localeOf(dates::UiLocale locale)
{
    return locale == dates::UiLocale::Polish ? QLocale(QLocale::Polish, QLocale::Poland)
                                             : QLocale(QLocale::English, QLocale::UnitedStates);
}

/// Replacement for one variable, or a null QString to keep the placeholder as written.
QString resolveVar(const QString &name, const QString &arg, const TemplateVars &vars)
{
    const QLocale locale = localeOf(vars.locale);
    if (name == u"date")
        return locale.toString(vars.now, arg.isEmpty() ? u"yyyy-MM-dd"_s : arg);
    if (name == u"time")
        return locale.toString(vars.now, arg.isEmpty() ? u"HH:mm"_s : arg);
    if (name == u"title")
        return vars.title.isNull() ? u""_s : vars.title;
    if (name == u"weekday")
        return dates::weekdayName(vars.now.date().dayOfWeek(), vars.locale);
    return {};
}

} // namespace

TemplateResult applyTemplate(QStringView tpl, const TemplateVars &vars)
{
    static const QRegularExpression re(u"\\{\\{[ \\t]*([A-Za-z]+)[ \\t]*(?::([^}\\r\\n]*))?\\}\\}"_s);
    TemplateResult out;
    qsizetype last = 0;
    auto it = re.globalMatchView(tpl);
    while (it.hasNext()) {
        const auto m = it.next();
        out.text += tpl.sliced(last, m.capturedStart(0) - last);
        last = m.capturedEnd(0);
        const QString name = m.captured(1).toLower();
        if (name == u"cursor") {
            if (out.cursor < 0)
                out.cursor = out.text.size();
            continue;
        }
        const QString value = resolveVar(name, m.captured(2).trimmed(), vars);
        out.text += value.isNull() ? m.captured(0) : value;
    }
    out.text += tpl.sliced(last);
    return out;
}

} // namespace brulion::markdown
