pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BTimePickerDialog — the round clock (BTimePicker) in a small dialog, for a field that shows
// only the time. "Now" fills in the current time; the dial starts on hours and moves on to
// minutes after an hour is picked.
//
//   BTimePickerDialog { id: clock; onPicked: (value) => field.text = value }
//   clock.openFor("18:30")
BDialog {
    id: control

    property string time: "09:00"

    signal picked(string time)

    /// Open for `value` ("HH:mm"; empty or invalid starts at the current time), on the hours.
    function openFor(value) {
        const t = DatesHelper.normalizeTime(value || "")
        picker.time = t !== "" ? t : DatesHelper.now().slice(11, 16)
        picker.mode = "hour"
        open()
    }

    title: qsTr("Pick a time")
    iconName: "clock"
    preferredWidth: Theme.dialogWidthSm
    acceptText: qsTr("Set")
    initialFocusItem: picker.focusItem
    onAccepted: control.picked(picker.displayTime)

    footerLeading: [
        BButton {
            text: qsTr("Now")
            iconName: "clock"
            variant: "ghost"
            onClicked: {
                picker.time = DatesHelper.now().slice(11, 16)
                picker.mode = "minute"
            }
        }
    ]

    BTimePicker {
        id: picker
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
    }
}
