pragma ComponentBehavior: Bound
import QtQuick
import QtQml
import QtQuick.Templates as T
import Brulion

// BBreadcrumb — path of the current location (folders, zoomed blocks); earlier parts are
// buttons, the last part is the current page.
//
//   BBreadcrumb {
//       model: [{ title: qsTr("Dziennik") }, { title: "2026" }, { title: "Październik" }]
//       onActivated: (index) => navigateTo(index)
//   }
//
// model: array of { title, icon? } or strings, shown as plain text (folder names, page titles
// and zoomed blocks' text never turn into markup). When it does not fit, the middle parts
// collapse into a "…" button that opens a menu with the hidden parts.
Item {
    id: root

    property list<var> model
    property string rootIcon: ""

    signal activated(int index)

    /// { title, icon } per part (UiModels.crumbRows: strings become titles).
    readonly property list<var> items: UiModels.crumbRows(model)
    readonly property list<string> titles: UiModels.stringFields(items, "title")
    /// The width part `i` of `n` takes (the last one without the chevron's room).
    function partWidth(i: int, n: int): real {
        return metrics.advanceWidth(titles[i]) + (i === n - 1 ? 8 : 16)
    }
    // How many trailing parts stay visible next to the first one when collapsed.
    readonly property int visibleTail: {
        // (0 +: a plain number, which compiled code passes on after the calls below.)
        const n = 0 + titles.length
        if (n <= 2)
            return n
        const sep = 22
        const room = width
        let total = sep * (n - 1)
        for (let i = 0; i < n; ++i)
            total += partWidth(i, n)
        if (total <= room)
            return n
        let used = partWidth(0, n) + sep + 32 + sep
        let tail = 0
        for (let i = n - 1; i >= 1; --i) {
            const w = partWidth(i, n)
            if (used + w + (tail > 0 ? sep : 0) > room && tail >= 1)
                break
            used += w + (tail > 0 ? sep : 0)
            ++tail
        }
        return Math.max(1, tail)
    }
    readonly property bool collapsed: visibleTail < titles.length - 1 && titles.length > 2
    /// The titles of the parts collapsed into "…" (its menu).
    readonly property list<string> hiddenTitles: collapsed ? titles.slice(1, titles.length - visibleTail) : []
    /// { title, icon, partIndex } per part shown; "…" (partIndex -1) for the collapsed middle.
    readonly property list<var> shown: UiModels.shownCrumbs(items, visibleTail, collapsed)

    implicitHeight: Theme.touchUi ? Theme.touchTarget : 28
    implicitWidth: row.implicitWidth

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Breadcrumb")

    FontMetrics {
        id: metrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
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

                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: part.index > 0
                    name: part.index > 0 ? "chevron-right" : ""
                    size: 14
                    color: Theme.fgFaint
                }

                T.AbstractButton {
                    id: crumb
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height
                    enabled: !part.last
                    focusPolicy: part.last ? Qt.NoFocus : Qt.StrongFocus
                    hoverEnabled: !Theme.touchUi
                    leftPadding: 4
                    rightPadding: 4
                    text: part.title
                    // Touch: at least a touch target wide (short names, the "…" button).
                    implicitWidth: Math.max(crumbRow.implicitWidth + leftPadding + rightPadding,
                                            Theme.touchUi && !part.last ? Theme.touchTarget : 0)
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
                            width: Math.min(implicitWidth, part.last ? Math.max(60, root.width - part.x - 30) : 220)
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
                onTriggered: root.activated(index + 1)
            }
            onObjectAdded: (index, object) => overflowMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => overflowMenu.removeItem(object as Item)
        }
    }
}
