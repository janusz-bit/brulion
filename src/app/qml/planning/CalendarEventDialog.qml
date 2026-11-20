pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarEventDialog — an event of another calendar (Workspace.calendars; docs/UI.md "Calendars"):
// its details — when (in the device's time), the series it belongs to, where, the calendar and
// its colour, the description, a link — and "Note in the journal" (a line about it in the day's
// journal: journalRequested()). An event of a CalDAV calendar Brulion may write also has "Edit"
// (the form: title, all day, start and end, repeat, reminder, place, description; a series asks
// whether the change is for this event or all of them) and "Delete" (a series: this one or all).
// "New event" (openNew()) starts with the form in a calendar picked from the writable ones.
// Subscriptions are read-only: the details only.
//
//   CalendarEventDialog { id: eventDialog; onJournalRequested: (date, line) => …; onMessage: (text, kind) => … }
//   eventDialog.openFor(entry.eventKey)      eventDialog.openNew("2026-11-20")
BDialog {
    id: control

    property string key
    property bool editing: false
    readonly property WorkspaceCalendars calendars: Workspace.calendars
    readonly property list<var> writable: calendars !== null ? calendars.writableCalendars : noRows
    readonly property list<var> noRows: []
    // The details shown (eventDetails()).
    property string detailTitle
    property string detailWhen
    property string detailLocation
    property string detailDescription
    property string detailUrl
    property string detailCalendar
    property string detailColor
    property string detailRule
    property string detailRuleText
    property bool recurring: false
    property bool canEdit: false
    property string detailDate
    // The form.
    property string calendarId
    property string startDate
    property string startTime: "09:00"
    property string endDate
    property string endTime: "10:00"
    property bool allDay: false
    property string rule
    property int reminderMinutes: -1
    property string scope: "all"
    /// The date the inline picker sets: "start" | "end" | "" (none shown).
    property string pickerFor
    readonly property bool timesValid: allDay || (DatesHelper.normalizeTime(startTime) !== ""
                                                  && DatesHelper.normalizeTime(endTime) !== "")
    readonly property bool endBeforeStart: DatesHelper.compare(endDate + (allDay ? "" : " " + endTime),
                                                               startDate + (allDay ? "" : " " + startTime)) < 0

    signal journalRequested(string date, string line)
    signal message(string text, string kind)

    /// The repeat choices: [value, label].
    readonly property list<string> ruleValues: ["", "every day", "every weekday", "every week", "every 2 weeks",
                                                "every month", "every year"]
    function ruleName(value: string): string {
        if (value === "")
            return qsTr("Does not repeat")
        return "" + DatesHelper.ruleLabel(value)
    }
    readonly property list<int> reminderValues: [-1, 0, 5, 15, 30, 60, 1440]
    function reminderName(minutes: int): string {
        switch (minutes) {
        case -1: return qsTr("No reminder")
        case 0: return qsTr("At the start")
        case 60: return qsTr("1 hour before")
        case 1440: return qsTr("1 day before")
        }
        return qsTr("%n minute(s) before", "", minutes)
    }

    function openFor(eventKey: string): void {
        holder.map = control.calendars.eventDetails(eventKey)
        if (AppController.routeText(holder.map, "key") === "") {
            control.message(qsTr("This event is no longer in the calendar."), "warning")
            return
        }
        key = eventKey
        fillFromDetails()
        editing = false
        pickerFor = ""
        open()
    }
    function openNew(date: string): void {
        if (control.writable.length === 0) {
            control.message(qsTr("Add a CalDAV calendar in the sync center to create events."), "info")
            return
        }
        key = ""
        recurring = false
        canEdit = true
        calendarId = AppController.routeText(control.writable[0], "id")
        detailTitle = ""
        titleField.text = ""
        locationField.text = ""
        descriptionField.text = ""
        startDate = date
        endDate = date
        startTime = "09:00"
        endTime = "10:00"
        allDay = false
        rule = ""
        reminderMinutes = 15
        scope = "all"
        pickerFor = ""
        editing = true
        open()
    }
    function fillFromDetails(): void {
        detailTitle = AppController.routeText(holder.map, "title")
        detailWhen = AppController.routeText(holder.map, "whenText")
        detailLocation = AppController.routeText(holder.map, "location")
        detailDescription = AppController.routeText(holder.map, "description")
        detailUrl = AppController.routeText(holder.map, "url")
        detailCalendar = AppController.routeText(holder.map, "calendarName")
        detailColor = AppController.routeText(holder.map, "color")
        detailRule = AppController.routeText(holder.map, "rule")
        detailRuleText = AppController.routeText(holder.map, "ruleText")
        detailDate = AppController.routeText(holder.map, "startDate")
        recurring = AppController.routeFlag(holder.map, "recurring")
        canEdit = AppController.routeFlag(holder.map, "canEdit")
        calendarId = AppController.routeText(holder.map, "calendarId")
        startDate = AppController.routeText(holder.map, "startDate")
        endDate = AppController.routeText(holder.map, "endDate")
        allDay = AppController.routeFlag(holder.map, "allDay")
        startTime = allDay ? "09:00" : AppController.routeText(holder.map, "startTime")
        endTime = allDay ? "10:00" : AppController.routeText(holder.map, "endTime")
        rule = detailRule
        reminderMinutes = AppController.routeInt(holder.map, "reminderMinutes", -1)
        scope = "all"
    }
    function beginEdit(): void {
        titleField.text = detailTitle
        locationField.text = detailLocation
        descriptionField.text = detailDescription
        scope = recurring ? "this" : "all"
        editing = true
    }
    function save(): void {
        const ok = control.calendars.saveEvent({
            key: control.key, calendarId: control.calendarId, title: Js.trim(titleField.text),
            location: locationField.text, description: descriptionField.text,
            startDate: control.startDate, startTime: DatesHelper.normalizeTime(control.startTime),
            endDate: control.endDate, endTime: DatesHelper.normalizeTime(control.endTime),
            allDay: control.allDay, rule: control.rule, reminderMinutes: control.reminderMinutes,
            scope: control.scope
        })
        if (ok)
            control.message(control.key === "" ? qsTr("Event added") : qsTr("Event saved"), "success")
    }
    function remove(onlyThis: bool): void {
        if (control.calendars.deleteEvent(control.key, onlyThis))
            control.message(qsTr("Event deleted"), "success")
        control.close()
    }
    function setDate(which: string, value: string): void {
        if (which === "start") {
            // The end moves with the start: the event keeps its length.
            const length = daysBetween(startDate, endDate)
            startDate = value
            endDate = "" + DatesHelper.addDays(value, length)
        } else {
            endDate = value
        }
        pickerFor = ""
    }
    /// Days from `a` to `b` ("YYYY-MM-DD"), at most a year.
    function daysBetween(a: string, b: string): int {
        for (let i = 0; i <= 366; ++i) {
            if (DatesHelper.addDays(a, i) === b)
                return i
        }
        return 0
    }

    MapHolder { id: holder }

    title: editing ? (key === "" ? qsTr("New event") : qsTr("Edit event")) : detailTitle
    subtitle: editing ? "" : detailWhen
    iconName: editing ? "calendar-plus" : "calendar-clock"
    iconColor: detailColor !== "" && !editing ? detailColor : Theme.fgMuted
    preferredWidth: Theme.dialogWidth
    acceptText: editing ? (key === "" ? qsTr("Add") : qsTr("Save")) : qsTr("Close")
    showReject: editing
    acceptEnabled: !editing || (Js.trim(titleField.typedText) !== "" && DatesHelper.isValidDate(startDate)
                                && timesValid && !endBeforeStart && calendarId !== "")
    initialFocusItem: editing ? titleField : null
    onAccepted: if (editing) save()
    onRejected: editing = false

    // (One item of a width it binds itself: hidden buttons of the footer's row kept their width
    // there once the form showed, pushing the dialog's own buttons out.)
    footerLeading: [
        Item {
            implicitWidth: control.editing ? 0 : viewActions.implicitWidth
            implicitHeight: viewActions.implicitHeight
            Row {
                id: viewActions
                visible: !control.editing
                spacing: Theme.space2
                BButton {
                    visible: control.canEdit
                    objectName: "calendarEventEdit"
                    text: qsTr("Edit")
                    iconName: "pencil"
                    variant: "ghost"
                    onClicked: control.beginEdit()
                }
                BButton {
                    visible: control.canEdit
                    objectName: "calendarEventDelete"
                    text: qsTr("Delete")
                    iconName: "trash-2"
                    variant: "ghost"
                    textColor: Theme.dangerText
                    onClicked: control.recurring ? seriesDelete.open() : confirmDelete.open()
                }
                BButton {
                    objectName: "calendarEventJournal"
                    text: control.sheet ? qsTr("Journal") : qsTr("Note in the journal")
                    iconName: "book-open"
                    variant: "ghost"
                    Accessible.name: qsTr("Note in the journal")
                    onClicked: {
                        const line = "" + control.calendars.journalLine(control.key)
                        control.close()
                        if (line !== "")
                            control.journalRequested(control.detailDate, line)
                    }
                }
            }
        }
    ]

    // ---- The details -------------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: !control.editing
        spacing: Theme.space3

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: 10
                height: 10
                radius: 5
                color: control.detailColor !== "" ? control.detailColor : Theme.fgFaint
            }
            BText {
                Layout.fillWidth: true
                text: control.detailCalendar
                variant: "small"
                color: Theme.fgMuted
                elide: Text.ElideRight
            }
            BBadge {
                visible: !control.canEdit
                text: qsTr("Read-only")
                variant: "neutral"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: control.recurring
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "repeat"; size: Theme.iconSizeSm; color: Theme.fgMuted }
            BText {
                Layout.fillWidth: true
                text: control.detailRule !== "" ? control.ruleName(control.detailRule)
                    : control.detailRuleText !== "" ? qsTr("Repeats (%1)").arg(control.detailRuleText) : qsTr("Part of a series")
                variant: "small"
                wrapMode: Text.Wrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: control.detailLocation !== ""
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "map-pin"; size: Theme.iconSizeSm; color: Theme.fgMuted }
            BText {
                Layout.fillWidth: true
                text: control.detailLocation
                wrapMode: Text.Wrap
            }
        }
        BText {
            Layout.fillWidth: true
            visible: control.detailDescription !== ""
            text: control.detailDescription
            wrapMode: Text.Wrap
            color: Theme.fg
        }
        BButton {
            visible: control.detailUrl !== ""
            text: qsTr("Open link")
            iconName: "external-link"
            size: "small"
            variant: "secondary"
            onClicked: Workspace.openUrl(control.detailUrl)
        }
    }

    // ---- The form -----------------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: control.editing
        spacing: Theme.space3

        // A new event: which calendar.
        Flow {
            Layout.fillWidth: true
            visible: control.key === "" && control.writable.length > 1
            spacing: Theme.space2
            Repeater {
                model: control.writable
                delegate: BChip {
                    id: calendarChip
                    required property var modelData
                    readonly property string chipId: AppController.routeText(modelData, "id")
                    text: AppController.routeText(modelData, "name")
                    iconName: "circle"
                    interactive: true
                    selected: control.calendarId === chipId
                    onClicked: control.calendarId = calendarChip.chipId
                }
            }
        }
        BTextField {
            id: titleField
            objectName: "calendarEventTitle"
            Layout.fillWidth: true
            placeholderText: qsTr("Title")
            leadingIcon: "calendar-clock"
            EnterKey.type: Qt.EnterKeyNext
        }
        BSegmented {
            Layout.fillWidth: true
            visible: control.recurring && control.key !== ""
            fill: true
            size: "small"
            model: [{ value: "this", text: qsTr("This event") }, { value: "all", text: qsTr("All events") }]
            currentValue: control.scope
            onActivated: (value) => control.scope = "" + value
        }
        BSwitch {
            Layout.fillWidth: true
            text: qsTr("All day")
            iconName: "calendar-days"
            checked: control.allDay
            onToggled: control.allDay = checked
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.space2
            rowSpacing: Theme.space2
            BText { text: qsTr("Starts"); variant: "label" }
            BButton {
                objectName: "calendarEventStartDate"
                Layout.fillWidth: true
                text: DatesHelper.formatShort(control.startDate)
                iconName: "calendar"
                size: "small"
                onClicked: control.pickerFor = control.pickerFor === "start" ? "" : "start"
            }
            BTextField {
                visible: !control.allDay
                Layout.preferredWidth: Theme.touchUi ? 96 : 80
                size: "small"
                text: control.startTime
                accessibleName: qsTr("Start time (HH:mm)")
                inputMethodHints: Qt.ImhPreferNumbers
                errorText: DatesHelper.normalizeTime(text) === "" ? qsTr("HH:mm") : ""
                onEditingFinished: {
                    const t = "" + DatesHelper.normalizeTime(text)
                    if (t !== "") {
                        control.startTime = t
                        text = t
                    }
                }
            }
            Item { visible: control.allDay; Layout.preferredWidth: 1 }
            BText { text: qsTr("Ends"); variant: "label" }
            BButton {
                objectName: "calendarEventEndDate"
                Layout.fillWidth: true
                text: DatesHelper.formatShort(control.endDate)
                iconName: "calendar"
                size: "small"
                onClicked: control.pickerFor = control.pickerFor === "end" ? "" : "end"
            }
            BTextField {
                visible: !control.allDay
                Layout.preferredWidth: Theme.touchUi ? 96 : 80
                size: "small"
                text: control.endTime
                accessibleName: qsTr("End time (HH:mm)")
                inputMethodHints: Qt.ImhPreferNumbers
                errorText: DatesHelper.normalizeTime(text) === "" ? qsTr("HH:mm") : ""
                onEditingFinished: {
                    const t = "" + DatesHelper.normalizeTime(text)
                    if (t !== "") {
                        control.endTime = t
                        text = t
                    }
                }
            }
            Item { visible: control.allDay; Layout.preferredWidth: 1 }
        }
        BText {
            Layout.fillWidth: true
            visible: control.endBeforeStart
            text: qsTr("The event ends before it starts.")
            variant: "small"
            color: Theme.dangerText
            wrapMode: Text.Wrap
        }
        BDatePicker {
            Layout.fillWidth: true
            visible: control.pickerFor !== ""
            showNaturalInput: false
            selectedDate: control.pickerFor === "end" ? control.endDate : control.startDate
            onPicked: (value) => control.setDate(control.pickerFor, "" + DatesHelper.specDate(value))
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                id: repeatButton
                Layout.fillWidth: true
                text: control.ruleName(control.rule)
                iconName: "repeat"
                iconRight: false
                size: "small"
                onClicked: repeatMenu.popup(repeatButton, 0, repeatButton.height)
            }
            BButton {
                id: reminderButton
                Layout.fillWidth: true
                text: control.reminderName(control.reminderMinutes)
                iconName: "bell"
                size: "small"
                onClicked: reminderMenu.popup(reminderButton, 0, reminderButton.height)
            }
        }
        BTextField {
            id: locationField
            Layout.fillWidth: true
            placeholderText: qsTr("Place")
            leadingIcon: "map-pin"
        }
        BTextArea {
            id: descriptionField
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(implicitHeight, 72)
            placeholderText: qsTr("Description")
        }
    }

    BMenu {
        id: repeatMenu
        Repeater {
            model: control.ruleValues
            delegate: BMenuItem {
                required property string modelData
                text: control.ruleName(modelData)
                checkable: true
                checked: control.rule === modelData
                onTriggered: control.rule = modelData
            }
        }
    }
    BMenu {
        id: reminderMenu
        Repeater {
            model: control.reminderValues
            delegate: BMenuItem {
                required property int modelData
                text: control.reminderName(modelData)
                checkable: true
                checked: control.reminderMinutes === modelData
                onTriggered: control.reminderMinutes = modelData
            }
        }
    }

    BConfirmDialog {
        id: confirmDelete
        title: qsTr("Delete the event?")
        subject: control.detailTitle
        subjectIcon: "calendar-clock"
        message: qsTr("It is deleted from the calendar on the server too.")
        confirmText: qsTr("Delete")
        destructive: true
        onAccepted: control.remove(false)
    }
    BDialog {
        id: seriesDelete
        title: qsTr("Delete a repeating event")
        subtitle: control.detailTitle
        iconName: "repeat"
        acceptText: qsTr("All events")
        acceptVariant: "danger"
        footerLeading: [
            BButton {
                text: qsTr("This event")
                variant: "secondary"
                onClicked: {
                    seriesDelete.close()
                    control.remove(true)
                }
            }
        ]
        onAccepted: control.remove(false)
        BText {
            Layout.fillWidth: true
            text: qsTr("Delete only this event, or every event of the series?")
            wrapMode: Text.Wrap
        }
    }
}
