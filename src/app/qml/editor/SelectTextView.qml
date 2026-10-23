pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SelectTextView — "Zaznacz tekst" (docs/UI.md "Selecting and copying"): the whole page (both
// note modes, a day of the journal too) as ONE read-only rich text, for selecting any characters
// across paragraphs, list items and headings — what the block editor itself selects only as
// whole blocks. The text is PageController.readingHtml(): the editor's escaping renderer, never
// a note's own HTML; bullets, numbers and checkboxes are characters, so a copy keeps them.
//   * desktops: the mouse selects by dragging (Shift+click extends), Shift+arrows, Ctrl+A;
//   * touch UIs: a finger scrolls; a long press selects the word under it. Android shows its own
//     selection handles and Copy / Select all popup for it (Qt 6.11 gives them to read-only text;
//     the popup's Copy arrives as Ctrl+C, the exact copy below); elsewhere — the browser on a
//     phone, a desktop's touch UI — two handles of the view's own (44 px targets) move the ends;
//     a tap elsewhere clears it;
//   * "Kopiuj" / Ctrl+C copy the selection as plain text with every character (no-break spaces
//     too), the whole page when nothing is selected; "Zamknij", Esc and Back close the view.
// BlockEditor.openSelectText() shows it over the editor (a day: over the journal stream's frame).
FocusScope {
    id: view

    property BlockEditor editor
    readonly property PageController controller: editor ? editor.controller : null
    signal closed()

    /// The page at the time the view opened (later changes are not followed: reopen for them) —
    /// in the current theme's colours: a theme change renders it again, the selection kept.
    property string html: ""
    readonly property alias textEdit: text
    readonly property bool hasSelection: text.selectionStart !== text.selectionEnd

    focus: true
    Component.onCompleted: {
        html = controller ? controller.readingHtml() : ""
        text.forceActiveFocus()
    }
    Connections {
        target: view.controller
        function onPaletteChanged() {
            const start = text.selectionStart
            const end = text.selectionEnd
            const y = flick.contentY
            view.html = view.controller.readingHtml()
            if (start !== end)
                text.select(start, end)
            flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
        }
    }

    /// Copy the selection — or, with nothing selected, the whole page — as plain text and say so.
    function copy() {
        if (!controller)
            return false
        const all = !hasSelection
        const plain = EditorInput.textRange(text.textDocument, all ? 0 : text.selectionStart,
                                            all ? text.length : text.selectionEnd)
        const r = controller.copyText(plain)
        if (r.ok)
            editor.notice(all ? qsTr("Copied the whole page") : qsTr("Copied the selected text"), "success")
        else
            editor.notice(editor.selectionCopyFailure(r.reason), "error")
        return r.ok
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }
    // Clicks between the bar and the text, and wheel turns, stay in the view: a wheel turn at the
    // text's end (or over the bar, or a text that fits) must not scroll the page or the journal
    // stream underneath (a day scrolled away takes the view with it).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // ---- The bar ------------------------------------------------------------------------------
    Rectangle {
        id: bar
        width: parent.width
        height: Theme.touchUi ? Theme.touchTarget + Theme.space2 : Theme.controlHeight + Theme.space3
        color: Theme.bgElevated
        z: 2
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
        BIconButton {
            id: closeButton
            objectName: "selectTextClose"
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.space1
            iconName: "x"
            label: qsTr("Close")
            shortcut: "Esc"
            focusPolicy: Qt.NoFocus
            onClicked: view.closed()
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: closeButton.right
            anchors.leftMargin: Theme.space1
            anchors.right: buttons.left
            anchors.rightMargin: Theme.space2
            Text {
                width: parent.width
                text: qsTr("Select text")
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightSemiBold
                color: Theme.fg
                elide: Text.ElideRight
            }
            Text {
                objectName: "selectTextHint"
                width: parent.width
                visible: !Theme.compact || !view.hasSelection
                text: Theme.touchUi ? qsTr("Hold a word, then drag the handles")
                                    : qsTr("Drag over the text; Shift+arrows, Ctrl+A")
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgMuted
                elide: Text.ElideRight
            }
        }
        Row {
            id: buttons
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: Theme.space2
            spacing: Theme.space1
            BIconButton {
                objectName: "selectTextAll"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "text-select"
                label: qsTr("Select all")
                shortcut: "Ctrl+A"
                focusPolicy: Qt.NoFocus
                onClicked: text.selectAll()
            }
            BButton {
                objectName: "selectTextCopy"
                anchors.verticalCenter: parent.verticalCenter
                size: "small"
                variant: "primary"
                iconName: "copy"
                text: view.hasSelection ? qsTr("Copy") : qsTr("Copy all")
                focusPolicy: Qt.NoFocus
                onClicked: view.copy()
            }
        }
    }

    // ---- The text -----------------------------------------------------------------------------
    Flickable {
        id: flick
        objectName: "selectTextFlick"
        TouchFlick.enabled: true
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: text.y + text.height + Theme.space12
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drag selects text (TextEdit), the wheel and a finger scroll.
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        clip: true
        T.ScrollBar.vertical: BScrollBar {}

        readonly property real pad: Theme.compact ? Theme.space4 : Theme.space8
        TextEdit {
            id: text
            objectName: "selectTextEdit"
            x: Math.max(flick.pad, (flick.width - view.measure) / 2)
            y: Theme.compact ? Theme.space4 : Theme.space6
            width: Math.min(view.measure, flick.width - 2 * flick.pad)
            readOnly: true
            textFormat: TextEdit.RichText
            text: view.html
            wrapMode: TextEdit.Wrap
            // Not on touch UIs: a finger scrolls (a TextEdit that selects by dragging keeps the
            // finger); the long press and the handles select there.
            selectByMouse: !Theme.touchUi
            selectByKeyboard: true
            persistentSelection: true
            activeFocusOnPress: true
            focus: true
            font.family: Theme.fontEditor
            font.pixelSize: Theme.textEditor
            color: Theme.fg
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionFg
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("The page as text")

            Keys.onShortcutOverride: (event) => {
                if ((event.modifiers & Qt.ControlModifier) && [Qt.Key_C, Qt.Key_A, Qt.Key_Insert].indexOf(event.key) >= 0)
                    event.accepted = true
            }
            Keys.onPressed: (event) => {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                if (ctrl && (event.key === Qt.Key_C || event.key === Qt.Key_Insert)) {
                    view.copy()
                    event.accepted = true
                } else if (ctrl && event.key === Qt.Key_A) {
                    text.selectAll()
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    event.accepted = true
                }
            }
            Keys.onReleased: (event) => {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) {
                    event.accepted = true
                    view.closed()
                }
            }
            onCursorRectangleChanged: if (!Theme.touchUi && activeFocus) view.keepVisible(cursorRectangle)
        }

        // Touch UIs: a long press selects a word, a tap clears the selection (on an item above the
        // text, as in the source mode: a drag past the threshold goes to the Flickable).
        Item {
            x: text.x
            y: text.y
            width: text.width
            height: text.height
            visible: Theme.touchUi
            TapHandler {
                id: touchTap
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: text.deselect()
                onLongPressed: {
                    text.forceActiveFocus()
                    text.cursorPosition = text.positionAt(touchTap.point.position.x, touchTap.point.position.y)
                    text.selectWord()
                }
            }
        }

        // The handles of a touch selection: the start's and the end's, dragged with a finger
        // (not on Android: the system's are there).
        Repeater {
            model: Theme.touchUi && view.hasSelection && DeviceInfo.platform !== "android" ? 2 : 0
            delegate: Item {
                id: handle
                required property int index
                readonly property bool atStart: index === 0
                // width and contentHeight read for real: positionToRectangle() is a method call,
                // not tracked, and a bare `void text.width` is compiled away with the dependency
                // (the handles stayed put when a narrower window wrapped the text again).
                readonly property rect at: {
                    if (text.width < 0 || text.contentHeight < 0)
                        return Qt.rect(0, 0, 0, 0)
                    return text.positionToRectangle(handle.atStart ? text.selectionStart : text.selectionEnd)
                }
                objectName: atStart ? "selectTextHandleStart" : "selectTextHandleEnd"
                width: Theme.touchTarget
                height: Theme.touchTarget
                x: text.x + at.x - width / 2
                y: text.y + at.y + at.height - 4
                z: 3
                Rectangle {
                    // The stem from the text line, then the knob.
                    x: parent.width / 2 - 1
                    y: 0
                    width: 2
                    height: 8
                    color: Theme.accent
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 6
                    width: 18
                    height: 18
                    radius: 9
                    color: Theme.accent
                    border.width: 2
                    border.color: Theme.bg
                }
                MouseArea {
                    /// The selection's other end, fixed while this handle is dragged (the handle
                    /// may cross it: then it is the start that moves past the end, or back).
                    property int fixedEnd: -1
                    anchors.fill: parent
                    preventStealing: true
                    onPressed: fixedEnd = handle.atStart ? text.selectionEnd : text.selectionStart
                    onReleased: fixedEnd = -1
                    onCanceled: fixedEnd = -1
                    onPositionChanged: (mouse) => {
                        if (fixedEnd < 0)
                            return
                        const p = mapToItem(text, mouse.x, mouse.y - handle.height / 2 + 4)
                        const pos = text.positionAt(p.x, p.y)
                        if (pos !== fixedEnd)
                            text.select(Math.min(pos, fixedEnd), Math.max(pos, fixedEnd))
                        view.keepVisible(Qt.rect(p.x, p.y, 1, Theme.touchTarget))
                    }
                }
            }
        }
    }

    /// The text column's width: the editor's.
    readonly property real measure: editor ? editor.maxTextWidth : Theme.measure

    /// Scroll so that `rect` (in the text's coordinates) is in view.
    function keepVisible(rect) {
        const top = text.y + rect.y
        const bottom = top + rect.height
        if (top < flick.contentY + Theme.space4)
            flick.contentY = Math.max(0, top - Theme.space4)
        else if (bottom > flick.contentY + flick.height - Theme.space4)
            flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), bottom - flick.height + Theme.space4)
    }
}
