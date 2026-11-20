pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// MediaSpeechSettings — Settings → Extensions → Speech (SettingsView, after AI; docs/UI.md "Voice
// notes"): speech recognition on this device (AppController.speech): which Whisper model (Small,
// Base, Large Turbo — their sizes and what they are good at), its file (Download / Stop / Remove,
// with the progress), the language it listens for (Automatic, Polish, English), whether a voice
// note is transcribed by itself, and whose model it is (OpenAI's, MIT). Hidden in builds without the
// recognizer (`available`). A card like SettingsView's groups (its heading, note, hairlines).
ColumnLayout {
    id: group

    objectName: "speechSettings"
    visible: AppController.speech.available
    Layout.fillWidth: true
    spacing: Theme.space2

    readonly property list<var> languages: [
        { value: "auto", text: qsTr("Automatic") },
        { value: "pl", text: qsTr("Polish") },
        { value: "en", text: qsTr("English") }
    ]
    readonly property string modelId: AppController.speech.model

    BText {
        Layout.fillWidth: true
        text: qsTr("Speech")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("Voice notes and other recordings in your notes become text on this device: OpenAI's Whisper runs here, in Polish too, and no sound leaves the device. The text goes under the recording, as a block you can edit.")
        variant: "muted"
        wrapMode: Text.Wrap
    }
    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: cardColumn.implicitWidth
        contentHeight: cardColumn.implicitHeight
        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            ShellSettingRow {
                objectName: "speechModelRow"
                Layout.fillWidth: true
                title: qsTr("Model")
                description: group.modelId === "whisper-base"
                             ? qsTr("%1: fast on any device, but rough in Polish — names and numbers often come out wrong.").arg(AppController.speech.modelName)
                             : group.modelId === "whisper-large-v3-turbo"
                               ? qsTr("%1: the most accurate, also with names and numbers; slower, and on a phone a minute of speech takes a while.").arg(AppController.speech.modelName)
                               : qsTr("%1: Polish heard well enough to edit, a minute of speech in seconds on a computer.").arg(AppController.speech.modelName)
                BSegmented {
                    objectName: "speechModelSetting"
                    accessibleName: qsTr("Model")
                    model: AppController.speech.choices
                    currentValue: AppController.speech.model
                    onActivated: (value) => AppController.speech.model = "" + value
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space3
                ShellSettingRow {
                    objectName: "speechFilesRow"
                    Layout.fillWidth: true
                    title: qsTr("The model's file")
                    description: AppController.speech.modelHere
                                 ? qsTr("On this device (%1). Recordings are transcribed without the internet.").arg(AppController.speech.modelSize)
                                 : AppController.speech.checking
                                   ? qsTr("Checking the downloaded file…")
                                   : AppController.speech.downloading
                                     ? qsTr("Downloading: %1. You can leave this page; a download that stops goes on from where it was.").arg(AppController.speech.progressText)
                                     : AppController.speech.downloadError !== ""
                                       ? qsTr("The download stopped (%1). Download again: it goes on from where it was.").arg(AppController.speech.downloadError)
                                       : qsTr("Not on this device yet: %1 to download from Hugging Face, best over Wi-Fi.").arg(AppController.speech.modelSize)
                    BButton {
                        objectName: "speechDownload"
                        visible: !AppController.speech.modelHere && !AppController.speech.downloading
                        enabled: !AppController.speech.downloadingOther
                        size: "small"
                        variant: "primary"
                        iconName: "download"
                        text: qsTr("Download")
                        onClicked: AppController.speech.download()
                    }
                    BButton {
                        objectName: "speechCancel"
                        visible: AppController.speech.downloading
                        size: "small"
                        text: qsTr("Stop")
                        onClicked: AppController.speech.cancelDownload()
                    }
                    BButton {
                        objectName: "speechRemove"
                        visible: AppController.speech.modelHere
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Remove")
                        onClicked: AppController.speech.remove()
                    }
                }
                BProgressBar {
                    objectName: "speechProgress"
                    Layout.fillWidth: true
                    visible: AppController.speech.downloading
                    indeterminate: AppController.speech.checking
                    value: AppController.speech.progress
                }
            }
            ShellSettingRow {
                objectName: "speechLanguageRow"
                Layout.fillWidth: true
                title: qsTr("Language")
                description: AppController.speech.language === "auto"
                             ? qsTr("Recognized from the first half minute of each recording.")
                             : qsTr("Every recording is heard as this language: better for short notes.")
                BSegmented {
                    objectName: "speechLanguageSetting"
                    accessibleName: qsTr("Language")
                    model: group.languages
                    currentValue: AppController.speech.language
                    onActivated: (value) => AppController.speech.language = "" + value
                }
            }
            BSwitch {
                objectName: "speechAutomaticSetting"
                Layout.fillWidth: true
                text: qsTr("Transcribe voice notes")
                description: qsTr("A voice note recorded in Brulion is transcribed by itself when a model is on this device. Any recording can be transcribed from its menu (More → Transcribe).")
                iconName: "captions"
                checked: AppController.speech.automatic
                onToggled: AppController.speech.automatic = checked
            }
            ShellSettingRow {
                objectName: "speechLicense"
                Layout.fillWidth: true
                title: qsTr("Whose model")
                description: qsTr("OpenAI made Whisper and gives it under the %1 licence; whisper.cpp's authors converted it for this device. Brulion only downloads it when you ask.").arg(AppController.speech.modelLicense)
                BButton {
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Licence")
                    onClicked: Workspace.openUrl(AppController.speech.modelLicenseUrl)
                }
                BButton {
                    size: "small"
                    variant: "ghost"
                    text: qsTr("About the model")
                    onClicked: Workspace.openUrl(AppController.speech.modelPageUrl)
                }
            }
        }
        ShellSettingDividers { column: cardColumn }
    }
}
