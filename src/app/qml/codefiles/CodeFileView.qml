pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// CodeFileView — a project file's lines in the file viewer (docs/CODE-FILES.md "The file viewer";
// CodeFilesPanel): read-only, a row per line of `document` (CodeFileDocument: its colours as
// StyledText) with its number in a gutter that stays at the left edge while the lines scroll
// sideways; `markedLine` tinted (the line a link or a search opened the file at, scrolled into view
// by showLine()). Lines are selected for a review comment: a click on a line (or its number), Shift
// with a click extends it, a drag over the numbers selects a range; Esc clears. Over a selection a
// bar: "Comment…" (commentRequested(from, to)) and Copy (the lines); a right click has both and
// "Copy the path with the lines". Ctrl+C copies the selected lines.
FocusScope {
    id: view

    property CodeFileDocument document: null
    /// The line marked (0 none).
    property int markedLine: 0
    /// The selected lines, either order (0: none).
    property int selFrom: 0
    property int selTo: 0
    readonly property int selFirst: Math.min(view.selFrom, view.selTo)
    readonly property int selLast: Math.max(view.selFrom, view.selTo)
    readonly property bool hasSelection: view.selFirst > 0
    /// The path shown in "Copy the path with the lines".
    property string path

    signal commentRequested(int from, int to)
    signal copyRequested(string text)

    readonly property int lineCount: view.document !== null ? view.document.lineCount : 0
    readonly property real charWidth: metrics.height > 0 ? metrics.advanceWidth("0") : 7
    readonly property real lineHeight: Math.ceil(metrics.height * 1.35)
    readonly property int digits: ("" + Math.max(9, view.lineCount)).length
    readonly property real gutterWidth: Math.ceil(view.digits * view.charWidth) + 2 * Theme.space2
    readonly property int columns: view.document !== null ? view.document.maxColumns : 0
    readonly property real textWidth: view.gutterWidth + Theme.space2 + Math.ceil((view.columns + 2) * view.charWidth)

    /// Line `line` in view (in the middle) and marked; 0: the file's start.
    function showLine(line: int): void {
        view.markedLine = line
        // (The lines' starts in view: a file opened at a line is read from its left edge.)
        lines.contentX = lines.originX
        if (line <= 0 || line > view.lineCount) {
            lines.positionViewAtBeginning()
            return
        }
        lines.positionViewAtIndex(line - 1, ListView.Center)
    }
    /// The first line in view (1-based; 0 none).
    function topLine(): int {
        return view.lineAtContentY(lines.contentY)
    }
    /// `line` at the top of the view (a file read again keeps its place).
    function scrollTo(line: int): void {
        if (line > 0 && line <= view.lineCount)
            lines.positionViewAtIndex(line - 1, ListView.Beginning)
    }
    function select(line: int, extend: bool): void {
        if (line <= 0)
            return
        if (!extend || view.selFrom <= 0)
            view.selFrom = line
        view.selTo = line
        view.forceActiveFocus()
    }
    function clearSelection(): void {
        view.selFrom = 0
        view.selTo = 0
    }
    function selectedText(): string {
        return view.hasSelection && view.document !== null ? view.document.linesText(view.selFirst, view.selLast) : ""
    }
    function rangeText(): string {
        return view.selLast > view.selFirst ? view.selFirst + "-" + view.selLast : "" + view.selFirst
    }
    /// The line under `y` of the list's content (1-based; 0 none).
    function lineAtContentY(y: real): int {
        const i = lines.indexAt(0, Math.max(0, Math.min(y, lines.contentHeight - 1)))
        return i >= 0 ? i + 1 : 0
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape && view.hasSelection) {
            view.clearSelection()
            event.accepted = true
        } else if (event.matches(StandardKey.Copy) && view.hasSelection) {
            view.copyRequested(view.selectedText())
            event.accepted = true
        }
    }

    FontMetrics {
        id: metrics
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ListView {
        id: lines
        objectName: "codeFileLines"
        anchors.fill: parent
        clip: true
        model: view.document
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        contentWidth: Math.max(width, view.textWidth)
        reuseItems: true
        cacheBuffer: 400
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Lines of %1").arg(view.path)
        T.ScrollBar.vertical: BScrollBar {}
        T.ScrollBar.horizontal: BScrollBar {}

        delegate: Item {
            id: row
            required property int number
            required property string html
            required property int index
            readonly property bool selected: view.hasSelection && row.number >= view.selFirst && row.number <= view.selLast
            readonly property bool marked: row.number === view.markedLine
            width: lines.contentWidth
            height: view.lineHeight
            objectName: "codeFileLine_" + row.number

            Rectangle {
                anchors.fill: parent
                color: row.selected ? Theme.selection : row.marked ? Theme.mark : "transparent"
            }
            Text {
                x: view.gutterWidth + Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.StyledText
                text: row.html
                color: Theme.fg
                font.family: Theme.fontMono
                font.pixelSize: Theme.textSm
            }
            // A click on the line: it alone selected (Shift: the selection grows to it).
            TapHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.NoModifier
                onTapped: (eventPoint, button) => view.select(row.number, false)
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.ShiftModifier
                onTapped: (eventPoint, button) => view.select(row.number, true)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                onTapped: (eventPoint, button) => {
                    if (!row.selected)
                        view.select(row.number, false)
                    lineMenu.popup()
                }
            }
            // The number, at the view's left edge whatever the sideways scroll; a drag over the
            // numbers selects lines.
            Rectangle {
                x: lines.contentX
                width: view.gutterWidth
                height: parent.height
                color: row.selected ? Theme.selection : Theme.bgSunken
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space2
                    anchors.verticalCenter: parent.verticalCenter
                    text: "" + row.number
                    textFormat: Text.PlainText
                    color: row.marked || row.selected ? Theme.fg : Theme.fgFaint
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                }
                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: (mouse) => {
                        const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        view.select(row.number, shift)
                    }
                    onPositionChanged: (mouse) => {
                        const y = row.y + mouse.y
                        const line = view.lineAtContentY(y)
                        if (line > 0)
                            view.selTo = line
                    }
                }
            }
        }
    }

    // ---- Over a selection: Comment…, Copy --------------------------------------------------------
    Rectangle {
        id: selectionBar
        objectName: "codeFileSelectionBar"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.space2
        visible: view.hasSelection
        width: barRow.implicitWidth + 2 * Theme.space1
        height: barRow.implicitHeight + 2 * Theme.space1
        radius: Theme.radiusSm
        color: Theme.bgElevated
        border.width: 1
        border.color: Theme.border
        Row {
            id: barRow
            anchors.centerIn: parent
            spacing: Theme.space1
            BButton {
                objectName: "codeFileComment"
                size: "small"
                variant: "ghost"
                iconName: "message-square"
                text: view.selLast > view.selFirst ? qsTr("Comment on lines %1–%2…").arg(view.selFirst).arg(view.selLast)
                                                   : qsTr("Comment on line %1…").arg(view.selFirst)
                onClicked: view.commentRequested(view.selFirst, view.selLast)
            }
            BIconButton {
                objectName: "codeFileCopyLines"
                size: "small"
                iconName: "copy"
                label: qsTr("Copy the lines")
                shortcut: "Ctrl+C"
                onClicked: view.copyRequested(view.selectedText())
            }
            BIconButton {
                size: "small"
                iconName: "x"
                label: qsTr("Clear the selection")
                shortcut: "Esc"
                onClicked: view.clearSelection()
            }
        }
    }

    BMenu {
        id: lineMenu
        objectName: "codeFileLineMenu"
        BMenuItem {
            text: qsTr("Comment…")
            iconName: "message-square"
            onTriggered: view.commentRequested(view.selFirst, view.selLast)
        }
        BMenuItem {
            text: qsTr("Copy the lines")
            iconName: "copy"
            onTriggered: view.copyRequested(view.selectedText())
        }
        BMenuItem {
            text: qsTr("Copy the path with the lines")
            iconName: "link"
            onTriggered: view.copyRequested(view.path + ":" + view.rangeText())
        }
    }
}
