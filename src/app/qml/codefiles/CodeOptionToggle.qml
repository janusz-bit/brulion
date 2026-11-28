import QtQuick
import QtQuick.Templates as T
import Brulion

// CodeOptionToggle — a search option's toggle ("Aa", "ab", ".*") of the project search and the find in
// a file (CodeSearchPanel, CodeFileFindBar): its letters in the mono font, a tooltip, the link colour
// and a frame while on.
T.AbstractButton {
    id: toggle
    required property string label
    required property string tip
    checkable: true
    focusPolicy: Qt.NoFocus
    implicitWidth: Math.max(28, toggleText.implicitWidth + 12)
    implicitHeight: 28
    hoverEnabled: !Theme.touchUi
    Accessible.name: toggle.tip
    BToolTip {
        visible: toggle.hovered
        text: toggle.tip
    }
    background: Rectangle {
        radius: Theme.radiusSm
        color: toggle.checked ? Theme.bgSelected : toggle.hovered ? Theme.bgHover : "transparent"
        border.width: toggle.checked ? 1 : 0
        border.color: Theme.link
    }
    contentItem: Text {
        id: toggleText
        text: toggle.label
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        font.weight: Theme.weightSemiBold
        color: toggle.checked ? Theme.link : Theme.fgMuted
    }
}
