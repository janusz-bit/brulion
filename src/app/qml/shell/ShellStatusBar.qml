pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellStatusBar — the thin bar along the bottom of wide windows (docs/UI.md "Status bar"; the
// "Nocny" look, IDE-like): on the left the sync status — a dot (green: synced, amber: syncing, red:
// failed, grey: only on this device) and its words, a click opens the sync center —, Vim's mode or
// command line while the view's editor has Vim's keys (ShellRouteView.viewStatusVim); on the right
// what the view reports (ShellRouteView.viewStatusPosition / viewStatusText: the edited block, the
// word count), the extensions' texts (b.app.contribute("statusbar", …): an icon, the label in its
// tone; one that does something is a button — hover, focus ring, Enter / Space, a click runs it:
// docs/UI.md "Extension items in the UI") and the command palette's key. Only what there is: an
// empty part leaves no gap.
//
//   ShellStatusBar { routeView: shell.routeView; onPaletteRequested: shell.openPalette("") }
Rectangle {
    id: bar

    /// The focused pane's view (its texts).
    property ShellRouteView routeView: null
    signal paletteRequested()

    readonly property string position: routeView ? routeView.viewStatusPosition : ""
    readonly property string words: routeView ? routeView.viewStatusText : ""
    /// Vim's mode, the command typed or the command line (docs/UI.md "Vim mode").
    readonly property string vim: routeView ? routeView.viewStatusVim : ""
    readonly property string paletteKey: ShortcutMap.sequences["palette"] || ""
    readonly property list<var> noRows: []
    /// The extensions' status bar items as text: made again only when they change.
    readonly property string itemsKey: Workspace.extensions !== null
                                       ? Workspace.extensions.contributionsKey("statusbar", Workspace.extensions.appRevision) : ""
    /// The text colour of an extension item's `tone`: accent, success, danger, warning, info.
    function toneColor(tone: string): color {
        switch (tone) {
        case "accent": return Theme.accentText
        case "success": return Theme.successText
        case "danger": return Theme.dangerText
        case "warning": return Theme.warningText
        case "info": return Theme.infoText
        default: return Theme.fgMuted
        }
    }

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

        // ---- Vim's mode and command line (as Vim shows them at the bottom left) ----------------
        BText {
            objectName: "statusBarVim"
            visible: bar.vim !== ""
            Layout.maximumWidth: bar.width / 3
            text: bar.vim
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
            font.bold: true
            color: Theme.accentText
            elide: Text.ElideRight
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
        // ---- The extensions' texts ---------------------------------------------------------
        Repeater {
            model: bar.itemsKey !== "" ? Workspace.extensions.contributions("statusbar", bar.itemsKey.length) : bar.noRows
            delegate: T.AbstractButton {
                id: extensionText
                required property var modelData
                required property string extension
                required property string label
                required property string tooltip
                required property string tone
                required property string command
                required property string view
                required property string page
                required property string run
                readonly property string itemId: UiModels.stringField(extensionText.modelData, "id")
                readonly property string iconName: UiModels.stringField(extensionText.modelData, "icon")
                /// It does something when clicked (else a plain text).
                readonly property bool active: extensionText.command !== "" || extensionText.view !== ""
                                               || extensionText.page !== "" || extensionText.run !== ""
                objectName: "statusBar_" + extensionText.itemId
                Layout.fillHeight: true
                Layout.maximumWidth: bar.width / 4
                implicitWidth: extensionRow.implicitWidth + 2 * Theme.space1
                // (Enabled either way: a plain text's tooltip shows on hover too.)
                hoverEnabled: !Theme.touchUi
                // (A click leaves the keyboard focus where it was, as the top bar's extension buttons.)
                focusPolicy: extensionText.active ? Qt.TabFocus : Qt.NoFocus
                Accessible.role: extensionText.active ? Accessible.Button : Accessible.StaticText
                Accessible.name: extensionText.label
                Accessible.description: extensionText.tooltip !== extensionText.label ? extensionText.tooltip : ""
                onClicked: {
                    if (extensionText.active)
                        AppScripting.activate("statusbar", extensionText.extension, extensionText.itemId, "", -1)
                }

                background: Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 2
                    anchors.bottomMargin: 1
                    radius: Theme.radiusXs
                    color: !extensionText.active ? "transparent"
                           : extensionText.down ? Theme.bgPressed : extensionText.hovered ? Theme.bgHover : "transparent"
                    BFocusRing {
                        visible: extensionText.visualFocus
                        targetRadius: Theme.radiusXs
                        inset: true
                    }
                }
                contentItem: Item {}
                Row {
                    id: extensionRow
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.space1
                    spacing: Theme.space1
                    BIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: extensionText.iconName !== ""
                        name: extensionText.iconName
                        size: Theme.iconSizeSm - 2
                        color: extensionText.tone !== "" ? bar.toneColor(extensionText.tone) : Theme.fgMuted
                    }
                    BText {
                        anchors.verticalCenter: parent.verticalCenter
                        text: extensionText.label
                        font.pixelSize: Theme.textXs
                        color: extensionText.tone !== "" ? bar.toneColor(extensionText.tone) : Theme.fgMuted
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, bar.width / 4 - 2 * Theme.space1
                                        - (extensionText.iconName !== "" ? Theme.iconSizeSm + Theme.space1 : 0))
                    }
                }
                HoverHandler {
                    enabled: extensionText.active
                    cursorShape: Qt.PointingHandCursor
                }
                Loader {
                    active: extensionText.hovered && extensionText.tooltip !== extensionText.label
                    sourceComponent: BToolTip {
                        parent: extensionText
                        text: extensionText.tooltip
                        visible: extensionText.hovered
                    }
                }
            }
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
