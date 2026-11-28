#include "calendarkeys.h"

#include <QHash>

namespace brulion::views::atr {

using namespace Qt::StringLiterals;

namespace {

// How far a key moves the selected day: ± 1, ± 7, to its week's first or last day.
std::optional<int> moveOf(const QString &key, int inWeek)
{
    static const QHash<QString, int> moves{{u"left"_s, -1}, {u"right"_s, 1}, {u"up"_s, -7}, {u"down"_s, 7}};
    if (moves.contains(key))
        return moves.value(key);
    if (key != u"home" && key != u"end")
        return std::nullopt;
    return key == u"home" ? -inWeek : 6 - inWeek;
}

} // namespace

// Enter: the selected day picked (Ctrl: its daily note); PageUp / PageDown: another range.
std::optional<GridKey> pickOrPage(const QString &key, bool ctrl, const QString &selected)
{
    if (key == u"pageup" || key == u"pagedown")
        return GridKey{key == u"pageup" ? u"previous"_s : u"next"_s, {}};
    if (key != u"enter" || selected.isEmpty())
        return std::nullopt;
    return GridKey{ctrl ? u"open"_s : u"day"_s, {{u"date"_s, selected}}};
}

std::optional<GridKey> gridKey(const QString &key, bool ctrl, bool otherModifiers, int inWeek, const QString &selected)
{
    if (otherModifiers || (ctrl && key != u"enter"))
        return std::nullopt;
    if (const std::optional<int> days = moveOf(key, inWeek))
        return GridKey{u"move"_s, {{u"days"_s, *days}}};
    return pickOrPage(key, ctrl, selected);
}

std::optional<GridKey> gridKey(const atrament::Keystroke &k, int inWeek, const QString &selected)
{
    const atrament::Modifiers &m = k.modifiers;
    return gridKey(QString::fromStdU16String(std::u16string(k.key)), m.control, m.alt || m.shift || m.platform || m.function,
                   inWeek, selected);
}

atrament::Div dayDisc(const DayDisc &look, int day, bool isToday, bool isSelected, bool inMonth)
{
    namespace at = brulion::atrament;
    const bool marked = isToday || isSelected;
    const at::Color disc = isToday ? look.today : isSelected ? look.selected : at::transparent();
    const at::Color ink = marked ? (isToday ? look.onToday : look.onSelected) : inMonth ? look.text : look.faint;
    const std::uint16_t weight = marked ? at::FontWeight::Bold : inMonth ? at::FontWeight::Medium : at::FontWeight::Normal;
    return at::div()
            .size(look.size)
            .flexShrink0()
            .roundedFull()
            .bg(disc)
            .flexRow()
            .itemsCenter()
            .justifyCenter()
            .textSize(look.textSize)
            .fontFeatures(u"tnum")
            .fontWeight(weight)
            .textColor(ink)
            .child(at::text(std::u16string(QString::number(day).toStdU16String())));
}

} // namespace brulion::views::atr
