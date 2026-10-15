pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// QueryBlockView — a ```query block (docs/SYNTAX.md §10). Until the editor host can run index
// queries, the block shows its filters as chips ("type: tasks", "tag: zakupy") and a note that
// the results appear once the vault is indexed. Click (or Enter when selected) edits the query
// source in place.
Rectangle {
    id: root

    property BlockDelegate block
    readonly property var query: block.model.query || ({})
    readonly property var filters: query.filters || []
    readonly property var terms: query.terms || []
    readonly property real firstLineCenter: Theme.space3 + headerRow.height / 2

    implicitHeight: column.implicitHeight + 2 * Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgElevated
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.border

    function filterLabel(key) {
        switch (key) {
        case "type": return qsTr("type")
        case "status": return qsTr("status")
        case "tag": return qsTr("tag")
        case "page": return qsTr("page")
        case "path": return qsTr("path")
        case "due": return qsTr("due")
        case "scheduled": return qsTr("scheduled")
        case "start": return qsTr("start")
        case "done": return qsTr("done")
        case "remind": return qsTr("reminder")
        case "priority": return qsTr("priority")
        case "sort": return qsTr("sort")
        case "group": return qsTr("group")
        case "limit": return qsTr("limit")
        default: return key
        }
    }
    function chipVariant(key) {
        switch (key) {
        case "tag": return "tag"
        case "due": return "due"
        case "scheduled": return "scheduled"
        case "remind": return "reminder"
        case "status": return "done"
        default: return "neutral"
        }
    }

    ColumnLayout {
        id: column
        x: Theme.space4
        y: Theme.space3
        width: parent.width - 2 * Theme.space4
        spacing: Theme.space2

        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon { name: "filter"; size: Theme.iconSizeSm; color: Theme.fgMuted }
            Text {
                Layout.fillWidth: true
                text: qsTr("Query")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightSemiBold
                color: Theme.fgMuted
                elide: Text.ElideRight
            }
        }

        Flow {
            Layout.fillWidth: true
            visible: !root.block.editing && (root.filters.length > 0 || root.terms.length > 0)
            spacing: 6
            Repeater {
                model: root.filters
                delegate: BChip {
                    required property var modelData
                    size: "small"
                    variant: root.chipVariant(modelData.key)
                    showIcon: variant !== "neutral" && variant !== "done"
                    text: root.block.editor.autoTextSafe((modelData.negated ? "≠ " : "") + root.filterLabel(modelData.key) + ": " + modelData.value)
                }
            }
            Repeater {
                model: root.terms
                delegate: BChip {
                    required property var modelData
                    size: "small"
                    iconName: "search"
                    text: root.block.editor.autoTextSafe("„" + modelData + "”")
                }
            }
        }

        Loader {
            id: sourceLoader
            Layout.fillWidth: true
            active: root.block.editing
            visible: active
            sourceComponent: InlineEditor {
                width: sourceLoader.width
                block: root.block
                multiline: true
                highlightMode: "code"
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
                lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.5)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon { name: "info"; size: Theme.iconSizeSm; color: Theme.fgFaint }
            Text {
                Layout.fillWidth: true
                text: qsTr("Query results are not shown in this version yet.")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgFaint
                wrapMode: Text.Wrap
            }
        }
    }

    TapHandler {
        enabled: !root.block.editing
        onTapped: root.block.editor.startEditing(root.block.index, -1)
    }
}
