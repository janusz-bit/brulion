pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ShellBottomNav — the phone's bottom navigation: four views (AppSettings.bottomNavTabs, by
// default Journal, Calendar, Search, Tasks; Settings → Appearance → Layout), quick capture
// ("+ Note", AppSettings.captureButtonShown) and Menu (opens the drawer), always last. Every
// item is one NavTab — a ≥ 48 px touch target with an icon in a pill and a label under it —;
// capture's "+" sits in a small ringed pill (the raised surface, as a button), so it reads as an
// action, never as the current view. The items share the bar by their labels (`widths`): evenly
// when every label fits an even share, else a long label ("Calendar") takes what it needs from
// the short ones ("Menu"), so the names stay whole at larger text sizes and on narrow phones.
// Nothing floats over the content. The bar extends under the Android navigation bar
// (`bottomInset`).
Rectangle {
    id: nav

    property real bottomInset: 0

    signal menuClicked()
    signal captureClicked()

    readonly property string view: AppController.currentView
    /// The items shown, in the bar's order: the views' tabs (AppSettings.bottomNavTabs: known
    /// views only), "capture" (quick capture, when shown) and "menu".
    readonly property list<string> shownIds: AppSettings.captureButtonShown
                                             ? Js.appendedString(Js.appendedString(AppSettings.bottomNavTabs, "capture"), "menu")
                                             : Js.appendedString(AppSettings.bottomNavTabs, "menu")
    /// An item's label.
    function titleOf(id: string): string {
        switch (id) {
        case "journals": return qsTr("Journal")
        case "calendar": return qsTr("Calendar")
        case "search": return qsTr("Search")
        case "tasks": return qsTr("Tasks")
        case "habits": return qsTr("Habits")
        case "allPages": return qsTr("Pages")
        case "tags": return qsTr("Tags")
        case "graph": return qsTr("Graph")
        case "capture": return qsTr("Note")
        case "menu": return qsTr("Menu")
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
        case "allPages": return "files"
        case "tags": return "tags"
        case "graph": return "waypoints"
        case "capture": return "plus"
        case "menu": return "menu"
        }
        return ""
    }

    // The items' geometry (one place for every item, capture's included).
    /// The smallest item: a touch target (Material's 48 px).
    readonly property int minItemWidth: 48
    /// The icon's pill: a view's (the current view's tint), an action's (narrower, ringed).
    readonly property int pillWidth: 56
    readonly property int actionPillWidth: 40
    readonly property int pillHeight: 28
    /// Above the pill and between the pill and the label.
    readonly property int pillTop: 7
    readonly property int labelGap: 3

    /// Each shown item's width by its id (0 for one not shown): the labels' needs (the label in
    /// the semi-bold of the current view, 4 px on each side, at least minItemWidth) shared out over
    /// the bar (`evenShare()`); when they do not fit, 2 px on each side; when even that does not
    /// fit, the needs scaled to the bar (the longest labels elide).
    function itemWidth(id: string): real {
        const index = nav.shownIds.indexOf(id)
        // (font.pixelSize read for real: the widths follow the text size and the UI font.)
        if (index < 0 || nav.width <= 0 || labelMetrics.font.pixelSize <= 0)
            return 0
        const wide = nav.evenShare(8)
        if (wide >= 0)
            return Math.max(nav.need(index, 8), wide)
        const narrow = nav.evenShare(4)
        if (narrow >= 0)
            return Math.max(nav.need(index, 4), narrow)
        let sum = 0
        for (let i = 0; i < nav.shownIds.length; ++i)
            sum += nav.need(i, 4)
        return nav.need(index, 4) * nav.width / sum
    }
    /// What item `index` needs with `pad` px around its label.
    function need(index: int, pad: int): real {
        return Math.max(nav.minItemWidth, Math.ceil(labelMetrics.advanceWidth(nav.titleOf(nav.shownIds[index]))) + pad)
    }
    /// The needs (`pad`) shared out over the bar: evenly, but an item that needs more than an even
    /// share gets its need and the others share the rest evenly — that even share; -1 when the
    /// needs do not fit. (The largest needs first, as sorted.)
    function evenShare(pad: int): real {
        const n = nav.shownIds.length
        let fixedMask = 0
        let fixed = 0
        let k = 0
        while (k < n) {
            let largest = -1
            for (let i = 0; i < n; ++i) {
                if ((fixedMask & (1 << i)) === 0 && (largest < 0 || nav.need(i, pad) > nav.need(largest, pad)))
                    largest = i
            }
            if (nav.need(largest, pad) <= (nav.width - fixed) / (n - k))
                break
            fixed += nav.need(largest, pad)
            fixedMask |= 1 << largest
            ++k
        }
        if (k === n)
            return -1
        return (nav.width - fixed) / (n - k)
    }

    implicitHeight: 60 + bottomInset
    color: Theme.bgElevated

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Navigation")

    FontMetrics {
        id: labelMetrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs
        font.weight: Theme.weightSemiBold
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    /// A label under an icon, elided only when it does not fit its item after all (the extreme
    /// text sizes; 1 px kept on each side).
    component NavLabel: BText {
        required property Item item
        // (Centred in its column.)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        font.pixelSize: Theme.textXs
        readonly property bool tooWide: implicitWidth > item.width - 2 && item.width > 2
        width: tooWide ? item.width - 2 : implicitWidth
        elide: tooWide ? Text.ElideRight : Text.ElideNone
    }

    function openView(id: string, current: bool): void {
        if (id === "journals" && current)
            AppController.openToday()
        else
            AppController.navigate({ view: id })
    }

    /// An item of the bar: a view's tab, quick capture (`itemId` "capture") or Menu ("menu")
    /// (`currentView`: the route's view; the instance acts on clicked()).
    component NavTab: T.AbstractButton {
        id: tab
        property string itemId
        property string currentView
        readonly property string title: nav.titleOf(itemId)
        /// An action (quick capture), not a view: never the current one.
        readonly property bool isAction: itemId === "capture"
        readonly property bool current: !isAction
                                        && (itemId === currentView
                                            || (itemId === "journals" && currentView === "journal")
                                            || (itemId === "tags" && currentView === "tag"))

        objectName: "bottomNav_" + itemId
        width: nav.itemWidth(itemId)
        height: parent ? parent.height : 0
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: isAction ? Accessible.Button : Accessible.PageTab
        Accessible.name: title
        Accessible.selected: current

        contentItem: Column {
            spacing: nav.labelGap
            topPadding: nav.pillTop

            Rectangle {
                id: pill
                anchors.horizontalCenter: parent.horizontalCenter
                // Never wider than the item: it would run into a neighbour on a narrow phone.
                width: Math.min(tab.isAction ? nav.actionPillWidth : nav.pillWidth, tab.width - 4)
                height: nav.pillHeight
                radius: height / 2
                antialiasing: true
                // An action: the old floating button's look, scaled down — the popup surface with
                // a 1-px ring (it carries the edge where the surface is as light as the bar).
                color: tab.isAction ? Theme.bgElevated2
                     : tab.current ? Theme.bgSelected : tab.pressed ? Theme.bgPressed : "transparent"
                border.width: tab.isAction ? 1 : 0
                border.color: Theme.dark ? Theme.borderStrong : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                // An action's hover and press, as overlays on its surface.
                Rectangle {
                    visible: tab.isAction
                    anchors.fill: parent
                    radius: pill.radius
                    antialiasing: true
                    color: tab.down ? Theme.bgPressed : tab.hovered ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
                BIcon {
                    anchors.centerIn: parent
                    name: nav.iconOf(tab.itemId)
                    size: Theme.iconSize
                    color: tab.current || tab.isAction ? Theme.fg : Theme.fgMuted
                }
            }
            NavLabel {
                item: tab
                text: tab.title
                font.weight: tab.current ? Theme.weightSemiBold : Theme.weightMedium
                color: tab.current ? Theme.fg : Theme.fgMuted
            }
        }
        background: Item {
            BFocusRing {
                target: tab
                visible: tab.visualFocus
                inset: true
                targetRadius: Theme.radius
            }
        }
    }

    Row {
        width: parent.width
        height: parent.height - nav.bottomInset

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

        // Quick capture: a note into today's journal (ShellQuickCapture). Hidden by the user
        // (Settings → Appearance → Layout): capture stays in the palette and under Ctrl+Shift+N.
        // Its accessible name holds its label (WCAG 2.5.3: "Note" in "Quick note").
        NavTab {
            objectName: "shellCaptureButton"
            itemId: "capture"
            visible: AppSettings.captureButtonShown
            Accessible.name: qsTr("Quick note")
            onClicked: nav.captureClicked()
        }

        NavTab {
            itemId: "menu"
            currentView: nav.view
            onClicked: nav.menuClicked()
        }
    }
}
