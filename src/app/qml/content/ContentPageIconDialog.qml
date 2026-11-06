pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ContentPageIconDialog — the page icon (docs/UI.md "Page icon and cover"): an emoji — typed or
// pasted into the field (any emoji: a phone's emoji keyboard too) or one of the common ones — or
// one of the app's icons; "Remove icon" removes it. A pick is written at once
// (PageController.setPageIcon: the page's `icon` property — Iconize's `LiBookOpen` for an app
// icon —, one undo step, "Undo" in the toast) and closes the dialog.
//
//   ContentPageIconDialog { id: iconDialog; controller: page; editor: editor }   iconDialog.open()
BDialog {
    id: dialog

    property PageController controller
    property BlockEditor editor
    /// A map a call returned, read with AppController.route*() (compiled calls cannot take a map
    /// held in a local after another call: kept here).
    property var _held: ({})
    /// The page's icon now (PageController.look.icon: kind "emoji" | "app" | "", name).
    readonly property string currentKind: dialog.controller ? AppController.routeText(dialog.controller.look.icon, "kind") : ""
    readonly property string currentName: dialog.controller ? AppController.routeText(dialog.controller.look.icon, "name") : ""
    readonly property bool hasIcon: currentKind !== ""
    /// What the field holds as an icon ({ kind, name }; kind "" when it is none).
    readonly property var typed: TextHelper.pageIcon(emojiField.text)
    readonly property string typedKind: AppController.routeText(dialog.typed, "kind")
    readonly property string typedName: AppController.routeText(dialog.typed, "name")

    /// Common emoji (all drawn by the app's own emoji font where the system has none: the browser).
    readonly property list<string> emojis: ["📝", "📚", "📖", "📓", "📄", "📁", "📂", "📅", "📆", "🗓", "⏰", "⌛",
        "✅", "✔", "⭐", "🌟", "✨", "💡", "🔥", "❤", "💯", "🎯", "🏆", "🏁", "🚩", "🚀", "✈", "🚗", "🚲",
        "🏠", "🏢", "🏫", "🏥", "🌍", "🌱", "🌳", "🌸", "🌹", "🍀", "☀", "🌙", "⛅", "❄", "⚡", "🌈",
        "🍎", "🍕", "🍰", "☕", "🍷", "🎉", "🎁", "🎂", "🎓", "🎨", "🎵", "🎮", "🎬", "📷", "💻", "📱",
        "🖥", "🔧", "🔨", "🛠", "⚙", "🔑", "🔒", "📦", "📎", "📌", "📍", "🔖", "🏷", "💼", "💰", "💳",
        "📈", "📊", "📋", "🧾", "✉", "📧", "📞", "💬", "💭", "🔔", "📢", "🧠", "💪", "🧘", "🏃", "🩺",
        "🐱", "🐶", "👪", "👶", "😀", "😊", "😎", "🤔", "🙏", "👍", "👋", "🧳", "🛒", "🧹", "🤖", "🔍",
        "🗂", "🔴", "🟠", "🟡", "🟢", "🔵", "🟣"]
    /// The app's icons offered for pages (assets/icons, Lucide).
    readonly property list<string> appIcons: ["file-text", "book-open", "notebook-pen", "bookmark", "house", "folder",
        "calendar", "calendar-days", "clock", "alarm-clock", "hourglass", "bell", "list-todo", "list-checks",
        "inbox", "star", "heart", "sparkles", "pin", "tag", "hash", "quote", "scroll-text", "message-square",
        "music", "image", "palette", "pen-tool", "highlighter", "code", "terminal", "calculator", "sigma",
        "laptop", "smartphone", "monitor", "server", "network", "hard-drive", "cloud", "sun", "moon", "hand",
        "lock", "history", "settings", "sliders-horizontal", "link", "at-sign", "info", "circle-help",
        "triangle-alert", "diamond", "hexagon", "triangle", "circle", "square", "waypoints", "table",
        "git-merge"]

    /// Write `kind` / `name` ("" removes the icon) and close.
    function pick(kind: string, name: string): void {
        if (!dialog.controller)
            return
        // (Strings from calls laundered: a call cannot take one a call before may change.)
        const before = "" + dialog.controller.undoToken()
        dialog._held = dialog.controller.setPageIcon(kind, name)
        if (!AppController.routeFlag(dialog._held, "ok")) {
            AppController.toast(AppController.routeText(dialog._held, "reason"), "error")
            return
        }
        dialog.close()
        if (AppController.routeText(dialog._held, "status") !== "changed")
            return
        const text = kind === "" ? qsTr("Page icon removed") : qsTr("Page icon set")
        if (dialog.editor)
            dialog.editor.noticeUndoable(text, "success", before)
        else
            AppController.toast(text, "success")
    }

    title: qsTr("Page icon")
    iconName: "sparkles"
    acceptText: qsTr("Use")
    acceptEnabled: typedKind !== ""
    initialFocusItem: Theme.touchUi ? null : emojiField
    onAccepted: dialog.pick(dialog.typedKind, dialog.typedName)
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
        readonly property bool current: dialog.currentKind === (emoji !== "" ? "emoji" : "app")
                                        && dialog.currentName === (emoji !== "" ? emoji : appIcon)
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
        errorText: Js.trim(text) !== "" && dialog.typedKind === "" ? qsTr("Not an emoji") : ""
        onAccepted: if (dialog.typedKind !== "") dialog.pick(dialog.typedKind, dialog.typedName)
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
