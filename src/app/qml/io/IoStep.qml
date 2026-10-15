pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// IoStep — one numbered step of an explanation ("1  Zainstaluj Syncthing…"), with optional
// content below the text (a path, a button).
//
//   IoStep { number: 1; text: qsTr("Zainstaluj Syncthing na komputerze.") }
//   IoStep { number: 2; text: qsTr("Udostępnij folder:"); done: true; BText { … } }
RowLayout {
    id: step

    property int number: 1
    property string text
    /// Already done here (a check mark instead of the number).
    property bool done: false
    default property alias extra: extraColumn.data

    Layout.fillWidth: true
    spacing: Theme.space3

    Rectangle {
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 1
        implicitWidth: 24
        implicitHeight: 24
        radius: 12
        color: step.done ? Theme.successBg : Theme.bgSunken
        border.width: 1
        border.color: step.done ? Theme.success : Theme.border
        Text {
            anchors.centerIn: parent
            visible: !step.done
            text: step.number
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightSemiBold
            color: Theme.fgMuted
        }
        BIcon {
            anchors.centerIn: parent
            visible: step.done
            name: "check"
            size: Theme.iconSizeSm
            color: Theme.successText
        }
    }
    ColumnLayout {
        id: extraColumn
        Layout.fillWidth: true
        spacing: Theme.space2
        BText {
            Layout.fillWidth: true
            text: step.text
            wrapMode: Text.Wrap
        }
    }
}
