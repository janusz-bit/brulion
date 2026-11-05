pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BSuggestField — a BTextField with a list of suggestions under it (above it when there is no
// room: the on-screen keyboard, the window's bottom). With `suggesting` set, every change of the
// text typed (and gaining the focus) emits suggestionsRequested(text): its handler sets
// `suggestions` — rows { title, subtitle, icon, value }, every key in every row, all strings ("" for
// none; value: what picking it gives, "" = the title).
//
//   BSuggestField {
//       suggesting: true
//       onSuggestionsRequested: (text) => suggestions = controller.propertyKeySuggestions(text)
//       onPicked: (value) => use(value); onCommitted: (text) => use(text); onCancelled: close()
//   }
//
// Keys: Down / Up move the highlight (none at first: Enter takes what is typed — committed();
// with a highlighted row picked()), Tab picks the highlighted row or the first one, Escape —
// cancelled(); keyPressedFirst(event) sees every key first (event.accepted = true: the field
// ignores it). The older `provider` function and `keyFilter` still work, not compiled. A tap or click on a row picks it. The list never takes the keyboard (the field
// keeps typing), and it is created when it is first shown (docs/UI.md "Scrolling performance").
BTextField {
    id: field

    /// The owner provides suggestions (suggestionsRequested).
    property bool suggesting: false
    /// The older form, not compiled: (text) → [{ title, subtitle?, icon?, value? }]; null: none.
    property var provider: null
    /// The suggestions for the text now: { title, subtitle, icon, value } (strings).
    property list<var> suggestions
    /// The highlighted row, -1: none.
    property int currentIndex: -1
    /// The older form of keyPressedFirst, not compiled: (event) → true when it handled the key.
    property var keyFilter: null
    /// The list is shown (the field has the focus and there is something to suggest).
    readonly property bool listShown: popupLoader.item !== null && (popupLoader.item as T.Popup).opened
    /// The list's popup (null until first shown; self-tests check where it is).
    readonly property T.Popup suggestionPopup: popupLoader.item as T.Popup
    readonly property real rowHeight: Theme.touchUi ? 44 : 32

    /// A suggestion was chosen: its value (or title).
    signal picked(string value)
    /// The suggestions for `text` are wanted: set `suggestions` (only while focused and suggesting).
    signal suggestionsRequested(string text)
    /// Every key, before the field's own handling (a list's Backspace in an empty field, say):
    /// event.accepted = true when it handled it.
    signal keyPressedFirst(KeyEvent event)
    /// Enter without a highlighted suggestion: the text typed.
    signal committed(string text)
    /// Escape.
    signal cancelled()

    function refresh(): void {
        if (activeFocus && suggesting)
            suggestionsRequested(typedText)
        else if (activeFocus && provider !== null)
            suggestions = _provided(typedText)
        else
            suggestions = []
        currentIndex = Math.min(currentIndex, suggestions.length - 1)
        if (suggestions.length > 0 && activeFocus)
            popupLoader.active = true
        if (popupLoader.item) {
            const popup = popupLoader.item as T.Popup
            if (suggestions.length > 0 && activeFocus) {
                placePopup()
                popup.open()
            } else {
                popup.close()
            }
        }
    }
    /// The older `provider`'s suggestions, as rows with every key; not compiled.
    function _provided(text: string): list<var> {
        return (provider(text) || []).map(s => ({
            title: String(s.title), subtitle: s.subtitle || "", icon: s.icon || "",
            value: s.value !== undefined ? String(s.value) : ""
        }))
    }
    /// The older `keyFilter`'s answer; not compiled.
    function _filtered(event: KeyEvent): bool {
        return !!keyFilter(event)
    }
    /// The list under the field, above it when there is no room (the keyboard, the window's bottom).
    function placePopup(): void {
        const popup = popupLoader.item as T.Popup
        const overlay = popup ? popup.parent : null
        if (!overlay)
            return
        field._at = field.mapToItem(overlay, 0, 0)
        const p = field._at
        const fieldTop = p.y + field.topInset
        const fieldBottom = p.y + field.height - field.bottomInset
        const bottomLimit = overlay.height - KeyboardInsets.overlap - Theme.space2
        const h = popup.implicitHeight
        popup.x = Math.max(Theme.space2, Math.min(p.x, overlay.width - popup.width - Theme.space2))
        popup.y = fieldBottom + 4 + h <= bottomLimit || fieldTop - 4 - h < Theme.space2
                ? Math.min(fieldBottom + 4, bottomLimit - h) : fieldTop - 4 - h
    }
    /// The field's place in the overlay (placePopup()).
    property point _at
    /// What picking row `i` gives: its value, else its title.
    function valueOf(i: int): string {
        const value = UiModels.stringField(suggestions[i], "value")
        return value !== "" ? value : UiModels.stringField(suggestions[i], "title")
    }
    function pick(i: int): void {
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
    onSuggestingChanged: if (activeFocus) refresh()

    Keys.onPressed: (event) => {
        field.keyPressedFirst(event)
        if (event.accepted)
            return
        if (field.keyFilter !== null && field._filtered(event)) {
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

            onImplicitHeightChanged: if (visible) field.placePopup()
            Connections {
                target: popup.visible ? KeyboardInsets : null
                function onOverlapChanged() { field.placePopup() }
            }
            // The window shrinking for the keyboard (a browser's resizes-content: no overlap then).
            Connections {
                target: popup.visible ? popup.parent : null
                function onHeightChanged() { field.placePopup() }
                function onWidthChanged() { field.placePopup() }
            }
            // The field moved under the list (BKeyboardFocusKeeper scrolling it above the keyboard,
            // a flick): placed again where the field is now. (Qt Quick reports no scene position.)
            Timer {
                property point last: Qt.point(-1, -1)
                interval: 100
                repeat: true
                running: popup.visible
                onRunningChanged: last = Qt.point(-1, -1)
                onTriggered: {
                    const overlay = popup.parent
                    if (!overlay)
                        return
                    field._at = field.mapToItem(overlay, 0, 0)
                    if (field._at.x !== last.x || field._at.y !== last.y) {
                        last = field._at
                        field.placePopup()
                    }
                }
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
                // (An Item around the button: a row's `icon` is no button's icon.)
                delegate: Item {
                    id: suggestion
                    required property int index
                    required property string title
                    required property string subtitle
                    required property string icon
                    width: ListView.view.width
                    height: field.rowHeight

                    T.AbstractButton {
                        id: option
                        objectName: "suggestion_" + suggestion.index
                        anchors.fill: parent
                        focusPolicy: Qt.NoFocus
                        hoverEnabled: !Theme.touchUi
                        Accessible.role: Accessible.ListItem
                        Accessible.name: suggestion.title
                        onClicked: field.pick(suggestion.index)
                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: suggestion.index === field.currentIndex ? Theme.selection
                                 : option.hovered || option.down ? Theme.bgHover : "transparent"
                        }
                        contentItem: Item {
                            BIcon {
                                id: optionIcon
                                x: Theme.space2
                                anchors.verticalCenter: parent.verticalCenter
                                name: suggestion.icon
                                size: Theme.iconSizeSm
                                color: Theme.fgMuted
                                visible: name !== ""
                            }
                            Text {
                                id: optionTitle
                                x: optionIcon.visible ? optionIcon.x + optionIcon.width + Theme.space2 : Theme.space2
                                width: parent.width - x - (optionSubtitle.visible ? optionSubtitle.width + Theme.space3 : Theme.space2)
                                anchors.verticalCenter: parent.verticalCenter
                                text: suggestion.title
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
                                text: suggestion.subtitle
                                textFormat: Text.PlainText
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textSm
                                color: Theme.fgMuted
                            }
                        }
                    }
                }
            }
        }
        onLoaded: field.refresh()
    }
}
