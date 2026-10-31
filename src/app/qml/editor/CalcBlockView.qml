pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalcBlockView — a ```calc block (docs/SYNTAX.md §9): the input lines on the left, each line's
// result right-aligned on the same line (calcResults role, evaluated in C++ with the page's
// variables). Assignments show their value muted, errors in red; a `sum` line is emphasised.
// The inputs are edited in place (persistent InlineEditor); results follow while typing.
// A calculation is note text, not code: inputs and results are in the editor's font and size
// (results with tabular figures, so digits still line up right-aligned), errors smaller in the
// UI font; the 1-px frame shows only while editing (as the properties panel's).
Rectangle {
    id: root

    property BlockDelegate block
    readonly property var results: block.model.calcResults || []
    /// The note's text size (the editor's), and its line height for a list of lines.
    readonly property int fontSize: root.block && root.block.editor ? root.block.editor.fontSize : Theme.textEditor
    readonly property int lineHeightPx: Theme.lineHeightPx(fontSize, Theme.lineHeight)
    /// The family the note's text is drawn in ("serif" resolved, e.g. "Noto Serif"): the inputs'
    /// TextEdit gets that name, so its text stays in the note's font while it is edited.
    readonly property string family: familyProbe.fontInfo.family !== "" ? familyProbe.fontInfo.family : Theme.fontEditor
    readonly property real firstLineCenter: input.y + lineHeightPx / 2
    /// Width reserved for the results column (widest result, at most 45 %).
    readonly property real resultsWidth: {
        let w = 0
        for (let i = 0; i < results.length; ++i) {
            const r = results[i]
            const t = r.error !== "" ? r.error : r.value
            if (t !== "")
                w = Math.max(w, metrics.advanceWidth(t))
        }
        return Math.min(parent ? parent.width * 0.45 : 200, Math.ceil(w) + Theme.space6)
    }
    /// Offsets of the line starts in the input text.
    readonly property var lineStarts: {
        const t = input.text
        const starts = [0]
        for (let i = 0; i < t.length; ++i) {
            if (t.charAt(i) === "\n")
                starts.push(i + 1)
        }
        return starts
    }

    implicitHeight: input.y + input.implicitHeight + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgSunken
    border.width: root.block.editing ? 1 : 0
    border.color: Theme.alpha(Theme.focusRing, 0.5)

    Text {
        id: familyProbe
        visible: false
        text: "0"
        font.family: Theme.fontEditor
        font.pixelSize: root.fontSize
    }
    FontMetrics {
        id: metrics
        font.family: root.family
        font.pixelSize: root.fontSize
        font.weight: Theme.weightSemiBold
        font.features: { "tnum": 1 }
    }
    FontMetrics {
        id: errorMetrics
        font.family: Theme.fontUi
        font.pixelSize: Theme.textSm
    }

    Row {
        x: Theme.space4
        y: Theme.space1 + 2
        spacing: Theme.space1
        BIcon { name: "calculator"; size: Theme.iconSizeSm - 2; color: Theme.fgFaint; anchors.verticalCenter: parent.verticalCenter }
        Text {
            text: qsTr("Calculation")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            color: Theme.fgFaint
        }
    }

    // Separator between inputs and results.
    Rectangle {
        visible: root.results.length > 0
        x: parent.width - root.resultsWidth - Theme.space2
        y: input.y
        width: 1
        height: input.height
        color: Theme.borderSubtle
    }

    InlineEditor {
        id: input
        objectName: "calcInput"
        x: Theme.space4
        y: Theme.space6
        width: parent.width - x - root.resultsWidth - Theme.space4
        block: root.block
        persistent: true
        multiline: true
        highlightMode: "code" // (only colours comments: the font stays the note's)
        font.family: root.family
        font.pixelSize: root.fontSize
        font.weight: Theme.weightNormal
        lineHeightPx: root.lineHeightPx
    }

    Repeater {
        model: root.results
        delegate: Text {
            id: result
            objectName: "calcResult" // (the editor's self-tests)
            required property var modelData
            required property int index
            readonly property bool isError: modelData.error !== ""
            readonly property bool isTotal: /^\s*(sum|suma|total|razem|avg|średnia|srednia)\s*$/i.test(input.text.split("\n")[modelData.line] || "")
            readonly property int offset: modelData.line < root.lineStarts.length ? root.lineStarts[modelData.line] : 0
            visible: text !== ""
            x: root.width - root.resultsWidth
            width: root.resultsWidth - Theme.space4
            // The line's top as the input wraps it now: contentHeight and width read for real —
            // positionToRectangle() is a method call, not tracked, and a bare `void input.width` is
            // compiled away with the dependency (results stayed put when a narrower window wrapped
            // the lines above them again).
            y: {
                if (input.contentHeight < 0 || input.width < 0)
                    return input.y
                return input.y + (offset <= input.length ? input.positionToRectangle(offset).y : 0)
            }
            height: root.lineHeightPx
            // On the line's baseline: the input's lines have their text at the top of the line
            // (a fixed line height); an error's smaller text moved down to the same baseline.
            verticalAlignment: Text.AlignTop
            topPadding: isError ? Math.max(0, Math.round(metrics.ascent - errorMetrics.ascent)) : 0
            horizontalAlignment: Text.AlignRight
            text: isError ? modelData.error : modelData.value
            textFormat: Text.PlainText
            elide: Text.ElideLeft
            // A message in the UI font, smaller; a number in the note's, with tabular figures.
            font.family: isError ? Theme.fontUi : root.family
            font.pixelSize: isError ? Theme.textSm : root.fontSize
            font.weight: isTotal ? Theme.weightSemiBold : Theme.weightNormal
            font.features: { "tnum": 1 }
            color: isError ? Theme.dangerText : modelData.assigned !== "" && !isTotal ? Theme.fgMuted : Theme.fg
            Accessible.role: Accessible.StaticText
            Accessible.name: (isError ? qsTr("Error: %1") : qsTr("Result: %1")).arg(text)
            Rectangle {
                visible: result.isTotal && !result.isError
                anchors.right: parent.right
                anchors.top: parent.top
                width: Math.min(parent.width, parent.contentWidth + 4)
                height: 1
                color: Theme.borderStrong
            }
        }
    }
}
