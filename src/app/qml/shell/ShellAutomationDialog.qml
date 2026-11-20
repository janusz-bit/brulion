pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellAutomationDialog — a brulion:// address without the notebook's automation key asks to write
// (AppController.pendingAutomation; docs/UI.md "Automation"): where it would write (today's
// journal, a page and its heading or block, a new page, an extension's command) and exactly what,
// as plain text. Nothing is written before "Write" (AppController.confirmAutomation); "Cancel"
// drops the request (AppController.dismissAutomation). Links come from anywhere — a web page, a
// message, a note synced from elsewhere —, so the caller's own name (`x-source`) is shown as what
// the link says, never as a fact, and the hint says how automation tools write without asking.
BDialog {
    id: dialog

    readonly property var request: AppController.pendingAutomation
    readonly property string action: AppController.routeText(request, "action")
    readonly property bool runs: AppController.routeText(request, "access") === "run"
    readonly property bool newPage: AppController.routeFlag(request, "newPage")
    readonly property bool journal: AppController.routeFlag(request, "journal")
    readonly property string target: AppController.routeText(request, "target")
    readonly property string heading: AppController.routeText(request, "heading")
    readonly property string blockId: AppController.routeText(request, "blockId")
    readonly property bool atStart: AppController.routeText(request, "place") === "start"
    readonly property string source: AppController.routeText(request, "source")
    readonly property string kind: AppController.routeText(request, "kind")
    readonly property string when: AppController.routeText(request, "when")
    /// The last confirmation failed (the page changed meanwhile, say): shown under the text.
    property string error

    /// What it would write into or run (a whole phrase per case: each language words them its own way).
    function targetText(): string {
        if (dialog.runs)
            return qsTr("The command “%1” of the extension “%2”").arg(AppController.routeText(dialog.request, "command"))
                                                               .arg(AppController.routeText(dialog.request, "extension"))
        if (dialog.newPage)
            return qsTr("A new page “%1”").arg(dialog.target)
        if (dialog.journal)
            return AppController.routeFlag(dialog.request, "today")
                   ? qsTr("Today's journal")
                   : qsTr("The journal of %1").arg(AppController.routeText(dialog.request, "day"))
        return qsTr("The page “%1”").arg(dialog.target)
    }
    /// Where in it ("" for a new page and a command).
    function placeText(): string {
        if (dialog.runs || dialog.newPage)
            return ""
        if (dialog.heading !== "")
            return dialog.atStart ? qsTr("At the start of the section “%1”").arg(dialog.heading)
                                  : qsTr("At the end of the section “%1”").arg(dialog.heading)
        if (dialog.blockId !== "")
            return qsTr("Under the block ^%1").arg(dialog.blockId)
        return dialog.atStart ? qsTr("At the start") : qsTr("At the end")
    }

    title: dialog.runs ? qsTr("Run from a link?") : qsTr("Write from a link?")
    subtitle: dialog.source !== "" ? qsTr("The link says it comes from “%1”.").arg(dialog.source)
                                   : qsTr("A link asks Brulion to change your notebook.")
    iconName: "zap"
    acceptText: dialog.runs ? qsTr("Run") : qsTr("Write")
    acceptEnabled: AppController.routeText(request, "action") !== ""

    onAboutToShow: dialog.error = ""
    onAcceptRequested: {
        dialog._result = AppController.confirmAutomation()
        if (AppController.routeFlag(dialog._result, "ok"))
            dialog.close()
        else
            dialog.error = AppController.routeText(dialog._result, "error")
    }
    onRejected: AppController.dismissAutomation()
    /// (Kept in a property: a call's result read after another call is not compiled.)
    property var _result: ({})

    ColumnLayout {
        objectName: "automationWhere"
        Layout.fillWidth: true
        spacing: Theme.space1

        BText {
            objectName: "automationTarget"
            Layout.fillWidth: true
            text: dialog.targetText()
            textFormat: Text.PlainText
            variant: "label"
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        BText {
            objectName: "automationPlace"
            Layout.fillWidth: true
            visible: text !== ""
            text: dialog.placeText()
            textFormat: Text.PlainText
            variant: "muted"
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    Rectangle {
        Layout.fillWidth: true
        visible: !dialog.runs && blockText.text !== ""
        implicitHeight: Math.min(blockText.implicitHeight, Theme.touchUi ? 200 : 180) + 2 * Theme.space3
        radius: Theme.radiusSm
        color: Theme.bgSunken
        border.color: Theme.border
        clip: true

        BText {
            id: blockText
            objectName: "automationBlock"
            anchors.fill: parent
            anchors.margins: Theme.space3
            text: AppController.routeText(dialog.request, "block")
            textFormat: Text.PlainText
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            wrapMode: Text.WrapAnywhere
            elide: Text.ElideRight
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: dialog.when !== ""
        spacing: Theme.space2
        BIcon {
            name: dialog.kind === "alarm" ? "alarm-clock" : "bell"
            color: Theme.warningText
        }
        BText {
            Layout.fillWidth: true
            text: dialog.kind === "alarm" ? qsTr("An alarm at %1").arg(dialog.when) : qsTr("A reminder at %1").arg(dialog.when)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }
    }

    BText {
        objectName: "automationNotebook"
        Layout.fillWidth: true
        text: qsTr("Notebook: %1").arg(AppController.routeText(dialog.request, "notebook"))
        textFormat: Text.PlainText
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Links on web pages and in other apps can ask this. Write only if you just used such a link yourself. Automation tools write without asking with the notebook's key (Settings → Automation).")
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }

    BText {
        objectName: "automationError"
        Layout.fillWidth: true
        visible: dialog.error !== ""
        text: dialog.error
        textFormat: Text.PlainText
        color: Theme.dangerText
        wrapMode: Text.Wrap
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
