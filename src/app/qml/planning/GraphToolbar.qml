pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GraphToolbar — the controls floating over the graph: search a page (results list, Enter takes
// the first), whole graph / local graph around a page with depth 1 or 2, and toggles for tags,
// pages that do not exist yet, daily notes and templates. On phones the options fold behind a
// button.
//
//   GraphToolbar { model: graphModel; localMode: …; depth: …
//                  onNodeChosen: (id) => …; onLocalModeRequested: (on) => …; onDepthRequested: (d) => …
//                  onOptionToggled: (name, on) => … }
BCard {
    id: root

    required property GraphModel model
    property bool localMode: false
    property int depth: 1
    /// The local graph needs a page: the route's or the selected node.
    property bool localAvailable: false
    property bool showTags: false
    property bool showMissing: false
    property bool showJournals: true
    property bool showTemplates: false
    /// Options folded (phones).
    property bool collapsible: Theme.compact
    property bool expanded: !collapsible

    signal nodeChosen(string id)
    signal localModeRequested(bool on)
    signal depthRequested(int depth)
    signal optionToggled(string name, bool on)

    /// The pages matching the search: [{ id, kind, title, degree }].
    readonly property list<var> results: findResults(search.text)
    readonly property list<var> scopes: [
        { value: "global", text: qsTr("Whole graph"), icon: "waypoints" },
        { value: "local", text: qsTr("Local"), icon: "network" }
    ]
    readonly property list<var> depths: [{ value: 1, text: "1" }, { value: 2, text: "2" }]

    /// A search is typed.
    readonly property bool searching: Js.trim(search.text) !== ""

    function findResults(text: string): list<var> {
        if (Js.trim(text) === "")
            return []
        return root.model.find(text, 8)
    }
    /// A toggle chip of the options: `optionName` is sent to optionToggled() with the opposite of `isOn`.
    component OptionChip: BChip {
        required property string optionName
        required property bool isOn
        interactive: true
        selected: isOn
        onClicked: root.optionToggled(optionName, !isOn)
    }
    function focusSearch(): void { search.forceActiveFocus() }
    function choose(index: int): void {
        if (index < 0 || index >= results.length)
            return
        nodeChosen(AppController.routeText(results[index], "id"))
        search.text = ""
    }

    elevation: 2
    padding: Theme.space2
    implicitWidth: Theme.compact ? 320 : 340

    contentItem: ColumnLayout {
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space1
            BSearchField {
                id: search
                Layout.fillWidth: true
                size: "small"
                debounce: 0
                placeholderText: qsTr("Find a page in the graph…")
                onAccepted: root.choose(resultList.currentIndex >= 0 ? resultList.currentIndex : 0)
                Keys.onDownPressed: resultList.incrementCurrentIndex()
                Keys.onUpPressed: resultList.decrementCurrentIndex()
            }
            BIconButton {
                visible: root.collapsible
                iconName: "filter"
                label: root.expanded ? qsTr("Hide options") : qsTr("Graph options")
                checkable: true
                checked: root.expanded
                onClicked: root.expanded = !root.expanded
            }
        }

        // Search results.
        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            visible: root.searching
            interactive: false
            model: root.results
            currentIndex: count > 0 ? 0 : -1
            delegate: BListItem {
                id: resultRow
                required property string kind
                required property string title
                required property int degree
                required property int index
                width: ListView.view.width
                dense: true
                selected: ListView.isCurrentItem
                iconName: kind === "tag" ? "hash" : kind === "journal" ? "book-open"
                        : kind === "missing" ? "file" : "file-text"
                text: title
                count: degree
                onClicked: root.choose(resultRow.index)
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.searching && root.results.length === 0
            text: qsTr("No such page in the graph.")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgMuted
            leftPadding: Theme.space2
        }

        // Scope and toggles.
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: Theme.space2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BSegmented {
                    Layout.fillWidth: true
                    fill: true
                    size: "small"
                    model: root.scopes
                    currentValue: root.localMode ? "local" : "global"
                    enabled: root.localAvailable || root.localMode
                    accessibleName: qsTr("Graph scope")
                    onActivated: (value) => root.localModeRequested(value === "local")
                }
                BSegmented {
                    visible: root.localMode
                    size: "small"
                    model: root.depths
                    currentValue: root.depth
                    accessibleName: qsTr("Links away from the page")
                    onActivated: (value) => root.depthRequested(value)
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                OptionChip { optionName: "tags"; isOn: root.showTags; iconName: "hash"; text: qsTr("Tags") }
                OptionChip { optionName: "missing"; isOn: root.showMissing; iconName: "file"; text: qsTr("Missing pages") }
                OptionChip { optionName: "journals"; isOn: root.showJournals; iconName: "book-open"; text: qsTr("Journals") }
                OptionChip { optionName: "templates"; isOn: root.showTemplates; iconName: "sparkles"; text: qsTr("Templates") }
            }
        }
    }
}
