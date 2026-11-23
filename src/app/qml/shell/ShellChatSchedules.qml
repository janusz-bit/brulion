pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellChatSchedules — "Scheduled conversations" of Settings → Extensions (docs/UI.md "Scheduled
// conversations"; C++: Workspace.chats.schedules, shell/chatschedules.h): each schedule — its
// title, its rule in words and its next run, who answers, whether each run is a new conversation
// or the next message of one, on or off — with Run now, Edit… and Delete, and Add…. The rule is
// written in words ("codziennie o 7:30", "co 3 godziny", "jutro o 8") and shown back with its first
// run while it is typed. Kept on this device for the open notebook, never in it.
ColumnLayout {
    id: section

    readonly property ChatSchedules schedules: Workspace.chats.schedules
    readonly property list<var> rows: section.schedules.rows
    property list<var> partners
    /// The schedule the dialog edits ("" a new one) and its choices.
    property string editing: ""
    property string draftPartner: ""
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
        section.whenText = section.schedules.whenText(when)
        section.whenNext = section.schedules.whenNext(when)
        section.whenError = Js.trim(when) === "" ? "" : section.schedules.whenError(when)
        section.whenOk = section.whenText !== ""
    }
    function edit(id: string, title: string, prompt: string, partner: string, when: string, continues: bool,
                  enabled: bool): void {
        section.editing = id
        titleField.text = title
        promptField.text = prompt
        whenField.text = when
        section.draftPartner = partner
        destination.currentValue = continues ? "continue" : "new"
        enabledSwitch.checked = enabled
        section.checkWhen()
        dialog.open()
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
        else
            line += qsTr(" · done")
        if (by !== "")
            line += qsTr(" · set up by %1").arg(by)
        if (lastError !== "")
            line += "\n" + qsTr("Last time: %1").arg(lastError)
        return line
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
                    objectName: "chatScheduleRow"
                    Layout.fillWidth: true
                    title: AppController.routeText(scheduleRow.modelData, "title")
                    description: section.summary(scheduleRow.rule, scheduleRow.partnerName, scheduleRow.next, scheduleRow.on,
                                                 scheduleRow.continues, scheduleRow.lastError, scheduleRow.by)
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
                                                scheduleRow.partner, scheduleRow.when, scheduleRow.continues, scheduleRow.on)
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
                    onClicked: section.edit("", "", "", "", "", false, true)
                }
            }
        }
        ShellSettingDividers { column: rowsColumn }
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
            let saved = false
            if (section.editing === "")
                saved = section.schedules.add(titleField.text, promptField.text, section.draftPartner, whenField.text,
                                              destination.currentValue === "continue", enabledSwitch.checked) !== ""
            else
                saved = section.schedules.update(section.editing, titleField.text, promptField.text, section.draftPartner,
                                                 whenField.text, destination.currentValue === "continue",
                                                 enabledSwitch.checked)
            if (!saved)
                AppController.toast(section.schedules.lastError(), "error")
        }

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
