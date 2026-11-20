pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// MediaNowPlaying — the sound that plays on while nothing on screen shows it (its block scrolled
// away, its page closed: MediaPlayback.views is 0; docs/UI.md "Audio and video"): a small bar
// floating over the bottom of the window with the file's name (a click opens it in the player
// view), where it is, play / pause and close (MediaPlayback.stop). A video paused when it left
// the screen shows here too: its play button opens the player view (a video plays only where it is
// seen).
//
//   MediaNowPlaying { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: …; }
Item {
    id: root

    objectName: "mediaNowPlaying"
    readonly property bool wanted: MediaPlayback.available && MediaPlayback.path !== "" && MediaPlayback.views === 0
    visible: root.wanted
    implicitWidth: Math.min(row.implicitWidth + Theme.space3 + Theme.space1, 520)
    implicitHeight: row.implicitHeight + 2 * Theme.space1

    BShadow { target: card; level: 2 }
    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.border
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.space3
        anchors.rightMargin: Theme.space1
        spacing: Theme.space2
        BIcon {
            name: MediaPlayback.hasVideo ? "film" : "audio-lines"
            size: Theme.iconSizeSm
            color: MediaPlayback.playing ? Theme.accent : Theme.fgMuted
        }
        BText {
            id: name
            objectName: "mediaNowPlayingName"
            Layout.fillWidth: true
            Layout.maximumWidth: 260
            text: MediaPlayback.title
            variant: "small"
            elide: Text.ElideMiddle
            TapHandler { onTapped: AppController.navigate({ view: "page", path: MediaPlayback.path }) }
            HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: !Theme.touchUi }
        }
        BText {
            text: MediaPlayback.timeText(MediaPlayback.position, MediaPlayback.duration)
                  + (MediaPlayback.duration > 0 ? " / " + MediaPlayback.timeText(Math.round(MediaPlayback.duration / 1000) * 1000, MediaPlayback.duration) : "")
            variant: "small"
            color: Theme.fgMuted
            font.features: { "tnum": 1 }
        }
        BIconButton {
            focusPolicy: Qt.TabFocus
            objectName: "mediaNowPlayingToggle"
            size: "small"
            iconName: MediaPlayback.playing ? "pause" : "play"
            label: MediaPlayback.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: {
                if (MediaPlayback.playing)
                    MediaPlayback.pause()
                else if (MediaPlayback.hasVideo)
                    AppController.navigate({ view: "page", path: MediaPlayback.path })
                else
                    MediaPlayback.play(MediaPlayback.path, Workspace.assetUrl(MediaPlayback.path), -1, MediaPlayback.endPosition)
            }
        }
        BIconButton {
            focusPolicy: Qt.TabFocus
            objectName: "mediaNowPlayingClose"
            size: "small"
            iconName: "x"
            label: qsTr("Close the player")
            onClicked: MediaPlayback.stop()
        }
    }
}
