pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CodePanelTabs — the tabs over Code mode's right panel (docs/CODE-FILES.md, docs/UI.md "Code
// mode"): Changes (the git panel, GitChangesPanel), Files (the file viewer, CodeFilesPanel), Search
// (the project search, CodeSearchPanel). `current` is the tab shown ("changes" | "files" |
// "search"); a click says `chosen(tab)` and the owner sets `current`.
BTabBar {
    id: tabs

    property string current: "changes"
    signal chosen(string tab)

    readonly property list<string> names: ["changes", "files", "search"]

    function follow(): void {
        tabs.currentIndex = Math.max(0, tabs.names.indexOf(tabs.current))
    }
    onCurrentChanged: tabs.follow()
    Component.onCompleted: tabs.follow()
    onPicked: (index) => tabs.chosen(tabs.names[index])

    BTabButton {
        objectName: "codeTabChanges"
        text: qsTr("Changes")
        iconName: "git-compare"
    }
    BTabButton {
        objectName: "codeTabFiles"
        text: qsTr("Files")
        iconName: "file-text"
    }
    BTabButton {
        objectName: "codeTabSearch"
        text: qsTr("Search")
        iconName: "search"
    }
}
