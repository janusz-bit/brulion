pragma ComponentBehavior: Bound
import QtQuick
import QtMultimedia
import Brulion

// MediaVideoSurface — the picture of a video MediaPlayback plays, where the build plays video (the
// media area's CMake picks this file: qml/media/novideo/ has the stand-in). It shows the picture of
// `path` while that file is the loaded one and the surface is `active` (MediaPlayback.attachVideo:
// the last surface attached wins — the player view over an embed of the same file); when it goes
// (its block scrolled away, the view closed) the video pauses (detachVideo: a video plays only
// where it is seen).
//
//   MediaVideoSurface { anchors.fill: parent; path: tile.path; active: tile.visible }
Item {
    id: root

    property string path
    property bool active: true
    /// It shows the loaded file's picture now.
    readonly property bool showing: root.active && root.path !== "" && MediaPlayback.path === root.path

    VideoOutput {
        id: output
        objectName: "mediaVideoOutput"
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    function sync(): void {
        if (root.showing)
            MediaPlayback.attachVideo(root.path, output)
        else
            MediaPlayback.detachVideo(output)
    }
    /// Takes the picture before the file starts (MediaPlayback.play loads it into this surface).
    function claim(): void {
        if (root.active && root.path !== "")
            MediaPlayback.attachVideo(root.path, output)
    }

    onShowingChanged: sync()
    Component.onCompleted: sync()
    Component.onDestruction: MediaPlayback.detachVideo(output)
}
