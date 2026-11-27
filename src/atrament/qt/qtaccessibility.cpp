#include "qtaccessibility.h"

#include "qthost.h"
#include "text/textlayout.h"
#include "ui/window.h"

#if QT_CONFIG(accessibility)

#include <QAccessibleObject>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTextBoundaryFinder>
// The item is accessible only when Qt Quick knows it is (as if it had QML's Accessible
// properties): its window lists only such items as children. No public API sets it.
#include <QtQuick/private/qquickaccessibleattached_p.h>
#include <QtQuick/private/qquickitem_p.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace brulion::atrament {

namespace {

QString toQString(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

std::u16string toU16(const QString &s)
{
    return std::u16string(reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size()));
}

QAccessible::Role qtRole(Role role)
{
    switch (role) {
    case Role::None: return QAccessible::NoRole;
    case Role::Window: return QAccessible::Client;
    case Role::Group: return QAccessible::Grouping;
    case Role::Button: return QAccessible::Button;
    case Role::CheckBox: return QAccessible::CheckBox;
    case Role::RadioButton: return QAccessible::RadioButton;
    case Role::Switch: return QAccessible::Switch;
    case Role::Link: return QAccessible::Link;
    case Role::Text: return QAccessible::StaticText;
    case Role::Heading: return QAccessible::Heading;
    case Role::TextInput: return QAccessible::EditableText;
    case Role::List: return QAccessible::List;
    case Role::ListItem: return QAccessible::ListItem;
    case Role::Image: return QAccessible::Graphic;
    case Role::ScrollView: return QAccessible::Pane; // Android: a ViewGroup, scrollable by its actions
    case Role::TabList: return QAccessible::PageTabList;
    case Role::Tab: return QAccessible::PageTab;
    case Role::MenuItem: return QAccessible::MenuItem;
    case Role::Separator: return QAccessible::Separator;
    case Role::ProgressBar: return QAccessible::ProgressBar;
    case Role::Slider: return QAccessible::Slider;
    case Role::Dialog: return QAccessible::Dialog;
    case Role::Table: return QAccessible::Table;
    case Role::Row: return QAccessible::Row;
    case Role::Cell: return QAccessible::Cell;
    case Role::ColumnHeader: return QAccessible::ColumnHeader;
    }
    return QAccessible::NoRole;
}

std::optional<bool> &selectionAsCheckedOverride()
{
    static std::optional<bool> on;
    return on;
}

// A tab's selection is its checked state where the platform's tree has no selected state.
bool selectionShownAsChecked(const AccessibilityNode &n)
{
    return n.role == Role::Tab && n.states.selectable && AtramentAccessibility::selectionAsChecked();
}

QAccessible::State qtState(const AccessibilityNode &n)
{
    const AccessibilityStates &s = n.states;
    QAccessible::State q;
    q.focusable = s.focusable;
    q.focused = s.focused;
    q.selectable = s.selectable;
    q.selected = s.selected;
    q.checkable = s.checkable;
    q.checked = s.checked;
    q.checkStateMixed = s.mixed;
    q.disabled = s.disabled;
    q.expandable = s.expandable;
    q.expanded = s.expandable && s.expanded;
    q.collapsed = s.expandable && !s.expanded;
    q.editable = s.editable;
    q.multiLine = s.multiline;
    q.passwordEdit = s.password;
    q.offscreen = s.offscreen;
    q.readOnly = n.role == Role::TextInput && !s.editable;
    q.selectableText = n.role == Role::TextInput || n.role == Role::Text;
    if (selectionShownAsChecked(n)) {
        q.checkable = true;
        q.checked = s.selected;
    }
    return q;
}

bool has(const AccessibilityNode &n, AccessibilityAction a)
{
    return (n.actions & actionBit(a)) != 0;
}

std::u16string_view textOf(const AccessibilityNode &n)
{
    return n.role == Role::TextInput ? n.value : n.name;
}

// Something around the node can scroll it into view (its content is larger than it).
bool insideScrolling(const AccessibilityTree &tree, const AccessibilityNode &n)
{
    for (const AccessibilityNode *s = tree.node(n.scrollAncestor); s; s = tree.node(s->scrollAncestor)) {
        if (has(*s, AccessibilityAction::ScrollForward) || has(*s, AccessibilityAction::ScrollBackward))
            return true;
    }
    return false;
}

// One step: the states that changed, each in an event of its own (AT-SPI reads one per event).
std::vector<QAccessible::State> splitStates(const AccessibilityStates &c)
{
    std::vector<QAccessible::State> out;
    auto add = [&](bool on, auto set) {
        if (!on)
            return;
        QAccessible::State s;
        set(s);
        out.push_back(s);
    };
    add(c.checked || c.checkable, [](QAccessible::State &s) { s.checked = true; });
    add(c.mixed, [](QAccessible::State &s) { s.checkStateMixed = true; });
    add(c.disabled, [](QAccessible::State &s) { s.disabled = true; });
    add(c.expanded || c.expandable, [](QAccessible::State &s) {
        s.expanded = true;
        s.collapsed = true;
    });
    add(c.selected, [](QAccessible::State &s) { s.selected = true; });
    add(c.offscreen, [](QAccessible::State &s) { s.offscreen = true; });
    add(c.editable, [](QAccessible::State &s) { s.editable = true; });
    add(c.focusable, [](QAccessible::State &s) { s.focusable = true; });
    return out;
}

// The part of `before` that `after` replaced: a common prefix and suffix around it.
struct TextChange {
    int position = 0;
    QString removed;
    QString inserted;
};

TextChange textChange(std::u16string_view before, std::u16string_view after)
{
    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix])
        ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    return {int(prefix), toQString(before.substr(prefix, before.size() - prefix - suffix)),
            toQString(after.substr(prefix, after.size() - prefix - suffix))};
}

