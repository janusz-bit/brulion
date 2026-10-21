pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ZoomBreadcrumb — shown above the blocks while zoomed into a block: the page, then the zoomed
// block's ancestors, then the block itself. Earlier parts zoom out to that level (the page:
// whole page); the button on the left goes one level up.
Item {
    id: root

    property PageController controller
    readonly property var crumbs: controller ? controller.breadcrumb : []

    implicitHeight: row.implicitHeight + Theme.space2

    RowLayout {
        id: row
        width: parent.width
        spacing: Theme.space1

        BIconButton {
            size: "small"
            iconName: "arrow-left"
            label: qsTr("Zoom out")
            onClicked: root.controller.zoomOut()
        }
        BBreadcrumb {
            Layout.fillWidth: true
            rootIcon: "file-text"
            model: {
                const out = [{ title: root.controller ? root.controller.title : "" }]
                for (let i = 0; i < root.crumbs.length; ++i)
                    out.push({ title: root.crumbs[i].title !== "" ? root.crumbs[i].title : "…" })
                return out
            }
            onActivated: (index) => {
                if (index === 0)
                    root.controller.zoomTo(-1)
                else if (index - 1 < root.crumbs.length)
                    root.controller.zoomTo(root.crumbs[index - 1].row)
            }
        }
    }
}
