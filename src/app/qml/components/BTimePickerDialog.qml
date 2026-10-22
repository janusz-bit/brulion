pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// BTimePickerDialog — the round clock (BTimePicker) in a small dialog, for a field that shows
// only the time. "Now" fills in the current time; the dial starts on hours and moves on to
// minutes after an hour is picked. In a window too short for the clock (a phone in landscape)
// the dial shrinks to the room there is and the hours / minutes fields go beside it, so the
// whole dial shows without scrolling.
//
//   BTimePickerDialog { id: clock; onPicked: (value) => field.text = value }
//   clock.openFor("18:30")
BDialog {
    id: control

    /// The clock in the dialog.
    readonly property BTimePicker picker: clock
    /// The dial's size where there is room for it.
    readonly property int fullDialSize: Theme.touchUi ? 260 : 232
    /// Height the dialog's body has for the clock at most: the window's room for the dialog
    /// (BDialog's height cap) less its header, footer and their gaps.
    readonly property real clockRoom: overlayHeight - (sheet ? safeTop + Theme.space10 : 2 * Theme.space6)
                                      - implicitHeaderHeight - implicitFooterHeight - 2 * spacing
                                      - topPadding - bottomPadding - 4 - Theme.space1
    /// The fields above the full dial do not fit, but beside it they would: a wide, short window.
    readonly property bool sideBySide: overlayWidth > overlayHeight
                                       && clock.fieldsHeight + fullDialSize > clockRoom

    signal picked(string time)

    /// Open for `value` ("HH:mm"; empty or invalid starts at the current time), on the hours.
    function openFor(value) {
        const t = DatesHelper.normalizeTime(value || "")
        clock.time = t !== "" ? t : DatesHelper.now().slice(11, 16)
        clock.mode = "hour"
        open()
    }

    title: qsTr("Pick a time")
    iconName: "clock"
    preferredWidth: sideBySide ? Theme.dialogWidth : Theme.dialogWidthSm
    acceptText: qsTr("Set")
    initialFocusItem: clock.focusItem
    onAccepted: control.picked(clock.displayTime)

    footerLeading: [
        BButton {
            text: qsTr("Now")
            iconName: "clock"
            variant: "ghost"
            onClicked: {
                clock.time = DatesHelper.now().slice(11, 16)
                clock.mode = "minute"
            }
        }
    ]

    BTimePicker {
        id: clock
        objectName: "timePicker" // (the self-tests)
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        horizontal: control.sideBySide
        // Never below 180 px: the inner ring's twelve numbers need their room.
        dialSize: Math.max(180, Math.min(control.fullDialSize,
                                         control.sideBySide ? control.clockRoom
                                                            : control.clockRoom - clock.fieldsHeight))
    }
}
