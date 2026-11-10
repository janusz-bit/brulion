pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellStatusBar — the thin bar along the bottom of wide windows (docs/UI.md "Status bar"; the
// "Nocny" look, IDE-like): on the left the sync status — a dot (green: synced, amber: syncing, red:
// failed, grey: only on this device) and its words, a click opens the sync center —; on the right
// what the view reports (ShellRouteView.viewStatusPosition / viewStatusText: the edited block, the
// word count) and the command palette's key. Only what there is: an empty part leaves no gap.
//
//   ShellStatusBar { routeView: shell.routeView; onPaletteRequested: shell.openPalette("") }
Rectangle {
    id: bar

    /// The focused pane's view (its texts).
    property ShellRouteView routeView: null
    signal paletteRequested()

    readonly property string position: routeView ? routeView.viewStatusPosition : ""
    readonly property string words: routeView ? routeView.viewStatusText : ""
    readonly property string paletteKey: ShortcutMap.sequences["palette"] || ""

    implicitHeight: Theme.dense ? 22 : 24
    color: Theme.bgSunken
    Accessible.role: Accessible.StatusBar
    Accessible.name: qsTr("Status bar")

    // The sync status's words and state (the rail's button computes them).
    ShellSyncFooter {
        id: sync
        visible: false
    }
    readonly property color dotColor: sync.syncState === "idle" ? Theme.success
                                      : sync.syncState === "syncing" ? Theme.warning
                                      : sync.syncState === "error" ? Theme.dangerText
                                      : sync.syncState === "offline" ? Theme.warningText
                                      : Theme.fgFaint
    readonly property string syncText: sync.syncState === "idle" && AppController.routeText(AppController.syncStatus, "text") === ""
                                       ? qsTr("synced") : sync.statusText

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space3
        spacing: Theme.space3

        // ---- Sync --------------------------------------------------------------------------
        Item {
            objectName: "statusBarSync"
            Layout.fillHeight: true
            Layout.preferredWidth: syncRow.implicitWidth + 2 * Theme.space1
            Layout.maximumWidth: bar.width / 2
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Sync: %1").arg(sync.statusText)
            Accessible.onPressAction: AppController.navigate({ view: "sync" })

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 2
                anchors.bottomMargin: 1
                visible: syncHover.hovered
                radius: Theme.radiusXs
                color: Theme.bgHover
            }
            Row {
                id: syncRow
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.space1
                spacing: Theme.space1 + 2
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: bar.dotColor
                }
                BText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: bar.syncText
                    font.pixelSize: Theme.textXs
                    color: sync.syncState === "idle" ? Theme.successText
                         : sync.syncState === "error" ? Theme.dangerText : Theme.fgMuted
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, bar.width / 2 - 24)
                }
            }
            HoverHandler { id: syncHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: AppController.navigate({ view: "sync" }) }
        }

        Item { Layout.fillWidth: true }

        // ---- The view's texts ----------------------------------------------------------------
        BText {
            objectName: "statusBarPosition"
            visible: bar.position !== ""
            text: bar.position
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
            color: Theme.fgMuted
        }
        BText {
            objectName: "statusBarWords"
            visible: bar.words !== ""
            Layout.maximumWidth: bar.width / 3
            text: bar.words
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
            color: Theme.fgMuted
            elide: Text.ElideRight
        }
        // The palette's key: a quiet way in for those who never heard of it.
        Item {
            objectName: "statusBarPalette"
            visible: bar.paletteKey !== ""
            Layout.fillHeight: true
            Layout.preferredWidth: paletteRow.implicitWidth + 2 * Theme.space1
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Command palette")
            Accessible.onPressAction: bar.paletteRequested()

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 2
                anchors.bottomMargin: 1
                visible: paletteHover.hovered
                radius: Theme.radiusXs
                color: Theme.bgHover
            }
            Row {
                id: paletteRow
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.space1
                spacing: Theme.space1
                BKbd {
                    anchors.verticalCenter: parent.verticalCenter
                    sequence: bar.paletteKey
                    small: true
                }
                BText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("commands")
                    font.pixelSize: Theme.textXs
                    color: Theme.fgFaint
                }
            }
            HoverHandler { id: paletteHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: bar.paletteRequested() }
        }
    }
}
