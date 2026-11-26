#include "ui/element.h"

#include "ui/window.h"

namespace brulion::atrament {

void Element::prepaint(Rect, Window &, App &)
{
}

namespace {

class EmptyElement final : public Element {
public:
    NodeId requestLayout(Window &window, App &) override
    {
        LayoutStyle style;
        style.display = Display::None;
        return window.requestLayout(style, {});
    }
    void paint(Rect, Window &, App &) override { }
};

} // namespace

AnyElement empty()
{
    return EmptyElement();
}

ElementId AnyElement::pathId() const
{
    const ElementId own = m_element->id();
    return own.isValid() ? own : ElementId::sibling(m_siblingIndex);
}

NodeId AnyElement::requestLayout(Window &window, App &app)
{
    if (!m_element)
        return m_layout = window.requestLayout(LayoutStyle {}, {});
    ElementIdScope scope(window, pathId());
    m_layout = m_element->requestLayout(window, app);
    return m_layout;
}

void AnyElement::prepaint(Window &window, App &app)
{
    if (!m_element)
        return;
    ElementIdScope scope(window, pathId());
    m_bounds = window.layoutBounds(m_layout);
    m_element->prepaint(m_bounds, window, app);
}

void AnyElement::paint(Window &window, App &app)
{
    if (!m_element)
        return;
    ElementIdScope scope(window, pathId());
    m_element->paint(m_bounds, window, app);
}

Size AnyElement::layoutAsRoot(Size available, Window &window, App &app)
{
    return layoutAsRoot(AvailableSpace::definite(available.width), AvailableSpace::definite(available.height), window,
            app);
}

Size AnyElement::layoutAsRoot(AvailableSpace width, AvailableSpace height, Window &window, App &app)
{
    requestLayout(window, app);
    window.layoutEngine().compute(m_layout, width, height);
    return window.layoutEngine().layout(m_layout).rect.size;
}

void AnyElement::prepaintAt(Point origin, Window &window, App &app)
{
    if (!m_element)
        return;
    window.pushElementOffset(origin);
    prepaint(window, app);
    window.popElementOffset();
}

} // namespace brulion::atrament
