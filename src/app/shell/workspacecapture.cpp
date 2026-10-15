#include "workspacecapture.h"

#include "dates/dates.h"
#include "markdown/markdown.h"

#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::shell {

QString captureKindName(CaptureLine::Kind kind)
{
    switch (kind) {
    case CaptureLine::Kind::Empty: return {};
    case CaptureLine::Kind::Note: return u"note"_s;
    case CaptureLine::Kind::Reminder: return u"reminder"_s;
    case CaptureLine::Kind::Due: return u"due"_s;
    }
    return {};
}

CaptureLine buildCaptureLine(const QString &input, const QDateTime &now, Qt::DayOfWeek weekStart,
                             dates::UiLocale locale)
{
    static const QRegularExpression listMarker(u"^([-*+]|\\d+[.)])[ \\t]+"_s);
    CaptureLine out;
    QStringList lines = QString(input).remove(u'\r').split(u'\n');
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return out;

    const QString first = lines.first().trimmed();
    // "- ", "* ", "1. " as typed; otherwise a bullet.
    QString marker = u"- "_s;
    QString content = first;
    if (const QRegularExpressionMatch m = listMarker.match(first); m.hasMatch()) {
        marker = m.captured(1) + u' ';
        content = first.mid(m.capturedLength());
    }
    // A checkbox or TODO keyword typed by the user stays as it is.
    QString task;
    if (const auto t = markdown::matchTaskMarker(content, 0)) {
        task = content.left(t->contentStart);
        if (!task.endsWith(u' '))
            task += u' ';
        content = content.mid(t->contentStart);
    }
    out.text = content.trimmed();
    out.kind = CaptureLine::Kind::Note;

    const bool hasToken = content.contains(u"⏰"_s) || content.contains(u"📅"_s);
    std::optional<dates::NaturalDate> found;
    if (!hasToken && now.isValid())
        found = dates::parseNaturalDate(content, now, weekStart, locale);
    if (found && found->spec.date < now.date())
        found.reset(); // "wczoraj …": a note about the past, not a task
    QString line;
    if (found) {
        QString stripped = dates::stripNaturalDate(content, *found).trimmed();
        if (stripped.isEmpty())
            stripped = content.trimmed(); // only a date ("jutro o 9"): keep the words
        out.text = stripped;
        out.when = found->spec;
        QString token;
        if (found->spec.hasTime()) {
            out.kind = CaptureLine::Kind::Reminder;
            token = u"⏰ "_s + found->spec.toString();
        } else {
            out.kind = CaptureLine::Kind::Due;
            token = u"📅 "_s + found->spec.toString();
        }
        line = marker + (task.isEmpty() ? u"[ ] "_s : task) + stripped + u' ' + token;
    } else {
        line = marker + task + content.trimmed();
    }

    QStringList block{ line.trimmed() };
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString rest = lines.at(i).trimmed();
        block.append(rest.isEmpty() ? QString() : u"  "_s + rest);
    }
    out.block = block.join(u'\n');
    return out;
}

} // namespace brulion::shell
