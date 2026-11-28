pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeScriptsEditor — a project's scripts (T3 Code's actions) in CodeProjectDialog (docs/UI.md
// "Code mode"): a row each — its icon (T3 Code's six: play, test, lint, configure, build, debug), its
// name and command, a keyboard shortcut of Code mode (recorded: the next key pressed; checked against
// ShortcutMap's commands), "Run when a worktree is made" and "Wait for it before the agent starts";
// moved up or down, deleted. The project's t3.json scripts come after this device's, marked "from
// t3.json": changed, a row becomes the device's own of that name, which overrides the file's
// (deleting it brings the file's back); the file is never written. CodeScriptsModel holds the rows;
// the dialog's Save keeps them (save()).
ColumnLayout {
    id: editor

    property string project
    readonly property CodeScriptsModel model: scripts
    /// The row whose shortcut is being recorded (-1 none) and why the last one was refused.
    property int recording: -1
    property string shortcutError

    readonly property list<var> icons: [
        { value: "play", text: qsTr("Run") },
        { value: "test", text: qsTr("Test") },
        { value: "lint", text: qsTr("Lint") },
        { value: "configure", text: qsTr("Configure") },
        { value: "build", text: qsTr("Build") },
        { value: "debug", text: qsTr("Debug") }
    ]

    function reload(): void {
        editor.recording = -1
        editor.shortcutError = ""
        scripts.reload()
    }
    function record(row: int, sequence: string, scriptId: string): void {
        verdict.map = ShortcutMap.checkIn("code", sequence, "code.script." + editor.project + "." + scriptId)
        const status = AppController.routeText(verdict.map, "status")
        if (status !== "ok" && status !== "same") {
            editor.shortcutError = AppController.routeText(verdict.map, "message")
            return
        }
        editor.shortcutError = ""
        const normalized = AppController.routeText(verdict.map, "sequence")
        scripts.setText(row, "shortcut", normalized)
        editor.recording = -1
    }

    spacing: Theme.space2

    MapHolder {
        id: verdict
    }
    CodeScriptsModel {
        id: scripts
        projects: Workspace.projects
        project: editor.project
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Buttons over the thread that run a command in a terminal of the thread's folder — also from the palette (“Run: …”) and with their shortcuts in Code mode. A t3.json in the project's folder adds its own.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    Repeater {
        model: scripts
        delegate: Rectangle {
            id: row
            required property int index
            required property string name
            required property string command
            required property string scriptIcon
            required property string iconName
            required property string shortcut
            required property bool runOnWorktreeCreate
            required property bool wait
            required property bool stopOnSettle
            required property bool runOnSettle
            required property string previewUrl
            required property bool autoOpenPreview
            required property string source
            required property bool overrides
            required property string scriptId
            readonly property bool fromFile: row.source === "t3json"

            objectName: "codeScriptRow_" + row.index
            Layout.fillWidth: true
            implicitHeight: rowColumn.implicitHeight + 2 * Theme.space2
            radius: Theme.radius
            color: Theme.bgSunken
            border.color: Theme.borderSubtle

            ColumnLayout {
                id: rowColumn
                anchors.fill: parent
                anchors.margins: Theme.space2
                spacing: Theme.space1

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space1
                    BIconButton {
                        id: iconButton
                        objectName: "codeScriptIcon_" + row.index
                        size: "small"
                        iconName: row.iconName
                        label: qsTr("Icon")
                        onClicked: iconMenu.popup(iconButton, 0, iconButton.height)
                        BMenu {
                            id: iconMenu
                            Instantiator {
                                model: editor.icons
                                delegate: BMenuItem {
                                    id: iconItem
                                    required property var modelData
                                    readonly property string value: AppController.routeText(iconItem.modelData, "value")
                                    text: AppController.routeText(iconItem.modelData, "text")
                                    iconName: Workspace.projects.scriptIcon(iconItem.value)
                                    onTriggered: scripts.setText(row.index, "icon", iconItem.value)
                                }
                                onObjectAdded: (index, object) => iconMenu.insertItem(index, object as Item)
                                onObjectRemoved: (index, object) => iconMenu.removeItem(object as Item)
                            }
                        }
                    }
                    BTextField {
                        objectName: "codeScriptName_" + row.index
                        Layout.preferredWidth: 140
                        size: "small"
                        text: row.name
                        placeholderText: qsTr("Name")
                        accessibleName: qsTr("The script's name")
                        errorText: scripts.revision >= 0 && scripts.nameTaken(row.index) ? qsTr("Another script has this name") : ""
                        onEditingFinished: scripts.setText(row.index, "name", text)
                    }
                    BTextField {
                        objectName: "codeScriptCommand_" + row.index
                        Layout.fillWidth: true
                        size: "small"
                        text: row.command
                        placeholderText: "npm test"
                        font.family: Theme.fontMono
                        accessibleName: qsTr("The script's command")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        onEditingFinished: scripts.setText(row.index, "command", text)
                    }
                    BIconButton {
                        objectName: "codeScriptUp_" + row.index
                        size: "small"
                        iconName: "arrow-up"
                        label: qsTr("Move up")
                        enabled: scripts.revision >= 0 && scripts.canMove(row.index, -1)
                        onClicked: scripts.moveRow(row.index, -1)
                    }
                    BIconButton {
                        objectName: "codeScriptDown_" + row.index
                        size: "small"
                        iconName: "arrow-down"
                        label: qsTr("Move down")
                        enabled: scripts.revision >= 0 && scripts.canMove(row.index, 1)
                        onClicked: scripts.moveRow(row.index, 1)
                    }
                    BIconButton {
                        objectName: "codeScriptDelete_" + row.index
                        size: "small"
                        iconName: "trash-2"
                        label: row.overrides ? qsTr("Delete: the one of t3.json again") : qsTr("Delete")
                        unavailable: row.fromFile ? qsTr("It is in the project's t3.json: change it there") : ""
                        onClicked: {
                            if (!row.fromFile)
                                scripts.remove(row.index)
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BButton {
                        id: shortcutButton
                        objectName: "codeScriptShortcut_" + row.index
                        size: "small"
                        variant: editor.recording === row.index ? "primary" : "ghost"
                        iconName: "keyboard"
                        text: editor.recording === row.index ? qsTr("Press a key…")
                              : row.shortcut !== "" ? row.shortcut : qsTr("Shortcut")
                        Accessible.name: qsTr("Keyboard shortcut: %1").arg(row.shortcut !== "" ? row.shortcut : qsTr("none"))
                        onClicked: {
                            editor.shortcutError = ""
                            editor.recording = editor.recording === row.index ? -1 : row.index
                            if (editor.recording === row.index)
                                shortcutButton.forceActiveFocus()
                        }
                        onActiveFocusChanged: {
                            if (!shortcutButton.activeFocus && editor.recording === row.index)
                                editor.recording = -1
                        }
                        Keys.onPressed: (event) => {
                            if (editor.recording !== row.index)
                                return
                            const key = event.key
                            const modifiers = event.modifiers
                            event.accepted = true
                            if (key === Qt.Key_Escape && modifiers === Qt.NoModifier) {
                                editor.recording = -1
                                return
                            }
                            if ((key === Qt.Key_Backspace || key === Qt.Key_Delete) && modifiers === Qt.NoModifier) {
                                scripts.setText(row.index, "shortcut", "")
                                editor.recording = -1
                                return
                            }
                            const sequence = ShortcutMap.sequenceForKey(key, modifiers)
                            if (sequence !== "")
                                editor.record(row.index, sequence, row.scriptId)
                        }
                    }
                    BSwitch {
                        objectName: "codeScriptOnWorktree_" + row.index
                        text: qsTr("Run when a worktree is made")
                        checked: row.runOnWorktreeCreate
                        onToggled: scripts.setFlag(row.index, "runOnWorktreeCreate", checked)
                    }
                    BSwitch {
                        objectName: "codeScriptWait_" + row.index
                        visible: row.runOnWorktreeCreate
                        text: qsTr("Wait for it before the agent starts")
                        checked: row.wait
                        onToggled: scripts.setFlag(row.index, "wait", checked)
                    }
                    BChip {
                        visible: row.fromFile || row.overrides
                        size: "small"
                        variant: row.fromFile ? "scheduled" : "due"
                        iconName: "file-text"
                        text: row.fromFile ? qsTr("from t3.json") : qsTr("overrides t3.json")
                    }
                }
                BText {
                    visible: editor.recording === row.index
                    Layout.fillWidth: true
                    text: editor.shortcutError !== "" ? editor.shortcutError
                          : qsTr("Press the keys, Esc to cancel, Backspace for none.")
                    variant: "small"
                    color: editor.shortcutError !== "" ? Theme.dangerText : Theme.fgMuted
                    wrapMode: Text.Wrap
                }
                // T3 Code's settling options (docs/CODE.md "Settling and snoozing") and its preview.
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BSwitch {
                        objectName: "codeScriptStopOnSettle_" + row.index
                        text: qsTr("Stop it when the thread settles")
                        checked: row.stopOnSettle
                        onToggled: scripts.setFlag(row.index, "stopOnSettle", checked)
                    }
                    BSwitch {
                        objectName: "codeScriptRunOnSettle_" + row.index
                        text: qsTr("Run it in the worktree when the thread settles")
                        checked: row.runOnSettle
                        onToggled: scripts.setFlag(row.index, "runOnSettle", checked)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BTextField {
                        objectName: "codeScriptPreview_" + row.index
                        Layout.fillWidth: true
                        text: row.previewUrl
                        placeholderText: qsTr("Preview address, e.g. %1").arg("http://localhost:3000")
                        accessibleName: qsTr("Preview address")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                        onEditingFinished: scripts.setText(row.index, "previewUrl", text)
                    }
                    BSwitch {
                        objectName: "codeScriptAutoPreview_" + row.index
                        enabled: row.previewUrl !== ""
                        text: qsTr("Open it when the script runs")
                        checked: row.autoOpenPreview
                        onToggled: scripts.setFlag(row.index, "autoOpenPreview", checked)
                    }
                }
            }
        }
    }

    BButton {
        objectName: "codeScriptAdd"
        size: "small"
        variant: "secondary"
        iconName: "plus"
        text: qsTr("Add a script")
        enabled: scripts.count < 50
        onClicked: scripts.add()
    }
}
