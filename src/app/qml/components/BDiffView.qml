pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BDiffView — line diff of two texts (version history, sync conflicts, import previews).
//
//   BDiffView {
//       oldText: history.textAt(version); newText: page.text
//       oldLabel: qsTr("Wersja z 12:40"); newLabel: qsTr("Obecna")
//   }
//
// mode: "auto" (default: side by side when ≥ 720 px wide, else unified) | "split" | "unified".
// Changed line pairs get word-level highlights. Unchanged runs longer than 2 × `context`
// lines collapse into "⋯ N unchanged lines" rows (click to show everything).
// Data: DiffHelper (C++, Myers). Line terminators are ignored for comparison.
Item {
    id: control

    property string oldText
    property string newText
    property string oldLabel: qsTr("Before")
    property string newLabel: qsTr("After")
    property string mode: "auto"
    property int context: 3
    property bool showToolbar: true
    property bool expanded: false

    readonly property bool split: mode === "split" || (mode === "auto" && width >= 720)
    /// { added, removed, unchanged } (DiffHelper.stats).
    readonly property var stats: statsHolder.map
    MapHolder {
        id: statsHolder
        map: DiffHelper.stats(control.oldText, control.newText)
    }
    readonly property int addedCount: statsHolder.map.added
    readonly property int removedCount: statsHolder.map.removed
    readonly property int unchangedCount: statsHolder.map.unchanged
    readonly property bool identical: addedCount === 0 && removedCount === 0
    /// DiffHelper's rows, each with every key the row delegates read.
    readonly property list<var> rows: UiModels.withDefaults(split ? DiffHelper.splitRows(oldText, newText, expanded ? -1 : context)
                                                                  : DiffHelper.unifiedRows(oldText, newText, expanded ? -1 : context), {
        kind: "", oldNo: 0, newNo: 0, count: 0, text: "", left: "", right: ""
    }) // (a missing list arrives as an empty one)
    /// Some unchanged lines are collapsed into a "skip" row.
    readonly property list<string> rowKinds: UiModels.stringFields(rows, "kind")
    readonly property bool hasSkips: rowKinds.indexOf("skip") >= 0
    readonly property int lineDigits: ("" + Math.max(1, addedCount + unchangedCount, removedCount + unchangedCount)).length
    /// The changed words' highlights as text (DiffHelper.segmentsHtml takes a colour's text).
    readonly property string delStrongText: ThemePalette.colorText(Theme.diffDelStrong.r, Theme.diffDelStrong.g,
                                                                   Theme.diffDelStrong.b, Theme.diffDelStrong.a)
    readonly property string addStrongText: ThemePalette.colorText(Theme.diffAddStrong.r, Theme.diffAddStrong.g,
                                                                   Theme.diffAddStrong.b, Theme.diffAddStrong.a)
    readonly property real gutterWidth: mono.advanceWidth("0") * Math.max(2, lineDigits) + 14

    readonly property list<var> layoutChoices: [
        { value: "split", text: qsTr("Side by side"), icon: "columns-2" },
        { value: "unified", text: qsTr("One below the other"), icon: "rows-2" }
    ]
    readonly property list<string> columnLabels: [oldLabel, newLabel]

    implicitWidth: 640
    implicitHeight: 360

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Differences: %1 added, %2 removed").arg(addedCount).arg(removedCount)

    FontMetrics {
        id: mono
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.space2

        // ---- Toolbar: labels, stats, layout switch ------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: control.showToolbar
            spacing: Theme.space3

            Row {
                spacing: 6
                Layout.alignment: Qt.AlignVCenter
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+" + control.addedCount
                    color: Theme.diffAddFg
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "−" + control.removedCount
                    color: Theme.diffDelFg
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                }
            }
            Text {
                Layout.fillWidth: true
                text: control.oldLabel + "  →  " + control.newLabel
                color: Theme.fgMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                elide: Text.ElideMiddle
            }
            BIconButton {
                visible: control.hasSkips || control.expanded
                iconName: control.expanded ? "chevrons-down-up" : "chevrons-up-down"
                label: control.expanded ? qsTr("Collapse unchanged lines") : qsTr("Show all lines")
                checkable: true
                checked: control.expanded
                size: "small"
                onClicked: control.expanded = !control.expanded
            }
            BSegmented {
                size: "small"
                iconOnly: true
                accessibleName: qsTr("Layout")
                model: control.layoutChoices
                currentValue: control.split ? "split" : "unified"
                onActivated: (value) => control.mode = value
            }
        }

        // ---- Diff --------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.border
            clip: true

            BEmptyState {
                anchors.centerIn: parent
                visible: control.identical
                compact: true
                iconName: "check"
                title: qsTr("No differences")
                description: qsTr("Both versions have the same text.")
            }

            // Split view: column headers.
            Row {
                id: splitHeader
                visible: control.split && !control.identical
                x: 1
                y: 1
                width: parent.width - 2
                height: visible ? 26 : 0
                Repeater {
                    model: control.columnLabels
                    delegate: Rectangle {
                        id: columnHeader
                        required property string modelData
                        required property int index
                        width: splitHeader.width / 2
                        height: splitHeader.height
                        color: Theme.bgSunken
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: columnHeader.modelData
                            color: Theme.fgMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            font.weight: Theme.weightMedium
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            visible: columnHeader.index === 0
                            anchors.right: parent.right
                            width: 1
                            height: parent.height
                            color: Theme.border
                        }
                    }
                }
            }

            ListView {
                id: list
                visible: !control.identical
                anchors.fill: parent
                anchors.margins: 1
                anchors.topMargin: 1 + splitHeader.height
                clip: true
                model: control.rows
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                T.ScrollBar.vertical: BScrollBar {}

                delegate: RowLoader {
                    width: list.width
                    sourceComponent: kind === "skip" ? skipRow : control.split ? splitRow : unifiedRow
                }
            }
        }
    }

    // ---- Row components -----------------------------------------------------------------
    /// A row of the diff (the delegate): its row's keys, read by the row it loads (its parent).
    component RowLoader: Loader {
        required property int index
        /// "equal" | "delete" | "insert" | "change" | "skip"
        required property string kind
        required property int oldNo
        required property int newNo
        /// skip: the lines it hides.
        required property int count
        /// unified rows: the line; split rows: the old and the new line.
        required property string text
        required property string left
        required property string right
        /// Word segments [{ text, changed }] (DiffHelper.segmentsHtml).
        required property list<var> segments
        required property list<var> leftSegments
        required property list<var> rightSegments
    }

    component LineText: Text {
        property list<var> segments
        property bool deleted: false
        textFormat: segments && segments.length > 0 ? Text.RichText : Text.PlainText
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        color: Theme.fg
        lineHeight: 1.15
    }

    component Gutter: Text {
        width: control.gutterWidth
        horizontalAlignment: Text.AlignRight
        rightPadding: 8
        topPadding: 3
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs + 1
        color: Theme.fgFaint
    }

    Component {
        id: skipRow
        T.AbstractButton {
            id: skip
            readonly property RowLoader row: parent as RowLoader
            width: list.width
            implicitHeight: Theme.touchUi ? Theme.touchTarget : 28
            hoverEnabled: !Theme.touchUi
            focusPolicy: Qt.StrongFocus
            text: qsTr("%n unchanged line(s)", "", row ? row.count : 0)
            Accessible.role: Accessible.Button
            Accessible.name: text
            onClicked: control.expanded = true
            background: Rectangle {
                color: skip.hovered ? Theme.bgHover : Theme.bgSunken
                Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }
                Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.borderSubtle }
                BFocusRing { visible: skip.visualFocus; inset: true; targetRadius: 0 }
            }
            contentItem: Row {
                spacing: 8
                leftPadding: 10
                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevrons-up-down"
                    size: Theme.iconSizeSm
                    color: Theme.fgFaint
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: skip.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: skip.hovered ? Theme.fg : Theme.fgMuted
                }
            }
        }
    }

    Component {
        id: unifiedRow
        Rectangle {
            id: urow
            readonly property RowLoader row: parent as RowLoader
            readonly property string kind: row ? row.kind : ""
            readonly property bool del: kind === "delete"
            readonly property bool ins: kind === "insert"
            width: list.width
            implicitHeight: Math.ceil(Math.max(lineText.implicitHeight + 6, 22))
            color: del ? Theme.diffDelBg : ins ? Theme.diffAddBg : "transparent"
            Accessible.role: Accessible.StaticText
            Accessible.name: (del ? qsTr("Removed: ") : ins ? qsTr("Added: ") : "") + (row ? row.text : "")

            Row {
                id: gutters
                Gutter { text: urow.row && urow.row.oldNo > 0 ? "" + urow.row.oldNo : "" }
                Gutter { text: urow.row && urow.row.newNo > 0 ? "" + urow.row.newNo : "" }
                Text {
                    width: 16
                    topPadding: 3
                    text: urow.del ? "−" : urow.ins ? "+" : ""
                    color: urow.del ? Theme.diffDelFg : Theme.diffAddFg
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                }
            }
            LineText {
                id: lineText
                x: gutters.width
                y: 3
                width: urow.width - x - 8
                segments: urow.row ? urow.row.segments : []
                text: segments.length > 0
                      ? DiffHelper.segmentsHtml(segments, urow.del ? control.delStrongText : control.addStrongText)
                      : (urow.row ? urow.row.text : "")
            }
        }
    }

    Component {
        id: splitRow
        Item {
            id: srow
            readonly property RowLoader row: parent as RowLoader
            readonly property string kind: row ? row.kind : ""
            readonly property string left: row ? row.left : ""
            readonly property string right: row ? row.right : ""
            readonly property bool leftChanged: kind === "delete" || kind === "change"
            readonly property bool rightChanged: kind === "insert" || kind === "change"
            readonly property real half: width / 2
            width: list.width
            implicitHeight: Math.ceil(Math.max(leftText.implicitHeight, rightText.implicitHeight, 16) + 6)
            Accessible.role: Accessible.StaticText
            Accessible.name: kind === "equal" ? left : qsTr("Before: %1. After: %2").arg(left).arg(right)

            Rectangle {
                width: srow.half
                height: parent.height
                color: srow.leftChanged ? Theme.diffDelBg : srow.kind === "insert" ? Theme.bgSunken : "transparent"
            }
            Rectangle {
                x: srow.half
                width: srow.half
                height: parent.height
                color: srow.rightChanged ? Theme.diffAddBg : srow.kind === "delete" ? Theme.bgSunken : "transparent"
            }
            Rectangle {
                x: srow.half - 1
                width: 1
                height: parent.height
                color: Theme.border
            }

            Gutter {
                id: leftNo
                text: srow.row && srow.row.oldNo > 0 ? "" + srow.row.oldNo : ""
            }
            LineText {
                id: leftText
                x: leftNo.width
                y: 3
                width: srow.half - x - 8
                segments: srow.row ? srow.row.leftSegments : []
                text: segments.length > 0 ? DiffHelper.segmentsHtml(segments, control.delStrongText) : srow.left
            }
            Gutter {
                id: rightNo
                x: srow.half
                text: srow.row && srow.row.newNo > 0 ? "" + srow.row.newNo : ""
            }
            LineText {
                id: rightText
                x: srow.half + rightNo.width
                y: 3
                width: srow.half - rightNo.width - 8
                segments: srow.row ? srow.row.rightSegments : []
                text: segments.length > 0 ? DiffHelper.segmentsHtml(segments, control.addStrongText) : srow.right
            }
        }
    }
}
