pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ChatAgentLimits — the subscription limits of an AI agent of this computer that is Claude Code or
// Codex (docs/UI.md "AI agents (ACP)", "Its limits"; Workspace.chats.limits, shell/agentlimits.h):
// a pill with the five hours and the week, "5 h: 12 % · week: 68 %", tinted with the warning colour
// above 80 % and the danger colour at 100 % (any window, a model's own week too); on hover each
// window with its reset in local time, the plan and when it was read; a click opens the menu of
// windows and Refresh. A sign-in that cannot be used says so in a few words ("Limits: sign in
// again"), what to do in its tooltip. Read when shown — at most every five minutes per account —,
// again every five minutes while it stays shown. Hidden for any other partner (and where no
// programs run). Beside the session's usage in ChatAgentOptions, and on the agent's row in
// Settings → Extensions → AI agents. The tooltip ends with what the agent used today, from its own
// session logs (Workspace.chats.usage, shell/agentusage.h: "Used today: 1.2 M tokens · ≈ 4.10 USD at
// API prices"), read when the pill is.
T.AbstractButton {
    id: chip

    /// The agent ("acp:<id>").
    property string partner

    readonly property AgentLimits limits: Workspace.chats.limits
    readonly property var info: chip.limits.state(chip.partner, chip.limits.serial)
    readonly property bool applies: AppController.routeFlag(chip.info, "applies")
    readonly property string summary: AppController.routeText(chip.info, "summary")
    readonly property string failure: AppController.routeText(chip.info, "failure")
    readonly property string failureShort: AppController.routeText(chip.info, "failureShort")
    readonly property string level: AppController.routeText(chip.info, "level")
    readonly property string limitsTooltip: AppController.routeText(chip.info, "tooltip")
    readonly property AgentUsage usage: Workspace.chats.usage
    readonly property string usedToday: AppController.routeText(chip.usage.state(chip.partner, chip.usage.serial), "today")
    readonly property string tooltip: chip.usedToday === "" ? chip.limitsTooltip
                                    : chip.limitsTooltip === "" ? chip.usedToday : chip.limitsTooltip + "\n" + chip.usedToday
    readonly property bool busy: AppController.routeFlag(chip.info, "busy")
    readonly property list<var> windows: AppController.routeList(chip.info, "windows")
    readonly property color fgColor: chip.level === "danger" ? Theme.dangerText
                                   : chip.level === "warning" ? Theme.warningText
                                   : chip.failure !== "" ? Theme.fgMuted : Theme.fg
    readonly property int visualHeight: Theme.touchUi ? 24 : 20

    function look(): void {
        if (chip.visible && chip.partner !== "") {
            chip.limits.show(chip.partner)
            chip.usage.show(chip.partner)
        }
    }
    // (After the bindings that showed it: a reading changes `serial`, which they read.)
    Later {
        id: lookLater
        onTriggered: chip.look()
    }
    onVisibleChanged: lookLater.restart()
    onPartnerChanged: lookLater.restart()
    Component.onCompleted: lookLater.restart()
    // (Shown for long: read again when the five minutes are over.)
    Timer {
        interval: 5 * 60 * 1000 + 1000
        repeat: true
        running: chip.visible
        onTriggered: chip.look()
    }

    objectName: "chatAgentLimits"
    visible: chip.applies && chip.partner !== ""
    text: chip.summary !== "" ? chip.summary : chip.failureShort !== "" ? chip.failureShort : qsTr("Limits…")
    implicitWidth: chipRow.implicitWidth + 16
    implicitHeight: Math.max(chip.visualHeight, Theme.touchUi ? Theme.touchTarget : 0)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: !Theme.touchUi
    font.family: Theme.fontUi
    font.pixelSize: Theme.textXs
    font.weight: Theme.weightMedium
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Subscription limits: %1").arg(chip.text)
    Accessible.description: chip.tooltip
    onClicked: menu.popup(chip, 0, chip.height)

    contentItem: Item {}
    background: Rectangle {
        id: pill
        y: (chip.height - chip.visualHeight) / 2
        width: chip.width
        height: chip.visualHeight
        radius: height / 2
        color: chip.level === "danger" ? Theme.dangerBg
             : chip.level === "warning" ? Theme.warningBg
             : chip.down ? Theme.bgPressed : chip.hovered ? Theme.bgHover : Theme.bgElevated
        border.width: chip.level === "" ? 1 : 0
        border.color: chip.hovered ? Theme.borderControl : Theme.borderStrong
        BFocusRing {
            visible: chip.visualFocus
            targetRadius: pill.radius
        }
    }
    Row {
        id: chipRow
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        BIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: chip.busy ? "refresh-cw" : chip.failure !== "" || chip.level !== "" ? "triangle-alert" : "gauge"
            size: 12
            color: chip.fgColor
        }
        Text {
            objectName: "chatAgentLimitsText"
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            textFormat: Text.PlainText
            font: chip.font
            color: chip.fgColor
            elide: Text.ElideRight
            width: Math.min(implicitWidth, 280)
        }
    }

    Loader {
        active: chip.hovered && chip.tooltip !== ""
        sourceComponent: BToolTip {
            parent: chip
            text: chip.tooltip
            visible: chip.hovered && !chip.pressed
        }
    }

    BMenu {
        id: menu
        objectName: "chatAgentLimitsMenu"
        BMenuItem {
            text: qsTr("Subscription limits")
            iconName: "gauge"
            hint: AppController.routeText(chip.info, "plan")
            enabled: false
        }
        Instantiator {
            model: chip.windows
            delegate: BMenuItem {
                id: windowItem
                required property var modelData
                readonly property string level: AppController.routeText(windowItem.modelData, "level")
                objectName: "chatAgentLimitsWindow"
                text: AppController.routeText(windowItem.modelData, "label")
                hint: AppController.routeText(windowItem.modelData, "text")
                iconName: windowItem.level !== "" ? "triangle-alert" : ""
                danger: windowItem.level === "danger"
                enabled: false
            }
            // (After the title.)
            onObjectAdded: (index, object) => menu.insertItem(index + 1, object as Item)
            onObjectRemoved: (index, object) => menu.removeItem(object as Item)
        }
        BMenuItem {
            objectName: "chatAgentLimitsFailure"
            visible: chip.failure !== ""
            height: visible ? implicitHeight : 0
            text: chip.failureShort
            iconName: "circle-alert"
            unavailable: chip.failure
            // (On touch screens no tooltip: the reason as a toast.)
            onTriggered: AppController.toast(chip.failure, "info")
        }
        BMenuItem {
            visible: AppController.routeText(chip.info, "checked") !== ""
            height: visible ? implicitHeight : 0
            text: AppController.routeText(chip.info, "checked")
            enabled: false
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "chatAgentLimitsRefresh"
            text: qsTr("Refresh")
            iconName: "refresh-cw"
            enabled: !chip.busy
            onTriggered: {
                chip.limits.refresh(chip.partner)
                chip.usage.refresh(chip.partner)
            }
        }
    }
}
