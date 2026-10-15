import QtQuick
import Brulion

// PLACEHOLDER — replaced by the views agent
//
// AllPagesView — route "allPages" ({path?: folder}): every page, sortable.
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
        iconName: "files"
        title: qsTr("All pages")
        description: qsTr("This view is coming soon.")
    }
}
