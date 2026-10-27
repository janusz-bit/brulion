pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BCard — elevated surface grouping related content (settings groups, conflict hunks,
// sync status, empty-state panels).
//
//   BCard { elevation: 1; ColumnLayout { anchors.fill: parent; ... } }
//   BCard { clickable: true; onClicked: open(); ... }   // hover + focus ring + Enter/Space
//
// elevation 0 (border only) … 3; `tone`: "default" | "sunken" | "accent" | "warning" |
// "danger" | "success" (tinted background + border for callouts).
T.Pane {
    id: control

    property int elevation: 0
    property bool clickable: false
    property string tone: "default"
    property real radius: Theme.radiusLg
    /// Name for assistive technology (clickable cards: say what opens).
    property string accessibleName

    signal clicked()

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: Theme.space4
    focusPolicy: clickable ? Qt.StrongFocus : Qt.NoFocus
    hoverEnabled: clickable && !Theme.touchUi
    Accessible.role: clickable ? Accessible.Button : Accessible.Grouping
    Accessible.name: accessibleName

    Keys.onPressed: (event) => {
        if (control.clickable && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            control.clicked()
            event.accepted = true
        }
    }
    TapHandler {
        enabled: control.clickable
        onTapped: control.clicked()
    }

    background: Item {
        BShadow {
            target: cardRect
            level: control.elevation
        }
        Rectangle {
            id: cardRect
            anchors.fill: parent
            radius: control.radius
            antialiasing: true
            color: {
                switch (control.tone) {
                case "sunken": return Theme.bgSunken
                case "accent": return Qt.tint(Theme.bgElevated, Theme.accentBg)
                case "warning": return Qt.tint(Theme.bgElevated, Theme.warningBg)
                case "danger": return Qt.tint(Theme.bgElevated, Theme.dangerBg)
                case "success": return Qt.tint(Theme.bgElevated, Theme.successBg)
                default: return Theme.bgElevated
                }
            }
            border.width: 1
            border.color: {
                switch (control.tone) {
                case "accent": return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                case "danger": return Qt.rgba(Theme.marginRed.r, Theme.marginRed.g, Theme.marginRed.b, 0.35)
                case "warning": return Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.55)
                case "success": return Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.35)
                case "sunken": return Theme.borderSubtle
                default: return control.clickable && control.hovered ? Theme.borderStrong : Theme.border
                }
            }
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

            BFocusRing {
                visible: control.visualFocus
                targetRadius: control.radius
            }
        }
    }
}
