pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellAboutDialog — name, version, licence (AGPL-3.0-or-later), source code, credits.
BDialog {
    id: dialog

    title: qsTr("About Brulion")
    showReject: false
    acceptText: qsTr("Close")
    preferredWidth: Theme.dialogWidthSm

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space3

        RowLayout {
            spacing: Theme.space4
            Image {
                source: "qrc:/images/logo.svg"
                sourceSize: Qt.size(64, 64)
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                Accessible.ignored: true
            }
            ColumnLayout {
                spacing: 2
                BText {
                    text: "Brulion"
                    variant: "heading"
                }
                BText {
                    text: qsTr("Version %1").arg(AppController.version)
                    variant: "muted"
                }
            }
        }
        BText {
            Layout.fillWidth: true
            text: qsTr("Offline-first notes in plain Markdown files: an outliner with daily journals, a calendar, tasks, reminders and alarms, calculations, Syncthing and Nextcloud sync.")
            wrapMode: Text.Wrap
        }
        BText {
            Layout.fillWidth: true
            text: qsTr("Free software under the GNU Affero General Public License, version 3 or later. Icons: Lucide (ISC licence).")
            variant: "muted"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        BButton {
            text: qsTr("Source code on GitHub")
            iconName: "external-link"
            variant: "secondary"
            onClicked: Workspace.openUrl("https://github.com/janusz-bit/brulion")
        }
    }
}
