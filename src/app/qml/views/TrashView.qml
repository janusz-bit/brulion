import QtQuick
import Brulion

// PLACEHOLDER — replaced by the views agent
//
// TrashView — route "trash": deleted files, restore / delete permanently.
// Contract with the shell (qml/shell/ShellRouteView.qml): the view fills its area and gets
//   property var route            the route it shows (QVariantMap, see src/app/shell/navigation.h)
// and may offer
//   property string title         the top bar title (default: derived from the route)
//   property Component sidePanel  content for the side panel (backlinks, outline, mini calendar)
Item {
    id: root

    property var route: ({})

    BEmptyState {
        anchors.centerIn: parent
        iconName: "trash-2"
        title: qsTr("Trash")
        description: qsTr("This view is coming soon.")
    }
}
