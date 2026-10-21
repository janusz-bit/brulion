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

    property var model: []
    property string rootIcon: ""

    signal activated(int index)

    readonly property var items: {
        const out = []
        for (let i = 0; i < (model ? model.length : 0); ++i)
            out.push(typeof model[i] === "object" ? model[i] : { title: String(model[i]) })
        return out
    }
    // How many trailing parts stay visible next to the first one when collapsed.
    readonly property int visibleTail: {
        const n = items.length
        if (n <= 2)
            return n
        const sep = 22
        const widths = items.map((it, i) => metrics.advanceWidth(it.title) + (i === n - 1 ? 8 : 16))
        const total = widths.reduce((a, b) => a + b, 0) + sep * (n - 1)
        if (total <= width)
            return n
        let used = widths[0] + sep + 32 + sep
        let tail = 0
        for (let i = n - 1; i >= 1; --i) {
            if (used + widths[i] + (tail > 0 ? sep : 0) > width && tail >= 1)
                break
            used += widths[i] + (tail > 0 ? sep : 0)
            ++tail
        }
        return Math.max(1, tail)
    }
    readonly property bool collapsed: visibleTail < items.length - 1 && items.length > 2
    readonly property var shown: {
        if (!collapsed) {
            return items.map((it, i) => ({ title: it.title, icon: it.icon || "", index: i }))
        }
        const out = [{ title: items[0].title, icon: items[0].icon || "", index: 0 }, { title: "…", icon: "", index: -1 }]
        for (let i = items.length - visibleTail; i < items.length; ++i)
            out.push({ title: items[i].title, icon: items[i].icon || "", index: i })
        return out
    }

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
                required property var modelData
                required property int index
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
                    text: part.modelData.title
                    // Touch: at least a touch target wide (short names, the "…" button).
                    implicitWidth: Math.max(crumbRow.implicitWidth + leftPadding + rightPadding,
                                            Theme.touchUi && !part.last ? Theme.touchTarget : 0)
                    Accessible.role: part.last ? Accessible.StaticText : Accessible.Link
                    Accessible.name: part.modelData.index < 0 ? qsTr("Show hidden path parts") : text
                    onClicked: {
                        if (part.modelData.index < 0)
                            overflowMenu.popup(crumb, 0, crumb.height)
                        else
                            root.activated(part.modelData.index)
                    }

                    contentItem: Row {
                        id: crumbRow
                        spacing: 4
                        BIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: part.modelData.icon
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
                            font.underline: crumb.hovered && !part.last && part.modelData.index >= 0
                            color: part.last || crumb.hovered ? Theme.fg : Theme.fgMuted
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, part.last ? Math.max(60, root.width - part.x - 30) : 220)
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: crumb.down ? Theme.bgPressed : crumb.hovered && part.modelData.index < 0 ? Theme.bgHover : "transparent"
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
            model: root.collapsed ? root.items.slice(1, root.items.length - root.visibleTail) : []
            delegate: BMenuItem {
                required property var modelData
                required property int index
                text: modelData.title
                iconName: "folder"
                onTriggered: root.activated(index + 1)
            }
            onObjectAdded: (index, object) => overflowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => overflowMenu.removeItem(object)
        }
    }
}
