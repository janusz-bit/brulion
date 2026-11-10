pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// TasksQuickAdd — the "add a task" field of TasksView with natural-language dates: "Call Ann
// tomorrow at 9 #work" becomes "- [ ] Call Ann #work ⏰ 2026-10-16 09:00" (a time gives a
// reminder, a day without one a 📅 due date — like quick capture). A live preview below the
// field shows what will be written and where (today's journal). Enter adds; Escape clears.
//
//   TasksQuickAdd { actions: actions; onSubmitted: (line) => … }
ColumnLayout {
    id: root

    required property PlanningActions actions
    property alias text: field.text
    /// PlanningActions.parseQuickAdd() of the text ({ ok, line, kind, when, text, … }).
    readonly property var parsed: parsedHolder.map
    /// Something was typed and understood.
    readonly property bool ok: Js.trim(field.text) !== "" && parsedHolder.map.ok === true
    /// The text has no date: a plain task for today's journal.
    readonly property bool dateless: parsedHolder.map.kind === "task"

    signal submitted(string line, var parsed)

    function submit(): void {
        if (!ok)
            return
        submitted(AppController.routeText(parsedHolder.map, "line"), parsedHolder.map)
        field.text = ""
    }
    function focusField(): void { field.forceActiveFocus() }

    spacing: Theme.space1

    MapHolder {
        id: parsedHolder
        map: root.actions.parseQuickAdd(Js.trim(field.text) !== "" ? field.text : "")
    }

    BTextField {
        id: field
        Layout.fillWidth: true
        leadingIcon: "plus"
        clearable: true
        placeholderText: qsTr("Add a task… e.g. “Call Ann tomorrow at 9 #work”")
        accessibleName: qsTr("New task")
        onAccepted: root.submit()
        trailingItem: BButton {
            visible: root.ok && !Theme.touchUi
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
        visible: root.ok
        spacing: Theme.space2

        BIcon {
            name: "corner-down-left"
            size: Theme.iconSizeSm
            color: Theme.fgFaint
        }
        BChip {
            visible: !root.dateless
            size: "small"
            variant: parsedHolder.map.kind === "reminder" ? "reminder" : "due"
            text: AppController.routeText(parsedHolder.map, "when")
        }
        Text {
            Layout.fillWidth: true
            text: root.dateless ? qsTr("Without a date — goes into today's journal")
                                : qsTr("“%1” — into today's journal").arg(AppController.routeText(parsedHolder.map, "text"))
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
            elide: Text.ElideRight
        }
    }
}
