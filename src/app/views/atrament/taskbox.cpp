#include "taskbox.h"

#include "pageicons.h"

namespace brulion::views::atr {

namespace at = brulion::atrament;
using namespace Qt::StringLiterals;

at::Div taskBoxShape(const QString &status, const TaskBoxInk &ink)
{
    const bool closed = status == u"done" || status == u"cancelled";
    const at::Color border = closed ? ink.closed : ink.open;
    const QString icon = status == u"cancelled" ? u"x"_s : u"check"_s;
    const at::Color mark = ink.mark;
    return at::div()
            .relative()
            .size(kTaskBoxSize)
            .flexShrink0()
            .rounded(5)
            .border(1.5f)
            .borderColor(border)
            .bg(closed ? border : at::transparent())
            .cursorPointer()
            .role(at::Role::CheckBox)
            .checked(status == u"done")
            .when(closed, [icon, mark](at::Div &&box) {
                return std::move(box).child(at::canvas([icon, mark](at::Rect b, at::Window &w, at::App &) {
                    if (const auto i = editor::atr::pageIcon(icon))
                        w.paintIcon(*i, b.inset(at::Edges::all(2)), mark);
                }).absolute().inset(0));
            });
}

} // namespace brulion::views::atr
