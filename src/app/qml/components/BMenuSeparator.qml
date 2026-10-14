pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BMenuSeparator — thin line between groups of BMenuItems.
T.MenuSeparator {
    id: control
    implicitWidth: 180
    implicitHeight: 9
    padding: 0
    verticalPadding: 4
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.border
    }
}
