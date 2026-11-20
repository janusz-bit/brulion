pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellAutomationSettings — the "Automation" section of SettingsView (docs/UI.md "Automation";
// C++: AppController's automation part, shell/appautomation.cpp): what brulion:// links without
// the key may do on this device (AppController.automationPolicy: ask before writing, only open,
// off), the open notebook's automation key (create, show, copy, renew, remove — kept on this
// device only, never in the notebook), a builder for links (quick note, task, a page's section,
// open a page, a new page) with "Copy" and "Try it", and where the browser version can take
// web+brulion: links (WasmShell.registerProtocolHandler).
ColumnLayout {
    id: section

    readonly property string key: AppController.automationKey
    readonly property bool hasNotebook: Workspace.isOpen && !AppController.inDocsVault
    property bool keyShown: false
    /// The builder's action: "capture" | "task" | "append" | "open" | "new".
    property string action: "capture"
    property bool withKey: false
    /// The builder's choices ({ value, text, icon }).
    readonly property list<var> actions: [
        { value: "capture", text: qsTr("Quick note"), icon: "zap" },
        { value: "task", text: qsTr("Task"), icon: "list-todo" },
        { value: "append", text: qsTr("Add to a page"), icon: "list" },
        { value: "open", text: qsTr("Open"), icon: "file-text" },
        { value: "new", text: qsTr("New page"), icon: "square-pen" }
    ]
    readonly property string link: AppController.automationLink(section.action, {
        page: section.action === "append" || section.action === "open" ? Js.trim(pageField.text) : "",
        title: section.action === "new" ? Js.trim(pageField.text) : "",
        heading: section.action === "append" ? Js.trim(headingField.text) : "",
        text: section.action !== "open" ? textField.text : "",
        remind: section.action === "task" ? Js.trim(whenField.text) : ""
    }, section.withKey && section.key !== "")

    Layout.fillWidth: true
    spacing: Theme.space2

    BText {
        Layout.fillWidth: true
        text: qsTr("Automation")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        objectName: "automationNote"
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("brulion:// links open pages and add notes, tasks and reminders from other apps, launchers, scripts and automation tools. The documentation's page “Automation” lists every link.")
    }

    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: rows.implicitWidth
        contentHeight: rows.implicitHeight

        ColumnLayout {
            id: rows
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            ShellSettingRow {
                objectName: "automationPolicyRow"
                title: qsTr("Links without the key")
                description: AppController.automationPolicy === "ask"
                             ? qsTr("Opening works at once. Before a link writes, Brulion shows what it would add and asks.")
                             : AppController.automationPolicy === "key"
                               ? qsTr("Links only open pages and views. Writing needs the notebook's key.")
                               : qsTr("brulion:// links do nothing on this device.")
                BSegmented {
                    objectName: "automationPolicy"
                    accessibleName: qsTr("Links without the key")
                    model: [{ value: "ask", text: qsTr("Ask") }, { value: "key", text: qsTr("Only open") },
                            { value: "off", text: qsTr("Off") }]
                    currentValue: AppController.automationPolicy
                    onActivated: (value) => AppController.automationPolicy = value
                }
            }

            ShellSettingRow {
                objectName: "automationKeyRow"
                visible: section.hasNotebook
                title: qsTr("Automation key")
                description: section.key === ""
                             ? qsTr("With the key in a link (key=…), Tasker, Shortcuts, scripts and launchers write into this notebook without asking. It stays on this device.")
                             : section.keyShown ? section.key
                                                : qsTr("Keep it private like a password: whoever has it can add to this notebook.")
                BButton {
                    objectName: "automationCreateKey"
                    visible: section.key === ""
                    text: qsTr("Create a key")
                    iconName: "lock"
                    onClicked: AppController.createAutomationKey()
                }
                BButton {
                    objectName: "automationShowKey"
                    visible: section.key !== ""
                    text: section.keyShown ? qsTr("Hide") : qsTr("Show")
                    size: "small"
                    variant: "ghost"
                    onClicked: section.keyShown = !section.keyShown
                }
                BButton {
                    objectName: "automationCopyKey"
                    visible: section.key !== ""
                    text: qsTr("Copy")
                    iconName: "copy"
                    size: "small"
                    variant: "ghost"
                    onClicked: {
                        if (Logs.copyText(section.key))
                            AppController.toast(qsTr("Key copied"), "success")
                    }
                }
                BButton {
                    objectName: "automationRenewKey"
                    visible: section.key !== ""
                    text: qsTr("New key")
                    iconName: "refresh-cw"
                    size: "small"
                    variant: "ghost"
                    onClicked: renewDialog.open()
                }
                BButton {
                    objectName: "automationRemoveKey"
                    visible: section.key !== ""
                    text: qsTr("Remove")
                    size: "small"
                    variant: "ghost"
                    onClicked: removeDialog.open()
                }
            }
            BText {
                Layout.fillWidth: true
                visible: !section.hasNotebook
                text: qsTr("Open your notebook to make its automation key.")
                variant: "muted"
                wrapMode: Text.Wrap
            }

            ShellSettingRow {
                objectName: "automationBrowserRow"
                visible: DeviceInfo.platform === "wasm"
                title: qsTr("Links in this browser")
                description: qsTr("web+brulion: links open this Brulion. The browser asks once.")
                BButton {
                    objectName: "automationRegisterHandler"
                    text: qsTr("Allow")
                    iconName: "external-link"
                    onClicked: AppController.registerWebLinks()
                }
            }
        }
    }

    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space4
        text: qsTr("Make a link")
        variant: "label"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("For a launcher, a script, a bookmark or an automation app. Words like “jutro o 18” in a task's reminder become its date.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    BCard {
        Layout.fillWidth: true
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: builder.implicitWidth
        contentHeight: builder.implicitHeight

        ColumnLayout {
            id: builder
            anchors.fill: parent
            spacing: Theme.space3

            // What the link does: chips that wrap (five choices do not fit a phone's row).
            Flow {
                objectName: "automationAction"
                Layout.fillWidth: true
                spacing: Theme.space2
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("What the link does")

                Repeater {
                    model: section.actions
                    delegate: BChip {
                        id: actionChip
                        // (The choices above: value, text, icon.)
                        required property var modelData
                        readonly property string value: AppController.routeText(modelData, "value")
                        objectName: "automationAction_" + value
                        text: AppController.routeText(modelData, "text")
                        iconName: AppController.routeText(modelData, "icon")
                        interactive: true
                        selected: section.action === value
                        onClicked: section.action = actionChip.value
                    }
                }
            }
            BTextField {
                id: pageField
                objectName: "automationPage"
                Layout.fillWidth: true
                visible: section.action === "append" || section.action === "open" || section.action === "new"
                placeholderText: section.action === "new" ? qsTr("Title of the new page") : qsTr("Page (a name, a path or a date)")
                leadingIcon: "file-text"
            }
            BTextField {
                id: headingField
                objectName: "automationHeading"
                Layout.fillWidth: true
                visible: section.action === "append"
                placeholderText: qsTr("Under the heading (optional)")
                leadingIcon: "heading"
            }
            BTextField {
                id: textField
                objectName: "automationText"
                Layout.fillWidth: true
                visible: section.action !== "open"
                placeholderText: section.action === "task" ? qsTr("Task") : qsTr("Text")
            }
            BTextField {
                id: whenField
                objectName: "automationWhen"
                Layout.fillWidth: true
                visible: section.action === "task"
                placeholderText: qsTr("Reminder (optional): jutro o 18, tomorrow 6pm")
                leadingIcon: "bell"
            }
            BCheckBox {
                objectName: "automationWithKey"
                Layout.fillWidth: true
                visible: section.key !== "" && section.action !== "open"
                text: qsTr("With the key (writes without asking)")
                checked: section.withKey
                onToggled: section.withKey = checked
            }
            BText {
                objectName: "automationLink"
                Layout.fillWidth: true
                text: section.link
                textFormat: Text.PlainText
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
                wrapMode: Text.WrapAnywhere
                color: Theme.fgMuted
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BButton {
                    objectName: "automationCopyLink"
                    text: qsTr("Copy link")
                    iconName: "copy"
                    enabled: section.link !== ""
                    onClicked: {
                        if (Logs.copyText(section.link))
                            AppController.toast(qsTr("Link copied"), "success")
                    }
                }
                BButton {
                    objectName: "automationTryLink"
                    text: qsTr("Try it")
                    iconName: "zap"
                    variant: "ghost"
                    enabled: section.link !== "" && section.hasNotebook
                    onClicked: AppController.receiveUri(section.link, "settings")
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    BConfirmDialog {
        id: removeDialog
        title: qsTr("Remove the key?")
        message: qsTr("Links and automations with it stop writing. Links without a key still ask first.")
        confirmText: qsTr("Remove")
        destructive: true
        onAccepted: {
            AppController.removeAutomationKey()
            section.keyShown = false
        }
    }

    BConfirmDialog {
        id: renewDialog
        title: qsTr("Make a new key?")
        message: qsTr("Links and automations with the old key stop writing until they get the new one.")
        confirmText: qsTr("New key")
        onAccepted: {
            AppController.renewAutomationKey()
            section.keyShown = true
        }
    }
}
