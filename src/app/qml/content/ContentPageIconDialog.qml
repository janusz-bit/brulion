pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentPageIconDialog — the page icon (docs/UI.md "Page icon and cover"): an emoji — typed or
// pasted into the field (any emoji: a phone's emoji keyboard too) or one of the common ones — or
// one of the app's icons; "Usuń ikonę" removes it. A pick is written at once
// (PageController.setPageIcon: the page's `icon` property — Iconize's `LiBookOpen` for an app
// icon —, one undo step, "Cofnij" in the toast) and closes the dialog.
//
//   ContentPageIconDialog { id: iconDialog; controller: page; editor: editor }   iconDialog.open()
BDialog {
    id: dialog

    property PageController controller
    property BlockEditor editor
    readonly property var current: controller && controller.look ? (controller.look.icon || ({})) : ({})
    readonly property bool hasIcon: !!current.kind
    /// What the field holds as an icon ({ kind, name }; kind "" when it is none).
    readonly property var typed: TextHelper.pageIcon(emojiField.text)

    /// Common emoji (all drawn by the app's own emoji font where the system has none: the browser).
    readonly property var emojis: ["📝", "📚", "📖", "📓", "📄", "📁", "📂", "📅", "📆", "🗓", "⏰", "⌛",
        "✅", "✔", "⭐", "🌟", "✨", "💡", "🔥", "❤", "💯", "🎯", "🏆", "🏁", "🚩", "🚀", "✈", "🚗", "🚲",
        "🏠", "🏢", "🏫", "🏥", "🌍", "🌱", "🌳", "🌸", "🌹", "🍀", "☀", "🌙", "⛅", "❄", "⚡", "🌈",
        "🍎", "🍕", "🍰", "☕", "🍷", "🎉", "🎁", "🎂", "🎓", "🎨", "🎵", "🎮", "🎬", "📷", "💻", "📱",
        "🖥", "🔧", "🔨", "🛠", "⚙", "🔑", "🔒", "📦", "📎", "📌", "📍", "🔖", "🏷", "💼", "💰", "💳",
        "📈", "📊", "📋", "🧾", "✉", "📧", "📞", "💬", "💭", "🔔", "📢", "🧠", "💪", "🧘", "🏃", "🩺",
        "🐱", "🐶", "👪", "👶", "😀", "😊", "😎", "🤔", "🙏", "👍", "👋", "🧳", "🛒", "🧹", "🤖", "🔍",
        "🗂", "🔴", "🟠", "🟡", "🟢", "🔵", "🟣"]
    /// The app's icons offered for pages (assets/icons, Lucide).
    readonly property var appIcons: ["file-text", "book-open", "notebook-pen", "bookmark", "house", "folder",
        "calendar", "calendar-days", "clock", "alarm-clock", "hourglass", "bell", "list-todo", "list-checks",
        "inbox", "star", "heart", "sparkles", "pin", "tag", "hash", "quote", "scroll-text", "message-square",
        "music", "image", "palette", "pen-tool", "highlighter", "code", "terminal", "calculator", "sigma",
        "laptop", "smartphone", "monitor", "server", "network", "hard-drive", "cloud", "sun", "moon", "hand",
        "lock", "history", "settings", "sliders-horizontal", "link", "at-sign", "info", "circle-help",
        "triangle-alert", "diamond", "hexagon", "triangle", "circle", "square", "waypoints", "table",
        "git-merge"]

    /// Write `kind` / `name` ("" removes the icon) and close.
    function pick(kind, name) {
        if (!controller)
            return
        const before = controller.undoToken()
        const r = controller.setPageIcon(kind, name)
        if (!r.ok) {
            AppController.toast(r.reason, "error")
            return
        }
        dialog.close()
        if (r.status !== "changed")
            return
        const text = kind === "" ? qsTr("Page icon removed") : qsTr("Page icon set")
        const action = editor ? editor.undoAction(before) : null
        if (action)
            AppController.toastAction(text, "success", action.text, action.run)
        else
            AppController.toast(text, "success")
    }

    title: qsTr("Page icon")
    iconName: "sparkles"
    acceptText: qsTr("Use")
    acceptEnabled: typed.kind !== ""
    initialFocusItem: Theme.touchUi ? null : emojiField
    onAccepted: pick(typed.kind, typed.name)
    onAboutToShow: emojiField.text = ""
    footerLeading: BButton {
        objectName: "pageIconRemove"
        visible: dialog.hasIcon
        variant: "ghost"
        text: qsTr("Remove icon")
        iconName: "trash-2"
        onClicked: dialog.pick("", "")
    }

    component Choice: T.AbstractButton {
        id: choice
        property string emoji
        property string appIcon
        readonly property bool current: dialog.current.kind === (emoji !== "" ? "emoji" : "app")
                                        && dialog.current.name === (emoji !== "" ? emoji : appIcon)
        implicitWidth: Theme.touchUi ? Theme.touchTarget : 36
        implicitHeight: implicitWidth
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.Button
        Accessible.name: emoji !== "" ? emoji : appIcon
        background: Rectangle {
            radius: Theme.radius
            color: choice.down ? Theme.bgPressed : choice.current ? Theme.bgSelected
                 : choice.hovered || choice.visualFocus ? Theme.bgHover : "transparent"
            border.width: choice.visualFocus ? 2 : 0
            border.color: Theme.focusRing
        }
        contentItem: BPageIcon {
            icon: choice.emoji !== "" ? { kind: "emoji", name: choice.emoji } : { kind: "app", name: choice.appIcon }
            size: Theme.iconSizeLg
            color: Theme.fg
        }
    }

    BTextField {
        id: emojiField
        objectName: "pageIconField"
        Layout.fillWidth: true
        placeholderText: qsTr("Type or paste an emoji")
        accessibleName: qsTr("Emoji")
        errorText: text.trim() !== "" && dialog.typed.kind === "" ? qsTr("Not an emoji") : ""
        onAccepted: if (dialog.typed.kind !== "") dialog.pick(dialog.typed.kind, dialog.typed.name)
    }
    BSectionHeader {
        Layout.fillWidth: true
        text: qsTr("Emoji")
    }
    Flow {
        objectName: "pageIconEmojis"
        Layout.fillWidth: true
        spacing: 2
        Repeater {
            model: dialog.emojis
            delegate: Choice {
                required property string modelData
                emoji: modelData
                onClicked: dialog.pick("emoji", modelData)
            }
        }
    }
    BSectionHeader {
        Layout.fillWidth: true
        text: qsTr("Icons")
    }
    Flow {
        objectName: "pageIconIcons"
        Layout.fillWidth: true
        spacing: 2
        Repeater {
            model: dialog.appIcons
            delegate: Choice {
                required property string modelData
                appIcon: modelData
                onClicked: dialog.pick("app", modelData)
            }
        }
    }
}