// The item's hosted children (qthost.h "Hosted children") as Qt Quick's accessibility sees them:
// the accessible items among them, else the accessible items inside (Qt's unignored children).
void hostedAccessibleItems(QQuickItem *item, QList<QQuickItem *> &out)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *c : children) {
        if (!c->isVisible())
            continue;
        if (QQuickItemPrivate::get(c)->isAccessible)
            out.append(c);
        else
            hostedAccessibleItems(c, out);
    }
}

// --- the item's interface

class ItemInterface final : public QAccessibleObject {
public:
    ItemInterface(AtramentItem *item, AtramentAccessibility *bridge)
        : QAccessibleObject(item), m_item(item), m_bridge(bridge)
    { }

    bool isValid() const override { return QAccessibleObject::isValid() && m_item && m_bridge; }
    QWindow *window() const override { return m_item ? m_item->window() : nullptr; }

    QAccessibleInterface *parent() const override
    {
        // The nearest accessible item around it, else its window (as Qt Quick's items do).
        if (!m_item)
            return nullptr;
        QQuickWindow *w = m_item->window();
        QQuickItem *content = w ? w->contentItem() : nullptr;
        for (QQuickItem *p = m_item->parentItem(); p && p != content; p = p->parentItem()) {
            if (QQuickItemPrivate::get(p)->isAccessible && p->parentItem())
                return QAccessible::queryAccessibleInterface(p);
        }
        return w ? QAccessible::queryAccessibleInterface(w) : nullptr;
    }

    const AccessibilityNode *rootNode() const
    {
        const AccessibilityTree *t = m_bridge ? m_bridge->tree() : nullptr;
        return t ? t->root() : nullptr;
    }

    // The window's nodes, then the hosted children's own (their rows are left out of the tree).
    int nodeCount() const
    {
        const AccessibilityNode *r = rootNode();
        return r ? r->childCount : 0;
    }
    QList<QQuickItem *> hostedItems() const
    {
        QList<QQuickItem *> items;
        if (m_item)
            hostedAccessibleItems(m_item, items);
        return items;
    }

    int childCount() const override { return nodeCount() + int(hostedItems().size()); }

    QAccessibleInterface *child(int index) const override
    {
        const AccessibilityNode *r = rootNode();
        const AccessibilityTree *t = m_bridge ? m_bridge->tree() : nullptr;
        if (index >= nodeCount()) {
            const QList<QQuickItem *> items = hostedItems();
            const int k = index - nodeCount();
            return k >= 0 && k < items.size() ? QAccessible::queryAccessibleInterface(items.at(k)) : nullptr;
        }
        const AccessibilityNode *c = r && t ? t->child(*r, index) : nullptr;
        return c ? m_bridge->interfaceFor(c->id) : nullptr;
    }

    int indexOfChild(const QAccessibleInterface *child) const override;
    QAccessibleInterface *childAt(int x, int y) const override;

    QAccessibleInterface *focusChild() const override
    {
        const AccessibilityTree *t = m_bridge ? m_bridge->tree() : nullptr;
        const AccessibilityNode *f = t ? t->focused() : nullptr;
        return f ? m_bridge->interfaceFor(f->id) : nullptr;
    }

    QString text(QAccessible::Text t) const override
    {
        // QML's Accessible.name / .description on the item, as on any Qt Quick item.
        QQuickAccessibleAttached *attached = m_item ? QQuickAccessibleAttached::attachedProperties(m_item) : nullptr;
        if (attached && t == QAccessible::Name)
            return attached->name();
        if (attached && t == QAccessible::Description)
            return attached->description();
        return {};
    }

    QRect rect() const override
    {
        if (!m_item || !m_item->window())
            return {};
        const QPointF origin = m_item->mapToGlobal(QPointF(0, 0));
        return QRectF(origin, QSizeF(m_item->width(), m_item->height())).toAlignedRect();
    }

    QAccessible::Role role() const override
    {
        QQuickAccessibleAttached *attached = m_item ? QQuickAccessibleAttached::attachedProperties(m_item) : nullptr;
        return attached && attached->role() != QAccessible::NoRole ? attached->role() : QAccessible::Client;
    }

    QAccessible::State state() const override
    {
        QAccessible::State s;
        if (!m_item)
            return s;
        s.focusable = true;
        const AccessibilityTree *t = m_bridge ? m_bridge->tree() : nullptr;
        s.focused = m_item->hasActiveFocus() && !(t && t->focused());
        s.invisible = !m_item->isVisible();
        return s;
    }

private:
    QPointer<AtramentItem> m_item;
    AtramentAccessibility *m_bridge;
};

// --- a node's interface

// A table (Role::Table): its Row children are its rows, their Cell and ColumnHeader children its
// cells, in order — what Atrament's elements describe for a grid; other children (a filter's
// fields) are no rows. Qt's table and cell interfaces count and find them.
bool isCellRole(Role r)
{
    return r == Role::Cell || r == Role::ColumnHeader;
}

std::vector<const AccessibilityNode *> childrenOf(const AccessibilityTree &tree, const AccessibilityNode &n, bool cells)
{
    std::vector<const AccessibilityNode *> out;
    for (const int i : tree.childIndexes(n)) {
        const AccessibilityNode *c = tree.node(i);
        if (c && (cells ? isCellRole(c->role) : c->role == Role::Row))
            out.push_back(c);
    }
    return out;
}

