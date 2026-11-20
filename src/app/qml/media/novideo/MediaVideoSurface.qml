pragma ComponentBehavior: Bound
import QtQuick

// MediaVideoSurface's stand-in where the build plays no video (BRULION_MULTIMEDIA off: the media
// area's CMake picks this file instead of qml/media/video/): the same interface, no picture — the
// views show a video's poster or icon and "Open in another app".
Item {
    id: root

    property string path
    property bool active: true
    readonly property bool showing: false

    function sync(): void {
    }
    function claim(): void {
    }
}
