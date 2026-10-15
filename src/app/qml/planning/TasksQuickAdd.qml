pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TasksQuickAdd — the "add a task" field of TasksView with natural-language dates: "Zadzwonić do
// Ani jutro o 9 #praca" becomes "- [ ] Zadzwonić do Ani #praca ⏰ 2026-10-16 09:00" (a time gives a
// reminder, a day without one a 📅 due date — like quick capture). A live preview below the
// field shows what will be written and where (today's journal). Enter adds; Escape clears.
//
//   TasksQuickAdd { actions: actions; onSubmitted: (line) => … }
ColumnLayout {
    id: root

    required property PlanningActions actions
    property alias text: field.text
    readonly property var parsed: field.text.trim() !== "" ? actions.parseQuickAdd(field.text) : ({ ok: false })

    signal submitted(string line, var parsed)

    function submit() {
        if (!parsed.ok)
            return
        submitted(parsed.line, parsed)
        field.text = ""
    }
    function focusField() { field.forceActiveFocus() }

    spacing: Theme.space1

    BTextField {
        id: field
        Layout.fillWidth: true
        leadingIcon: "plus"
        clearable: true
        placeholderText: qsTr("Add a task… e.g. “Call Ann tomorrow at 9 #work”")
        accessibleName: qsTr("New task")
        onAccepted: root.submit()
        trailingItem: BButton {
            visible: root.parsed.ok && !Theme.touchUi
            size: "small"
            variant: "primary"
            text: qsTr("Add")
            onClicked: root.submit()
        }
    }

    // Preview: the date that was understood and where the task goes.
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space1
        visible: root.parsed.ok === true
        spacing: Theme.space2

        BIcon {
            name: "corner-down-left"
            size: Theme.iconSizeSm
            color: Theme.fgFaint
        }
        BChip {
            visible: root.parsed.kind !== "task"
            size: "small"
            variant: root.parsed.kind === "reminder" ? "reminder" : "due"
            text: root.parsed.when || ""
        }
        Text {
            Layout.fillWidth: true
            text: root.parsed.kind === "task" ? qsTr("Without a date — goes into today's journal")
                                              : qsTr("“%1” — into today's journal").arg(root.parsed.text || "")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
            elide: Text.ElideRight
        }
    }
}