class NodeInterface final : public QAccessibleInterface,
                            public QAccessibleActionInterface,
                            public QAccessibleTextInterface,
                            public QAccessibleEditableTextInterface,
                            public QAccessibleValueInterface,
                            public QAccessibleViewportInterface,
                            public QAccessibleTableInterface,
                            public QAccessibleTableCellInterface {
public:
    NodeInterface(AtramentAccessibility *bridge, AccessNodeId id) : m_bridge(bridge), m_id(id) { }

    AccessNodeId nodeId() const { return m_id; }
    // The node is gone: the interface answers for its last place (parent) until Qt lets it go.
    void detach(AccessNodeId parent)
    {
        m_detached = true;
        if (parent)
            m_parent = parent;
    }

    const AccessibilityNode *node() const
    {
        if (m_detached || !m_bridge)
            return nullptr;
        const AccessibilityTree *t = m_bridge->tree();
        const AccessibilityNode *n = t ? t->find(m_id) : nullptr;
        if (n) {
            const AccessibilityNode *p = t->parentOf(*n);
            m_parent = p ? p->id : 0;
        }
        return n;
    }

    // QAccessibleInterface
    bool isValid() const override { return m_detached || node(); }
    QObject *object() const override { return nullptr; }
    QWindow *window() const override { return m_bridge && m_bridge->item() ? m_bridge->item()->window() : nullptr; }

    QAccessibleInterface *parent() const override
    {
        node(); // refreshes the parent's id
        QAccessibleInterface *p = m_parent ? m_bridge->interfaceFor(m_parent) : nullptr;
        return p ? p : QAccessible::queryAccessibleInterface(m_bridge->item());
    }

    QAccessibleInterface *child(int index) const override
    {
        const AccessibilityNode *n = node();
        const AccessibilityNode *c = n ? m_bridge->tree()->child(*n, index) : nullptr;
        return c ? m_bridge->interfaceFor(c->id) : nullptr;
    }

    int childCount() const override
    {
        const AccessibilityNode *n = node();
        return n ? n->childCount : 0;
    }

    int indexOfChild(const QAccessibleInterface *child) const override
    {
        const auto *c = dynamic_cast<const NodeInterface *>(child);
        const AccessibilityNode *n = node();
        const AccessibilityNode *cn = c ? c->node() : nullptr;
        return n && cn && cn->parent == n->treeIndex ? cn->indexInParent : -1;
    }

    QAccessibleInterface *childAt(int x, int y) const override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return nullptr;
        const AccessibilityTree *t = m_bridge->tree();
        const Point p = m_bridge->fromScreen({x, y});
        const std::span<const int> children = t->childIndexes(*n);
        for (std::size_t i = children.size(); i-- > 0;) {
            const AccessibilityNode *c = t->node(children[i]);
            if (c->bounds.contains(p) && !c->states.offscreen)
                return m_bridge->interfaceFor(c->id);
        }
        return nullptr;
    }

    QString text(QAccessible::Text t) const override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return t == QAccessible::Name ? m_lastName : QString();
        switch (t) {
        case QAccessible::Name:
            m_lastName = toQString(n->name); // shared, not copied: what a removed node says
            return m_lastName;
        case QAccessible::Description: return toQString(n->description);
        case QAccessible::Value:
            if (!n->value.empty())
                return toQString(n->value);
            return n->hasRange ? QString::number(double(n->range.value)) : QString();
        default: return {};
        }
    }

    void setText(QAccessible::Text t, const QString &text) override
    {
        const AccessibilityNode *n = node();
        if (n && t == QAccessible::Value && has(*n, AccessibilityAction::SetValue)) {
            const std::u16string s = toU16(text);
            perform({AccessibilityAction::SetValue, s, {}, 0, {}});
        }
    }

    QRect rect() const override
    {
        const AccessibilityNode *n = node();
        return n ? m_bridge->toScreen(n->bounds) : QRect();
    }

    QAccessible::Role role() const override
    {
        if (const AccessibilityNode *n = node())
            m_lastRole = qtRole(n->role);
        return m_lastRole;
    }

    QAccessible::State state() const override
    {
        const AccessibilityNode *n = node();
        QAccessible::State s;
        if (n)
            s = qtState(*n);
        else
            s.invalid = true;
        return s;
    }

    void *interface_cast(QAccessible::InterfaceType type) override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return nullptr;
        switch (type) {
        case QAccessible::ActionInterface:
            return actionNames().isEmpty() ? nullptr : static_cast<QAccessibleActionInterface *>(this);
        case QAccessible::TextInterface:
            return n->role == Role::TextInput || n->role == Role::Text ? static_cast<QAccessibleTextInterface *>(this)
                                                                       : nullptr;
        case QAccessible::EditableTextInterface:
            return n->role == Role::TextInput && n->states.editable
                           ? static_cast<QAccessibleEditableTextInterface *>(this)
                           : nullptr;
        case QAccessible::ValueInterface: return n->hasRange ? static_cast<QAccessibleValueInterface *>(this) : nullptr;
        case QAccessible::ViewportInterface:
            return n->scrolls ? static_cast<QAccessibleViewportInterface *>(this) : nullptr;
        case QAccessible::TableInterface:
            return n->role == Role::Table ? static_cast<QAccessibleTableInterface *>(this) : nullptr;
        case QAccessible::TableCellInterface:
            return isCellRole(n->role) && tableOf(*n) ? static_cast<QAccessibleTableCellInterface *>(this) : nullptr;
        default: return nullptr;
        }
    }

    // --- a table's rows and cells (see childrenOf above)
    std::vector<const AccessibilityNode *> rows() const
    {
        const AccessibilityNode *n = node();
        return n ? childrenOf(*m_bridge->tree(), *n, false) : std::vector<const AccessibilityNode *>{};
    }
    const AccessibilityNode *cellNode(int row, int column) const
    {
        const std::vector<const AccessibilityNode *> r = rows();
        if (row < 0 || std::size_t(row) >= r.size())
            return nullptr;
        const std::vector<const AccessibilityNode *> cells = childrenOf(*m_bridge->tree(), *r[std::size_t(row)], true);
        return column >= 0 && std::size_t(column) < cells.size() ? cells[std::size_t(column)] : nullptr;
    }
    // A cell's row and table (nullptr: not in one).
    const AccessibilityNode *rowOf(const AccessibilityNode &cell) const
    {
        const AccessibilityNode *r = m_bridge->tree()->parentOf(cell);
        return r && r->role == Role::Row ? r : nullptr;
    }
    const AccessibilityNode *tableOf(const AccessibilityNode &cell) const
    {
        const AccessibilityNode *r = rowOf(cell);
        const AccessibilityNode *t = r ? m_bridge->tree()->parentOf(*r) : nullptr;
        return t && t->role == Role::Table ? t : nullptr;
    }
    QList<QAccessibleInterface *> interfacesOf(const std::vector<const AccessibilityNode *> &nodes) const
    {
        QList<QAccessibleInterface *> out;
        for (const AccessibilityNode *c : nodes)
            if (QAccessibleInterface *i = m_bridge->interfaceFor(c->id))
                out.append(i);
        return out;
    }
    std::vector<const AccessibilityNode *> selectedCellNodes() const
    {
        std::vector<const AccessibilityNode *> out;
        for (const AccessibilityNode *r : rows())
            for (const AccessibilityNode *c : childrenOf(*m_bridge->tree(), *r, true))
                if (c->states.selected)
                    out.push_back(c);
        return out;
    }

    // QAccessibleTableInterface
    QAccessibleInterface *caption() const override { return nullptr; }
    QAccessibleInterface *summary() const override { return nullptr; }
    QAccessibleInterface *cellAt(int row, int column) const override
    {
        const AccessibilityNode *c = cellNode(row, column);
        return c ? m_bridge->interfaceFor(c->id) : nullptr;
    }
    int selectedCellCount() const override { return int(selectedCellNodes().size()); }
    QList<QAccessibleInterface *> selectedCells() const override { return interfacesOf(selectedCellNodes()); }
    QString columnDescription(int column) const override
    {
        // The column's header (the first row's, when it is one).
        const AccessibilityNode *c = cellNode(0, column);
        return c && c->role == Role::ColumnHeader ? toQString(c->name) : QString();
    }
    QString rowDescription(int) const override { return {}; }
    int selectedColumnCount() const override { return int(selectedColumns().size()); }
    int selectedRowCount() const override { return int(selectedRows().size()); }
    int columnCount() const override
    {
        std::size_t most = 0;
        for (const AccessibilityNode *r : rows())
            most = std::max(most, childrenOf(*m_bridge->tree(), *r, true).size());
        return int(most);
    }
    int rowCount() const override { return int(rows().size()); }
    QList<int> selectedColumns() const override
    {
        QList<int> out;
        for (int c = 0; c < columnCount(); ++c)
            if (isColumnSelected(c))
                out.append(c);
        return out;
    }
    QList<int> selectedRows() const override
    {
        QList<int> out;
        for (int r = 0; r < rowCount(); ++r)
            if (isRowSelected(r))
                out.append(r);
        return out;
    }
    bool isColumnSelected(int column) const override
    {
        const int n = rowCount();
        for (int r = 0; r < n; ++r) {
            const AccessibilityNode *c = cellNode(r, column);
            if (!c || !c->states.selected)
                return false;
        }
        return n > 0;
    }
    bool isRowSelected(int row) const override
    {
        const std::vector<const AccessibilityNode *> r = rows();
        if (row < 0 || std::size_t(row) >= r.size())
            return false;
        const std::vector<const AccessibilityNode *> cells = childrenOf(*m_bridge->tree(), *r[std::size_t(row)], true);
        return !cells.empty() && std::all_of(cells.begin(), cells.end(), [](const AccessibilityNode *c) { return c->states.selected; });
    }
    // (The view selects its cells with its own keys.)
    bool selectRow(int) override { return false; }
    bool selectColumn(int) override { return false; }
    bool unselectRow(int) override { return false; }
    bool unselectColumn(int) override { return false; }
    void modelChange(QAccessibleTableModelChangeEvent *) override { }

    // QAccessibleTableCellInterface
    bool isSelected() const override
    {
        const AccessibilityNode *n = node();
        return n && n->states.selected;
    }
    QList<QAccessibleInterface *> columnHeaderCells() const override
    {
        // The column's header cell above it (none for a header itself).
        const AccessibilityNode *n = node();
        const AccessibilityNode *t = n ? tableOf(*n) : nullptr;
        if (!t || n->role == Role::ColumnHeader)
            return {};
        const int column = columnIndex();
        for (const AccessibilityNode *r : childrenOf(*m_bridge->tree(), *t, false)) {
            const std::vector<const AccessibilityNode *> cells = childrenOf(*m_bridge->tree(), *r, true);
            if (column >= 0 && std::size_t(column) < cells.size() && cells[std::size_t(column)]->role == Role::ColumnHeader)
                return interfacesOf({cells[std::size_t(column)]});
        }
        return {};
    }
    QList<QAccessibleInterface *> rowHeaderCells() const override { return {}; }
    int columnIndex() const override
    {
        const AccessibilityNode *n = node();
        const AccessibilityNode *r = n ? rowOf(*n) : nullptr;
        if (!r)
            return -1;
        const std::vector<const AccessibilityNode *> cells = childrenOf(*m_bridge->tree(), *r, true);
        const auto it = std::find(cells.begin(), cells.end(), n);
        return it == cells.end() ? -1 : int(it - cells.begin());
    }
    int rowIndex() const override
    {
        const AccessibilityNode *n = node();
        const AccessibilityNode *t = n ? tableOf(*n) : nullptr;
        if (!t)
            return -1;
        const std::vector<const AccessibilityNode *> r = childrenOf(*m_bridge->tree(), *t, false);
        const auto it = std::find(r.begin(), r.end(), rowOf(*n));
        return it == r.end() ? -1 : int(it - r.begin());
    }
    int columnExtent() const override { return 1; }
    int rowExtent() const override { return 1; }
    QAccessibleInterface *table() const override
    {
        const AccessibilityNode *n = node();
        const AccessibilityNode *t = n ? tableOf(*n) : nullptr;
        return t ? m_bridge->interfaceFor(t->id) : nullptr;
    }

    // QAccessibleActionInterface
    QStringList actionNames() const override
    {
        const AccessibilityNode *n = node();
        QStringList names;
        if (!n)
            return names;
        if (has(*n, AccessibilityAction::Click)) {
            if (n->states.checkable)
                names << toggleAction();
            names << pressAction();
        }
        if (has(*n, AccessibilityAction::Focus))
            names << setFocusAction();
        // Android scrolls forward and backward with Increase / Decrease.
        if (has(*n, AccessibilityAction::Increment) || has(*n, AccessibilityAction::ScrollForward))
            names << increaseAction();
        if (has(*n, AccessibilityAction::Decrement) || has(*n, AccessibilityAction::ScrollBackward))
            names << decreaseAction();
        if (has(*n, AccessibilityAction::ScrollForward))
            names << (n->scrollY ? scrollDownAction() : scrollRightAction());
        if (has(*n, AccessibilityAction::ScrollBackward))
            names << (n->scrollY ? scrollUpAction() : scrollLeftAction());
        if (insideScrolling(*m_bridge->tree(), *n))
            names << showOnScreenAction();
        return names;
    }

    void doAction(const QString &name) override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return;
        AccessibilityAction a;
        if (name == pressAction() || name == toggleAction())
            a = AccessibilityAction::Click;
        else if (name == setFocusAction())
            a = AccessibilityAction::Focus;
        else if (name == increaseAction())
            a = has(*n, AccessibilityAction::Increment) ? AccessibilityAction::Increment
                                                        : AccessibilityAction::ScrollForward;
        else if (name == decreaseAction())
            a = has(*n, AccessibilityAction::Decrement) ? AccessibilityAction::Decrement
                                                        : AccessibilityAction::ScrollBackward;
        else if (name == scrollDownAction() || name == scrollRightAction())
            a = AccessibilityAction::ScrollForward;
        else if (name == scrollUpAction() || name == scrollLeftAction())
            a = AccessibilityAction::ScrollBackward;
        else if (name == showOnScreenAction())
            a = AccessibilityAction::ScrollIntoView;
        else
            return;
        perform({a, {}, {}, 0, {}});
    }

    QStringList keyBindingsForAction(const QString &) const override { return {}; }

    // QAccessibleTextInterface
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override
    {
        *startOffset = *endOffset = 0;
        const AccessibilityNode *n = node();
        if (n && selectionIndex == 0 && n->role == Role::TextInput) {
            const Range r = n->selection.range();
            *startOffset = r.start;
            *endOffset = r.end;
        }
    }

    int selectionCount() const override
    {
        const AccessibilityNode *n = node();
        return n && n->role == Role::TextInput && !n->selection.range().isEmpty() ? 1 : 0;
    }

    void addSelection(int startOffset, int endOffset) override { select(startOffset, endOffset); }
    void removeSelection(int) override
    {
        const AccessibilityNode *n = node();
        if (n)
            select(n->selection.focus, n->selection.focus);
    }
    void setSelection(int, int startOffset, int endOffset) override { select(startOffset, endOffset); }

    int cursorPosition() const override
    {
        const AccessibilityNode *n = node();
        return n && n->role == Role::TextInput ? n->selection.focus : 0;
    }

    void setCursorPosition(int position) override { select(position, position); }

    QString text(int startOffset, int endOffset) const override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return {};
        const std::u16string_view t = textOf(*n);
        const int a = std::clamp(startOffset, 0, int(t.size()));
        const int b = std::clamp(endOffset < 0 ? int(t.size()) : endOffset, a, int(t.size()));
        return toQString(t.substr(std::size_t(a), std::size_t(b - a)));
    }

    QString textBeforeOffset(int offset, QAccessible::TextBoundaryType type, int *startOffset,
                             int *endOffset) const override
    {
        const Range at = boundary(offset, type);
        // The unit that ends where the one at the offset starts.
        const Range before = at.start > 0 ? boundary(at.start - 1, type) : Range{0, 0};
        *startOffset = before.start;
        *endOffset = before.end;
        return text(before.start, before.end);
    }

    QString textAfterOffset(int offset, QAccessible::TextBoundaryType type, int *startOffset,
                            int *endOffset) const override
    {
        const Range at = boundary(offset, type);
        const Range after = at.end < characterCount() ? boundary(at.end, type) : Range{at.end, at.end};
        *startOffset = after.start;
        *endOffset = after.end;
        return text(after.start, after.end);
    }

    QString textAtOffset(int offset, QAccessible::TextBoundaryType type, int *startOffset,
                         int *endOffset) const override
    {
        const Range at = boundary(offset, type);
        *startOffset = at.start;
        *endOffset = at.end;
        return text(at.start, at.end);
    }

    int characterCount() const override
    {
        const AccessibilityNode *n = node();
        return n ? int(textOf(*n).size()) : 0;
    }

    QRect characterRect(int offset) const override
    {
        const AccessibilityNode *n = node();
        if (!n || !n->textLayout)
            return {};
        const int length = int(textOf(*n).size());
        Rect r;
        if (offset >= length) {
            r = n->textLayout->caretRect(length);
        } else {
            for (const Rect &s : n->textLayout->selectionRects({offset, offset + 1}))
                r = r.isEmpty() ? s : r.united(s);
        }
        return m_bridge->toScreen(r.translated(n->textOrigin));
    }

    int offsetAtPoint(const QPoint &point) const override
    {
        const AccessibilityNode *n = node();
        if (!n || !n->textLayout)
            return -1;
        const Point p = m_bridge->fromScreen(point);
        if (!n->bounds.contains(p))
            return -1;
        return n->textLayout->offsetAt(p - n->textOrigin);
    }

    void scrollToSubstring(int, int) override { perform({AccessibilityAction::ScrollIntoView, {}, {}, 0, {}}); }

    QString attributes(int offset, int *startOffset, int *endOffset) const override
    {
        *startOffset = *endOffset = offset;
        return {};
    }

    // QAccessibleEditableTextInterface
    void deleteText(int startOffset, int endOffset) override { replaceText(startOffset, endOffset, QString()); }
    void insertText(int offset, const QString &text) override { replaceText(offset, offset, text); }
    void replaceText(int startOffset, int endOffset, const QString &text) override
    {
        const std::u16string s = toU16(text);
        perform({AccessibilityAction::ReplaceText, s, {startOffset, endOffset}, 0, {}});
    }

    // QAccessibleValueInterface
    QVariant currentValue() const override
    {
        const AccessibilityNode *n = node();
        return n && n->hasRange ? QVariant(double(n->range.value)) : QVariant();
    }
    void setCurrentValue(const QVariant &value) override
    {
        const AccessibilityNode *n = node();
        if (n && n->hasRange)
            perform({AccessibilityAction::SetValue, {}, {}, value.toFloat(), {}});
    }
    QVariant maximumValue() const override
    {
        const AccessibilityNode *n = node();
        return n && n->hasRange ? QVariant(double(n->range.max)) : QVariant();
    }
    QVariant minimumValue() const override
    {
        const AccessibilityNode *n = node();
        return n && n->hasRange ? QVariant(double(n->range.min)) : QVariant();
    }
    QVariant minimumStepSize() const override
    {
        const AccessibilityNode *n = node();
        return n && n->hasRange && n->range.step > 0 ? QVariant(double(n->range.step)) : QVariant();
    }

    // QAccessibleViewportInterface: fractions of the content, as Qt Quick's Flickable and ListView
    // give them; a list counts rows.
    bool isIndexed() const override
    {
        const AccessibilityNode *n = node();
        return n && n->rowCount >= 0;
    }

    QSizeF contentSize() const override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return {};
        if (n->rowCount >= 0)
            return {1, double(n->rowCount)};
        return {double(n->scrollContent.width), double(n->scrollContent.height)};
    }

    QPointF position() const override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return {};
        if (n->rowCount > 0) {
            const Visible v = visibleRows(*n);
            return {0, double(v.first) / double(n->rowCount)};
        }
        const Size c = n->scrollContent;
        return {c.width > 0 ? double(n->scrollOffset.x / c.width) : 0.0,
                c.height > 0 ? double(n->scrollOffset.y / c.height) : 0.0};
    }

    QSizeF viewportSize() const override
    {
        const AccessibilityNode *n = node();
        if (!n)
            return {};
        if (n->rowCount > 0) {
            const Visible v = visibleRows(*n);
            return {1, double(v.count) / double(n->rowCount)};
        }
        const Size c = n->scrollContent;
        const Rect vp = n->scrollViewport;
        return {c.width > 0 ? std::min(1.0, double(vp.width() / c.width)) : 1.0,
                c.height > 0 ? std::min(1.0, double(vp.height() / c.height)) : 1.0};
    }

    void setPosition(QPointF position) override
    {
        const AccessibilityNode *n = node();
        if (!n || !n->scrolls)
            return;
        AccessibilityActionRequest r{AccessibilityAction::ScrollTo, {}, {}, 0, {}};
        r.offset = {float(position.x()) * n->scrollContent.width, float(position.y()) * n->scrollContent.height};
        perform(r);
    }

