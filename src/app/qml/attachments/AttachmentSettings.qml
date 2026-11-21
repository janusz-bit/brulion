pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// AttachmentSettings — the "Search" section of SettingsView (docs/UI.md "Searching attachments";
// C++: Workspace.attachments, src/app/attachments): whether the whole-notebook search reads PDFs and
// pictures (AppSettings.attachmentSearch, on by default), text recognition — OCR — of pictures and
// scanned pages (AppSettings.attachmentOcr, off by default: a page costs a core about a second;
// where this device has no Tesseract it says why and what to install), OCR's languages ("auto":
// the UI language and English, or the models installed, one switch each), what the notebook's
// attachments hold now and what is being read (with Pause / Resume), and the device's cache with
// "Clear" (read again from the files).
ColumnLayout {
    id: section

    readonly property AttachmentIndexer attachments: Workspace.attachments
    readonly property bool working: section.attachments.state === "reading" || section.attachments.state === "recognizing"
    /// OCR's languages chosen by hand (Tesseract's codes): [] for "auto".
    readonly property list<string> chosenLanguages: AppSettings.attachmentOcrLanguages === "auto"
                                                    ? [] : Js.splitRegExp(AppSettings.attachmentOcrLanguages, "[+,\\s]+", "")

    /// Tesseract's codes of "pol+eng".
    function codesOf(languages: string): list<string> {
        return Js.nonEmptyStrings(Js.split(languages, "+"))
    }
    /// `codes` with `code` in (`on`) or out.
    function withLanguage(codes: list<string>, code: string, on: bool): list<string> {
        const without = Js.withoutString(codes, code)
        return on ? Js.appendedString(without, code) : without
    }
    function chosen(code: string): bool {
        return Js.includes("+" + Js.joinNonEmpty(section.chosenLanguages, "+") + "+", "+" + code + "+")
    }
    /// `code` added to or taken from the languages chosen by hand (the first change from "auto"
    /// starts from the languages it used).
    function toggleLanguage(code: string, on: bool): void {
        const from = section.chosenLanguages.length > 0 ? Js.joinNonEmpty(section.chosenLanguages, "+")
                                                         : section.attachments.ocrLanguages
        const next = Js.joinNonEmpty(section.withLanguage(section.codesOf(from), code, on), "+")
        AppSettings.attachmentOcrLanguages = next === "" ? "auto" : next
    }
    /// Why OCR is not available here, in words.
    readonly property string unavailableText: {
        switch (section.attachments.ocrUnavailableReason) {
        case "unsupported":
            return qsTr("Text recognition is not available in this version of Brulion (Android, the browser). PDFs are still searched by their own text.")
        case "noLibrary":
            return DeviceInfo.platform === "windows"
                   ? qsTr("Text recognition needs Tesseract OCR installed on this computer (the installer of UB Mannheim, with Polish and English). Brulion finds it in Program Files and uses it after a restart.")
                   : qsTr("Text recognition needs Tesseract OCR on this computer: install the packages tesseract-ocr, tesseract-ocr-pol and tesseract-ocr-eng (or your system's equivalent) and restart Brulion.")
        case "noLanguages":
            return qsTr("Tesseract is installed, but without any language: add its language files (*.traineddata) to %1 or install your system's tesseract language packages.")
                   .arg(section.attachments.ocrUserDataDir)
        case "probing":
            return qsTr("Looking for text recognition on this device…")
        default:
            return ""
        }
    }
    /// What is being read now, in words ("" when idle).
    readonly property string statusText: {
        switch (section.attachments.state) {
        case "reading":
            return qsTr("Reading attachments… %1 of %2").arg(section.attachments.filesDone).arg(section.attachments.filesTotal)
        case "recognizing":
            return qsTr("Recognizing text: page %1 of %2 in “%3” (%4 of %5 pages waiting for OCR done)")
                   .arg(section.attachments.currentPage).arg(section.attachments.currentPages)
                   .arg(section.baseName(section.attachments.currentFile))
                   .arg(section.attachments.pagesRecognized).arg(section.attachments.pagesToRecognize)
        case "waiting":
            return qsTr("Waiting for the sync to end.")
        case "paused":
            return qsTr("Paused.")
        case "unavailable":
            return qsTr("This version of Brulion reads no PDFs and recognizes no text.")
        default:
            return ""
        }
    }
    function baseName(path: string): string {
        const i = Js.lastIndexOf(path, "/")
        return i < 0 ? path : Js.sliceFrom(path, i + 1)
    }

    Layout.fillWidth: true
    spacing: Theme.space2

    BText {
        Layout.fillWidth: true
        text: qsTr("Search")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        objectName: "attachmentSearchNote"
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("Search also finds words in the notebook's PDFs and pictures. Their text is read offline and kept on this device.")
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
                objectName: "attachmentSearchSwitch"
                Layout.fillWidth: true
                text: qsTr("Search in attachments")
                description: section.attachments.pdfTextAvailable
                             ? qsTr("The text of PDFs, page by page; a result opens the PDF at its page with the words highlighted.")
                             : qsTr("PDFs are not read in this version of Brulion; pictures are found by their names (and with text recognition by their words).")
                checked: AppSettings.attachmentSearch
                onToggled: AppSettings.attachmentSearch = checked
            }
            BSwitch {
                objectName: "attachmentOcrSwitch"
                Layout.fillWidth: true
                visible: AppSettings.attachmentSearch
                enabled: section.attachments.ocrAvailable
                text: qsTr("Recognize text in pictures and scans (OCR)")
                description: section.attachments.ocrAvailable
                             ? qsTr("Photos, screenshots and scanned pages become searchable. Read in the background, at low priority.")
                             : section.unavailableText
                checked: AppSettings.attachmentOcr && section.attachments.ocrAvailable
                onToggled: AppSettings.attachmentOcr = checked
            }
            ShellSettingRow {
                objectName: "attachmentOcrLanguagesRow"
                visible: AppSettings.attachmentSearch && AppSettings.attachmentOcr && section.attachments.ocrAvailable
                title: qsTr("Recognition languages")
                description: section.attachments.ocrLanguages === ""
                             ? qsTr("None of the chosen languages is installed.")
                             : (AppSettings.attachmentOcrLanguages === "auto"
                                ? qsTr("Automatic — now %1 (the language of Brulion and English).")
                                : qsTr("Now %1. Changing them reads the recognized pages again."))
                               .arg(section.attachments.languageNames(section.attachments.ocrLanguages))
                BButton {
                    objectName: "attachmentOcrAuto"
                    visible: AppSettings.attachmentOcrLanguages !== "auto"
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Automatic")
                    onClicked: AppSettings.attachmentOcrLanguages = "auto"
                }
            }
            Flow {
                objectName: "attachmentOcrLanguages"
                visible: AppSettings.attachmentSearch && AppSettings.attachmentOcr && section.attachments.ocrAvailable
                         && section.attachments.ocrInstalledLanguages.length > 1
                Layout.fillWidth: true
                spacing: Theme.space2
                Repeater {
                    model: section.attachments.ocrInstalledLanguages
                    delegate: BChip {
                        id: languageChip
                        required property string modelData
                        text: section.attachments.languageNames(modelData)
                        showIcon: false
                        interactive: true
                        selected: AppSettings.attachmentOcrLanguages === "auto"
                                  ? Js.includes("+" + section.attachments.ocrLanguages + "+", "+" + modelData + "+")
                                  : section.chosen(modelData)
                        Accessible.checkable: true
                        Accessible.checked: languageChip.selected
                        onClicked: section.toggleLanguage(languageChip.modelData, !languageChip.selected)
                    }
                }
            }
            ShellSettingRow {
                objectName: "attachmentStatusRow"
                visible: AppSettings.attachmentSearch && Workspace.isOpen
                title: qsTr("In this notebook")
                description: (section.attachments.indexedFiles === 0
                              ? qsTr("No PDFs or pictures read yet.")
                              : qsTr("%n file(s) read", "", section.attachments.indexedFiles) + ", "
                                + qsTr("%n page(s) with text", "", section.attachments.indexedPages) + ".")
                             + (section.statusText !== "" ? " " + section.statusText : "")
                BSpinner {
                    visible: section.working
                    running: visible
                    size: Theme.iconSizeSm
                    Layout.alignment: Qt.AlignVCenter
                }
                BButton {
                    objectName: "attachmentPauseButton"
                    visible: section.working || section.attachments.paused
                    size: "small"
                    variant: "secondary"
                    iconName: section.attachments.paused ? "play" : "pause"
                    text: section.attachments.paused ? qsTr("Resume") : qsTr("Pause")
                    onClicked: section.attachments.paused = !section.attachments.paused
                }
            }
            ShellSettingRow {
                objectName: "attachmentCacheRow"
                visible: AppSettings.attachmentSearch && Workspace.isOpen
                title: qsTr("Text kept on this device")
                description: qsTr("%1 for all notebooks. Clearing it reads the files again.")
                             .arg(section.attachments.cacheSizeText)
                BButton {
                    objectName: "attachmentClearCache"
                    size: "small"
                    variant: "ghost"
                    iconName: "trash-2"
                    text: qsTr("Clear")
                    onClicked: clearConfirm.open()
                }
            }
        }
        ShellSettingDividers { column: rows }
    }

    BConfirmDialog {
        id: clearConfirm
        title: qsTr("Clear the text kept for search?")
        message: AppSettings.attachmentOcr
              ? qsTr("The PDFs and pictures of every notebook on this device are read again, and their text recognized again — for many scans that takes long.")
              : qsTr("The PDFs and pictures of every notebook on this device are read again.")
        confirmText: qsTr("Clear")
        destructive: true
        onAccepted: section.attachments.clearCache()
    }
}
