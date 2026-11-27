#include "ui/accessibility.h"

#include "base/hash.h"

#include <algorithm>
#include <cstdio>

namespace brulion::atrament {

bool takesNameFromContent(Role role)
{
    switch (role) {
    case Role::Button:
    case Role::CheckBox:
    case Role::RadioButton:
    case Role::Switch:
    case Role::Link:
    case Role::Heading:
    case Role::ListItem:
    case Role::Tab:
    case Role::MenuItem:
    case Role::Cell:
    case Role::ColumnHeader: return true;
    default: return false;
    }
}

std::u16string_view roleName(Role role)
{
    switch (role) {
    case Role::None: return u"none";
    case Role::Window: return u"window";
    case Role::Group: return u"group";
    case Role::Button: return u"button";
    case Role::CheckBox: return u"checkBox";
    case Role::RadioButton: return u"radioButton";
    case Role::Switch: return u"switch";
    case Role::Link: return u"link";
    case Role::Text: return u"text";
    case Role::Heading: return u"heading";
    case Role::TextInput: return u"textInput";
    case Role::List: return u"list";
    case Role::ListItem: return u"listItem";
    case Role::Image: return u"image";
    case Role::ScrollView: return u"scrollView";
    case Role::TabList: return u"tabList";
    case Role::Tab: return u"tab";
    case Role::MenuItem: return u"menuItem";
    case Role::Separator: return u"separator";
    case Role::ProgressBar: return u"progressBar";
    case Role::Slider: return u"slider";
    case Role::Dialog: return u"dialog";
    case Role::Table: return u"table";
    case Role::Row: return u"row";
    case Role::Cell: return u"cell";
    case Role::ColumnHeader: return u"columnHeader";
    }
    return u"?";
}

// --- the tree

void AccessibilityTree::clear()
{
    m_nodes.clear();
    m_children.clear();
    m_byId.clear();
    m_focused = -1;
}

AccessibilityNode *AccessibilityTree::add(AccessibilityNode *node, int parent)
{
    node->treeIndex = int(m_nodes.size());
    node->parent = parent;
    if (AccessibilityNode *p = parent >= 0 ? m_nodes[std::size_t(parent)] : nullptr) {
        node->indexInParent = p->childCount++;
        node->scrollAncestor = p->scrolls ? p->treeIndex : p->scrollAncestor;
    }
    m_nodes.push_back(node);
    return node;
}

void AccessibilityTree::finish()
{
    // Children in one list, each node's together (the nodes are in pre-order, so a parent's
    // children come in their order).
    m_children.assign(m_nodes.size(), 0);
    int offset = 0;
    for (AccessibilityNode *n : m_nodes) {
        n->childOffset = offset;
        offset += n->childCount;
    }
    for (AccessibilityNode *n : m_nodes) {
        if (n->parent >= 0) {
            const AccessibilityNode *p = m_nodes[std::size_t(n->parent)];
            m_children[std::size_t(p->childOffset + n->indexInParent)] = n->treeIndex;
        }
    }
    m_children.resize(std::size_t(offset));

    // Ids are unique: a second node with an id (an element painted twice) gets one made from it
    // and its rank, so it still stays the same across frames.
    m_byId.clear();
    m_byId.reserve(m_nodes.size());
    for (const AccessibilityNode *n : m_nodes)
        m_byId.emplace_back(n->id, n->treeIndex);
    std::sort(m_byId.begin(), m_byId.end());
    bool renamed = false;
    for (std::size_t i = 0; i < m_byId.size();) {
        std::size_t j = i + 1;
        for (; j < m_byId.size() && m_byId[j].first == m_byId[i].first; ++j) {
            AccessibilityNode *n = m_nodes[std::size_t(m_byId[j].second)];
            n->id = hashCombine(n->id, j - i) | 1;
            m_byId[j].first = n->id;
            renamed = true;
        }
        i = j;
    }
    if (renamed)
        std::sort(m_byId.begin(), m_byId.end());

    m_focused = -1;
    for (const AccessibilityNode *n : m_nodes) {
        if (n->states.focused)
            m_focused = n->treeIndex; // the deepest: an inner element tracking the same focus
    }
}

const AccessibilityNode *AccessibilityTree::find(AccessNodeId id) const
{
    auto it = std::lower_bound(m_byId.begin(), m_byId.end(), std::pair<AccessNodeId, int>(id, -1));
    if (it == m_byId.end() || it->first != id)
        return nullptr;
    return m_nodes[std::size_t(it->second)];
}

std::span<const int> AccessibilityTree::childIndexes(const AccessibilityNode &n) const
{
    if (n.childCount == 0 || std::size_t(n.childOffset + n.childCount) > m_children.size())
        return {};
    return std::span<const int>(m_children).subspan(std::size_t(n.childOffset), std::size_t(n.childCount));
}

const AccessibilityNode *AccessibilityTree::child(const AccessibilityNode &n, int i) const
{
    const std::span<const int> c = childIndexes(n);
    return i >= 0 && std::size_t(i) < c.size() ? m_nodes[std::size_t(c[std::size_t(i)])] : nullptr;
}

const AccessibilityNode *AccessibilityTree::focused() const
{
    return node(m_focused);
}

const AccessibilityNode *AccessibilityTree::hitTest(Point p) const
{
    const AccessibilityNode *at = root();
    if (!at || !at->bounds.contains(p))
        return nullptr;
    for (;;) {
        const AccessibilityNode *next = nullptr;
        const std::span<const int> c = childIndexes(*at);
        for (std::size_t i = c.size(); i-- > 0;) { // painted last: on top
            const AccessibilityNode *n = m_nodes[std::size_t(c[i])];
            if (n->bounds.contains(p) && !n->states.offscreen) {
                next = n;
                break;
            }
        }
        if (!next)
            return at;
        at = next;
    }
}

namespace {

// Line breaks as \n: one node per line.
void appendEscaped(std::u16string &out, std::u16string_view s)
{
    for (char16_t c : s) {
        if (c == u'\n')
            out += u"\\n";
        else
            out += c;
    }
}

void appendAscii(std::u16string &out, const char *s)
{
    while (*s)
        out += char16_t(*s++);
}

void appendNumber(std::u16string &out, double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    appendAscii(out, buf);
}

} // namespace

std::u16string AccessibilityTree::dump() const
{
    std::u16string out;
    for (const AccessibilityNode *n : m_nodes) {
        int depth = 0;
        for (int p = n->parent; p >= 0; p = m_nodes[std::size_t(p)]->parent)
            ++depth;
        out.append(std::size_t(depth) * 2, u' ');
        out += roleName(n->role);
        if (n->level > 0) {
            out += u' ';
            appendNumber(out, n->level);
        }
        if (!n->name.empty()) {
            out += u" \"";
            appendEscaped(out, n->name);
            out += u'"';
        }
        if (!n->value.empty()) {
            out += u" = \"";
            appendEscaped(out, n->value);
            out += u'"';
        }
        if (n->hasRange) {
            out += u" = ";
            appendNumber(out, n->range.value);
        }
        if (!n->description.empty()) {
            out += u" (";
            appendEscaped(out, n->description);
            out += u')';
        }
        if (n->rowIndex >= 0) {
            out += u" #";
            appendNumber(out, double(n->rowIndex));
        }
        if (n->rowCount >= 0) {
            out += u" of ";
            appendNumber(out, double(n->rowCount));
        }
        const AccessibilityStates &s = n->states;
        const std::pair<bool, const char *> flags[] = {
                {s.focusable, "focusable"},
                {s.focused, "focused"},
                {s.selected, "selected"},
                {s.checkable && !s.checked && !s.mixed, "unchecked"},
                {s.checked, "checked"},
                {s.mixed, "mixed"},
                {s.disabled, "disabled"},
                {s.expandable && s.expanded, "expanded"},
                {s.expandable && !s.expanded, "collapsed"},
                {s.editable, "editable"},
                {s.multiline, "multiline"},
                {s.password, "password"},
                {s.offscreen, "offscreen"},
        };
        bool first = true;
        for (const auto &[on, label] : flags) {
            if (!on)
                continue;
            out += first ? u" [" : u", ";
            appendAscii(out, label);
            first = false;
        }
        if (!first)
            out += u']';
        out += u'\n';
    }
    return out;
}

// --- differences between frames

namespace {

AccessibilityStates changedStates(const AccessibilityStates &a, const AccessibilityStates &b)
{
    AccessibilityStates c;
    c.focusable = a.focusable != b.focusable;
    c.selectable = a.selectable != b.selectable;
    c.selected = a.selected != b.selected;
    c.checkable = a.checkable != b.checkable;
    c.checked = a.checked != b.checked;
    c.mixed = a.mixed != b.mixed;
    c.disabled = a.disabled != b.disabled;
    c.expandable = a.expandable != b.expandable;
    c.expanded = a.expanded != b.expanded;
    c.editable = a.editable != b.editable;
    c.multiline = a.multiline != b.multiline;
    c.password = a.password != b.password;
    c.offscreen = a.offscreen != b.offscreen;
    // Focus is reported as its own event.
    return c;
}

AccessibilityEvent makeEvent(AccessibilityEvent::Type type, AccessNodeId node, AccessNodeId parent = 0)
{
    AccessibilityEvent e{type, node, parent, {}, {}};
    return e;
}

bool sameChildren(const AccessibilityTree &before, const AccessibilityNode &a, const AccessibilityTree &after,
                  const AccessibilityNode &b)
{
    if (a.childCount != b.childCount)
        return false;
    const std::span<const int> ca = before.childIndexes(a);
    const std::span<const int> cb = after.childIndexes(b);
    for (std::size_t i = 0; i < ca.size(); ++i) {
        if (before.node(ca[i])->id != after.node(cb[i])->id)
            return false;
    }
    return true;
}

} // namespace

void diffAccessibilityTrees(const AccessibilityTree &before, const AccessibilityTree &after,
                            std::vector<AccessibilityEvent> &events)
{
    using Type = AccessibilityEvent::Type;
    events.clear();
    if (after.isEmpty())
        return;
    if (before.isEmpty()) {
        events.push_back(makeEvent(Type::TreeReset, after.root()->id));
        if (const AccessibilityNode *f = after.focused())
            events.push_back(makeEvent(Type::Focus, f->id));
        return;
    }
    // What went: the topmost node of each removed part.
    for (const AccessibilityNode *a : before.nodes()) {
        if (after.find(a->id))
            continue;
        const AccessibilityNode *p = before.parentOf(*a);
        if (p && after.find(p->id))
            events.push_back(makeEvent(Type::Removed, a->id, p->id));
    }
    for (const AccessibilityNode *b : after.nodes()) {
        const AccessibilityNode *a = before.find(b->id);
        if (!a) {
            const AccessibilityNode *p = after.parentOf(*b);
            if (p && before.find(p->id))
                events.push_back(makeEvent(Type::Added, b->id, p->id));
            continue;
        }
        if (a->role != b->role)
            events.push_back(makeEvent(Type::RoleChanged, b->id));
        if (a->name != b->name)
            events.push_back(makeEvent(Type::NameChanged, b->id));
        if (a->description != b->description)
            events.push_back(makeEvent(Type::DescriptionChanged, b->id));
        if (a->value != b->value || a->hasRange != b->hasRange || !(a->range == b->range)) {
            AccessibilityEvent e = makeEvent(Type::ValueChanged, b->id);
            e.oldText = a->value;
            events.push_back(e);
        }
        if (b->role == Role::TextInput
            && (a->selection.anchor != b->selection.anchor || a->selection.focus != b->selection.focus))
            events.push_back(makeEvent(Type::SelectionChanged, b->id));
        const AccessibilityStates changed = changedStates(a->states, b->states);
        if (!(changed == AccessibilityStates{})) {
            AccessibilityEvent e = makeEvent(Type::StateChanged, b->id);
            e.changed = changed;
            events.push_back(e);
        }
        if (b->scrolls && !(a->scrollOffset == b->scrollOffset))
            events.push_back(makeEvent(Type::ScrollChanged, b->id));
        if (b->states.focused && !(a->bounds == b->bounds))
            events.push_back(makeEvent(Type::BoundsChanged, b->id));
        if (!sameChildren(before, *a, after, *b))
            events.push_back(makeEvent(Type::ChildrenChanged, b->id));
    }
    const AccessibilityNode *fa = before.focused();
    const AccessibilityNode *fb = after.focused();
    if ((fa ? fa->id : 0) != (fb ? fb->id : 0))
        events.push_back(makeEvent(Type::Focus, fb ? fb->id : 0));
}

} // namespace brulion::atrament