private:
    struct Visible {
        std::int64_t first = 0;
        std::int64_t count = 0;
    };
    Visible visibleRows(const AccessibilityNode &list) const
    {
        Visible v{-1, 0};
        const AccessibilityTree *t = m_bridge->tree();
        for (int i : t->childIndexes(list)) {
            const AccessibilityNode *c = t->node(i);
            if (c->rowIndex < 0 || c->states.offscreen)
                continue;
            if (v.first < 0)
                v.first = c->rowIndex;
            ++v.count;
        }
        if (v.first < 0)
            v.first = 0;
        return v;
    }

    void select(int start, int end) { perform({AccessibilityAction::SetSelection, {}, {start, end}, 0, {}}); }

    void perform(const AccessibilityActionRequest &request)
    {
        // The frame is drawn at once, so the next question sees the result (Android compares a
        // child's place right after scrolling). This interface may be let go by then: nothing of
        // it is used afterwards.
        AtramentAccessibility *bridge = m_bridge;
        Window *w = bridge->window();
        AtramentItem *item = bridge->item();
        // A screen reader moving its focus onto a field (TalkBack sets the input focus with it)
        // opens no keyboard; activating the field (a double tap: Click) does.
        const bool focusOnly = request.action == AccessibilityAction::Focus;
        if (item && focusOnly)
            item->setAccessibilityFocusing(true);
        const bool done = w && w->performAccessibilityAction(m_id, request);
        if (done)
            bridge->flush();
        if (item) {
            item->setAccessibilityFocusing(false);
            if (done && request.action == AccessibilityAction::Click)
                item->showInputPanel();
        }
    }

    // The unit of text around an offset.
    Range boundary(int offset, QAccessible::TextBoundaryType type) const
    {
        const AccessibilityNode *n = node();
        if (!n)
            return {};
        const std::u16string_view t = textOf(*n);
        const int length = int(t.size());
        const int at = std::clamp(offset, 0, length);
        if (type == QAccessible::NoBoundary || length == 0)
            return {0, length};
        if (type == QAccessible::LineBoundary && n->textLayout && !n->textLayout->lines().empty()) {
            const TextLine &line = n->textLayout->lines()[std::size_t(n->textLayout->lineIndex(at))];
            return {line.start, std::max(line.start, line.end)};
        }
        if (type == QAccessible::ParagraphBoundary || type == QAccessible::LineBoundary) {
            const std::size_t a = at > 0 ? t.rfind(u'\n', std::size_t(at - 1)) : std::u16string_view::npos;
            const std::size_t b = t.find(u'\n', std::size_t(at));
            return {a == std::u16string_view::npos ? 0 : int(a) + 1,
                    b == std::u16string_view::npos ? length : int(b) + 1};
        }
        const QString s = toQString(t);
        const QTextBoundaryFinder::BoundaryType bt = type == QAccessible::CharBoundary ? QTextBoundaryFinder::Grapheme
                                                     : type == QAccessible::WordBoundary
                                                             ? QTextBoundaryFinder::Word
                                                             : QTextBoundaryFinder::Sentence;
        QTextBoundaryFinder f(bt, s);
        f.setPosition(at);
        int start = f.isAtBoundary() ? at : int(f.toPreviousBoundary());
        f.setPosition(at);
        int end = int(f.toNextBoundary());
        if (start < 0)
            start = 0;
        if (end < 0)
            end = length;
        return {start, end};
    }

    AtramentAccessibility *m_bridge;
    AccessNodeId m_id;
    mutable AccessNodeId m_parent = 0;
    // What it last said, for the events after its node went.
    mutable QString m_lastName;
    mutable QAccessible::Role m_lastRole = QAccessible::NoRole;
    bool m_detached = false;
};

