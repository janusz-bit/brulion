pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSuggestField — a BTextField with a list of suggestions under it (above it when there is no
// room: the on-screen keyboard, the window's bottom). The suggestions come from `provider`, a
// function of the text typed giving [{ title, subtitle?, icon?, value? }] (value: what picking
// it gives, else the title); the list follows every change of the text.
//
//   BSuggestField {
//       provider: (text) => controller.suggestPropertyKeys(text).map(k => ({ title: k.key }))
//       onPicked: (value) => use(value); onCommitted: (text) => use(text); onCancelled: close()
//   }
//
// Keys: Down / Up move the highlight (none at first: Enter takes what is typed — committed();
// with a highlighted row picked()), Tab picks the highlighted row or the first one, Escape —
// cancelled(); `keyFilter` sees every key first. A tap or click on a row picks it. The list never takes the keyboard (the field
// keeps typing), and it is created when it is first shown (docs/UI.md "Scrolling performance").
BTextField {
    id: field

    /// (text) → [{ title, subtitle, icon, value }]; null: no suggestions.
    property var provider: null
    /// The suggestions for the text now.
    property var suggestions: []
    /// The highlighted row, -1: none.
    property int currentIndex: -1
    /// (event) → true when it handled the key: called before the field's own keys (a list's
    /// Backspace in an empty field, say).
    property var keyFilter: null
    /// The list is shown (the field has the focus and there is something to suggest).
    readonly property bool listShown: popupLoader.item !== null && (popupLoader.item as T.Popup).opened
    readonly property real rowHeight: Theme.touchUi ? 44 : 32

    /// A suggestion was chosen: its value (or title).
    signal picked(var value)
    /// Enter without a highlighted suggestion: the text typed.
    signal committed(string text)
    /// Escape.
    signal cancelled()

    function refresh() {
        suggestions = provider && activeFocus ? (provider(typedText) || []) : []
        currentIndex = Math.min(currentIndex, suggestions.length - 1)
        if (suggestions.length > 0 && activeFocus)
            popupLoader.active = true
        if (popupLoader.item) {
            const popup = popupLoader.item as T.Popup
            if (suggestions.length > 0 && activeFocus) {
                popupLoader.item.place()
                popup.open()
            } else {
                popup.close()
            }
        }
    }
    function valueOf(i) {
        const s = suggestions[i]
        return s.value !== undefined ? s.value : s.title
    }
    function pick(i) {
        if (i < 0 || i >= suggestions.length)
            return
        const value = valueOf(i)
        currentIndex = -1
        if (popupLoader.item)
            (popupLoader.item as T.Popup).close()
        picked(value)
    }

    onTypedTextChanged: if (activeFocus) refresh()
    onActiveFocusChanged: {
        if (activeFocus) {
            refresh()
        } else {
            currentIndex = -1
            if (popupLoader.item)
                (popupLoader.item as T.Popup).close()
        }
    }
    onProviderChanged: if (activeFocus) refresh()

    Keys.onPressed: (event) => {
        if (field.keyFilter && field.keyFilter(event)) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Down && suggestions.length > 0) {
            currentIndex = (currentIndex + 1) % suggestions.length
            event.accepted = true
        } else if (event.key === Qt.Key_Up && suggestions.length > 0 && currentIndex >= 0) {
            currentIndex = currentIndex - 1
            event.accepted = true
        } else if (event.key === Qt.Key_Tab && suggestions.length > 0 && listShown) {
            field.pick(Math.max(0, currentIndex))
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // A word the keyboard is still composing is part of what was typed.
            if (preeditText !== "")
                EditorInput.commitPreedit(field)
            if (currentIndex >= 0 && listShown)
                field.pick(currentIndex)
            else
                field.committed(field.text)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (popupLoader.item)
                (popupLoader.item as T.Popup).close()
            field.cancelled()
            event.accepted = true
        }
    }

    Loader {
        id: popupLoader
        active: false
        sourceComponent: T.Popup {
            id: popup
            parent: T.Overlay.overlay
            focus: false
            modal: false
            closePolicy: T.Popup.NoAutoClose
            padding: Theme.space1
            width: Math.max(field.width, Math.min(320, parent ? parent.width - 2 * Theme.space2 : 320))
            implicitHeight: Math.min(list.contentHeight, field.rowHeight * (Theme.touchUi ? 4 : 7)) + topPadding + bottomPadding

            /// Under the field, above it when there is no room (the keyboard, the window's bottom).
            function place() {
                if (!parent)
                    return
                const p = field.mapToItem(parent, 0, 0)
                const fieldTop = p.y + field.topInset
                const fieldBottom = p.y + field.height - field.bottomInset
                const bottomLimit = parent.height - KeyboardInsets.overlap - Theme.space2
                const h = implicitHeight
                x = Math.max(Theme.space2, Math.min(p.x, parent.width - width - Theme.space2))
                y = fieldBottom + 4 + h <= bottomLimit || fieldTop - 4 - h < Theme.space2
                    ? Math.min(fieldBottom + 4, bottomLimit - h) : fieldTop - 4 - h
            }
            onImplicitHeightChanged: if (visible) place()
            Connections {
                target: popup.visible ? KeyboardInsets : null
                function onOverlapChanged() { popup.place() }
            }

            background: Item {
                BShadow { target: popupBackground; level: 2 }
                Rectangle {
                    id: popupBackground
                    anchors.fill: parent
                    radius: Theme.radius
                    color: Theme.bgElevated2
                    border.width: 1
                    border.color: Theme.border
                }
            }
            contentItem: ListView {
                id: list
                implicitHeight: contentHeight
                clip: true
                model: field.suggestions
                currentIndex: field.currentIndex
                boundsBehavior: Flickable.StopAtBounds
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                delegate: T.AbstractButton {
                    id: option
                    required property int index
                    required property var modelData
                    objectName: "suggestion_" + index
                    width: ListView.view.width
                    height: field.rowHeight
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: !Theme.touchUi
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.title
                    onClicked: field.pick(index)
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: option.index === field.currentIndex ? Theme.selection
                             : option.hovered || option.down ? Theme.bgHover : "transparent"
                    }
                    contentItem: Item {
                        BIcon {
                            id: optionIcon
                            x: Theme.space2
                            anchors.verticalCenter: parent.verticalCenter
                            name: option.modelData.icon || ""
                            size: Theme.iconSizeSm
                            color: Theme.fgMuted
                            visible: name !== ""
                        }
                        Text {
                            id: optionTitle
                            x: optionIcon.visible ? optionIcon.x + optionIcon.width + Theme.space2 : Theme.space2
                            width: parent.width - x - (optionSubtitle.visible ? optionSubtitle.width + Theme.space3 : Theme.space2)
                            anchors.verticalCenter: parent.verticalCenter
                            text: option.modelData.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textControl
                            color: Theme.fg
                        }
                        Text {
                            id: optionSubtitle
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.space2
                            anchors.verticalCenter: parent.verticalCenter
                            visible: text !== ""
                            text: option.modelData.subtitle || ""
                            textFormat: Text.PlainText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            color: Theme.fgMuted
                        }
                    }
                }
            }
        }
        onLoaded: field.refresh()
    }
}
