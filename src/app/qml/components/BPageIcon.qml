pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BPageIcon — a page's icon (docs/UI.md "Page icon and cover"): its emoji, or one of the app's
// icons; `fallback` (an icon name, "" for none) when the page has neither. Square, `size` wide.
//
//   BPageIcon { icon: Workspace.pageIconOf(path); fallback: "file-text"; size: Theme.iconSizeSm }
//
// icon: { kind: "emoji" | "app" | "", name } (Workspace.pageIconOf, PageController.look.icon).
// The emoji is plain text, made only for a page that has one.
Item {
    id: root

    property var icon: ({})
    property string fallback
    property real size: Theme.iconSize
    property color color: Theme.fgFaint

    /// icon.kind and icon.name ("" when missing).
    readonly property string _kind: UiModels.stringField(icon, "kind")
    readonly property string _name: UiModels.stringField(icon, "name")
    readonly property bool emoji: _kind === "emoji" && _name !== ""
    readonly property string iconName: emoji ? "" : _kind === "app" && _name !== "" ? _name : fallback

    width: emoji || iconName !== "" ? size : 0
    height: size
    implicitWidth: width
    implicitHeight: height
    Accessible.ignored: true

    BIcon {
        anchors.centerIn: parent
        name: root.iconName
        size: root.size
        color: root.color
    }
    Loader {
        anchors.centerIn: parent
        active: root.emoji
        sourceComponent: Text {
            text: root._name
            textFormat: Text.PlainText
            font.pixelSize: Math.round(root.size * 0.85)
            color: Theme.fg
        }
    }
}
