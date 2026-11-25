pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatSidePane — a side chat of the conversation shown (docs/UI.md "AI chat": side chats, `/btw`):
// a question asked beside the conversation, never added to it. Its own messages (ChatMessageModel
// with ownOnly: not the copy of the conversation before them), the answer as it is written, what
// the AI does, a question of its turn as a card with its options, a failure with Try again; its
// field (a follow-up of the side chat: in line while it answers) and Stop. "Bring into the
// conversation" hands its last answer to the conversation's field (ChatView quotes it there),
// Delete moves it to the trash (Undo in the toast). ChatView shows it beside the messages in a
// wide window and in their place in a narrow one; Close and Escape close it (Back too: opening it
// was a step of the history).
FocusScope {
    id: pane

    /// The side chat ("" none: nothing shown).
    property string path
    /// The conversation it is beside.
    property string conversation

    /// Close, Escape, or the side chat deleted.
    signal closeRequested()
    /// Its last answer (Markdown) for the conversation's field.
    signal broughtIn(string markdown)

    readonly property WorkspaceChats chats: Workspace.chats
    readonly property ChatTurn turn: pane.chats.turn(pane.path)
    readonly property bool busy: pane.path !== "" && pane.turn !== null && pane.turn.running
    readonly property bool queued: pane.busy && pane.turn.queued
    readonly property int questionSerial: pane.chats.questionSerial
    readonly property bool asking: pane.path !== "" && pane.chats.asking(pane.path, pane.questionSerial)
    readonly property string questionText: pane.asking ? pane.chats.questionText(pane.path, pane.questionSerial) : ""
    readonly property list<string> questionOptions: pane.asking ? pane.chats.questionOptions(pane.path, pane.questionSerial) : pane.noStrings
    readonly property list<string> noStrings: []
    readonly property list<var> noVars: []
    readonly property string failure: pane.turn !== null ? pane.turn.failure : ""
    readonly property list<var> waiting: pane.path !== "" ? pane.chats.queued(pane.path, pane.chats.queueSerial) : pane.noVars
    readonly property string partial: pane.busy && !pane.asking ? pane.turn.partial : ""
    /// Its last answer ("" none yet): read again with every message.
    readonly property string lastAnswer: pane.path !== "" && messages.count >= 0 ? pane.chats.lastAnswer(pane.path) : ""

    /// The side chat this pane tells Workspace.chats it shows (its questions are answered here).
    property string registered
    readonly property string shownPath: pane.visible ? pane.path : ""
    onShownPathChanged: pane.register()
    Component.onDestruction: {
        if (pane.registered !== "")
            Workspace.chats.showing(pane.registered, false)
    }
    function register(): void {
        if (pane.registered === pane.shownPath)
            return
        if (pane.registered !== "")
            pane.chats.showing(pane.registered, false)
        pane.registered = pane.shownPath
        if (pane.registered !== "")
            pane.chats.showing(pane.registered, true)
    }
    onPathChanged: {
        field.text = ""
        toEnd.restart()
        if (pane.path !== "" && !Theme.touchUi)
            focusLater.restart()
    }

    function send(): void {
        const text = Js.trim(field.text)
        if (text === "" || pane.path === "")
            return
        // (While it answers: in line after the answer.)
        const sent = pane.busy || pane.waiting.length > 0 ? pane.chats.queue(pane.path, text) : pane.chats.send(pane.path, text) !== ""
        if (!sent)
            return
        field.text = ""
        toEnd.restart()
    }
    function remove(): void {
        const trashPath = "" + pane.chats.remove(pane.path)
        if (trashPath === "")
            return
        AppController.toastRestore(qsTr("Side chat moved to trash"), qsTr("Undo"), pane.chats.lastRemoved())
        pane.closeRequested()
    }

    Keys.onEscapePressed: pane.closeRequested()

    Later {
        id: toEnd
        onTriggered: list.positionViewAtEnd()
    }
    Later {
        id: focusLater
        onTriggered: field.forceActiveFocus()
    }
    Connections {
        target: pane.chats
        function onWritten(path: string) {
            if (path !== pane.path)
                return
            messages.refreshNow()
            toEnd.restart()
        }
    }
    onPartialChanged: if (pane.partial !== "") toEnd.restart()
    onAskingChanged: if (pane.asking) toEnd.restart()

    ChatMessageModel {
        id: messages
        workspace: Workspace
        path: pane.path
        ownOnly: true
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
    PageTools {
        id: tools
        workspace: Workspace
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgSunken
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Its name and what can be done with it.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space2
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space1
            BIcon {
                name: "message-square"
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
            BText {
                objectName: "chatSideTitle"
                Layout.fillWidth: true
                text: qsTr("Side chat: %1").arg(pane.chats.nameOf(pane.path))
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            BIconButton {
                objectName: "chatSideBringIn"
                enabled: pane.lastAnswer !== ""
                label: qsTr("Bring into the conversation")
                iconName: "quote"
                onClicked: pane.broughtIn(pane.lastAnswer)
            }
            BIconButton {
                objectName: "chatSideDelete"
                label: qsTr("Delete the side chat")
                iconName: "trash-2"
                onClicked: pane.remove()
            }
            BIconButton {
                objectName: "chatSideClose"
                label: qsTr("Close (Esc)")
                iconName: "x"
                onClicked: pane.closeRequested()
            }
        }
        BText {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space3
            Layout.rightMargin: Theme.space3
            text: qsTr("Answered beside “%1”, never added to it.").arg(pane.chats.nameOf(pane.conversation))
            variant: "caption"
            color: Theme.fgFaint
            wrapMode: Text.Wrap
        }

        ListView {
            id: list
            objectName: "chatSideMessages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: messages
            spacing: Theme.space2
            topMargin: Theme.space3
            bottomMargin: Theme.space3
            leftMargin: Theme.space3
            rightMargin: Theme.space3
            TouchFlick.enabled: true
            boundsBehavior: Flickable.StopAtBounds
            BRowScrollBar { list: list }
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Side chat")

            delegate: Item {
                id: message
                required property string sender
                required property string messageHtml
                required property string markdown
                required property list<var> segments
                readonly property bool mine: message.sender === "user"
                readonly property bool inParts: message.segments.length > 0
                readonly property real maxWidth: message.width * (message.mine ? 0.86 : 0.96)
                property string hoveredLink
                property point linkPoint
                width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                height: bubble.height

                /// A tap at `pos` of the bubble: the link there opens (from the side chat's page).
                function tapAt(pos: point): void {
                    const x = pos.x
                    const y = pos.y
                    if (message.inParts)
                        return
                    message.linkPoint = body.mapFromItem(bubble, x, y)
                    const link = "" + body.linkAt(message.linkPoint.x, message.linkPoint.y)
                    if (link !== "" && !Js.startsWith(link, "task:"))
                        tools.openLink(link, pane.path, false)
                }

                Rectangle {
                    id: bubble
                    objectName: message.mine ? "chatSideUser" : "chatSideAi"
                    x: message.mine ? parent.width - width : 0
                    width: Math.min(message.maxWidth, Math.max(body.visible ? body.implicitWidth : 0,
                                                               message.inParts ? parts.naturalWidth : 0, 24) + 2 * Theme.space3)
                    height: bubbleColumn.height + 2 * Theme.space2 + 2
                    radius: Theme.radiusXl
                    color: message.mine ? Theme.accentBg : Theme.bgElevated
                    border.color: message.mine ? "transparent" : Theme.borderSubtle
                    Accessible.role: Accessible.StaticText
                    Accessible.name: (message.mine ? qsTr("You") : qsTr("AI")) + ": " + message.markdown
                    Column {
                        id: bubbleColumn
                        x: Theme.space3
                        y: Theme.space2 + 1
                        width: bubble.width - 2 * Theme.space3
                        BRichText {
                            id: body
                            measured: true
                            width: parent.width
                            visible: message.messageHtml !== "" && !message.inParts
                            html: message.inParts ? "" : message.messageHtml
                            hoveredLink: message.hoveredLink
                            font.family: Theme.fontEditor
                            font.pixelSize: Theme.textBase
                            color: Theme.fg
                        }
                        ChatSegments {
                            id: parts
                            width: parent.width
                            visible: message.inParts
                            segments: message.segments
                            hoveredLink: message.hoveredLink
                            english: DatesHelper.locale === "en"
                        }
                    }
                    TapHandler {
                        onTapped: (eventPoint, button) => message.tapAt(eventPoint.position)
                        onLongPressed: copyMenu.popup()
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        onTapped: copyMenu.popup()
                    }
                    BMenu {
                        id: copyMenu
                        BMenuItem {
                            objectName: "chatSideCopy"
                            text: qsTr("Copy")
                            iconName: "copy"
                            onTriggered: tools.copyText(message.markdown)
                        }
                        BMenuItem {
                            objectName: "chatSideBringInMessage"
                            visible: !message.mine
                            height: visible ? implicitHeight : 0
                            text: qsTr("Bring into the conversation")
                            iconName: "quote"
                            onTriggered: pane.broughtIn(message.markdown)
                        }
                    }
                }
            }

            footer: Column {
                width: list.width - list.leftMargin - list.rightMargin
                topPadding: Theme.space2
                spacing: Theme.space2

                // The answer as it is written.
                Rectangle {
                    objectName: "chatSideLive"
                    width: Math.min(parent.width * 0.96, live.implicitWidth + 2 * Theme.space3)
                    height: live.height + 2 * Theme.space2
                    visible: pane.partial !== ""
                    radius: Theme.radiusXl
                    color: Theme.bgElevated
                    border.color: Theme.borderSubtle
                    BRichText {
                        id: live
                        objectName: "chatSidePartial"
                        x: Theme.space3
                        y: Theme.space2
                        width: parent.width - 2 * Theme.space3
                        measured: true
                        html: pane.partial !== "" ? messages.render(pane.partial) : ""
                        font.family: Theme.fontEditor
                        font.pixelSize: Theme.textBase
                        color: Theme.fg
                    }
                }
                // What the AI does (no words yet, a tool, in line for the model on the device).
                RowLayout {
                    objectName: "chatSideWorking"
                    width: parent.width
                    visible: pane.busy && !pane.asking && pane.partial === ""
                    spacing: Theme.space2
                    BSpinner {
                        running: parent.visible && !pane.queued
                        visible: !pane.queued
                        size: Theme.iconSizeSm
                    }
                    BIcon {
                        visible: pane.queued
                        name: "hourglass"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: pane.queued ? qsTr("Waiting for the model on this device…")
                                          : pane.turn !== null && pane.turn.status !== "" ? pane.turn.status : qsTr("The AI is answering…")
                        variant: "muted"
                        wrapMode: Text.Wrap
                    }
                }
                // A change to allow, a permission: its options.
                Rectangle {
                    objectName: "chatSideQuestion"
                    width: parent.width
                    height: questionColumn.implicitHeight + 2 * Theme.space3
                    visible: pane.asking
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
                            text: pane.questionText
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            Repeater {
                                model: pane.questionOptions
                                delegate: BButton {
                                    id: option
                                    required property string modelData
                                    required property int index
                                    objectName: "chatSideQuestionOption"
                                    size: "small"
                                    variant: option.index === 0 ? "primary" : "secondary"
                                    text: option.modelData
                                    onClicked: pane.chats.answer(pane.path, option.modelData)
                                }
                            }
                        }
                    }
                }
                // Its turn failed: why, and Try again.
                Rectangle {
                    objectName: "chatSideFailure"
                    width: parent.width
                    height: failureRow.implicitHeight + 2 * Theme.space2
                    visible: pane.failure !== "" && !pane.busy
                    radius: Theme.radiusLg
                    color: Theme.dangerBg
                    RowLayout {
                        id: failureRow
                        x: Theme.space3
                        y: Theme.space2
                        width: parent.width - 2 * Theme.space3
                        spacing: Theme.space2
                        BText {
                            Layout.fillWidth: true
                            text: pane.failure
                            color: Theme.dangerText
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                        BButton {
                            objectName: "chatSideRetry"
                            size: "small"
                            variant: "secondary"
                            text: qsTr("Try again")
                            onClicked: pane.chats.retry(pane.path)
                        }
                        BIconButton {
                            size: "small"
                            label: qsTr("Close")
                            iconName: "x"
                            onClicked: pane.chats.dismissFailure(pane.path)
                        }
                    }
                }
                BText {
                    objectName: "chatSideWaiting"
                    width: parent.width
                    visible: pane.waiting.length > 0
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("%n message(s) wait for the answer", "", pane.waiting.length)
                    variant: "caption"
                    color: Theme.fgMuted
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.space2
            spacing: Theme.space2
            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                implicitHeight: Math.min(field.implicitHeight, 120) + 2
                radius: Theme.radiusLg
                color: Theme.bgElevated
                border.color: field.activeFocus ? Theme.accent : Theme.border
                Flickable {
                    id: fieldFlick
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    contentWidth: width
                    contentHeight: field.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                    BTextArea {
                        id: field
                        objectName: "chatSideInput"
                        width: fieldFlick.width
                        background: null
                        wrapMode: TextEdit.Wrap
                        placeholderText: pane.busy ? qsTr("Write on: it waits for the answer") : qsTr("Ask on, beside the conversation…")
                        accessibleName: qsTr("Message of the side chat")
                        Keys.onPressed: (event) => {
                            const key = event.key
                            const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                            if ((key === Qt.Key_Return || key === Qt.Key_Enter) && !shift && !Theme.touchUi) {
                                event.accepted = true
                                pane.send()
                            } else if (key === Qt.Key_Escape) {
                                event.accepted = true
                                pane.closeRequested()
                            }
                        }
                    }
                }
            }
            BButton {
                id: sideSend
                readonly property bool stops: pane.busy && Js.trim(field.text) === ""
                objectName: sideSend.stops ? "chatSideStop" : "chatSideSend"
                Layout.alignment: Qt.AlignBottom
                variant: sideSend.stops ? "secondary" : "primary"
                iconName: sideSend.stops ? "square" : pane.busy ? "clock" : "arrow-up"
                enabled: sideSend.stops || Js.trim(field.text) !== ""
                Accessible.name: sideSend.stops ? qsTr("Stop") : pane.busy ? qsTr("Send after the answer") : qsTr("Send")
                onClicked: {
                    if (sideSend.stops)
                        pane.chats.stop(pane.path)
                    else
                        pane.send()
                }
            }
        }
    }
}
