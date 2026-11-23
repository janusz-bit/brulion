pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellBottomNav — the phone's bottom navigation: five views (AppSettings.bottomNavTabs, by
// default Journal, Pages, Search, Tasks, Calendar; Settings → Appearance → Layout), then at most
// two buttons of extensions (b.app.contribute("bottomNav", …), docs/UI.md "Extension items in the
// UI"): their icon, their label as the name; one that opens a view of its extension shows as
// current while that view does. They come after the views and only while every item still gets
// its 48 px (a narrow phone shows fewer of them: never one view less). Every item switches the
// view and shows which one is current. Actions live on the top
// bar — the notebook's button opens the drawer, + quick capture —, so none of them repeats here.
// A floating capsule (as in Instagram or Material 3's apps): a raised, rounded bar held off the
// screen's edges, its items icons only; the current view's item grows into an accent-tinted pill
// with its name beside the icon, so the bar says where you are without five labels. The others
// share the rest evenly; each is a ≥ 48 px touch target with its name as the accessible name (and
// a tooltip on a long press). The page's surface (Theme.bg) shows around the capsule; the bar's
// room is its own, nothing of a view hides under it. The bar extends under the Android navigation
// bar (`bottomInset`).
Rectangle {
    id: nav

    property real bottomInset: 0

    readonly property string view: AppController.currentView
    /// The items shown, in the bar's order (AppSettings.bottomNavTabs: known views only).
    readonly property list<string> shownIds: AppSettings.bottomNavTabs
    readonly property list<var> noRows: []
    /// The extensions' buttons as text: made again only when they change.
    readonly property string itemsKey: Workspace.extensions !== null
                                       ? Workspace.extensions.contributionsKey("bottomNav", Workspace.extensions.appRevision) : ""
    readonly property list<var> extensionItems: nav.itemsKey !== ""
                                                ? Workspace.extensions.contributions("bottomNav", nav.itemsKey.length) : nav.noRows
    /// How many of them are shown: as many as leave every item its smallest width.
    readonly property int extensionsShown: Math.max(0, Math.min(nav.extensionItems.length,
            Math.floor((capsule.width - 2 * nav.innerPadding + 2) / (nav.minItemWidth + 2)) - nav.shownIds.length))
    /// The items in the bar: the views and the extensions' buttons shown.
    readonly property int itemCount: nav.shownIds.length + nav.extensionsShown
    /// The route's extension view ("extension/name"), "" for another view.
    readonly property string extensionView: view === "extension"
                                            ? AppController.routeText(AppController.currentRoute, "extension") + "/"
                                              + AppController.routeText(AppController.currentRoute, "name")
                                            : ""
    /// An item's label.
    function titleOf(id: string): string {
        switch (id) {
        case "journals": return qsTr("Journal")
        case "calendar": return qsTr("Calendar")
        case "search": return qsTr("Search")
        case "tasks": return qsTr("Tasks")
        case "habits": return qsTr("Habits")
        case "gym": return qsTr("Gym")
        case "flashcards": return qsTr("Flashcards")
        case "allPages": return qsTr("Pages")
        case "tags": return qsTr("Tags")
        case "graph": return qsTr("Graph")
        }
        return ""
    }
    /// An item's icon.
    function iconOf(id: string): string {
        switch (id) {
        case "journals": return "book-open"
        case "calendar": return "calendar"
        case "search": return "search"
        case "tasks": return "list-todo"
        case "habits": return "flame"
        case "gym": return "dumbbell"
        case "flashcards": return "layers"
        case "allPages": return "files"
        case "tags": return "tags"
        case "graph": return "waypoints"
        }
        return ""
    }

    // The capsule's geometry (one place for every item).
    /// Its height, its distance from the screen's sides and from what lies below it.
    readonly property int capsuleHeight: 56
    readonly property int sideMargin: 14
    readonly property int topMargin: 2
    readonly property int bottomMargin: 6
    /// The items' room inside the capsule (the items 46 px high: touch targets).
    readonly property int innerPadding: 5
    /// The smallest item: a touch target (Material's 48 px).
    readonly property int minItemWidth: 48

    implicitHeight: topMargin + capsuleHeight + bottomMargin + bottomInset
    color: Theme.bg

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Navigation")

    function openView(id: string, current: bool): void {
        if (id === "journals" && current)
            AppController.openToday()
        else
            AppController.navigate({ view: id })
    }

    /// An item of the bar: a view's tab (`currentView`: the route's view), or an extension's
    /// button (`label` and `glyph` given; `extensionCurrent`: the view it opens is shown).
    component NavTab: T.AbstractButton {
        id: tab
        property string itemId
        property string currentView
        property string label
        property string glyph
        property bool extensionCurrent: false
        readonly property string title: tab.label !== "" ? tab.label : nav.titleOf(itemId)
        readonly property bool current: tab.label !== "" ? tab.extensionCurrent
                                        : itemId === currentView
                                          || (itemId === "journals" && currentView === "journal")
                                          || (itemId === "tags" && currentView === "tag")
        /// The current item's width: its pill with the icon and the name — at most what the other
        /// items leave at their smallest (a name too long even then elides).
        /// (font.pixelSize read for real: a method's result is not followed — the width follows
        /// the text size and the UI font, docs/UI.md "Design system in code".)
        readonly property real pillWidth: nameMetrics.font.pixelSize <= 0 ? nav.minItemWidth
                                          : Math.min(Math.ceil(nameMetrics.advanceWidth(tab.title)) + Theme.iconSize + 8 + 2 * 14,
                                                   capsule.width - 2 * nav.innerPadding
                                                   - (nav.itemCount - 1) * (nav.minItemWidth + 2))

        objectName: "bottomNav_" + itemId
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        Layout.fillHeight: true
        Layout.fillWidth: !tab.current
        Layout.minimumWidth: nav.minItemWidth
        /// The width asked of the layout, animated: the pill grows and the others make room.
        property real wantedWidth: tab.current ? tab.pillWidth : nav.minItemWidth
        Behavior on wantedWidth { NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
        Layout.preferredWidth: tab.wantedWidth
        Accessible.role: Accessible.PageTab
        Accessible.name: title
        Accessible.description: itemId === "flashcards" && Workspace.extensions.flashcardsDue > 0
                                ? qsTr("%n to study now", "", Workspace.extensions.flashcardsDue) : ""
        Accessible.selected: current
        scale: tab.pressed ? Theme.pressScale : 1
        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }

        FontMetrics {
            id: nameMetrics
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightSemiBold
        }

        contentItem: Item {
            Row {
                id: content
                anchors.centerIn: parent
                spacing: 8
                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: tab.glyph !== "" ? tab.glyph : nav.iconOf(tab.itemId)
                    size: Theme.iconSize
                    color: tab.current ? Theme.accentText : tab.pressed || tab.hovered ? Theme.fg : Theme.fgMuted
                    // The flashcards to study now, at the icon's corner.
                    BBadge {
                        objectName: "bottomNavBadge"
                        readonly property int due: tab.itemId === "flashcards" ? Workspace.extensions.flashcardsDue : 0
                        visible: due > 0
                        x: parent.width - 6
                        y: -6
                        count: due
                        variant: "accent"
                        Accessible.ignored: true
                    }
                }
                BText {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tab.current
                    width: Math.min(implicitWidth, Math.max(0, tab.width - Theme.iconSize - 8 - 2 * 12))
                    elide: Text.ElideRight
                    text: tab.title
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                    color: Theme.accentText
                }
            }
        }
        background: Rectangle {
            radius: height / 2
            antialiasing: true
            color: tab.current ? Theme.accentBg
                 : tab.pressed ? Theme.bgPressed : tab.hovered ? Theme.bgHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            BFocusRing {
                target: tab
                visible: tab.visualFocus
                inset: true
                targetRadius: tab.height / 2
            }
        }

        // The name on a long press (touch has no hover), as an icon button's.
        property bool _touchTip: false
        onPressAndHold: _touchTip = true
        Loader {
            active: tab._touchTip || (tab.hovered && !tab.current)
            sourceComponent: BToolTip {
                parent: tab
                text: tab.title
                visible: tab._touchTip || (tab.hovered && !tab.pressed)
                onClosed: tab._touchTip = false
            }
        }
    }

    BShadow {
        target: capsule
        level: 2
    }
    Rectangle {
        id: capsule
        objectName: "bottomNavCapsule"
        x: nav.sideMargin
        y: nav.topMargin
        width: nav.width - 2 * nav.sideMargin
        height: nav.capsuleHeight
        radius: height / 2
        antialiasing: true
        // (bgElevated: the accent's text keeps 4.5:1 on its tint there, ThemePalette.)
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.borderSubtle

        RowLayout {
            anchors.fill: parent
            anchors.margins: nav.innerPadding
            spacing: 2

            Repeater {
                // (Known views only: AppSettings normalizes them.)
                model: AppSettings.bottomNavTabs
                delegate: NavTab {
                    id: viewTab
                    required property string modelData
                    itemId: modelData
                    currentView: nav.view
                    onClicked: nav.openView(viewTab.itemId, viewTab.current)
                }
            }
            Repeater {
                // The extensions' buttons (b.app.contribute("bottomNav", …)): a command gets the
                // page on screen (ctx.page).
                model: nav.extensionItems
                delegate: NavTab {
                    id: extensionTab
                    // (A row of WorkspaceExtensions.contributions(): its `id` and `icon` read
                    // through modelData — a delegate's property cannot have those names.)
                    required property var modelData
                    required property int index
                    required property string extension
                    readonly property string opens: UiModels.stringField(extensionTab.modelData, "view")
                    visible: extensionTab.index < nav.extensionsShown
                    itemId: UiModels.stringField(extensionTab.modelData, "id")
                    objectName: "contribution_bottomNav_" + extensionTab.itemId
                    label: UiModels.stringField(extensionTab.modelData, "label")
                    glyph: UiModels.stringField(extensionTab.modelData, "icon")
                    currentView: nav.view
                    extensionCurrent: extensionTab.opens !== "" && nav.extensionView === extensionTab.extension + "/" + extensionTab.opens
                    onClicked: AppScripting.activate("bottomNav", extensionTab.extension, extensionTab.itemId,
                                                     AppController.routeText(AppController.currentRoute, "path"), -1)
                }
            }
        }
    }
}
