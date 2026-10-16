pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ImportExportView — route "importExport": bring notes in from other apps (qml/io/ImportPanel:
// Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Bear, Markdown/TXT,
// HTML, iCalendar — how to export from each, auto-detection with an override, a preview before
// anything is written, the report) and take them out (qml/io/ExportPanel: nine formats; the whole
// notebook, a folder or a page).
//
// Route parameters: tab ("import" | "export"); import: importer (an importer id), files (local
// paths or file:// / content:// URLs separated by "|"); export: format, scope, folder, path (the
// page to export). Ways here: Więcej → "Importuj notatki" / "Import i eksport", the drawer's
// "Importuj notatki" (phones), the empty journal's button, onboarding's import card.
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Import and export")

    function applyRoute() {
        if (route && route.tab)
            tabs.currentIndex = route.tab === "export" ? 1 : 0
        else if (route && (route.format || route.scope || (route.path && !route.files)))
            tabs.currentIndex = 1
    }
    onRouteChanged: applyRoute()
    Component.onCompleted: applyRoute()

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    component Page: Flickable {
        id: page
        default property alias content: column.data
        readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * margin + (Theme.narrow ? 56 + Theme.space4 : 0)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        ColumnLayout {
            id: column
            x: Math.max(page.margin, Math.round((page.width - width) / 2))
            y: page.margin
            width: Math.min(page.width - 2 * page.margin, 860)
            spacing: Theme.space5
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BTabBar {
            id: tabs
            Layout.fillWidth: true
            leftPadding: Theme.compact ? Theme.space2 : Theme.space6
            stretch: Theme.compact
            fadeColor: Theme.bg
            BTabButton { text: qsTr("Import"); iconName: "download" }
            BTabButton { text: qsTr("Export"); iconName: "upload" }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Page {
                BEmptyState {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !Workspace.isOpen
                    iconName: "notebook-pen"
                    title: qsTr("No notebook open")
                    description: qsTr("Open or create a notebook first.")
                }
                ImportPanel {
                    Layout.fillWidth: true
                    visible: Workspace.isOpen
                    preset: root.route && (root.route.importer || root.route.files)
                            ? { importer: root.route.importer || "", files: root.route.files || "" } : ({})
                }
            }
            Page {
                ExportPanel {
                    Layout.fillWidth: true
                    visible: Workspace.isOpen
                    preset: root.route && (root.route.format || root.route.scope || root.route.path || root.route.folder)
                            ? { format: root.route.format || "", scope: root.route.scope || "", folder: root.route.folder || "", path: root.route.path || "" }
                            : ({})
                }
            }
        }
    }
}
