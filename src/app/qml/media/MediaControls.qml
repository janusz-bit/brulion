pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// MediaControls — the bar of a player (MediaEmbedTile under a recording or a video in a note,
// ContentMediaView): play / pause, where it is and how long it is ("1:23 / 3:42"), the seek bar,
// the speed (a click: the next of MediaPlayback.rates(), 0.5× … 2×), the sound (mute; beside it the
// volume where there is room), ±10 s and fullscreen where asked for, and "More" (the owner's menu).
// It steers MediaPlayback, the app's one player: it shows that player's state while `path` is the
// file loaded, else where `path` was left (MediaPlayback.positionOf) and the length the owner knows
// (`knownDuration`, MediaPlayback.info). A press on the seek bar of a file not loaded starts it
// there. `startMs` / `endMs`: the embed's time fragment (`#t=83`, `#t=10,20`). A click on the bar
// never takes the keyboard (Tab reaches it): pausing a recording does not stop the writing.
//
//   MediaControls { path: tile.path; source: tile.url; knownDuration: tile.infoDuration
//                   onMoreRequested: (item) => menu.popup(item, 0, item.height) }
RowLayout {
    id: root

    required property string path
    property url source
    property real startMs: -1
    property real endMs: -1
    property real knownDuration: -1
    /// The volume slider beside the mute button (wide layouts).
    property bool showVolume: !Theme.compact
    /// The speed button (a phone's narrow tile puts it into "More").
    property bool showRate: true
    /// Back and forward by 10 s (the player view).
    property bool showSkip: false
    property bool showFullscreen: false
    property bool fullscreen: false
    property bool showMore: true
    signal fullscreenRequested()
    signal moreRequested(Item anchor)

    readonly property bool current: root.path !== "" && MediaPlayback.path === root.path
    readonly property bool playing: root.current && MediaPlayback.playing
    readonly property bool loading: root.current && MediaPlayback.loading
    readonly property real duration: root.current && MediaPlayback.duration > 0 ? MediaPlayback.duration : root.knownDuration
    /// (Re-read when another file loads: the one before remembers its place then.)
    /// Not loaded: where it was left, else where its fragment starts.
    readonly property real leftAt: MediaPlayback.path.length >= 0 ? MediaPlayback.positionOf(root.path) : 0
    readonly property real position: root.current ? MediaPlayback.position
                                                  : root.leftAt > 0 ? root.leftAt : Math.max(0, root.startMs)
    readonly property string positionText: MediaPlayback.timeText(root.position, root.duration)
    /// (Whole seconds, rounded: a file of 2.999 s is "0:03" long.)
    readonly property string durationText: root.duration > 0
                                           ? MediaPlayback.timeText(Math.round(root.duration / 1000) * 1000, root.duration) : ""

    spacing: Theme.space1

    function toggle(): void {
        MediaPlayback.toggle(root.path, root.source, root.startMs, root.endMs)
    }
    function seekTo(ms: real): void {
        if (root.current)
            MediaPlayback.seek(ms)
        else
            MediaPlayback.play(root.path, root.source, ms, root.endMs)
    }

    BIconButton {

        focusPolicy: Qt.TabFocus
        id: playButton
        objectName: "mediaPlayButton"
        iconName: root.playing ? "pause" : "play"
        label: root.playing ? qsTr("Pause") : qsTr("Play")
        enabled: MediaPlayback.available && root.path !== ""
        onClicked: root.toggle()
        BSpinner {
            anchors.centerIn: parent
            size: playButton.visualSize - 6
            running: root.loading
            visible: running
        }
    }
    BIconButton {
        focusPolicy: Qt.TabFocus
        visible: root.showSkip
        iconName: "rotate-ccw"
        label: qsTr("Back 10 seconds")
        enabled: root.current
        onClicked: MediaPlayback.skip(-10000)
    }
    BIconButton {
        focusPolicy: Qt.TabFocus
        visible: root.showSkip
        iconName: "rotate-cw"
        label: qsTr("Forward 10 seconds")
        enabled: root.current
        onClicked: MediaPlayback.skip(10000)
    }
    BText {
        objectName: "mediaPosition"
        text: root.positionText
        font.pixelSize: Theme.textSm
        font.features: { "tnum": 1 }
        color: Theme.fgMuted
        Layout.alignment: Qt.AlignVCenter
    }
    MediaSlider {
        id: seekBar
        objectName: "mediaSeekBar"
        Layout.fillWidth: true
        Layout.minimumWidth: 48
        Layout.alignment: Qt.AlignVCenter
        enabled: MediaPlayback.available && root.duration > 0
        // (A share of the length, not milliseconds: a length that arrives after the position
        // would have clamped the position to the old end.)
        from: 0
        to: 1
        label: qsTr("Position")
        valueText: root.positionText
        // (A binding while not dragged: the player moves it.)
        Binding on value {
            when: !seekBar.pressed
            value: root.duration > 0 ? Math.min(1, root.position / root.duration) : 0
        }
        onMoved: root.seekTo(seekBar.value * root.duration)
    }
    BText {
        objectName: "mediaDuration"
        visible: root.durationText !== ""
        text: root.durationText
        font.pixelSize: Theme.textSm
        font.features: { "tnum": 1 }
        color: Theme.fgMuted
        Layout.alignment: Qt.AlignVCenter
    }
    BButton {
        focusPolicy: Qt.TabFocus
        objectName: "mediaRateButton"
        visible: root.showRate
        size: "small"
        variant: "ghost"
        text: MediaPlayback.rateText(MediaPlayback.rate)
        Accessible.name: qsTr("Speed %1").arg(MediaPlayback.rateText(MediaPlayback.rate))
        Layout.alignment: Qt.AlignVCenter
        onClicked: MediaPlayback.cycleRate()
        HoverHandler { id: rateHover; enabled: !Theme.touchUi }
        BToolTip {
            visible: rateHover.hovered
            text: qsTr("Speed: a click for the next one")
        }
    }
    BIconButton {
        focusPolicy: Qt.TabFocus
        objectName: "mediaMuteButton"
        iconName: MediaPlayback.muted || MediaPlayback.volume <= 0 ? "volume-x" : "volume-2"
        label: MediaPlayback.muted ? qsTr("Unmute") : qsTr("Mute")
        visible: !Theme.compact
        onClicked: MediaPlayback.muted = !MediaPlayback.muted
    }
    MediaSlider {
        id: volumeBar
        objectName: "mediaVolume"
        visible: root.showVolume
        Layout.preferredWidth: 72
        Layout.alignment: Qt.AlignVCenter
        from: 0
        to: 1
        label: qsTr("Volume")
        valueText: Math.round(volumeBar.value * 100) + " %"
        Binding on value {
            when: !volumeBar.pressed
            value: MediaPlayback.muted ? 0 : MediaPlayback.volume
        }
        onMoved: MediaPlayback.volume = volumeBar.value
    }
    BIconButton {
        focusPolicy: Qt.TabFocus
        objectName: "mediaFullscreenButton"
        visible: root.showFullscreen
        iconName: root.fullscreen ? "minimize" : "maximize"
        label: root.fullscreen ? qsTr("Exit full screen") : qsTr("Full screen")
        onClicked: root.fullscreenRequested()
    }
    BIconButton {
        focusPolicy: Qt.TabFocus
        id: moreButton
        objectName: "mediaMoreButton"
        visible: root.showMore
        iconName: "ellipsis"
        label: qsTr("More")
        onClicked: root.moreRequested(moreButton)
    }
}
