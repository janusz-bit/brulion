import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 1100
    height: 760
    visible: true
    title: qsTr("Brulion")

    Label {
        anchors.centerIn: parent
        text: qsTr("Brulion — offline-first notes")
    }
}
