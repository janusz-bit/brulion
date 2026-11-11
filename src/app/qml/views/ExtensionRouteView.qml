pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ExtensionRouteView — route "extension" ({extension, name, title?, …params}, docs/UI.md
// "Extensions"): one view of an extension written in the notebook (b.view with a label: the
// palette offers it), drawn by ShellExtensionView like the gym's; the route's other keys are the
// view's params. Its title is the view's label (also kept in the route for tabs and the history).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string extensionId: AppController.routeText(root.route, "extension")
    readonly property string viewName: AppController.routeText(root.route, "name")
    readonly property string title: Workspace.extensions.revision >= 0
                                    ? Workspace.extensions.viewTitle(root.extensionId, root.viewName) : ""
    readonly property int maximumWidth: 720

    /// A page of a link: opened, or made when it does not exist yet.
    function openPage(name: string): void {
        let path = Workspace.resolveLink(name, "")
        if (path === "")
            path = Workspace.createPage(name)
        if (path !== "")
            AppController.openPage(path)
    }

    onRouteChanged: lines.setRouteParams(root.route)
    Component.onCompleted: {
        lines.setRouteParams(root.route)
        list.forceActiveFocus()
    }
    focus: true

    ExtensionView {
        id: lines
        objectName: "extensionRouteModel"
        extensions: Workspace.extensions
        extension: root.extensionId
        view: root.viewName
        onPageRequested: (page) => root.openPage(page)
        onViewRequested: (view, params) => AppController.navigate(lines.routeFor(view, params))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ShellExtensionView {
        id: list
        objectName: "extensionRouteLines"
        anchors.fill: parent
        focus: true
        lines: lines
        maximumWidth: root.maximumWidth
        Accessible.name: root.title

        header: Item {
            width: list.width
            height: headerColumn.y + headerColumn.height + Theme.space3

            Column {
                id: headerColumn
                x: list.inset
                y: Theme.narrow ? Theme.space3 : Theme.space5
                width: list.lineWidth
                spacing: Theme.space2

                BText {
                    width: parent.width
                    text: root.title
                    variant: "heading"
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                BText {
                    width: parent.width
                    variant: "muted"
                    elide: Text.ElideRight
                    text: Workspace.extensions.revision >= 0 ? qsTr("Extension: %1").arg(Workspace.extensions.title(root.extensionId)) : ""
                }
                BText {
                    visible: lines.error !== ""
                    width: parent.width
                    text: qsTr("The view could not be drawn: %1").arg(lines.error)
                    color: Theme.danger
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    BEmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 440)
        visible: Workspace.extensions.revision >= 0 && list.count === 0 && lines.error === ""
        iconName: "puzzle"
        title: qsTr("Nothing to show")
        description: qsTr("This view of the extension is not loaded: its page may wait for approval or have an error (Settings → Extensions).")
    }
}
