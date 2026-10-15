pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// OnboardingView — first run and "Open another notebook": what Brulion is, how syncing works
// (Syncthing folder or Nextcloud), choosing or creating the notebook folder for this platform,
// the sample notebook ("Przykładowy brulion") and the recently used notebooks.
//
// Per platform: desktop — a path field with a folder picker (QtQuick.Dialogs FolderDialog,
// asynchronous); Android — a path on the phone's storage (default /storage/emulated/0/Brulion,
// needs "All files access", which AppController asks for) or the app's private storage; browser
// — one notebook kept in the browser's storage (IndexedDB).
Rectangle {
    id: root

    property var route: ({})

    readonly property var safe: SafeArea.margins
    readonly property bool pending: AppController.pendingVaultPath !== ""

    color: Theme.bg

    function openChosen() {
        AppController.openVault(AppController.isWasm ? AppController.defaultVaultPath : pathField.text)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: root.safe.top
        anchors.bottomMargin: root.safe.bottom
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * margin
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        readonly property int margin: Theme.compact ? Theme.space5 : Theme.space10

        ColumnLayout {
            id: column
            x: Math.round((flick.width - width) / 2)
            y: flick.margin
            width: Math.min(flick.width - 2 * flick.margin, 680)
            spacing: Theme.space6

            // ---- Welcome -----------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space3

                Image {
                    source: "qrc:/images/logo.svg"
                    sourceSize: Qt.size(Theme.compact ? 64 : 80, Theme.compact ? 64 : 80)
                    Layout.preferredWidth: Theme.compact ? 64 : 80
                    Layout.preferredHeight: Theme.compact ? 64 : 80
                    Accessible.ignored: true
                }
                BText {
                    Layout.fillWidth: true
                    text: qsTr("Welcome to Brulion")
                    variant: "display"
                    font.pixelSize: Theme.compact ? Theme.text2xl : Theme.text3xl
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                }
                BText {
                    Layout.fillWidth: true
                    text: qsTr("Your notes as plain Markdown files: an outliner with a daily journal, tasks, reminders and calculations. Everything works offline and stays yours.")
                    variant: "muted"
                    font.pixelSize: Theme.textMd
                    lineHeight: Theme.lineHeightPx(Theme.textMd)
                    wrapMode: Text.Wrap
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: Theme.compact ? 1 : 3
                columnSpacing: Theme.space4
                rowSpacing: Theme.space3

                Repeater {
                    model: [
                        { icon: "file-text", title: qsTr("Plain files"),
                          text: qsTr("Every page is a .md file in a folder you choose — readable by Obsidian, Logseq or any editor.") },
                        { icon: "bell", title: qsTr("Journal and reminders"),
                          text: qsTr("A page for every day, tasks with dates, reminders and alarms that ring on time.") },
                        { icon: "folder-sync", title: qsTr("Sync your way"),
                          text: qsTr("Share the folder with Syncthing, or connect Nextcloud. No account needed.") }
                    ]
                    delegate: RowLayout {
                        id: feature
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.space3

                        Rectangle {
                            Layout.alignment: Qt.AlignTop
                            implicitWidth: 36
                            implicitHeight: 36
                            radius: Theme.radiusLg
                            color: Theme.bgSunken
                            BIcon {
                                anchors.centerIn: parent
                                name: feature.modelData.icon
                                color: Theme.fgMuted
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            BText {
                                Layout.fillWidth: true
                                text: feature.modelData.title
                                variant: "label"
                                wrapMode: Text.Wrap
                            }
                            BText {
                                Layout.fillWidth: true
                                text: feature.modelData.text
                                variant: "muted"
                                font.pixelSize: Theme.textSm
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            // ---- Choose a notebook ---------------------------------------------------------
            BCard {
                Layout.fillWidth: true
                elevation: 1
                padding: Theme.compact ? Theme.space4 : Theme.space5

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space3

                    BText {
                        Layout.fillWidth: true
                        text: AppController.isWasm ? qsTr("Your notebook in this browser") : qsTr("Your notebook")
                        variant: "title"
                        Accessible.role: Accessible.Heading
                    }
                    BText {
                        Layout.fillWidth: true
                        text: AppController.isWasm
                              ? qsTr("Notes are kept in this browser's storage on this device and work offline. Move them with import and export, or sync them with Nextcloud.")
                              : AppController.isAndroid
                                ? qsTr("To sync with Syncthing, use a folder on the phone's storage — Brulion then asks for “All files access”. Without it, keep the notes in the app's own storage and sync them with Nextcloud.")
                                : qsTr("Choose a folder for your notes: a new one, or one you already sync with Syncthing. A missing folder is created.")
                        variant: "muted"
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        visible: !AppController.isWasm
                        columns: Theme.compact ? 1 : 2
                        columnSpacing: Theme.space2
                        rowSpacing: Theme.space2

                        BTextField {
                            id: pathField
                            Layout.fillWidth: true
                            text: AppSettings.currentVault !== "" ? AppSettings.currentVault : AppController.defaultVaultPath
                            leadingIcon: "folder"
                            accessibleName: qsTr("Notebook folder")
                            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                            onAccepted: root.openChosen()
                        }
                        BButton {
                            visible: AppController.isDesktop
                            text: qsTr("Browse…")
                            iconName: "folder-open"
                            onClicked: folderDialog.open()
                        }
                    }

                    // Android: waiting for "All files access".
                    BCard {
                        Layout.fillWidth: true
                        visible: root.pending
                        tone: "warning"
                        padding: Theme.space3

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Theme.space2
                            BText {
                                Layout.fillWidth: true
                                text: qsTr("Brulion needs “All files access” to keep notes in %1. Allow it in the system settings and come back — the notebook opens by itself.")
                                      .arg(AppController.pendingVaultPath)
                                wrapMode: Text.Wrap
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.space2
                                BButton {
                                    text: qsTr("Open system settings")
                                    iconName: "settings"
                                    variant: "primary"
                                    onClicked: AppController.requestPermission("allFilesAccess")
                                }
                                BButton {
                                    text: qsTr("Use app storage instead")
                                    variant: "ghost"
                                    onClicked: AppController.openVault(AppController.appStorageVaultPath)
                                }
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        BButton {
                            text: AppController.isWasm ? qsTr("Start with an empty notebook") : qsTr("Open notebook")
                            iconName: AppController.isWasm ? "square-pen" : "notebook-pen"
                            variant: "primary"
                            enabled: AppController.isWasm || pathField.text.trim() !== ""
                            onClicked: root.openChosen()
                        }
                        BButton {
                            visible: AppController.isAndroid && !root.pending
                            text: qsTr("Use app storage")
                            iconName: "smartphone"
                            onClicked: AppController.openVault(AppController.appStorageVaultPath)
                        }
                    }
                }
            }

            // ---- Sample notebook -----------------------------------------------------------
            BCard {
                id: sampleCard
                Layout.fillWidth: true
                clickable: true
                accessibleName: qsTr("Open the sample notebook")
                onClicked: AppController.openDemoVault()

                RowLayout {
                    anchors.fill: parent
                    spacing: Theme.space4

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 44
                        implicitHeight: 44
                        radius: 22
                        color: Theme.warningBg
                        BIcon {
                            anchors.centerIn: parent
                            name: "sparkles"
                            size: Theme.iconSizeLg
                            color: Theme.warningText
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        BText {
                            Layout.fillWidth: true
                            text: qsTr("Try the sample notebook")
                            variant: "label"
                            font.pixelSize: Theme.textMd
                            wrapMode: Text.Wrap
                        }
                        BText {
                            Layout.fillWidth: true
                            text: qsTr("“Przykładowy brulion” — a guided tour in Polish with live examples of every feature: links, tasks, reminders, calculations, tables and queries.")
                            variant: "muted"
                            font.pixelSize: Theme.textSm
                            wrapMode: Text.Wrap
                        }
                    }
                    BIcon {
                        name: "chevron-right"
                        color: Theme.fgFaint
                    }
                }
            }

            // ---- Recent notebooks ----------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                visible: AppSettings.recentVaults.length > 0
                spacing: 2

                BSectionHeader {
                    Layout.fillWidth: true
                    text: qsTr("Recent notebooks")
                    count: AppSettings.recentVaults.length
                }
                Repeater {
                    model: AppSettings.recentVaults
                    delegate: BListItem {
                        id: recentItem
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.name
                        subtitle: modelData.path
                        iconName: "folder"
                        onClicked: AppController.openVault(modelData.path)
                        trailing: BIconButton {
                            iconName: "x"
                            label: qsTr("Remove from the list")
                            size: "small"
                            onClicked: AppSettings.forgetRecentVault(recentItem.modelData.path)
                        }
                    }
                }
            }

            // ---- Syncing between devices -----------------------------------------------------
            BCard {
                Layout.fillWidth: true
                tone: "sunken"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.space3

                    BText {
                        Layout.fillWidth: true
                        text: qsTr("How do notes get to my other devices?")
                        variant: "label"
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: [
                            { icon: "folder-sync", title: "Syncthing",
                              text: qsTr("Share the notebook folder between computers and phones (on Android with Syncthing-Fork) and pick that folder in Brulion on every device. Edits arrive within seconds; conflicting copies can be merged in the sync center.") },
                            { icon: "cloud", title: "Nextcloud",
                              text: qsTr("Keep the notebook anywhere — also in app storage or in the browser — and connect your Nextcloud in Settings → Sync with an app password. Edits from several devices are merged, nothing is lost.") }
                        ]
                        delegate: RowLayout {
                            id: syncItem
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.space3
                            BIcon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                name: syncItem.modelData.icon
                                color: Theme.fgMuted
                            }
                            BText {
                                Layout.fillWidth: true
                                text: "<b>" + syncItem.modelData.title + "</b> — " + syncItem.modelData.text
                                textFormat: Text.StyledText
                                font.pixelSize: Theme.textSm
                                color: Theme.fgMuted
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose the notebook folder")
        onAccepted: pathField.text = AppController.localPath(selectedFolder)
    }
}
