// The interface's layout as text (uilayout.h).
#include "uilayout/uilayout.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::uilayout {

namespace {

/// The buttons among `items` (no groups, no hidden ones).
qsizetype buttonsOf(const QStringList &items)
{
    return std::ranges::count_if(items, [](const QString &i) { return i != u"|" && !i.startsWith(u'-'); });
}

/// A button more than a place holds (the bottom bar's MaxBottom): why `item` is left out, "" it is not.
QString overflowOf(const QString &placeName, const QStringList &items, const QString &item)
{
    if (placeName != u"bottom" || item.startsWith(u'-') || buttonsOf(items) < MaxBottom)
        return {};
    return u"more than %1 bottom buttons: “%2” left out"_s.arg(MaxBottom).arg(item);
}

/// Why `item` cannot come next in the place `placeName` (`items` its items so far; `known` its names):
/// "" it can.
QString problemOf(const QString &placeName, const QStringList &known, const QStringList &items, const QString &item)
{
    if (item.isEmpty())
        return u"an empty item"_s;
    if (item == u"|")
        return {};
    const QString name = item.startsWith(u'-') ? item.mid(1) : item;
    if (!known.contains(name))
        return u"“%1” is no item of %2"_s.arg(name, placeName);
    if (items.contains(name) || items.contains(u'-' + name))
        return u"“%1” is in %2 twice"_s.arg(name, placeName);
    return overflowOf(placeName, items, item);
}

/// One call's place read from its arguments, its problems added to `warnings` ("line N: …").
Place readCall(const QString &name, const QString &arguments, const QStringList &known, int line, QStringList &warnings)
{
    Place place{name, {}};
    for (const QString &raw : arguments.split(u',')) {
        const QString item = raw.trimmed();
        const QString why = problemOf(name, known, place.items, item);
        if (why.isEmpty())
            place.items << item;
        else
            warnings << u"line %1: %2"_s.arg(line).arg(why);
    }
    return place;
}

} // namespace

const Place *Layout::place(QStringView name) const
{
    for (const Place &p : places)
        if (p.name == name)
            return &p;
    return nullptr;
}

const Vocabulary &defaultVocabulary()
{
    // (The app's own ids: the icon bar's entries, the pages panel's sections, the bottom bar's views.)
    static const Vocabulary v{
            {u"rail"_s, {u"journals"_s, u"calendar"_s, u"tasks"_s, u"habits"_s, u"gym"_s, u"flashcards"_s, u"allPages"_s,
                         u"tags"_s, u"graph"_s, u"search"_s}},
            {u"panel"_s, {u"pages"_s, u"favorites"_s, u"recent"_s}},
            {u"bottom"_s, {u"journals"_s, u"calendar"_s, u"search"_s, u"tasks"_s, u"habits"_s, u"gym"_s, u"flashcards"_s,
                           u"allPages"_s, u"tags"_s, u"graph"_s}},
    };
    return v;
}

Layout parse(QStringView text, const Vocabulary &vocabulary)
{
    static const QRegularExpression call(uR"(^#([A-Za-z][A-Za-z0-9]*)\((.*)\)$)"_s);
    Layout out;
    const QStringList lines = text.toString().split(u'\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(u"//"))
            continue;
        const QRegularExpressionMatch m = call.match(line);
        const QString name = m.captured(1);
        if (!m.hasMatch()) {
            out.warnings << u"line %1: no call"_s.arg(i + 1);
        } else if (!vocabulary.contains(name)) {
            out.warnings << u"line %1: no place “%2”"_s.arg(i + 1).arg(name);
        } else if (out.place(name)) {
            out.warnings << u"line %1: a second %2"_s.arg(i + 1).arg(name);
        } else {
            out.places << readCall(name, m.captured(2), vocabulary.value(name), i + 1, out.warnings);
        }
    }
    return out;
}

QString print(const Layout &layout)
{
    QString out;
    for (const Place &p : layout.places)
        out += u'#' + p.name + u'(' + p.items.join(u", "_s) + u")\n"_s;
    return out;
}

QString blockOf(QStringView note)
{
    static const QRegularExpression open(uR"(^[ \t]*(```|~~~)[ \t]*brulion-layout[ \t]*$)"_s);
    const QStringList lines = note.toString().remove(u'\r').split(u'\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = open.match(lines.at(i));
        if (!m.hasMatch())
            continue;
        QString body;
        for (int j = i + 1; j < lines.size(); ++j) {
            if (lines.at(j).trimmed() == m.captured(1))
                return body;
            body += lines.at(j) + u'\n';
        }
        return {}; // (never closed)
    }
    return {};
}

QStringList shown(const Layout &layout, QStringView place, const QStringList &fallback)
{
    const Place *p = layout.place(place);
    if (!p)
        return fallback;
    QStringList out;
    for (const QString &item : p->items)
        if (item != u"|" && !item.startsWith(u'-'))
            out << item;
    return out;
}

} // namespace brulion::uilayout