int ItemInterface::indexOfChild(const QAccessibleInterface *child) const
{
    const auto *c = dynamic_cast<const NodeInterface *>(child);
    const AccessibilityNode *n = c ? c->node() : nullptr;
    if (n)
        return n->parent == 0 ? n->indexInParent : -1;
    const qsizetype k = child ? hostedItems().indexOf(qobject_cast<QQuickItem *>(child->object())) : -1;
    return k >= 0 ? nodeCount() + int(k) : -1;
}

QAccessibleInterface *ItemInterface::childAt(int x, int y) const
{
    // A hosted child over the window first (it is drawn over it).
    const QList<QQuickItem *> items = hostedItems();
    for (qsizetype i = items.size(); i-- > 0;) {
        QAccessibleInterface *c = QAccessible::queryAccessibleInterface(items.at(i));
        if (c && !c->state().invisible && c->rect().contains(x, y))
            return c;
    }
    const AccessibilityTree *t = m_bridge ? m_bridge->tree() : nullptr;
    const AccessibilityNode *r = t ? t->root() : nullptr;
    if (!r)
        return nullptr;
    const Point p = m_bridge->fromScreen({x, y});
    const std::span<const int> children = t->childIndexes(*r);
    for (std::size_t i = children.size(); i-- > 0;) {
        const AccessibilityNode *c = t->node(children[i]);
        if (c->bounds.contains(p) && !c->states.offscreen)
            return m_bridge->interfaceFor(c->id);
    }
    return nullptr;
}

