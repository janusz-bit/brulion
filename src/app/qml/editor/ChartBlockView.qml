pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ChartBlockView — a ```chart block (docs/SYNTAX.md §9 "Charts"): the numbers of the table, calc
// block or query it names (BlockListModel's chartData) drawn by ChartView — Brulion's own layout,
// nothing from the note is ever run — in the theme's colours on a sunken panel, as wide as the
// column. The fence lines stay in the file; the delegate draws the frame. A click (or Enter on the
// selected block) edits the source in place: the monospace editor of CodeBlockView with the chart
// under it, drawn again as you type (also when the table above changes). A chart Brulion cannot
// draw shows the source with the reason under it (its line) in the danger colour. Screen readers
// get the chart's type, title and every value (ChartView.description).
Rectangle {
    id: root

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    /// The source is on screen: while editing, or when there is no chart to show.
    readonly property bool showSource: root.block.editing || chart.status !== "ok"
    readonly property real codeLine: Theme.lineHeightPx(Theme.textSm, 1.5)
    readonly property real firstLineCenter: showSource ? sourceLoader.y + codeLine / 2
                                                       : chart.y + Math.min(chart.height, codeLine) / 2

    implicitHeight: (message.visible ? message.y + message.implicitHeight
                                     : chart.visible ? chart.y + chart.height
                                                     : sourceLoader.y + sourceLoader.height) + Theme.space3
    height: implicitHeight
    radius: Theme.radius
    color: Theme.bgSunken
    border.width: 1
    border.color: root.block.editing ? Theme.alpha(Theme.focusRing, 0.5) : Theme.borderSubtle

    Text {
        id: label
        visible: root.showSource
        anchors.right: parent.right
        anchors.rightMargin: Theme.space3
        y: Theme.space2
        text: "chart"
        textFormat: Text.PlainText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    // The source: read-only until clicked when shown for an error, edited in place.
    Loader {
        id: sourceLoader
        x: Theme.space4
        y: Theme.space3
        width: parent.width - 2 * Theme.space4 - label.width - Theme.space2
        height: active && item ? (item as Item).implicitHeight : 0
        active: root.showSource
        sourceComponent: InlineEditor {
            objectName: "chartSource"
            width: sourceLoader.width
            block: root.block
            persistent: true
            multiline: true
            highlightMode: "code"
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            lineHeightPx: root.codeLine
            wrapMode: TextEdit.Wrap
        }
    }

    ChartView {
        id: chart
        objectName: "chartView"
        visible: status === "ok"
        x: Theme.space3
        y: root.showSource ? sourceLoader.y + sourceLoader.height + Theme.space3 : Theme.space3
        width: parent.width - 2 * Theme.space3
        height: visible ? implicitHeight : 0
        source: root.block.rowData.editText
        chartData: root.block.rowData.chartData
        english: root.block.controller.locale === "en"
        fontFamily: Theme.fontUi
        fontPixelSize: Theme.textSm
        textColor: Theme.fg
        mutedColor: Theme.fgMuted
        backgroundColor: Theme.bgSunken
        borderColor: Theme.border
        accentColor: Theme.accent
        dark: Theme.dark
        Accessible.role: Accessible.Graphic
        Accessible.name: typeName
        Accessible.description: description

        // Not editing: a click starts editing (Shift / Ctrl+click select blocks), a finger held on
        // it selects the block.
        TapHandler {
            id: tap
            enabled: !root.block.editing
            onTapped: if (!root.block.editor.blockTappedWith(root.block.index, tap.point.modifiers))
                          root.block.editor.startEditing(root.block.index, -1)
        }
        TapHandler {
            enabled: !root.block.editing && Theme.touchUi
            acceptedDevices: PointerDevice.TouchScreen
            onLongPressed: root.block.editor.longPressText(root.block.index)
        }
        HoverHandler {
            enabled: !root.block.editing && !Theme.touchUi
            cursorShape: Qt.PointingHandCursor
        }
    }

    Text {
        id: message
        objectName: "chartMessage"
        visible: chart.status !== "ok"
        x: Theme.space4
        y: sourceLoader.y + sourceLoader.height + Theme.space2
        width: parent.width - 2 * Theme.space4
        text: chart.message
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs
        color: Theme.dangerText
    }
}
