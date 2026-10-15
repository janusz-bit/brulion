pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TasksQueryDialog — "Show as query": the current task list as a ```query block (docs/SYNTAX.md
// §10) to paste into any page, where it shows the same tasks live, with every part of the query
// explained and a few more filters to try. "Copy" puts the block on the clipboard.
//
//   TasksQueryDialog { query: tasks.queryText; onCopied: toast(…) }
BDialog {
    id: control

    property string query
    readonly property string block: "```query\n" + query + "\n```"
    readonly property var parts: query.match(/"[^"]*"|\S+/g) || []

    signal copied()

    title: qsTr("Show as query")
    subtitle: qsTr("This list as a live query block")
    iconName: "code"
    acceptText: qsTr("Copy")
    rejectText: qsTr("Close")
    preferredWidth: Theme.dialogWidth

    onAccepted: {
        clipboard.text = block
        clipboard.selectAll()
        clipboard.copy()
        copied()
    }

    /// What one part of the query means.
    function explain(part) {
        const colon = part.indexOf(":")
        const key = colon > 0 ? part.slice(0, colon) : ""
        const value = colon > 0 ? part.slice(colon + 1).replace(/^"|"$/g, "") : part.replace(/^"|"$/g, "")
        switch (key) {
        case "type": return qsTr("only tasks (not blocks or pages)")
        case "status":
            switch (value) {
            case "open": return qsTr("open tasks: to do and in progress")
            case "todo": return qsTr("tasks to do")
            case "doing": return qsTr("tasks in progress")
            case "deferred": return qsTr("waiting tasks")
            case "done": return qsTr("done tasks")
            case "cancelled": return qsTr("cancelled tasks")
            }
            return qsTr("tasks with this status")
        case "tag": return qsTr("tagged #%1 (also #%1/…, and on pages tagged so)").arg(value)
        case "page": return qsTr("on the page “%1”").arg(value)
        case "path": return qsTr("in the folder “%1”").arg(value.replace(/\/$/, ""))
        case "priority": return value === "none" ? qsTr("without a priority") : qsTr("with priority “%1”").arg(value)
        case "sort":
            switch (value) {
            case "due": return qsTr("sorted by due date")
            case "priority": return qsTr("most important first")
            case "page": return qsTr("sorted by page")
            case "-created": return qsTr("newest first")
            }
            return qsTr("sort order")
        }
        return qsTr("containing “%1” (letters with or without Polish diacritics)").arg(value)
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Paste this block into any page: it lists the same tasks there and stays up to date. You can change it like any other text.")
        font.family: Theme.fontUi
        font.pixelSize: Theme.textBase
        color: Theme.fgMuted
        wrapMode: Text.Wrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.lineHeightPx(font.pixelSize)
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        implicitHeight: code.contentHeight + 2 * Theme.space3 + 2
        radius: Theme.radius
        color: Theme.bgSunken
        border.width: 1
        border.color: Theme.border

        TextEdit {
            id: code
            x: Theme.space3
            y: Theme.space3
            width: parent.width - 2 * Theme.space3
            text: control.block
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            color: Theme.fg
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionFg
            Accessible.name: qsTr("Query")
        }
    }

    // The parts explained.
    GridLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        columns: 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space2

        Repeater {
            model: control.parts
            delegate: Text {
                id: partLabel
                required property string modelData
                required property int index
                Layout.row: index
                Layout.column: 0
                Layout.alignment: Qt.AlignTop
                text: modelData
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
                color: Theme.link
            }
        }
        Repeater {
            model: control.parts
            delegate: Text {
                required property string modelData
                required property int index
                Layout.row: index
                Layout.column: 1
                Layout.fillWidth: true
                text: control.explain(modelData)
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fg
                wrapMode: Text.Wrap
            }
        }
    }

    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        tone: "sunken"
        padding: Theme.space3
        contentItem: ColumnLayout {
            spacing: Theme.space1
            Text {
                Layout.fillWidth: true
                text: qsTr("More to try")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightSemiBold
                color: Theme.fg
            }
            Repeater {
                model: [
                    { code: "due:<=+7d", text: qsTr("due within a week") },
                    { code: "due:overdue", text: qsTr("overdue") },
                    { code: "-tag:prywatne", text: qsTr("without the tag #prywatne") },
                    { code: "group:page", text: qsTr("grouped by page") },
                    { code: "limit:20", text: qsTr("at most 20 tasks") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: modelData.code
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textSm
                        color: Theme.link
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        text: "— " + modelData.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // Clipboard helper (QML has no clipboard API: copy from an invisible text edit).
    TextEdit {
        id: clipboard
        visible: false
    }
}
