pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// HabitAddDialog — "Add habit" of HabitsView: a name and a schedule ("codziennie", "pn, śr, pt",
// "3 razy w tygodniu" — Polish: daily, Mon, Wed, Fri, 3 times a week), previewed in words as the
// core reads it (HabitListModel.describeSchedule); accepted only for a new name and a schedule
// that is understood. The caller writes the definition (Workspace.addHabit: "- Name" +
// "  habit:: schedule" on the habits' page).
//
//   HabitAddDialog { habits: habitModel; onAddRequested: (name, schedule) => … }
BDialog {
    id: control

    /// The HabitListModel (describeSchedule, hasHabit).
    property HabitListModel habits: null

    signal addRequested(string name, string schedule)

    // What is typed with the word an on-screen keyboard still composes (BTextField.typedText):
    // Android keeps it outside `text`, so "codziennie" typed alone left Add disabled, no preview.
    readonly property string name: Js.trim(nameField.typedText)
    readonly property string schedule: Js.trim(scheduleField.typedText)
    readonly property string preview: habits ? habits.describeSchedule(schedule) : ""
    readonly property bool taken: name !== "" && habits !== null && habits.hasHabit(name)

    objectName: "habitAddDialog"
    title: qsTr("New habit")
    subtitle: qsTr("Tick it every day in the journal")
    iconName: "flame"
    acceptText: qsTr("Add")
    acceptEnabled: name !== "" && preview !== "" && !taken
    initialFocusItem: nameField

    onOpened: {
        nameField.text = ""
        scheduleField.text = ""
    }
    onAccepted: addRequested(name, schedule)

    BText {
        Layout.fillWidth: true
        text: qsTr("Name")
        variant: "label"
    }
    BTextField {
        id: nameField
        objectName: "habitAddName"
        Layout.fillWidth: true
        placeholderText: qsTr("e.g. Meditation")
        errorText: control.taken ? qsTr("A habit of this name exists already") : ""
        onAccepted: scheduleField.forceActiveFocus()
    }
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("How often")
        variant: "label"
    }
    BTextField {
        id: scheduleField
        objectName: "habitAddSchedule"
        Layout.fillWidth: true
        placeholderText: qsTr("every day, Mon Wed Fri, 3 times a week…")
        errorText: control.schedule !== "" && control.preview === "" ? qsTr("Not understood — try “every day”, “mon, wed, fri” or “3 times a week”") : ""
        onAccepted: if (control.acceptEnabled) control.accept()
    }
    BText {
        Layout.fillWidth: true
        visible: control.preview !== ""
        text: qsTr("Due: %1").arg(control.preview)
        variant: "muted"
        wrapMode: Text.Wrap
    }
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("Written to the page as “- Name” with the property “habit:: …”; each day's tick is a task tagged #nawyk (or #habit) in that day's journal.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
