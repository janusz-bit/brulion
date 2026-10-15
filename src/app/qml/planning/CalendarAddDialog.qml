pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarAddDialog — "Add task" / "Add reminder" for a day of the calendar: what (one line) and,
// for reminders, the time (quick picks or typed, 24-hour) and whether it is a loud alarm. "More
// options…" hands the draft to BReminderDialog (any date, recurrence). The view writes the
// result into that day's journal.
//
//   CalendarAddDialog { id: add; onTaskAdded: (text, date) => …
//                       onReminderAdded: (text, at, alarm) => …
//                       onMoreOptionsRequested: (text, at, alarm) => reminderDialog.edit(…) }
//   add.openFor("reminder", "2026-10-16")
BDialog {
    id: control

    property string mode: "task"   // "task" | "reminder"
    property string date
    property string time: defaultTime
    property bool alarm: false
    property string defaultTime: "09:00"

    readonly property string at: date + " " + time
    readonly property bool validTime: time !== ""
    readonly property var quickTimes: ["08:00", "09:00", "12:00", "15:00", "18:00", "20:00"]

    signal taskAdded(string text, string date)
    signal reminderAdded(string text, string at, bool alarm)
    signal moreOptionsRequested(string text, string at, bool alarm)

    function openFor(newMode, day) {
        mode = newMode
        date = day
        time = defaultTime
        alarm = false
        field.text = ""
        timeField.text = ""
        open()
    }

    title: mode === "task" ? qsTr("New task") : qsTr("New reminder")
    subtitle: date !== "" ? DatesHelper.formatLong(date) : ""
    iconName: mode === "task" ? "square-check-big" : (alarm ? "alarm-clock" : "bell")
    iconColor: mode === "task" ? Theme.fgMuted : Theme.chipReminderFg
    preferredWidth: Theme.dialogWidth
    acceptText: qsTr("Add")
    acceptEnabled: field.text.trim() !== "" && (mode === "task" || validTime)
    initialFocusItem: field

    footerLeading: [
        BButton {
            visible: control.mode === "reminder"
            variant: "ghost"
            iconName: "repeat"
            text: control.sheet ? qsTr("More") : qsTr("More options…")
            Accessible.name: qsTr("More options: date, repetition")
            onClicked: {
                const text = field.text.trim()
                control.close()
                control.moreOptionsRequested(text, control.at, control.alarm)
            }
        }
    ]

    onAccepted: {
        const text = field.text.trim()
        if (mode === "task")
            taskAdded(text, date)
        else
            reminderAdded(text, at, alarm)
    }

    BTextField {
        id: field
        Layout.fillWidth: true
        leadingIcon: control.mode === "task" ? "square-check-big" : "bell"
        placeholderText: control.mode === "task" ? qsTr("What needs to be done?") : qsTr("What should I remind you of?")
        onAccepted: control.tryAccept()
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: control.mode === "reminder"
        spacing: Theme.space2

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            text: qsTr("Time")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightMedium
            color: Theme.fgMuted
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: control.quickTimes
                delegate: BChip {
                    required property string modelData
                    text: modelData
                    iconName: "clock"
                    interactive: true
                    selected: control.time === modelData
                    onClicked: {
                        control.time = modelData
                        timeField.text = ""
                    }
                }
            }
            BTextField {
                id: timeField
                width: Theme.touchUi ? 112 : 96
                size: "small"
                placeholderText: qsTr("other")
                accessibleName: qsTr("Other time (HH:mm)")
                inputMethodHints: Qt.ImhPreferNumbers
                errorText: text !== "" && DatesHelper.normalizeTime(text) === "" ? qsTr("e.g. 14:30") : ""
                onTextEdited: {
                    const t = DatesHelper.normalizeTime(text)
                    if (t !== "")
                        control.time = t
                }
                onAccepted: control.tryAccept()
            }
        }
        BSwitch {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            text: qsTr("Alarm (loud, until dismissed)")
            iconName: "alarm-clock"
            checked: control.alarm
            onToggled: control.alarm = checked
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        text: control.mode === "task"
              ? qsTr("Goes into the journal of %1, due that day.").arg(DatesHelper.formatShort(control.date))
              : qsTr("Goes into the journal of %1; the reminder comes at %2.").arg(DatesHelper.formatShort(control.date))
                                                                              .arg(control.time)
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
        color: Theme.fgFaint
        wrapMode: Text.Wrap
    }
}
