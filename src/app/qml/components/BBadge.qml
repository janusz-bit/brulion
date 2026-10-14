pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BBadge — small pill with a count or a short label, or a dot.
//
//   BBadge { count: 12 }                    BBadge { text: qsTr("Nowe"); variant: "accent" }
//   BBadge { dot: true; variant: "warning" }
//
// variant: "neutral" (default) | "accent" | "danger" | "warning" | "success" | "info".
// `solid` (default for accent/danger) fills with the strong colour. Counts above `max`
// show as "99+". A count of 0 hides the badge unless `showZero`.
Rectangle {
    id: badge

    property string text
    property int count: -1
    property int max: 99
    property bool showZero: false
    property bool dot: false
    property string variant: "neutral"
    property bool solid: variant === "accent" || variant === "danger"

    readonly property string label: count >= 0 ? (count > max ? max + "+" : String(count)) : text
    readonly property color softBg: {
        switch (variant) {
        case "accent": return Theme.accentBg
        case "danger": return Theme.dangerBg
        case "warning": return Theme.warningBg
        case "success": return Theme.successBg
        case "info": return Theme.infoBg
        default: return Theme.chipNeutralBg
        }
    }
    readonly property color strong: {
        switch (variant) {
        case "accent": return Theme.accentSolid
        case "danger": return Theme.danger
        case "warning": return Theme.warning
        case "success": return Theme.success
        case "info": return Theme.info
        default: return Theme.fgMuted
        }
    }
    readonly property color softFg: {
        switch (variant) {
        case "accent": return Theme.accentText
        case "danger": return Theme.dangerText
        case "warning": return Theme.warningText
        case "success": return Theme.successText
        case "info": return Theme.infoText
        default: return Theme.fgMuted
        }
    }

    visible: dot || label !== "" && (count !== 0 || showZero)
    implicitHeight: dot ? 8 : (Theme.touchUi ? 20 : 18)
    implicitWidth: dot ? 8 : Math.max(implicitHeight, labelText.implicitWidth + 12)
    radius: height / 2
    color: dot ? (variant === "accent" ? Theme.accent : strong) : solid ? strong : softBg
    antialiasing: true

    Accessible.role: Accessible.StaticText
    Accessible.name: label

    Text {
        id: labelText
        visible: !badge.dot
        anchors.centerIn: parent
        text: badge.label
        // On solid fills: white on the dark fills (accent, danger, light-theme success/info/
        // neutral), ink on the light ones (amber, and the pale dark-theme tints).
        color: !badge.solid ? badge.softFg
             : badge.variant === "accent" || badge.variant === "danger" || (!Theme.dark && badge.variant !== "warning")
               ? "#ffffff" : Theme.ink
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs
        font.weight: Theme.weightSemiBold
        font.features: { "tnum": 1 }
    }
}
