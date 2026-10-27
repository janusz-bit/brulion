pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Brulion

// ShellTrayCalendar — the tray calendar (desktop; docs/UI.md "Window layout"): a small frameless
// window next to the tray icon, like the clock's calendar of Windows 7/10. Today's date, the month
// with dots (ContentMiniCalendar) and the picked day's tasks and reminders; "Open Brulion" and
// "Quick capture…". An entry or "Open journal" brings up the main window on it. Without an open
// notebook only the month and a hint.
//
// Opened by Main.qml ("trayCalendar" / "toggleTrayCalendar", AppController.showTrayCalendar) at
// AppController.trayPopupPosition (traypopup.h: against the taskbar's edge, inside the work area;
// Wayland's compositor places it where it wants). It hides itself on Escape, on Alt+F4 and when
// it loses the activation (a click elsewhere, as a flyout) — only once it has been active: a
// refused activation (Wayland, Windows' foreground rules) leaves it open until Escape or a click
// on the icon. Always hide(), never a close that would end a Brulion hidden in the tray.
// No transient parent: owned by a hidden main window it would hide with it (Windows).
ApplicationWindow {
    id: popup
    objectName: "trayCalendar"

    width: 336
    height: 500
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    transientParent: null
    visible: false
    title: qsTr("Calendar")
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    /// Date.now() when it last hid itself on losing the activation (-1: never) — Main.qml's
    /// guard against the click on the icon that took the activation reopening it at once.
    property real autoHiddenAt: -1
    property bool wasActive: false

    function open() {
        calendar.showDay(DatesHelper.today)
        const p = AppController.trayPopupPosition(popup.width, popup.height)
        popup.x = p.x
        popup.y = p.y
        popup.wasActive = false
        popup.show()
        popup.raise()
        popup.requestActivate()
        content.forceActiveFocus()
    }

    onActiveChanged: {
        if (popup.active)
            popup.wasActive = true
        else if (popup.visible && popup.wasActive) {
            popup.autoHiddenAt = Date.now()
            popup.hide()
        }
    }
    onVisibleChanged: if (!popup.visible) popup.wasActive = false
    onClosing: (close) => {
        close.accepted = false
        popup.hide()
    }

    Item {
        id: content
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: popup.hide()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1 // inside the frame
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space3
                Layout.rightMargin: Theme.space1
                Layout.topMargin: Theme.space2
                spacing: Theme.space2
                BText {
                    objectName: "trayCalendarToday"
                    Layout.fillWidth: true
                    text: DatesHelper.formatLong(DatesHelper.today)
                    variant: "title"
                    wrapMode: Text.Wrap
                }
                BIconButton {
                    iconName: "x"
                    label: qsTr("Close")
                    size: "small"
                    onClicked: popup.hide()
                }
            }

            ContentMiniCalendar {
                id: calendar
                objectName: "trayCalendarMonth"
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholder: AppController.status === "ready" ? ""
                                                              : qsTr("Open a notebook to see its tasks and reminders.")
                // Shows the main window on what was opened; Main.qml hides the calendar then.
                onActivated: AppController.showWindow()
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.borderSubtle
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.space2
                spacing: Theme.space2
                BButton {
                    objectName: "trayCalendarOpen"
                    Layout.fillWidth: true
                    text: qsTr("Open Brulion")
                    iconName: "maximize"
                    onClicked: AppController.showWindow()
                }
                BButton {
                    objectName: "trayCalendarCapture"
                    Layout.fillWidth: true
                    visible: AppController.status === "ready"
                    text: qsTr("Quick capture…")
                    iconName: "notebook-pen"
                    onClicked: AppController.quickCapture()
                }
            }
        }
    }

    // The frame (a frameless window on a desktop of the same colour needs an edge).
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.border
        border.width: 1
    }
}
