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
    readonly property list<var> crumbs: controller ? controller.breadcrumb : []
    /// BBreadcrumb's entries without a page.
    readonly property list<var> noPage: [{ title: "" }]

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
            model: root.controller ? root.controller.breadcrumbItems(root.controller.title, root.crumbs) : root.noPage
            onActivated: (index) => {
                if (index === 0)
                    root.controller.zoomTo(-1)
                else if (index - 1 < root.crumbs.length)
                    root.controller.zoomTo(root.controller.breadcrumbRow(index - 1))
            }
        }
    }
}
