pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeSetupBanner — "Setting up the worktree…" over a new thread (docs/CODE.md "Setup scripts"):
// while the project's scripts that run when a worktree is made run in the thread's terminal and
// the first message waits for them (T3 Code's `async: false`); when one failed, what failed and the
// user's choice — send the message anyway, or not (it stays in the field; the worktree stays).
Rectangle {
    id: banner

    /// "running" or "failed".
    property string phase: "running"
    /// The scripts waited for ("Setup, Build") / what failed ("Setup: exit code 1").
    property string detail

    signal sendAnyway()
    signal dontSend()

    objectName: "codeSetup"
    implicitHeight: row.implicitHeight + 2 * Theme.space2
    color: banner.phase === "failed" ? Theme.dangerBg : Theme.bgSunken
    Accessible.role: Accessible.StatusBar
    Accessible.name: label.text

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.space4
        anchors.rightMargin: Theme.space3
        spacing: Theme.space2
        BSpinner {
            visible: banner.phase === "running"
            size: Theme.iconSizeSm
        }
        BIcon {
            visible: banner.phase === "failed"
            name: "circle-alert"
            size: Theme.iconSizeSm
            color: Theme.danger
        }
        BText {
            id: label
            objectName: "codeSetupText"
            Layout.fillWidth: true
            text: banner.phase === "failed"
                  ? qsTr("The worktree's setup failed (%1). Send the message to the agent anyway?").arg(banner.detail)
                  : qsTr("Setting up the worktree… (%1) — the message goes once it is done.").arg(banner.detail)
            variant: "small"
            color: banner.phase === "failed" ? Theme.dangerText : Theme.fgMuted
            wrapMode: Text.Wrap
        }
        BButton {
            objectName: "codeSetupSend"
            size: "small"
            variant: banner.phase === "failed" ? "primary" : "ghost"
            text: qsTr("Send anyway")
            onClicked: banner.sendAnyway()
        }
        BButton {
            objectName: "codeSetupCancel"
            visible: banner.phase === "failed"
            size: "small"
            variant: "ghost"
            text: qsTr("Don't send")
            onClicked: banner.dontSend()
        }
    }
}
