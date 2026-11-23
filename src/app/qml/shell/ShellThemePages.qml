pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellThemePages — Settings → Appearance → Themes (SettingsView's "appearance" section, after the
// appearance settings; docs/UI.md "Look & feel"; C++: ThemePages, src/app/shell/themepages.h): the
// notebook's theme pages (pages tagged #theme / #motyw, docs/SYNTAX.md §13 "Theme pages") next to
// "None" — the appearance settings alone —, each with its colours in both modes and how many of its
// tokens were ignored; a click applies one, at once, in both modes. The notebook's choice syncs with
// it; "Only on this device" keeps this device's own. A choice whose page is gone says so. "Save the
// look as a theme page" writes the look in force into a new page and opens it. A card like
// SettingsView's groups (its heading, note, hairlines).
ColumnLayout {
    id: section

    objectName: "themePagesSettings"
    Layout.fillWidth: true
    spacing: Theme.space2

    readonly property var pages: ThemePages.pages
    readonly property string current: ThemePages.current
    /// The swatches of a row: its page in each mode.
    readonly property list<string> modes: ["light", "dark"]

    BText {
        Layout.fillWidth: true
        text: qsTr("Themes")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        objectName: "themePagesNote"
        text: qsTr("A theme is a page of the notebook tagged #theme: its colours, fonts, corners and spacing over the settings above. Every device that follows the notebook shows the one picked; share a theme by copying its page's file.")
        variant: "muted"
        wrapMode: Text.Wrap
    }
    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space3
        contentWidth: rows.implicitWidth
        contentHeight: rows.implicitHeight
        ColumnLayout {
            id: rows
            anchors.fill: parent
            spacing: Theme.space1

            BListItem {
                objectName: "themePage_none"
                Layout.fillWidth: true
                iconName: "palette"
                text: qsTr("None")
                subtitle: qsTr("The appearance settings alone")
                selected: section.current === ""
                trailing: BIcon {
                    visible: section.current === ""
                    name: "check"
                    size: Theme.iconSizeSm
                    color: Theme.accentText
                }
                onClicked: ThemePages.pick("")
            }
            Repeater {
                objectName: "themePagesList"
                model: section.pages
                delegate: BListItem {
                    id: pageRow
                    // (The rows' keys: ThemePages.pages.)
                    required property int index
                    required property string path
                    required property string title
                    required property string description
                    required property int tokens
                    required property int issues
                    required property string lightBg
                    required property string lightAccent
                    required property string lightFg
                    required property string darkBg
                    required property string darkAccent
                    required property string darkFg
                    objectName: "themePage_" + index
                    Layout.fillWidth: true
                    text: title
                    subtitle: issues > 0 ? qsTr("%n line(s) ignored: open the page to see why", "", issues)
                                         : description !== "" ? description
                                         : qsTr("%n token(s)", "", tokens)
                    subtitleColor: issues > 0 ? Theme.warningText : Theme.fgMuted
                    selected: section.current === path
                    trailing: RowLayout {
                        spacing: Theme.space2
                        // The page over the settings in force, light and dark: its background with its
                        // accent and text on it.
                        Repeater {
                            model: section.modes
                            delegate: Rectangle {
                                id: swatch
                                required property string modelData
                                readonly property bool darkMode: modelData === "dark"
                                implicitWidth: 34
                                implicitHeight: 22
                                radius: Theme.radiusSm
                                color: darkMode ? pageRow.darkBg : pageRow.lightBg
                                border.color: Theme.border
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 6
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: swatch.darkMode ? pageRow.darkAccent : pageRow.lightAccent
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 20
                                    width: 8
                                    height: 3
                                    radius: 1
                                    color: swatch.darkMode ? pageRow.darkFg : pageRow.lightFg
                                }
                            }
                        }
                        BIconButton {
                            objectName: "themePageOpen_" + pageRow.index
                            label: qsTr("Open the page")
                            iconName: "file-text"
                            size: "small"
                            onClicked: AppController.openPage(pageRow.path, -1, false)
                        }
                        BIcon {
                            opacity: section.current === pageRow.path ? 1 : 0
                            name: "check"
                            size: Theme.iconSizeSm
                            color: Theme.accentText
                        }
                    }
                    onClicked: ThemePages.pick(pageRow.path)
                }
            }
            BText {
                objectName: "themePagesEmpty"
                Layout.fillWidth: true
                Layout.margins: Theme.space2
                visible: ThemePages.count === 0
                text: qsTr("No theme pages in this notebook yet. Save the look below as one, or tag a page #theme and write its colours in a ```theme block.")
                variant: "muted"
                wrapMode: Text.Wrap
            }
            BText {
                objectName: "themePagesMissing"
                Layout.fillWidth: true
                Layout.margins: Theme.space2
                visible: ThemePages.missing
                text: qsTr("The theme “%1” is not in this notebook (deleted, or not synced yet): the appearance settings show meanwhile.")
                      .arg(section.current)
                color: Theme.warningText
                wrapMode: Text.Wrap
            }
            BSwitch {
                objectName: "themePagesDeviceOnly"
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                text: qsTr("Only on this device")
                description: ThemePages.deviceOnly
                             ? qsTr("This device keeps its own theme; the notebook's is left as it is for the others.")
                             : qsTr("The theme picked is the notebook's: every device that follows the notebook shows it.")
                checked: ThemePages.deviceOnly
                onToggled: ThemePages.deviceOnly = checked
            }
            BButton {
                objectName: "themePagesExport"
                Layout.topMargin: Theme.space2
                size: "small"
                variant: "ghost"
                iconName: "plus"
                text: qsTr("Save the look as a theme page")
                onClicked: {
                    const path = ThemePages.exportCurrent()
                    if (path !== "")
                        AppController.openPage(path, -1, false)
                    else
                        AppController.toast(qsTr("The theme page could not be made."), "error")
                }
            }
        }
    }
}
