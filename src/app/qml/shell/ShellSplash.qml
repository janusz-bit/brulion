pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellSplash — shown while the app boots or a vault is being opened.
Rectangle {
    color: Theme.bg

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.space4

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/images/logo.svg"
            sourceSize: Qt.size(72, 72)
            Layout.preferredWidth: 72
            Layout.preferredHeight: 72
            Accessible.ignored: true
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.space2
            visible: AppController.status === "loading"
            BSpinner {
                size: Theme.iconSize
            }
            BText {
                // The first copy of a picked folder's notes says what it copies (and how far).
                text: AppController.loadingText !== "" ? AppController.loadingText : qsTr("Opening the notebook…")
                variant: "muted"
                Layout.maximumWidth: 300
                wrapMode: Text.Wrap
            }
        }
    }
}
