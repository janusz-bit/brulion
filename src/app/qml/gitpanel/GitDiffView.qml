pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// GitDiffView — one file's diff in the changes panel (docs/CODE-GIT.md "The panel"): the hunks of a
// line diff (CodeGit.diffRows, gitdiffrows.h), each under its "@@ -a,b +c,d @@" header with, for the
// Uncommitted source, Stage / Unstage / Discard of that hunk; "N unchanged lines" rows that show the
// whole file; one below the other or side by side, lines wrapped or scrolled sideways, whitespace
// changes hidden (the panel's view options); find in the diff (Ctrl+F while it has focus, Enter /
// Shift+Enter go through the matches, the whole file shown meanwhile); lines selected — a click on
// a line, Shift+click for a range — and "Add to chat" with a comment (`commented(text)`: the file,
// the lines in a fenced block, the comment; CodeGit.reviewComment). Lines are plain text, or rich
// text of escaped pieces (CodeGit.lineHtml: changed words, found text): a file can hold anything.
//
//   GitDiffView { git: repo; path: "src/a.cpp"; oldText: …; newText: …; canStage: true
//                 onHunkRequested: (action, first, last) => …; onCommented: (text) => … }
FocusScope {
    id: view
    objectName: "gitDiffView"

    property CodeGit git: null
    property string path
    property string oldText
    property string newText
    property string oldLabel: qsTr("Before")
    property string newLabel: qsTr("After")
    property bool split: false
    property bool wrap: true
    property bool ignoreWhitespace: false
    /// Every line shown (no "unchanged lines" rows).
    property bool expanded: false
    /// The hunks' actions (the Uncommitted source): what the file allows.
    property bool canStage: false
    property bool canUnstage: false
    /// The selected lines alone can be staged / thrown away (linesRequested()).
    property bool canStageLines: false
    property bool canDiscardLines: false
    property bool canDiscard: false
    /// A hunk of another source (a turn, the branch, a commit) can be taken out of the files alone.
    property bool canRevert: false

    /// Find in the diff.
    property bool finding: false
    property string query
    property int matchIndex: 0
    /// The lines chosen for a comment: rows [selectionStart, selectionEnd] (-1 none).
    property int selectionStart: -1
    property int selectionEnd: -1

    readonly property list<var> noRows: []
    readonly property bool showAll: view.expanded || (view.finding && view.query !== "")
    readonly property list<var> rows: view.git !== null
                                      ? view.git.diffRows(view.oldText, view.newText, view.split, view.ignoreWhitespace,
                                                          view.showAll ? -1 : 3)
                                      : view.noRows
    readonly property list<string> rowKinds: UiModels.stringFields(view.rows, "kind")
    readonly property bool hasSkips: view.rowKinds.indexOf("skip") >= 0
    readonly property list<int> matches: view.matchRows()
    readonly property int currentMatch: view.matches.length > 0 ? view.matches[Math.min(view.matchIndex, view.matches.length - 1)] : -1
    readonly property bool hasSelection: view.selectionStart >= 0
    readonly property int selectedFirst: Math.min(view.selectionStart, view.selectionEnd)
    readonly property int selectedLast: Math.max(view.selectionStart, view.selectionEnd)
    readonly property bool sideways: !view.wrap && !view.split
    readonly property real charWidth: mono.height > 0 ? mono.advanceWidth("0") : 8
    readonly property real gutterWidth: view.charWidth * 5 + 12
    readonly property real contentWidth: view.sideways
                                         ? Math.max(list.width, 2 * view.gutterWidth + 24 + view.charWidth * (view.git !== null ? view.git.longestLine(view.rows) : 0))
                                         : list.width
    readonly property string delStrongText: ThemePalette.colorText(Theme.diffDelStrong.r, Theme.diffDelStrong.g,
                                                                   Theme.diffDelStrong.b, Theme.diffDelStrong.a)
    readonly property string addStrongText: ThemePalette.colorText(Theme.diffAddStrong.r, Theme.diffAddStrong.g,
                                                                   Theme.diffAddStrong.b, Theme.diffAddStrong.a)
    readonly property string markText: ThemePalette.colorText(Theme.mark.r, Theme.mark.g, Theme.mark.b, Theme.mark.a)

    /// A hunk's action: "stage" | "unstage" | "discard", with the lines it changes — the work
    /// tree's (stage, discard) or HEAD's (unstage).
    signal hunkRequested(string action, int first, int last)
    /// The selected rows' changed lines: "stage" | "discard", rows [from, to] of `rows`.
    signal linesRequested(string action, int from, int to)
    /// A comment on the selected lines, for the chat.
    signal commented(string text)

    function matchRows(): list<int> {
        if (view.git === null || !view.finding || view.query === "")
            return []
        return view.git.findRows(view.rows, view.query)
    }
    function openFind(): void {
        view.finding = true
        findField.forceActiveFocus()
        findField.selectAll()
    }
    function closeFind(): void {
        view.finding = false
        view.query = ""
        findField.text = ""
        list.forceActiveFocus()
    }
    function nextMatch(step: int): void {
        const count = view.matches.length
        if (count === 0)
            return
        view.matchIndex = (view.matchIndex + step + count) % count
        list.positionViewAtIndex(view.currentMatch, ListView.Center)
    }
    function selectRow(row: int, extend: bool): void {
        const kind = view.rowKinds[row]
        if (kind === "hunk" || kind === "skip")
            return
        if (extend && view.selectionStart >= 0) {
            view.selectionEnd = row
        } else {
            view.selectionStart = row
            view.selectionEnd = row
        }
        commentField.forceActiveFocus()
    }
    function clearSelection(): void {
        view.selectionStart = -1
        view.selectionEnd = -1
        commentField.text = ""
    }
    function commentText(): string {
        if (view.git === null)
            return ""
        const comment = commentField.text
        return view.git.reviewComment(view.path, view.rows, view.selectedFirst, view.selectedLast, comment)
    }
    function addComment(): void {
        if (view.git === null || !view.hasSelection)
            return
        const text = view.commentText()
        view.clearSelection()
        view.commented(text)
    }
    /// The rich text of a line (changed words: `hasSegs`, found text) — "" for plain text.
    function htmlOf(line: string, segs: list<var>, hasSegs: bool, strong: string): string {
        const query = view.finding ? view.query : ""
        if (view.git === null || (!hasSegs && query === ""))
            return ""
        return view.git.lineHtml(line, segs, strong, query, view.markText, view.wrap)
    }

    onPathChanged: view.clearSelection()
    onRowsChanged: {
        if (view.selectionStart >= view.rows.length)
            view.clearSelection()
    }
    onQueryChanged: view.matchIndex = 0

    implicitWidth: 640
    implicitHeight: 360

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Changes of %1").arg(view.path)

    FontMetrics {
        id: mono
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
    }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: view.visible && (list.activeFocus || findField.activeFocus || commentField.activeFocus)
        onActivated: view.openFind()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.space2

        // ---- Find --------------------------------------------------------------------------------
        RowLayout {
            objectName: "gitDiffFindBar"
            Layout.fillWidth: true
            visible: view.finding
            spacing: Theme.space1
            BTextField {
                id: findField
                objectName: "gitDiffFind"
                Layout.fillWidth: true
                accessibleName: qsTr("Find in the diff")
                placeholderText: qsTr("Find in the diff")
                onTypedTextChanged: view.query = findField.typedText
                onAccepted: view.nextMatch(1)
                Keys.onEscapePressed: view.closeFind()
                Keys.onPressed: (event) => {
                    const shiftEnter = (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                       && (event.modifiers & Qt.ShiftModifier) !== 0
                    if (shiftEnter) {
                        view.nextMatch(-1)
                        event.accepted = true
                    }
                }
            }
            BText {
                objectName: "gitDiffFindCount"
                visible: view.query !== ""
                text: view.matches.length === 0 ? qsTr("No matches")
                    : qsTr("%1 of %2").arg(Math.min(view.matchIndex, view.matches.length - 1) + 1).arg(view.matches.length)
                variant: "faint"
                font.pixelSize: Theme.textSm
            }
            BIconButton {
                objectName: "gitDiffFindPrevious"
                iconName: "chevron-up"
                label: qsTr("Previous match")
                shortcut: "Shift+Enter"
                size: "small"
                enabled: view.matches.length > 0
                onClicked: view.nextMatch(-1)
            }
            BIconButton {
                objectName: "gitDiffFindNext"
                iconName: "chevron-down"
                label: qsTr("Next match")
                shortcut: "Enter"
                size: "small"
                enabled: view.matches.length > 0
                onClicked: view.nextMatch(1)
            }
            BIconButton {
                objectName: "gitDiffFindClose"
                iconName: "x"
                label: qsTr("Close the search")
                size: "small"
                onClicked: view.closeFind()
            }
        }

        // ---- The rows ------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.bgElevated
            border.width: 1
            border.color: Theme.border
            clip: true

            BEmptyState {
                objectName: "gitDiffIdentical"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                visible: view.rows.length === 0
                compact: true
                iconName: "check"
                title: qsTr("No differences")
                description: view.ignoreWhitespace && view.oldText !== view.newText
                             ? qsTr("Only whitespace changed (hidden by the view options).")
                             : qsTr("Both versions have the same text.")
            }

            ListView {
                id: list
                objectName: "gitDiffRows"
                visible: view.rows.length > 0
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: view.rows
                contentWidth: view.contentWidth
                flickableDirection: view.sideways ? Flickable.HorizontalAndVerticalFlick : Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                TouchFlick.enabled: true
                T.ScrollBar.vertical: BScrollBar {}
                T.ScrollBar.horizontal: BScrollBar {}
                Keys.onEscapePressed: {
                    if (view.finding)
                        view.closeFind()
                    else
                        view.clearSelection()
                }
                delegate: RowLoader {
                    width: view.contentWidth
                    sourceComponent: kind === "hunk" ? hunkRow : kind === "skip" ? skipRow : view.split ? splitRow : unifiedRow
                }
            }
        }

        // ---- A comment on the selected lines ---------------------------------------------------------
        RowLayout {
            objectName: "gitDiffCommentBar"
            Layout.fillWidth: true
            visible: view.hasSelection
            spacing: Theme.space1
            BText {
                objectName: "gitDiffSelection"
                text: view.selectedFirst === view.selectedLast ? qsTr("1 line") : qsTr("%1 lines").arg(view.selectedLast - view.selectedFirst + 1)
                variant: "faint"
                font.pixelSize: Theme.textSm
            }
            BTextField {
                id: commentField
                objectName: "gitDiffComment"
                Layout.fillWidth: true
                accessibleName: qsTr("Comment on the selected lines")
                placeholderText: qsTr("A comment for the agent (optional)…")
                onAccepted: view.addComment()
                Keys.onEscapePressed: view.clearSelection()
            }
            BButton {
                objectName: "gitDiffStageLines"
                visible: view.canStageLines
                text: qsTr("Stage lines")
                size: "small"
                variant: "ghost"
                onClicked: view.linesRequested("stage", view.selectedFirst, view.selectedLast)
            }
            BButton {
                objectName: "gitDiffDiscardLines"
                visible: view.canDiscardLines
                text: qsTr("Discard lines…")
                size: "small"
                variant: "ghost"
                onClicked: view.linesRequested("discard", view.selectedFirst, view.selectedLast)
            }
            BButton {
                objectName: "gitDiffCommentAdd"
                text: qsTr("Add to chat")
                iconName: "message-square"
                size: "small"
                onClicked: view.addComment()
            }
            BIconButton {
                objectName: "gitDiffCommentCancel"
                iconName: "x"
                label: qsTr("Clear the selection")
                size: "small"
                onClicked: view.clearSelection()
            }
        }
    }

    // ---- Rows ---------------------------------------------------------------------------------------
    /// A row of the diff (the delegate): its keys (gitdiffrows.h), read by the row it loads.
    component RowLoader: Loader {
        required property int index
        required property string kind
        required property int hunk
        required property int oldNo
        required property int newNo
        required property string line
        required property string leftLine
        required property string rightLine
        required property list<var> segs
        required property list<var> leftSegs
        required property list<var> rightSegs
        required property int hiddenLines
        required property int oldFirst
        required property int oldLast
        required property int newFirst
        required property int newLast
    }

    component LineText: Text {
        property string plain
        property list<var> segs
        /// The highlight of its changed words.
        property string strong
        readonly property string html: view.htmlOf(plain, segs, segs.length > 0, strong)
        height: Math.max(1, implicitHeight) // (rich text grown from 0 is laid out again: docs/UI.md)
        text: html !== "" ? html : plain
        textFormat: html !== "" ? Text.RichText : Text.PlainText
        wrapMode: view.wrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
        elide: view.wrap || !view.split ? Text.ElideNone : Text.ElideRight
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        color: Theme.fg
        lineHeight: 1.15
    }

    component Gutter: Text {
        textFormat: Text.PlainText
        width: view.gutterWidth
        horizontalAlignment: Text.AlignRight
        rightPadding: 6
        topPadding: 3
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs + 1
        color: Theme.fgFaint
    }

    /// The click and Shift+click that select lines, on a row.
    component SelectArea: MouseArea {
        property int row: -1
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (mouse) => {
            const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
            list.forceActiveFocus()
            view.selectRow(row, shift)
        }
        // (Touch: a long press extends the selection to this line.)
        onPressAndHold: view.selectRow(row, true)
    }

    Component {
        id: hunkRow
        Rectangle {
            id: hrow
            readonly property RowLoader row: parent as RowLoader
            objectName: "gitHunk_" + (hrow.row ? hrow.row.hunk : -1)
            implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 0, 28)
            color: Theme.bgSunken
            Rectangle { width: parent.width; height: 1; color: Theme.borderSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space2
                anchors.rightMargin: Theme.space2
                spacing: Theme.space1
                Text {
                    Layout.fillWidth: true
                    text: hrow.row ? hrow.row.line : ""
                    textFormat: Text.PlainText
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textXs + 1
                    color: Theme.fgMuted
                    elide: Text.ElideRight
                }
                BButton {
                    objectName: "gitHunkStage"
                    visible: view.canStage
                    text: qsTr("Stage")
                    variant: "ghost"
                    size: "small"
                    onClicked: view.hunkRequested("stage", hrow.row.newFirst, hrow.row.newLast)
                }
                BButton {
                    objectName: "gitHunkUnstage"
                    visible: view.canUnstage
                    text: qsTr("Unstage")
                    variant: "ghost"
                    size: "small"
                    onClicked: view.hunkRequested("unstage", hrow.row.oldFirst, hrow.row.oldLast)
                }
                BButton {
                    objectName: "gitHunkDiscard"
                    visible: view.canDiscard
                    text: qsTr("Discard…")
                    variant: "ghost"
                    size: "small"
                    onClicked: view.hunkRequested("discard", hrow.row.newFirst, hrow.row.newLast)
                }
                BButton {
                    objectName: "gitHunkRevert"
                    visible: view.canRevert
                    text: qsTr("Revert…")
                    variant: "ghost"
                    size: "small"
                    onClicked: view.hunkRequested("revert", hrow.row.newFirst, hrow.row.newLast)
                }
            }
        }
    }

    Component {
        id: skipRow
        T.AbstractButton {
            id: skip
            readonly property RowLoader row: parent as RowLoader
            implicitHeight: Theme.touchUi ? Theme.touchTarget : 26
            hoverEnabled: !Theme.touchUi
            focusPolicy: Qt.StrongFocus
            text: qsTr("%n unchanged line(s)", "", skip.row ? skip.row.hiddenLines : 0)
            Accessible.role: Accessible.Button
            Accessible.name: text
            onClicked: view.expanded = true
            background: Rectangle {
                color: skip.hovered ? Theme.bgHover : Theme.bgSunken
                BFocusRing { visible: skip.visualFocus; inset: true; targetRadius: 0 }
            }
            contentItem: Row {
                spacing: 8
                leftPadding: 10
                BIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevrons-up-down"
                    size: Theme.iconSizeSm
                    color: Theme.fgFaint
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: skip.text
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: skip.hovered ? Theme.fg : Theme.fgMuted
                }
            }
        }
    }

    Component {
        id: unifiedRow
        Rectangle {
            id: urow
            readonly property RowLoader row: parent as RowLoader
            readonly property int rowIndex: urow.row ? urow.row.index : -1
            readonly property string kind: urow.row ? urow.row.kind : ""
            readonly property bool del: urow.kind === "delete"
            readonly property bool ins: urow.kind === "insert"
            readonly property bool selected: view.hasSelection && urow.rowIndex >= view.selectedFirst && urow.rowIndex <= view.selectedLast
            implicitHeight: Math.ceil(Math.max(lineText.implicitHeight + 6, 22))
            color: urow.rowIndex === view.currentMatch ? Theme.mark
                 : urow.del ? Theme.diffDelBg : urow.ins ? Theme.diffAddBg : "transparent"
            Accessible.role: Accessible.StaticText
            Accessible.name: (urow.del ? qsTr("Removed: ") : urow.ins ? qsTr("Added: ") : "") + (urow.row ? urow.row.line : "")

            Rectangle {
                anchors.fill: parent
                visible: urow.selected
                color: Theme.selection
                opacity: 0.35
            }
            Row {
                id: gutters
                Gutter { text: urow.row && urow.row.oldNo > 0 ? "" + urow.row.oldNo : "" }
                Gutter { text: urow.row && urow.row.newNo > 0 ? "" + urow.row.newNo : "" }
                Text {
                    width: 16
                    topPadding: 3
                    textFormat: Text.PlainText
                    text: urow.del ? "−" : urow.ins ? "+" : ""
                    color: urow.del ? Theme.diffDelFg : Theme.diffAddFg
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textSm
                    font.weight: Theme.weightSemiBold
                }
            }
            LineText {
                id: lineText
                x: gutters.width
                y: 3
                width: view.sideways ? Math.max(implicitWidth, urow.width - x - 8) : urow.width - x - 8
                plain: urow.row ? urow.row.line : ""
                segs: urow.row ? urow.row.segs : view.noRows
                strong: urow.del ? view.delStrongText : view.addStrongText
            }
            SelectArea {
                anchors.fill: parent
                row: urow.rowIndex
            }
        }
    }

    Component {
        id: splitRow
        Item {
            id: srow
            readonly property RowLoader row: parent as RowLoader
            readonly property int rowIndex: srow.row ? srow.row.index : -1
            readonly property string kind: srow.row ? srow.row.kind : ""
            readonly property bool leftChanged: srow.kind === "delete" || srow.kind === "change"
            readonly property bool rightChanged: srow.kind === "insert" || srow.kind === "change"
            readonly property bool selected: view.hasSelection && srow.rowIndex >= view.selectedFirst && srow.rowIndex <= view.selectedLast
            readonly property real half: width / 2
            implicitHeight: Math.ceil(Math.max(leftText.implicitHeight, rightText.implicitHeight, 16) + 6)
            Accessible.role: Accessible.StaticText
            Accessible.name: srow.kind === "equal" ? (srow.row ? srow.row.leftLine : "")
                           : Js.arg2(qsTr("Before: %1. After: %2"), srow.row ? srow.row.leftLine : "", srow.row ? srow.row.rightLine : "")

            Rectangle {
                width: srow.half
                height: parent.height
                color: srow.rowIndex === view.currentMatch ? Theme.mark
                     : srow.leftChanged ? Theme.diffDelBg : srow.kind === "insert" ? Theme.bgSunken : "transparent"
            }
            Rectangle {
                x: srow.half
                width: srow.half
                height: parent.height
                color: srow.rowIndex === view.currentMatch ? Theme.mark
                     : srow.rightChanged ? Theme.diffAddBg : srow.kind === "delete" ? Theme.bgSunken : "transparent"
            }
            Rectangle {
                anchors.fill: parent
                visible: srow.selected
                color: Theme.selection
                opacity: 0.35
            }
            Rectangle { x: srow.half - 1; width: 1; height: parent.height; color: Theme.border }
            Gutter {
                id: leftNo
                text: srow.row && srow.row.oldNo > 0 ? "" + srow.row.oldNo : ""
            }
            LineText {
                id: leftText
                x: leftNo.width
                y: 3
                width: srow.half - x - 8
                plain: srow.row ? srow.row.leftLine : ""
                segs: srow.row ? srow.row.leftSegs : view.noRows
                strong: view.delStrongText
            }
            Gutter {
                id: rightNo
                x: srow.half
                text: srow.row && srow.row.newNo > 0 ? "" + srow.row.newNo : ""
            }
            LineText {
                id: rightText
                x: srow.half + rightNo.width
                y: 3
                width: srow.half - rightNo.width - 8
                plain: srow.row ? srow.row.rightLine : ""
                segs: srow.row ? srow.row.rightSegs : view.noRows
                strong: view.addStrongText
            }
            SelectArea {
                anchors.fill: parent
                row: srow.rowIndex
            }
        }
    }
}
