pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BReminderDialog — create or edit a reminder: when (quick picks, natural language, calendar,
// 24-hour time), alarm or plain notification, and recurrence.
//
//   BReminderDialog {
//       id: reminderDialog
//       onReminderAccepted: (r) => page.setReminder(blockId, r)   // { at, alarm, recurrence }
//       onDeleteRequested: page.removeReminder(blockId)
//   }
//   reminderDialog.edit({ at: "2026-10-15 09:00", alarm: false, recurrence: "every week on Monday",
//                         context: "Zadzwonić do mamy" })
//   reminderDialog.create("Zadzwonić do mamy")
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
    property var repeatWeekdays: []
    property int repeatInterval: 2
    property string repeatUnit: "week"   // day | week | month | year

    signal reminderAccepted(var reminder)
    signal deleteRequested()

    readonly property string atDate: at.length >= 10 ? at.slice(0, 10) : ""
    readonly property string atTime: at.length >= 16 ? at.slice(11, 16) : ""
    readonly property bool valid: atDate !== "" && atTime !== ""
    readonly property bool past: valid && DatesHelper.compare(at, DatesHelper.now()) <= 0
    readonly property var rule: {
        const dow = atDate !== "" ? DatesHelper.dayOfWeek(atDate) : 1
        switch (repeatKind) {
        case "daily": return { freq: "day", interval: 1 }
        case "weekdays": return { freq: "day", interval: 1, weekdaysOnly: true }
        case "weekly": return { freq: "week", interval: 1, weekdays: repeatWeekdays.length > 0 ? repeatWeekdays : [dow] }
        case "monthly": return { freq: "month", interval: 1 }
        case "yearly": return { freq: "year", interval: 1 }
        case "custom": return {
            freq: repeatUnit, interval: Math.max(1, repeatInterval),
            weekdays: repeatUnit === "week" ? (repeatWeekdays.length > 0 ? repeatWeekdays : [dow]) : []
        }
        default: return null
        }
    }
    readonly property string canonicalRule: rule ? DatesHelper.formatRule(rule, "en") : ""
    // The locale passed for real (a method call is not tracked; a bare `void DatesHelper.locale` is
    // compiled away with the dependency): the label and the weekday names follow a language change.
    readonly property string ruleLabel: rule ? DatesHelper.formatRule(rule, DatesHelper.locale) : ""
    readonly property var nextDates: rule && valid ? DatesHelper.nextOccurrences(rule, at, 3) : []

    property var quickPicks: []

    function refreshQuickPicks() {
        const now = DatesHelper.now()
        const today = DatesHelper.today
        const tomorrow = DatesHelper.addDays(today, 1)
        const dow = DatesHelper.dayOfWeek(today)
        const monday = DatesHelper.addDays(today, ((8 - dow) % 7) || 7)
        const tonight = today + " 19:00"
        quickPicks = [
            { label: qsTr("in 10 min"), at: DatesHelper.addMinutes(now, 10), enabled: true },
            { label: qsTr("in an hour"), at: DatesHelper.addMinutes(now, 60), enabled: true },
            { label: qsTr("today 19:00"), at: tonight, enabled: DatesHelper.compare(tonight, now) > 0 },
            { label: qsTr("tomorrow 8:00"), at: tomorrow + " 08:00", enabled: true },
            { label: qsTr("Monday 9:00"), at: monday + " 09:00", enabled: true }
        ]
    }

    function setRecurrence(text) {
        repeatKind = "none"
        repeatWeekdays = []
        repeatInterval = 2
        repeatUnit = "week"
        if (!text)
            return
        const r = DatesHelper.parseRule(text)
        if (!r.ok)
            return
        const days = r.weekdays || []
        if (r.interval === 1 && !r.whenDone && !r.monthDay && !r.lastDayOfMonth) {
            if (r.freq === "day" && r.weekdaysOnly) { repeatKind = "weekdays"; return }
            if (r.freq === "day" && !r.weekdaysOnly) { repeatKind = "daily"; return }
            if (r.freq === "week" && !r.weekdaysOnly) { repeatKind = "weekly"; repeatWeekdays = days; return }
            if (r.freq === "month") { repeatKind = "monthly"; return }
            if (r.freq === "year") { repeatKind = "yearly"; return }
        }
        repeatKind = "custom"
        repeatInterval = r.interval
        repeatUnit = r.freq
        repeatWeekdays = days
    }

    /// Open for a new reminder (default: tomorrow 9:00).
    function create(contextText) {
        context = contextText || ""
        editing = false
        alarm = false
        at = DatesHelper.addDays(DatesHelper.today, 1) + " 09:00"
        setRecurrence("")
        refreshQuickPicks()
        picker.syncFromDialog()
        open()
    }

    /// Open for an existing reminder { at, alarm, recurrence, context }.
    function edit(reminder) {
        const r = reminder || {}
        context = r.context || ""
        editing = true
        alarm = !!r.alarm
        at = r.at || ""
        setRecurrence(r.recurrence || "")
        refreshQuickPicks()
        picker.syncFromDialog()
        open()
    }

    function toggleWeekday(day) {
        const dow = atDate !== "" ? DatesHelper.dayOfWeek(atDate) : 1
        let days = repeatWeekdays.length > 0 ? repeatWeekdays.slice() : [dow]
        const i = days.indexOf(day)
        if (i >= 0) {
            if (days.length > 1)
                days.splice(i, 1)
        } else {
            days.push(day)
        }
        days.sort((a, b) => a - b)
        repeatWeekdays = days
    }

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
    onAccepted: reminderAccepted({ at: control.at, alarm: control.alarm, recurrence: control.canonicalRule })

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
                        required property var modelData
                        text: modelData.label
                        iconName: "clock"
                        interactive: true
                        enabled: modelData.enabled
                        selected: control.at === modelData.at
                        onClicked: {
                            control.at = modelData.at
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
                function syncFromDialog() {
                    clearTyped()
                    selectedDate = control.atDate
                    time = control.atTime
                    if (control.atDate !== "") {
                        focusDate = control.atDate
                        showMonthOf(control.atDate)
                    }
                }
                onPicked: (value) => {
                    const s = DatesHelper.splitSpec(value)
                    if (s.ok)
                        control.at = s.date + " " + (s.time !== "" ? s.time : defaultTime)
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
                        model: [
                            { kind: "none", label: qsTr("Don't repeat") },
                            { kind: "daily", label: qsTr("Daily") },
                            { kind: "weekdays", label: qsTr("Weekdays") },
                            { kind: "weekly", label: qsTr("Weekly") },
                            { kind: "monthly", label: qsTr("Monthly") },
                            { kind: "yearly", label: qsTr("Yearly") },
                            { kind: "custom", label: qsTr("Custom…") }
                        ]
                        delegate: BChip {
                            required property var modelData
                            text: modelData.label
                            showIcon: false
                            interactive: true
                            selected: control.repeatKind === modelData.kind
                            onClicked: control.repeatKind = modelData.kind
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
                        text: String(control.repeatInterval)
                        horizontalAlignment: TextInput.AlignHCenter
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1; top: 999 }
                        accessibleName: qsTr("Interval")
                        onTextEdited: {
                            const n = parseInt(text, 10)
                            if (n >= 1)
                                control.repeatInterval = n
                        }
                        // Show the clean value again and keep following repeatInterval
                        // (a plain assignment would break the binding above).
                        onEditingFinished: text = Qt.binding(() => String(control.repeatInterval))
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
                    readonly property var effective: control.rule && control.rule.weekdays ? control.rule.weekdays : []
                    readonly property var shortNames: DatesHelper.weekdayNames("short", DatesHelper.locale)
                    readonly property var fullNames: DatesHelper.weekdayNames("full", DatesHelper.locale)
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
                    visible: control.rule !== null
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
                        text: qsTr("Next: %1").arg(control.nextDates.map(d => DatesHelper.formatHuman(d.slice(0, 10))).join("  ·  "))
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
