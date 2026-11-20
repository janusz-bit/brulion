pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// VimStatus — Vim's mode, the command being typed, the command line or the last message of an
// editor (VimSession; docs/UI.md "Vim mode"), as a small bar in a corner of its view where the
// shell's status bar does not show them (narrow windows, the side panel, focus mode, a page's own
// window). The mode is a badge: normal quiet, insert in the accent's colour, visual amber, replace
// red; the command line in the editor's monospace font.
//
//   VimStatus { session: editor.vimSession; anchors.left: parent.left; anchors.bottom: parent.bottom }
Rectangle {
    id: bar

    property VimSession session: null
    readonly property string mode: session !== null ? session.mode : ""
    readonly property bool commandLine: session !== null && session.commandLine !== ""
    readonly property string detail: session === null ? ""
                                     : commandLine ? session.commandLine
                                     : session.pending !== "" ? session.pending : session.message
    readonly property bool error: session !== null && !commandLine && session.pending === "" && session.messageError

    implicitWidth: row.implicitWidth + 2 * Theme.space2
    implicitHeight: Math.max(modeBadge.height, detailText.implicitHeight) + 2 * Theme.space1
    radius: Theme.radiusSm
    // Opaque: the page's text scrolls under it.
    color: Theme.bgElevated
    border.color: Theme.borderSubtle
    Accessible.role: Accessible.StatusBar
    Accessible.name: qsTr("Vim: %1").arg(session !== null ? session.status : "")

    Row {
        id: row
        x: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space2
        Rectangle {
            id: modeBadge
            objectName: "vimModeBadge"
            visible: !bar.commandLine
            anchors.verticalCenter: parent.verticalCenter
            width: modeText.implicitWidth + 2 * Theme.space1 + 2
            height: modeText.implicitHeight + 2
            radius: 3
            color: bar.mode === "insert" ? Theme.accentSolid
                 : bar.mode === "visual" || bar.mode === "visualLine" ? Theme.warningBg
                 : bar.mode === "replace" ? Theme.dangerBg : Theme.bgSunken
            BText {
                id: modeText
                anchors.centerIn: parent
                text: bar.session !== null ? bar.session.modeLabel : ""
                font.family: Theme.fontMono
                font.pixelSize: Theme.textXs
                font.bold: true
                color: bar.mode === "insert" ? Theme.fgOnAccent
                     : bar.mode === "visual" || bar.mode === "visualLine" ? Theme.warningText
                     : bar.mode === "replace" ? Theme.dangerText : Theme.fgMuted
            }
        }
        BText {
            id: detailText
            objectName: "vimStatusDetail"
            visible: text !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 420)
            text: bar.detail
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
            color: bar.error ? Theme.dangerText : Theme.fg
            elide: Text.ElideRight
        }
    }
}
