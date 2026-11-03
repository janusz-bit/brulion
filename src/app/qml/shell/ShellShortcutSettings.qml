pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellShortcutSettings — the "Skróty klawiszowe" section of SettingsView (docs/UI.md "Custom
// shortcuts"; C++: ShortcutMap, shell/shortcutmap.h): every command of the palette with its
// shortcut, a search over titles and keys, "Zmień" records a new key (the next key press with
// Ctrl, Alt or a function key; Esc cancels), what cannot be a shortcut said at once, another
// command's shortcut offered with "Przypisz tutaj" (that command is left without one), a
// command's own default back, no shortcut at all, everything back to the defaults. A device
// setting: every device keeps its own.
ColumnLayout {
    id: section

    /// The command whose new shortcut is being recorded ("" when none).
    property string recording: ""
    /// ShortcutMap.check() of the key pressed while recording ({} before one).
    property var verdict: ({})
    property string query: ""

    function startRecording(id) {
        section.verdict = ({})
        section.recording = id
        recorder.forceActiveFocus()
    }
    function stopRecording() {
        section.recording = ""
        section.verdict = ({})
    }
    /// A key pressed while recording: assigned when it can be, else the reason stays shown.
    function record(sequence) {
        const verdict = ShortcutMap.check(section.recording, sequence)
        if (verdict.status === "ok" || verdict.status === "same") {
            const id = section.recording
            ShortcutMap.assign(id, verdict.sequence)
            section.stopRecording()
            AppController.toast(qsTr("“%1” now has %2").arg(ShortcutMap.title(id)).arg(verdict.sequence), "success")
            return
        }
        section.verdict = verdict
    }
    /// The conflict shown: the shortcut moves here, the other command is left without one.
    function takeOver() {
        const id = section.recording
        const verdict = section.verdict
        if (verdict.status !== "conflict" || !ShortcutMap.assign(id, verdict.sequence, true))
            return
        section.stopRecording()
        AppController.toast(qsTr("“%1” now has %2; “%3” has no shortcut").arg(ShortcutMap.title(id))
                            .arg(verdict.sequence).arg(verdict.conflictTitle), "success")
    }
    function reset(id) {
        const loser = ShortcutMap.reset(id)
        if (loser !== "")
            AppController.toast(qsTr("“%1” has no shortcut now").arg(ShortcutMap.title(loser)), "info")
    }

    Layout.fillWidth: true
    spacing: Theme.space2

    BText {
        Layout.fillWidth: true
        text: qsTr("Keyboard shortcuts")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("The shortcuts of the commands in the command palette, kept on this device. The keys of writing and editing (Enter, Tab, Ctrl+C, Ctrl+Z…) stay as they are.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        spacing: Theme.space2
        BSearchField {
            objectName: "shortcutSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Search commands and keys…")
            onSearch: (query) => section.query = query
        }
        BButton {
            objectName: "shortcutsResetAll"
            text: qsTr("Restore all")
            iconName: "rotate-ccw"
            size: "small"
            variant: "ghost"
            enabled: ShortcutMap.customCount > 0
            onClicked: resetAllDialog.open()
        }
    }

    // While recording, every key goes here first — the window's shortcuts do not fire.
    Item {
        id: recorder
        objectName: "shortcutRecorder"
        width: 0
        height: 0
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Press the new shortcut")
        Keys.onShortcutOverride: (event) => event.accepted = section.recording !== ""
        Keys.onPressed: (event) => {
            if (section.recording === "")
                return
            event.accepted = true
            if ((event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) || event.key === Qt.Key_Back) {
                section.stopRecording()
                return
            }
            const sequence = ShortcutMap.sequenceForKey(event.key, event.modifiers)
            if (sequence !== "") // (a modifier alone: the key comes next)
                section.record(sequence)
        }
        // The keyboard went elsewhere (the search field, another control): no longer recording.
        // (The rows' Zmień / Anuluj / Przypisz tutaj never take it.)
        onActiveFocusChanged: if (!activeFocus && section.recording !== "") section.stopRecording()
    }

    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: rows.implicitWidth
        contentHeight: rows.implicitHeight

        ColumnLayout {
            id: rows
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space4 : Theme.space5

            BText {
                Layout.fillWidth: true
                visible: list.count === 0
                text: qsTr("No command matches “%1”.").arg(section.query)
                variant: "muted"
                wrapMode: Text.Wrap
            }

            Repeater {
                id: list
                // (Read again whenever a shortcut changes or the language does.)
                model: ShortcutMap.sequences && ShortcutMap.customCount >= 0 ? ShortcutMap.entries(section.query) : []
                delegate: ColumnLayout {
                    id: entry
                    required property var modelData
                    readonly property bool recordingThis: section.recording === modelData.id
                    objectName: "shortcutRow_" + modelData.id
                    Layout.fillWidth: true
                    spacing: Theme.space2

                    ShellSettingRow {
                        Layout.fillWidth: true
                        title: entry.modelData.title
                        description: entry.recordingThis ? qsTr("Press the new shortcut… Esc cancels.")
                                     : entry.modelData.custom ? (entry.modelData.defaultSequence !== ""
                                                                 ? qsTr("Default: %1").arg(entry.modelData.defaultSequence)
                                                                 : qsTr("Default: none"))
                                     : ""
                        BKbd {
                            visible: entry.modelData.sequence !== "" && !entry.recordingThis
                            Layout.alignment: Qt.AlignVCenter
                            sequence: entry.modelData.sequence
                            alwaysVisible: true
                        }
                        BText {
                            visible: entry.modelData.sequence === "" && !entry.recordingThis
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("No shortcut")
                            variant: "muted"
                        }
                        BButton {
                            objectName: "shortcutChange_" + entry.modelData.id
                            text: entry.recordingThis ? qsTr("Cancel") : qsTr("Change")
                            size: "small"
                            variant: entry.recordingThis ? "secondary" : "ghost"
                            focusPolicy: Qt.NoFocus
                            onClicked: entry.recordingThis ? section.stopRecording() : section.startRecording(entry.modelData.id)
                        }
                        BIconButton {
                            objectName: "shortcutClear_" + entry.modelData.id
                            visible: entry.modelData.sequence !== "" && !entry.recordingThis
                            iconName: "x"
                            size: "small"
                            label: qsTr("No shortcut for “%1”").arg(entry.modelData.title)
                            onClicked: ShortcutMap.clear(entry.modelData.id)
                        }
                        BIconButton {
                            objectName: "shortcutReset_" + entry.modelData.id
                            visible: entry.modelData.custom && !entry.recordingThis
                            iconName: "rotate-ccw"
                            size: "small"
                            label: entry.modelData.defaultSequence !== ""
                                   ? qsTr("Restore the default, %1").arg(entry.modelData.defaultSequence)
                                   : qsTr("Restore the default (no shortcut)")
                            onClicked: section.reset(entry.modelData.id)
                        }
                    }
                    // Why the key pressed cannot be this command's shortcut.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: entry.recordingThis && (section.verdict.message || "") !== ""
                        spacing: Theme.space2
                        BIcon {
                            Layout.alignment: Qt.AlignTop
                            name: "circle-alert"
                            color: section.verdict.status === "conflict" ? Theme.warningText : Theme.dangerText
                            size: 16
                        }
                        BText {
                            objectName: "shortcutMessage"
                            Layout.fillWidth: true
                            text: section.verdict.message || ""
                            wrapMode: Text.Wrap
                        }
                        BButton {
                            objectName: "shortcutTakeOver"
                            visible: section.verdict.status === "conflict"
                            text: qsTr("Assign here")
                            size: "small"
                            focusPolicy: Qt.NoFocus
                            onClicked: section.takeOver()
                        }
                    }
                }
            }
        }
    }

    BConfirmDialog {
        id: resetAllDialog
        objectName: "shortcutsResetAllConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Restore every default shortcut?")
        message: qsTr("%n command(s) have a shortcut of their own now.", "", ShortcutMap.customCount)
        confirmText: qsTr("Restore all")
        onAccepted: {
            section.stopRecording()
            ShortcutMap.resetAll()
        }
    }
}
