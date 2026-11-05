pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BReminderDialog — create or edit a reminder: when (quick picks, natural language, calendar,
// 24-hour time), alarm or plain notification, and recurrence.
//
//   BReminderDialog {
//       id: reminderDialog
//       onReminderChosen: (at, alarm, recurrence) => page.setReminder(blockId, at, alarm, recurrence)
//       onDeleteRequested: page.removeReminder(blockId)
//   }
//   reminderDialog.editWith("2026-10-15 09:00", false, "every week on Monday", "Zadzwonić do mamy")
//   reminderDialog.create("Zadzwonić do mamy")
//
// (The older forms still work, not compiled: edit({ at, alarm, recurrence, context }) and
// reminderAccepted(reminder) with { at, alarm, recurrence }, emitted before reminderChosen.)
//
// Result: at = "YYYY-MM-DD HH:mm" (local wall-clock time), alarm = bool, recurrence =
// canonical English rule written into the file ("every week on Monday", via
// DatesHelper.formatRule(rule, "en")) or "" — the UI shows the rule in the UI language.
// Quick picks: in 10 min, in an hour, today 19:00, tomorrow 8:00, Monday 9:00. A date or time
// typed but not yet confirmed (no Enter / Done) applies when its field is left and on Save.
BDialog {
    id: control

    property string at            // "YYYY-MM-DD HH:mm"
    property bool alarm: false
    property string context
    property bool editing: false
    property var marks: ({})

    // Recurrence draft.
    property string repeatKind: "none"   // none | daily | weekdays | weekly | monthly | yearly | custom
    property list<int> repeatWeekdays
    property int repeatInterval: 2
    property string repeatUnit: "week"   // day | week | month | year

    signal reminderAccepted(var reminder)
    /// Save: the reminder chosen (recurrence: canonicalRule, "" for none).
    signal reminderChosen(string at, bool alarm, string recurrence)
    signal deleteRequested()

    readonly property string atDate: at.length >= 10 ? Js.slice(at, 0, 10) : ""
    readonly property string atTime: at.length >= 16 ? Js.slice(at, 11, 16) : ""
    readonly property bool valid: atDate !== "" && atTime !== ""
    readonly property bool past: valid && DatesHelper.compare(at, DatesHelper.now()) <= 0
    /// The weekdays of a weekly rule (the chosen ones, else the date's own); [] for other rules.
    readonly property list<int> effectiveWeekdays: repeatKind === "weekly" || (repeatKind === "custom" && repeatUnit === "week")
                                                   ? (repeatWeekdays.length > 0 ? repeatWeekdays : [atDate !== "" ? DatesHelper.dayOfWeek(atDate) : 1])
                                                   : []
    readonly property bool hasRule: repeatKind === "daily" || repeatKind === "weekdays" || repeatKind === "weekly"
                                    || repeatKind === "monthly" || repeatKind === "yearly" || repeatKind === "custom"
    readonly property list<int> _noDays: []
    /// The rule object (DatesHelper.makeRule); null for none.
    readonly property var rule: hasRule ? ruleHolder.map : null
    // (A typed map: what formatRule() and nextOccurrences() take; freq "" for none.)
    MapHolder {
        id: ruleHolder
        map: {
            switch (control.repeatKind) {
            case "daily": return DatesHelper.makeRule("day", 1, control._noDays, false)
            case "weekdays": return DatesHelper.makeRule("day", 1, control._noDays, true)
            case "weekly": return DatesHelper.makeRule("week", 1, control.effectiveWeekdays, false)
            case "monthly": return DatesHelper.makeRule("month", 1, control._noDays, false)
            case "yearly": return DatesHelper.makeRule("year", 1, control._noDays, false)
            case "custom": return DatesHelper.makeRule(control.repeatUnit, Math.max(1, control.repeatInterval),
                                                       control.effectiveWeekdays, false)
            default: return DatesHelper.makeRule("", 0, control._noDays, false)
            }
        }
    }
    readonly property string canonicalRule: hasRule ? DatesHelper.formatRule(ruleHolder.map, "en") : ""
    // The locale passed for real (a method call is not tracked; a bare `void DatesHelper.locale` is
    // compiled away with the dependency): the label and the weekday names follow a language change.
    readonly property string ruleLabel: hasRule ? DatesHelper.formatRule(ruleHolder.map, DatesHelper.locale) : ""
    readonly property list<string> nextDates: hasRule && valid ? DatesHelper.nextOccurrences(ruleHolder.map, at, 3) : []
    readonly property string nextDatesText: {
        let text = ""
        for (let i = 0; i < nextDates.length; ++i)
            text += (i > 0 ? "  ·  " : "") + DatesHelper.formatHuman(Js.slice(nextDates[i], 0, 10))
        return text
    }

    /// { label, at, enabled } per quick pick.
    property list<var> quickPicks
    readonly property list<var> repeatChoices: [
        { kind: "none", label: qsTr("Don't repeat") },
        { kind: "daily", label: qsTr("Daily") },
        { kind: "weekdays", label: qsTr("Weekdays") },
        { kind: "weekly", label: qsTr("Weekly") },
        { kind: "monthly", label: qsTr("Monthly") },
        { kind: "yearly", label: qsTr("Yearly") },
        { kind: "custom", label: qsTr("Custom…") }
    ]

    function refreshQuickPicks(): void {
        // ("" + and 0 +: plain values, which compiled code passes on to the calls after them.)
        const now = "" + DatesHelper.now()
        const today = "" + DatesHelper.today
        const tomorrow = "" + DatesHelper.addDays(today, 1)
        const dow = 0 + DatesHelper.dayOfWeek(today)
        const toMonday = (8 - dow) % 7
        const monday = "" + DatesHelper.addDays(today, toMonday !== 0 ? toMonday : 7)
        const tonight = today + " 19:00"
        const in10 = DatesHelper.addMinutes(now, 10)
        const in60 = DatesHelper.addMinutes(now, 60)
        const tonightLater = DatesHelper.compare(tonight, now) > 0
        const labels = [qsTr("in 10 min"), qsTr("in an hour"), qsTr("today 19:00"), qsTr("tomorrow 8:00"),
                        qsTr("Monday 9:00")]
        quickPicks = [
            { label: labels[0], at: in10, enabled: true },
            { label: labels[1], at: in60, enabled: true },
            { label: labels[2], at: tonight, enabled: tonightLater },
            { label: labels[3], at: tomorrow + " 08:00", enabled: true },
            { label: labels[4], at: monday + " 09:00", enabled: true }
        ]
    }

    function setRecurrence(text: string): void {
        const kind = DatesHelper.repeatKind(text)
        repeatKind = kind
        repeatWeekdays = kind === "weekly" || kind === "custom" ? DatesHelper.ruleWeekdays(text) : []
        repeatInterval = kind === "custom" ? DatesHelper.ruleInterval(text) : 2
        repeatUnit = kind === "custom" ? DatesHelper.ruleFreq(text) : "week"
    }

    /// Open for a new reminder (default: tomorrow 9:00).
    function create(contextText: string): void {
        context = contextText
        editing = false
        alarm = false
        at = DatesHelper.addDays(DatesHelper.today, 1) + " 09:00"
        setRecurrence("")
        refreshQuickPicks()
        picker.syncFromDialog()
        open()
    }

    /// Open for an existing reminder.
    function editWith(at: string, alarm: bool, recurrence: string, context: string): void {
        control.context = context
        editing = true
        control.alarm = alarm
        control.at = at
        setRecurrence(recurrence)
        refreshQuickPicks()
        picker.syncFromDialog()
        open()
    }

    /// The older form of editWith(): { at, alarm, recurrence, context } (a missing key: none).
    function edit(reminder: var): void {
        // (Kept in a property, the fields as plain values: compiled code passes them on after the
        // calls that read the others.)
        _editArgument = reminder
        const at = "" + UiModels.stringField(_editArgument, "at")
        const alarm = UiModels.boolField(_editArgument, "alarm") === true
        const recurrence = "" + UiModels.stringField(_editArgument, "recurrence")
        const context = "" + UiModels.stringField(_editArgument, "context")
        _editArgument = null
        editWith(at, alarm, recurrence, context)
    }
    property var _editArgument: null

    function toggleWeekday(day: int): void {
        repeatWeekdays = DatesHelper.toggledWeekday(effectiveWeekdays, day)
    }

    /// The interval field shows repeatInterval (also after editing: the clean value).
    function syncIntervalField(): void {
        intervalField.text = "" + repeatInterval
    }
    onRepeatIntervalChanged: syncIntervalField()

    title: editing ? qsTr("Edit reminder") : qsTr("New reminder")
    iconName: alarm ? "alarm-clock" : "bell"
    iconColor: Theme.chipReminderFg
    subtitle: context
    preferredWidth: Theme.dialogWidthLg
    acceptText: qsTr("Save")
    acceptEnabled: valid && !picker.timeInvalid
    initialFocusItem: null

    footerLeading: [
        BButton {
            visible: control.editing
            text: control.sheet ? "" : qsTr("Delete")
            iconName: "trash-2"
            variant: "ghost"
            textColor: Theme.dangerText
            Accessible.name: qsTr("Delete reminder")
            onClicked: {
                control.deleteRequested()
                control.close()
            }
        }
    ]

    // What is still typed in the picker counts (the field may keep the focus: Ctrl+Enter, a
    // touch keyboard's Save tap).
    onAcceptRequested: picker.commitPending()
    onAccepted: {
        control.reminderAccepted({ at: control.at, alarm: control.alarm, recurrence: control.canonicalRule })
        control.reminderChosen(control.at, control.alarm, control.canonicalRule)
    }

    // Summary of the chosen moment (shown at the top of the single-column layout and at
    // the top of the right column in the two-column layout).
    component Summary: BCard {
        tone: control.past ? "warning" : "sunken"
        padding: Theme.space3
        contentItem: RowLayout {
            spacing: Theme.space3
            BIcon {
                name: control.alarm ? "alarm-clock" : "bell"
                size: Theme.iconSizeLg
                color: Theme.chipReminderFg
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: control.valid ? DatesHelper.formatShort(control.at) : qsTr("Pick a date and time")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textMd
                    font.weight: Theme.weightSemiBold
                    color: Theme.fg
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: control.valid
                    text: control.past ? qsTr("This time has already passed")
                                       : (control.alarm ? qsTr("Alarm") : qsTr("Notification")) + " · " + DatesHelper.formatHuman(control.at)
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textSm
                    color: control.past ? Theme.warningText : Theme.fgMuted
                    elide: Text.ElideRight
                }
            }
        }
    }

    // Two columns on wide dialogs (when | summary + how); one column on sheets, with the
    // summary first so the chosen moment is always visible at the top.
    GridLayout {
        id: grid
        readonly property bool twoColumns: !control.sheet && control.width >= 680
        Layout.fillWidth: true
        columns: twoColumns ? 2 : 1
        columnSpacing: Theme.space6
        rowSpacing: Theme.space5

        Summary {
            Layout.fillWidth: true
            visible: !grid.twoColumns
        }

        // ---- When ------------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.space3

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: control.quickPicks
                    delegate: BChip {
                        id: quickPick
                        required property string label
                        required property string at
                        required enabled
                        text: label
                        iconName: "clock"
                        interactive: true
                        selected: control.at === quickPick.at
                        onClicked: {
                            control.at = quickPick.at
                            picker.syncFromDialog()
                        }
                    }
                }
            }

            BDatePicker {
                id: picker
                objectName: "reminderDatePicker" // (the editor's self-tests)
                Layout.fillWidth: true
                withTime: true
                timeRequired: true
                applyTypedOnFocusLoss: true
                marks: control.marks
                // Opening, a quick pick: the dialog's moment replaces whatever was typed.
                function syncFromDialog(): void {
                    clearTyped()
                    selectedDate = control.atDate
                    time = control.atTime
                    if (control.atDate !== "") {
                        focusDate = control.atDate
                        showMonthOf(control.atDate)
                    }
                }
                onPicked: (value) => {
                    const date = DatesHelper.specDate(value)
                    const time = DatesHelper.specTime(value)
                    if (date !== "")
                        control.at = date + " " + (time !== "" ? time : picker.defaultTime)
                }
            }
        }

        // ---- How -------------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.space4

            Summary {
                Layout.fillWidth: true
                visible: grid.twoColumns
            }

            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Alarm (loud, until dismissed)")
                description: qsTr("Rings like an alarm clock instead of a quiet notification.")
                iconName: "alarm-clock"
                checked: control.alarm
                onToggled: control.alarm = checked
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.borderSubtle
            }

            // Recurrence.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space2

                RowLayout {
                    spacing: 8
                    BIcon {
                        name: "repeat"
                        size: Theme.iconSize
                        color: Theme.fgMuted
                    }
                    Text {
                        text: qsTr("Repeat")
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textBase
                        font.weight: Theme.weightMedium
                        color: Theme.fg
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: control.repeatChoices
                        delegate: BChip {
                            id: repeatChoice
                            required property string kind
                            required property string label
                            text: label
                            showIcon: false
                            interactive: true
                            selected: control.repeatKind === repeatChoice.kind
                            onClicked: control.repeatKind = repeatChoice.kind
                        }
                    }
                }

                // Custom interval: "every [2] [weeks]".
                RowLayout {
                    Layout.fillWidth: true
                    visible: control.repeatKind === "custom"
                    spacing: Theme.space2
                    Text {
                        text: qsTr("Every")
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textBase
                        color: Theme.fg
                    }
                    BTextField {
                        id: intervalField
                        Layout.preferredWidth: Theme.touchUi ? 64 : 56
                        implicitWidth: Layout.preferredWidth
                        size: "small"
                        Component.onCompleted: control.syncIntervalField()
                        horizontalAlignment: TextInput.AlignHCenter
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1; top: 999 }
                        accessibleName: qsTr("Interval")
                        onTextEdited: {
                            const n = Js.parseInt(intervalField.text)
                            if (n >= 1)
                                control.repeatInterval = n
                        }
                        // Show the clean value again (the field follows repeatInterval:
                        // syncIntervalField()).
                        onEditingFinished: control.syncIntervalField()
                    }
                    BSegmented {
                        Layout.fillWidth: true
                        size: "small"
                        fill: true
                        accessibleName: qsTr("Unit")
                        model: [
                            { value: "day", text: qsTr("days", "recurrence unit", control.repeatInterval) },
                            { value: "week", text: qsTr("weeks", "recurrence unit", control.repeatInterval) },
                            { value: "month", text: qsTr("months", "recurrence unit", control.repeatInterval) },
                            { value: "year", text: qsTr("years", "recurrence unit", control.repeatInterval) }
                        ]
                        currentValue: control.repeatUnit
                        onActivated: (value) => control.repeatUnit = value
                    }
                }

                // Weekday toggles for weekly rules.
                Row {
                    id: weekdayRow
                    objectName: "reminderWeekdays"
                    Layout.fillWidth: true
                    visible: control.repeatKind === "weekly" || (control.repeatKind === "custom" && control.repeatUnit === "week")
                    spacing: 4
                    readonly property list<int> effective: control.effectiveWeekdays
                    readonly property list<string> shortNames: DatesHelper.weekdayNames("short", DatesHelper.locale)
                    readonly property list<string> fullNames: DatesHelper.weekdayNames("full", DatesHelper.locale)
                    readonly property real buttonSize: Math.min(Theme.touchUi ? 44 : 36, (width - 6 * spacing) / 7)
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Repeat on")

                    Repeater {
                        model: 7
                        delegate: BButton {
                            id: dayButton
                            required property int index
                            readonly property int day: ((DatesHelper.weekStart - 1 + index) % 7) + 1
                            readonly property bool on: weekdayRow.effective.indexOf(day) >= 0
                            width: weekdayRow.buttonSize
                            implicitHeight: Math.max(weekdayRow.buttonSize, Theme.touchUi ? Theme.touchTarget : 0)
                            leftPadding: 0
                            rightPadding: 0
                            size: "small"
                            text: weekdayRow.shortNames[day - 1]
                            variant: on ? "primary" : "secondary"
                            checkable: false
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: weekdayRow.fullNames[day - 1]
                            Accessible.checkable: true
                            Accessible.checked: on
                            onClicked: control.toggleWeekday(day)
                        }
                    }
                }

                // Preview: label in the UI language, the canonical text and next dates.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    visible: control.hasRule
                    spacing: 2
                    Row {
                        Layout.fillWidth: true
                        spacing: 6
                        BChip {
                            variant: "recurrence"
                            text: control.ruleLabel
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: DatesHelper.locale !== "en"
                        text: qsTr("Written in the note as: %1").arg(control.canonicalRule)
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        color: Theme.fgFaint
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: control.nextDates.length > 0
                        text: qsTr("Next: %1").arg(control.nextDatesText)
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
