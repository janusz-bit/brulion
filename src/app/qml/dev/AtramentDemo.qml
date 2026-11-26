pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Brulion

// Atrament inside the app (`brulion --atrament`, docs/ATRAMENT.md M2): a QML window of the design
// system with an Atrament view under its bar — how views move from QML to Atrament one by one.
ApplicationWindow {
    id: window

    width: 1280
    height: 800
    visible: true
    title: "Brulion — Atrament"
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    header: Rectangle {
        implicitHeight: 48
        color: Theme.bgSunken

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            spacing: 12

            BText {
                anchors.verticalCenter: parent.verticalCenter
                text: "QML: ten pasek · Atrament: wszystko poniżej"
                color: Theme.fgMuted
            }
        }
    }

    // Above the on-screen keyboard (Android, the browser), as the app's views end at its top.
    AtramentView {
        anchors.fill: parent
        anchors.bottomMargin: KeyboardInsets.overlap
        view: "gallery"
        focus: true
    }
}
