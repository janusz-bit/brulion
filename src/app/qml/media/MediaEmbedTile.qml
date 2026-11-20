pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// MediaEmbedTile — a recording or a video embedded in a block (`![[Nagranie 2026-11-20 18.30.m4a]]`,
// `![](film.mp4#t=83)`; EmbedView, BlockListModel's media embed: {media, kind: "audio" | "video",
// path, target, name, start, end}; docs/UI.md "Audio and video"). A card with the file's name and
// the player's bar (MediaControls) — a video with its picture above it: the first frame
// (MediaPlayback.info's poster) with a play button until it plays, then the picture itself
// (MediaVideoSurface), a click on it plays or pauses, "Full screen" shows it over the window
// (MediaFullscreen). The bar steers the app's one player (MediaPlayback): one file plays at a time,
// a sound plays on when its block scrolls away (the now-playing bar shows it), a video pauses.
// "More": open it in the player view, transcribe it (AppController.speech, the transcript goes
// under the block), copy a link to the moment it is at (`[[name#t=83]]`), open it in another app.
// While it is transcribed a row under the bar says how far, with Stop.
// Where nothing plays (a build without Qt Multimedia, the browser) the card names the file and
// offers "Open in another app" (a new tab in the browser).
//
//   MediaEmbedTile { entry: embed.modelData; columnWidth: root.width
//                    onTranscribeRequested: (path, target) => …; onCopyRequested: (text) => … }
Item {
    id: tile

    required property var entry
    property real columnWidth: 480
    signal transcribeRequested(string path, string target)
    signal copyRequested(string text)

    readonly property MapHolder rec: MapHolder { map: tile.entry }
    readonly property string path: tile.rec.map.path || ""
    readonly property string target: tile.rec.map.target || ""
    readonly property string name: tile.rec.map.name || ""
    readonly property bool videoKind: tile.rec.map.kind === "video"
    readonly property real startMs: tile.rec.map.start === undefined ? -1 : tile.rec.map.start
    readonly property real endMs: tile.rec.map.end === undefined ? -1 : tile.rec.map.end
    readonly property bool playable: MediaPlayback.available
    readonly property url source: tile.playable && tile.path !== "" ? Workspace.assetUrl(tile.path) : ""
    /// What the file is (MediaPlayback.info; read again when it is known).
    readonly property MapHolder info: MapHolder {}
    readonly property string infoState: tile.info.map.state || "unknown"
    readonly property real infoDuration: tile.info.map.duration === undefined ? -1 : tile.info.map.duration
    readonly property string poster: tile.info.map.poster || ""
    /// A video until the file says it has no picture (a .webm of sound alone: the audio card).
    readonly property bool video: tile.videoKind && (tile.info.map.hasVideo === true || tile.infoState !== "ready")
    readonly property real videoAspect: (tile.info.map.width || 0) > 0 && (tile.info.map.height || 0) > 0
                                        ? tile.info.map.width / tile.info.map.height : 16 / 9
    readonly property bool current: tile.path !== "" && MediaPlayback.path === tile.path
    readonly property bool playing: tile.current && MediaPlayback.playing
    readonly property bool started: tile.current && (MediaPlayback.playing || MediaPlayback.position > 0)
    readonly property bool transcribing: AppController.speech.queue.includes(tile.path)
    readonly property bool hearingThis: AppController.speech.queue.length > 0 && AppController.speech.queue[0] === tile.path

    width: Math.min(tile.columnWidth, tile.video ? 640 : 560)
    height: card.height

    function refreshInfo(): void {
        if (tile.playable && tile.path !== "")
            tile.info.map = MediaPlayback.info(tile.path, tile.source)
    }
    property string viewPath: ""
    function registerView(): void {
        if (tile.viewPath === tile.path)
            return
        if (tile.viewPath !== "")
            MediaPlayback.removeView(tile.viewPath)
        tile.viewPath = tile.path
        if (tile.path !== "")
            MediaPlayback.addView(tile.path)
    }
    onPathChanged: {
        registerView()
        refreshInfo()
    }
    Component.onCompleted: {
        registerView()
        refreshInfo()
    }
    Component.onDestruction: if (tile.viewPath !== "") MediaPlayback.removeView(tile.viewPath)
    Connections {
        target: MediaPlayback
        function onInfoChanged(changed: string) {
            if (changed === tile.path)
                tile.refreshInfo()
        }
    }

    Rectangle {
        id: card
        objectName: "mediaEmbed"
        width: parent.width
        height: column.implicitHeight + (tile.video ? 0 : 2 * Theme.space2)
        radius: Theme.radius
        color: Theme.bgSunken
        border.width: 1
        border.color: Theme.borderSubtle
        clip: true

        ColumnLayout {
            id: column
            x: tile.video ? 0 : Theme.space3
            y: tile.video ? 0 : Theme.space2
            width: parent.width - 2 * x
            spacing: tile.video ? 0 : Theme.space1

            // A video's picture: its poster, then the picture itself.
            Rectangle {
                id: screen
                objectName: "mediaScreen"
                visible: tile.video && tile.playable
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(360, Math.round(width / tile.videoAspect))
                color: "#000000"
                Image {
                    anchors.fill: parent
                    visible: !tile.started && tile.poster !== ""
                    source: tile.poster
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                }
                MediaVideoSurface {
                    id: surface
                    anchors.fill: parent
                    path: tile.path
                    active: tile.video && tile.playable
                }
                Rectangle {
                    objectName: "mediaBigPlay"
                    visible: !tile.playing
                    anchors.centerIn: parent
                    width: 56
                    height: 56
                    radius: 28
                    color: "#99000000"
                    BIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        name: "play"
                        size: 26
                        color: "#ffffff"
                    }
                }
                BSpinner {
                    anchors.centerIn: parent
                    size: 28
                    color: "#ffffff"
                    running: tile.current && MediaPlayback.loading
                    visible: running
                }
                TapHandler {
                    onTapped: {
                        surface.claim()
                        controls.toggle()
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor; enabled: !Theme.touchUi }
            }

            // The file's name (plain text: a name from anywhere is no markup).
            RowLayout {
                visible: !tile.video || !tile.playable
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon {
                    name: tile.videoKind ? "film" : "audio-lines"
                    size: Theme.iconSizeSm
                    color: Theme.fgMuted
                }
                BText {
                    objectName: "mediaName"
                    Layout.fillWidth: true
                    text: tile.name
                    variant: "small"
                    color: Theme.fgMuted
                    elide: Text.ElideMiddle
                }
                BText {
                    visible: tile.infoState === "failed" && tile.playable
                    text: qsTr("Cannot be played here")
                    variant: "small"
                    color: Theme.warningText
                }
            }

            MediaControls {
                id: controls
                visible: tile.playable
                Layout.fillWidth: true
                Layout.leftMargin: tile.video ? Theme.space2 : 0
                Layout.rightMargin: tile.video ? Theme.space2 : 0
                path: tile.path
                source: tile.source
                startMs: tile.startMs
                endMs: tile.endMs
                knownDuration: tile.infoDuration
                showVolume: tile.width >= 460 && !Theme.touchUi
                showRate: tile.width >= 340
                showFullscreen: tile.video
                onFullscreenRequested: fullscreenLoader.active = true
                onMoreRequested: (anchor) => {
                    menuLoader.active = true
                    const menu = menuLoader.item as MediaEmbedMenu
                    if (menu)
                        menu.popup(anchor, 0, anchor.height)
                }
                Component.onCompleted: if (tile.video) surface.claim()
            }

            // Being transcribed: how far, and Stop.
            RowLayout {
                objectName: "mediaTranscribing"
                visible: tile.transcribing
                Layout.fillWidth: true
                Layout.leftMargin: tile.video ? Theme.space3 : 0
                Layout.bottomMargin: tile.video ? Theme.space2 : 0
                spacing: Theme.space2
                BSpinner { size: Theme.iconSizeSm; running: parent.visible }
                BText {
                    Layout.fillWidth: true
                    variant: "small"
                    color: Theme.fgMuted
                    elide: Text.ElideRight
                    text: !tile.hearingThis ? qsTr("Waiting to be transcribed…")
                          : AppController.speech.jobStage === "decoding" ? qsTr("Reading the recording…")
                          : AppController.speech.jobStage === "loading" ? qsTr("Opening the speech model…")
                          : AppController.speech.jobProgress <= 0 ? qsTr("Transcribing…")
                          : qsTr("Transcribing… %1 %").arg(Math.round(AppController.speech.jobProgress * 100))
                }
                BButton {
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Stop")
                    onClicked: AppController.speech.cancel(tile.path)
                }
            }

            // Nothing plays here: the system's app (a new tab in the browser).
            RowLayout {
                visible: !tile.playable
                Layout.fillWidth: true
                BText {
                    Layout.fillWidth: true
                    variant: "small"
                    color: Theme.fgMuted
                    wrapMode: Text.Wrap
                    text: Qt.platform.os === "wasm" ? qsTr("Plays in a new tab of the browser.")
                                                    : qsTr("This version of Brulion plays no audio or video.")
                }
                BButton {
                    objectName: "mediaOpenElsewhere"
                    size: "small"
                    variant: "secondary"
                    iconName: "external-link"
                    text: Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                    onClicked: Workspace.openInOtherApp(tile.path)
                }
            }
        }
    }

    Loader {
        id: menuLoader
        active: false
        sourceComponent: MediaEmbedMenu {
            path: tile.path
            target: tile.target
            onTranscribeRequested: tile.transcribeRequested(tile.path, tile.target)
            onCopyRequested: (text) => tile.copyRequested(text)
        }
    }
    Loader {
        id: fullscreenLoader
        active: false
        sourceComponent: MediaFullscreen {
            path: tile.path
            source: tile.source
            startMs: tile.startMs
            endMs: tile.endMs
            knownDuration: tile.infoDuration
            host: tile
            onClosed: fullscreenLoader.active = false
        }
        onLoaded: (fullscreenLoader.item as MediaFullscreen).open()
    }
}
