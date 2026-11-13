pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BBreadcrumb — path of the current location (folders, zoomed blocks); earlier parts are
// buttons, the last part is the current page.
//
//   BBreadcrumb {
//       model: [{ title: qsTr("Journal") }, { title: "2026" }, { title: "October" }]
//       onActivated: (index) => navigateTo(index)
//   }
//
// model: array of { title, icon? } or strings, shown as plain text (folder names, page titles
// and zoomed blocks' text never turn into markup). When it does not fit, the middle parts
// collapse into a "…" button that opens a menu with the hidden parts — on a phone the first part
// too, when it would leave the current page too little room.
Item {
    id: root

    property list<var> model
    property string rootIcon: ""

    signal activated(int index)

    /// { title, icon } per part (UiModels.crumbRows: strings become titles).
    readonly property list<var> items: UiModels.crumbRows(model)
    readonly property list<string> titles: UiModels.stringFields(items, "title")
    readonly property list<string> icons: UiModels.stringFields(items, "icon")
    // The widths below follow the delegate: a part is [chevron 14, 2] [4 icon? text 4] with 2
    // between parts; a button on touch UIs is at least a touch target wide (the current page,
    // the last part, is no button).
    readonly property real sep: 18
    readonly property real minButton: Theme.touchUi ? Theme.touchTarget : 0
    /// The width part `i` of `n` takes without its chevron (the last one in semi-bold, whole).
    function partWidth(i: int, n: int): real {
        const icon = icons[i] !== "" ? Theme.iconSizeSm + 4 : 0
        if (i === n - 1)
            return boldMetrics.advanceWidth(titles[i]) + icon + 8
        return Math.max(Math.min(metrics.advanceWidth(titles[i]), 220) + icon + 8, minButton)
    }
    readonly property real dotsWidth: Math.max(metrics.advanceWidth("…") + 8, minButton)
    /// The room the current page (the last part) keeps at least before parts give way to "…".
    readonly property real lastRoom: {
        const n = 0 + titles.length
        return n === 0 ? 0 : Math.min(partWidth(n - 1, n), 96)
    }
    readonly property bool fitsAll: {
        const n = 0 + titles.length
        let total = sep * Math.max(0, n - 1)
        for (let i = 0; i < n; ++i)
            total += partWidth(i, n)
        return total <= width
    }
    /// Whether the first part stays in view when the path does not fit: not when it and "…"
    /// would leave the current page too little (a phone).
    readonly property bool keepFirst: {
        const n = 0 + titles.length
        if (fitsAll || n < 2)
            return true
        return partWidth(0, n) + sep + (n > 2 ? dotsWidth + sep : 0) + lastRoom <= width
    }
    /// Whether "…" (the hidden parts' menu) is shown: not when even it would leave the current
    /// page too little — then the page's title alone.
    readonly property bool showDots: keepFirst || dotsWidth + sep + lastRoom <= width
    /// The leading parts in view: all of them when they fit, else the first one or none.
    readonly property int head: fitsAll ? titles.length : keepFirst ? 1 : 0
    // How many trailing parts stay visible when the path does not fit.
    readonly property int visibleTail: {
        // (0 +: a plain number, which compiled code passes on after the calls below.)
        const n = 0 + titles.length
        if (fitsAll)
            return n
        let used = (keepFirst ? partWidth(0, n) + sep : 0) + (showDots ? dotsWidth + sep : 0)
        let tail = 0
        // At least one part goes into "…" (index head).
        for (let i = n - 1; i > head; --i) {
            const w = i === n - 1 ? lastRoom : partWidth(i, n)
            if (used + w + (tail > 0 ? sep : 0) > width && tail >= 1)
                break
            used += w + (tail > 0 ? sep : 0)
            ++tail
        }
        return Math.max(1, tail)
    }
    readonly property bool collapsed: head + visibleTail < titles.length
    /// The titles of the parts collapsed into "…" (its menu).
    readonly property list<string> hiddenTitles: collapsed ? titles.slice(head, titles.length - visibleTail) : []
    /// { title, icon, partIndex } per part shown; "…" (partIndex -1) for the collapsed middle.
    readonly property list<var> shown: UiModels.shownCrumbs(items, head, visibleTail, showDots)

    implicitHeight: Theme.touchUi ? Theme.touchTarget : 28
    implicitWidth: row.implicitWidth

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Breadcrumb")

    FontMetrics {
        id: metrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
    }
    FontMetrics {
        id: boldMetrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
        font.weight: Theme.weightSemiBold
    }

    Row {
        id: row
        height: parent.height
        spacing: 2

        Repeater {
            model: root.shown
            delegate: Row {
                id: part
                required property int index
                required property string title
                required property string icon
                /// The part's index in the model; -1: the "…" button.
                required property int partIndex
                readonly property bool last: index === root.shown.length - 1
                height: row.height
                spacing: 2

                // (No name on the first part: no size, and the Row leaves it out.)
                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: part.index > 0 ? "chevron-right" : ""
                    size: 14
                    color: Theme.fgFaint
                }

                T.AbstractButton {
                    id: crumb
                    height: parent.height
                    enabled: !part.last
                    focusPolicy: part.last ? Qt.NoFocus : Qt.StrongFocus
                    hoverEnabled: !Theme.touchUi
                    // Touch: at least a touch target wide (short names, the "…" button), the
                    // name in the middle.
                    readonly property real minWidth: part.last ? 0 : root.minButton
                    leftPadding: Math.max(4, (minWidth - crumbRow.implicitWidth) / 2)
                    rightPadding: leftPadding
                    text: part.title
                    implicitWidth: crumbRow.implicitWidth + leftPadding + rightPadding
                    Accessible.role: part.last ? Accessible.StaticText : Accessible.Link
                    Accessible.name: part.partIndex < 0 ? qsTr("Show hidden path parts") : text
                    onClicked: {
                        if (part.partIndex < 0)
                            overflowMenu.popup(crumb, 0, crumb.height)
                        else
                            root.activated(part.partIndex)
                    }

                    contentItem: Row {
                        id: crumbRow
                        spacing: 4
                        BIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: part.icon
                            size: Theme.iconSizeSm
                            color: Theme.fgMuted
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: crumb.text
                            textFormat: Text.PlainText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            font.weight: part.last ? Theme.weightSemiBold : Theme.weightNormal
                            font.underline: crumb.hovered && !part.last && part.partIndex >= 0
                            color: part.last || crumb.hovered ? Theme.fg : Theme.fgMuted
                            elide: Text.ElideRight
                            // The current page takes what is left of the width (the chevron, the
                            // padding and its icon aside); earlier parts at most 220.
                            width: Math.min(implicitWidth, part.last
                                            ? Math.max(24, root.width - part.x - (part.index > 0 ? 16 : 0) - 8
                                                           - (part.icon !== "" ? Theme.iconSizeSm + 4 : 0))
                                            : 220)
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: crumb.down ? Theme.bgPressed : crumb.hovered && part.partIndex < 0 ? Theme.bgHover : "transparent"
                        BFocusRing {
                            visible: crumb.visualFocus
                            targetRadius: Theme.radiusSm
                        }
                    }
                }
            }
        }
    }

    BMenu {
        id: overflowMenu
        Instantiator {
            model: root.hiddenTitles
            delegate: BMenuItem {
                required property string modelData
                required property int index
                text: modelData
                iconName: "folder"
                onTriggered: root.activated(index + root.head)
            }
            onObjectAdded: (index, object) => overflowMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => overflowMenu.removeItem(object as Item)
        }
    }
}