QAccessibleInterface *atramentFactory(const QString &, QObject *object)
{
    auto *item = qobject_cast<AtramentItem *>(object);
    AtramentAccessibility *bridge = item ? item->accessibility() : nullptr;
    return bridge ? new ItemInterface(item, bridge) : nullptr;
}

} // namespace

// --- the bridge

AtramentAccessibility::AtramentAccessibility(AtramentItem *item) : m_item(item)
{
    // Asked for the most derived class first, so this factory answers before Qt Quick's (which
    // matches QQuickItem by name).
    static const bool installed = [] {
        QAccessible::installFactory(atramentFactory);
        return true;
    }();
    (void)installed;
    QQuickItemPrivate::get(item)->setAccessible();
    QAccessible::installActivationObserver(this);
}

AtramentAccessibility::~AtramentAccessibility()
{
    QAccessible::removeActivationObserver(this);
    releaseAll();
}

void AtramentAccessibility::setWindow(Window *window)
{
    if (m_window == window)
        return;
    releaseAll();
    m_window = window;
    if (m_window && QAccessible::isActive())
        m_window->setAccessibilityEnabled(true);
}

bool AtramentAccessibility::selectionAsChecked()
{
    if (const std::optional<bool> on = selectionAsCheckedOverride())
        return *on;
    static const bool android = QGuiApplication::platformName() == QLatin1String("android");
    return android;
}

