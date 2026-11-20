pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// WritingDialog — the editor's AI assistant (docs/UI.md "AI in the editor"): the selection, or the
// block or paragraph at the caret, rewritten after a preset or the user's own words ("Rewrite"),
// continued ("Continue writing") or summed up ("Summarize") by the model of Settings → Extensions
// → AI (WritingSession: core writing/writing.h), shown as it is written — the text that would go
// into the note, or (a rewrite) the change word by word — with Replace / Insert (one undo step:
// PageController.pasteText, replaceSelection or insertAfterBlock; in source mode one step of the
// text's own undo), Try again, Stop and Cancel. Nothing is sent but by the user's action (the menu,
// the shortcut, the palette: BlockEditor.startWriting) and nothing changes in the note but on
// Replace / Insert. A centred card on wide windows, a bottom sheet on a phone (BDialog); modal, so
// the text it works on stays as it was while the AI writes (checked again before it is changed).
//
//   writingDialog.startInline(action, preset, row, editText, from, to)   // text of the block being edited
//   writingDialog.startBlocks(action, preset, selectionMarkdown, firstRow, lastRow)
//   writingDialog.startSource(action, preset, sourceText, from, to)
// (A Rewrite without a preset shows the presets first: WritingSession.choosing.)
BDialog {
    id: dialog
    objectName: "writingDialog"

    /// The editor as an Item, for the editor itself to pass (a file cannot give its own root to a
    /// property of its own type in compiled code).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor
    /// A rewrite shows the change word by word (else the new text).
    property bool changesShown: true
    readonly property WritingSession session: writing
    readonly property string action: writing.action
    readonly property bool working: writing.state === "waiting" || writing.state === "writing"
    readonly property bool acceptable: writing.result !== "" && (writing.state === "done" || writing.state === "stopped")
    /// The change word by word is shown (a rewrite that is written: while it comes, the new text).
    readonly property bool changesNow: dialog.action === "rewrite" && dialog.changesShown && dialog.acceptable

    WritingSession {
        id: writing
        workspace: Workspace
    }

    title: dialog.action === "continue" ? qsTr("Continue writing") : dialog.action === "summarize" ? qsTr("Summarize") : qsTr("Rewrite")
    subtitle: writing.instruction !== "" ? writing.instruction : ""
    iconName: "sparkles"
    iconColor: Theme.accent
    preferredWidth: 680
    acceptText: dialog.action === "rewrite" ? qsTr("Replace") : qsTr("Insert")
    acceptEnabled: dialog.acceptable
    onAccepted: dialog.apply()
    onRejected: writing.clear()

    // ---- Starting (WritingSession takes the pieces: the text, the context, where it goes) ----------
    function pageName(): string {
        return dialog.editor && dialog.editor.controller ? dialog.editor.controller.title : ""
    }
    function shown(started: bool): bool {
        dialog.changesShown = true
        instructionField.text = ""
        if (started)
            dialog.open()
        return started
    }
    /// The text of the block being edited ([from, to) of `editText`, the whole block when they
    /// meet, up to the caret for a continuation).
    function startInline(action: string, preset: string, r: int, editText: string, f: int, t: int): bool {
        return dialog.shown(writing.startInline(action, preset, r, editText, f, t, dialog.pageName()))
    }
    /// The selected blocks (Markdown with their subtrees), from `firstRow` to `lastRow`.
    function startBlocks(action: string, preset: string, markdown: string, firstRow: int, lastRow: int): bool {
        return dialog.shown(writing.startBlocks(action, preset, markdown, firstRow, lastRow, dialog.pageName()))
    }
    /// Source mode's text: [f, t), or the paragraph at the caret when they meet.
    function startSource(action: string, preset: string, source: string, f: int, t: int): bool {
        return dialog.shown(writing.startSource(action, preset, source, f, t, dialog.pageName()))
    }

    // ---- Applying ---------------------------------------------------------------------------------
    function apply(): void {
        // (Every property read before any call: compiled calls take no arguments a call may change.)
        const editor = dialog.editor
        const controller = editor ? editor.controller : null
        const result = "" + writing.result
        const base = "" + writing.baseText
        const target = "" + writing.target
        const row = writing.row + 0
        const firstRow = writing.firstRow + 0
        const rewrite = writing.action === "rewrite"
        const summary = writing.action === "summarize"
        const ok = writing.canAccept()
        const f = writing.changeFrom()
        const t = writing.changeTo()
        const text = "" + writing.changeText()
        writing.clear()
        if (!editor || !controller || !ok)
            return
        if (target === "source") {
            if (editor.sourcePlainText() !== base)
                editor.notice(qsTr("The text changed meanwhile: nothing was changed"), "info", null)
            else
                editor.replaceSourceRange(f, t, text)
        } else if (target === "blocks") {
            if (controller.selectionMarkdown() !== base)
                editor.notice(qsTr("The selection changed meanwhile: nothing was changed"), "info", null)
            else if (rewrite)
                controller.replaceSelection(result)
            else
                controller.insertAfterBlock(row, result, firstRow)
        } else if (target === "inline") {
            if (editor.editTextOf(row) !== base)
                editor.notice(qsTr("The text changed meanwhile: nothing was changed"), "info", null)
            else if (summary)
                controller.insertAfterBlock(row, result)
            else
                editor.pasteWritten(row, f, t, text)
        }
    }

    footerLeading: [
        BButton {
            objectName: "writingStop"
            visible: dialog.working
            text: qsTr("Stop")
            iconName: "square"
            variant: "ghost"
            onClicked: writing.stop()
        },
        BButton {
            objectName: "writingRetry"
            visible: !dialog.working && !writing.choosing && writing.original !== ""
            text: qsTr("Try again")
            iconName: "rotate-cw"
            variant: "ghost"
            onClicked: writing.retry()
        }
    ]

    // How to rewrite it: the presets and the user's own words.
    Flow {
        objectName: "writingPresets"
        Layout.fillWidth: true
        visible: dialog.action === "rewrite" || writing.choosing
        spacing: Theme.space2
        Repeater {
            model: writing.presets
            delegate: BChip {
                id: presetChip
                required property string modelData
                objectName: "writingPreset_" + presetChip.modelData
                text: writing.presetLabel(presetChip.modelData)
                iconName: presetChip.modelData === "english" || presetChip.modelData === "polish" ? "languages" : ""
                showIcon: presetChip.iconName !== ""
                interactive: !dialog.working
                selected: writing.preset === presetChip.modelData
                onClicked: dialog.rewriteWith(presetChip.modelData, "")
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: dialog.action === "rewrite" || writing.choosing
        spacing: Theme.space2
        BTextField {
            id: instructionField
            objectName: "writingInstruction"
            Layout.fillWidth: true
            leadingIcon: "sparkles"
            placeholderText: qsTr("Or say how: “as a list”, “friendlier, for a letter”…")
            onAccepted: if (Js.trim(instructionField.typedText) !== "") dialog.rewriteWith("", Js.trim(instructionField.typedText))
        }
        BButton {
            objectName: "writingAsk"
            text: qsTr("Rewrite")
            variant: "secondary"
            enabled: Js.trim(instructionField.typedText) !== "" && !dialog.working
            onClicked: dialog.rewriteWith("", Js.trim(instructionField.typedText))
        }
    }
    function rewriteWith(preset: string, instruction: string): void {
        writing.rewriteWith(preset, instruction)
    }

    // What the AI does, or why it failed.
    RowLayout {
        objectName: "writingStatus"
        Layout.fillWidth: true
        visible: dialog.working || writing.state === "failed" || writing.state === "stopped"
        spacing: Theme.space2
        BSpinner {
            visible: dialog.working
            running: dialog.working && dialog.visible
            size: Theme.iconSizeSm
            calm: true
        }
        BIcon {
            visible: writing.state === "failed"
            name: "circle-alert"
            size: Theme.iconSize
            color: Theme.dangerText
        }
        BText {
            objectName: "writingStatusText"
            Layout.fillWidth: true
            text: writing.state === "failed" ? writing.error
                  : writing.state === "stopped" ? qsTr("Stopped: what was written so far.")
                  : writing.state === "waiting" && writing.onDevice ? qsTr("Waiting for the model on this device…")
                  : qsTr("The AI is writing…")
            color: writing.state === "failed" ? Theme.dangerText : Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // The result: the text as it would go in, or (a rewrite) the change.
    // A Rewrite… not asked yet: what it will rewrite.
    Rectangle {
        objectName: "writingOriginal"
        Layout.fillWidth: true
        visible: writing.choosing
        implicitHeight: originalText.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.bgSunken
        border.color: Theme.borderSubtle
        BText {
            id: originalText
            x: Theme.space3
            y: Theme.space3
            width: parent.width - 2 * Theme.space3
            // Plain text: the note's own words.
            text: writing.original
            textFormat: Text.PlainText
            font.family: Theme.fontEditor
            color: Theme.fgMuted
            wrapMode: Text.Wrap
            maximumLineCount: 6
            elide: Text.ElideRight
        }
    }
    BSegmented {
        objectName: "writingView"
        visible: dialog.action === "rewrite" && dialog.acceptable
        model: [{ value: "changes", text: qsTr("Changes"), icon: "file-diff" },
                { value: "result", text: qsTr("New text"), icon: "file-text" }]
        currentValue: dialog.changesShown ? "changes" : "result"
        onActivated: (value) => dialog.changesShown = value === "changes"
    }
    Rectangle {
        objectName: "writingResult"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(resultText.implicitHeight, changesText.implicitHeight, 64) + 2 * Theme.space3, dialog.sheet ? 280 : 360)
        visible: writing.result !== "" || dialog.working
        radius: Theme.radius
        color: Theme.bgSunken
        border.color: Theme.borderSubtle
        Flickable {
            id: resultFlick
            anchors.fill: parent
            anchors.margins: Theme.space3
            clip: true
            contentWidth: width
            contentHeight: dialog.changesNow ? changesText.implicitHeight : resultText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            TouchFlick.enabled: true
            // The end in view while it is written.
            onContentHeightChanged: if (dialog.working && contentHeight > height) contentY = contentHeight - height
            BText {
                id: resultText
                objectName: "writingResultText"
                width: resultFlick.width
                visible: !dialog.changesNow
                // Plain text: the Markdown that goes into the note, as it is.
                text: writing.result !== "" ? writing.result : (dialog.working ? "…" : "")
                textFormat: Text.PlainText
                font.family: Theme.fontEditor
                font.pixelSize: Theme.textBase
                wrapMode: Text.Wrap
                mutabilityGroup: Item.DynamicMutabilityGroup
            }
            BRichText {
                id: changesText
                objectName: "writingChanges"
                width: resultFlick.width
                visible: dialog.changesNow
                // (Our own escaped HTML of the two texts: WritingSession.changesHtml.)
                html: dialog.changesNow && writing.result !== "" ? writing.changesHtml(Theme.dangerText, Theme.successBg) : ""
                font.family: Theme.fontEditor
                font.pixelSize: Theme.textBase
                color: Theme.fg
                mutability: Item.DynamicMutabilityGroup
            }
        }
    }
    BText {
        Layout.fillWidth: true
        visible: writing.state === "done"
        text: dialog.action === "rewrite" ? qsTr("Replace puts it in place of the text; Undo brings the text back.")
              : dialog.action === "continue" ? qsTr("Insert puts it after the text; Undo takes it out.")
              : qsTr("Insert puts the summary under the text; Undo takes it out.")
        variant: "caption"
        color: Theme.fgFaint
        wrapMode: Text.Wrap
    }
}
