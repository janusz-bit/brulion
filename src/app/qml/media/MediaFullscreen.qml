pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// MediaFullscreen — a video over the whole window (MediaEmbedTile's "Full screen"; the window goes
// full screen too where it can, and back when this closes): the picture on black, the player's bar
// at the bottom (±10 s, volume), Escape / the button / Android's back close it. The picture moves
// here while it is open (MediaVideoSurface: the last surface attached wins) and back to the embed
// after, without a pause. Keys: Space plays or pauses, ← / → 5 s, M mutes, F or Escape leaves.
T.Popup {
    id: root

    required property string path
    property url source
    property real startMs: -1
    property real endMs: -1
    property real knownDuration: -1
    /// What opened it: its window is made full screen while this is open.
    property Item host: null

    parent: T.Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    modal: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape
    /// Room for the system's bars over an edge-to-edge window (Android).
    readonly property real safeBottom: parent && parent.SafeArea ? parent.SafeArea.margins.bottom : 0
    /// The window's visibility before (restored on close).
    property int previousVisibility: Window.Windowed

    background: Rectangle { color: "#000000" }

    onOpened: {
        const w = root.host ? root.host.Window.window : null
        if (w && Qt.platform.os !== "android" && Qt.platform.os !== "wasm") {
            root.previousVisibility = w.visibility
            w.visibility = Window.FullScreen
        }
        content.forceActiveFocus()
    }
    onAboutToHide: {
        const w = root.host ? root.host.Window.window : null
        if (w && Qt.platform.os !== "android" && Qt.platform.os !== "wasm" && w.visibility === Window.FullScreen)
            w.visibility = root.previousVisibility
    }

    contentItem: FocusScope {
        id: content
        objectName: "mediaFullscreen"
        Keys.onPressed: (event) => {
            const key = event.key
            if (key === Qt.Key_Space) {
                bar.toggle()
                event.accepted = true
            } else if (key === Qt.Key_Left) {
                MediaPlayback.skip(-5000)
                event.accepted = true
            } else if (key === Qt.Key_Right) {
                MediaPlayback.skip(5000)
                event.accepted = true
            } else if (key === Qt.Key_M) {
                MediaPlayback.muted = !MediaPlayback.muted
                event.accepted = true
            } else if (key === Qt.Key_F) {
                root.close()
                event.accepted = true
            }
        }
        MediaVideoSurface {
            id: surface
            anchors.fill: parent
            anchors.bottomMargin: barCard.height
            path: root.path
            active: root.opened
            Component.onCompleted: surface.claim()
        }
        TapHandler { onTapped: bar.toggle() }
        Rectangle {
            id: barCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: bar.implicitHeight + 2 * Theme.space2 + root.safeBottom
            color: Theme.bgElevated
            MediaControls {
                id: bar
                x: Theme.space3
                y: Theme.space2
                width: parent.width - 2 * Theme.space3
                path: root.path
                source: root.source
                startMs: root.startMs
                endMs: root.endMs
                knownDuration: root.knownDuration
                showSkip: true
                showFullscreen: true
                fullscreen: true
                showMore: false
                onFullscreenRequested: root.close()
            }
        }
    }
}
