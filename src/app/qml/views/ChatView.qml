pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatView — route "chat" (docs/UI.md "AI chat"): conversations with the AI, as in a messenger.
// Each conversation is a page in AI/ (Workspace.chats, core chat/chat.h): the user's messages on
// the right, the AI's on the left — rendered note text (ChatMessageModel: links open their pages),
// their pictures, the time each was written and the day above a day's first —, under them the
// answer as the model writes it (the conversation's ChatTurn: partial; above it what the model
// thinks, collapsed — opened, all of it —; under it how fast it writes) and what the AI does now
// ("Czytam „Dom”…", turn.status; in line for the model on the device: its place) with Stop, a
// change it asks a yes for as a card with the question's options (the question of its turn —
// shown or waiting behind another conversation's: Workspace.extensions.chatQuestion*), a failed
// turn with Try again; at the bottom the field (Enter sends with a keyboard, Shift+Enter is a new
// line; on touch the send button), the pictures going with the message (a file, the clipboard, a
// drop, the camera), dictation, and — before a new conversation's first message — the page it is
// about as a chip that can be taken away and who answers (the built-in assistant, or an
// extension's chat command).
//
//   {view: "chat"}                          the conversations (a wide window: beside a new one)
//   {view: "chat", path: "AI/….md"}         one conversation
//   {view: "chat", new: true, page, prompt} a new one about `page`; `prompt` is sent at once
//
// Wide windows (≥ 720 px, not in the side panel) show the list beside the conversation; narrow
// ones one of them (the list → a conversation is a step of the history, Back returns). The first
// message of a new conversation replaces the route with the conversation's (in the side panel:
// pins it there again). The list's rows have a menu (right-click, a long press): Rename…, Delete
// (to the trash, with Undo). Several conversations answer at once: each its own turn. Contract
// with the shell (qml/shell/ShellRouteView.qml): `route`, `title`.
FocusScope {
    id: root

    property var route: ({})
    property bool inSidePanel: false
    /// The conversation shown: the route's, or the one its first message just started ("" none).
    property string path
    /// A new conversation's page (the chip; taken away before the first message: none).
    property string page
    /// A new conversation is being written (the route's `new`).
    property bool starting
    /// Who answers a new conversation ("" the built-in assistant; an extension's "id/command").
    property string newPartner
    /// Who answers this one (its page's `partner:`).
    property string partnerHere
    /// The pictures going with the next message (vault paths, stored when picked).
    property list<string> attached
    readonly property string title: root.path !== "" && messages.title !== "" ? messages.title : qsTr("AI chat")

    readonly property bool wide: !root.inSidePanel && root.width >= 720
    readonly property bool listShown: root.wide || (root.path === "" && !root.starting)
    readonly property bool conversationShown: root.wide || root.path !== "" || root.starting
    readonly property WorkspaceChats chats: Workspace.chats
    /// This conversation's turn ("" a new one's: only its failure).
    readonly property ChatTurn turn: root.chats.turn(root.path)
    readonly property bool busyHere: root.path !== "" && root.turn !== null && root.turn.running
    /// It waits for the model on the device, which answers another conversation.
    readonly property bool queuedHere: root.busyHere && root.turn.queued
    /// The change the AI asks a yes for, in this conversation (shown here, or waiting behind
    /// another conversation's question).
    readonly property int questionSerial: Workspace.extensions.questionSerial + Workspace.extensions.chatQuestionSerial
    readonly property bool asking: root.path !== "" && Workspace.extensions.chatQuestionKind(root.path, root.questionSerial) === "choose"
    readonly property string questionText: root.asking ? Workspace.extensions.chatQuestionTitle(root.path, root.questionSerial) : ""
    readonly property list<string> questionOptions: root.asking ? Workspace.extensions.chatQuestionOptions(root.path, root.questionSerial) : root.noStrings
    readonly property int questionTitleEnd: Js.indexOf(root.questionText, "\n")
    readonly property list<string> noStrings: []
    readonly property string failure: root.turn !== null ? root.turn.failure : ""
    readonly property bool failedHere: root.failure !== ""

    /// The list follows the answer being written while it is at its end (scrolled up: it stays).
    property bool following: true
    /// The answer as it is written: what the model thinks first, then its words.
    readonly property bool liveShown: root.busyHere && !root.queuedHere && !root.asking
                                      && (root.turn.partial !== "" || root.turn.thoughts !== "")
    /// The model's thoughts are opened (all of them), else one line of their end.
    property bool thoughtsOpen: false
    /// Who may answer: the assistant and the extensions' chat commands ({id, label, extension}).
    property list<var> partners
    readonly property bool choosesPartner: root.partners.length > 1
    /// The field waits for dictation's text / a photo from the camera (this view asked for it).
    property bool dictating: false
    property bool takingPhoto: false

    /// The conversation this view tells Workspace.chats it shows ("" none).
    property string registered
    readonly property string shownPath: root.visible && root.conversationShown ? root.path : ""
    onShownPathChanged: root.register()
    Component.onDestruction: {
        if (root.registered !== "")
            Workspace.chats.showing(root.registered, false)
        // Pictures picked but never sent: not left in the notebook.
        for (let i = 0; i < root.attached.length; ++i)
            Workspace.chats.dropPicture(root.attached[i])
        if (root.dictating)
            Workspace.chats.cancelDictation()
    }
    function register(): void {
        if (root.registered === root.shownPath)
            return
        if (root.registered !== "")
            root.chats.showing(root.registered, false)
        root.registered = root.shownPath
        if (root.registered !== "")
            root.chats.showing(root.registered, true)
    }

    function syncRoute(): void {
        root.path = AppController.routeText(root.route, "path")
        root.page = AppController.routeText(root.route, "page")
        root.starting = root.path === "" && AppController.routeFlag(root.route, "new")
        root.thoughtsOpen = false
        const prompt = AppController.routeText(root.route, "prompt")
        if (root.starting && prompt !== "") {
            input.text = prompt
            sendLater.restart()
        } else if (root.conversationShown && (root.starting || !Theme.touchUi)) {
            focusLater.restart()
        }
    }
    onRouteChanged: root.syncRoute()
    Component.onCompleted: {
        root.syncRoute()
        root.refreshPartners()
    }
    function refreshPartners(): void {
        root.partners = Workspace.extensions.chatPartners()
    }
    Connections {
        target: Workspace.extensions
        function onCommandsChanged() { root.refreshPartners() }
    }
    /// A partner's name as the chat shows it (one not running: its id).
    function partnerName(id: string): string {
        const label = Workspace.extensions.partnerLabel(id)
        return label !== "" ? label : id
    }

    /// Opens the conversation `path` ("" with `fresh`: a new one).
    function open(path: string, fresh: bool): void {
        if (path === "" && !fresh)
            return
        if (root.inSidePanel)
            AppController.navigate(fresh ? { view: "chat", new: true } : { view: "chat", path: path }, true)
        else
            AppController.navigate(fresh ? { view: "chat", new: true } : { view: "chat", path: path })
    }
    function send(): void {
        const text = Js.trim(input.typedText)
        if ((text === "" && root.attached.length === 0) || root.busyHere)
            return
        const sent = root.chats.send(root.path, text, root.path === "" ? root.page : "", root.attached,
                                     root.path === "" ? root.newPartner : "")
        if (sent === "")
            return
        input.text = ""
        root.attached = root.noStrings
        root.following = true
        if (root.path === "") {
            root.path = sent
            root.starting = false
            if (root.inSidePanel)
                AppController.navigate({ view: "chat", path: sent }, true)
            else
                AppController.replaceRoute({ view: "chat", path: sent })
        }
        toEnd.restart()
    }
    function suggest(text: string): void {
        input.text = text
        root.send()
    }

    // ---- Pictures with the message --------------------------------------------------------------
    function attach(path: string): void {
        if (path !== "")
            root.attached = Js.uniqueStrings(Js.appendedString(root.attached, path))
    }
    function detach(path: string): void {
        root.attached = Js.withoutString(root.attached, path)
        root.chats.dropPicture(path)
    }
    function attachFiles(files: list<url>): void {
        // (A file that is no picture: picturesRefused().)
        root.attached = root.chats.addPicturesTo(root.attached, files)
    }
    /// Ctrl+V: a picture on the clipboard goes with the message (text is pasted as text).
    function pastePicture(): bool {
        const stored = root.chats.addClipboardPicture()
        if (stored === "")
            return false
        root.attach(stored)
        return true
    }
    function takePhoto(): void {
        root.takingPhoto = root.chats.takePhoto()
        if (!root.takingPhoto)
            AppController.toast(qsTr("No camera app on this device"), "info")
    }
    function toggleDictation(): void {
        if (root.chats.listening) {
            root.chats.stopDictation()
            return
        }
        root.dictating = root.chats.startDictation()
        if (!root.dictating)
            AppController.toast(qsTr("Dictation could not start"), "error")
    }
    Connections {
        target: root.chats
        function onPicturesRefused() {
            if (root.visible)
                AppController.toast(qsTr("Only pictures go with a message (PNG, JPEG, WebP, GIF)"), "info")
        }
        function onPhotoTaken(path: string) {
            if (!root.takingPhoto)
                return
            root.takingPhoto = false
            root.attach(path)
        }
        function onCaptureFailed(why: string) {
            if (!root.takingPhoto)
                return
            root.takingPhoto = false
            AppController.toast(qsTr("No photo: %1").arg(why), "error")
        }
        function onDictated(text: string) {
            if (!root.dictating)
                return
            root.dictating = false
            const heard = Js.trim(text)
            if (heard === "")
                return
            // At the caret, a space between it and the words around it.
            const at = input.cursorPosition
            const before = at > 0 ? Js.charAt(input.text, at - 1) : ""
            input.insert(at, (before !== "" && before !== " " && before !== "\n" ? " " : "") + heard)
            input.forceActiveFocus()
        }
        function onDictationFailed(why: string) {
            if (!root.dictating)
                return
            root.dictating = false
            if (why !== "")
                AppController.toast(qsTr("Dictation: %1").arg(why), "error")
        }
    }
    FilePicker {
        id: picturePicker
        title: qsTr("Pictures for the message")
        fileMode: FilePicker.OpenFiles
        dark: Theme.dark
        colors: Theme.colors
        nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg *.webp *.gif *.bmp)")]
        onAccepted: root.attachFiles(selectedFiles)
    }

    // ---- The list's menu: rename, delete ----------------------------------------------------------
    /// The conversation the list's menu is about.
    property string menuPath
    function openRowMenu(path: string, item: Item): void {
        root.menuPath = path
        rowMenu.popup(item, Theme.space4, item.height)
    }
    function rename(path: string, name: string): void {
        // (Copies, read before the rename: compiled calls take no arguments a call before may change.)
        const shown = root.path === path
        const oldTitle = "" + Workspace.pageTitle(path)
        const renamed = "" + root.chats.rename(path, name)
        if (renamed === "") {
            AppController.toast(qsTr("The conversation could not be renamed: is the name in use?"), "error")
            return
        }
        if (renamed === path)
            return
        const message = "" + qsTr("Renamed to “%1”").arg(root.chats.nameOf(renamed))
        AppController.toastUndoRename(message, qsTr("Undo"), renamed, oldTitle)
        if (shown)
            root.open(renamed, false)
    }
    function remove(path: string): void {
        const shown = root.path === path
        const trashPath = root.chats.remove(path)
        if (trashPath === "")
            return
        AppController.toastRestorePage(qsTr("Conversation moved to trash"), qsTr("Undo"), trashPath)
        if (shown) {
            root.path = ""
            AppController.replaceRoute({ view: "chat" })
        }
    }
    BMenu {
        id: rowMenu
        objectName: "chatRowMenu"
        BMenuItem {
            objectName: "chatRename"
            text: qsTr("Rename…")
            iconName: "pencil"
            onTriggered: {
                renameField.text = root.chats.nameOf(root.menuPath)
                renameDialog.path = root.menuPath
                renameDialog.open()
            }
        }
        BMenuItem {
            objectName: "chatOpenAsPage"
            text: qsTr("Open as a page")
            iconName: "file-text"
            onTriggered: AppController.openPage(root.menuPath)
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "chatDelete"
            text: qsTr("Move to trash")
            iconName: "trash-2"
            danger: true
            onTriggered: root.remove(root.menuPath)
        }
    }
    BDialog {
        id: renameDialog
        objectName: "chatRenameDialog"
        property string path
        title: qsTr("Rename conversation")
        iconName: "pencil"
        acceptText: qsTr("Rename")
        acceptEnabled: Js.trim(renameField.typedText) !== ""
        initialFocusItem: renameField
        onAccepted: root.rename(renameDialog.path, Js.trim(renameField.typedText))
        BText {
            Layout.fillWidth: true
            text: qsTr("Its page keeps the day it began in front; links to it follow the new name.")
            variant: "muted"
            wrapMode: Text.Wrap
        }
        BTextField {
            id: renameField
            objectName: "chatRenameField"
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            onAccepted: if (renameDialog.acceptEnabled) renameDialog.accept()
        }
    }

    Later {
        id: sendLater
        onTriggered: root.send()
    }
    Later {
        id: focusLater
        onTriggered: input.forceActiveFocus()
    }
    Later {
        id: toEnd
        onTriggered: list.positionViewAtEnd()
    }
    Connections {
        target: root.chats
        function onWritten(path: string) {
            if (path !== root.path)
                return
            // At once: the answer's message is a row before the answer being written goes.
            messages.refreshNow()
            toEnd.restart()
        }
    }
    Connections {
        target: root.turn
        function onPartialChanged() {
            if (root.liveShown && root.following)
                toEnd.restart()
        }
    }
    // A change to allow or a failure shows under the last message: in view.
    onAskingChanged: if (root.asking) toEnd.restart()
    onFailedHereChanged: if (root.failedHere) toEnd.restart()

    ChatMessageModel {
        id: messages
        workspace: Workspace
        path: root.path
        locale: DatesHelper.locale
        budget: root.chats.historyBudget
        palette: ({
            link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
            codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
            tagFg: Theme.tagFg, tagBg: Theme.tagBg,
            dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
            overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
            scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
            reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
            alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
            recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
            doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
            neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
        })
        onCountChanged: toEnd.restart()
        onLoaded: root.partnerHere = root.path !== "" ? root.chats.partnerOf(root.path) : ""
    }
    ChatListModel {
        id: conversations
        workspace: Workspace
        locale: DatesHelper.locale
    }
    PageTools {
        id: tools
        workspace: Workspace
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- The conversations ---------------------------------------------------------------
        ColumnLayout {
            id: listPane
            objectName: "chatList"
            visible: root.listShown
            Layout.fillHeight: true
            Layout.fillWidth: !root.wide
            Layout.preferredWidth: root.wide ? 300 : -1
            Layout.maximumWidth: root.wide ? 300 : 100000
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.space3
                spacing: Theme.space2
                BText {
                    Layout.fillWidth: true
                    text: qsTr("Conversations")
                    variant: "heading"
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                BButton {
                    objectName: "chatNew"
                    size: "small"
                    variant: root.wide && root.path === "" ? "secondary" : "primary"
                    iconName: "plus"
                    text: qsTr("New")
                    Accessible.name: qsTr("New conversation")
                    onClicked: root.open("", true)
                }
            }

            ListView {
                id: conversationList
                objectName: "chatConversations"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: conversations
                TouchFlick.enabled: true
                boundsBehavior: Flickable.StopAtBounds
                BRowScrollBar { list: conversationList }
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Conversations")
                delegate: BListItem {
                    id: conversation
                    required property string path
                    required property string title
                    required property string preview
                    required property string dateLabel
                    required property bool fromAi
                    required property bool running
                    required property bool queued
                    objectName: "chatConversation_" + conversation.title
                    width: ListView.view.width
                    iconName: "message-square"
                    text: conversation.title
                    subtitle: (conversation.fromAi ? "🤖 " : "") + conversation.preview
                    selected: conversation.path === root.path
                    selectionBar: true
                    trailing: Item {
                        implicitWidth: conversation.running ? runningMark.implicitWidth : dateText.implicitWidth
                        implicitHeight: Math.max(runningMark.implicitHeight, dateText.implicitHeight)
                        BText {
                            id: dateText
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            visible: !conversation.running
                            text: conversation.dateLabel
                            variant: "caption"
                            color: Theme.fgFaint
                        }
                        RowLayout {
                            id: runningMark
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            visible: conversation.running
                            spacing: Theme.space1
                            BIcon {
                                visible: conversation.queued
                                name: "hourglass"
                                size: Theme.iconSizeSm
                                color: Theme.fgFaint
                            }
                            BSpinner {
                                visible: !conversation.queued
                                running: conversation.running && !conversation.queued && root.visible
                                size: Theme.iconSizeSm
                                calm: true
                            }
                        }
                    }
                    Accessible.name: conversation.title + (conversation.running ? ", " + (conversation.queued ? qsTr("waiting for the model") : qsTr("the AI is answering")) : "")
                    onClicked: root.open(conversation.path, false)
                    onPressAndHold: root.openRowMenu(conversation.path, conversation)
                    TapHandler {
                        // Right-click only (a finger's tap opens the conversation).
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openRowMenu(conversation.path, conversation)
                    }
                }
            }

            BEmptyState {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: Theme.space8
                visible: conversations.count === 0
                compact: root.wide
                iconName: "message-square"
                title: qsTr("No conversations yet")
                description: qsTr("Ask the AI about your notes, or ask it to do something in them. Each conversation is a page in the folder %1.").arg(root.chats.folder())
                actionText: root.wide ? "" : qsTr("New conversation")
                onActionTriggered: root.open("", true)
            }
        }

        Rectangle {
            visible: root.wide
            Layout.fillHeight: true
            implicitWidth: 1
            color: Theme.borderSubtle
        }

        // ---- One conversation ----------------------------------------------------------------
        ColumnLayout {
            id: conversationPane
            objectName: "chatConversation"
            visible: root.conversationShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // What it is about, who answers, and the page it is.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space3
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space2
                visible: root.path !== ""
                spacing: Theme.space2
                // (Its name is the top bar's title, the side panel's header in the panel.)
                BChip {
                    id: partnerChip
                    objectName: "chatPartner"
                    visible: root.choosesPartner || root.partnerHere !== ""
                    Layout.maximumWidth: 200
                    iconName: "sparkles"
                    text: root.partnerName(root.partnerHere)
                    interactive: root.choosesPartner
                    outlined: false
                    Accessible.name: qsTr("Answered by %1").arg(partnerChip.text)
                    onClicked: partnerMenu.popup(partnerChip, 0, partnerChip.height)
                }
                Item {
                    Layout.fillWidth: true
                }
                BChip {
                    visible: messages.page !== ""
                    Layout.maximumWidth: 220
                    iconName: "file-text"
                    text: messages.page
                    interactive: true
                    outlined: false
                    Accessible.name: qsTr("About the page %1").arg(messages.page)
                    onClicked: tools.openLink("page:" + Js.encodeURIComponent(messages.page), root.path, false)
                }
                BIconButton {
                    objectName: "chatOpenPage"
                    label: qsTr("Open the conversation as a page")
                    iconName: "file-text"
                    onClicked: AppController.openPage(root.path)
                }
                BIconButton {
                    objectName: "chatNewHere"
                    visible: !root.listShown
                    label: qsTr("New conversation")
                    iconName: "plus"
                    onClicked: root.open("", true)
                }
            }
            BMenu {
                id: partnerMenu
                objectName: "chatPartnerMenu"
                Instantiator {
                    model: root.partners
                    delegate: BMenuItem {
                        id: partnerItem
                        required property var modelData
                        readonly property string partnerId: AppController.routeText(partnerItem.modelData, "id")
                        objectName: "chatPartnerItem"
                        text: AppController.routeText(partnerItem.modelData, "label")
                        hint: AppController.routeText(partnerItem.modelData, "extension")
                        iconName: partnerItem.partnerId === (root.path !== "" ? root.partnerHere : root.newPartner) ? "check" : ""
                        onTriggered: {
                            if (root.path === "") {
                                root.newPartner = partnerItem.partnerId
                            } else if (root.chats.setPartner(root.path, partnerItem.partnerId)) {
                                root.partnerHere = partnerItem.partnerId
                            }
                        }
                    }
                    onObjectAdded: (index, object) => partnerMenu.insertItem(index, object as Item)
                    onObjectRemoved: (index, object) => partnerMenu.removeItem(object as Item)
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list
                    objectName: "chatMessages"
                    anchors.fill: parent
                    clip: true
                    model: messages
                    spacing: Theme.space2
                    topMargin: Theme.space3
                    bottomMargin: Theme.space3
                    leftMargin: Theme.narrow ? Theme.space3 : Theme.space5
                    rightMargin: Theme.narrow ? Theme.space3 : Theme.space5
                    TouchFlick.enabled: true
                    boundsBehavior: Flickable.StopAtBounds
                    BRowScrollBar { list: list }
                    Accessible.role: Accessible.List
                    Accessible.name: root.title
                    onHeightChanged: if (list.atYEnd) toEnd.restart()
                    onMovementEnded: root.following = list.atYEnd

                    delegate: Item {
                        id: message
                        required property int index
                        required property string sender
                        required property string messageHtml
                        required property string markdown
                        required property int line
                        required property string time
                        required property string day
                        required property list<url> pictures
                        required property list<string> picturePaths
                        required property bool sent
                        required property bool firstSent
                        readonly property bool mine: message.sender === "user"
                        property string hoveredLink
                        property point linkPoint
                        readonly property real maxWidth: Math.min(message.width * (message.mine ? 0.82 : 0.92), 720)
                        readonly property real picturesWidth: message.pictures.length > 0 ? Math.min(message.maxWidth - 2 * Theme.space3, 280) : 0
                        width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                        height: messageColumn.height

                        function trackLink(pos: point): void {
                            message.linkPoint = body.mapFromItem(message, pos.x, pos.y)
                            const link = body.linkAt(message.linkPoint.x, message.linkPoint.y)
                            const h = Js.startsWith(link, "task:") ? "" : link
                            if (h !== message.hoveredLink)
                                message.hoveredLink = h
                        }

                        Column {
                            id: messageColumn
                            width: parent.width
                            spacing: Theme.space1

                            // The day, above its first message.
                            BText {
                                objectName: "chatDay"
                                width: parent.width
                                visible: message.day !== ""
                                topPadding: message.index > 0 ? Theme.space2 : 0
                                bottomPadding: Theme.space1
                                horizontalAlignment: Text.AlignHCenter
                                text: message.day
                                variant: "caption"
                                color: Theme.fgFaint
                                font.weight: Font.DemiBold
                            }
                            // The model does not get the messages above (they do not fit it).
                            RowLayout {
                                objectName: "chatNotSent"
                                width: parent.width
                                visible: message.firstSent
                                spacing: Theme.space2
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.border
                                }
                                BText {
                                    Layout.maximumWidth: message.width * 0.8
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root.chats.onDevice
                                          ? qsTr("The model on this device does not get the %n message(s) above: too long for it", "", messages.omitted)
                                          : qsTr("The AI does not get the %n message(s) above: too long", "", messages.omitted)
                                    variant: "caption"
                                    color: Theme.fgMuted
                                    wrapMode: Text.Wrap
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 1
                                    color: Theme.border
                                }
                            }

                            Item {
                                width: parent.width
                                height: bubble.height

                                Rectangle {
                                    id: bubble
                                    objectName: message.mine ? "chatMessageUser" : "chatMessageAi"
                                    x: message.mine ? parent.width - width : 0
                                    width: Math.min(message.maxWidth, Math.max(body.visible ? body.implicitWidth : 0, message.picturesWidth, 24) + 2 * Theme.space3)
                                    height: bubbleColumn.height + 2 * Theme.space2 + 2
                                    radius: Theme.radiusXl
                                    color: message.mine ? Theme.accentBg : Theme.bgElevated
                                    border.color: message.mine ? "transparent" : Theme.borderSubtle
                                    opacity: message.sent ? 1 : 0.6
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: (message.mine ? qsTr("You") : qsTr("AI")) + (message.time !== "" ? " " + message.time : "") + ": " + message.markdown

                                    Column {
                                        id: bubbleColumn
                                        x: Theme.space3
                                        y: Theme.space2 + 1
                                        width: bubble.width - 2 * Theme.space3
                                        spacing: Theme.space2
                                        // Its pictures (a tap opens one).
                                        Flow {
                                            width: parent.width
                                            visible: message.pictures.length > 0
                                            spacing: Theme.space1
                                            Repeater {
                                                model: message.pictures.length
                                                delegate: Image {
                                                    id: picture
                                                    required property int index
                                                    objectName: "chatPicture"
                                                    width: message.pictures.length === 1 ? message.picturesWidth : (message.picturesWidth - Theme.space1) / 2
                                                    height: Math.min(width * 0.75, picture.implicitHeight > 0 && picture.implicitWidth > 0 ? width * picture.implicitHeight / picture.implicitWidth : width * 0.75)
                                                    source: message.pictures[picture.index]
                                                    sourceSize.width: 560
                                                    fillMode: Image.PreserveAspectCrop
                                                    asynchronous: true
                                                    cache: true
                                                    smooth: true
                                                    Accessible.role: Accessible.Graphic
                                                    Accessible.name: qsTr("Picture %1").arg(message.picturePaths[picture.index])
                                                    TapHandler {
                                                        onTapped: AppController.openPage(message.picturePaths[picture.index])
                                                    }
                                                }
                                            }
                                        }
                                        BRichText {
                                            id: body
                                            width: parent.width
                                            visible: message.messageHtml !== ""
                                            html: message.messageHtml
                                            hoveredLink: message.hoveredLink
                                            font.family: Theme.fontEditor
                                            font.pixelSize: Theme.textBase
                                            color: Theme.fg
                                        }
                                    }
                                }

                                HoverHandler {
                                    id: hover
                                    enabled: !Theme.touchUi
                                    cursorShape: message.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onPointChanged: message.trackLink(hover.point.position)
                                    onHoveredChanged: if (!hovered) message.hoveredLink = ""
                                }
                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    gesturePolicy: TapHandler.ReleaseWithinBounds
                                    onTapped: (eventPoint, button) => {
                                        message.linkPoint = body.mapFromItem(message, eventPoint.position.x, eventPoint.position.y)
                                        const link = body.linkAt(message.linkPoint.x, message.linkPoint.y)
                                        if (link !== "" && !Js.startsWith(link, "task:"))
                                            tools.openLink(link, root.path, false)
                                    }
                                    onLongPressed: messageMenu.popup()
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: messageMenu.popup()
                                }
                                BMenu {
                                    id: messageMenu
                                    BMenuItem {
                                        objectName: "chatMessageCopy"
                                        text: qsTr("Copy")
                                        iconName: "copy"
                                        onTriggered: {
                                            if (tools.copyText(message.markdown))
                                                AppController.toast(qsTr("Copied"), "success")
                                        }
                                    }
                                    BMenuItem {
                                        text: qsTr("Show in the page")
                                        iconName: "file-text"
                                        onTriggered: AppController.openPage(root.path, message.line)
                                    }
                                }
                            }

                            // When it was written.
                            BText {
                                objectName: "chatTime"
                                width: parent.width
                                visible: message.time !== ""
                                horizontalAlignment: message.mine ? Text.AlignRight : Text.AlignLeft
                                leftPadding: message.mine ? 0 : Theme.space3
                                rightPadding: message.mine ? Theme.space3 : 0
                                text: message.time
                                variant: "caption"
                                color: Theme.fgFaint
                            }
                        }
                    }

                    footer: Item {
                        width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                        height: footerColumn.implicitHeight + (footerColumn.implicitHeight > 0 ? Theme.space2 : 0)
                        // (What the AI does grows the end: kept in view while the list follows it.)
                        onHeightChanged: if (root.following && root.busyHere) toEnd.restart()
                        ColumnLayout {
                            id: footerColumn
                            y: Theme.space2
                            width: parent.width
                            spacing: Theme.space2

                            // The answer as the model writes it — above it, what the model thinks.
                            Rectangle {
                                id: live
                                objectName: "chatLive"
                                readonly property bool writing: root.liveShown && root.turn.partial !== ""
                                readonly property bool thinking: root.liveShown && root.turn.thoughts !== ""
                                readonly property real maxWidth: Math.min(footerColumn.width * 0.92, 720)
                                visible: root.liveShown
                                Layout.preferredWidth: Math.min(live.maxWidth, Math.max(live.writing ? Math.max(liveSettled.implicitWidth, liveTail.implicitWidth) : 0,
                                                                                        live.thinking ? (root.thoughtsOpen ? liveThoughts.implicitWidth : thoughtsHeader.implicitWidth) : 0, 24) + 2 * Theme.space3)
                                implicitHeight: liveColumn.implicitHeight + 2 * Theme.space2 + 2
                                radius: Theme.radiusXl
                                color: Theme.bgElevated
                                border.color: Theme.borderSubtle
                                Accessible.role: Accessible.StaticText
                                Accessible.name: live.writing ? qsTr("AI") + ": " + root.turn.partial : qsTr("The AI thinks: %1").arg(root.liveShown ? root.turn.thoughts : "")

                                Column {
                                    id: liveColumn
                                    x: Theme.space3
                                    y: Theme.space2 + 1
                                    width: live.width - 2 * Theme.space3
                                    spacing: Theme.space2

                                    // What the model thinks, collapsed to a line of its end; a tap
                                    // opens all of it.
                                    Item {
                                        id: thoughtsHeader
                                        objectName: "chatThoughtsToggle"
                                        width: parent.width
                                        visible: live.thinking
                                        implicitWidth: thoughtsRow.implicitWidth
                                        implicitHeight: thoughtsRow.implicitHeight
                                        height: thoughtsRow.implicitHeight
                                        Accessible.role: Accessible.Button
                                        Accessible.name: root.thoughtsOpen ? qsTr("Hide the AI's thoughts") : qsTr("Show the AI's thoughts")
                                        RowLayout {
                                            id: thoughtsRow
                                            width: parent.width
                                            spacing: Theme.space1
                                            BIcon {
                                                name: root.thoughtsOpen ? "chevron-down" : "chevron-right"
                                                size: Theme.iconSizeSm
                                                color: Theme.fgMuted
                                            }
                                            BText {
                                                text: live.writing ? qsTr("Thoughts") : qsTr("Thinking…")
                                                variant: "caption"
                                                color: Theme.fgMuted
                                                font.weight: Font.DemiBold
                                            }
                                            BText {
                                                id: thoughtsLine
                                                objectName: "chatThoughts"
                                                Layout.fillWidth: true
                                                visible: !root.thoughtsOpen
                                                // Plain text: the model's own words, never markup.
                                                text: root.liveShown ? root.turn.thoughts : ""
                                                mutabilityGroup: Item.DynamicMutabilityGroup
                                                textFormat: Text.PlainText
                                                variant: "muted"
                                                font.italic: true
                                                font.pixelSize: Theme.textSm
                                                elide: Text.ElideLeft
                                                maximumLineCount: 1
                                            }
                                        }
                                        TapHandler {
                                            onTapped: root.thoughtsOpen = !root.thoughtsOpen
                                        }
                                    }
                                    BText {
                                        id: liveThoughts
                                        objectName: "chatThoughtsAll"
                                        width: parent.width
                                        visible: live.thinking && root.thoughtsOpen
                                        text: live.thinking && root.thoughtsOpen ? root.turn.allThoughts : ""
                                        mutabilityGroup: Item.DynamicMutabilityGroup
                                        textFormat: Text.PlainText
                                        variant: "muted"
                                        font.italic: true
                                        font.pixelSize: Theme.textSm
                                        wrapMode: Text.Wrap
                                    }

                                    // Its finished blocks, rendered when one is finished; the block
                                    // being written, rendered at every change — alone (the docs'
                                    // "Calculating text layouts can be a slow operation"), and kept
                                    // out of the others' batches (Item.DynamicMutabilityGroup).
                                    Column {
                                        id: liveBody
                                        objectName: "chatPartial"
                                        width: parent.width
                                        visible: live.writing
                                        spacing: liveSettled.html !== "" ? 10 : 0 // (a paragraph's gap, as renderMessage's)
                                        BRichText {
                                            id: liveSettled
                                            objectName: "chatPartialSettled"
                                            width: parent.width
                                            visible: liveSettled.html !== ""
                                            html: live.writing && root.turn.partialSettled !== "" ? messages.render(root.turn.partialSettled) : ""
                                            font.family: Theme.fontEditor
                                            font.pixelSize: Theme.textBase
                                            color: Theme.fg
                                        }
                                        BRichText {
                                            id: liveTail
                                            objectName: "chatPartialTail"
                                            width: parent.width
                                            html: live.writing ? messages.render(root.turn.partialTail) : ""
                                            mutability: Item.DynamicMutabilityGroup
                                            font.family: Theme.fontEditor
                                            font.pixelSize: Theme.textBase
                                            color: Theme.fg
                                        }
                                    }
                                }
                            }

                            // How fast the model writes (the model on the device: it knows).
                            BText {
                                objectName: "chatSpeed"
                                Layout.leftMargin: Theme.space3
                                visible: root.liveShown && root.turn.speed !== ""
                                text: root.liveShown ? root.turn.speed : ""
                                variant: "caption"
                                color: Theme.fgFaint
                                Accessible.name: qsTr("The AI writes %1").arg(text)
                            }

                            // What the AI does now (not while its words come: they show it, and a
                            // spinner's frames would take the processor from a model on the
                            // device), or its place in line for the model on the device.
                            RowLayout {
                                objectName: "chatWorking"
                                Layout.fillWidth: true
                                visible: root.busyHere && !root.asking && (root.queuedHere || root.turn.status !== "" || !root.liveShown)
                                spacing: Theme.space2
                                Rectangle {
                                    Layout.maximumWidth: Math.min(parent.width, 520)
                                    Layout.fillWidth: true
                                    implicitHeight: workingRow.implicitHeight + 2 * Theme.space2
                                    radius: Theme.radiusXl
                                    color: Theme.bgElevated
                                    border.color: Theme.borderSubtle
                                    RowLayout {
                                        id: workingRow
                                        x: Theme.space3
                                        y: Theme.space2
                                        width: parent.width - 2 * Theme.space3
                                        spacing: Theme.space2
                                        BIcon {
                                            visible: root.queuedHere
                                            name: "hourglass"
                                            size: Theme.iconSizeSm
                                            color: Theme.fgMuted
                                        }
                                        BSpinner {
                                            visible: !root.queuedHere
                                            running: root.busyHere && !root.queuedHere && root.visible
                                            size: Theme.iconSizeSm
                                            calm: true
                                        }
                                        BText {
                                            objectName: "chatStatus"
                                            Layout.fillWidth: true
                                            text: root.queuedHere
                                                  ? (root.turn.place <= 1 ? qsTr("Waiting for the model on this device: it answers another conversation first.")
                                                                          : qsTr("Waiting for the model on this device: %1 conversations are answered first.").arg(root.turn.place))
                                                  : root.busyHere && root.turn.status !== "" ? root.turn.status : qsTr("The AI is answering…")
                                            variant: "muted"
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // A change to allow: the question of this conversation's turn.
                            Rectangle {
                                objectName: "chatQuestion"
                                Layout.fillWidth: true
                                Layout.maximumWidth: 640
                                visible: root.asking
                                implicitHeight: questionColumn.implicitHeight + 2 * Theme.space3
                                radius: Theme.radiusLg
                                color: Theme.bgElevated
                                border.color: Theme.accent
                                ColumnLayout {
                                    id: questionColumn
                                    x: Theme.space3
                                    y: Theme.space3
                                    width: parent.width - 2 * Theme.space3
                                    spacing: Theme.space2
                                    BText {
                                        Layout.fillWidth: true
                                        text: root.questionTitleEnd >= 0 ? Js.slice(root.questionText, 0, root.questionTitleEnd) : root.questionText
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.Wrap
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        visible: root.questionTitleEnd >= 0
                                        implicitHeight: questionDetails.implicitHeight + 2 * Theme.space2
                                        radius: Theme.radius
                                        color: Theme.bgSunken
                                        BText {
                                            id: questionDetails
                                            objectName: "chatQuestionDetails"
                                            x: Theme.space2
                                            y: Theme.space2
                                            width: parent.width - 2 * Theme.space2
                                            // Plain text: it quotes notes.
                                            text: root.questionTitleEnd >= 0 ? Js.trim(Js.sliceFrom(root.questionText, root.questionTitleEnd + 1)) : ""
                                            textFormat: Text.PlainText
                                            font.family: Theme.fontMono
                                            font.pixelSize: Theme.textSm
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 120
                                            elide: Text.ElideRight
                                        }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: Theme.space2
                                        Repeater {
                                            model: root.questionOptions
                                            delegate: BButton {
                                                id: option
                                                required property int index
                                                required property string modelData
                                                objectName: "chatQuestionOption"
                                                text: option.modelData
                                                variant: option.index === 0 ? "primary" : option.index === 1 ? "secondary" : "ghost"
                                                onClicked: Workspace.extensions.answerChat(root.path, option.modelData)
                                            }
                                        }
                                    }
                                }
                            }

                            // A turn that failed.
                            Rectangle {
                                objectName: "chatFailure"
                                Layout.fillWidth: true
                                Layout.maximumWidth: 640
                                visible: root.failedHere
                                implicitHeight: failureRow.implicitHeight + 2 * Theme.space2
                                radius: Theme.radiusLg
                                color: Theme.dangerBg
                                RowLayout {
                                    id: failureRow
                                    x: Theme.space3
                                    y: Theme.space2
                                    width: parent.width - 2 * Theme.space3
                                    spacing: Theme.space2
                                    BIcon {
                                        name: "circle-alert"
                                        size: Theme.iconSize
                                        color: Theme.dangerText
                                    }
                                    BText {
                                        Layout.fillWidth: true
                                        text: root.failure
                                        color: Theme.dangerText
                                        wrapMode: Text.Wrap
                                    }
                                    BButton {
                                        objectName: "chatRetry"
                                        visible: root.path !== ""
                                        size: "small"
                                        text: qsTr("Try again")
                                        iconName: "rotate-cw"
                                        enabled: !root.busyHere
                                        onClicked: root.chats.retry(root.path)
                                    }
                                    BIconButton {
                                        size: "small"
                                        label: qsTr("Dismiss")
                                        iconName: "x"
                                        onClicked: root.chats.dismissFailure(root.path)
                                    }
                                }
                            }
                        }
                    }
                }

                // Pictures dropped on the conversation go with the next message.
                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (event) => {
                        root.attachFiles(event.urls)
                        event.acceptProposedAction()
                    }
                }
                Rectangle {
                    objectName: "chatDropHint"
                    anchors.fill: parent
                    anchors.margins: Theme.space2
                    visible: drop.containsDrag
                    radius: Theme.radiusLg
                    color: Theme.accentBg
                    opacity: 0.92
                    border.color: Theme.accent
                    border.width: 2
                    BText {
                        anchors.centerIn: parent
                        text: qsTr("Drop pictures to send them with the message")
                        font.weight: Font.DemiBold
                    }
                }
            }

            // A new conversation: what to ask.
            ColumnLayout {
                objectName: "chatEmpty"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space4
                Layout.rightMargin: Theme.space4
                Layout.bottomMargin: Theme.space4
                visible: root.path === "" && root.conversationShown
                spacing: Theme.space3
                BEmptyState {
                    Layout.alignment: Qt.AlignHCenter
                    iconName: "sparkles"
                    title: qsTr("Ask about your notes")
                    description: qsTr("The AI searches and reads the whole notebook, looks at its pictures and, with your yes, changes it. The conversation is kept as a page.")
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 560
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.space2
                    BChip {
                        objectName: "chatSuggestSummary"
                        visible: root.page !== ""
                        interactive: true
                        iconName: "scroll-text"
                        text: qsTr("Summarise this page")
                        onClicked: root.suggest(qsTr("Summarise this page in a few sentences and list its most important tasks."))
                    }
                    BChip {
                        interactive: true
                        iconName: "list-todo"
                        text: qsTr("What do I have to do this week?")
                        onClicked: root.suggest(qsTr("What do I have to do this week?"))
                    }
                    BChip {
                        interactive: true
                        iconName: "notebook-pen"
                        text: qsTr("What did I write yesterday?")
                        onClicked: root.suggest(qsTr("What did I write yesterday?"))
                    }
                    BChip {
                        interactive: true
                        iconName: "calendar"
                        text: qsTr("Plan my tomorrow")
                        onClicked: root.suggest(qsTr("Plan my tomorrow from my tasks and reminders."))
                    }
                }
            }

            // ---- What to send ---------------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.borderSubtle
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.narrow ? Theme.space2 : Theme.space3
                spacing: Theme.space2

                // A new conversation: the page that goes along, who answers.
                Flow {
                    Layout.fillWidth: true
                    visible: root.path === "" && (root.page !== "" || root.choosesPartner)
                    spacing: Theme.space2
                    BChip {
                        objectName: "chatPage"
                        visible: root.page !== ""
                        width: Math.min(implicitWidth, 260)
                        iconName: "file-text"
                        text: root.page
                        removable: true
                        Accessible.name: qsTr("The page %1 goes along").arg(root.page)
                        onRemoveClicked: root.page = ""
                    }
                    BText {
                        visible: root.page !== ""
                        topPadding: Theme.space1
                        text: qsTr("goes along with the question")
                        variant: "caption"
                        color: Theme.fgFaint
                    }
                    BChip {
                        id: newPartnerChip
                        objectName: "chatNewPartner"
                        visible: root.choosesPartner
                        iconName: "sparkles"
                        text: qsTr("Answers: %1").arg(root.partnerName(root.newPartner))
                        interactive: true
                        Accessible.name: qsTr("Who answers: %1").arg(root.partnerName(root.newPartner))
                        onClicked: partnerMenu.popup(newPartnerChip, 0, newPartnerChip.height)
                    }
                }

                // The pictures going with the message.
                Flow {
                    objectName: "chatAttached"
                    Layout.fillWidth: true
                    visible: root.attached.length > 0
                    spacing: Theme.space2
                    Repeater {
                        model: root.attached
                        delegate: Rectangle {
                            id: thumb
                            required property string modelData
                            objectName: "chatAttachedPicture"
                            width: 64
                            height: 64
                            radius: Theme.radius
                            color: Theme.bgSunken
                            border.color: Theme.border
                            clip: true
                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: Workspace.assetUrl(thumb.modelData)
                                sourceSize.width: 128
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            BIconButton {
                                objectName: "chatDetachPicture"
                                anchors.top: parent.top
                                anchors.right: parent.right
                                size: "small"
                                variant: "secondary"
                                label: qsTr("Take the picture off")
                                iconName: "x"
                                onClicked: root.detach(thumb.modelData)
                            }
                        }
                    }
                    BText {
                        visible: !root.chats.seesPictures
                        width: Math.min(implicitWidth, 320)
                        text: qsTr("The model on this device does not see pictures: it gets only their names.")
                        variant: "caption"
                        color: Theme.warningText
                        wrapMode: Text.Wrap
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    // A picture: a file, or a photo from the camera.
                    BIconButton {
                        id: attachButton
                        objectName: "chatAttach"
                        Layout.alignment: Qt.AlignBottom
                        visible: picturePicker.available || root.chats.cameraAvailable
                        label: qsTr("Add a picture")
                        iconName: "paperclip"
                        onClicked: {
                            if (root.chats.cameraAvailable)
                                attachMenu.popup(attachButton, 0, -attachMenu.implicitHeight - Theme.space1)
                            else
                                picturePicker.open()
                        }
                    }
                    BMenu {
                        id: attachMenu
                        objectName: "chatAttachMenu"
                        BMenuItem {
                            objectName: "chatTakePhoto"
                            text: qsTr("Take a photo")
                            iconName: "camera"
                            onTriggered: root.takePhoto()
                        }
                        BMenuItem {
                            objectName: "chatPickPicture"
                            visible: picturePicker.available
                            height: visible ? implicitHeight : 0
                            text: qsTr("Picture from a file…")
                            iconName: "image"
                            onTriggered: picturePicker.open()
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        implicitHeight: Math.min(input.implicitHeight, 168) + 2
                        radius: Theme.radiusLg
                        color: Theme.bgElevated
                        border.color: root.chats.listening && root.dictating ? Theme.danger : input.activeFocus ? Theme.accent : Theme.border
                        Flickable {
                            id: inputFlick
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            contentWidth: width
                            contentHeight: input.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            interactive: contentHeight > height
                            function keepCaret(): void {
                                const r = input.cursorRectangle
                                if (r.y < inputFlick.contentY)
                                    inputFlick.contentY = r.y
                                else if (r.y + r.height + input.bottomPadding > inputFlick.contentY + inputFlick.height)
                                    inputFlick.contentY = r.y + r.height + input.bottomPadding - inputFlick.height
                            }
                            BTextArea {
                                id: input
                                objectName: "chatInput"
                                width: inputFlick.width
                                background: null
                                wrapMode: TextEdit.Wrap
                                placeholderText: root.chats.listening && root.dictating ? qsTr("Listening…")
                                                 : root.queuedHere ? qsTr("Waiting for the model on this device…")
                                                 : root.busyHere ? qsTr("The AI is answering…")
                                                 : qsTr("Ask, or ask to do something…")
                                accessibleName: qsTr("Message to the AI")
                                onCursorRectangleChanged: inputFlick.keepCaret()
                                // Enter sends (Shift+Enter: a new line); on touch the keyboard's Enter
                                // is a new line and the button sends. Ctrl+V with a picture on the
                                // clipboard: it goes with the message.
                                Keys.onPressed: (event) => {
                                    const key = event.key
                                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                                    if ((key === Qt.Key_Return || key === Qt.Key_Enter) && !shift && !Theme.touchUi) {
                                        event.accepted = true
                                        root.send()
                                    } else if (event.matches(StandardKey.Paste) && root.pastePicture()) {
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                    }
                    // Dictation: the system's speech recognizer (or the app's own).
                    BIconButton {
                        objectName: "chatDictate"
                        Layout.alignment: Qt.AlignBottom
                        visible: root.chats.dictationAvailable
                        variant: root.chats.listening && root.dictating ? "secondary" : "ghost"
                        iconColor: root.chats.listening && root.dictating ? Theme.danger : Theme.fgMuted
                        label: root.chats.listening && root.dictating ? qsTr("Stop dictating") : qsTr("Dictate")
                        iconName: "mic"
                        onClicked: root.toggleDictation()
                    }
                    BButton {
                        objectName: root.busyHere ? "chatStop" : "chatSend"
                        Layout.alignment: Qt.AlignBottom
                        variant: root.busyHere ? "secondary" : "primary"
                        iconName: root.busyHere ? "square" : "arrow-up"
                        enabled: root.busyHere || Js.trim(input.typedText) !== "" || root.attached.length > 0
                        Accessible.name: root.busyHere ? qsTr("Stop") : qsTr("Send")
                        onClicked: {
                            if (root.busyHere)
                                root.chats.stop(root.path)
                            else
                                root.send()
                        }
                    }
                }
            }
        }
    }
}
