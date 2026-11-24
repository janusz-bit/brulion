pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentMediaView — a sound or video file opened as a page (assets/Nagranie 2026-11-20 18.30.m4a,
// film.mp4; PageView's mediaRoute; docs/UI.md "Audio and video"): the player instead of the file's
// bytes as text. A video fills the view (its first frame until it plays; a click plays or pauses), a
// sound shows its icon; under it the player's bar (MediaControls with ±10 s, the volume, full
// screen for a video), and below, like a page's references, the notes that embed or link it
// (ContentBacklinks). The bar at the top names the file and has "Open in another app" (a new tab in
// the browser) and the speed. The route's `mediaStart` / `mediaEnd` (a link `[[x.mp3#t=83]]`,
// PageController.activateLink) start it there — once, when the view opens. Keys: Space plays or
// pauses, ← / → 5 s, M mutes, F full screen. Where nothing plays (a build without Qt Multimedia,
// the browser) it names the file and offers the other app.
//
//   ContentMediaView { path: routePath; route: root.route; rendererPalette: editor.rendererPalette }
FocusScope {
    id: root

    objectName: "mediaView" // (the shell's self-test)

    property string path
    property var route: ({})
    property var rendererPalette: ({})

    readonly property string fileName: {
        const i = Js.lastIndexOf(root.path, "/")
        return i < 0 ? root.path : Js.sliceFrom(root.path, i + 1)
    }
    readonly property bool playable: MediaPlayback.available
    readonly property url source: root.playable && root.path !== "" ? Workspace.assetUrl(root.path) : ""
    readonly property bool videoKind: Workspace.isVideo(root.path)
    readonly property MapHolder info: MapHolder {}
    readonly property string infoState: root.info.map.state || "unknown"
    readonly property bool video: root.videoKind && (root.info.map.hasVideo === true || root.infoState !== "ready")
    readonly property real infoDuration: root.info.map.duration === undefined ? -1 : root.info.map.duration
    readonly property string poster: root.info.map.poster || ""
    readonly property bool current: root.path !== "" && MediaPlayback.path === root.path
    readonly property bool started: root.current && (MediaPlayback.playing || MediaPlayback.position > 0)
    readonly property real startMs: AppController.routeText(root.route, "mediaStart") !== ""
                                    ? 0 + AppController.routeInt(root.route, "mediaStart", -1) : -1
    readonly property real endMs: AppController.routeText(root.route, "mediaEnd") !== ""
                                  ? 0 + AppController.routeInt(root.route, "mediaEnd", -1) : -1
    readonly property bool narrow: root.width < Theme.compactBreakpoint

    function refreshInfo(): void {
        if (root.playable && root.path !== "")
            root.info.map = MediaPlayback.info(root.path, root.source)
    }
    property string viewPath: ""
    function registerView(): void {
        if (root.viewPath !== "")
            MediaPlayback.removeView(root.viewPath)
        root.viewPath = root.path
        if (root.path !== "")
            MediaPlayback.addView(root.path)
    }
    onPathChanged: {
        registerView()
        refreshInfo()
    }
    Component.onCompleted: {
        registerView()
        refreshInfo()
        // A link to a moment (`#t=83`): it starts there.
        if (root.startMs >= 0 && root.playable && root.path !== "") {
            surface.claim()
            MediaPlayback.play(root.path, root.source, root.startMs, root.endMs)
        }
    }
    Component.onDestruction: if (root.viewPath !== "") MediaPlayback.removeView(root.viewPath)
    Connections {
        target: MediaPlayback
        function onInfoChanged(changed: string) {
            if (changed === root.path)
                root.refreshInfo()
        }
    }
    Connections {
        target: Workspace
        function onFileChanged(changed: string) {
            if (changed === root.path) {
                MediaPlayback.forget(root.path)
                root.refreshInfo()
            }
        }
    }

    Keys.onPressed: (event) => {
        if (!root.playable)
            return
        const key = event.key
        if (key === Qt.Key_Space || key === Qt.Key_K) {
            controls.toggle()
            event.accepted = true
        } else if (key === Qt.Key_Left && root.current) {
            MediaPlayback.skip(-5000)
            event.accepted = true
        } else if (key === Qt.Key_Right && root.current) {
            MediaPlayback.skip(5000)
            event.accepted = true
        } else if (key === Qt.Key_M) {
            MediaPlayback.muted = !MediaPlayback.muted
            event.accepted = true
        } else if (key === Qt.Key_F && root.video) {
            fullscreenLoader.active = true
            event.accepted = true
        }
    }

    // The bar: the file's name, the other app.
    Rectangle {
        id: bar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        implicitHeight: barRow.implicitHeight + Theme.space3 * 2
        color: Theme.bg
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.borderSubtle
        }
        RowLayout {
            id: barRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.space4
            spacing: Theme.space3
            BIcon {
                name: root.videoKind ? "film" : "audio-lines"
                size: Theme.iconSize
                color: Theme.fgMuted
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                objectName: "mediaViewName"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                text: root.fileName
                textFormat: Text.PlainText // a file name from anywhere (sync, imports) is no markup
                elide: Text.ElideMiddle
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
            }
            BButton {
                objectName: "mediaViewOpenElsewhere"
                visible: root.path !== "" && Workspace.canOpenInOtherApp(root.path)
                size: "small"
                variant: "secondary"
                iconName: "external-link"
                text: root.narrow ? qsTr("Open elsewhere")
                    : Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                Accessible.name: Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                Layout.alignment: Qt.AlignVCenter
                onClicked: Workspace.openInOtherApp(root.path)
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: bar.height
        clip: true
        contentWidth: width
        contentHeight: content.height + Theme.space8
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            x: root.narrow ? Theme.space4 : Theme.space8
            y: root.narrow ? Theme.space4 : Theme.space6
            width: flick.width - 2 * x
            spacing: Theme.space6

            // The picture (a video) or the sound's icon.
            Rectangle {
                id: screen
                objectName: "mediaViewScreen"
                visible: root.playable
                width: parent.width
                height: root.video ? Math.max(160, Math.min(flick.height * 0.7, width * 9 / 16)) : 160
                radius: Theme.radiusLg
                color: root.video ? "#000000" : Theme.bgSunken
                border.width: root.video ? 0 : 1
                border.color: Theme.borderSubtle
                clip: true
                Image {
                    anchors.fill: parent
                    visible: root.video && !root.started && root.poster !== ""
                    source: root.poster
                    // (A frame of the video, up to 4K: decoded for the view that shows it, in
                    // steps — not again at every width of a window being resized.)
                    sourceSize.width: PictureDecode.side(width, 0, Screen.devicePixelRatio)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                MediaVideoSurface {
                    id: surface
                    anchors.fill: parent
                    path: root.path
                    active: root.video
                }
                Column {
                    visible: !root.video
                    anchors.centerIn: parent
                    spacing: Theme.space2
                    BIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "audio-lines"
                        size: 48
                        color: MediaPlayback.playing && root.current ? Theme.accent : Theme.fgFaint
                    }
                    BText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.infoState === "failed"
                        text: qsTr("This file cannot be played on this device.")
                        variant: "muted"
                    }
                }
                Rectangle {
                    visible: root.video && !(root.current && MediaPlayback.playing)
                    anchors.centerIn: parent
                    width: 64
                    height: 64
                    radius: 32
                    color: "#99000000"
                    BIcon {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 2
                        name: "play"
                        size: 30
                        color: "#ffffff"
                    }
                }
                TapHandler {
                    onTapped: {
                        surface.claim()
                        controls.toggle()
                    }
                }
            }

            MediaControls {
                id: controls
                visible: root.playable
                width: parent.width
                path: root.path
                source: root.source
                startMs: root.startMs
                endMs: root.endMs
                knownDuration: root.infoDuration
                showSkip: !root.narrow
                showVolume: !root.narrow && !Theme.touchUi
                showFullscreen: root.video
                showMore: false
                onFullscreenRequested: fullscreenLoader.active = true
            }

            // Nothing plays here: the other app (a new tab in the browser).
            BEmptyState {
                objectName: "mediaViewUnplayable"
                visible: !root.playable
                width: parent.width
                iconName: root.videoKind ? "film" : "audio-lines"
                title: root.fileName
                description: Qt.platform.os === "wasm" ? qsTr("The browser plays it in a new tab.")
                                                       : qsTr("This version of Brulion plays no audio or video: open it in another app.")
                actionText: Workspace.canOpenInOtherApp(root.path)
                            ? Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app") : ""
                actionIcon: "external-link"
                onActionTriggered: Workspace.openInOtherApp(root.path)
            }

            ContentBacklinks {
                id: refs
                objectName: "mediaReferences"
                visible: !empty
                width: Math.min(parent.width, 760)
                path: root.path
                rendererPalette: root.rendererPalette
                compact: Theme.compact
                showUnlinked: false
            }
        }
    }

    Loader {
        id: fullscreenLoader
        active: false
        sourceComponent: MediaFullscreen {
            path: root.path
            source: root.source
            startMs: root.startMs
            endMs: root.endMs
            knownDuration: root.infoDuration
            host: root
            onClosed: fullscreenLoader.active = false
        }
        onLoaded: (fullscreenLoader.item as MediaFullscreen).open()
    }
}
