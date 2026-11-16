pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatView — route "chat" (docs/UI.md "AI chat"): conversations with the AI assistant, as in a
// messenger. Each conversation is a page in AI/ (Workspace.chats, core chat/chat.h): the user's
// messages on the right, the AI's on the left — rendered note text (ChatMessageModel: links open
// their pages) —, under them the answer as the model writes it (Workspace.chats.partial; before
// it, the end of what the model thinks: chats.thoughts; under it how fast it writes, chats.speed)
// and what the AI does now ("Czytam „Dom”…", Workspace.chats.status) with Stop, a change it asks a yes for as a card with the question's options (the question of its
// turn: Workspace.extensions.questionChat — the dialog asks it only while no chat shows the
// conversation), a failed turn with Try again; at the bottom the field (Enter sends with a
// keyboard, Shift+Enter is a new line; on touch the send button) and, before a new conversation's
// first message, the page it is about as a chip that can be taken away.
//
//   {view: "chat"}                          the conversations (a wide window: beside a new one)
//   {view: "chat", path: "AI/….md"}         one conversation
//   {view: "chat", new: true, page, prompt} a new one about `page`; `prompt` is sent at once
//
// Wide windows (≥ 720 px, not in the side panel) show the list beside the conversation; narrow
// ones one of them (the list → a conversation is a step of the history, Back returns). The first
// message of a new conversation replaces the route with the conversation's (in the side panel:
// pins it there again). Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `title`.
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
    readonly property string title: root.path !== "" && messages.title !== "" ? messages.title : qsTr("AI chat")

    readonly property bool wide: !root.inSidePanel && root.width >= 720
    readonly property bool listShown: root.wide || (root.path === "" && !root.starting)
    readonly property bool conversationShown: root.wide || root.path !== "" || root.starting
    readonly property WorkspaceChats chats: Workspace.chats
    /// A turn runs (in any conversation: one at a time).
    readonly property bool busy: root.chats.running !== ""
    readonly property bool busyHere: root.path !== "" && root.chats.running === root.path
    /// The change the AI asks a yes for, in this conversation.
    readonly property bool asking: root.path !== "" && Workspace.extensions.questionKind === "choose"
                                   && Workspace.extensions.questionChat === root.path
    readonly property bool failedHere: root.chats.failure !== "" && root.chats.failedPath === root.path
    readonly property string questionText: Workspace.extensions.questionTitle
    readonly property int questionTitleEnd: Js.indexOf(root.questionText, "\n")

    /// The list follows the answer being written while it is at its end (scrolled up: it stays).
    property bool following: true
    /// The answer as it is written: what the model thinks first, then its words.
    readonly property bool liveShown: root.busyHere && !root.asking
                                      && (root.chats.partial !== "" || root.chats.thoughts !== "")

    /// The conversation this view tells Workspace.chats it shows ("" none).
    property string registered
    readonly property string shownPath: root.visible && root.conversationShown ? root.path : ""
    onShownPathChanged: root.register()
    Component.onDestruction: {
        if (root.registered !== "")
            Workspace.chats.showing(root.registered, false)
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
        const prompt = AppController.routeText(root.route, "prompt")
        if (root.starting && prompt !== "") {
            input.text = prompt
            sendLater.restart()
        } else if (root.conversationShown && (root.starting || !Theme.touchUi)) {
            focusLater.restart()
        }
    }
    onRouteChanged: root.syncRoute()
    Component.onCompleted: root.syncRoute()

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
        if (text === "" || root.busy)
            return
        const sent = root.chats.send(root.path, text, root.path === "" ? root.page : "")
        if (sent === "")
            return
        input.text = ""
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
                    width: ListView.view.width
                    iconName: "message-square"
                    text: conversation.title
                    subtitle: (conversation.fromAi ? "🤖 " : "") + conversation.preview
                    selected: conversation.path === root.path
                    selectionBar: true
                    trailing: BText {
                        text: conversation.dateLabel
                        variant: "caption"
                        color: Theme.fgFaint
                    }
                    onClicked: root.open(conversation.path, false)
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

            // What it is about, and the page it is.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space3
                Layout.rightMargin: Theme.space2
                Layout.topMargin: Theme.space2
                visible: root.path !== ""
                spacing: Theme.space2
                // (Its name is the top bar's title, the side panel's header in the panel.)
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

            ListView {
                id: list
                objectName: "chatMessages"
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                    readonly property bool mine: message.sender === "user"
                    property string hoveredLink
                    property point linkPoint
                    readonly property real maxWidth: Math.min(message.width * (message.mine ? 0.82 : 0.92), 720)
                    width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                    height: bubble.height

                    function trackLink(pos: point): void {
                        message.linkPoint = body.mapFromItem(message, pos.x, pos.y)
                        const link = body.linkAt(message.linkPoint.x, message.linkPoint.y)
                        const h = Js.startsWith(link, "task:") ? "" : link
                        if (h !== message.hoveredLink)
                            message.hoveredLink = h
                    }

                    Rectangle {
                        id: bubble
                        objectName: message.mine ? "chatMessageUser" : "chatMessageAi"
                        x: message.mine ? message.width - width : 0
                        width: Math.min(message.maxWidth, Math.max(body.implicitWidth, 24) + 2 * Theme.space3)
                        height: body.height + 2 * Theme.space2 + 2
                        radius: Theme.radiusXl
                        color: message.mine ? Theme.accentBg : Theme.bgElevated
                        border.color: message.mine ? "transparent" : Theme.borderSubtle
                        Accessible.role: Accessible.StaticText
                        Accessible.name: (message.mine ? qsTr("You") : qsTr("AI")) + ": " + message.markdown

                        BRichText {
                            id: body
                            x: Theme.space3
                            y: Theme.space2 + 1
                            width: bubble.width - 2 * Theme.space3
                            html: message.messageHtml
                            hoveredLink: message.hoveredLink
                            font.family: Theme.fontEditor
                            font.pixelSize: Theme.textBase
                            color: Theme.fg
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

                footer: Item {
                    width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                    height: footerColumn.implicitHeight + (footerColumn.implicitHeight > 0 ? Theme.space2 : 0)
                    ColumnLayout {
                        id: footerColumn
                        y: Theme.space2
                        width: parent.width
                        spacing: Theme.space2

                        // The answer as the model writes it — before it, what the model thinks.
                        Rectangle {
                            id: live
                            objectName: "chatLive"
                            readonly property bool writing: root.chats.partial !== ""
                            readonly property real maxWidth: Math.min(footerColumn.width * 0.92, 720)
                            visible: root.liveShown
                            Layout.preferredWidth: Math.min(live.maxWidth, Math.max(live.writing ? Math.max(liveSettled.implicitWidth, liveTail.implicitWidth) : liveThoughts.implicitWidth, 24) + 2 * Theme.space3)
                            implicitHeight: (live.writing ? liveBody.height : liveThoughts.height) + 2 * Theme.space2 + 2
                            radius: Theme.radiusXl
                            color: Theme.bgElevated
                            border.color: Theme.borderSubtle
                            Accessible.role: Accessible.StaticText
                            Accessible.name: live.writing ? qsTr("AI") + ": " + root.chats.partial : qsTr("The AI thinks: %1").arg(root.chats.thoughts)

                            // Its finished blocks, rendered when one is finished; the block being
                            // written, rendered at every change — alone (the docs' "Calculating
                            // text layouts can be a slow operation"), and kept out of the others'
                            // batches (Item.DynamicMutabilityGroup, Qt 6.12).
                            Column {
                                id: liveBody
                                objectName: "chatPartial"
                                x: Theme.space3
                                y: Theme.space2 + 1
                                width: live.width - 2 * Theme.space3
                                visible: live.writing
                                spacing: liveSettled.html !== "" ? 10 : 0 // (a paragraph's gap, as renderMessage's)
                                BRichText {
                                    id: liveSettled
                                    objectName: "chatPartialSettled"
                                    width: parent.width
                                    visible: liveSettled.html !== ""
                                    html: root.chats.partialSettled !== "" ? messages.render(root.chats.partialSettled) : ""
                                    font.family: Theme.fontEditor
                                    font.pixelSize: Theme.textBase
                                    color: Theme.fg
                                }
                                BRichText {
                                    id: liveTail
                                    objectName: "chatPartialTail"
                                    width: parent.width
                                    html: live.writing ? messages.render(root.chats.partialTail) : ""
                                    mutability: Item.DynamicMutabilityGroup
                                    font.family: Theme.fontEditor
                                    font.pixelSize: Theme.textBase
                                    color: Theme.fg
                                }
                            }
                            BText {
                                id: liveThoughts
                                objectName: "chatThoughts"
                                x: Theme.space3
                                y: Theme.space2 + 1
                                width: live.width - 2 * Theme.space3
                                visible: !live.writing
                                // Plain text: the model's own words, never markup.
                                text: root.chats.thoughts
                                mutabilityGroup: Item.DynamicMutabilityGroup
                                textFormat: Text.PlainText
                                variant: "muted"
                                font.italic: true
                                font.pixelSize: Theme.textSm
                                wrapMode: Text.Wrap
                            }
                        }

                        // How fast the model writes (the model on the device: it knows).
                        BText {
                            objectName: "chatSpeed"
                            Layout.leftMargin: Theme.space3
                            visible: root.liveShown && root.chats.speed !== ""
                            text: root.chats.speed
                            variant: "caption"
                            color: Theme.fgFaint
                            Accessible.name: qsTr("The AI writes %1").arg(root.chats.speed)
                        }

                        // What the AI does now (not while its words come: they show it, and a
                        // spinner's frames would take the processor from a model on the device).
                        RowLayout {
                            objectName: "chatWorking"
                            Layout.fillWidth: true
                            visible: root.busyHere && !root.asking && (root.chats.status !== "" || !root.liveShown)
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
                                    BSpinner {
                                        size: Theme.iconSizeSm
                                        calm: true
                                    }
                                    BText {
                                        objectName: "chatStatus"
                                        Layout.fillWidth: true
                                        text: root.chats.status !== "" ? root.chats.status : qsTr("The AI is answering…")
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
                                        model: Workspace.extensions.questionOptions
                                        delegate: BButton {
                                            id: option
                                            required property int index
                                            required property string modelData
                                            objectName: "chatQuestionOption"
                                            text: option.modelData
                                            variant: option.index === 0 ? "primary" : option.index === 1 ? "secondary" : "ghost"
                                            onClicked: Workspace.extensions.answerChoice(option.modelData)
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
                                    text: root.chats.failure
                                    color: Theme.dangerText
                                    wrapMode: Text.Wrap
                                }
                                BButton {
                                    objectName: "chatRetry"
                                    visible: root.path !== ""
                                    size: "small"
                                    text: qsTr("Try again")
                                    iconName: "rotate-cw"
                                    enabled: !root.busy
                                    onClicked: root.chats.retry(root.path)
                                }
                                BIconButton {
                                    size: "small"
                                    label: qsTr("Dismiss")
                                    iconName: "x"
                                    onClicked: root.chats.dismissFailure()
                                }
                            }
                        }
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

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.path === "" && root.page !== ""
                    spacing: Theme.space2
                    BChip {
                        objectName: "chatPage"
                        Layout.maximumWidth: 260
                        iconName: "file-text"
                        text: root.page
                        removable: true
                        Accessible.name: qsTr("The page %1 goes along").arg(root.page)
                        onRemoveClicked: root.page = ""
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("goes along with the question")
                        variant: "caption"
                        color: Theme.fgFaint
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        implicitHeight: Math.min(input.implicitHeight, 168) + 2
                        radius: Theme.radiusLg
                        color: Theme.bgElevated
                        border.color: input.activeFocus ? Theme.accent : Theme.border
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
                                placeholderText: root.busy && !root.busyHere ? qsTr("The AI is answering in another conversation…")
                                                                             : qsTr("Ask, or ask to do something…")
                                accessibleName: qsTr("Message to the AI")
                                onCursorRectangleChanged: inputFlick.keepCaret()
                                // Enter sends (Shift+Enter: a new line); on touch the keyboard's Enter
                                // is a new line and the button sends.
                                Keys.onPressed: (event) => {
                                    const key = event.key
                                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                                    if ((key === Qt.Key_Return || key === Qt.Key_Enter) && !shift && !Theme.touchUi) {
                                        event.accepted = true
                                        root.send()
                                    }
                                }
                            }
                        }
                    }
                    BButton {
                        objectName: root.busyHere ? "chatStop" : "chatSend"
                        Layout.alignment: Qt.AlignBottom
                        variant: root.busyHere ? "secondary" : "primary"
                        iconName: root.busyHere ? "square" : "arrow-up"
                        enabled: root.busyHere || (!root.busy && Js.trim(input.typedText) !== "")
                        Accessible.name: root.busyHere ? qsTr("Stop") : qsTr("Send")
                        onClicked: {
                            if (root.busyHere)
                                root.chats.stop()
                            else
                                root.send()
                        }
                    }
                }
            }
        }
    }
}