void AtramentAccessibility::setSelectionAsChecked(bool on)
{
    selectionAsCheckedOverride() = on;
}

void AtramentAccessibility::accessibilityActiveChanged(bool active)
{
    if (m_window)
        m_window->setAccessibilityEnabled(active);
    if (!active)
        releaseAll();
}

const AccessibilityTree *AtramentAccessibility::tree()
{
    if (!m_window)
        return nullptr;
    if (!m_window->accessibilityEnabled() && !m_drawing) {
        // Someone asks, so an assistive technology is there even if Qt has not said so (yet).
        m_window->setAccessibilityEnabled(true);
        flush();
    }
    return &m_window->accessibilityTree();
}

void AtramentAccessibility::flush()
{
    if (!m_window || m_drawing)
        return;
    m_drawing = true;
    m_window->draw();
    m_drawing = false;
}

QRect AtramentAccessibility::toScreen(Rect r) const
{
    if (!m_item || !m_item->window())
        return {};
    const QPointF origin = m_item->mapToGlobal(QPointF(r.left(), r.top()));
    return QRectF(origin, QSizeF(r.width(), r.height())).toAlignedRect();
}

Point AtramentAccessibility::fromScreen(QPoint p) const
{
    if (!m_item)
        return {};
    const QPointF local = m_item->mapFromGlobal(QPointF(p));
    return {float(local.x()), float(local.y())};
}

QAccessibleInterface *AtramentAccessibility::interfaceFor(AccessNodeId id)
{
    if (!m_window || id == 0)
        return nullptr;
    const AccessibilityNode *root = m_window->accessibilityTree().root();
    if (root && root->id == id)
        return QAccessible::queryAccessibleInterface(m_item);
    auto it = m_interfaces.find(id);
    if (it != m_interfaces.end()) {
        if (QAccessibleInterface *iface = QAccessible::accessibleInterface(it->second))
            return iface;
        m_interfaces.erase(it); // Qt let it go (its cache was cleaned up)
    }
    if (!m_window->accessibilityTree().find(id))
        return nullptr;
    auto *iface = new NodeInterface(this, id);
    m_interfaces.emplace(id, QAccessible::registerAccessibleInterface(iface));
    return iface;
}

QAccessibleInterface *AtramentAccessibility::existingInterface(AccessNodeId id) const
{
    auto it = m_interfaces.find(id);
    return it == m_interfaces.end() ? nullptr : QAccessible::accessibleInterface(it->second);
}

