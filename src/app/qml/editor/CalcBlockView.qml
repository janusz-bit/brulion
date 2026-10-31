pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// CalcBlockView — a ```calc block (docs/SYNTAX.md §9): the input lines on the left, each line's
// result right-aligned on the same line (calcResults role, evaluated in C++ with the page's
// variables). Assignments show their value muted, errors in red; a `sum` line is emphasised.
// The inputs are edited in place (persistent InlineEditor); results follow while typing.
Rectangle {
    id: root

    property BlockDelegate block
    readonly property var results: block.model.calcResults || []
    readonly property int lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.6)
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
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    FontMetrics { id: metrics; font.family: Theme.fontMono; font.pixelSize: Theme.textSm; font.weight: Theme.weightSemiBold }

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
        highlightMode: "code"
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
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
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignRight
            text: isError ? modelData.error : modelData.value
            textFormat: Text.PlainText
            elide: Text.ElideLeft
            font.family: Theme.fontMono
            font.pixelSize: isError ? Theme.textXs : Theme.textSm
            font.weight: isTotal ? Theme.weightSemiBold : Theme.weightNormal
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
