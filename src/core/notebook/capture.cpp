#include "notebook/capture.h"

#include "dates/dates.h"
#include "markdown/markdown.h"

#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

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

    const bool hasToken = content.contains(u"⏰") || content.contains(u"📅");
    std::optional<dates::NaturalDate> found;
    if (!hasToken && now.isValid())
        found = dates::parseNaturalDate(content, now, weekStart, locale);
    if (found && found->spec.date < now.date())
        found.reset(); // "wczoraj …": a note about the past, not a task
    QString line;
    if (found) {
        // Only a date ("jutro o 9"): the words stay.
        if (const QString stripped = dates::stripNaturalDate(content, *found).trimmed(); !stripped.isEmpty())
            out.text = stripped;
        const bool reminder = found->spec.hasTime();
        out.kind = reminder ? CaptureLine::Kind::Reminder : CaptureLine::Kind::Due;
        out.when = found->spec;
        line = marker + (task.isEmpty() ? u"[ ] "_s : task) + out.text + (reminder ? u" ⏰ "_s : u" 📅 "_s)
               + found->spec.toString();
    } else {
        line = marker + task + out.text;
    }

    out.block = line.trimmed();
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString rest = lines.at(i).trimmed();
        out.block += rest.isEmpty() ? u"\n"_s : u"\n  "_s + rest;
    }
    return out;
}

} // namespace brulion::notebook
