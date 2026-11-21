pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// BMenu — popup menu (context menus, "more" buttons, overflow).
//
//   BIconButton { id: more; iconName: "ellipsis"; label: qsTr("More"); onClicked: menu.popup(more, 0, more.height) }
//   BMenu {
//       id: menu
//       BMenuItem { text: qsTr("Rename"); iconName: "pencil"; shortcut: "F2" }
//       BMenuSeparator {}
//       BMenuItem { text: qsTr("Move to trash"); iconName: "trash-2"; danger: true }
//   }
//
// Keyboard (every BMenuItem shown takes part, so no menu needs key handlers of its own): a menu
// opens with no item highlighted, Down starts at the first item shown, Down / Up move to the next /
// previous one (hidden and disabled ones skipped; the ends do not wrap — Qt's T.Menu), Enter,
// Return and Space trigger it, Right opens its submenu, Left / Escape close. Tab moves on too and
// also reaches what is no BMenuItem (the More menu's page tiles and Mode switch: not the arrows).
// Closing gives the focus back to what had it (the edited text keeps its selection: the menu took
// the focus as a popup). A menu that knows where to start sets `currentIndex` after popup() (the
// task status menu: the task's status). Use `popup()` (at the pointer or
// at an item) — never blocking; react in each item's triggered(). Menus stay above the on-screen
// keyboard (bottomMargin; they flip above their item when there is no room below) and below the
// system's status bar over an edge-to-edge window (topMargin, the window's safe area: a finger's
// tap there goes to the status bar — a menu taller than the room left scrolls instead).
// A submenu is a BMenu inside the menu (its `title` and `iconName` make its item); it opens beside
// the menu with a mouse, in place of it on touch UIs (one menu at a time on a phone's width).
//   BMenu { BMenu { title: qsTr("Copy link"); iconName: "link"; BMenuItem { … } } }
// `touched`: something in the open menu was pressed or scrolled — whoever moves a menu out of the
// way of something else (the editor's, when the keyboard goes) leaves it where it is from then on.
// `sheet`: on touch UIs the menu is a sheet from the bottom edge instead of a popup at its item —
// the window's width, rounded top corners with a grabber, the page dimmed behind it — as the
// "…" menus of Instagram and FluffyChat; for the long menus of a page (the top bar's ⋮, the page's
// ⋯). A tap outside or Back closes it. With a mouse it is an ordinary menu.
T.Menu {
    id: control

    /// The icon of this menu's item in its parent menu (a submenu).
    property string iconName
    /// An item was pressed or the list scrolled since the menu opened.
    property bool touched: false
    /// A sheet from the bottom edge on touch UIs (see above).
    property bool sheet: false
    readonly property bool asSheet: sheet && Theme.touchUi
    /// The window's bottom edge the sheet sits on: above the on-screen keyboard and the
    /// navigation bar's inset.
    readonly property real sheetBottomInset: T.Overlay.overlay && T.Overlay.overlay.SafeArea
                                             ? T.Overlay.overlay.SafeArea.margins.bottom : 0
    onAboutToShow: {
        touched = false
        if (control.asSheet)
            control.placeAsSheet()
    }
    // (Its items are laid out after it opens: placed again as they grow.)
    onImplicitHeightChanged: if (control.asSheet && control.visible) control.placeAsSheet()
    /// The sheet's place: the overlay's full width at its bottom (in the parent's coordinates,
    /// which popup() set).
    function placeAsSheet(): void {
        const overlay = T.Overlay.overlay
        const parentItem = control.parent
        if (!overlay || !parentItem)
            return
        // (Everything read before the first call: docs/UI.md "Compiled QML", side effects.)
        const w = overlay.width
        const keyboard = KeyboardInsets.overlap
        const h = Math.min(control.implicitHeight, overlay.height - control.safeTop - 48 - keyboard)
        const top = overlay.height - keyboard - h
        control.width = w
        control.height = h
        control._sheetAt = parentItem.mapFromItem(overlay, 0, top)
        control.x = control._sheetAt.x
        control.y = control._sheetAt.y
    }
    /// (placeAsSheet()'s point: a method's QPointF only through a typed property, docs/UI.md
    /// "Compiled QML".)
    property point _sheetAt
    /// What the system's bars cover of the window's top (Android 11+ draws the window under the
    /// status bar).
    readonly property real safeTop: T.Overlay.overlay && T.Overlay.overlay.SafeArea
                                    ? T.Overlay.overlay.SafeArea.margins.top : 0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    margins: asSheet ? 0 : 8
    topMargin: (asSheet ? 0 : 8) + safeTop
    bottomMargin: (asSheet ? 0 : 8) + KeyboardInsets.overlap
    padding: 4
    topPadding: asSheet ? 22 : 4
    bottomPadding: asSheet ? 12 + sheetBottomInset : 4
    leftPadding: asSheet ? 8 : 4
    rightPadding: asSheet ? 8 : 4
    overlap: 4
    focus: true
    modal: Theme.touchUi
    dim: asSheet
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
        onMovementStarted: control.touched = true
        T.ScrollBar.vertical: BScrollBar {}
    }

    background: Item {
        implicitWidth: Theme.touchUi ? 220 : 200
        implicitHeight: 32
        BShadow {
            target: menuBg
            level: control.asSheet ? 3 : 2
        }
        Rectangle {
            id: menuBg
            anchors.fill: parent
            // A sheet: rounded at the top only (its bottom runs past the window's edge).
            anchors.bottomMargin: control.asSheet ? -Theme.radiusSheet : 0
            radius: control.asSheet ? Theme.radiusSheet : Theme.radiusLg
            color: Theme.dark ? Theme.bgElevated2 : Theme.bgElevated
            border.width: control.asSheet ? 0 : 1
            border.color: Theme.border
        }
        // The sheet's grabber.
        Rectangle {
            visible: control.asSheet
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            width: 36
            height: 4
            radius: 2
            color: Theme.fgFaint
            opacity: 0.5
        }
    }

    // A sheet dims the page behind it (Theme.scrim); a popup menu does not.
    T.Overlay.modal: Rectangle {
        color: control.asSheet ? Theme.scrim : "transparent"
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    enter: control.asSheet ? sheetEnter : menuEnter
    Transition {
        id: menuEnter
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationFast; easing.type: Theme.easingEnter }
        }
    }
    // A sheet rises from the bottom edge.
    Transition {
        id: sheetEnter
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
            NumberAnimation { property: "y"; from: control.y + 48; to: control.y; duration: Theme.duration; easing.type: Theme.easingEnter }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant }
    }
}
