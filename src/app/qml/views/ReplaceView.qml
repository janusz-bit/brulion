pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ReplaceView — route "replace" ({query?, replacement?}; docs/UI.md "Find and replace"): text replaced
// in every note, seen first. The query and the replacement, what a match must fit, then the notes
// that have it (Workspace.replacing: looked through on other threads, a moment after typing stops),
// each with its hits as "before → after" (from a note, so always plain text), a tick per note and per
// hit. Everything is ticked at first: the preview is what is reviewed. "Zamień (N)" asks once more and
// writes in slices; the toast's "Cofnij" restores every file still holding what was written, also
// from another view. A hit inside a link says so: the link will point elsewhere (renaming a page is
// what keeps links). A hit opens its note at its line; the route keeps the query for Back.
FocusScope {
    id: root

    property var route: ({})
    readonly property ReplaceSession session: Workspace.replacing
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)

    focus: true
    Component.onCompleted: {
        session.reset()
        session.replacement = route.replacement || ""
        session.query = route.query || ""
        queryField.text = session.query
        replacementField.text = session.replacement
        queryField.forceActiveFocus()
        if (session.query !== "")
            session.scanNow()
    }
    Component.onDestruction: session.reset()

    function open(path, line) {
        AppController.replaceRoute({ view: "replace", query: session.query, replacement: session.replacement })
        AppController.navigate({ view: "page", path: path, line: line - 1 })
    }

    Connections {
        target: root.session
        function onApplied(token, hits, files, changed, failed) {
            if (hits > 0) {
                // A bound method, not a function of this view: the toast outlives the view.
                AppController.toastAction(qsTr("%n occurrence(s) replaced", "", hits) + " · " + qsTr("%n note(s)", "", files),
                                          "success", qsTr("Undo"), root.session.undo.bind(root.session, token))
            }
            if (changed.length > 0)
                AppController.toast(qsTr("Left as they are, changed since the preview: %1").arg(changed.join(", ")), "warning")
            if (failed.length > 0)
                AppController.toast(qsTr("Could not write: %1").arg(failed.join(", ")), "error")
            root.session.scanNow()
        }
        function onUndone(restored, left) {
            AppController.toast(left.length > 0
                                ? qsTr("%n note(s) restored", "", restored) + " · "
                                  + qsTr("Changed since, left as they are: %1").arg(left.join(", "))
                                : qsTr("%n note(s) restored", "", restored),
                                left.length > 0 ? "warning" : "info")
            if (root.session.query !== "")
                root.session.scanNow()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    component OptionButton: BButton {
        id: option
        required property string tip
        size: "small"
        checkable: true
        variant: checked ? "secondary" : "ghost"
        Accessible.name: tip
        BToolTip { visible: option.hovered && !Theme.touchUi; text: option.tip }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.sideMargin
        anchors.rightMargin: root.sideMargin
        anchors.topMargin: Theme.compact ? Theme.space3 : Theme.space6
        spacing: Theme.space3

        BSearchField {
            id: queryField
            objectName: "replaceQuery"
            Layout.fillWidth: true
            debounce: 0 // (the session waits itself)
            placeholderText: qsTr("Find in every note")
            onTextEdited: root.session.query = text
            onCleared: root.session.query = ""
            onAccepted: root.session.scanNow()
        }
        BTextField {
            id: replacementField
            objectName: "replaceWith"
            Layout.fillWidth: true
            leadingIcon: "pencil"
            placeholderText: qsTr("Replace with")
            onTextChanged: root.session.replacement = text
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space1
            OptionButton {
                objectName: "replaceMatchCase"
                text: "Aa"
                tip: qsTr("Match case")
                checked: root.session.matchCase
                onToggled: root.session.matchCase = checked
            }
            OptionButton {
                objectName: "replaceMatchDiacritics"
                text: "ąa"
                tip: qsTr("Match Polish letters and other diacritics")
                checked: root.session.matchDiacritics
                onToggled: root.session.matchDiacritics = checked
            }
            OptionButton {
                objectName: "replaceWholeWord"
                text: qsTr("Word")
                tip: qsTr("Whole words only")
                checked: root.session.wholeWord
                onToggled: root.session.wholeWord = checked
            }
        }

        BProgressBar {
            visible: root.session.scanning || root.session.applying
            Layout.fillWidth: true
            value: root.session.applying ? root.session.applyProgress : root.session.scanProgress
        }

        Flow {
            visible: root.session.fileCount > 0
            Layout.fillWidth: true
            spacing: Theme.space3

            BCheckBox {
                objectName: "replaceTickAll"
                text: qsTr("%n occurrence(s)", "", root.session.hitCount) + " · " + qsTr("%n note(s)", "", root.session.fileCount)
                tristate: true
                nextCheckState: () => root.session.tickedHits === root.session.hitCount ? Qt.Unchecked : Qt.Checked
                onClicked: root.session.tickAll(root.session.tickedHits !== root.session.hitCount)
                Binding on checkState {
                    value: root.session.tickedHits === 0 ? Qt.Unchecked
                           : root.session.tickedHits === root.session.hitCount ? Qt.Checked : Qt.PartiallyChecked
                }
            }
            BButton {
                objectName: "replaceApply"
                variant: "primary"
                iconName: "check"
                enabled: root.session.tickedHits > 0 && !root.session.scanning && !root.session.applying
                text: qsTr("Replace (%1)").arg(root.session.tickedHits)
                onClicked: confirmDialog.open()
            }
        }
        BText {
            visible: root.session.hiddenHits > 0 || root.session.skippedNotes > 0
            Layout.fillWidth: true
            variant: "muted"
            wrapMode: Text.Wrap
            text: {
                const parts = []
                if (root.session.hiddenHits > 0)
                    parts.push(qsTr("%n more not shown: they are replaced with their ticked note.", "", root.session.hiddenHits))
                if (root.session.skippedNotes > 0)
                    parts.push(qsTr("%n note(s) left out: not UTF-8 text, or too large.", "", root.session.skippedNotes))
                return parts.join(" ")
            }
        }

        ListView {
            id: list
            objectName: "replaceList"
            TouchFlick.enabled: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: -Theme.space2
            Layout.rightMargin: -Theme.space2
            clip: true
            model: root.session
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space8
            T.ScrollBar.vertical: BScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Occurrences to replace")

            delegate: Item {
                id: row
                required property int index
                required property string kind
                required property string path
                required property string title
                required property string folder
                required property int hits
                required property int tick
                required property int hit
                required property int line
                required property string before
                required property string matched
                required property string after
                required property bool inLink

                width: list.width
                height: row.kind === "file" ? fileRow.implicitHeight : row.kind === "hit" ? hitRow.implicitHeight : moreText.implicitHeight + Theme.space2

                // A note: its tick for all its hits, its name, its count.
                RowLayout {
                    id: fileRow
                    visible: row.kind === "file"
                    width: parent.width
                    spacing: Theme.space1
                    BCheckBox {
                        objectName: "replaceFileTick"
                        Layout.leftMargin: Theme.space2
                        tristate: true
                        Accessible.name: qsTr("Replace in %1").arg(row.title)
                        nextCheckState: () => row.tick === 2 ? Qt.Unchecked : Qt.Checked
                        onClicked: root.session.setFileTicked(row.path, row.tick !== 2)
                        Binding on checkState {
                            value: row.tick === 0 ? Qt.Unchecked : row.tick === 2 ? Qt.Checked : Qt.PartiallyChecked
                        }
                    }
                    BText {
                        Layout.fillWidth: true
                        text: row.folder !== "" ? row.title + "  ·  " + row.folder : row.title
                        font.weight: Theme.weightSemiBold
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                    BBadge {
                        Layout.rightMargin: Theme.space2
                        count: row.hits
                        max: 9999
                    }
                }

                // A hit: its tick, the line, before → after; a tap on the text opens the note there.
                RowLayout {
                    id: hitRow
                    visible: row.kind === "hit"
                    width: parent.width
                    spacing: Theme.space1
                    BCheckBox {
                        objectName: "replaceHitTick"
                        Layout.leftMargin: Theme.space6
                        Layout.alignment: Qt.AlignTop
                        Accessible.name: qsTr("Replace on line %1").arg(row.line)
                        onToggled: root.session.setHitTicked(row.path, row.hit, checked)
                        Binding on checked { value: row.tick === 2 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.rightMargin: Theme.space2
                        spacing: 0
                        // From a note: escaped by hand for the strikeout and bold, never its raw HTML.
                        Text {
                            objectName: "replaceHitText"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textSm
                            color: Theme.fg
                            text: row.line + ": " + TextHelper.escapeHtml(row.before)
                                  + "<s>" + TextHelper.escapeHtml(row.matched) + "</s> → <b>"
                                  + TextHelper.escapeHtml(root.session.replacement) + "</b>"
                                  + TextHelper.escapeHtml(row.after)
                            TapHandler { onTapped: root.open(row.path, row.line) }
                        }
                        BText {
                            visible: row.inLink
                            Layout.fillWidth: true
                            variant: "muted"
                            wrapMode: Text.Wrap
                            text: qsTr("Inside a link: it will point elsewhere.")
                        }
                    }
                }

                BText {
                    id: moreText
                    visible: row.kind === "more"
                    x: Theme.space6 + Theme.space2
                    width: parent.width - x
                    variant: "muted"
                    text: qsTr("and %n more", "", row.hits)
                }
            }

            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: !root.session.scanning && root.session.query !== "" && root.session.fileCount === 0
                iconName: "search"
                title: qsTr("No results")
                description: qsTr("No note has this text. Trash, drawings and other files are not searched.")
            }
            BEmptyState {
                parent: list
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                visible: root.session.query === ""
                iconName: "search"
                title: qsTr("Replace in notebook")
                description: qsTr("Type the text to find: nothing changes until you have seen the notes that have it and confirmed.")
            }
        }
    }

    BConfirmDialog {
        id: confirmDialog
        objectName: "replaceConfirm"
        parent: T.Overlay.overlay
        title: qsTr("Replace in notebook?")
        message: qsTr("%n occurrence(s)", "", root.session.tickedHits) + " · " + qsTr("%n note(s)", "", root.session.tickedFiles)
        detail: qsTr("You can undo right after, and each note's version history keeps its old text.")
        confirmText: qsTr("Replace")
        onAccepted: root.session.apply()
    }
}
