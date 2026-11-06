pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BToastHost — shows BToasts stacked at the bottom of the window (`atTop`: at its top, the newest
// one only — while typing on a touch screen, where the bottom is the line being written),
// auto-dismisses them and announces them politely to screen readers.
//
// Put one in the main window, above everything (also above dialogs):
//   BToastHost { id: toasts; parent: T.Overlay.overlay; anchors.fill: parent; z: 1000 }
// and call:
//   toasts.showToast(qsTr("Synced 12 files"), "success", "", -1)
//   undoToast = toasts.showToast(qsTr("Moved to trash"), "info", qsTr("Undo"), -1)
//   Connections { target: toasts; function onActionTriggered(toastId: int) { if (toastId === undoToast) undo() } }
//
// showToast(text, kind, actionText, timeout) -> id. kind: "info" (also "") | "success" | "warning" |
// "error"; actionText: the button ("" for none; pressing it dismisses the toast and emits
// actionTriggered(id)); timeout: ms, < 0 the default (4 s, 7 s with an action), 0 = until closed.
// Hovering a toast pauses its timer. dismiss(id), clear(). toastShown(id, text, kind) tells about
// each one shown (the remote control's state.lastToasts).
Item {
    id: host

    property int maxVisible: 3
    property int defaultTimeout: 4000
    property real bottomMargin: Theme.space4
    /// At the window's top (below `topMargin`), one toast at a time: nothing is covered but the
    /// top bar.
    property bool atTop: false
    property real topMargin: Theme.space2
    property int _nextId: 1

    signal toastShown(int toastId, string text, string kind)
    /// The action button of toast `toastId` was pressed (the toast is gone from the screen already).
    signal actionTriggered(int toastId)
    /// Toast `toastId` is gone (dismissed, timed out, pushed out by newer ones, its button
    /// pressed: after actionTriggered) — the last signal about it.
    signal toastRemoved(int toastId)

    function showToast(text: string, kind: string, actionText: string, timeout: int): int {
        const id = _nextId++
        const k = kind !== "" ? kind : "info"
        toastModel.prepend(id, text, k, actionText, timeout >= 0 ? timeout : (actionText !== "" ? 7000 : defaultTimeout))
        while (toastModel.count > (atTop ? 1 : maxVisible))
            _remove(toastModel.count - 1)
        // Qt 6.8+: polite live-region style announcement.
        host.Accessible.announce(text, Accessible.Polite)
        toastShown(id, text, k)
        return id
    }

    function dismiss(toastId: int): void {
        const i = toastModel.indexOf(toastId)
        if (i >= 0)
            _remove(i)
    }

    function clear(): void {
        while (toastModel.count > 0)
            _remove(0)
    }

    function _remove(index: int): void {
        const id = toastModel.idAt(index)
        toastModel.removeAt(index)
        toastRemoved(id)
    }

    /// Toast `toastId`'s button was pressed: it goes, then actionTriggered, then toastRemoved.
    function _trigger(toastId: int): void {
        const i = toastModel.indexOf(toastId)
        if (i < 0)
            return
        toastModel.removeAt(i)
        actionTriggered(toastId)
        toastRemoved(toastId)
    }

    ToastListModel {
        id: toastModel
    }

    // (The older toasts go when they move to the top.)
    onAtTopChanged: {
        while (host.atTop && toastModel.count > 1)
            host._remove(toastModel.count - 1)
    }

    ListView {
        id: list
        anchors.horizontalCenter: parent.horizontalCenter
        y: host.atTop ? host.topMargin : parent.height - host.bottomMargin - height
        width: Theme.compact ? parent.width - 2 * Theme.space3 : Math.min(480, parent.width - 2 * Theme.space4)
        height: Math.min(contentHeight, parent.height - (host.atTop ? host.topMargin : host.bottomMargin))
        interactive: false
        // No current item: a view gives its current item the focus, and the first toast shown
        // (none before it) took the keyboard from whatever had it — the block being edited when
        // "Merged changes made elsewhere" arrived while typing, the keys after it went nowhere.
        currentIndex: -1
        keyNavigationEnabled: false
        spacing: Theme.space2
        verticalLayoutDirection: host.atTop ? ListView.TopToBottom : ListView.BottomToTop
        model: toastModel

        delegate: Item {
            id: wrapper
            required property int index
            required property int toastId
            required property string text
            required property string kind
            required property string actionText
            required property int timeout

            width: list.width
            height: toast.implicitHeight

            BToast {
                id: toast
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.compact ? parent.width : Math.min(implicitWidth, parent.width)
                text: wrapper.text
                kind: wrapper.kind
                actionText: wrapper.actionText
                onActionTriggered: host._trigger(wrapper.toastId)
                onCloseClicked: host.dismiss(wrapper.toastId)
            }

            Timer {
                interval: Math.max(1, wrapper.timeout)
                running: wrapper.timeout > 0 && !toast.hovered && !toast.activeFocus
                onTriggered: host.dismiss(wrapper.toastId)
            }
        }

        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.duration; easing.type: Theme.easing }
                NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Theme.duration; easing.type: Theme.easingEnter }
            }
        }
        remove: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
        displaced: Transition {
            NumberAnimation { properties: "x,y"; duration: Theme.duration; easing.type: Theme.easing }
        }
    }
}
