pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentMissedBanner — reminders that came due while Brulion was closed or the device was off
// (Workspace.reminders.missed), on top of the journals: each with its time, "Open" (the note
// at the reminder's line), "Snooze" (snooze: the new time is written into the note),
// "Done" (a task's reminder: the task is marked done in the note, ✅ today — the shell's
// toast confirms it) and dismiss; "Dismiss all". Hidden when nothing was missed.
//
//   ContentMissedBanner { Layout.fillWidth: true }
BCard {
    id: banner

    /// Most reminders listed (the rest are counted).
    property int maxShown: 3
    readonly property WorkspaceReminders reminders: Workspace.reminders
    readonly property list<var> missed: reminders ? reminders.missed : []

    visible: reminders !== null && reminders.missedCount > 0

    function snooze(id: string): void {
        if (banner.reminders.snooze(id))
            AppController.toast(qsTr("Snoozed for %n min", "", banner.reminders.snoozeMinutes), "success")
    }
    tone: "warning"
    padding: Theme.space3
    accessibleName: qsTr("Missed reminders")

    contentItem: ColumnLayout {
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon { name: "bell-ring"; size: Theme.iconSize; color: Theme.warningText }
            BText {
                Layout.fillWidth: true
                text: qsTr("%n missed reminder(s)", "", banner.reminders ? banner.reminders.missedCount : 0)
                variant: "label"
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BButton {
                visible: banner.missed.length > 1
                size: "small"
                variant: "ghost"
                text: qsTr("Dismiss all")
                onClicked: banner.reminders.clearMissed()
            }
        }

        Repeater {
            model: banner.missed.slice(0, banner.maxShown)
            delegate: ColumnLayout {
                id: item
                // The rows of WorkspaceReminders.missed (every key is always present; `id` cannot
                // be a property's name: read from modelData).
                required property var modelData
                readonly property string reminderId: AppController.routeText(modelData, "id")
                required property string title
                required property string body
                required property string when
                required property bool alarm
                required property string path
                required property bool task
                readonly property string day: path !== "" ? Workspace.journalDate(path) : ""
                Layout.fillWidth: true
                spacing: Theme.space1

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2

                    BIcon {
                        name: item.alarm ? "alarm-clock" : "bell"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 3
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        BText {
                            Layout.fillWidth: true
                            text: item.title !== "" ? item.title : item.body
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        BText {
                            Layout.fillWidth: true
                            text: item.when + (item.path === "" ? ""
                                  : " · " + (item.day !== "" ? DatesHelper.formatShort(item.day) : Workspace.pageTitle(item.path)))
                            variant: "caption"
                            color: Theme.fgMuted
                            elide: Text.ElideRight
                        }
                    }
                    BButton {
                        visible: !Theme.compact
                        size: "small"
                        text: qsTr("Open")
                        onClicked: banner.reminders.openMissed(item.reminderId)
                    }
                    BIconButton {
                        visible: !Theme.compact
                        size: "small"
                        iconName: "clock"
                        label: qsTr("Snooze %n min", "", banner.reminders.snoozeMinutes)
                        onClicked: banner.snooze(item.reminderId)
                    }
                    BIconButton {
                        visible: !Theme.compact && item.task
                        size: "small"
                        iconName: "check"
                        label: qsTr("Done")
                        onClicked: banner.reminders.complete(item.reminderId)
                    }
                    BIconButton {
                        size: "small"
                        iconName: "x"
                        label: qsTr("Dismiss")
                        Layout.alignment: Qt.AlignTop
                        onClicked: banner.reminders.dismissMissed(item.reminderId)
                    }
                }
                // Phones: the actions under the text, which keeps the whole width.
                RowLayout {
                    visible: Theme.compact
                    Layout.leftMargin: Theme.iconSizeSm + Theme.space2
                    spacing: Theme.space2
                    BButton {
                        size: "small"
                        text: qsTr("Open")
                        onClicked: banner.reminders.openMissed(item.reminderId)
                    }
                    BButton {
                        size: "small"
                        variant: "ghost"
                        iconName: "clock"
                        text: qsTr("Snooze %n min", "", banner.reminders.snoozeMinutes)
                        onClicked: banner.snooze(item.reminderId)
                    }
                    BButton {
                        visible: item.task
                        size: "small"
                        variant: "ghost"
                        iconName: "check"
                        text: qsTr("Done")
                        onClicked: banner.reminders.complete(item.reminderId)
                    }
                }
            }
        }
        BText {
            visible: banner.missed.length > banner.maxShown
            text: qsTr("and %n more", "", banner.missed.length - banner.maxShown)
            variant: "caption"
            color: Theme.fgMuted
        }
    }
}
