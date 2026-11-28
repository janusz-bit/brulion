import QtQuick
import Brulion

// CodeSnoozeMenu — a thread menu's "Snooze" submenu (docs/CODE.md "Settling and snoozing"; T3 Code's
// snooze options): an hour, three hours, this evening, tomorrow morning, Monday morning — each with
// the time it ends beside it (CodeThreads.snoozeLabel) — and "Until…", a time typed (custom()).
BMenu {
    id: menu

    /// A preset of CodeThreads.snooze(): "1h", "3h", "evening", "tomorrow", "monday".
    signal picked(string preset)
    signal custom()

    objectName: "codeThreadSnooze"
    title: qsTr("Snooze")
    iconName: "alarm-clock"

    BMenuItem {
        objectName: "codeSnooze1h"
        text: qsTr("For an hour")
        shortcut: menu.opened ? Workspace.codeThreads.snoozeLabel("1h") : ""
        onTriggered: menu.picked("1h")
    }
    BMenuItem {
        objectName: "codeSnooze3h"
        text: qsTr("For three hours")
        shortcut: menu.opened ? Workspace.codeThreads.snoozeLabel("3h") : ""
        onTriggered: menu.picked("3h")
    }
    BMenuItem {
        objectName: "codeSnoozeEvening"
        text: qsTr("Until the evening")
        shortcut: menu.opened ? Workspace.codeThreads.snoozeLabel("evening") : ""
        onTriggered: menu.picked("evening")
    }
    BMenuItem {
        objectName: "codeSnoozeTomorrow"
        text: qsTr("Until tomorrow")
        shortcut: menu.opened ? Workspace.codeThreads.snoozeLabel("tomorrow") : ""
        onTriggered: menu.picked("tomorrow")
    }
    BMenuItem {
        objectName: "codeSnoozeMonday"
        text: qsTr("Until Monday")
        shortcut: menu.opened ? Workspace.codeThreads.snoozeLabel("monday") : ""
        onTriggered: menu.picked("monday")
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "codeSnoozeCustom"
        text: qsTr("Until…")
        iconName: "calendar"
        onTriggered: menu.custom()
    }
}
