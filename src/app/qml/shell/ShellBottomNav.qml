pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ShellBottomNav — the phone's bottom navigation: four views (AppSettings.bottomNavTabs, by
// default Dziennik, Kalendarz, Szukaj, Zadania; Ustawienia → Wygląd → Układ), quick capture
// ("+ Notatka", AppSettings.captureButtonShown) and Menu (opens the drawer), always last. Every
// item is one NavTab — a ≥ 48 px touch target with an icon in a pill and a label under it —;
// capture's "+" sits in a small ringed pill (the raised surface, as a button), so it reads as an
// action, never as the current view. The items share the bar by their labels (`widths`): evenly
// when every label fits an even share, else a long label ("Kalendarz") takes what it needs from
// the short ones ("Menu"), so the names stay whole at larger text sizes and on narrow phones.
// Nothing floats over the content. The bar extends under the Android navigation bar
// (`bottomInset`).
Rectangle {
    id: nav

    property real bottomInset: 0

    signal menuClicked()
    signal captureClicked()

    readonly property string view: AppController.currentRoute.view || ""
    /// The views a tab can show.
    readonly property var tabs: ({
        journals: { id: "journals", title: qsTr("Journal"), icon: "book-open" },
        calendar: { id: "calendar", title: qsTr("Calendar"), icon: "calendar" },
        search: { id: "search", title: qsTr("Search"), icon: "search" },
        tasks: { id: "tasks", title: qsTr("Tasks"), icon: "list-todo" },
        allPages: { id: "allPages", title: qsTr("Pages"), icon: "files" },
        tags: { id: "tags", title: qsTr("Tags"), icon: "tags" },
        graph: { id: "graph", title: qsTr("Graph"), icon: "waypoints" }
    })
    readonly property var captureItem: ({ id: "capture", title: qsTr("Note"), icon: "plus", action: true })
    readonly property var menuItem: ({ id: "menu", title: qsTr("Menu"), icon: "menu" })
    /// The items shown, in the bar's order.
    readonly property var shownItems: {
        const out = AppSettings.bottomNavTabs.map(v => nav.tabs[v]).filter(t => t !== undefined)
        if (AppSettings.captureButtonShown)
            out.push(captureItem)
        out.push(menuItem)
        return out
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

    /// Each shown item's width by its id: `share()` of the labels' needs (the label in the
    /// semi-bold of the current view, 4 px on each side, at least minItemWidth); when they do not
    /// fit, 2 px on each side; when even that does not fit, the needs scaled to the bar (the
    /// longest labels elide).
    readonly property var widths: {
        const out = {}
        const items = shownItems
        // (font.pixelSize read for real: the widths follow the text size and the UI font.)
        if (items.length === 0 || width <= 0 || labelMetrics.font.pixelSize <= 0)
            return out
        const label = items.map(i => Math.ceil(labelMetrics.advanceWidth(i.title)))
        let result = null
        for (const pad of [8, 4]) {
            result = share(label.map(l => Math.max(minItemWidth, l + pad)), width)
            if (result)
                break
        }
        if (!result) {
            const needs = label.map(l => Math.max(minItemWidth, l + 4))
            const sum = needs.reduce((a, b) => a + b, 0)
            result = needs.map(n => n * width / sum)
        }
        items.forEach((item, i) => { out[item.id] = result[i] })
        return out
    }
    /// `needs` shared out over `w`: evenly, but an item that needs more than an even share gets
    /// its need and the others share the rest evenly; null when the needs do not fit.
    function share(needs, w) {
        const n = needs.length
        const sorted = needs.slice().sort((a, b) => b - a)
        let fixed = 0
        let k = 0
        while (k < n && sorted[k] > (w - fixed) / (n - k)) {
            fixed += sorted[k]
            ++k
        }
        if (k === n)
            return null
        const even = (w - fixed) / (n - k)
        return needs.map(x => Math.max(x, even))
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
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        font.pixelSize: Theme.textXs
        readonly property bool tooWide: implicitWidth > item.width - 2 && item.width > 2
        width: tooWide ? item.width - 2 : implicitWidth
        elide: tooWide ? Text.ElideRight : Text.ElideNone
    }

    function openView(id, current) {
        if (id === "journals" && current)
            AppController.openToday()
        else
            AppController.navigate({ view: id })
    }

    /// An item of the bar: a view's tab, quick capture (`modelData.action`) or Menu
    /// (`currentView`: the route's view; the instance acts on clicked()).
    component NavTab: T.AbstractButton {
        id: tab
        required property var modelData
        property string currentView
        /// An action (quick capture), not a view: never the current one.
        readonly property bool isAction: modelData.action === true
        readonly property bool current: !isAction
                                        && (modelData.id === currentView
                                            || (modelData.id === "journals" && currentView === "journal")
                                            || (modelData.id === "tags" && currentView === "tag"))

        objectName: "bottomNav_" + modelData.id
        width: nav.widths[modelData.id] || 0
        height: parent ? parent.height : 0
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: isAction ? Accessible.Button : Accessible.PageTab
        Accessible.name: modelData.title
        Accessible.selected: current

        contentItem: Column {
            spacing: nav.labelGap
            topPadding: nav.pillTop

            Rectangle {
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
                    radius: parent.radius
                    antialiasing: true
                    color: tab.down ? Theme.bgPressed : tab.hovered ? Theme.bgHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
                BIcon {
                    anchors.centerIn: parent
                    name: tab.modelData.icon
                    size: Theme.iconSize
                    color: tab.current || tab.isAction ? Theme.fg : Theme.fgMuted
                }
            }
            NavLabel {
                item: tab
                text: tab.modelData.title
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
            model: AppSettings.bottomNavTabs.map(v => nav.tabs[v]).filter(t => t !== undefined)
            delegate: NavTab {
                id: viewTab
                currentView: nav.view
                onClicked: nav.openView(viewTab.modelData.id, viewTab.current)
            }
        }

        // Quick capture: a note into today's journal (ShellQuickCapture). Hidden by the user
        // (Ustawienia → Wygląd → Układ): capture stays in the palette and under Ctrl+Shift+N.
        // Its accessible name holds its label (WCAG 2.5.3: "Note" in "Quick note").
        NavTab {
            objectName: "shellCaptureButton"
            modelData: nav.captureItem
            visible: AppSettings.captureButtonShown
            Accessible.name: qsTr("Quick note")
            onClicked: nav.captureClicked()
        }

        NavTab {
            modelData: nav.menuItem
            currentView: nav.view
            onClicked: nav.menuClicked()
        }
    }
}
