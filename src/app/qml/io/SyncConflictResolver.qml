pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SyncConflictResolver — one conflict copy (ConflictResolver): which note, where the other version
// came from, and the ways out:
//   * the copy is identical / an older version already contained in the note → remove the copy;
//   * a three-way merge with an ancestor from this device's history merged everything → review
//     the result (BDiffView "now" → "after merging") and "Scal";
//   * collisions left (or no ancestor) → BMergeView, hunk by hunk ("Scal" when all are chosen);
//   * "Porównaj" shows both versions side by side (BDiffView);
//   * a drawing (never merged line by line): previews of both versions and of their merge
//     (read-only DrawingCanvas items; a drawing Brulion does not draw as its picture), the
//     elements of each and "Scal rysunki" — the union of both, written into the drawing;
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
        BButton {
            visible: !Theme.compact && view.conflict.originalExists === true && resolverObject.drawing
            text: qsTr("Open the drawing")
            iconName: "pen-tool"
            size: "small"
            variant: "ghost"
            onClicked: AppController.navigate({ view: "drawing", path: view.conflict.original })
        }
    }

    // Phones need the room for the merge itself; the overview explains conflicts already (and the
    // card of a drawing what merging does).
    BText {
        Layout.fillWidth: true
        visible: resolverObject.loaded && (!Theme.compact || view.conflict.originalExists === false
                                           || (!resolverObject.text && !drawingScroll.visible))
        variant: "muted"
        wrapMode: Text.Wrap
        text: view.conflict.originalExists === false
              ? (resolverObject.drawing
                 ? qsTr("The drawing was deleted on this device while it changed on another one. The other version is in the copy.")
                 : qsTr("The note was deleted on this device while it changed on another one. The other version is in the copy."))
              : resolverObject.drawing
                ? qsTr("This drawing changed on two devices at the same time. Your version is in the drawing, the other one in a copy next to it. Nothing is lost until you decide.")
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
        title: resolverObject.drawing
               ? (resolverObject.identical ? qsTr("The copy has the same drawing")
                                           : qsTr("Everything in the copy is already in the drawing"))
               : resolverObject.identical ? qsTr("The copy is the same as the note")
                                          : qsTr("The copy is an older version of this note")
        subtitle: resolverObject.drawing
                  ? (resolverObject.identical ? qsTr("Nothing differs. The copy can simply go.")
                                              : qsTr("Keeping yours loses nothing."))
                  : resolverObject.identical ? qsTr("Nothing differs (line endings aside). The copy can simply go.")
                                             : qsTr("Everything in it is already contained in your version. Keeping yours loses nothing.")
        BButton {
            text: qsTr("Remove the copy")
            iconName: "trash-2"
            variant: "primary"
            onClicked: resolverObject.keepMine()
        }
    }

    IoCard {
        visible: resolverObject.loaded && !resolverObject.text && !resolverObject.identical && !resolverObject.drawing
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
                    // The pictures the app shows as pictures (one list: paths::pictureExtensions()).
                    readonly property bool picture: Workspace.isPicture(modelData.path)
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

    // ---- Drawings: both versions and their merge ---------------------------------------------------
    // (In a Flickable: three previews may be taller than a phone's screen.)
    Flickable {
        id: drawingScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: resolverObject.loaded && resolverObject.drawing && !resolverObject.identical
                 && !resolverObject.copyIsOlder && view.conflict.originalExists !== false
        contentWidth: width
        contentHeight: drawingCard.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        IoCard {
            id: drawingCard
            readonly property bool mergeable: resolverObject.canMergeDrawings
            readonly property var counts: resolverObject.drawingCounts
            width: drawingScroll.width
            iconName: "pen-tool"
            title: qsTr("Two versions of a drawing")
            subtitle: mergeable
                      ? qsTr("Merging keeps the elements of both versions. Elements erased on one device only come back – erase them again.")
                      : qsTr("Brulion cannot merge these drawings: one has elements it does not edit (such as frames) or cannot be read as a drawing. Keep one of the versions.")
            GridLayout {
                Layout.fillWidth: true
                columns: drawingCard.mergeable && !Theme.compact ? 3 : 2
                columnSpacing: Theme.space3
                rowSpacing: Theme.space3
                DrawingTile {
                    active: drawingScroll.visible
                    label: qsTr("This device")
                    bytes: resolverObject.oursBytes
                    path: view.conflict.original || ""
                    detail: drawingCard.mergeable ? qsTr("%n element(s)", "", drawingCard.counts.ours || 0)
                                                  : resolverObject.oursSizeText
                }
                DrawingTile {
                    active: drawingScroll.visible
                    label: view.otherLabel
                    bytes: resolverObject.theirsBytes
                    path: view.conflict.copy || ""
                    detail: drawingCard.mergeable ? qsTr("%n element(s)", "", drawingCard.counts.theirs || 0)
                                                  : resolverObject.theirsSizeText
                }
                DrawingTile {
                    active: drawingScroll.visible
                    visible: drawingCard.mergeable
                    Layout.columnSpan: Theme.compact ? 2 : 1
                    canvasHeight: Theme.compact ? 140 : 220
                    result: true
                    label: qsTr("After merging")
                    bytes: resolverObject.mergedBytes
                    detail: drawingCard.mergeable
                            ? qsTr("%n element(s)", "", drawingCard.counts.merged || 0) + " · "
                              + qsTr("%1 in both, %2 only here, %3 only in the copy")
                                    .arg(drawingCard.counts.common || 0).arg(drawingCard.counts.onlyOurs || 0)
                                    .arg(drawingCard.counts.onlyTheirs || 0)
                            : ""
                }
            }
            BButton {
                objectName: "mergeDrawingsButton"
                visible: drawingCard.mergeable
                Layout.fillWidth: Theme.compact
                //: Polish: "Scal rysunki"
                text: qsTr("Merge drawings")
                iconName: "git-merge"
                variant: "primary"
                onClicked: resolverObject.mergeDrawings()
            }
        }
    }

    // A preview of one version (or of the merge): the drawing as the editor shows it, read-only;
    // one Brulion does not draw (frames, a picture without a scene) as its picture.
    component DrawingTile: Rectangle {
        id: tile
        property string label
        property string detail
        property var bytes
        /// The file (its picture when the canvas cannot show it); "" for the merge.
        property string path
        /// The merge: framed in the accent colour.
        property bool result: false
        property int canvasHeight: Theme.compact ? 120 : 220
        /// Shown: only then the drawing is loaded (or its picture asked for).
        property bool active: true
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        implicitHeight: tileColumn.implicitHeight + 2 * Theme.space2
        radius: Theme.radius
        color: Theme.bgSunken
        border.width: result ? 1 : 0
        border.color: Theme.accent
        onBytesChanged: preview.show()
        onActiveChanged: preview.show()
        Component.onCompleted: preview.show()

        ColumnLayout {
            id: tileColumn
            anchors.fill: parent
            anchors.margins: Theme.space2
            spacing: Theme.space1
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: tile.canvasHeight
                clip: true
                DrawingCanvas {
                    id: preview
                    /// load()'s answer: "" shown, else the picture below.
                    property string status: "notDrawing"
                    // (Not interactive: the whole drawing in view, whatever the tile's size.)
                    function show() {
                        preview.status = tile.active && tile.bytes && tile.bytes.byteLength > 0
                                         ? preview.load(tile.bytes) : "notDrawing"
                    }
                    anchors.fill: parent
                    visible: status === ""
                    interactive: false
                    appDark: Theme.dark
                    Accessible.role: Accessible.Graphic
                    Accessible.name: tile.label
                }
                Image {
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    visible: preview.status !== "" && status === Image.Ready
                    source: tile.active && preview.status !== "" && tile.path !== ""
                            ? Workspace.drawingUrl(tile.path, Theme.dark) : ""
                    sourceSize.height: 2 * tile.canvasHeight
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                }
            }
            BText {
                Layout.fillWidth: true
                text: tile.label
                variant: "label"
                elide: Text.ElideRight
            }
            BText {
                Layout.fillWidth: true
                visible: text !== ""
                text: tile.detail
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
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

    // Binary files and the simple cases take no room below; text views and drawings fill it.
    Item {
        Layout.fillHeight: true
        visible: !view.textConflict && !drawingScroll.visible
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
        message: resolverObject.drawing
                 ? qsTr("“%1” gets the drawing from the copy. Your current version stays in its history.").arg(view.conflict.title || "")
                 : view.conflict.text === false
                   ? qsTr("“%1” gets the content of the copy. Your current version goes to the trash.").arg(view.conflict.title || "")
                   : qsTr("“%1” gets the content of the copy. Your current version stays in the note's history.").arg(view.conflict.title || "")
        confirmText: qsTr("Keep the copy")
        onAccepted: resolverObject.keepCopy()
    }
}
