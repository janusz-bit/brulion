pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// FindBar — find and replace in the open note (docs/UI.md "Find and replace"): at the top of a page
// or of the journal stream (`stream`: StreamFind over its days — the count, next / previous and
// Replace all span them, "3 z 12 · 4 dni"; the current match is `find`'s, in the day written in).
// The query field, "3 z 12", previous and
// next (Enter, Shift+Enter, F3), what a match must fit (letter case, Polish letters, whole word) and a
// row for replacing that opens with Ctrl+H or the arrow. Esc closes it (closeRequested(): the owner
// gives the keyboard back). The logic is NoteFind's; its buttons never take the keyboard from the
// field (Enter goes on to the next match; on a phone the keyboard stays up).
Rectangle {
    id: bar
    objectName: "findBar"

    required property NoteFind find
    required property BlockEditor editor
    /// The journal stream's search over its days (null: one note).
    property StreamFind stream: null
    /// What is searched when it is not plain.
    property string scopeLabel: ""
    /// The field's placeholder when not the default one.
    property string placeholder: ""
    property bool replaceOpen: false

    readonly property int matchCount: stream ? stream.count : find.count
    readonly property int currentMatch: stream ? stream.current : find.current
    function goNext() {
        if (bar.stream)
            bar.stream.next()
        else
            bar.find.next()
    }
    function goPrevious() {
        if (bar.stream)
            bar.stream.previous()
        else
            bar.find.previous()
    }
    function replaceOne(text) {
        if (bar.stream)
            bar.stream.replaceCurrent(text)
        else
            bar.find.replaceCurrent(text)
    }

    signal closeRequested()

    function focusField() {
        field.forceActiveFocus()
        field.selectAll()
    }
    function focusReplacement() {
        replacement.forceActiveFocus()
        replacement.selectAll()
    }
    function openReplace() {
        bar.replaceOpen = true
        Qt.callLater(bar.focusReplacement)
    }
    function hasKeyboard(): bool {
        return field.activeFocus || replacement.activeFocus
    }
    function replaceAll() {
        if (bar.stream) {
            const r = bar.stream.replaceAll(replacement.text)
            if (r.count <= 0 && r.changed.length === 0 && r.failed.length === 0)
                return
            const token = r.token
            const stream = bar.stream
            const editor = bar.editor
            let message = qsTr("%n occurrence(s) replaced", "", r.count)
            if (r.days > 1)
                message += " " + qsTr("in %n day(s)", "", r.days)
            const skipped = r.changed.length + r.failed.length
            if (skipped > 0)
                message += ". " + qsTr("%n day(s) left as they were: they changed meanwhile or could not be written.", "", skipped)
            bar.editor.notice(message, skipped > 0 ? "warning" : "success", token > 0 ? {
                text: qsTr("Undo"),
                run: () => {
                    const u = stream.undo(token)
                    if (u.left.length > 0 && editor)
                        editor.notice(qsTr("Not undone in %n day(s): they changed since", "", u.left.length), "info")
                }
            } : null)
            return
        }
        const ctl = bar.find.controller
        if (!ctl)
            return
        const before = ctl.sourceMode ? null : ctl.undoToken()
        const count = bar.find.replaceAll(replacement.text)
        if (count > 0)
            bar.editor.notice(qsTr("%n occurrence(s) replaced", "", count), "success",
                              before !== null ? bar.editor.undoAction(before) : null)
    }

    /// Keys of both fields: Esc, F3, Ctrl+H; Android's Back closes the bar first.
    function commonKey(event) {
        if (event.key === Qt.Key_Escape) {
            bar.closeRequested()
        } else if (event.key === Qt.Key_F3) {
            if (event.modifiers & Qt.ShiftModifier)
                bar.goPrevious()
            else
                bar.goNext()
        } else {
            return
        }
        event.accepted = true
    }

    implicitHeight: column.implicitHeight + Theme.space2 * 2
    color: Theme.bg
    z: 2

    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Find in note")

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.borderSubtle
    }

    Connections {
        target: bar.find
        function onTargetRequested(target) { bar.editor.showFindTarget(target, bar.hasKeyboard()) }
        function onSourceReplaceRequested(ranges, text) { bar.editor.replaceInSource(ranges, text) }
    }

    component OptionButton: BButton {
        id: option
        required property string tip
        size: "small"
        checkable: true
        variant: checked ? "secondary" : "ghost"
        focusPolicy: Qt.NoFocus
        Accessible.name: tip
        BToolTip { visible: option.hovered && !Theme.touchUi; text: option.tip }
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.space3
        anchors.rightMargin: Theme.space2
        spacing: Theme.space1

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space1

            BIconButton {
                objectName: "findReplaceToggle"
                size: "small"
                iconName: bar.replaceOpen ? "chevron-down" : "chevron-right"
                label: bar.replaceOpen ? qsTr("Hide replace") : qsTr("Replace")
                shortcut: "Ctrl+H"
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (bar.replaceOpen)
                        bar.replaceOpen = false
                    else
                        bar.openReplace()
                }
            }
            BSearchField {
                id: field
                objectName: "findField"
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                size: "small"
                debounce: 0 // (NoteFind waits itself)
                // Esc closes the bar (below), never only clears its text.
                clearable: false
                placeholderText: bar.placeholder !== "" ? bar.placeholder
                               : bar.scopeLabel !== "" ? qsTr("Find in %1").arg(bar.scopeLabel) : qsTr("Find in note")
                text: bar.find.query
                onTextEdited: bar.find.query = text
                onAccepted: {
                    if (field.lastModifiers & Qt.ShiftModifier)
                        bar.goPrevious()
                    else
                        bar.goNext()
                }
                property int lastModifiers: 0
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: (event) => {
                    field.lastModifiers = event.modifiers
                    bar.commonKey(event)
                }
                Keys.onReleased: (event) => {
                    if (event.key === Qt.Key_Back) {
                        bar.closeRequested()
                        event.accepted = true
                    }
                }
            }
            Text {
                objectName: "findStatus"
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: bar.width * 0.3
                elide: Text.ElideRight
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                text: {
                    if (bar.find.appliedQuery === "")
                        return ""
                    if (bar.matchCount === 0)
                        return qsTr("No results")
                    let counted = qsTr("%n result(s)", "", bar.matchCount)
                    if (bar.currentMatch >= 0)
                        //: Matches in the note: "3 z 12"
                        counted = qsTr("%1 of %2").arg(bar.currentMatch + 1).arg(bar.matchCount)
                    // The journal stream: in how many days.
                    return bar.stream && bar.stream.dayCount > 1
                            ? counted + " · " + qsTr("%n day(s)", "", bar.stream.dayCount) : counted
                }
            }
            BIconButton {
                objectName: "findPrevious"
                size: "small"
                iconName: "chevron-up"
                label: qsTr("Previous result")
                shortcut: "Shift+F3"
                enabled: bar.matchCount > 0
                focusPolicy: Qt.NoFocus
                onClicked: bar.goPrevious()
            }
            BIconButton {
                objectName: "findNext"
                size: "small"
                iconName: "chevron-down"
                label: qsTr("Next result")
                shortcut: "F3"
                enabled: bar.matchCount > 0
                focusPolicy: Qt.NoFocus
                onClicked: bar.goNext()
            }
            BIconButton {
                objectName: "findClose"
                size: "small"
                iconName: "x"
                label: qsTr("Close find")
                shortcut: "Esc"
                focusPolicy: Qt.NoFocus
                onClicked: bar.closeRequested()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space1

            OptionButton {
                objectName: "findMatchCase"
                text: "Aa"
                tip: qsTr("Match case")
                checked: bar.find.matchCase
                onToggled: bar.find.matchCase = checked
            }
            OptionButton {
                objectName: "findMatchDiacritics"
                text: "ąa"
                tip: qsTr("Match Polish letters and other diacritics")
                checked: bar.find.matchDiacritics
                onToggled: bar.find.matchDiacritics = checked
            }
            OptionButton {
                objectName: "findWholeWord"
                text: qsTr("Word")
                tip: qsTr("Whole words only")
                checked: bar.find.wholeWord
                onToggled: bar.find.wholeWord = checked
            }
            BButton {
                objectName: "findInNotebook"
                size: "small"
                variant: "ghost"
                text: qsTr("In the whole notebook…")
                focusPolicy: Qt.NoFocus
                visible: !AppController.inDocsVault
                onClicked: AppController.navigate({ view: "replace", query: bar.find.query, replacement: replacement.text })
            }
        }

        // What Replace all would also change: the bar says so before it does.
        BText {
            objectName: "findReplaceNote"
            visible: bar.replaceOpen && text !== ""
            Layout.fillWidth: true
            variant: "muted"
            wrapMode: Text.Wrap
            text: {
                const links = bar.stream ? bar.stream.linkMatches : bar.find.linkMatches
                const properties = bar.stream ? bar.stream.frontmatterMatches : bar.find.frontmatterMatches
                const parts = []
                if (links > 0)
                    parts.push(qsTr("%n match(es) inside links: they will point elsewhere.", "", links))
                if (properties > 0)
                    parts.push(qsTr("%n match(es) in the page's properties.", "", properties))
                return parts.join(" ")
            }
        }
        // The journal stream searched only its latest days (StreamFind.dayLimit, its text limit).
        BText {
            objectName: "findLimitNote"
            visible: bar.stream !== null && bar.stream.limited && bar.find.appliedQuery !== ""
            Layout.fillWidth: true
            variant: "muted"
            wrapMode: Text.Wrap
            text: bar.stream ? qsTr("Searched the latest %n day(s). Older ones: In the whole notebook…", "", bar.stream.searchedDays) : ""
        }
        RowLayout {
            visible: bar.replaceOpen
            Layout.fillWidth: true
            spacing: Theme.space1

            BTextField {
                id: replacement
                objectName: "findReplacement"
                Layout.fillWidth: true
                Layout.minimumWidth: 80
                size: "small"
                placeholderText: qsTr("Replace with")
                onAccepted: bar.replaceOne(text)
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: (event) => bar.commonKey(event)
                Keys.onReleased: (event) => {
                    if (event.key === Qt.Key_Back) {
                        bar.closeRequested()
                        event.accepted = true
                    }
                }
            }
            BButton {
                objectName: "findReplaceOne"
                size: "small"
                text: qsTr("Replace")
                enabled: bar.matchCount > 0
                focusPolicy: Qt.NoFocus
                onClicked: bar.replaceOne(replacement.text)
            }
            BButton {
                objectName: "findReplaceAll"
                size: "small"
                text: qsTr("Replace all")
                enabled: bar.matchCount > 0
                focusPolicy: Qt.NoFocus
                onClicked: bar.replaceAll()
            }
        }
    }
}
