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
    readonly property var stats: DiffHelper.stats(oldText, newText)
    readonly property bool identical: stats.added === 0 && stats.removed === 0
    readonly property var rows: split ? DiffHelper.splitRows(oldText, newText, expanded ? -1 : context)
                                      : DiffHelper.unifiedRows(oldText, newText, expanded ? -1 : context)
    readonly property int lineDigits: String(Math.max(1, stats.added + stats.unchanged, stats.removed + stats.unchanged)).length
    readonly property real gutterWidth: mono.advanceWidth("0") * Math.max(2, lineDigits) + 14

    implicitWidth: 640
    implicitHeight: 360

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Differences: %1 added, %2 removed").arg(stats.added).arg(stats.removed)

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
                    text: "+" + control.stats.added
                    color: Theme.diffAddFg
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "−" + control.stats.removed
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
                visible: control.rows.some(r => r.kind === "skip") || control.expanded
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
                model: [
                    { value: "split", text: qsTr("Side by side"), icon: "columns-2" },
                    { value: "unified", text: qsTr("One below the other"), icon: "rows-2" }
                ]
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
                    model: [control.oldLabel, control.newLabel]
                    delegate: Rectangle {
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
                            text: parent.modelData
                            color: Theme.fgMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            font.weight: Theme.weightMedium
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            visible: parent.index === 0
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

                delegate: Loader {
                    id: rowLoader
                    required property var modelData
                    required property int index
                    width: list.width
                    sourceComponent: modelData.kind === "skip" ? skipRow : control.split ? splitRow : unifiedRow
                    onLoaded: item.row = Qt.binding(() => rowLoader.modelData)
                }
            }
        }
    }

    // ---- Row components -----------------------------------------------------------------
    component LineText: Text {
        property var segments: []
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
            property var row: ({})
            width: list.width
            implicitHeight: Theme.touchUi ? Theme.touchTarget : 28
            hoverEnabled: !Theme.touchUi
            focusPolicy: Qt.StrongFocus
            text: qsTr("%n unchanged line(s)", "", row.count || 0)
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
            property var row: ({})
            readonly property bool del: row.kind === "delete"
            readonly property bool ins: row.kind === "insert"
            width: list.width
            implicitHeight: Math.ceil(Math.max(lineText.implicitHeight + 6, 22))
            color: del ? Theme.diffDelBg : ins ? Theme.diffAddBg : "transparent"
            Accessible.role: Accessible.StaticText
            Accessible.name: (del ? qsTr("Removed: ") : ins ? qsTr("Added: ") : "") + (row.text || "")

            Row {
                id: gutters
                Gutter { text: urow.row.oldNo > 0 ? urow.row.oldNo : "" }
                Gutter { text: urow.row.newNo > 0 ? urow.row.newNo : "" }
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
                segments: urow.row.segments || []
                text: segments.length > 0
                      ? DiffHelper.segmentsHtml(segments, urow.del ? Theme.diffDelStrong : Theme.diffAddStrong)
                      : (urow.row.text || "")
            }
        }
    }

    Component {
        id: splitRow
        Item {
            id: srow
            property var row: ({})
            readonly property bool leftChanged: row.kind === "delete" || row.kind === "change"
            readonly property bool rightChanged: row.kind === "insert" || row.kind === "change"
            readonly property real half: width / 2
            width: list.width
            implicitHeight: Math.ceil(Math.max(leftText.implicitHeight, rightText.implicitHeight, 16) + 6)
            Accessible.role: Accessible.StaticText
            Accessible.name: row.kind === "equal" ? (row.left || "")
                           : qsTr("Before: %1. After: %2").arg(row.left || "").arg(row.right || "")

            Rectangle {
                width: srow.half
                height: parent.height
                color: srow.leftChanged ? Theme.diffDelBg : srow.row.kind === "insert" ? Theme.bgSunken : "transparent"
            }
            Rectangle {
                x: srow.half
                width: srow.half
                height: parent.height
                color: srow.rightChanged ? Theme.diffAddBg : srow.row.kind === "delete" ? Theme.bgSunken : "transparent"
            }
            Rectangle {
                x: srow.half - 1
                width: 1
                height: parent.height
                color: Theme.border
            }

            Gutter {
                id: leftNo
                text: srow.row.oldNo > 0 ? srow.row.oldNo : ""
            }
            LineText {
                id: leftText
                x: leftNo.width
                y: 3
                width: srow.half - x - 8
                segments: srow.row.leftSegments || []
                text: segments.length > 0 ? DiffHelper.segmentsHtml(segments, Theme.diffDelStrong) : (srow.row.left || "")
            }
            Gutter {
                id: rightNo
                x: srow.half
                text: srow.row.newNo > 0 ? srow.row.newNo : ""
            }
            LineText {
                id: rightText
                x: srow.half + rightNo.width
                y: 3
                width: srow.half - rightNo.width - 8
                segments: srow.row.rightSegments || []
                text: segments.length > 0 ? DiffHelper.segmentsHtml(segments, Theme.diffAddStrong) : (srow.row.right || "")
            }
        }
    }
}
