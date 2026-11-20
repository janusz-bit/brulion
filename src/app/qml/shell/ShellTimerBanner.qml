pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellTimerBanner — the extensions' timers running (b.timer, Workspace.extensions.timers;
// docs/EXTENSIONS.md "Timers"): each one's label, the time left and a bar, and × to stop it. Under
// the top bar, in every view; the gym's rest timer shows in the Gym view's own banner instead while
// that view is on screen (`hiddenExtension`).
BCard {
    id: banner

    /// The extension whose timer the view on screen shows itself ("silownia" in the Gym view).
    property string hiddenExtension
    readonly property list<var> timers: Workspace.extensions.timers

    objectName: "shellTimerBanner"
    padding: Theme.space2
    accessibleName: qsTr("Timers")

    contentItem: ColumnLayout {
        spacing: Theme.space1
        Repeater {
            model: banner.timers
            delegate: RowLayout {
                id: timer
                required property string extension
                required property string title
                required property string label
                required property int remaining
                required property int total
                required property string text
                objectName: "shellTimer"
                visible: timer.extension !== banner.hiddenExtension
                Layout.fillWidth: true
                spacing: Theme.space2
                Accessible.role: Accessible.StatusBar
                Accessible.name: qsTr("%1: %2 left").arg(timer.label).arg(timer.text)

                BIcon { name: "timer"; size: Theme.iconSize; color: Theme.accent }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        BText {
                            Layout.fillWidth: true
                            text: timer.label === timer.title ? timer.label : timer.label + " · " + timer.title
                            elide: Text.ElideRight
                        }
                        BText {
                            text: timer.text
                            font.family: Theme.fontMono
                            font.weight: Font.DemiBold
                        }
                    }
                    BProgressBar {
                        Layout.fillWidth: true
                        value: timer.total > 0 ? timer.remaining / timer.total : 0
                    }
                }
                BIconButton {
                    objectName: "shellTimerStop"
                    size: "small"
                    iconName: "x"
                    label: qsTr("Stop the timer")
                    onClicked: Workspace.extensions.stopTimer(timer.extension)
                }
            }
        }
    }
}
