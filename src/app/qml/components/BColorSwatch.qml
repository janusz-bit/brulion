pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BColorSwatch — a round colour button (the accent presets in Ustawienia → Wygląd, the custom
// colour grid of BAccentDialog). `selected` draws a ring around it and a check mark inside (in
// black or white, whichever reads on the colour); `label` is the tooltip and accessible name;
// `iconName` replaces the check (e.g. "plus" for "Własny…" without a colour yet).
//
//   BColorSwatch { color: "#3b7dd8"; label: qsTr("Niebieski"); selected: true; onClicked: … }
T.AbstractButton {
    id: control

    property color color: Theme.accent
    property string label
    property bool selected: false
    property string iconName
    /// Visual diameter (the hit area is at least a touch target on touch UIs).
    property int size: Theme.touchUi ? 32 : 26

    implicitWidth: Math.max(size + 8, Theme.touchUi ? Theme.touchTarget : 0)
    implicitHeight: implicitWidth
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi
    checkable: false

    Accessible.role: Accessible.RadioButton
    Accessible.name: label
    Accessible.checked: selected

    readonly property bool lightColor: (0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b) > 0.55

    background: Item {
        // Selection ring.
        Rectangle {
            anchors.centerIn: parent
            width: control.size + 8
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: control.selected ? Theme.fg : control.hovered ? Theme.borderStrong : "transparent"
        }
        Rectangle {
            anchors.centerIn: parent
            width: control.size
            height: width
            radius: width / 2
            color: control.color
            border.width: 1
            border.color: Theme.alpha(Theme.fg, 0.18)
            scale: control.pressed ? 0.92 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationInstant } }
            BIcon {
                anchors.centerIn: parent
                visible: control.selected || control.iconName !== ""
                name: control.iconName !== "" ? control.iconName : "check"
                size: Theme.iconSizeSm
                color: control.lightColor ? "#1f2430" : "#ffffff"
            }
        }
        BFocusRing {
            target: control
            visible: control.visualFocus
            targetRadius: control.width / 2
        }
    }

    // Created on demand (a grid of swatches).
    property bool _touchTip: false
    onPressAndHold: if (Theme.touchUi) _touchTip = true
    Loader {
        active: control.label !== "" && (control.hovered || control.visualFocus || control._touchTip)
        sourceComponent: BToolTip {
            parent: control
            text: control.label
            visible: ((control.hovered && !control.down) || control.visualFocus) && !Theme.touchUi || control._touchTip
            onClosed: control._touchTip = false
        }
    }
}
