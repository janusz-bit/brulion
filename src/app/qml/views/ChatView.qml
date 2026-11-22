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
// shown or waiting behind another conversation's, or an AI agent's: Workspace.chats.asking), a failed
// turn with Try again; at the bottom the field (Enter sends with a keyboard, Shift+Enter is a new
// line; on touch the send button), the pictures going with the message (a file, the clipboard, a
// drop, the camera), dictation, and — before a new conversation's first message — the page it is
// about as a chip that can be taken away and who answers (the built-in assistant, an extension's
// chat command, or an AI agent of this device: Workspace.chats.partners()).
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
    readonly property int questionSerial: root.chats.questionSerial
    readonly property bool asking: root.chats.asking(root.path, root.questionSerial)
    readonly property string questionText: root.asking ? root.chats.questionText(root.path, root.questionSerial) : ""
    readonly property list<string> questionOptions: root.asking ? root.chats.questionOptions(root.path, root.questionSerial) : root.noStrings
    readonly property int questionTitleEnd: Js.indexOf(root.questionText, "\n")
    /// The change of a file an AI agent asks a yes for ({path, oldText, newText}; {} none).
    readonly property var questionDiff: root.asking ? root.chats.questionDiff(root.path, root.questionSerial) : root.noDiff
    readonly property var noDiff: ({})
    readonly property bool questionHasDiff: AppController.routeText(root.questionDiff, "path") !== ""
    readonly property list<string> noStrings: []
    readonly property list<var> noVars: []
    readonly property string failure: root.turn !== null ? root.turn.failure : ""
    readonly property bool failedHere: root.failure !== ""
    /// Messages written while this conversation's turn runs: they wait for it ({id, text,
    /// pictures}); held after a stop or a failure (Resume).
    readonly property list<var> waitingHere: root.path !== "" ? root.chats.queued(root.path, root.chats.queueSerial) : root.noVars
    readonly property bool heldHere: root.path !== "" && root.chats.queueHeld(root.path, root.chats.queueSerial)
    /// A plan an AI agent asks to approve (Markdown; "" none).
    readonly property string planHere: root.asking ? root.chats.questionPlan(root.path, root.questionSerial) : ""
    /// The files the agent's last turn changed ({path, oldText, newText, existed, exists, known}).
    readonly property list<var> changesHere: root.path !== "" && !root.busyHere ? root.chats.changes(root.path, root.chats.changesSerial) : root.noVars
    /// How an agent's questions are answered here: "ask", "edits", "all".
    readonly property string approvalsHere: root.chats.approvals(root.path, root.chats.agentSerial)
    /// The conversation this one was forked from ("" none).
    property string forkSource
    /// Up in an empty field: the questions asked before, newest first (recallIndex: the one
    /// shown, -1 none).
    property list<string> recallList
    property int recallIndex: -1

    /// The list follows the answer being written while it is at its end (scrolled up: it stays).
    property bool following: true
    /// The answer as it is written: what the model thinks first, then its words.
    readonly property bool liveShown: root.busyHere && !root.queuedHere && !root.asking
                                      && (root.turn.partial !== "" || root.turn.thoughts !== "")
    /// The model's thoughts are opened (all of them), else one line of their end.
    property bool thoughtsOpen: false
    /// Who may answer: the assistant, the extensions' chat commands and the AI agents ({id, label,
    /// extension}).
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
        root.keepDraft()
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
        // What was written and not sent stays with its conversation.
        root.keepDraft()
        root.path = AppController.routeText(root.route, "path")
        root.loadDraft()
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
        root.partners = root.chats.partners()
    }
    Connections {
        target: root.chats
        function onPartnersChanged() { root.refreshPartners() }
    }
    /// A list of strings of a map (a result's).
    function routeList(map: var, key: string): list<string> {
        return AppController.routeStrings(map, key)
    }
    /// `pictures` go with the message too.
    function attachAll(pictures: list<string>): void {
        root.attached = Js.uniqueStrings(Js.concatStrings(root.attached, pictures))
    }
    /// The approvals' mode in words ("ask", "edits", "all").
    function approvalsLabel(mode: string): string {
        return mode === "all" ? qsTr("Allow everything") : mode === "edits" ? qsTr("Allow note edits") : qsTr("Ask first")
    }
    /// A partner's name as the chat shows it (one not running: its id).
    function partnerName(id: string): string {
        const label = root.chats.partnerLabel(id)
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
        if (text === "" && root.attached.length === 0)
            return
        // While a turn runs (or messages wait): it waits for the answer, then goes.
        if (root.path !== "" && (root.busyHere || root.waitingHere.length > 0)) {
            if (root.chats.queue(root.path, text, root.attached))
                root.clearField()
            return
        }
        const sent = root.chats.send(root.path, text, root.path === "" ? root.page : "", root.attached,
                                     root.path === "" ? root.newPartner : "")
        if (sent === "")
            return
        root.clearField()
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
    /// The field's text as the draft of the conversation shown (`draftPath`).
    property string draftPath
    property bool draftLoaded: false
    function keepDraft(): void {
        if (root.draftLoaded)
            root.chats.setDraft(root.draftPath, input.text)
    }
    function loadDraft(): void {
        root.draftPath = root.path
        root.draftLoaded = true
        input.text = root.chats.draft(root.path)
        input.cursorPosition = input.length
        root.recallIndex = -1
    }
    function clearField(): void {
        input.text = ""
        root.attached = root.noStrings
        root.recallIndex = -1
        toEnd.restart()
    }
    /// Ctrl+Enter while a turn runs: the message now — the turn stops, it is answered first.
    function steer(): void {
        const text = Js.trim(input.typedText)
        if (text === "" && root.attached.length === 0)
            return
        if (root.chats.steer(root.path, text, root.attached))
            root.clearField()
    }
    /// Up / Down in the field: the questions asked before (Up from an empty field, or from one
    /// brought back).
    function recall(older: bool): bool {
        if (root.path === "")
            return false
        const shown = root.recallIndex >= 0 && root.recallIndex < root.recallList.length
                      && input.text === root.recallList[root.recallIndex]
        if (input.text !== "" && !shown)
            return false
        if (!shown)
            root.recallList = root.chats.prompts(root.path)
        const next = older ? (shown ? root.recallIndex + 1 : 0) : root.recallIndex - 1
        if (older && next >= root.recallList.length)
            return shown // (the oldest: it stays)
        if (!older && !shown)
            return false
        root.recallIndex = next
        input.text = next >= 0 ? root.recallList[next] : ""
        input.cursorPosition = input.length
        return true
    }
    /// A page named at the caret: its link in place of what was typed ("@Do" → "[[Dom]] ").
    function insertMention(title: string): void {
        const caret = 0 + input.cursorPosition
        const typed = "" + input.text
        const next = "" + root.chats.withMention(typed, caret, title)
        if (next === "")
            return
        input.text = next
        input.cursorPosition = root.chats.mentionEnd(typed, caret, title)
        input.forceActiveFocus()
    }
    /// A message's text as a quote at the field's start.
    function quoteMessage(markdown: string): void {
        const quote = root.chats.quote(markdown)
        if (quote === "")
            return
        input.insert(0, quote)
        input.cursorPosition = input.length
        input.forceActiveFocus()
    }
    /// "Edit from here": the message and those after it out of the conversation, its text back in
    /// the field (Undo in the toast).
    function rewindTo(line: int): void {
        const text = "" + root.chats.rewind(root.path, line)
        if (text === "")
            return
        input.text = Js.trim(text)
        root.attached = root.chats.takenPictures()
        root.recallIndex = -1
        input.cursorPosition = input.length
        input.forceActiveFocus()
    }
    /// A waiting message back in the field, to change it (sent again with Enter).
    function editWaiting(id: int): void {
        const text = "" + root.chats.unqueue(root.path, id, true)
        const before = "" + Js.trim(input.text)
        input.text = before === "" ? text : before + "\n\n" + text
        root.attachAll(root.chats.takenPictures())
        input.cursorPosition = input.length
        input.forceActiveFocus()
    }
    /// The files of the agent's last turn back as they were.
    function undoChanges(): void {
        AppController.toast(root.chats.undoChanges(root.path), "info")
    }
    /// "Fork from here": a new conversation up to the message, opened.
    function forkAt(line: int): void {
        const forked = "" + root.chats.fork(root.path, line)
        if (forked === "") {
            AppController.toast(qsTr("The conversation could not be forked."), "error")
            return
        }
        AppController.toast(qsTr("Forked: the new conversation goes on by itself"), "success")
        root.open(forked, false)
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
        onLoaded: {
            root.partnerHere = root.path !== "" ? root.chats.partnerOf(root.path) : ""
            root.forkSource = root.path !== "" ? root.chats.forkedFrom(root.path) : ""
        }
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
                // How an AI agent's questions are answered here (T3 Code's permission modes).
                BChip {
                    id: approvalsChip
                    objectName: "chatApprovals"
                    visible: root.chats.isAgent(root.partnerHere)
                    iconName: "shield-check"
                    text: root.approvalsLabel(root.approvalsHere)
                    interactive: true
                    outlined: false
                    Accessible.name: qsTr("Approvals: %1").arg(approvalsChip.text)
                    onClicked: approvalsMenu.popup(approvalsChip, 0, approvalsChip.height)
                }
                BChip {
                    objectName: "chatForkSource"
                    visible: root.forkSource !== ""
                    Layout.maximumWidth: 220
                    iconName: "git-branch"
                    text: qsTr("Forked from %1").arg(root.chats.nameOf(root.forkSource))
                    interactive: true
                    outlined: false
                    onClicked: root.open(root.forkSource, false)
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
            // An AI agent's options (its model, mode …) and how much of its context is used.
            ChatAgentOptions {
                objectName: "chatAgentOptions"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space3
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space1
                path: root.path
                partner: root.partnerHere
            }
            BMenu {
                id: approvalsMenu
                objectName: "chatApprovalsMenu"
                BMenuItem {
                    objectName: "chatApprovalsAsk"
                    text: root.approvalsLabel("ask")
                    hint: qsTr("each change and command waits for your answer")
                    iconName: root.approvalsHere === "ask" ? "check" : ""
                    onTriggered: root.chats.setApprovals(root.path, "ask")
                }
                BMenuItem {
                    objectName: "chatApprovalsEdits"
                    text: root.approvalsLabel("edits")
                    hint: qsTr("changes of notes go by themselves; commands ask")
                    iconName: root.approvalsHere === "edits" ? "check" : ""
                    onTriggered: root.chats.setApprovals(root.path, "edits")
                }
                BMenuItem {
                    objectName: "chatApprovalsAll"
                    text: root.approvalsLabel("all")
                    hint: qsTr("nothing asks but a plan and a sign-in")
                    iconName: root.approvalsHere === "all" ? "check" : ""
                    onTriggered: root.chats.setApprovals(root.path, "all")
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
                                            measured: true // (the bubble is as wide as its text)
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
                                        objectName: "chatMessageQuote"
                                        text: qsTr("Quote")
                                        iconName: "quote"
                                        onTriggered: root.quoteMessage(message.markdown)
                                    }
                                    BMenuItem {
                                        objectName: "chatMessageEdit"
                                        visible: message.mine
                                        height: visible ? implicitHeight : 0
                                        text: qsTr("Edit from here")
                                        hint: qsTr("this and what follows come out")
                                        iconName: "pencil"
                                        onTriggered: root.rewindTo(message.line)
                                    }
                                    BMenuItem {
                                        objectName: "chatMessageFork"
                                        text: qsTr("Fork from here")
                                        hint: qsTr("a new conversation")
                                        iconName: "git-branch"
                                        onTriggered: root.forkAt(message.line)
                                    }
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

                            // The files the agent's last turn changed, with Undo (T3 Code's turn diff).
                            ChatChanges {
                                objectName: "chatChanges"
                                Layout.fillWidth: true
                                Layout.maximumWidth: 640
                                visible: root.changesHere.length > 0
                                changes: root.changesHere
                                onOpenFile: (file) => AppController.openPage(file)
                                onUndo: root.undoChanges()
                                onDismiss: root.chats.forgetChanges(root.path)
                            }

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
                                            measured: true // (the bubble is as wide as its text)
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
                                            measured: true // (the bubble is as wide as its text)
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

                            // What an AI agent does: its plan, its tool calls with their output.
                            ChatActivity {
                                objectName: "chatActivity"
                                Layout.fillWidth: true
                                Layout.maximumWidth: 640
                                tools: root.busyHere && !root.queuedHere ? root.turn.tools : root.noVars
                                plan: root.busyHere && !root.queuedHere ? root.turn.plan : root.noVars
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
                                    // A file's change: as a diff (an AI agent's question).
                                    BDiffView {
                                        id: questionDiffView
                                        objectName: "chatQuestionDiff"
                                        Layout.fillWidth: true
                                        // (As tall as its lines, a few dozen at most: it scrolls.)
                                        Layout.preferredHeight: Math.min(260, questionDiffView.rows.length * 24 + 8)
                                        visible: root.questionHasDiff
                                        mode: "unified"
                                        showToolbar: false
                                        oldText: root.questionHasDiff ? AppController.routeText(root.questionDiff, "oldText") : ""
                                        newText: root.questionHasDiff ? AppController.routeText(root.questionDiff, "newText") : ""
                                        oldLabel: AppController.routeText(root.questionDiff, "path")
                                        newLabel: AppController.routeText(root.questionDiff, "path")
                                    }
                                    // A plan to approve: as a message, with "Save as a page".
                                    Rectangle {
                                        objectName: "chatQuestionPlan"
                                        Layout.fillWidth: true
                                        visible: root.planHere !== ""
                                        implicitHeight: planText.implicitHeight + 2 * Theme.space3
                                        radius: Theme.radius
                                        color: Theme.bgSunken
                                        BRichText {
                                            id: planText
                                            x: Theme.space3
                                            y: Theme.space3
                                            width: parent.width - 2 * Theme.space3
                                            html: root.planHere !== "" ? messages.render(root.planHere) : ""
                                            font.family: Theme.fontEditor
                                            font.pixelSize: Theme.textBase
                                            color: Theme.fg
                                        }
                                    }
                                    BButton {
                                        objectName: "chatSavePlan"
                                        visible: root.planHere !== ""
                                        size: "small"
                                        variant: "ghost"
                                        iconName: "file-output"
                                        text: qsTr("Save the plan as a page")
                                        onClicked: {
                                            const saved = root.chats.savePlan(root.path)
                                            if (saved === "")
                                                AppController.toast(qsTr("The plan could not be saved."), "error")
                                            else
                                                AppController.toast(qsTr("Saved as “%1”").arg(Workspace.pageTitle(saved)), "success")
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        visible: root.questionTitleEnd >= 0 && !root.questionHasDiff && root.planHere === ""
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
                                                onClicked: root.chats.answer(root.path, option.modelData)
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

                            // Messages written meanwhile: they wait for the answer (held after a
                            // stop or a failure: Resume).
                            RowLayout {
                                objectName: "chatHeld"
                                Layout.fillWidth: true
                                visible: root.heldHere && !root.busyHere
                                spacing: Theme.space2
                                Item { Layout.fillWidth: true }
                                BText {
                                    text: qsTr("%n message(s) wait: the answer before them stopped.", "", root.waitingHere.length)
                                    variant: "muted"
                                    wrapMode: Text.Wrap
                                }
                                BButton {
                                    objectName: "chatResume"
                                    size: "small"
                                    iconName: "play"
                                    text: qsTr("Resume")
                                    onClicked: root.chats.resumeQueue(root.path)
                                }
                            }
                            Repeater {
                                model: root.waitingHere
                                delegate: Item {
                                    id: waiting
                                    required property var modelData
                                    readonly property int waitingId: AppController.routeInt(waiting.modelData, "id", -1)
                                    readonly property string text: AppController.routeText(waiting.modelData, "text")
                                    readonly property list<string> pictures: root.routeList(waiting.modelData, "pictures")
                                    readonly property int pictureCount: waiting.pictures.length
                                    objectName: "chatWaiting"
                                    Layout.fillWidth: true
                                    implicitHeight: waitingBubble.height
                                    Rectangle {
                                        id: waitingBubble
                                        readonly property real maxWidth: Math.min(parent.width * 0.82, 720)
                                        x: parent.width - width
                                        width: Math.min(waitingBubble.maxWidth, Math.max(waitingText.implicitWidth, waitingActions.implicitWidth) + 2 * Theme.space3)
                                        height: waitingColumn.implicitHeight + 2 * Theme.space2
                                        radius: Theme.radiusXl
                                        color: "transparent"
                                        border.color: Theme.accent
                                        border.width: 1
                                        opacity: 0.85
                                        ColumnLayout {
                                            id: waitingColumn
                                            x: Theme.space3
                                            y: Theme.space2
                                            width: parent.width - 2 * Theme.space3
                                            spacing: Theme.space1
                                            BText {
                                                id: waitingText
                                                Layout.fillWidth: true
                                                // Plain text: what the user wrote, as written.
                                                text: waiting.text + (waiting.pictureCount > 0 ? (waiting.text !== "" ? "  " : "") + qsTr("[%n picture(s)]", "", waiting.pictureCount) : "")
                                                textFormat: Text.PlainText
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 8
                                                elide: Text.ElideRight
                                            }
                                            RowLayout {
                                                id: waitingActions
                                                Layout.alignment: Qt.AlignRight
                                                spacing: Theme.space1
                                                BIcon {
                                                    name: "clock"
                                                    size: Theme.iconSizeSm
                                                    color: Theme.fgFaint
                                                }
                                                BText {
                                                    text: root.heldHere ? qsTr("held") : qsTr("waits")
                                                    variant: "caption"
                                                    color: Theme.fgFaint
                                                }
                                                BIconButton {
                                                    objectName: "chatWaitingNow"
                                                    size: "small"
                                                    label: qsTr("Send now")
                                                    iconName: "zap"
                                                    onClicked: root.chats.sendNow(root.path, waiting.waitingId)
                                                }
                                                BIconButton {
                                                    objectName: "chatWaitingEdit"
                                                    size: "small"
                                                    label: qsTr("Edit")
                                                    iconName: "pencil"
                                                    onClicked: root.editWaiting(waiting.waitingId)
                                                }
                                                BIconButton {
                                                    objectName: "chatWaitingRemove"
                                                    size: "small"
                                                    label: qsTr("Remove")
                                                    iconName: "x"
                                                    onClicked: root.chats.unqueue(root.path, waiting.waitingId, false)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // A new conversation: what to ask. In the messages' place, at their bottom, right
                // above the message being written; scrolled when the keyboard leaves too little
                // room — it never pushes the message field under the keyboard.
                Flickable {
                    id: emptyFlick
                    objectName: "chatEmptyFlick"
                    anchors.fill: parent
                    visible: root.path === "" && root.conversationShown
                    clip: true
                    contentWidth: width
                    contentHeight: emptyColumn.implicitHeight + Theme.space4
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                    TouchFlick.enabled: true
                    ColumnLayout {
                        id: emptyColumn
                        objectName: "chatEmpty"
                        x: Theme.space4
                        y: Math.max(0, emptyFlick.height - emptyFlick.contentHeight)
                        width: emptyFlick.width - 2 * Theme.space4
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
                    visible: root.path === "" && (root.page !== "" || root.choosesPartner || root.chats.isAgent(root.newPartner))
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
                        id: newApprovalsChip
                        objectName: "chatNewApprovals"
                        visible: root.chats.isAgent(root.newPartner)
                        iconName: "shield-check"
                        text: root.approvalsLabel(root.approvalsHere)
                        interactive: true
                        Accessible.name: qsTr("Approvals: %1").arg(newApprovalsChip.text)
                        onClicked: approvalsMenu.popup(newApprovalsChip, 0, newApprovalsChip.height)
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
                ChatAgentOptions {
                    objectName: "chatNewAgentOptions"
                    Layout.fillWidth: true
                    visible: root.path === "" && root.chats.isAgent(root.newPartner)
                    path: ""
                    partner: root.newPartner
                }

                // An AI agent's slash commands: those that match what is typed ("/", "/rev").
                Flow {
                    id: commandList
                    objectName: "chatCommands"
                    readonly property list<var> commands: root.chats.agentCommands(root.path, root.path !== "" ? root.partnerHere : root.newPartner,
                                                                                  input.text, root.chats.agentSerial)
                    Layout.fillWidth: true
                    visible: commandList.commands.length > 0
                    spacing: Theme.space1
                    Repeater {
                        model: commandList.commands
                        delegate: BChip {
                            id: commandChip
                            required property var modelData
                            readonly property string command: AppController.routeText(commandChip.modelData, "name")
                            readonly property string about: AppController.routeText(commandChip.modelData, "description")
                            objectName: "chatCommand"
                            size: "small"
                            interactive: true
                            showIcon: false
                            text: "/" + commandChip.command
                            Accessible.name: commandChip.about !== "" ? qsTr("%1: %2").arg(commandChip.text).arg(commandChip.about) : commandChip.text
                            onClicked: {
                                input.text = "/" + commandChip.command + " "
                                input.cursorPosition = input.length
                                input.forceActiveFocus()
                            }
                        }
                    }
                }

                // A page being named ("@Dom", "[[Do"): the pages it may be; a tap (or Tab: the
                // first) writes its link — an AI agent gets the pages a message names.
                Flow {
                    id: mentionList
                    objectName: "chatMentions"
                    readonly property list<var> pages: root.chats.mentions(input.text, input.cursorPosition, 8)
                    Layout.fillWidth: true
                    visible: mentionList.pages.length > 0
                    spacing: Theme.space1
                    Repeater {
                        model: mentionList.pages
                        delegate: BChip {
                            id: mentionChip
                            required property var modelData
                            objectName: "chatMention"
                            size: "small"
                            interactive: true
                            iconName: "file-text"
                            text: AppController.routeText(mentionChip.modelData, "title")
                            Accessible.name: qsTr("Name the page %1").arg(mentionChip.text)
                            onClicked: root.insertMention(mentionChip.text)
                        }
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
                                                 : root.busyHere ? qsTr("Write on: it waits for the answer (Ctrl+Enter: now)")
                                                 : qsTr("Ask, or ask to do something…")
                                accessibleName: qsTr("Message to the AI")
                                onCursorRectangleChanged: inputFlick.keepCaret()
                                // Enter sends (Shift+Enter: a new line); on touch the keyboard's Enter
                                // is a new line and the button sends. Ctrl+V with a picture on the
                                // clipboard: it goes with the message.
                                Keys.onPressed: (event) => {
                                    const key = event.key
                                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                                    const control = (event.modifiers & Qt.ControlModifier) !== 0
                                    if ((key === Qt.Key_Return || key === Qt.Key_Enter) && control && root.busyHere) {
                                        event.accepted = true
                                        root.steer()
                                    } else if ((key === Qt.Key_Return || key === Qt.Key_Enter) && !shift && !Theme.touchUi) {
                                        event.accepted = true
                                        root.send()
                                    } else if (event.matches(StandardKey.Paste) && root.pastePicture()) {
                                        event.accepted = true
                                    } else if ((key === Qt.Key_Up || key === Qt.Key_Down) && !shift && !control
                                               && root.recall(key === Qt.Key_Up)) {
                                        event.accepted = true
                                    } else if (key === Qt.Key_Tab && mentionList.pages.length > 0) {
                                        event.accepted = true
                                        root.insertMention(AppController.routeText(mentionList.pages[0], "title"))
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
                        id: sendButton
                        /// A message being written while a turn runs: it waits (Stop shows with an empty field).
                        readonly property bool hasText: Js.trim(input.typedText) !== "" || root.attached.length > 0
                        readonly property bool stops: root.busyHere && !sendButton.hasText
                        objectName: sendButton.stops ? "chatStop" : root.busyHere ? "chatQueue" : "chatSend"
                        Layout.alignment: Qt.AlignBottom
                        variant: sendButton.stops ? "secondary" : "primary"
                        iconName: sendButton.stops ? "square" : root.busyHere ? "clock" : "arrow-up"
                        enabled: sendButton.stops || sendButton.hasText
                        Accessible.name: sendButton.stops ? qsTr("Stop") : root.busyHere ? qsTr("Send after the answer") : qsTr("Send")
                        onClicked: {
                            if (sendButton.stops)
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
