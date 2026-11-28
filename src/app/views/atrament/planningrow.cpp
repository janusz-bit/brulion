#include "planningrow.h"

#include "pageicons.h"
#include "renderedspans.h"
#include "taskbox.h"

#include <QColor>
#include <QCoreApplication>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using editor::atr::u16View;
using namespace Qt::StringLiterals;


void EntryRows::menuAt(const Act &act, const QString &action, const AgendaEntry &e, at::Point at)
{
    act(action, {{u"entry"_s, e.map}, {u"x"_s, qreal(at.x)}, {u"y"_s, qreal(at.y)}});
}

// An entry as PlanningItemRow: its marker, its time, its title and place, its chip; a click opens
// it, a right-click its date's menu.
at::AnyElement EntryRows::row(int index, const AgendaEntry &e, bool past) const
{
    const QVariantMap entry = e.map;
    const QString name = joined({e.time, e.title, e.kindLabel(), subtitle(e)}, u", "_s);
    at::Div line = at::div()
                           .id(at::ElementId::number(quint64(index)))
                           .relative()
                           .flexRow()
                           .itemsStart()
                           .gap(8)
                           .pl(6)
                           .pr(8)
                           .py(5)
                           .minH(look.rowHeight)
                           .rounded(look.radius)
                           .hover([hover = look.hover](at::Style &s) { s.background = at::Background::solid(hover); })
                           .cursorPointer()
                           .role(at::Role::ListItem)
                           .ariaLabel(u16View(name))
                           .onClick([onAct = act, entry](const at::ClickEvent &, at::Window &, at::App &) {
                               onAct(u"open"_s, {{u"entry"_s, entry}});
                           })
                           .child(marker(index, e));
    if (e.hasTokenMenu())
        std::move(line).onMouseDown(at::MouseButton::Right,
                                    [onAct = act, e](const at::MouseDownEvent &m, at::Window &, at::App &) {
                                        menuAt(onAct, u"menu"_s, e, m.position);
                                    });
    if (!e.time.isEmpty())
        std::move(line).child(at::div()
                                      .flexShrink0()
                                      .mt(4)
                                      .textSize(look.smallSize)
                                      .fontSemibold()
                                      .fontFeatures(u"tnum")
                                      .textColor(e.closed ? look.faint : look.text)
                                      .child(at::text(u16View(e.time))));
    std::move(line).child(texts(e, past));
    if (!e.kindLabel().isEmpty() && width > look.chipMinWidth)
        std::move(line).child(chip(e));
    return std::move(line).child(track(false, index));
}

// The entry's first column: a task's box, an event's colour, the entry's icon.
at::AnyElement EntryRows::marker(int index, const AgendaEntry &e) const
{
    at::Div column = at::div().size(look.entryMarker).flexShrink0().flexRow().itemsCenter().justifyCenter();
    if (e.isTask)
        return std::move(column).child(
                taskBoxShape(e.taskStatus(), {look.box, look.accent, look.background})
                        .ariaLabel(u16View(entryWords("Done: %1").arg(e.title)))
                        .onClick([onAct = act, e](const at::ClickEvent &, at::Window &w, at::App &) {
                            onAct(u"toggle"_s, {{u"entry"_s, e.map}});
                            w.stopPropagation();
                        })
                        .onMouseDown(at::MouseButton::Right,
                                     [onAct = act, e](const at::MouseDownEvent &m, at::Window &w, at::App &) {
                                         menuAt(onAct, u"statusMenu"_s, e, m.position);
                                         w.stopPropagation();
                                     })
                        .child(track(true, index)));
    if (e.isEvent()) {
        const QColor c(e.color);
        const at::Color color = c.isValid() ? at::Color{float(c.redF()), float(c.greenF()), float(c.blueF()), 1} : look.muted;
        return std::move(column).child(at::div().size(10).roundedFull().bg(color));
    }
    const bool marked = e.variant == u"alarm" || e.variant == u"reminder";
    return std::move(column).child(
            editor::atr::iconElement(e.icon.isEmpty() ? u"calendar"_s : e.icon, 16, marked ? look.reminder : look.muted));
}

// The entry's title (two lines at most; done or past: muted, cancelled: struck out) and where it is.
at::AnyElement EntryRows::texts(const AgendaEntry &e, bool past) const
{
    at::Div title = at::div()
                            .textSize(look.textSize)
                            .lineHeight(look.lineHeight)
                            .textColor(e.closed || past ? look.muted : look.text)
                            .when(e.status == u"cancelled", [](at::Div &&t) { return std::move(t).lineThrough(); })
                            .child(at::text(u16View(e.title)).maxLines(2));
    at::Div column = at::div().flex1().minW0().flexCol().gap(1).child(std::move(title));
    const QString where = subtitle(e);
    if (!where.isEmpty())
        std::move(column).child(at::div()
                                        .textSize(look.smallSize)
                                        .textColor(look.muted)
                                        .child(at::text(u16View(where)).maxLines(1)));
    return std::move(column);
}

// The kind of date as a small chip in its variant's colours (a recurring one's with its icon).
at::AnyElement EntryRows::chip(const AgendaEntry &e) const
{
    const AgendaLook::ChipColors colors = look.chip(e.variant);
    at::Div c = at::div()
                        .flexShrink0()
                        .mt(2)
                        .flexRow()
                        .itemsCenter()
                        .gap(4)
                        .px(8)
                        .py(2)
                        .roundedFull()
                        .bg(colors.background)
                        .textSize(look.chipSize)
                        .fontMedium()
                        .textColor(colors.text)
                        .ariaHidden();
    if (e.recurring)
        std::move(c).child(editor::atr::iconElement(u"repeat"_s, 12, colors.text));
    return std::move(c).child(at::text(u16View(e.kindLabel())));
}

} // namespace brulion::views::atr
