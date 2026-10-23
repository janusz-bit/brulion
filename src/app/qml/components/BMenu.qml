pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BMenu — popup menu (context menus, "more" buttons, overflow).
//
//   BIconButton { id: more; iconName: "ellipsis"; label: qsTr("Więcej"); onClicked: menu.popup(more, 0, more.height) }
//   BMenu {
//       id: menu
//       BMenuItem { text: qsTr("Zmień nazwę"); iconName: "pencil"; shortcut: "F2" }
//       BMenuSeparator {}
//       BMenuItem { text: qsTr("Przenieś do kosza"); iconName: "trash-2"; danger: true }
//   }
//
// Keyboard: Up/Down (wrapping), Enter/Space activate, Right opens a submenu, Left/Escape
// close. Opening by keyboard highlights the first item. Use `popup()` (at the pointer or
// at an item) — never blocking; react in each item's triggered(). Menus stay above the on-screen
// keyboard (bottomMargin; they flip above their item when there is no room below).
// A submenu is a BMenu inside the menu (its `title` and `iconName` make its item); it opens beside
// the menu with a mouse, in place of it on touch UIs (one menu at a time on a phone's width).
//   BMenu { BMenu { title: qsTr("Kopiuj odnośnik"); iconName: "link"; BMenuItem { … } } }
T.Menu {
    id: control

    /// The icon of this menu's item in its parent menu (a submenu).
    property string iconName

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    margins: 8
    bottomMargin: 8 + KeyboardInsets.overlap
    padding: 4
    overlap: 4
    focus: true
    modal: Theme.touchUi
    dim: false
    cascade: !Theme.touchUi

    delegate: BMenuItem {}

    contentItem: ListView {
        // Wide enough for the widest item (ListView itself has no implicit width).
        implicitWidth: {
            let w = 0
            for (let i = 0; i < control.count; ++i) {
                const item = control.itemAt(i)
                if (item)
                    w = Math.max(w, item.implicitWidth)
            }
            return Math.min(Math.ceil(w) + 4, 420)
        }
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        keyNavigationWraps: true
        T.ScrollBar.vertical: BScrollBar {}
    }

    background: Item {
        implicitWidth: Theme.touchUi ? 220 : 200
        implicitHeight: 32
        BShadow {
            target: menuBg
            level: 2
        }
        Rectangle {
            id: menuBg
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Theme.dark ? Theme.bgElevated2 : Theme.bgElevated
            border.width: 1
            border.color: Theme.border
        }
    }

    T.Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.0)
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationFast; easing.type: Theme.easingEnter }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant }
    }
}
