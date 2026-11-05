pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellUpdateBanner — a newer Brulion is released (AppController.updates): "Zobacz na GitHubie"
// opens the release's page, the cross dismisses this version. Under the top bar, in every view.
BCard {
    id: banner

    readonly property AppUpdates updates: AppController.updates

    objectName: "shellUpdateBanner"
    padding: Theme.space2
    accessibleName: qsTr("New version")

    contentItem: RowLayout {
        spacing: Theme.space2

        BIcon { name: "download"; size: Theme.iconSize; color: Theme.accent }
        BText {
            Layout.fillWidth: true
            text: qsTr("Brulion %1 is available").arg(banner.updates.availableVersion)
            wrapMode: Text.Wrap
        }
        BButton {
            objectName: "shellUpdateOpen"
            size: "small"
            iconName: "external-link"
            text: qsTr("View on GitHub")
            onClicked: Workspace.openUrl(banner.updates.releaseUrl)
        }
        BIconButton {
            objectName: "shellUpdateDismiss"
            size: "small"
            iconName: "x"
            label: qsTr("Dismiss")
            onClicked: banner.updates.dismiss()
        }
    }
}
