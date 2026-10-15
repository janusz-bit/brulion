pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// SyncConflictResolver — one conflict copy (ConflictResolver): which note, where the other version
// came from, and the ways out:
//   * the copy is identical / an older version already contained in the note → remove the copy;
//   * a three-way merge with an ancestor from this device's history merged everything → review
//     the result (BDiffView "now" → "after merging") and "Scal";
//   * collisions left (or no ancestor) → BMergeView, hunk by hunk ("Scal" when all are chosen);
//   * "Porównaj" shows both versions side by side (BDiffView);
//   * always: "Zachowaj moją" (the copy goes to the trash) and "Zachowaj kopię" (its content
//     replaces the note; the old text stays in the history). Binary files: only these two.
//
//   SyncConflictResolver { copy: list.currentCopy; showBack: Theme.narrow; onClosed: … }
ColumnLayout {
    id: view

    property string copy
    /// A back button in the header (narrow windows show the list and the resolver one at a time).
    property bool showBack: false
    /// "merge" | "compare"
    property string mode: "merge"
    /// Decide every difference by hand, ignoring the automatic three-way merge.
    property bool manual: false

    readonly property alias resolver: resolverObject
    readonly property var conflict: resolverObject.conflict
    readonly property string otherLabel: conflict.device ? qsTr("Copy from %1").arg(conflict.device) : qsTr("Copy")
    readonly property bool textConflict: resolverObject.loaded && resolverObject.text && !resolverObject.identical
    readonly property bool cleanMerge: textConflict && resolverObject.clean && !view.manual

    signal closed()

    onCopyChanged: {
        mode = "merge"
        manual = false
    }

    spacing: Theme.space3

    ConflictResolver {
        id: resolverObject
        workspace: Workspace
        copy: view.copy
        onResolved: (message) => {
            AppController.toast(message, "success")
            view.closed()
        }
        onFailed: (message) => AppController.toast(message, "error")
    }

    // ---- Header ----------------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BIconButton {
            visible: view.showBack
            Layout.alignment: Qt.AlignTop
            iconName: "arrow-left"
            label: qsTr("Back to the list")
            onClicked: view.closed()
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            BText {
                Layout.fillWidth: true
                text: view.conflict.title || ""
                variant: "heading"
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BText {
                Layout.fillWidth: true
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
                text: {
                    const c = view.conflict
                    if (!c.copy)
                        return ""
                    const parts = []
                    const folder = Workspace.folderOf(c.original)
                    if (folder !== "")
                        parts.push(folder)
                    parts.push(c.source === "syncthing" ? "Syncthing" : "Nextcloud")
                    if (c.device)
                        parts.push(qsTr("other version from “%1”").arg(c.device))
                    if (c.when)
                        parts.push(c.when)
                    return parts.join(" · ")
                }
            }
        }
        BButton {
            visible: !Theme.compact && view.conflict.originalExists === true && resolverObject.text
            text: qsTr("Open the note")
            iconName: "external-link"
            size: "small"
            variant: "ghost"
            onClicked: AppController.openPage(view.conflict.original)
        }
    }

    // Phones need the room for the merge itself; the overview explains conflicts already.
    BText {
        Layout.fillWidth: true
        visible: resolverObject.loaded && (!Theme.compact || view.conflict.originalExists === false || !resolverObject.text)
        variant: "muted"
        wrapMode: Text.Wrap
        text: view.conflict.originalExists === false
              ? qsTr("The note was deleted on this device while it changed on another one. The other version is in the copy.")
              : !resolverObject.text
                ? qsTr("This file changed on two devices at the same time. Nothing is lost until you decide.")
                : qsTr("This note changed on two devices at the same time. Your version is in the note, the other one in a copy next to it. Nothing is lost until you decide.")
    }

    // ---- Simple cases ---------------------------------------------------------------------------
    IoCard {
        visible: resolverObject.loaded && (resolverObject.identical || resolverObject.copyIsOlder)
        tone: "success"
        iconName: "circle-check"
        iconColor: Theme.successText
        title: resolverObject.identical ? qsTr("The copy is the same as the note")
                                        : qsTr("The copy is an older version of this note")
        subtitle: resolverObject.identical ? qsTr("Nothing differs (line endings aside). The copy can simply go.")
                                           : qsTr("Everything in it is already contained in your version. Keeping yours loses nothing.")
        BButton {
            text: qsTr("Remove the copy")
            iconName: "trash-2"
            variant: "primary"
            onClicked: resolverObject.keepMine()
        }
    }

    IoCard {
        visible: resolverObject.loaded && !resolverObject.text && !resolverObject.identical
        iconName: "file"
        title: qsTr("Two versions of a file")
        subtitle: qsTr("This file cannot be merged line by line. Keep one of the versions; the other goes to the trash.")
        GridLayout {
            Layout.fillWidth: true
            columns: Theme.compact ? 1 : 2
            columnSpacing: Theme.space3
            rowSpacing: Theme.space2
            Repeater {
                model: resolverObject.loaded && !resolverObject.text ? [
                    { label: qsTr("This device"), size: resolverObject.oursSizeText, path: view.conflict.original },
                    { label: view.otherLabel, size: resolverObject.theirsSizeText, path: view.conflict.copy }
                ] : []
                delegate: Rectangle {
                    id: side
                    required property var modelData
                    readonly property bool picture: /\.(png|jpe?g|gif|webp|svg|bmp)$/i.test(modelData.path)
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: sideColumn.implicitHeight + 2 * Theme.space3
                    radius: Theme.radius
                    color: Theme.bgSunken
                    ColumnLayout {
                        id: sideColumn
                        anchors.fill: parent
                        anchors.margins: Theme.space3
                        spacing: Theme.space2
                        Image {
                            id: picture
                            Layout.fillWidth: true
                            Layout.preferredHeight: status === Image.Ready ? Math.min(180, implicitHeight) : 0
                            visible: side.picture && status === Image.Ready
                            source: side.picture && side.modelData.path ? Workspace.assetUrl(side.modelData.path) : ""
                            sourceSize.height: 360
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                        }
                        ColumnLayout {
                            spacing: 2
                            BText { text: side.modelData.label; variant: "label" }
                            BText { text: side.modelData.size; variant: "small"; color: Theme.fgMuted }
                        }
                    }
                }
            }
        }
    }

    // ---- Text: merge or compare -------------------------------------------------------------------
    BSegmented {
        id: modeSwitch
        visible: resolverObject.loaded && resolverObject.text && !resolverObject.identical
        Layout.fillWidth: Theme.compact
        fill: Theme.compact
        accessibleName: qsTr("Show")
        model: [
            { value: "merge", text: qsTr("Merge"), icon: "git-merge" },
            { value: "compare", text: qsTr("Compare"), icon: "git-compare" }
        ]
        currentValue: view.mode
        onActivated: (value) => view.mode = value
    }

    Rectangle {
        Layout.fillWidth: true
        visible: view.textConflict && view.mode === "merge"
        implicitHeight: bannerGrid.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: view.cleanMerge ? Theme.successBg : Theme.infoBg
        GridLayout {
            id: bannerGrid
            anchors.fill: parent
            anchors.margins: Theme.space3
            columns: Theme.compact ? 2 : 3
            columnSpacing: Theme.space2
            rowSpacing: Theme.space1
            BIcon {
                Layout.row: 0
                Layout.column: 0
                Layout.alignment: Qt.AlignTop
                name: view.cleanMerge ? "sparkles" : "info"
                size: Theme.iconSize
                color: view.cleanMerge ? Theme.successText : Theme.infoText
            }
            ColumnLayout {
                Layout.row: 0
                Layout.column: 1
                Layout.fillWidth: true
                spacing: 2
                BText {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: {
                        if (view.cleanMerge)
                            return qsTr("The changes of both versions fit together. Check your note after merging below, then merge.")
                        if (resolverObject.hasBase && !view.manual)
                            return qsTr("Changes made on one side only are merged already. Decide the %n place(s) changed on both sides.", "", resolverObject.collisionCount)
                        return qsTr("For every difference choose which version to keep – or both, or edit the text yourself.")
                    }
                }
                BText {
                    Layout.fillWidth: true
                    visible: resolverObject.hasBase && !view.manual
                    text: qsTr("Compared with the common version from %1 in this device's history.").arg(DatesHelper.formatHuman(resolverObject.baseWhen))
                    variant: "small"
                    color: Theme.fgMuted
                    wrapMode: Text.Wrap
                }
            }
            BButton {
                visible: resolverObject.hasBase
                Layout.row: Theme.compact ? 1 : 0
                Layout.column: Theme.compact ? 1 : 2
                Layout.alignment: Theme.compact ? Qt.AlignLeft : Qt.AlignVCenter
                text: view.manual ? qsTr("Use the automatic merge") : qsTr("Decide myself")
                size: "small"
                variant: "ghost"
                onClicked: view.manual = !view.manual
            }
        }
    }

    // Clean three-way merge: review it.
    BDiffView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 200
        visible: view.cleanMerge && view.mode === "merge"
        oldText: resolverObject.ours
        newText: resolverObject.mergedText
        oldLabel: qsTr("Now")
        newLabel: qsTr("After merging")
    }
    BButton {
        visible: view.cleanMerge && view.mode === "merge"
        Layout.fillWidth: Theme.compact
        //: Polish: "Scal"
        text: qsTr("Merge")
        iconName: "git-merge"
        variant: "primary"
        onClicked: resolverObject.applyMerged(resolverObject.mergedText)
    }

    // Collisions: hunk by hunk.
    BMergeView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 260
        visible: view.textConflict && !view.cleanMerge && view.mode === "merge"
        ours: resolverObject.ours
        theirs: resolverObject.theirs
        hunks: resolverObject.hasBase && !view.manual ? resolverObject.hunks : DiffHelper.conflictHunks(resolverObject.ours, resolverObject.theirs)
        oursLabel: qsTr("This device")
        theirsLabel: view.otherLabel
        applyText: qsTr("Merge")
        onResolved: (text) => resolverObject.applyMerged(text)
    }

    BDiffView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 200
        visible: view.textConflict && view.mode === "compare"
        oldText: resolverObject.ours
        newText: resolverObject.theirs
        oldLabel: qsTr("This device")
        newLabel: view.otherLabel
    }

    // Binary files and the simple cases take no room below; text views fill it.
    Item {
        Layout.fillHeight: true
        visible: !view.textConflict
    }

    // ---- Always ----------------------------------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        visible: resolverObject.loaded
        spacing: Theme.space2
        BButton {
            //: Polish: "Zachowaj moją" (keep my version of the note)
            text: view.conflict.originalExists === false ? qsTr("Keep it deleted") : qsTr("Keep mine")
            iconName: "check"
            onClicked: resolverObject.keepMine()
        }
        BButton {
            //: Polish: "Zachowaj kopię"
            text: view.conflict.originalExists === false ? qsTr("Restore from the copy") : qsTr("Keep the copy")
            iconName: "copy"
            onClicked: view.conflict.originalExists === false ? resolverObject.keepCopy() : keepCopyDialog.open()
        }
    }

    BConfirmDialog {
        id: keepCopyDialog
        title: qsTr("Keep the copy?")
        message: view.conflict.text === false
                 ? qsTr("“%1” gets the content of the copy. Your current version goes to the trash.").arg(view.conflict.title || "")
                 : qsTr("“%1” gets the content of the copy. Your current version stays in the note's history.").arg(view.conflict.title || "")
        confirmText: qsTr("Keep the copy")
        onAccepted: resolverObject.keepCopy()
    }
}
