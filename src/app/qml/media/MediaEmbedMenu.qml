pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// MediaEmbedMenu — "More" of a media embed (MediaEmbedTile) and of the player view: open the file in
// the player view, transcribe it (AppController.speech: the transcript goes under the block — the
// owner does it, transcribeRequested()), copy a link to the moment it is at (`[[name#t=83]]`,
// copyRequested(text)), open it in another app (a new tab in the browser).
BMenu {
    id: menu

    property string path
    /// The embed's target as written (the link copied names the file so).
    property string target
    /// The player view itself has no "Open in the player" (and nothing to transcribe into).
    property bool inPlayer: false
    signal transcribeRequested()
    signal copyRequested(string text)

    readonly property real at: MediaPlayback.path === menu.path ? MediaPlayback.position : MediaPlayback.positionOf(menu.path)

    BMenuItem {
        objectName: "mediaOpenPlayer"
        visible: !menu.inPlayer
        text: qsTr("Open in the player")
        iconName: "external-link"
        onTriggered: AppController.navigate({ view: "page", path: menu.path })
    }
    BMenuItem {
        objectName: "mediaTranscribe"
        visible: !menu.inPlayer && AppController.speech.available
        text: qsTr("Transcribe")
        iconName: "captions"
        unavailable: !AppController.speech.ready ? qsTr("Download a speech model first: Settings → Extensions → Speech.")
                     : AppController.speech.queue.includes(menu.path) ? qsTr("It is being transcribed.") : ""
        onTriggered: {
            if (!AppController.speech.ready)
                AppController.navigate({ view: "settings", section: "extensions" })
            else if (unavailable === "")
                menu.transcribeRequested()
        }
    }
    BMenuItem {
        objectName: "mediaCopyMoment"
        visible: !menu.inPlayer
        text: qsTr("Copy link to %1").arg(MediaPlayback.timeText(menu.at, -1))
        iconName: "link"
        onTriggered: menu.copyRequested(MediaPlayback.momentLink(menu.target !== "" ? menu.target : menu.path, menu.at))
    }
    BMenuItem {
        objectName: "mediaMenuOpenElsewhere"
        visible: Workspace.canOpenInOtherApp(menu.path)
        text: Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
        iconName: "external-link"
        onTriggered: Workspace.openInOtherApp(menu.path)
    }
}
