pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentJournalDay — one day of JournalsView: the date as a heading ("środa, 15 października
// 2026", a click opens the day as a page), "Otwórz jako stronę", and the day's note in an
// embedded block editor (its own PageController; the editor's list does not scroll — the
// journal list does — and is as tall as its blocks). A day without a note yet (today) shows a
// prompt; the first edit creates the file — from the vault's journal template when there is one.
//
//   ContentJournalDay { date: model.date; path: model.path; exists: model.exists;
//                       isToday: model.isToday; scroller: list }
//
// Caret visibility is the journal list's job (scroller.ensureVisible(item, rect)): scroll
// requests of the embedded editor (keeping the caret above the keyboard toolbar) are passed on.
// Ctrl+E (Markdown source) opens the day as a page in source mode instead of a source editor
// inside the list. Fold state is kept per day like in PageView. Each day is shown in its own note
// mode (its `brulion:` key, else the notebook's journal mode — outline by default): a day switched
// to a document reads like one in the stream too.
FocusScope {
    id: day

    required property string date
    required property string path
    required property bool exists
    required property bool isToday
    /// The journal list: ensureVisible(item, rect) and scrollBy(dy) (any object with them).
    property var scroller: null
    property bool compactHeader: Theme.compact

    /// This day's editor got the caret (JournalsView stops editing in the others).
    signal activated()

    readonly property alias controller: page
    readonly property alias editor: editor
    readonly property bool editing: page.editingRow >= 0
    /// Nothing to show (also a day holding nothing but its note mode).
    readonly property bool empty: page.loaded && page.blocks.visibleCount === 0
    property bool _foldsRestored: false
    /// The editor has had its width (see its `controller`); stays true.
    property bool _editorSized: false

    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: DatesHelper.formatLong(day.date)

    /// Start writing in a day: a day without a note starts from the journal template (when the
    /// vault has one), the caret in its first empty block. Like a day opened on its own (PageView),
    /// the note is created by the first change, never by starting to write (startNewPage).
    function begin() {
        if (!page.loaded)
            return
        if (page.blocks.visibleCount > 0) {
            editor.startEditing(page.lastVisibleRow(), -1)
            return
        }
        editor.startEditing(page.startNewPage(page.exists ? "" : (Workspace.config.journalTemplate || "")), -1)
    }
    function openAsPage(source) {
        page.save()
        AppController.navigate(source ? { view: "journal", date: day.date, source: true }
                                       : { view: "journal", date: day.date })
    }
    function keepCaretVisible() {
        const edit = editor.activeEditor
        if (day.scroller && edit && edit.activeFocus)
            day.scroller.ensureVisible(edit, edit.cursorRectangle)
    }
    function restoreFolds() {
        if (day._foldsRestored || !page.loaded)
            return
        const folds = Workspace.foldState(page.path)
        if (folds.length > 0)
            page.setFoldState(folds)
        day._foldsRestored = true
    }

    PageController {
        id: page
        objectName: "journalDayController"
        host: Workspace
        path: day.path
        onLoadingChanged: if (page.loaded && !page.loading) day.restoreFolds()
        onFoldStateChanged: {
            if (day._foldsRestored && page.loaded && !page.loading)
                Workspace.setFoldState(page.path, page.foldState)
        }
        onEditingRowChanged: {
            if (page.editingRow >= 0) {
                day.activated()
                Qt.callLater(day.keepCaretVisible)
            }
        }
        // The list is no place for a whole-file editor: the day opens as a page in source mode.
        onSourceModeChanged: {
            if (page.sourceMode) {
                page.sourceMode = false
                day.openAsPage(true)
            }
        }
        onExternallyChanged: (merged) => {
            if (merged)
                AppController.toast(qsTr("Merged changes made elsewhere into this day"), "info")
        }
    }
    Component.onCompleted: restoreFolds()
    // The editor's delegates go first, while the editor still exists (they read its geometry).
    Component.onDestruction: editor.controller = null

    Connections {
        target: Workspace
        function onPathRenamed(oldPath, newPath) {
            if (page.path === oldPath)
                page.followRename(oldPath, newPath)
        }
        // The notebook's journal mode changed (settings, another device).
        function onConfigChanged() { page.refreshMode() }
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: 0

        // ---- The date -------------------------------------------------------------------------------
        // Wide: "środa, 14 października 2026" with a "Dziś" badge. Phones: the weekday (and "Dziś")
        // as a small line above "14 października 2026", so the date fits on one line.
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.leftMargin: editor.contentX + editor.gutter
            Layout.rightMargin: Math.max(Theme.space2, editor.width - editor.contentX - editor.contentWidth)
            spacing: Theme.space2

            readonly property string longDate: DatesHelper.formatLong(day.date)
            readonly property int comma: longDate.indexOf(", ")
            readonly property string weekday: comma > 0 ? longDate.slice(0, comma) : ""
            readonly property string restOfDate: comma > 0 ? longDate.slice(comma + 2) : longDate
            readonly property string relative: day.isToday ? qsTr("Today")
                                                : day.date === DatesHelper.addDays(DatesHelper.today, -1) ? qsTr("Yesterday") : ""

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                BText {
                    visible: day.compactHeader
                    Layout.fillWidth: true
                    text: (headerRow.relative !== "" ? headerRow.relative + " · " : "") + headerRow.weekday
                    variant: "caption"
                    font.weight: Theme.weightSemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.6
                    color: day.isToday ? Theme.accentText : Theme.fgMuted
                    elide: Text.ElideRight
                }
                Text {
                    id: dateText
                    Layout.fillWidth: true
                    text: day.compactHeader ? headerRow.restOfDate : headerRow.longDate
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.family: Theme.fontEditor
                    font.pixelSize: day.compactHeader ? Theme.textXl
                                                      : (day.isToday ? Theme.text3xl : Theme.text2xl)
                    font.weight: Theme.weightSemiBold
                    font.underline: dateHover.hovered
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightTight)
                    color: Theme.fg
                    Accessible.role: Accessible.Heading
                    Accessible.name: headerRow.longDate
                    HoverHandler { id: dateHover; enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: day.openAsPage(false) }
                }
            }
            BBadge {
                visible: !day.compactHeader && headerRow.relative !== ""
                Layout.alignment: Qt.AlignVCenter
                text: headerRow.relative
                variant: day.isToday ? "accent" : "neutral"
                solid: day.isToday
            }
            BButton {
                visible: !day.compactHeader
                Layout.alignment: Qt.AlignVCenter
                size: "small"
                variant: "ghost"
                iconName: "external-link"
                text: qsTr("Open as page")
                onClicked: day.openAsPage(false)
            }
            BIconButton {
                visible: day.compactHeader
                Layout.alignment: Qt.AlignVCenter
                size: "small"
                iconName: "external-link"
                label: qsTr("Open as page")
                onClicked: day.openAsPage(false)
            }
        }

        // ---- Nothing written yet ------------------------------------------------------------------------
        Item {
            visible: day.empty
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            implicitHeight: Math.max(Theme.touchTarget, prompt.implicitHeight + 2 * Theme.space2)

            Rectangle {
                x: editor.contentX + editor.gutter - Theme.space2
                width: Math.min(editor.contentWidth - editor.gutter + 2 * Theme.space2, parent.width - x - Theme.space2)
                height: parent.height
                radius: Theme.radius
                color: promptTap.pressed ? Theme.bgPressed : promptHover.hovered || prompt.activeFocus ? Theme.bgHover : "transparent"
                border.width: 1
                border.color: Theme.borderSubtle

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space3
                    anchors.rightMargin: Theme.space3
                    spacing: Theme.space2
                    BIcon { name: "square-pen"; size: Theme.iconSize; color: Theme.fgMuted }
                    BText {
                        id: prompt
                        Layout.fillWidth: true
                        text: day.isToday ? qsTr("What's on today? Click to start writing.")
                                          : qsTr("Nothing written on this day. Click to start writing.")
                        variant: "muted"
                        wrapMode: Text.Wrap
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.onPressAction: day.begin()
                        Keys.onReturnPressed: day.begin()
                        Keys.onSpacePressed: day.begin()
                    }
                }
                HoverHandler { id: promptHover; enabled: !Theme.touchUi; cursorShape: Qt.IBeamCursor }
                TapHandler { id: promptTap; onTapped: day.begin() }
                BFocusRing { visible: prompt.activeFocus; targetRadius: Theme.radius }
            }
        }

        // ---- The day's blocks -----------------------------------------------------------------------
        BlockEditor {
            id: editor
            objectName: "journalDayEditor"
            Layout.fillWidth: true
            // As tall as the blocks: the journal list scrolls, not this editor. On touch UIs the
            // editing toolbar (on the keyboard, else at the editor's bottom) gets its own room.
            // An empty day shows the prompt instead (the editor stays in the layout for its
            // geometry, which the heading and the prompt use).
            Layout.preferredHeight: day.empty && page.editingRow < 0 ? 0
                                    : editor.listView.contentHeight + editor.listView.y
                                      + (editor.toolbarVisible ? editor.toolbarHeight : 0)
            // The blocks only once the editor has its width: a day created by the journal list gets
            // its width after its children, and blocks laid out at the editor's minimum width made
            // the day several times too tall until the next layout, so the list could release and
            // re-create it without end (scrolled to the end on touch UIs, tst_content_journals_qml).
            controller: day._editorSized ? page : null
            onWidthChanged: if (width > 0) day._editorSized = true
            Component.onCompleted: if (width > 0) day._editorSized = true
            focus: true
            onNotice: (text, kind) => AppController.toast(text, kind)
            listView.interactive: false
            maxTextWidth: AppSettings.editorWidth > 0 ? AppSettings.editorWidth : 100000
            horizontalPadding: Theme.compact ? Theme.space3 : Theme.space8
            bottomSpace: Theme.space3
            // Document days: headings stay below the date (the size of other days' dates).
            headingCap: day.compactHeader ? Theme.textXl : Theme.text2xl
        }
    }

    // Scroll requests of the embedded editor (its list is exactly as tall as its content) go to
    // the journal list instead.
    Connections {
        target: editor.listView
        function onContentYChanged() {
            const lv = editor.listView
            const delta = lv.contentY - lv.originY
            if (Math.abs(delta) < 0.5)
                return
            lv.contentY = lv.originY
            if (day.scroller)
                day.scroller.scrollBy(delta)
        }
    }
    Connections {
        target: editor.activeEditor
        function onCursorRectangleChanged() { day.keepCaretVisible() }
    }
}
