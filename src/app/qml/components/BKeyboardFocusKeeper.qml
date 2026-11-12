pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BKeyboardFocusKeeper — keeps the focused form field above the on-screen keyboard (docs/UI.md
// "On-screen keyboard"). Nothing pans the window to a field (Android: adjustResize; the browser's
// page never moves): the content only ends at the keyboard's top. So while the keyboard is up,
// the Flickables around the focused field — an item with `keepAboveKeyboard: true`, BTextField
// and BTextArea — scroll it into the part the keyboard leaves: when it gets the focus, when the
// keyboard has settled (not frame by frame while it slides), when the window or the caret moves
// and when the Flickable around the field gets another height (the shell's view ending at the
// keyboard).
// A field taller than that part keeps its caret there. The block editor keeps its own caret,
// table cell and source above its toolbar (BlockEditor.ensureEditVisible).
//
//   BKeyboardFocusKeeper { window: window }   // once per window (Main.qml)
Item {
    id: keeper

    property Window window: null
    /// Room kept between the field and the keyboard (and the top edge).
    property real margin: Theme.space3

    readonly property Item focusItem: window ? window.activeFocusItem : null
    /// The focused field kept above the keyboard (BTextField and BTextArea: keepAboveKeyboard).
    readonly property Item field: (focusItem as BTextField) !== null && (focusItem as BTextField).keepAboveKeyboard
                                  || (focusItem as BTextArea) !== null && (focusItem as BTextArea).keepAboveKeyboard
                                  ? focusItem : null

    visible: false

    /// Places in keepVisible() (a method's point result is read through a typed property).
    property point _at
    property rect _caret

    function schedule(): void {
        if (field && KeyboardInsets.shown && !KeyboardInsets.animating)
            later.restart()
    }
    // (Once the current changes are done, merging repeated requests.)
    Later {
        id: later
        onTriggered: keeper.keepVisible()
    }
    /// Scroll the Flickables around the focused field, innermost first, until it is above the
    /// keyboard and below their top edges.
    function keepVisible(): void {
        const item = field
        if (!item || !item.visible || !window || !KeyboardInsets.shown || KeyboardInsets.animating)
            return
        const keyboardTop = window.height - KeyboardInsets.overlap
        for (let p = item.parent; p; p = p.parent) {
            const f = p as Flickable
            if (f === null || f.dragging)
                continue
            // The part of the Flickable left above the keyboard (its own coordinates: the shell
            // may not have laid out the shorter view yet).
            keeper._at = f.mapToItem(null, 0, 0)
            const visibleBottom = Math.min(f.height, keyboardTop - keeper._at.y)
            if (visibleBottom < 2 * margin)
                continue
            keeper._at = item.mapToItem(f, 0, 0)
            let top = keeper._at.y
            let bottom = keeper._at.y + item.height
            if (bottom - top > visibleBottom - 2 * margin) {
                const textField = item as T.TextField
                const textArea = item as T.TextArea
                if (textField !== null)
                    keeper._caret = textField.cursorRectangle
                else if (textArea !== null)
                    keeper._caret = textArea.cursorRectangle
                keeper._at = item.mapToItem(f, keeper._caret.x, keeper._caret.y)
                top = keeper._at.y
                bottom = top + keeper._caret.height
            }
            let delta = 0
            if (bottom > visibleBottom - margin)
                delta = bottom - (visibleBottom - margin)
            else if (top < margin)
                delta = top - margin
            if (delta === 0)
                continue
            const minY = f.originY - f.topMargin
            const maxY = Math.max(minY, f.originY + f.contentHeight + f.bottomMargin - visibleBottom)
            const target = Math.max(minY, Math.min(maxY, f.contentY + delta))
            if (Math.abs(target - f.contentY) >= 0.5)
                f.contentY = target
        }
    }

    /// The innermost Flickable around the field. The shell lays the view out again after the
    /// keyboard came, in steps (taller without the bottom navigation, then ending at the
    /// keyboard), and a Flickable pulls a scroll past its end back at each: its new height keeps
    /// the field in view again (a field near the end of a short content).
    readonly property Flickable fieldFlickable: innermostFlickable(field)
    function innermostFlickable(item: Item): Flickable {
        if (!item)
            return null
        for (let p = item.parent; p; p = p.parent) {
            const f = p as Flickable
            if (f !== null)
                return f
        }
        return null
    }

    onFieldChanged: schedule()
    Connections {
        target: KeyboardInsets
        function onShownChanged() { keeper.schedule() }
        function onAnimatingChanged() { keeper.schedule() }
        function onOverlapChanged() { keeper.schedule() }
    }
    Connections {
        target: keeper.window
        function onHeightChanged() { keeper.schedule() }
    }
    Connections {
        target: keeper.fieldFlickable
        function onHeightChanged() { keeper.schedule() }
    }
    // Typing moves the caret (a growing text area, a new line).
    Connections {
        target: keeper.field
        ignoreUnknownSignals: true
        function onCursorRectangleChanged() { keeper.schedule() }
    }
}
