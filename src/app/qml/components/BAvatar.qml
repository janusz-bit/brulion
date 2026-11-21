pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BAvatar — a notebook's (or anything's) round mark: its first letter on the accent's solid fill,
// like a server's icon in Discord or an account's picture. `active` (hovered, pressed, open)
// turns the circle into a rounded square — the same answer to the pointer as Discord's icons.
//
//   BAvatar { name: Workspace.name; size: 32; active: button.hovered }
//
// The letter is plain text (a notebook's name comes from its folder: anything).
Rectangle {
    id: avatar

    /// The name whose first letter it shows ("" → "B").
    property string name
    property int size: 32
    property bool active: false

    implicitWidth: size
    implicitHeight: size
    radius: active ? Math.round(size * 0.3) : size / 2
    antialiasing: true
    color: Theme.accentSolid
    Behavior on radius { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }

    BText {
        anchors.centerIn: parent
        text: Js.toUpperCase(Js.charAt(Js.trim(avatar.name !== "" ? avatar.name : "B"), 0))
        color: Theme.fgOnAccent
        font.weight: Theme.weightBold
        font.pixelSize: Math.round(avatar.size * 0.48)
        Accessible.ignored: true
    }
}
