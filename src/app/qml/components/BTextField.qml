pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BTextField — single-line text input.
//
//   BTextField { placeholderText: qsTr("Nazwa strony"); leadingIcon: "file-text"; clearable: true }
//   BTextField { text: url; errorText: valid ? "" : qsTr("Nieprawidłowy adres") }
//
// leadingIcon:  Lucide icon inside the field on the left
// clearable:    shows an × button while there is text; Escape clears too (signal cleared())
// errorText:    red border + message below the field (the field grows to make room)
// trailingItem: an Item shown inside on the right (e.g. a BKbd hint or a unit label)
// size:         "small" | "normal" (default)
// It is a real TextField: text, accepted(), validator, inputMethodHints … all work.
// Accessible name: `accessibleName`, else the placeholder text.
// Touch keyboards: the action key is Done (Enter: accepted()). Qt's own default on Android
// (QQuickTextInput, QTBUG-61652) is Next — a Tab to whatever focusable item lies below, a button
// as well — whenever there is one, so Enter/accepted() never came there. In a form, set
// `EnterKey.type: Qt.EnterKeyNext` on the fields that lead to another field.
T.TextField {
    id: control

    property string leadingIcon
    property bool clearable: false
    property string errorText
    property string size: "normal"
    property Item trailingItem: null
    property string accessibleName: placeholderText
    /// The window keeps it above the on-screen keyboard while it has the focus
    /// (BKeyboardFocusKeeper).
    readonly property bool keepAboveKeyboard: true

    signal cleared()
    /// Escape was pressed with nothing to clear (the key is passed on to the parent, so an
    /// enclosing dialog/popup still closes).
    signal escapePressed()

    readonly property int fieldHeight: size === "small" ? Theme.controlHeightSm : Theme.controlHeight
    readonly property int hitHeight: Math.max(fieldHeight, Theme.touchUi ? Theme.touchTarget : 0)
    readonly property int errorHeight: errorText !== "" ? errorLabel.implicitHeight + 4 : 0
    readonly property bool showClear: clearable && length > 0 && !readOnly && enabled
    readonly property int sidePadding: size === "small" ? 8 : 10

    implicitWidth: Math.max(220, placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: hitHeight + errorHeight
    topInset: (hitHeight - fieldHeight) / 2
    bottomInset: (hitHeight - fieldHeight) / 2 + errorHeight
    topPadding: topInset
    bottomPadding: bottomInset
    leftPadding: sidePadding + (leadingIcon !== "" ? iconLeft.size + 8 : 0)
    // The clear button sits at the right edge (touch: a full 44 px hit area, no side padding).
    rightPadding: (showClear ? (Theme.touchUi ? 0 : sidePadding) + clearButton.width + 2 : sidePadding)
                  + (trailingSlot.width > 0 ? trailingSlot.width + 6 : 0)

    verticalAlignment: TextInput.AlignVCenter
    color: Theme.fg
    selectionColor: Theme.selection
    selectedTextColor: Theme.selectionFg
    placeholderTextColor: Theme.fgFaint
    font.family: Theme.fontUi
    font.pixelSize: Theme.textControl
    selectByMouse: true
    hoverEnabled: !Theme.touchUi
    EnterKey.type: Qt.EnterKeyDone

    Accessible.role: Accessible.EditableText
    Accessible.name: accessibleName
    Accessible.description: errorText

    Keys.onEscapePressed: (event) => {
        if (control.clearable && control.length > 0) {
            control.clear()
            control.cleared()
            event.accepted = true
        } else {
            event.accepted = false
            control.escapePressed()
        }
    }

    // Long values (a folder path, a server address) set while the field is not being edited
    // show their beginning, not their end where the caret would be.
    onTextChanged: if (!activeFocus) cursorPosition = 0
    onActiveFocusChanged: if (!activeFocus) cursorPosition = 0
    Component.onCompleted: if (!activeFocus) cursorPosition = 0

    onTrailingItemChanged: {
        if (trailingItem) {
            trailingItem.parent = trailingSlot
            trailingItem.anchors.centerIn = trailingSlot
        }
    }

    Item {
        id: trailingSlot
        y: control.fieldTop
        height: control.fieldAreaHeight
        width: control.trailingItem && control.trailingItem.visible ? control.trailingItem.implicitWidth : 0
        x: control.width - width - (control.showClear ? clearButton.width + 2 : control.sidePadding)
    }

    Text {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        renderType: control.renderType
    }

    // The field area: the control minus its insets (callers may make the field taller).
    readonly property real fieldTop: topInset
    readonly property real fieldAreaHeight: height - topInset - bottomInset

    BIcon {
        id: iconLeft
        x: control.sidePadding
        y: control.fieldTop + (control.fieldAreaHeight - height) / 2
        name: control.leadingIcon
        size: Theme.iconSize
        color: control.activeFocus ? Theme.fgMuted : Theme.fgFaint
    }

    T.AbstractButton {
        id: clearButton
        visible: control.showClear
        width: Theme.touchUi ? Theme.touchTarget : 24
        height: Math.max(control.fieldAreaHeight, control.hitHeight)
        x: control.width - width - (Theme.touchUi ? 0 : control.sidePadding - 4)
        y: control.fieldTop + (control.fieldAreaHeight - height) / 2
        focusPolicy: Qt.NoFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Clear")
        onClicked: {
            control.clear()
            control.forceActiveFocus()
            control.cleared()
        }
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 18
                radius: 9
                color: clearButton.hovered ? Theme.bgPressed : "transparent"
            }
            BIcon {
                anchors.centerIn: parent
                name: "x"
                size: Theme.iconSizeSm
                color: clearButton.hovered ? Theme.fg : Theme.fgFaint
            }
        }
    }

    Row {
        id: errorRow
        visible: control.errorText !== ""
        x: 2
        y: control.height - control.errorHeight + 4
        width: control.width - 2
        spacing: 4
        BIcon {
            name: "circle-alert"
            size: Theme.iconSizeSm
            color: Theme.dangerText
            anchors.top: parent.top
            anchors.topMargin: 1
        }
        Text {
            id: errorLabel
            width: errorRow.width - Theme.iconSizeSm - 4
            text: control.errorText
            color: Theme.dangerText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
    }

    background: Rectangle {
        implicitWidth: 220
        implicitHeight: control.fieldHeight
        radius: Theme.radius
        color: control.readOnly ? Theme.bgSunken : Theme.bgElevated
        border.width: 1
        border.color: control.errorText !== "" ? Theme.dangerText
                    : control.activeFocus ? Theme.focusRing
                    : control.hovered ? Theme.borderControl : Theme.borderStrong
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        // Soft outer ring while focused (keyboard or pointer: a caret is focus too).
        Rectangle {
            visible: control.activeFocus
            x: -3
            y: -3
            width: parent.width + 6
            height: parent.height + 6
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Qt.rgba(control.errorText !== "" ? Theme.dangerText.r : Theme.focusRing.r,
                                  control.errorText !== "" ? Theme.dangerText.g : Theme.focusRing.g,
                                  control.errorText !== "" ? Theme.dangerText.b : Theme.focusRing.b, 0.28)
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
