#pragma once

// The accessibility bridge of the Qt host (docs/ATRAMENT.md D13): Atrament's semantic tree
// (ui/accessibility.h) as QAccessibleInterfaces under the AtramentItem, so that Qt's own bridges —
// AT-SPI on Linux, UI Automation on Windows, TalkBack on Android — read Atrament's views like
// any other Qt Quick content.
//
//   AtramentItem's interface   (a factory installed once) the item, its children the root
//                              node's; it turns the window's tree on when first asked (an
//                              assistive technology is there) and when Qt says one is active
//   a node's interface          object-less, one per node an assistive technology asked for,
//                              known to Qt by a QAccessible::Id that stays while the node does;
//                              text, editable text, actions, value and viewport where they fit
//
// Every frame's tree changes arrive as Atrament's events and leave as QAccessibleEvents, only
// for what an assistive technology has seen (the nodes with interfaces) and the focus.

#include "ui/accessibility.h"

#include <QtGui/qtguiglobal.h>

#include <QAccessible>
#include <QPointer>

#include <unordered_map>
#include <vector>

namespace brulion::atrament {

class AtramentItem;
class Window;

class AtramentAccessibility
#if QT_CONFIG(accessibility)
    : public QAccessible::ActivationObserver
#endif
{
public:
    explicit AtramentAccessibility(AtramentItem *item);
    ~AtramentAccessibility();
    AtramentAccessibility(const AtramentAccessibility &) = delete;
    AtramentAccessibility &operator=(const AtramentAccessibility &) = delete;

    // The item's Atrament window is there (or gone: nullptr).
    void setWindow(Window *window);
    // A frame changed the tree (PlatformWindow::accessibilityChanged).
    void treeChanged(const std::vector<AccessibilityEvent> &events);

#if QT_CONFIG(accessibility)
    void accessibilityActiveChanged(bool active) override;

    // --- for the interfaces
    AtramentItem *item() const { return m_item; }
    Window *window() const { return m_window; }
    // The tree on screen; the first question turns it on and draws it.
    const AccessibilityTree *tree();
    // A node's interface (made when first asked for); the root node's is the item's.
    QAccessibleInterface *interfaceFor(AccessNodeId id);
    // Draws the frame an action asked for now, so the next question sees its result.
    void flush();
    QRect toScreen(Rect r) const;
    Point fromScreen(QPoint p) const;
#endif

private:
    AtramentItem *m_item;
    Window *m_window = nullptr;
#if QT_CONFIG(accessibility)
    QAccessibleInterface *existingInterface(AccessNodeId id) const;
    // Detaches the interface (it keeps answering for its last place) and deletes it once the
    // event loop runs again; `notify`: tells Qt it went.
    void releaseLater(AccessNodeId id, AccessNodeId parent, bool notify);
    void releaseAll();

    std::unordered_map<AccessNodeId, QAccessible::Id> m_interfaces;
    std::vector<QAccessible::Id> m_released; // deleted when the event loop runs again
    bool m_drawing = false;
#endif
};

} // namespace brulion::atrament
