pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BToastHost — shows BToasts stacked at the bottom of the window, auto-dismisses them and
// announces them politely to screen readers.
//
// Put one in the main window, above everything (also above dialogs):
//   BToastHost { id: toasts; parent: T.Overlay.overlay; anchors.fill: parent; z: 1000 }
// and call:
//   toasts.show(qsTr("Zsynchronizowano 12 plików"), { kind: "success" })
//   toasts.show(qsTr("Przeniesiono do kosza"), { actionText: qsTr("Cofnij"), onAction: undo })
//
// show(text, options) -> id. options: kind ("info" | "success" | "warning" | "error"),
// actionText, onAction (function), timeout (ms; default 4 s, 7 s with an action, 0 = until
// closed). Hovering a toast pauses its timer. dismiss(id), clear(). toastShown(id, text, kind)
// tells about each one shown (the remote control's state.lastToasts).
Item {
    id: host

    property int maxVisible: 3
    property int defaultTimeout: 4000
    property real bottomMargin: Theme.space4
    property var _actions: ({})
    property int _nextId: 1

    signal toastShown(int toastId, string text, string kind)

    function show(text, options) {
        const o = options || {}
        const id = _nextId++
        const hasAction = !!o.actionText
        if (typeof o.onAction === "function")
            _actions[id] = o.onAction
        toastModel.insert(0, {
            toastId: id,
            text: String(text),
            kind: o.kind || "info",
            actionText: o.actionText || "",
            timeout: o.timeout !== undefined ? o.timeout : (hasAction ? 7000 : defaultTimeout)
        })
        while (toastModel.count > maxVisible)
            _remove(toastModel.count - 1)
        _announce(text)
        toastShown(id, String(text), o.kind || "info")
        return id
    }

    function dismiss(id) {
        for (let i = 0; i < toastModel.count; ++i) {
            if (toastModel.get(i).toastId === id) {
                _remove(i)
                return
            }
        }
    }

    function clear() {
        while (toastModel.count > 0)
            _remove(0)
    }

    function _remove(index) {
        const id = toastModel.get(index).toastId
        delete _actions[id]
        toastModel.remove(index)
    }

    function _announce(text) {
        // Qt 6.8+: polite live-region style announcement.
        if (typeof host.Accessible.announce === "function")
            host.Accessible.announce(String(text))
    }

    ListModel {
        id: toastModel
    }

    ListView {
        id: list
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: host.bottomMargin
        width: Theme.compact ? parent.width - 2 * Theme.space3 : Math.min(480, parent.width - 2 * Theme.space4)
        height: Math.min(contentHeight, parent.height - host.bottomMargin)
        interactive: false
        spacing: Theme.space2
        verticalLayoutDirection: ListView.BottomToTop
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
                onActionTriggered: {
                    const fn = host._actions[wrapper.toastId]
                    host.dismiss(wrapper.toastId)
                    if (fn)
                        fn()
                }
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
