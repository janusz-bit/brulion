pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellSemanticSettings — Settings → Search → Search by meaning (SettingsView's "search" section,
// after the attachments; docs/UI.md "Search by meaning"; C++: Workspace.semantic, semanticsearch.h):
// the switch (off by default; turning it on downloads the model when it is not here), the model's
// file (Download / Stop / Remove with the progress — Remove also deletes every notebook's vectors on
// this device, after a confirmation), how far the open notebook's notes are read by meaning, and
// whose model it is (Multilingual E5 Small, Microsoft, MIT). Hidden in builds that cannot search by
// meaning (`available`: the browser, a build without the model runner), and while the model is not
// published for download (`downloadable`) unless its file is on this device already. A card like SettingsView's
// groups (its heading, note, hairlines).
ColumnLayout {
    id: section

    objectName: "semanticSettings"
    readonly property SemanticSearch semantic: Workspace.semantic
    // (Until the model is published: only where its file is here already.)
    visible: section.semantic.available && (section.semantic.downloadable || section.semantic.modelHere)
    Layout.fillWidth: true
    spacing: Theme.space2

    /// How far the open notebook is, in words.
    readonly property string statusText: {
        switch (section.semantic.state) {
        case "indexing":
            return qsTr("Reading notes by meaning: %1 of %2. Until then the others are found by their words.")
                   .arg(section.semantic.notesDone).arg(section.semantic.notesTotal)
        case "loading":
            return qsTr("Getting ready…")
        case "ready":
            return qsTr("%n note(s) can be found by meaning.", "", section.semantic.notesTotal)
        case "failed":
            return qsTr("Search by meaning stopped: %1. Turn it off and on again to retry.").arg(section.semantic.error)
        case "idle":
            return qsTr("No notebook is open.")
        default:
            return ""
        }
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Search by meaning")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        objectName: "semanticNote"
        text: qsTr("Search also finds notes by what they mean, not only by their words: “apple cake” finds “Grandma's szarlotka”, an English query finds a Polish note, “kupiłem opony” finds “kupić oponę”. A small model reads your notes on this device; nothing leaves it.")
        variant: "muted"
        wrapMode: Text.Wrap
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
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            BSwitch {
                objectName: "semanticSwitch"
                Layout.fillWidth: true
                text: qsTr("Search by meaning")
                description: section.semantic.modelHere
                             ? qsTr("Results by meaning join the results by words in Search and in “Search the notes” of the quick switcher.")
                             : qsTr("Turning it on downloads the model (%1) once.").arg(section.semantic.modelSize)
                iconName: "sparkles"
                checked: section.semantic.enabled
                onToggled: section.semantic.enabled = checked
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space3
                ShellSettingRow {
                    objectName: "semanticFileRow"
                    Layout.fillWidth: true
                    title: qsTr("The model's file")
                    description: section.semantic.modelHere
                                 ? qsTr("On this device (%1). Notes and queries are read without the internet.").arg(section.semantic.modelSize)
                                 : section.semantic.checking
                                   ? qsTr("Checking the downloaded file…")
                                   : section.semantic.downloading
                                     ? qsTr("Downloading: %1. You can leave this page; a download that stops goes on from where it was.").arg(section.semantic.progressText)
                                     : section.semantic.downloadError !== ""
                                       ? qsTr("The download stopped (%1). Download again: it goes on from where it was.").arg(section.semantic.downloadError)
                                       : qsTr("Not on this device yet: %1 to download from Hugging Face, best over Wi-Fi.").arg(section.semantic.modelSize)
                    BButton {
                        objectName: "semanticDownload"
                        visible: !section.semantic.modelHere && !section.semantic.downloading
                        size: "small"
                        variant: "primary"
                        iconName: "download"
                        text: qsTr("Download")
                        onClicked: section.semantic.download()
                    }
                    BButton {
                        objectName: "semanticCancel"
                        visible: section.semantic.downloading
                        size: "small"
                        text: qsTr("Stop")
                        onClicked: section.semantic.cancelDownload()
                    }
                    BButton {
                        objectName: "semanticRemove"
                        visible: section.semantic.modelHere
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Remove")
                        onClicked: removeConfirm.open()
                    }
                }
                BProgressBar {
                    objectName: "semanticProgress"
                    Layout.fillWidth: true
                    visible: section.semantic.downloading
                    indeterminate: section.semantic.checking
                    value: section.semantic.progress
                }
            }
            ShellSettingRow {
                objectName: "semanticStatusRow"
                visible: section.semantic.enabled && section.semantic.modelHere && section.statusText !== ""
                Layout.fillWidth: true
                title: qsTr("In this notebook")
                description: section.statusText
                BSpinner {
                    visible: section.semantic.state === "indexing" || section.semantic.state === "loading"
                    running: visible
                    size: Theme.iconSizeSm
                    Layout.alignment: Qt.AlignVCenter
                }
            }
            ShellSettingRow {
                objectName: "semanticLicense"
                Layout.fillWidth: true
                title: qsTr("Whose model")
                description: qsTr("%1 by %2 (licence: %3), prepared for llama.cpp by Brulion. Downloaded only when you ask.")
                             .arg(section.semantic.modelName).arg(section.semantic.modelMaker).arg(section.semantic.modelLicense)
                BButton {
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Licence")
                    onClicked: Workspace.openUrl(section.semantic.modelLicenseUrl)
                }
                BButton {
                    size: "small"
                    variant: "ghost"
                    text: qsTr("About the model")
                    onClicked: Workspace.openUrl(section.semantic.modelPageUrl)
                }
            }
        }
        ShellSettingDividers { column: rows }
    }

    BConfirmDialog {
        id: removeConfirm
        title: qsTr("Remove the model?")
        message: qsTr("Search finds notes by their words again. What was read of every notebook on this device by meaning is deleted too; downloading the model again reads the notes anew.")
        confirmText: qsTr("Remove")
        destructive: true
        onAccepted: section.semantic.remove()
    }
}