void AtramentAccessibility::releaseLater(AccessNodeId id, AccessNodeId parent, bool notify)
{
    auto it = m_interfaces.find(id);
    if (it == m_interfaces.end())
        return;
    const QAccessible::Id qid = it->second;
    m_interfaces.erase(it);
    auto *iface = static_cast<NodeInterface *>(QAccessible::accessibleInterface(qid));
    if (!iface)
        return;
    iface->detach(parent);
    if (notify) {
        QAccessibleEvent hide(iface, QAccessible::ObjectHide);
        QAccessible::updateAccessibility(&hide);
        QAccessibleEvent destroyed(iface, QAccessible::ObjectDestroyed);
        QAccessible::updateAccessibility(&destroyed);
    }
    // Not now: the caller of an action that removed it may still hold it.
    m_released.push_back(qid);
    if (m_released.size() == 1) {
        // The item is the context: the call goes with it (the bridge goes first, releasing all).
        QMetaObject::invokeMethod(
                m_item,
                [this] {
                    for (QAccessible::Id id : std::exchange(m_released, {}))
                        QAccessible::deleteAccessibleInterface(id);
                },
                Qt::QueuedConnection);
    }
}

void AtramentAccessibility::releaseAll()
{
    for (const auto &[id, qid] : m_interfaces) {
        if (auto *iface = static_cast<NodeInterface *>(QAccessible::accessibleInterface(qid))) {
            iface->detach(0);
            m_released.push_back(qid);
        }
    }
    m_interfaces.clear();
    for (QAccessible::Id id : std::exchange(m_released, {}))
        QAccessible::deleteAccessibleInterface(id);
}

void AtramentAccessibility::treeChanged(const std::vector<AccessibilityEvent> &events)
{
    if (!m_window)
        return;
    const AccessibilityTree &tree = m_window->accessibilityTree();
    const AccessibilityNode *root = tree.root();
    auto known = [&](AccessNodeId id) { return (root && root->id == id) || m_interfaces.count(id) > 0; };
    auto send = [](QAccessibleEvent &&e) { QAccessible::updateAccessibility(&e); };
    using Type = AccessibilityEvent::Type;

    // Parents whose children changed by additions or removals already said so.
    std::vector<AccessNodeId> told;
    int added = 0;
    for (const AccessibilityEvent &e : events) {
        switch (e.type) {
        case Type::TreeReset: send(QAccessibleEvent(m_item, QAccessible::ObjectReorder)); break;
        case Type::Focus:
            if (!m_item->hasActiveFocus())
                break;
            if (QAccessibleInterface *iface = e.node ? interfaceFor(e.node) : nullptr)
                send(QAccessibleEvent(iface, QAccessible::Focus));
            else
                send(QAccessibleEvent(m_item, QAccessible::Focus));
            break;
        case Type::Added:
            // Only what an assistive technology could have seen around it.
            if (!known(e.parent))
                break;
            told.push_back(e.parent);
            if (++added > 64)
                break; // a whole new view: the reorder below covers it
            if (QAccessibleInterface *iface = interfaceFor(e.node)) {
                send(QAccessibleEvent(iface, QAccessible::ObjectCreated));
                send(QAccessibleEvent(iface, QAccessible::ObjectShow));
            }
            break;
        case Type::Removed:
            if (known(e.node)) {
                told.push_back(e.parent);
                releaseLater(e.node, e.parent, true);
            }
            break;
        default: break;
        }
    }
    for (const AccessibilityEvent &e : events) {
        QAccessibleInterface *iface = existingInterface(e.node);
        if (!iface && root && e.node == root->id && e.type == Type::ChildrenChanged)
            iface = QAccessible::queryAccessibleInterface(m_item);
        if (!iface)
            continue;
        const AccessibilityNode *n = tree.find(e.node);
        switch (e.type) {
        case Type::NameChanged: send(QAccessibleEvent(iface, QAccessible::NameChanged)); break;
        case Type::DescriptionChanged: send(QAccessibleEvent(iface, QAccessible::DescriptionChanged)); break;
        case Type::RoleChanged: send(QAccessibleEvent(iface, QAccessible::RoleChanged)); break;
        case Type::ValueChanged:
            if (n && n->role == Role::TextInput) {
                const TextChange c = textChange(e.oldText, n->value);
                if (!c.removed.isEmpty() || !c.inserted.isEmpty())
                    send(QAccessibleTextUpdateEvent(iface, c.position, c.removed, c.inserted));
            } else if (n && n->hasRange) {
                send(QAccessibleValueChangeEvent(iface, double(n->range.value)));
            } else if (n) {
                send(QAccessibleValueChangeEvent(iface, toQString(n->value)));
            }
            break;
        case Type::SelectionChanged:
            if (n) {
                send(QAccessibleTextCursorEvent(iface, n->selection.focus));
                const Range r = n->selection.range();
                send(QAccessibleTextSelectionEvent(iface, r.start, r.end));
            }
            break;
        case Type::StateChanged: {
            AccessibilityStates changed = e.changed;
            if (n && changed.selected && selectionShownAsChecked(*n))
                changed.checked = true;
            for (const QAccessible::State &s : splitStates(changed))
                send(QAccessibleStateChangeEvent(iface, s));
            break;
        }
        case Type::ScrollChanged: send(QAccessibleEvent(iface, QAccessible::ScrollingPositionChanged)); break;
        case Type::BoundsChanged: send(QAccessibleEvent(iface, QAccessible::LocationChanged)); break;
        case Type::ChildrenChanged:
            if (std::find(told.begin(), told.end(), e.node) == told.end() || added > 64)
                send(QAccessibleEvent(iface, QAccessible::ObjectReorder));
            break;
        default: break;
        }
    }
    // Interfaces of nodes that went with a part already reported (or unreported) go quietly.
    std::vector<AccessNodeId> gone;
    for (const auto &[id, qid] : m_interfaces) {
        if (!tree.find(id))
            gone.push_back(id);
    }
    for (AccessNodeId id : gone)
        releaseLater(id, 0, false);
}

} // namespace brulion::atrament

#else // no accessibility in this Qt

namespace brulion::atrament {

AtramentAccessibility::AtramentAccessibility(AtramentItem *item) : m_item(item) { }
AtramentAccessibility::~AtramentAccessibility() = default;
void AtramentAccessibility::setWindow(Window *window)
{
    m_window = window;
}
void AtramentAccessibility::treeChanged(const std::vector<AccessibilityEvent> &) { }

} // namespace brulion::atrament

#endif
