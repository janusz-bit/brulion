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
        id: bar

        // Below the status bar of an edge-to-edge window (Android).
        implicitHeight: 48 + bar.SafeArea.margins.top
        color: Theme.bgSunken

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: bar.SafeArea.margins.top / 2
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

    // Above the on-screen keyboard (Android, the browser), as the app's views end at its top: by
    // what of it the keyboard covers (Qt's safe area may already have lifted the window's content
    // by the keyboard's height).
    AtramentView {
        id: atrament

        property real keyboardCover: 0
        // Its top in the window (a method's point result is read through a typed property).
        property point _top

        function placeAboveKeyboard(): void {
            atrament._top = atrament.mapToItem(null, 0, 0)
            const bottom = atrament._top.y + atrament.height + atrament.keyboardCover
            atrament.keyboardCover = Math.max(0, bottom - (window.height - KeyboardInsets.overlap))
        }

        anchors.fill: parent
        anchors.bottomMargin: keyboardCover
        view: "gallery"
        focus: true
        onHeightChanged: placeAboveKeyboard()

        Connections {
            target: KeyboardInsets

            function onOverlapChanged(): void {
                atrament.placeAboveKeyboard()
            }
        }
    }
}
