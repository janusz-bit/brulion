pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// VoiceRecorderDialog — recording a voice note (docs/UI.md "Voice notes"; the slash command "Voice
// note", the touch toolbar's microphone, "Record voice note" in the palette and quick capture): it
// asks for the microphone and records as soon as it opens — the time so far, a level meter of the
// last seconds, Pause / Resume, and — with a speech model here and "Text while recording" on —
// the words heard so far (LiveTranscript; the transcript itself is the whole recording heard again
// once saved); "Save" ends the recording and recorded(file) hands the file over
// (the editor stores and embeds it at the caret, the shell into today's journal); Cancel, Escape,
// the back gesture throw it away. A denied microphone or a failure says why and offers Try again.
// The dialog stays (closed) until the file is whole: saving takes a moment after the last word;
// released() says when the owner may destroy it.
//
//   VoiceRecorderDialog {
//       onRecorded: (file) => controller.insertRecording(row, cursor, cursor, file)
//       onReleased: later.restart() // (→ loader.active = false, outside the signal)
//   }
BDialog {
    id: dialog

    /// The recording is whole: `file` (in the cache folder) is the owner's to store.
    signal recorded(string file)
    /// Nothing came of it (cancelled, failed).
    signal abandoned()
    /// Closed, and its recording handed over or thrown away: the owner may destroy the dialog now.
    /// Not before: a popup destroyed while it still closes never tells Qt's overlay it is gone, and
    /// the next menu to close anywhere in the window gives the keyboard to what had it when this
    /// dialog opened (an editor long hidden) instead of to what had it before that menu.
    signal released()
    property bool settled_: false
    function settle(): void {
        dialog.settled_ = true
        if (!dialog.visible)
            dialog.released()
    }

    readonly property string state_: recorder.state
    readonly property bool active: dialog.state_ === "recording" || dialog.state_ === "paused"
    readonly property bool problem: dialog.state_ === "denied" || dialog.state_ === "failed"

    title: qsTr("Voice note")
    iconName: "mic"
    subtitle: AppController.speech.ready && AppController.speech.automatic
              ? qsTr("Saved in the note and transcribed on this device")
              : qsTr("Saved in the note as a recording")
    acceptText: qsTr("Save")
    acceptEnabled: dialog.active && recorder.duration > 0
    rejectText: qsTr("Cancel")

    onAboutToShow: dialog.settled_ = false
    onOpened: recorder.start()
    onAccepted: recorder.stop()
    onRejected: {
        recorder.cancel()
        dialog.abandoned()
        dialog.settle()
    }
    onClosed: if (dialog.settled_) dialog.released()

    VoiceRecorder {
        id: recorder
        objectName: "voiceRecorder"
        namePrefix: qsTr("Recording", "a voice note's file name: Recording 2026-11-20 18.30.m4a")
        onFinished: (file) => {
            dialog.recorded(file)
            dialog.settle()
        }
        onFailed: (error) => {
            // Failed after the dialog closed (while saving): the owner says so.
            if (!dialog.opened) {
                dialog.abandoned()
                dialog.settle()
            }
        }
    }

    // Text while recording (docs/UI.md "Voice notes"): the windows heard as it records.
    LiveTranscript {
        id: live
        objectName: "voiceRecorderLiveText"
        recorder: recorder
        transcriber: AppController.speech
        tailChars: 240
    }

    // The time recorded so far.
    BText {
        objectName: "voiceRecorderTime"
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        horizontalAlignment: Text.AlignHCenter
        text: MediaPlayback.timeText(recorder.duration, recorder.duration)
        font.pixelSize: Theme.text3xl
        font.weight: Theme.weightMedium
        font.features: { "tnum": 1 }
        color: dialog.state_ === "paused" ? Theme.fgMuted : Theme.fg
    }

    // The level meter: the last seconds, newest on the right.
    Item {
        objectName: "voiceRecorderMeter"
        Layout.fillWidth: true
        Layout.preferredHeight: 56
        // The system records by itself (Qt's native Android backend): no level, a microphone that
        // breathes while it records.
        BIcon {
            visible: !recorder.metered
            anchors.centerIn: parent
            name: "mic"
            size: 32
            color: dialog.state_ === "recording" ? Theme.accent : Theme.fgFaint
            SequentialAnimation on opacity {
                running: !recorder.metered && dialog.state_ === "recording" && !Theme.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 700 }
                NumberAnimation { to: 1; duration: 700 }
            }
        }
        Row {
            id: bars
            visible: recorder.metered
            anchors.centerIn: parent
            spacing: 3
            readonly property list<real> levels: recorder.levels
            readonly property int count: 48
            readonly property real barWidth: Math.max(2, Math.min(6, (dialog.availableWidth - (count - 1) * spacing) / count))
            Repeater {
                model: bars.count
                delegate: Item {
                    id: bar
                    required property int index
                    readonly property int fromEnd: bars.count - 1 - bar.index
                    readonly property real level: bar.fromEnd < bars.levels.length ? bars.levels[bars.levels.length - 1 - bar.fromEnd] : 0
                    width: bars.barWidth
                    height: 56
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Math.max(3, bar.level * parent.height)
                        radius: width / 2
                        color: dialog.state_ === "recording" ? Theme.accent : Theme.fgFaint
                    }
                }
            }
        }
    }

    // What it does now, or why it does not.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        Rectangle {
            visible: dialog.state_ === "recording"
            width: 8
            height: 8
            radius: 4
            color: Theme.danger
            Layout.alignment: Qt.AlignVCenter
            SequentialAnimation on opacity {
                running: dialog.state_ === "recording" && !Theme.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { to: 0.25; duration: 600 }
                NumberAnimation { to: 1; duration: 600 }
            }
        }
        BText {
            objectName: "voiceRecorderState"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            variant: dialog.problem ? "body" : "muted"
            color: dialog.problem ? Theme.dangerText : Theme.fgMuted
            text: dialog.state_ === "asking" ? qsTr("Asking for the microphone…")
                  : dialog.state_ === "recording" ? qsTr("Recording", "what the recorder does now")
                  : dialog.state_ === "paused" ? qsTr("Paused")
                  : dialog.state_ === "saving" ? qsTr("Saving…")
                  : dialog.state_ === "denied" ? qsTr("Brulion may not use the microphone. Allow it in the system's settings, then try again.")
                  : dialog.state_ === "failed" ? recorder.error
                  : ""
        }
    }

    // What was heard so far (its end), or why nothing is heard any more.
    BText {
        objectName: "voiceRecorderLive"
        Layout.fillWidth: true
        visible: dialog.active && live.status !== "off"
        wrapMode: Text.Wrap
        maximumLineCount: 5
        variant: live.text === "" || live.status === "slow" ? "muted" : "body"
        text: live.status === "slow"
              ? qsTr("Text while recording stopped: this device hears it too slowly. The transcript comes once you save.")
              : live.text !== "" ? live.tail : qsTr("Listening…", "text while recording, before the first words")
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BButton {
            objectName: "voiceRecorderPause"
            visible: dialog.active
            variant: "secondary"
            iconName: dialog.state_ === "paused" ? "mic" : "pause"
            text: dialog.state_ === "paused" ? qsTr("Resume") : qsTr("Pause")
            onClicked: {
                if (dialog.state_ === "paused")
                    recorder.resume()
                else
                    recorder.pause()
            }
        }
        BButton {
            objectName: "voiceRecorderRetry"
            visible: dialog.problem
            variant: "secondary"
            iconName: "refresh-cw"
            text: qsTr("Try again")
            onClicked: recorder.start()
        }
        Item { Layout.fillWidth: true }
        BText {
            visible: recorder.device !== "" && recorder.device !== "test"
            Layout.maximumWidth: dialog.availableWidth / 2
            text: recorder.device
            variant: "faint"
            font.pixelSize: Theme.textXs
            elide: Text.ElideRight
        }
    }
}
