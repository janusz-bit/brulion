#pragma once

// Accessibility (docs/ATRAMENT.md D13, "Accessibility"): while an assistive technology listens, every
// frame's paint also builds a semantic tree — nodes with a role, a name, a value, states, actions
// and bounds — that a platform bridge exposes (hosted: QAccessibleInterface, src/atrament/qt;
// standalone: AccessKit later). Elements describe themselves:
//
//   div().id(u"save").onClick(…).child(u"Zapisz")         a button named "Zapisz" (its text)
//   div().role(Role::Heading).ariaLevel(1).child(u"Notatki")
//   div().id(u"done").checked(done).onClick(…)              a checkbox
//   div().ariaHidden().child(u"✏")                          decoration: nothing in the tree
//
// Nodes are known across frames by the path of element ids of the element that made them (as
// element state is), so a screen reader follows a node from frame to frame. The tree lives in the
// frame arena next to the elements; nothing of it is built while Window::accessibilityEnabled()
// is false. Actions from an assistive technology (Window::performAccessibilityAction) reach the
// element's own listeners, as input does.

#include "base/function.h"
#include "base/geometry.h"
#include "ui/platform.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brulion::atrament {

class App;
class TextLayout;
class Window;
struct ScrollState;

enum class Role : std::uint8_t {
    None,   // no node of its own: what is inside belongs to the nearest node around it
    Window, // the root of a window's tree
    Group,
    Button,
    CheckBox,
    RadioButton,
    Switch,
    Link,
    Text, // static text
    Heading,
    TextInput,
    List,
    ListItem,
    Image,
    ScrollView,
    TabList,
    Tab,
    MenuItem,
    Separator,
    ProgressBar,
    Slider,
    Dialog,
};

// Roles named by the text inside them, as on the web (a button's name is its label); the text of
// any other node becomes static text nodes under it.
bool takesNameFromContent(Role role);
std::u16string_view roleName(Role role); // "button", "textInput", … (tests, dumps)

enum class AccessibilityAction : std::uint8_t {
    Click, // press / activate (toggles a checkbox)
    Focus,
    Increment, // a slider's step up
    Decrement,
    ScrollIntoView, // scrolls the containers around the node until it is visible
    ScrollForward,  // a page further in a scrolling node
    ScrollBackward,
    SetValue,     // a text input's whole text, a slider's number
    SetSelection, // a text input's selection / caret
    ReplaceText,  // a range of a text input's text
    ScrollTo,     // a scrolling node's content to an offset
};

constexpr std::uint16_t actionBit(AccessibilityAction a)
{
    return std::uint16_t(1u << unsigned(a));
}

struct AccessibilityStates {
    bool focusable = false;
    bool focused = false;
    bool selectable = false;
    bool selected = false;
    bool checkable = false;
    bool checked = false;
    bool mixed = false; // a checkbox neither checked nor unchecked
    bool disabled = false;
    bool expandable = false;
    bool expanded = false;
    bool editable = false;
    bool multiline = false;
    bool password = false;
    bool offscreen = false; // outside the visible part (scrolled away)
    bool operator==(const AccessibilityStates &) const = default;
};

// A number (sliders, progress bars).
struct AccessibilityRange {
    float value = 0;
    float min = 0;
    float max = 0;
    float step = 0; // 0: none
    bool operator==(const AccessibilityRange &) const = default;
};

// What an assistive technology asks for.
struct AccessibilityActionRequest {
    AccessibilityAction action = AccessibilityAction::Click;
    std::u16string_view text; // SetValue, ReplaceText
    Range range;              // ReplaceText: the range replaced; SetSelection: anchor = start, focus = end
    float number = 0;         // SetValue of a range
    Point offset;             // ScrollTo: the content's offset
};

using AccessNodeId = std::uint64_t; // 0: none

struct AccessibilityNode {
    using Handler = Fn<bool(const AccessibilityActionRequest &, Window &, App &)>;

