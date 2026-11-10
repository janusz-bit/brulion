pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarAddDialog — "Add task" / "Add reminder" for a day of the calendar: what (one line) and,
// for reminders, the time (quick picks, a typed 24-hour time or a round clock like Android's)
// and whether it is a loud alarm. "More options…" hands the draft to BReminderDialog (any date,
// recurrence). The view writes the result into that day's journal.
// A typed time applies as BDatePicker's does: on Enter, when the field is left, on Add, the clock
// and "More options…" — never per keystroke; text that is no time keeps Add disabled at once,
// quietly: the hint under the field comes when such a text is applied and goes with the next
// keystroke (one that came and went while "14:30" was typed changed the sheet's height). Today
// the dialog opens on the next full hour once the notebook's default time has passed, and the
// quick times already past are disabled (other days keep every time: a past day can still get
// one); a time that has passed is said under the field.
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
    /// "Now" ("YYYY-MM-DD HH:mm", DatesHelper's clock: the dev tools' fixed one in tests) when the
    /// dialog opened: today, the quick times up to it are disabled.
    property string openedAt

    readonly property string at: date + " " + time
    readonly property bool validTime: time !== ""
    readonly property bool isToday: date === DatesHelper.today
    /// The reminder would come at a time that has already passed (asked again on every change of
    /// the time and on every opening).
    readonly property bool past: mode === "reminder" && validTime && openedAt !== ""
                                 && DatesHelper.compare(at, DatesHelper.now()) <= 0
    readonly property list<string> quickTimes: ["08:00", "09:00", "12:00", "15:00", "18:00", "20:00"]
    /// The round clock, null until the clock button first opens it.
    property BTimePickerDialog clockDialog: null
    readonly property string badTimeHint: qsTr("Use a 24-hour time, e.g. 18:30")
    readonly property string pastHint: qsTr("This time has already passed")

    signal taskAdded(string text, string date)
    signal reminderAdded(string text, string at, bool alarm)
    signal moreOptionsRequested(string text, string at, bool alarm)

    /// "More options…": the draft goes to the view (after this dialog closed), with a time still
    /// being typed.
    function requestMoreOptions(text: string): void {
        timeField.commitTyped()
        close()
        moreOptionsRequested(text, at, alarm)
    }
    /// A quick time of today that has passed (is not later than when the dialog opened).
    function quickTimePassed(quickTime: string): bool {
        return isToday && DatesHelper.compare(date + " " + quickTime, openedAt) <= 0
    }
    function openFor(newMode: string, day: string): void {
        mode = newMode
        date = day
        openedAt = DatesHelper.now()
        time = defaultTime
        // Today after the default time: the next full hour, while it is still today.
        if (isToday && DatesHelper.compare(day + " " + defaultTime, openedAt) <= 0) {
            const nextHour = "" + DatesHelper.addMinutes(Js.slice(openedAt, 0, 13) + ":00", 60)
            if (DatesHelper.specDate(nextHour) === day)
                time = DatesHelper.specTime(nextHour)
        }
        alarm = false
        field.text = ""
        timeField.clearTyped()
        open()
    }

    title: mode === "task" ? qsTr("New task") : qsTr("New reminder")
    subtitle: date !== "" ? DatesHelper.formatLong(date) : ""
    iconName: mode === "task" ? "square-check-big" : (alarm ? "alarm-clock" : "bell")
    iconColor: mode === "task" ? Theme.fgMuted : Theme.chipReminderFg
    preferredWidth: Theme.dialogWidth
    acceptText: qsTr("Add")
    acceptEnabled: Js.trim(field.typedText) !== "" && (mode === "task" || (validTime && !timeField.noTime))
    initialFocusItem: field

    footerLeading: [
        BButton {
            visible: control.mode === "reminder"
            variant: "ghost"
            iconName: "repeat"
            text: control.sheet ? qsTr("More") : qsTr("More options…")
            Accessible.name: qsTr("More options: date, repetition")
            onClicked: control.requestMoreOptions(Js.trim(field.text))
        }
    ]

    // A time typed but not yet applied (the field keeps the focus: Ctrl+Enter, a touch keyboard's
    // Add tap).
    onAcceptRequested: {
        if (mode === "reminder")
            timeField.commitTyped()
    }
    onAccepted: {
        const text = Js.trim(field.text)
        if (mode === "task")
            taskAdded(text, date)
        else
            reminderAdded(text, at, alarm)
    }

    BTextField {
        id: field
        objectName: "calendarAddText"
        Layout.fillWidth: true
        leadingIcon: control.mode === "task" ? "square-check-big" : "bell"
        placeholderText: control.mode === "task" ? qsTr("What needs to be done?") : qsTr("What should I remind you of?")
        onAccepted: control.tryAccept()
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: control.mode === "reminder"
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            spacing: Theme.space2

            Text {
                Layout.fillWidth: true
                text: qsTr("Time")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                font.weight: Theme.weightMedium
                color: Theme.fgMuted
            }
            BButton {
                objectName: "calendarAddClock"
                text: control.time
                iconName: "clock"
                size: "small"
                variant: "secondary"
                Accessible.name: qsTr("Pick a time on a clock, now %1").arg(control.time)
                onClicked: {
                    timeField.commitTyped()
                    if (!control.clockDialog)
                        control.clockDialog = clockComponent.createObject(control.contentItem) as BTimePickerDialog
                    control.clockDialog.openFor(control.time)
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: control.quickTimes
                delegate: BChip {
                    required property string modelData
                    objectName: "calendarAddQuick_" + modelData
                    text: modelData
                    iconName: "clock"
                    interactive: true
                    // (Today a time that has passed is no quick pick, as the reminder dialog's
                    // "today 19:00".)
                    enabled: !control.quickTimePassed(modelData)
                    selected: control.time === modelData
                    onClicked: {
                        control.time = modelData
                        timeField.clearTyped()
                    }
                }
            }
            BTextField {
                id: timeField
                objectName: "calendarAddTime" // (the editor's self-test)
                width: Theme.touchUi ? 112 : 96
                size: "small"
                placeholderText: qsTr("other")
                accessibleName: qsTr("Other time (HH:mm)")
                inputMethodHints: Qt.ImhPreferNumbers
                Accessible.description: invalid ? control.badTimeHint : ""
                /// Text that is no time (yet: "14:" on the way to "14:30"): Add stays disabled,
                /// without the hint.
                readonly property bool noTime: Js.trim(text) !== "" && DatesHelper.normalizeTime(text) === ""
                /// Text that is no time was applied: the hint below says why, until the next
                /// keystroke (BDatePicker's way).
                property bool invalid: false
                /// The typed time becomes the reminder's (normalised in the field: "1430" →
                /// "14:30"); text that is no time stays, with the hint.
                function commitTyped(): void {
                    const t = "" + DatesHelper.normalizeTime(timeField.text)
                    timeField.invalid = t === "" && Js.trim(timeField.text) !== ""
                    if (t === "")
                        return
                    if (timeField.text !== t)
                        timeField.text = t
                    control.time = t
                }
                /// Empty, without the hint: the time came from elsewhere (a quick time, the clock)
                /// or the dialog opens again.
                function clearTyped(): void {
                    timeField.text = ""
                    timeField.invalid = false
                }
                onTextEdited: invalid = false
                onEditingFinished: commitTyped()
                // (editingFinished, which applied the time, comes first.)
                onAccepted: control.tryAccept()
            }
        }
        Text {
            objectName: "calendarAddTimeHint"
            Layout.fillWidth: true
            visible: text !== ""
            text: timeField.invalid ? control.badTimeHint : control.past ? control.pastHint : ""
            color: timeField.invalid ? Theme.dangerText : Theme.warningText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
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

    Component {
        id: clockComponent
        BTimePickerDialog {
            onPicked: (value) => {
                control.time = value
                timeField.clearTyped()
            }
        }
    }
}
