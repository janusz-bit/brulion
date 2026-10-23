pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BKeyboardFocusKeeper — keeps the focused form field above the on-screen keyboard (docs/UI.md
// "On-screen keyboard"). Nothing pans the window to a field (Android: adjustResize; the browser's
// page never moves): the content only ends at the keyboard's top. So while the keyboard is up,
// the Flickables around the focused field — an item with `keepAboveKeyboard: true`, BTextField
// and BTextArea — scroll it into the part the keyboard leaves: when it gets the focus, when the
// keyboard has settled (not frame by frame while it slides), when the window or the caret moves.
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
    readonly property Item field: focusItem && focusItem.keepAboveKeyboard === true ? focusItem : null

    visible: false

    function schedule() {
        if (field && KeyboardInsets.shown && !KeyboardInsets.animating)
            Qt.callLater(keeper.keepVisible)
    }

    /// Scroll the Flickables around the focused field, innermost first, until it is above the
    /// keyboard and below their top edges.
    function keepVisible() {
        const item = field
        if (!item || !item.visible || !window || !KeyboardInsets.shown || KeyboardInsets.animating)
            return
        const keyboardTop = window.height - KeyboardInsets.overlap
        for (let f = item.parent; f; f = f.parent) {
            if (!(f instanceof Flickable) || f.dragging)
                continue
            // The part of the Flickable left above the keyboard (its own coordinates: the shell
            // may not have laid out the shorter view yet).
            const visibleBottom = Math.min(f.height, keyboardTop - f.mapToItem(null, 0, 0).y)
            if (visibleBottom < 2 * margin)
                continue
            const at = item.mapToItem(f, 0, 0)
            let top = at.y
            let bottom = at.y + item.height
            if (bottom - top > visibleBottom - 2 * margin && item.cursorRectangle !== undefined) {
                const caret = item.cursorRectangle
                top = item.mapToItem(f, caret.x, caret.y).y
                bottom = top + caret.height
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
    // Typing moves the caret (a growing text area, a new line).
    Connections {
        target: keeper.field
        ignoreUnknownSignals: true
        function onCursorRectangleChanged() { keeper.schedule() }
    }
}
