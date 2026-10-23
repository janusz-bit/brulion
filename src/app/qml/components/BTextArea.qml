pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BTextArea — multi-line plain-text input (notes, manual merge resolution, descriptions).
//
//   BTextArea { placeholderText: qsTr("Opis…"); text: draft }
//
// For long texts put it in a ScrollView/Flickable (TextArea.flickable) and set
// `background: null` on it, giving the ScrollView the frame instead. `monospace` switches
// to the fixed-width font (code, merge editing). `errorText` works like in BTextField.
T.TextArea {
    id: control

    property bool monospace: false
    property string errorText
    property string accessibleName: placeholderText
    /// The window keeps it (or its caret) above the on-screen keyboard while it has the focus
    /// (BKeyboardFocusKeeper).
    readonly property bool keepAboveKeyboard: true

    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding,
                            implicitBackgroundWidth + leftInset + rightInset,
                            placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight + topInset + bottomInset,
                             placeholder.implicitHeight + topPadding + bottomPadding)
    padding: Theme.touchUi ? 12 : 10

    color: Theme.fg
    selectionColor: Theme.selection
    selectedTextColor: Theme.selectionFg
    placeholderTextColor: Theme.fgFaint
    font.family: monospace ? Theme.fontMono : Theme.fontUi
    font.pixelSize: monospace ? Theme.textSm : Theme.textControl
    wrapMode: TextEdit.Wrap
    selectByMouse: true
    hoverEnabled: !Theme.touchUi

    Accessible.role: Accessible.EditableText
    Accessible.name: accessibleName
    Accessible.description: errorText

    Text {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        wrapMode: Text.Wrap
        visible: !control.length && !control.preeditText
    }

    background: Rectangle {
        implicitWidth: 240
        implicitHeight: 96
        radius: Theme.radius
        color: control.readOnly ? Theme.bgSunken : Theme.bgElevated
        border.width: 1
        border.color: control.errorText !== "" ? Theme.dangerText
                    : control.activeFocus ? Theme.focusRing
                    : control.hovered ? Theme.borderControl : Theme.borderStrong
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            visible: control.activeFocus
            x: -3
            y: -3
            width: parent.width + 6
            height: parent.height + 6
            radius: parent.radius + 3
            color: "transparent"
            border.width: 3
            border.color: Qt.rgba(Theme.focusRing.r, Theme.focusRing.g, Theme.focusRing.b, 0.28)
        }
    }

    opacity: enabled ? 1 : Theme.disabledOpacity
}
