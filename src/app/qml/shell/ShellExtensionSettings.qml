pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellExtensionSettings — an extension's own settings (b.settings, docs/EXTENSIONS.md "Settings"):
// the fields it declared — numbers with − / + and typing ("82,5"), texts, switches, one of a few
// choices — with their values on this device; each change is kept at once
// (Workspace.extensions.setSetting) and the extension reads it with b.setting. "Defaults" puts
// every one back. Settings → Extensions and an extension page's state line open it
// (AppShell.openExtensionSettings(extension)).
BDialog {
    id: dialog
    objectName: "extensionSettings"

    property string extension
    readonly property list<var> noRows: []
    readonly property list<var> rows: Workspace.extensions.revision >= 0 && dialog.extension !== ""
                                      ? Workspace.extensions.settingsOf(dialog.extension) : dialog.noRows

    title: qsTr("Settings of “%1”").arg(Workspace.extensions.title(dialog.extension))
    subtitle: qsTr("Kept on this device")
    iconName: "settings"
    acceptText: qsTr("Done")
    showReject: false

    function show(extensionId: string): void {
        dialog.extension = extensionId
        dialog.open()
    }

    footerLeading: BButton {
        objectName: "extensionSettingsReset"
        variant: "ghost"
        text: qsTr("Defaults")
        onClicked: Workspace.extensions.resetSettings(dialog.extension)
    }

    BEmptyState {
        Layout.fillWidth: true
        visible: dialog.rows.length === 0
        compact: true
        iconName: "settings"
        title: qsTr("No settings")
        description: qsTr("This extension declares none (b.settings), or it does not run now.")
    }

    Repeater {
        model: dialog.rows
        delegate: ColumnLayout {
            id: field
            required property string key
            required property string label
            required property string kind
            required property string description
            required property real step
            required property string unit
            required property list<string> options
            required property string placeholder
            required property string currentText
            required property real currentNumber
            required property bool currentOn
            required property string display
            objectName: "extensionSetting"
            Layout.fillWidth: true
            spacing: Theme.space1

            BSwitch {
                objectName: "extensionSettingToggle"
                Layout.fillWidth: true
                visible: field.kind === "toggle"
                text: field.label
                description: field.description
                checked: field.currentOn
                onToggled: Workspace.extensions.setSetting(dialog.extension, field.key, checked)
            }
            BText {
                Layout.fillWidth: true
                visible: field.kind !== "toggle"
                text: field.label
                variant: "label"
                wrapMode: Text.Wrap
            }
            BText {
                Layout.fillWidth: true
                visible: field.kind !== "toggle" && field.description !== ""
                text: field.description
                variant: "small"
                color: Theme.fgMuted
                wrapMode: Text.Wrap
            }
            RowLayout {
                visible: field.kind === "number"
                spacing: Theme.space2
                BIconButton {
                    label: qsTr("Less")
                    iconName: "minus"
                    onClicked: Workspace.extensions.setSetting(dialog.extension, field.key,
                                                               Math.max(0, field.currentNumber - field.step))
                }
                BTextField {
                    objectName: "extensionSettingNumber"
                    Layout.preferredWidth: 120
                    text: field.display
                    horizontalAlignment: TextInput.AlignHCenter
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    accessibleName: field.label
                    onEditingFinished: Workspace.extensions.setSetting(dialog.extension, field.key, text)
                }
                BText {
                    text: field.unit
                    variant: "muted"
                }
                BIconButton {
                    label: qsTr("More")
                    iconName: "plus"
                    onClicked: Workspace.extensions.setSetting(dialog.extension, field.key, field.currentNumber + field.step)
                }
            }
            BTextField {
                objectName: "extensionSettingText"
                Layout.fillWidth: true
                visible: field.kind === "text"
                text: field.currentText
                placeholderText: field.placeholder
                accessibleName: field.label
                onEditingFinished: Workspace.extensions.setSetting(dialog.extension, field.key, text)
            }
            Flow {
                Layout.fillWidth: true
                visible: field.kind === "choice"
                spacing: Theme.space2
                Repeater {
                    model: field.options
                    delegate: BChip {
                        required property string modelData
                        objectName: "extensionSettingChoice"
                        text: modelData
                        interactive: true
                        selected: modelData === field.currentText
                        onClicked: Workspace.extensions.setSetting(dialog.extension, field.key, modelData)
                    }
                }
            }
        }
    }
}
