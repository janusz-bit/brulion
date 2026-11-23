pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CodeBlockView — a fenced code block: monospace text on a sunken panel with the language as a
// small label. The fence lines are not shown; the content is edited in place (persistent
// InlineEditor, read-only until clicked, so nothing jumps). Enter inserts a line break, Tab a tab.
// With AppSettings.codeLineNumbers — or the note's own `line-numbers` option (PageController.codeLineNumbers,
// docs/SYNTAX.md §4 "Note options"), which wins — each line of the text is numbered on its left (a
// wrapped line once, on its first visual line: BlockViews.codeLineNumbers); the numbers are not in the text.
Rectangle {
    id: root

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    readonly property real firstLineCenter: code.y + Theme.lineHeightPx(Theme.textSm, 1.5) / 2
    /// The note's own choice: -1 none (the device's setting), 0 off, 1 on.
    readonly property int noteLineNumbers: root.block !== null && root.block.controller !== null
                                           ? root.block.controller.codeLineNumbers : -1
    readonly property bool lineNumbers: noteLineNumbers >= 0 ? noteLineNumbers > 0 : AppSettings.codeLineNumbers
    /// The numbers' column, one line per visual line of the code (updateLineNumbers()).
    property string lineNumberText: ""

    implicitHeight: code.y + code.implicitHeight + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgSunken
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    /// The column again from the code's laid-out lines: its text, its wrapping or its width
    /// changed (the layout is there by then: BlockViews lays out what is missing).
    function updateLineNumbers(): void {
        root.lineNumberText = root.lineNumbers ? BlockViews.codeLineNumbers(code.textDocument) : ""
    }
    onLineNumbersChanged: updateLineNumbers()
    // A width that moves the wrapping between lines without changing the visual line count.
    Later {
        id: lineNumbersLater
        onTriggered: root.updateLineNumbers()
    }

    Text {
        id: numbers
        objectName: "codeLineNumbers"
        visible: root.lineNumbers
        x: Theme.space3
        // The code's document (FixedHeight) puts a line's baseline at 4/5 of its height, a Text
        // its ascent below the line's top: moved down by the difference, each number sits on
        // its line's baseline (as RenderedTextItem does).
        y: code.y + Math.max(0, code.lineHeightPx * 0.8 - numberMetrics.ascent)
        // (Right-aligned: 9 and 10 end in one column; as wide as the longest number.)
        width: visible ? implicitWidth : 0
        horizontalAlignment: Text.AlignRight
        textFormat: Text.PlainText
        text: root.lineNumberText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        lineHeightMode: Text.FixedHeight
        lineHeight: code.lineHeightPx
        color: Theme.fgFaint
        Accessible.ignored: true

        FontMetrics { id: numberMetrics; font: numbers.font }
    }

    Text {
        id: label
        visible: root.block.rowData.lang !== ""
        anchors.right: parent.right
        anchors.rightMargin: Theme.space3
        y: Theme.space2
        text: root.block.rowData.lang
        textFormat: Text.PlainText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    InlineEditor {
        id: code
        x: root.lineNumbers ? numbers.x + numbers.width + Theme.space3 : Theme.space4
        y: Theme.space3
        width: parent.width - x - Theme.space4 - (label.visible ? label.width + Theme.space2 : 0)
        block: root.block
        persistent: true
        multiline: true
        highlightMode: "code"
        // ```luau (an extension page's code, docs/EXTENSIONS.md) in Luau's colours.
        highlightLanguage: root.block.rowData.lang
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.5)
        wrapMode: TextEdit.Wrap
        onTextChanged: if (root.lineNumbers) root.updateLineNumbers()
        onLineCountChanged: if (root.lineNumbers) root.updateLineNumbers()
        onWidthChanged: if (root.lineNumbers) lineNumbersLater.restart()
        Component.onCompleted: root.updateLineNumbers()
    }
}
