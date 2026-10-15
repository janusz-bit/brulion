pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// HistoryView — route "history" ({path, version?}): the versions of one page kept on this
// device (HistoryModel: the text before each change, newest first, grouped by day, with what
// replaced it — your edit, another program, sync, an import — and the size) and, for the chosen
// version, a diff against the page as it is now (BDiffView: side by side on wide windows) with
// "Przywróć tę wersję" (confirmed; the current text becomes a version itself, so a restore can be
// undone the same way). Wide windows show the list and the diff next to each other, phones one
// after the other (back returns to the list). A deleted page still has its versions: restoring
// one brings the page back.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string path: route && route.path ? route.path : ""
    /// Daily notes by their date, other pages by their title.
    readonly property string pageLabel: {
        const day = path !== "" ? Workspace.journalDate(path) : ""
        return day !== "" ? DatesHelper.formatLong(day) : Workspace.pageTitle(path)
    }
    readonly property bool wide: width >= 820
    /// Row of the version shown (-1: none).
    property int selected: -1
    /// Phones: the diff is shown instead of the list.
    property bool showingDiff: false
    readonly property var version: selected >= 0 && history.count > selected ? history.get(selected) : ({})
    readonly property string versionText: selected >= 0 && history.count > selected ? history.textAt(selected) : ""

    function select(row) {
        selected = row
        if (!wide)
            showingDiff = true
    }

    focus: true

    HistoryModel {
        id: history
        workspace: Workspace
        path: root.path
        locale: DatesHelper.locale
        today: DatesHelper.today
        onRefreshed: {
            if (root.selected >= count)
                root.selected = count - 1
            if (root.selected < 0 && count > 0 && root.wide) {
                const wanted = root.route && root.route.version ? rowOf(root.route.version) : -1
                root.selected = wanted >= 0 ? wanted : 0
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    // ---- The page and what is shown -------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.rightMargin: Theme.compact ? Theme.space3 : Theme.space6
            Layout.topMargin: Theme.compact ? Theme.space2 : Theme.space4
            Layout.bottomMargin: Theme.space2
            spacing: Theme.space2

            BIconButton {
                visible: !root.wide && root.showingDiff
                iconName: "arrow-left"
                label: qsTr("Back to the versions")
                onClicked: root.showingDiff = false
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                BText {
                    Layout.fillWidth: true
                    text: root.pageLabel
                    variant: "heading"
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                BText {
                    Layout.fillWidth: true
                    text: !history.exists ? qsTr("This page was deleted. Restoring a version brings it back.")
                          : history.count > 0 ? qsTr("%n saved version(s) on this device", "", history.count)
                          : qsTr("Version history")
                    variant: "small"
                    color: history.exists ? Theme.fgMuted : Theme.warningText
                    wrapMode: Text.Wrap
                }
            }
            BButton {
                visible: history.exists && (root.wide || !root.showingDiff)
                size: "small"
                iconName: "file-text"
                text: Theme.compact ? "" : qsTr("Open page")
                Accessible.name: qsTr("Open page")
                onClicked: AppController.openPage(root.path)
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        // The empty state takes the rest of the height (an item with a maximum height of its own
        // left the column's spare room to the header, which grew a large empty band).
        Item {
            visible: history.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            BEmptyState {
                anchors.centerIn: parent
                maximumWidth: 460
                iconName: "history"
                title: history.exists ? qsTr("No earlier versions yet") : qsTr("No versions of this page")
                description: qsTr("When a page changes — by you, by sync or by another program — Brulion keeps the previous text on this device (while you type, at most one version every few minutes).")
            }
        }

        RowLayout {
            visible: history.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- Versions ---------------------------------------------------------------------------
            ListView {
                id: versions
                visible: root.wide || !root.showingDiff
                Layout.fillHeight: true
                Layout.fillWidth: !root.wide
                Layout.preferredWidth: root.wide ? 320 : -1
                clip: true
                focus: true
                model: history
                currentIndex: root.selected
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                bottomMargin: Theme.space6
                topMargin: Theme.space2
                section.property: "day"
                section.criteria: ViewSection.FullString
                section.delegate: BText {
                    required property string section
                    width: versions.width
                    leftPadding: Theme.space4
                    topPadding: Theme.space3
                    bottomPadding: Theme.space1
                    text: section
                    variant: "caption"
                    font.weight: Theme.weightSemiBold
                    color: Theme.fgFaint
                }
                T.ScrollBar.vertical: BScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Versions")
                onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex !== root.selected && root.wide) root.selected = currentIndex
                Keys.onReturnPressed: root.select(currentIndex)

                delegate: BListItem {
                    id: v
                    required property int index
                    required property var model
                    // Full width (ListView places delegates at x 0); the row itself is inset.
                    width: versions.width
                    leftInset: Theme.space2
                    rightInset: Theme.space2
                    leftPadding: Theme.space2 + (Theme.touchUi ? 14 : 10)
                    rightPadding: Theme.space2 + (Theme.touchUi ? 12 : 8)
                    iconName: v.model.reasonIcon
                    text: v.model.clock + "  ·  " + v.model.reasonLabel
                    subtitle: v.model.sizeLabel + (v.model.newest ? " · " + qsTr("the text before the latest change") : "")
                    selected: root.wide && root.selected === v.index
                    selectionBar: true
                    onClicked: root.select(v.index)
                }

            }
            Rectangle {
                visible: root.wide
                Layout.fillHeight: true
                implicitWidth: 1
                color: Theme.borderSubtle
            }

            // ---- The chosen version against now ---------------------------------------------------------
            ColumnLayout {
                visible: root.wide || root.showingDiff
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.space2

                RowLayout {
                    visible: root.selected >= 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.compact ? Theme.space3 : Theme.space5
                    Layout.rightMargin: Theme.compact ? Theme.space3 : Theme.space5
                    Layout.topMargin: Theme.space3
                    spacing: Theme.space2
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        BText {
                            Layout.fillWidth: true
                            text: root.version.when ? qsTr("Version: %1").arg(root.version.when) : ""
                            variant: "label"
                            elide: Text.ElideRight
                        }
                        BText {
                            Layout.fillWidth: true
                            readonly property var stats: DiffHelper.stats(root.versionText, history.currentText)
                            text: history.exists
                                  ? (stats.added + stats.removed === 0 ? qsTr("Same as now")
                                     : qsTr("Since then: %n line(s) added", "", stats.added) + ", " + qsTr("%n removed", "", stats.removed))
                                  : qsTr("The page does not exist now")
                            variant: "small"
                            color: Theme.fgMuted
                            elide: Text.ElideRight
                        }
                    }
                    BButton {
                        variant: "primary"
                        iconName: "undo-2"
                        text: qsTr("Restore this version")
                        enabled: root.versionText !== history.currentText || !history.exists
                        onClicked: restoreDialog.open()
                    }
                }

                BDiffView {
                    visible: root.selected >= 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: Theme.compact ? Theme.space2 : Theme.space4
                    Layout.rightMargin: Theme.compact ? Theme.space2 : Theme.space4
                    Layout.bottomMargin: Theme.space3
                    oldText: root.versionText
                    newText: history.currentText
                    oldLabel: root.version.when ? qsTr("Version: %1").arg(root.version.when) : qsTr("Version")
                    newLabel: history.exists ? qsTr("Now") : qsTr("Deleted")
                }
                Item {
                    visible: root.selected < 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    BEmptyState {
                        anchors.centerIn: parent
                        visible: history.count > 0
                        compact: true
                        iconName: "git-compare"
                        title: qsTr("Choose a version")
                        description: qsTr("Its differences to the page as it is now appear here.")
                    }
                }
            }
        }
    }

    BConfirmDialog {
        id: restoreDialog
        parent: T.Overlay.overlay
        title: qsTr("Restore this version?")
        message: qsTr("“%1” gets the text from %2 back.").arg(root.pageLabel).arg(root.version.when || "")
        detail: qsTr("The current text is kept as a version, so you can go back to it.")
        confirmText: qsTr("Restore")
        onAccepted: {
            const when = root.version.when
            if (history.restore(root.selected)) {
                AppController.toast(qsTr("Restored the version from %1").arg(when), "success")
                root.selected = 0
            } else {
                AppController.toast(qsTr("The version could not be restored."), "error")
            }
        }
    }
}