    AccessNodeId id = 0;
    Role role = Role::None;
    AccessibilityStates states;
    std::uint16_t actions = 0; // actionBit()s
    std::u16string_view name;  // the frame arena's
    std::u16string_view description;
    std::u16string_view value; // a text input's text, a slider's value as text
    Rect bounds;               // window logical px
    int level = 0;             // a heading's 1–6
    // A list's row: its index; a list: the number of its rows (-1: none).
    std::int64_t rowIndex = -1;
    std::int64_t rowCount = -1;
    bool hasRange = false;
    AccessibilityRange range;
    // A text input's selection (anchor may be after focus); the caret is its focus.
    InputHandler::Selection selection;
    // Text laid out in this frame (character rectangles, hit tests): at textOrigin.
    const TextLayout *textLayout = nullptr;
    Point textOrigin;
    // Scrolling: the scrolled offset of the content, its size, the visible part (window px).
    bool scrolls = false;
    bool scrollX = false; // the axes it scrolls along
    bool scrollY = false;
    Point scrollOffset;
    Size scrollContent;
    Rect scrollViewport;

    // --- the tree (set by the builder)
    int treeIndex = 0; // position in AccessibilityTree::nodes()
    int parent = -1;
    int childOffset = 0; // into AccessibilityTree's child list
    int childCount = 0;
    int indexInParent = 0;
    int scrollAncestor = -1; // the nearest node around it that scrolls

    // --- the element's (valid while its frame is)
    Handler handler; // runs the element's listeners for an action; returns whether it did
    ScrollState *scrollState = nullptr;
    bool nameFromContent = false; // text inside is appended to the name
    bool adoptable = false;       // a list's row: the row's root element describes it
};

// The tree of one frame. Pointers into it are valid until the frame after the next one starts.
class AccessibilityTree {
public:
    bool isEmpty() const { return m_nodes.empty(); }
    std::size_t size() const { return m_nodes.size(); }
    const AccessibilityNode *root() const { return m_nodes.empty() ? nullptr : m_nodes.front(); }
    const AccessibilityNode *node(int index) const
    {
        return index >= 0 && std::size_t(index) < m_nodes.size() ? m_nodes[std::size_t(index)] : nullptr;
    }
    std::span<AccessibilityNode *const> nodes() const { return m_nodes; }
    const AccessibilityNode *find(AccessNodeId id) const;
    const AccessibilityNode *parentOf(const AccessibilityNode &n) const { return node(n.parent); }
    const AccessibilityNode *child(const AccessibilityNode &n, int i) const;
    std::span<const int> childIndexes(const AccessibilityNode &n) const;
    const AccessibilityNode *focused() const;
    // The deepest node whose bounds hold the point (window px), topmost first.
    const AccessibilityNode *hitTest(Point p) const;
    // An indented outline, one node per line: `button "Zapisz" [focusable]` (tests, debugging).
    std::u16string dump() const;

    // --- building (Window, during paint)
    void clear();
    AccessibilityNode *add(AccessibilityNode *node, int parent);
    void finish();

private:
    std::vector<AccessibilityNode *> m_nodes; // in the frame arena; pre-order
    std::vector<int> m_children;
    std::vector<std::pair<AccessNodeId, int>> m_byId; // sorted
    int m_focused = -1;
};

// A change between two frames' trees, for the platform's bridge. Strings point into both trees,
// valid during PlatformWindow::accessibilityChanged().
struct AccessibilityEvent {
    enum class Type : std::uint8_t {
        Added,           // a node whose parent was there before (the root of what was inserted)
        Removed,         // a node whose parent is still there (the root of what went)
        ChildrenChanged, // the node's children are others or in another order
        Focus,           // `node` has the focus now (0: nothing in the window has)
        NameChanged,
        DescriptionChanged,
        ValueChanged,     // `oldText` → the node's value
        SelectionChanged, // a text input's selection or caret
        StateChanged,     // `changed` says which
        RoleChanged,
        BoundsChanged, // only for the focused node (the screen reader's focus frame follows it)
        ScrollChanged,
        TreeReset, // a tree after none (accessibility was just turned on): read everything again
    };
    Type type;
    AccessNodeId node = 0;
    AccessNodeId parent = 0;     // Added, Removed
    AccessibilityStates changed; // StateChanged: the states that differ
    std::u16string_view oldText; // ValueChanged
};

// The events that turn `before` into `after`.
void diffAccessibilityTrees(const AccessibilityTree &before, const AccessibilityTree &after,
                            std::vector<AccessibilityEvent> &events);

} // namespace brulion::atrament
