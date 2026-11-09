pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BDatePicker — month calendar with natural-language input and an optional 24-hour time
// (typed, or picked on a round clock like Android's).
//
//   BDatePicker {
//       selectedDate: "2026-10-15"; time: "09:00"; withTime: true
//       marks: ({ "2026-10-15": ["due", "reminder"], "2026-10-20": ["overdue"] })
//       onPicked: (value) => console.log(value)          // "2026-10-15 09:00"
//   }
//
// * Natural-language field (showNaturalInput): "jutro o 9", "w piątek 18:00" (Polish: tomorrow
//   at 9, on Friday 18:00), "next monday" (Polish + English via DatesHelper.parseNatural) with
//   a live preview "→ Thu, Oct 15 09:00"; Enter (the keyboard's action key: Done) applies it,
//   and with applyTypedOnFocusLoss also leaving the field or hiding the touch keyboard.
// * Month grid: names from DatesHelper (UI locale), week starts on `weekStart` (default
//   DatesHelper.weekStart, Monday), today marked with a red ring, selected day filled,
//   days of other months faint, `marks` (date -> kinds) as coloured dots: due, overdue,
//   scheduled, reminder, alarm, journal, done (Theme.markColor).
// * Keyboard (grid focused): arrows ±1 day / ±1 week, PageUp/PageDown ±1 month (with Shift
//   ±1 year), Home/End start/end of week, T today, Enter/Space select.
// * Time (withTime): "9", "930", "18.30" are normalised to "HH:mm" when editing finishes (Enter,
//   Done, leaving the field, hiding the touch keyboard); a time that is not one stays in the
//   field with a hint (timeInvalid). The clock button opens BTimePickerDialog — a round dial
//   for tapping hours and minutes —, created the first time it is asked for (`clockDialog`:
//   most date pickers never show it).
// * In a dialog with an OK button, call commitPending() before accepting: it applies what is
//   still being typed (the field may keep the focus on OK: Ctrl+Enter, a touch keyboard).
//
// Properties are strings ("YYYY-MM-DD", "HH:mm") — no JavaScript Date (UTC pitfalls).
// value = "YYYY-MM-DD" or "YYYY-MM-DD HH:mm"; picked(value) is emitted for user changes.
// Bind selectedDate/time to your state and update it in onPicked (the bindings are kept).
T.Control {
    id: control

    property string selectedDate
    property string time
    property bool withTime: false
    property bool timeRequired: false
    property string defaultTime: "09:00"
    property bool showNaturalInput: true
    property var marks: ({})
    property int weekStart: DatesHelper.weekStart
    // The shown month and the keyboard cursor. Plain values (not bindings: navigation assigns
    // them); they start at selectedDate (else today) and follow every change of selectedDate.
    property int displayYear: 0
    property int displayMonth: 0
    property string focusDate
    /// Text of the natural-language field (e.g. to prefill it).
    property alias naturalText: naturalField.text
    /// A recognised date in the natural-language field is also applied when the field loses
    /// focus (the user moved on to the time, a switch, OK) — for pickers inside a dialog with an
    /// OK button, where Cancel throws the draft away anyway. Off where a pick acts at once
    /// (JournalsView's "Go to day": leaving the field must not navigate).
    property bool applyTypedOnFocusLoss: false
    /// The time field holds text that is not a 24-hour time (its hint is shown); a dialog should
    /// not accept then.
    readonly property bool timeInvalid: withTime && timeField.invalid

    readonly property string value: selectedDate === "" ? ""
                                  : (withTime && time !== "" ? selectedDate + " " + time : selectedDate)
    /// DatesHelper.parseNatural() of the natural-language field ({ ok: false } while it is empty).
    readonly property var natural: naturalResult.map
    MapHolder {
        id: naturalResult
        map: DatesHelper.parseNatural(Js.trim(naturalField.text) !== "" ? naturalField.text : "")
    }
    /// The round clock, null until the Clock button first opens it.
    property BTimePickerDialog clockDialog: null

    signal picked(string value)

    function showMonthOf(date: string): void {
        displayYear = Js.toNumber(Js.slice(date, 0, 4))
        displayMonth = Js.toNumber(Js.slice(date, 5, 7))
    }
    /// User change of the date and/or time: emits picked() first, then keeps the new values
    /// unless the consumer's bindings already followed. Controlled use (`selectedDate:
    /// model.date; onPicked: model.date = …`) keeps its bindings this way — assigning first
    /// would break them, and later changes of the source would not show.
    function commit(date: string, newTime: string): void {
        picked(withTime && newTime !== "" ? date + " " + newTime : date)
        if (selectedDate !== date)
            selectedDate = date
        if (time !== newTime)
            time = newTime
    }
    /// Apply a time chosen elsewhere (the clock): with a date set, commit() it (emits picked()
    /// first, keeping a consumer's binding); without one, just the time, as a typed one does.
    function applyTime(newTime: string): void {
        if (!withTime)
            return
        timeField.invalid = false
        if (selectedDate !== "")
            commit(selectedDate, newTime)
        else if (time !== newTime)
            time = newTime
    }
    function selectDate(date: string): void {
        if (!DatesHelper.isValidDate(date))
            return
        focusDate = date
        showMonthOf(date)
        commit(date, withTime && time === "" && timeRequired ? defaultTime : time)
    }
    function moveFocus(date: string): void {
        if (!DatesHelper.isValidDate(date))
            return
        focusDate = date
        showMonthOf(date)
    }
    function shiftMonth(delta: int): void {
        const year = "" + displayYear
        const month = Js.padStart("" + displayMonth, 2, "0")
        // ("" +: a plain string, which compiled code passes on after other calls.)
        const first = "" + DatesHelper.addMonths(year + "-" + month + "-01", delta)
        showMonthOf(first)
        // Keep the keyboard focus inside the shown month.
        const focusDay = Js.slice(control.focusDate, 8, 10)
        const day = Math.min(Js.toNumber(focusDay), 28)
        const start = Js.slice(first, 0, 8)
        const dayText = Js.padStart("" + day, 2, "0")
        focusDate = start + dayText
    }
    function applyNatural(): void {
        if (!naturalResult.map.ok || !DatesHelper.isValidDate(naturalResult.map.date))
            return
        const date = "" + naturalResult.map.date
        focusDate = date
        showMonthOf(date)
        commit(date, naturalResult.map.hasTime && withTime ? "" + naturalResult.map.time
                                                           : withTime && time === "" && timeRequired ? defaultTime : time)
        naturalField.clear()
    }
    function focusGrid(): void {
        grid.forceActiveFocus(Qt.TabFocusReason)
    }
    /// Apply what is still being typed, as Enter would: a recognised date in the
    /// natural-language field, then a changed time in the time field. Returns false when the
    /// typed time is not valid (it stays in the field with its hint).
    function commitPending(): bool {
        if (naturalResult.map.ok)
            applyNatural()
        return withTime ? timeField.commitTyped() : true
    }
    /// Open the round clock on `value` (created on first use).
    function openClock(value: string): void {
        if (!clockDialog)
            clockDialog = clockComponent.createObject(control) as BTimePickerDialog
        clockDialog.openFor(value)
    }
    /// Forget typed text (natural-language field, an invalid time) — when a dialog opens anew.
    function clearTyped(): void {
        naturalField.clear()
        timeField.invalid = false
    }

    onSelectedDateChanged: {
        if (DatesHelper.isValidDate(control.selectedDate)) {
            control.focusDate = control.selectedDate
            control.showMonthOf(control.selectedDate)
        }
    }
    // A time set from outside (a quick pick, a recognised natural date) replaces a rejected one.
    onTimeChanged: if (!timeField.activeFocus) timeField.invalid = false
    // Touch keyboards: hiding the keyboard (Back, its hide key) ends typing as well — the field
    // keeps the focus then, so editingFinished would come only later (the summary above kept
    // the old time meanwhile).
    // (DeviceInfo: compiled QML knows Qt.inputMethod only as a QObject.)
    Connections {
        target: DeviceInfo
        function onInputMethodVisibleChanged() {
            if (DeviceInfo.inputMethodVisible)
                return
            if (timeField.activeFocus)
                timeField.commitTyped()
            else if (naturalField.activeFocus && control.applyTypedOnFocusLoss)
                control.applyNatural()
        }
    }
    Component.onCompleted: {
        const start = DatesHelper.isValidDate(control.selectedDate) ? control.selectedDate : DatesHelper.today
        if (control.focusDate === "")
            control.focusDate = start
        if (control.displayYear === 0)
            control.showMonthOf(start)
    }

    // Method calls are not tracked: `today` (the today marker moves at midnight) and `locale` are
    // read for real — qmlcachegen compiles a bare `void DatesHelper.today` away with the dependency.
    /// { date, day, inMonth, isToday, isWeekend, dayOfWeek } per cell (DatesHelper.monthGrid).
    readonly property list<var> cells: DatesHelper.today !== "" ? DatesHelper.monthGrid(displayYear, displayMonth, weekStart)
                                                                : []
    readonly property list<string> weekdayLabels: DatesHelper.weekdayNamesFrom(weekStart, "short", DatesHelper.locale)
    readonly property int cellSize: Theme.touchUi ? 44 : 38

    implicitWidth: Math.max(7 * cellSize, column.implicitWidth) + leftPadding + rightPadding
    implicitHeight: column.implicitHeight + topPadding + bottomPadding
    padding: 0

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Date picker")

    contentItem: ColumnLayout {
        id: column
        // The shown month's name, e.g. "listopad 2026" (the header capitalises it). (`locale` read
        // for real: monthTitle() is a method call, not tracked.)
        readonly property string monthTitle: DatesHelper.locale !== ""
                                             ? DatesHelper.monthTitle(control.displayYear, control.displayMonth) : ""
        spacing: Theme.space3

        // ---- Natural-language input + preview ------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            visible: control.showNaturalInput
            spacing: 4

            BTextField {
                id: naturalField
                objectName: "datePickerNatural"
                Layout.fillWidth: true
                leadingIcon: "sparkles"
                clearable: true
                placeholderText: qsTr("Type a date, e.g. tomorrow 9:00")
                accessibleName: qsTr("Date in your own words")
                // No sentence: Gboard would capitalise its first letter ("Za 2 minuty", in 2 minutes).
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: control.applyNatural()
                // Leaving the field (editingFinished without focus: not Enter, which accepted()
                // handled and which cleared the field).
                onEditingFinished: if (!activeFocus && control.applyTypedOnFocusLoss) control.applyNatural()
                Keys.onDownPressed: control.focusGrid()
            }
            Item {
                Layout.fillWidth: true
                implicitHeight: previewRow.implicitHeight
                visible: Js.trim(naturalField.text) !== ""

                Row {
                    id: previewRow
                    spacing: 6
                    width: parent.width
                    BIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: naturalResult.map.ok ? "" : "circle-help"
                        size: Theme.iconSizeSm
                        color: Theme.fgFaint
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // Beside the icon (not recognised) or the Enter hint (recognised).
                        width: parent.width - 6 - (naturalResult.map.ok ? enterHint.width : Theme.iconSizeSm)
                        // "→ Thu, Oct 15 09:00 · tomorrow" (relative word only for yesterday…tomorrow)
                        text: naturalResult.map.ok ? "→ " + naturalResult.map.display
                                                   + (naturalResult.map.relative ? "  ·  " + naturalResult.map.relativeWord : "")
                                                 : qsTr("No date recognised")
                        color: naturalResult.map.ok ? Theme.fg : Theme.fgFaint
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        font.weight: naturalResult.map.ok ? Theme.weightMedium : Theme.weightNormal
                        elide: Text.ElideRight
                    }
                }
                BKbd {
                    id: enterHint
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: naturalResult.map.ok
                    sequence: "Enter"
                    small: true
                }
                Accessible.role: Accessible.StaticText
                Accessible.name: naturalResult.map.ok ? naturalResult.map.display : qsTr("No date recognised")
            }
        }

        // ---- Month header ----------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                objectName: "datePickerMonthTitle"
                Layout.fillWidth: true
                Layout.leftMargin: 4
                text: Js.toUpperCase(Js.charAt(column.monthTitle, 0)) + Js.sliceFrom(column.monthTitle, 1)
                font.family: Theme.fontUi
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightSemiBold
                color: Theme.fg
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            BButton {
                text: qsTr("Today")
                size: "small"
                variant: "ghost"
                onClicked: control.selectDate(DatesHelper.today)
            }
            BIconButton {
                iconName: "chevron-left"
                label: qsTr("Previous month")
                shortcut: "PgUp"
                size: "small"
                onClicked: control.shiftMonth(-1)
            }
            BIconButton {
                iconName: "chevron-right"
                label: qsTr("Next month")
                shortcut: "PgDown"
                size: "small"
                onClicked: control.shiftMonth(1)
            }
        }

        // ---- Weekday names + day grid -------------------------------------------------
        Column {
            Layout.alignment: Qt.AlignHCenter
            spacing: 2

            Row {
                Repeater {
                    model: control.weekdayLabels
                    delegate: Text {
                        required property string modelData
                        width: control.cellSize
                        height: 22
                        text: modelData
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        font.weight: Theme.weightMedium
                        color: Theme.fgFaint
                    }
                }
            }

            FocusScope {
                id: grid
                width: 7 * control.cellSize
                height: 6 * control.cellSize
                activeFocusOnTab: true

                Accessible.role: Accessible.Table
                Accessible.name: column.monthTitle

                Keys.onPressed: (event) => {
                    const f = control.focusDate
                    let handled = true
                    switch (event.key) {
                    case Qt.Key_Left: control.moveFocus(DatesHelper.addDays(f, -1)); break
                    case Qt.Key_Right: control.moveFocus(DatesHelper.addDays(f, 1)); break
                    case Qt.Key_Up: control.moveFocus(DatesHelper.addDays(f, -7)); break
                    case Qt.Key_Down: control.moveFocus(DatesHelper.addDays(f, 7)); break
                    case Qt.Key_PageUp:
                        control.moveFocus(DatesHelper.addMonths(f, (event.modifiers & Qt.ShiftModifier) ? -12 : -1)); break
                    case Qt.Key_PageDown:
                        control.moveFocus(DatesHelper.addMonths(f, (event.modifiers & Qt.ShiftModifier) ? 12 : 1)); break
                    case Qt.Key_Home: {
                        const off = (DatesHelper.dayOfWeek(f) - control.weekStart + 7) % 7
                        control.moveFocus(DatesHelper.addDays(f, -off)); break
                    }
                    case Qt.Key_End: {
                        const off = (DatesHelper.dayOfWeek(f) - control.weekStart + 7) % 7
                        control.moveFocus(DatesHelper.addDays(f, 6 - off)); break
                    }
                    case Qt.Key_T: control.moveFocus(DatesHelper.today); break
                    case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space:
                        control.selectDate(f); break
                    default: handled = false
                    }
                    event.accepted = handled
                }

                Grid {
                    columns: 7
                    Repeater {
                        model: control.cells
                        delegate: T.AbstractButton {
                            id: cell
                            required property string date
                            required property int day
                            required property bool inMonth
                            required property bool isToday
                            required property bool isWeekend
                            readonly property bool isSelected: date === control.selectedDate
                            readonly property bool isFocused: grid.activeFocus && date === control.focusDate
                            readonly property list<string> kinds: UiModels.stringListField(control.marks, date)

                            width: control.cellSize
                            height: control.cellSize
                            focusPolicy: Qt.NoFocus
                            hoverEnabled: !Theme.touchUi
                            text: "" + day
                            Accessible.role: Accessible.Button
                            Accessible.name: DatesHelper.formatLong(date)
                                             + (isToday ? ", " + qsTr("today") : "")
                                             + (kinds.length > 0 ? ", " + qsTr("%n item(s)", "", kinds.length) : "")
                            Accessible.checkable: true
                            Accessible.checked: isSelected
                            onClicked: {
                                grid.forceActiveFocus(Qt.MouseFocusReason)
                                control.selectDate(cell.date)
                            }

                            background: Item {
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width, parent.height) - 4
                                    height: width
                                    radius: width / 2
                                    antialiasing: true
                                    color: cell.isSelected ? Theme.primary
                                         : cell.down ? Theme.bgPressed
                                         : cell.hovered ? Theme.bgHover : "transparent"
                                    border.width: cell.isToday && !cell.isSelected ? 1.5 : 0
                                    border.color: Theme.accent
                                    Behavior on color { ColorAnimation { duration: Theme.durationInstant } }
                                }
                                Rectangle {
                                    visible: cell.isFocused
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width, parent.height) - 4 + 2 * Theme.focusRingWidth + 2
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.width: Theme.focusRingWidth
                                    border.color: Theme.focusRing
                                    antialiasing: true
                                }
                            }
                            contentItem: Item {
                                Text {
                                    anchors.centerIn: parent
                                    anchors.verticalCenterOffset: cell.kinds.length > 0 ? -3 : 0
                                    text: cell.text
                                    font.family: Theme.fontUi
                                    font.pixelSize: Theme.textSm + 1
                                    font.weight: cell.isSelected || cell.isToday ? Theme.weightSemiBold : Theme.weightNormal
                                    font.features: { "tnum": 1 }
                                    color: cell.isSelected ? Theme.primaryFg
                                         : cell.isToday ? Theme.accentText
                                         : !cell.inMonth ? Theme.fgFaint
                                         : cell.isWeekend ? Theme.fgMuted : Theme.fg
                                    opacity: cell.inMonth || cell.isSelected ? 1 : 0.75
                                }
                                Row {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: Theme.touchUi ? 9 : 7
                                    spacing: 2
                                    Repeater {
                                        model: cell.kinds.slice(0, 3)
                                        delegate: Rectangle {
                                            required property string modelData
                                            width: 4
                                            height: 4
                                            radius: 2
                                            color: cell.isSelected ? Theme.primaryFg : Theme.markColor(modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Time ------------------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: control.withTime
            spacing: Theme.space2

            BIcon {
                name: "clock"
                size: Theme.iconSize
                color: Theme.fgMuted
            }
            Text {
                text: qsTr("Time")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                color: Theme.fg
            }
            Item { Layout.fillWidth: true }
            BTextField {
                id: timeField
                objectName: "datePickerTime"
                Layout.preferredWidth: Theme.touchUi ? 96 : 84
                implicitWidth: Layout.preferredWidth
                size: "small"
                placeholderText: "HH:mm"
                accessibleName: qsTr("Time (24-hour)")
                horizontalAlignment: TextInput.AlignHCenter
                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                maximumLength: 5
                font.features: { "tnum": 1 }
                property bool invalid: false
                /// Normalise and apply the typed time (Enter, Done, leaving the field, OK);
                /// false when it is no time (then it stays, with the hint below).
                function commitTyped(): bool {
                    const typed = "" + timeField.text
                    const blank = Js.trim(typed) === ""
                    // ("" +: a plain string, passed on after the assignments below.)
                    const t = "" + (blank && !control.timeRequired ? "" : DatesHelper.normalizeTime(typed))
                    invalid = t === "" && (!blank || control.timeRequired)
                    if (invalid)
                        return false
                    if (timeField.text !== t)
                        timeField.text = t
                    if (t === control.time)
                        return true
                    if (control.selectedDate !== "")
                        control.commit("" + control.selectedDate, t)
                    else
                        control.time = t
                    return true
                }
                onTextEdited: invalid = false
                onEditingFinished: commitTyped()
                // Follows control.time except while the user is typing (and while a rejected
                // time is shown with its hint).
                Binding on text {
                    when: !timeField.activeFocus && !timeField.invalid
                    value: control.time
                    restoreMode: Binding.RestoreNone
                }
            }
            BButton {
                objectName: "datePickerClock"
                text: qsTr("Clock")
                iconName: "clock"
                size: "small"
                variant: "secondary"
                Accessible.name: qsTr("Pick a time on a clock")
                onClicked: {
                    if (Js.trim(timeField.text) !== "")
                        timeField.commitTyped()
                    control.openClock(control.time !== "" ? control.time : control.defaultTime)
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: control.withTime && timeField.invalid
            text: qsTr("Use a 24-hour time, e.g. 18:30")
            color: Theme.dangerText
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
        }
    }

    Component {
        id: clockComponent
        BTimePickerDialog {
            onPicked: (value) => control.applyTime(value)
        }
    }
}
