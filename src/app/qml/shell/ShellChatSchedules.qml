pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellChatSchedules — "Scheduled conversations" of Settings → Extensions (docs/UI.md "Scheduled
// conversations"; C++: Workspace.chats.schedules, shell/chatschedules.h): each schedule — its
// title, its rule in words and its next run, who answers, whether each run is a new conversation
// or the next message of one, on or off — with Run now, Edit… and Delete, and Add…. The rule is
// written in words ("codziennie o 7:30", "co 3 godziny", "jutro o 8") and shown back with its first
// run while it is typed. Kept on this device for the open notebook, never in it. A schedule may
// also be started by a webhook (docs/UI.md "Scheduled conversations" → "Webhooks"): its own address
// with a long secret, shown only masked (Copy puts all of it on the clipboard, "New address" makes
// another); with one, the rule may be left empty (only its webhook starts it). "Webhooks" below
// runs their server (Workspace.chats.hooks, shell/hookserver.h).
ColumnLayout {
    id: section

    readonly property ChatSchedules schedules: Workspace.chats.schedules
    readonly property HookServer hooks: Workspace.chats.hooks
    readonly property list<var> rows: section.schedules.rows
    property list<var> partners
    readonly property list<var> addressChoices: [{ value: "local", text: qsTr("This computer only") },
                                                 { value: "tailnet", text: qsTr("Tailnet") }]
    readonly property list<var> localChoice: [{ value: "local", text: qsTr("This computer only") }]
    /// The schedule the dialog edits ("" a new one) and its choices.
    property string editing: ""
    property string draftPartner: ""
    /// The webhook's secret of the schedule edited ("" none; never shown whole — the remote
    /// control's inspect leaves "token" properties out).
    property string draftHookToken: ""
    /// The rule being typed, checked: in words, its first run, why not.
    property bool whenOk: false
    property string whenText: ""
    property string whenNext: ""
    property string whenError: ""

    function partnerName(id: string): string {
        for (let i = 0; i < section.partners.length; ++i) {
            if (AppController.routeText(section.partners[i], "id") === id)
                return AppController.routeText(section.partners[i], "label")
        }
        return id
    }
    function checkWhen(): void {
        const when = whenField.text
        // With a webhook the rule may stay empty: only the webhook starts it.
        const hookOnly = Js.trim(when) === "" && section.draftHookToken !== ""
        section.whenText = hookOnly ? section.schedules.hookOnlyText() : section.schedules.whenText(when)
        section.whenNext = hookOnly ? "" : section.schedules.whenNext(when)
        section.whenError = Js.trim(when) === "" ? "" : section.schedules.whenError(when)
        section.whenOk = section.whenText !== ""
    }
    function edit(id: string, title: string, prompt: string, partner: string, when: string, continues: bool,
                  enabled: bool, hookToken: string): void {
        section.editing = id
        titleField.text = title
        promptField.text = prompt
        whenField.text = when
        section.draftPartner = partner
        section.draftHookToken = hookToken
        hookSwitch.checked = hookToken !== ""
        destination.currentValue = continues ? "continue" : "new"
        enabledSwitch.checked = enabled
        section.checkWhen()
        dialog.open()
    }
    function setHook(on: bool): void {
        section.draftHookToken = on ? (section.draftHookToken !== "" ? section.draftHookToken : section.schedules.newHookToken()) : ""
        section.checkWhen()
    }
    /// The row's line: its rule, who answers, its next run (or why its last did not start).
    function summary(rule: string, partner: string, next: string, enabled: bool, continues: bool, lastError: string,
                     by: string): string {
        let line = qsTr("%1 · %2").arg(rule).arg(partner)
        line += continues ? qsTr(" · one conversation") : qsTr(" · a new conversation each time")
        if (!enabled)
            line += qsTr(" · off")
        else if (next !== "")
            line += qsTr(" · next: %1").arg(next)
        else if (rule !== section.schedules.hookOnlyText())
            line += qsTr(" · done")
        if (by !== "")
            line += qsTr(" · set up by %1").arg(by)
        if (lastError !== "")
            line += "\n" + qsTr("Last time: %1").arg(lastError)
        return line
    }
    function setHooksAddress(value: string): void {
        AppSettings.chatHooksAddress = value === "tailnet" ? "tailnet" : ""
    }
    /// What the row says of its webhook ("" none).
    function hookLine(webhook: bool, lastHook: string): string {
        if (!webhook)
            return ""
        return lastHook !== "" ? qsTr("Webhook · last started by it: %1").arg(lastHook) : qsTr("Webhook")
    }

    visible: section.schedules.available
    Layout.fillWidth: true
    spacing: Theme.space2

    Component.onCompleted: section.partners = Workspace.chats.partners()
    Connections {
        target: Workspace.chats
        function onPartnersChanged() { section.partners = Workspace.chats.partners() }
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Scheduled conversations")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("A message sent to the AI assistant or an AI agent when you say: every morning, every few hours, once tomorrow. Kept on this device for this notebook; they run while Brulion runs, and one missed while it was closed runs once when it starts again.")
        variant: "muted"
        wrapMode: Text.Wrap
    }
    BCard {
        objectName: "chatSchedulesSettings"
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: rowsColumn.implicitWidth
        contentHeight: rowsColumn.implicitHeight
        ColumnLayout {
            id: rowsColumn
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            BText {
                objectName: "chatSchedulesEmpty"
                visible: section.rows.length === 0
                Layout.fillWidth: true
                text: qsTr("None yet.")
                variant: "muted"
            }
            Repeater {
                model: section.rows
                delegate: ShellSettingRow {
                    id: scheduleRow
                    required property var modelData
                    readonly property string scheduleId: AppController.routeText(scheduleRow.modelData, "id")
                    readonly property bool on: AppController.routeFlag(scheduleRow.modelData, "enabled")
                    readonly property bool continues: AppController.routeFlag(scheduleRow.modelData, "continues")
                    readonly property string prompt: AppController.routeText(scheduleRow.modelData, "prompt")
                    readonly property string partner: AppController.routeText(scheduleRow.modelData, "partner")
                    readonly property string partnerName: AppController.routeText(scheduleRow.modelData, "partnerName")
                    readonly property string when: AppController.routeText(scheduleRow.modelData, "when")
                    readonly property string rule: AppController.routeText(scheduleRow.modelData, "rule")
                    readonly property string next: AppController.routeText(scheduleRow.modelData, "next")
                    readonly property string lastError: AppController.routeText(scheduleRow.modelData, "lastError")
                    readonly property string by: AppController.routeText(scheduleRow.modelData, "by")
                    readonly property bool webhook: AppController.routeFlag(scheduleRow.modelData, "webhook")
                    readonly property string hookToken: AppController.routeText(scheduleRow.modelData, "hookToken")
                    readonly property string lastHook: AppController.routeText(scheduleRow.modelData, "lastHook")
                    readonly property string hookLine: section.hookLine(scheduleRow.webhook, scheduleRow.lastHook)
                    objectName: "chatScheduleRow"
                    Layout.fillWidth: true
                    title: AppController.routeText(scheduleRow.modelData, "title")
                    description: section.summary(scheduleRow.rule, scheduleRow.partnerName, scheduleRow.next, scheduleRow.on,
                                                 scheduleRow.continues, scheduleRow.lastError, scheduleRow.by)
                                 + (scheduleRow.hookLine !== "" ? "\n" + scheduleRow.hookLine : "")
                    BIcon {
                        objectName: "chatScheduleWebhook"
                        visible: scheduleRow.webhook
                        name: "link"
                        size: Theme.iconSize
                        color: Theme.fgMuted
                    }
                    BButton {
                        objectName: "chatScheduleRun"
                        size: "small"
                        variant: "secondary"
                        iconName: "play"
                        text: qsTr("Run now")
                        onClicked: {
                            const path = section.schedules.runNow(scheduleRow.scheduleId)
                            if (path !== "")
                                AppController.navigate({ view: "chat", path: path })
                        }
                    }
                    BIconButton {
                        objectName: "chatScheduleEdit"
                        label: qsTr("Edit…")
                        iconName: "pencil"
                        onClicked: section.edit(scheduleRow.scheduleId, scheduleRow.title, scheduleRow.prompt,
                                                scheduleRow.partner, scheduleRow.when, scheduleRow.continues, scheduleRow.on,
                                                scheduleRow.hookToken)
                    }
                    BIconButton {
                        objectName: "chatScheduleDelete"
                        label: qsTr("Delete")
                        iconName: "trash-2"
                        onClicked: section.schedules.remove(scheduleRow.scheduleId)
                    }
                }
            }
            ShellSettingRow {
                Layout.fillWidth: true
                title: qsTr("Add a scheduled conversation")
                description: qsTr("For example “Plan my day” every weekday at 7:30, or “What changed in my notes?” every evening.")
                BButton {
                    objectName: "chatScheduleAdd"
                    size: "small"
                    variant: "secondary"
                    iconName: "plus"
                    text: qsTr("Add…")
                    onClicked: section.edit("", "", "", Workspace.chats.defaultPartner, "", false, true, "") // (who answers: the default)
                }
            }
        }
        ShellSettingDividers { column: rowsColumn }
    }

    // Webhooks (shell/hookserver.h): their server, while Brulion runs.
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space4
        visible: section.hooks.available
        text: qsTr("Webhooks")
        variant: "label"
        wrapMode: Text.Wrap
    }
    BCard {
        objectName: "chatHooksSettings"
        Layout.fillWidth: true
        visible: section.hooks.available
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: hooksColumn.implicitWidth
        contentHeight: hooksColumn.implicitHeight
        ColumnLayout {
            id: hooksColumn
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            BSwitch {
                objectName: "chatHooksSwitch"
                Layout.fillWidth: true
                text: qsTr("Webhooks")
                description: section.hooks.status
                iconName: "link"
                checked: AppSettings.chatHooks
                onToggled: AppSettings.chatHooks = checked
            }
            ShellSettingRow {
                objectName: "chatHooksAddressRow"
                visible: AppSettings.chatHooks
                title: qsTr("Where it listens")
                description: section.hooks.tailnetName !== ""
                             ? qsTr("Your own devices in the tailnet reach it at %1.").arg(section.hooks.tailnetName)
                             : qsTr("This computer only (127.0.0.1): a reverse proxy of yours or Tailscale Funnel in front of it reaches it from the internet.")
                BSegmented {
                    objectName: "chatHooksAddress"
                    model: section.hooks.tailnetName !== "" || AppSettings.chatHooksAddress === "tailnet"
                           ? section.addressChoices : section.localChoice
                    currentValue: AppSettings.chatHooksAddress === "tailnet" ? "tailnet" : "local"
                    accessibleName: qsTr("Where it listens")
                    onActivated: (value) => section.setHooksAddress("" + value)
                }
            }
            ShellSettingRow {
                visible: AppSettings.chatHooks
                title: qsTr("Port")
                description: qsTr("The port it listens on.")
                BTextField {
                    id: hooksPortField
                    objectName: "chatHooksPort"
                    Layout.preferredWidth: Theme.compact ? -1 : 120
                    Layout.fillWidth: Theme.compact
                    text: "" + AppSettings.chatHooksPort
                    accessibleName: qsTr("Port")
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1024; top: 65535 }
                    onEditingFinished: {
                        AppSettings.chatHooksPort = Js.parseInt(hooksPortField.text)
                        hooksPortField.text = "" + AppSettings.chatHooksPort
                    }
                }
            }
            ShellSettingRow {
                visible: AppSettings.chatHooks
                title: qsTr("Public address")
                description: qsTr("The address callers reach it at, e.g. your reverse proxy or Tailscale Funnel (tailscale funnel --bg %1); empty: where it listens.").arg(AppSettings.chatHooksPort)
                BTextField {
                    objectName: "chatHooksPublicUrl"
                    Layout.preferredWidth: Theme.compact ? -1 : 260
                    Layout.fillWidth: Theme.compact
                    text: AppSettings.chatHooksPublicUrl
                    placeholderText: "https://home.example.com/brulion"
                    accessibleName: qsTr("Public address")
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onEditingFinished: AppSettings.chatHooksPublicUrl = text
                }
            }
            BText {
                Layout.fillWidth: true
                visible: AppSettings.chatHooks
                text: qsTr("Each schedule has its own address (Edit… → “Also start it by a webhook”): POST to it starts the conversation, what it sends goes along as data. At most once a minute; keep the address secret.")
                textFormat: Text.PlainText
                variant: "caption"
                wrapMode: Text.Wrap
            }
        }
        ShellSettingDividers { column: hooksColumn }
    }

    BDialog {
        id: dialog
        objectName: "chatScheduleDialog"
        title: section.editing === "" ? qsTr("New scheduled conversation") : qsTr("Scheduled conversation")
        subtitle: qsTr("Sent as a message of the AI chat when the rule says.")
        iconName: "calendar-clock"
        acceptText: qsTr("Save")
        acceptEnabled: section.whenOk && Js.trim(promptField.text) !== ""
        initialFocusItem: section.editing === "" ? promptField : titleField
        onAccepted: {
            const saved = section.schedules.save(section.editing, titleField.text, promptField.text, section.draftPartner,
                                                 whenField.text, destination.currentValue === "continue",
                                                 enabledSwitch.checked, section.draftHookToken)
            section.draftHookToken = ""
            if (saved === "")
                AppController.toast(section.schedules.lastError(), "error")
        }
        onRejected: section.draftHookToken = ""

        BText {
            Layout.fillWidth: true
            text: qsTr("Message")
            variant: "label"
        }
        BTextArea {
            id: promptField
            objectName: "chatSchedulePrompt"
            Layout.fillWidth: true
            Layout.minimumHeight: 72
            placeholderText: qsTr("What do I have to do today?")
            accessibleName: qsTr("Message")
            wrapMode: TextEdit.Wrap
        }
        BText {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            text: qsTr("When")
            variant: "label"
        }
        BTextField {
            id: whenField
            objectName: "chatScheduleWhen"
            Layout.fillWidth: true
            placeholderText: qsTr("every day at 7:30")
            accessibleName: qsTr("When")
            errorText: section.whenError
            onTextChanged: section.checkWhen()
        }
        BText {
            objectName: "chatScheduleWhenText"
            Layout.fillWidth: true
            visible: section.whenOk
            text: section.whenNext !== "" ? qsTr("%1 — first: %2").arg(section.whenText).arg(section.whenNext) : section.whenText
            variant: "caption"
            wrapMode: Text.Wrap
        }
        BText {
            Layout.fillWidth: true
            visible: !section.whenOk && section.whenError === ""
            text: qsTr("“codziennie o 7:30”, “w poniedziałki o 9:00”, “co 3 godziny”, “jutro o 8”, “every day at 7:30”.")
            variant: "caption"
            wrapMode: Text.Wrap
        }
        // A webhook: its own address starts it too (with one, the rule may stay empty).
        BSwitch {
            id: hookSwitch
            objectName: "chatScheduleHook"
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            text: qsTr("Also start it by a webhook")
            description: qsTr("A request from elsewhere — GitHub, Home Assistant, a cron job, iOS Shortcuts — to its own address starts it, with what it sends as data. Leave “When” empty to start it only so.")
            onToggled: section.setHook(hookSwitch.checked)
        }
        RowLayout {
            objectName: "chatScheduleHookRow"
            Layout.fillWidth: true
            visible: section.draftHookToken !== ""
            spacing: Theme.space2
            BText {
                objectName: "chatScheduleHookUrl"
                Layout.fillWidth: true
                text: section.schedules.maskedHookUrl(section.draftHookToken)
                textFormat: Text.PlainText
                variant: "mono"
                font.pixelSize: Theme.textSm
                wrapMode: Text.WrapAnywhere
            }
            BIconButton {
                objectName: "chatScheduleHookCopy"
                iconName: "copy"
                label: qsTr("Copy the address")
                onClicked: {
                    if (section.schedules.copyHookUrl(section.draftHookToken))
                        AppController.toast(qsTr("The webhook's address is copied: keep it secret, it starts this conversation."), "success")
                }
            }
            BIconButton {
                objectName: "chatScheduleHookNew"
                iconName: "refresh-cw"
                label: qsTr("New address (the old one stops working when you save)")
                onClicked: section.draftHookToken = section.schedules.newHookToken()
            }
        }
        BText {
            Layout.fillWidth: true
            visible: section.draftHookToken !== "" && !section.hooks.on
            text: qsTr("Webhooks are off: turn them on under “Webhooks” below, or nothing answers at this address.")
            textFormat: Text.PlainText
            color: Theme.warningText
            variant: "caption"
            wrapMode: Text.Wrap
        }
        BText {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            text: qsTr("Who answers")
            variant: "label"
        }
        BButton {
            id: partnerButton
            objectName: "chatSchedulePartner"
            variant: "secondary"
            iconName: "sparkles"
            text: section.partnerName(section.draftPartner)
            onClicked: partnerMenu.popup(partnerButton, 0, partnerButton.height)
        }
        BMenu {
            id: partnerMenu
            objectName: "chatSchedulePartnerMenu"
            Instantiator {
                model: section.partners
                delegate: BMenuItem {
                    id: partnerItem
                    required property var modelData
                    readonly property string partnerId: AppController.routeText(partnerItem.modelData, "id")
                    objectName: "chatSchedulePartnerItem"
                    text: AppController.routeText(partnerItem.modelData, "label")
                    hint: AppController.routeText(partnerItem.modelData, "extension")
                    iconName: partnerItem.partnerId === section.draftPartner ? "check" : ""
                    onTriggered: section.draftPartner = partnerItem.partnerId
                }
                onObjectAdded: (index, object) => partnerMenu.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => partnerMenu.removeItem(object as Item)
            }
        }
        BText {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            text: qsTr("Each time")
            variant: "label"
        }
        BSegmented {
            id: destination
            objectName: "chatScheduleDestination"
            model: [{ value: "new", text: qsTr("A new conversation") },
                    { value: "continue", text: qsTr("The same conversation") }]
            currentValue: "new"
            accessibleName: qsTr("Each time")
            onActivated: (value) => destination.currentValue = value
        }
        BText {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            text: qsTr("Name")
            variant: "label"
        }
        BTextField {
            id: titleField
            objectName: "chatScheduleTitle"
            Layout.fillWidth: true
            placeholderText: qsTr("Its message's first words")
            accessibleName: qsTr("Name")
        }
        BSwitch {
            id: enabledSwitch
            objectName: "chatScheduleEnabled"
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            text: qsTr("On")
            description: qsTr("Off, it keeps its rule and does not run.")
        }
    }
}
