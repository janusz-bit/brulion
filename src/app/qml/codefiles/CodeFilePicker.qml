pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeFilePicker — Code mode's Ctrl+P (docs/CODE-FILES.md "The file picker"; T3 Code's mod+p): the
// project's files by fuzzy match (CodeFileIndex over the folder's index: the base name's hits
// first, the matched letters marked, the folder dimmed after the name), the recent files first
// while nothing is typed. ↑/↓ (Ctrl+N / Ctrl+P too), Page up/down move; Enter opens the file in the
// viewer (fileChosen), Ctrl+Enter in the external editor (editorChosen); Esc closes. Looks like the
// command palette (BPalette): a floating panel in the window's upper third, full screen on phones.
//
//   CodeFilePicker { folder: root.folder; recent: files.recent; onFileChosen: (path) => files.open(path, 0) }
//   picker.openWith("src/")
T.Popup {
    id: control

    property string folder
    property list<string> recent
    property int currentIndex: 0

    signal fileChosen(string path)
    signal editorChosen(string path)

    readonly property int resultCount: files.rows.length
    readonly property real overlayWidth: parent ? parent.width : Theme.windowWidth
    readonly property real overlayHeight: parent ? parent.height : Theme.windowHeight
    readonly property bool fullScreen: Theme.compact
    readonly property real topEdge: control.fullScreen ? 0 : Math.round(control.overlayHeight * 0.12)
    readonly property real listRoom: Math.min(440, control.overlayHeight * 0.72 - 120)
    readonly property real rowHeight: Theme.listItemHeight
    readonly property list<var> hintRows: [
        { keys: "↑+↓", text: qsTr("navigate") },
        { keys: "Enter", text: qsTr("open") },
        { keys: "Ctrl+Enter", text: qsTr("open in editor") },
        { keys: "Esc", text: qsTr("close") }
    ]

    /// Open with `query` typed (its caret at the end).
    function openWith(query: string): void {
        control.open()
        field.text = query
        field.cursorPosition = field.text.length
    }
    function move(delta: int): void {
        const n = control.resultCount
        if (n === 0)
            return
        control.currentIndex = (control.currentIndex + delta + n) % n
        list.positionViewAtIndex(control.currentIndex, ListView.Contain)
    }
    function activate(row: int, editor: bool): void {
        if (row < 0 || row >= control.resultCount)
            return
        const path = files.pathAt(row)
        control.close()
        if (editor)
            control.editorChosen(path)
        else
            control.fileChosen(path)
    }

    CodeFileIndex {
        id: files
        objectName: "codeFilePickerIndex"
        folder: control.folder
        active: control.visible
        live: control.visible
        kinds: "files"
        limit: 60
        query: field.text
        recent: control.recent
        markColor: Theme.link
        onRowsChanged: control.currentIndex = 0
    }

    parent: T.Overlay.overlay
    modal: true
    dim: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    width: control.fullScreen ? control.overlayWidth : Math.min(680, control.overlayWidth - 2 * Theme.space6)
    height: control.fullScreen ? control.overlayHeight - KeyboardInsets.overlap
                               : Math.min(implicitHeight, control.overlayHeight - KeyboardInsets.overlap - control.topEdge - Theme.space2)
    x: Math.round((control.overlayWidth - control.width) / 2)
    y: control.topEdge
    padding: 0
    implicitHeight: searchRow.implicitHeight + 1 + (control.resultCount > 0 ? Math.min(list.contentHeight + 12, control.listRoom) : 150)
                    + (hints.visible ? hints.implicitHeight : 0)

    onAboutToShow: {
        field.text = ""
        control.currentIndex = 0
    }
    onOpened: field.forceActiveFocus(Qt.PopupFocusReason)

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            id: searchRow
            Layout.fillWidth: true
            Layout.leftMargin: control.fullScreen ? 4 : Theme.space3
            Layout.rightMargin: Theme.space2
            spacing: 4
            BIconButton {
                visible: control.fullScreen
                iconName: "arrow-left"
                label: qsTr("Close")
                onClicked: control.close()
            }
            BIcon {
                visible: !control.fullScreen
                name: "search"
                size: Theme.iconSizeLg
                color: Theme.fgFaint
            }
            BTextField {
                id: field
                objectName: "codeFilePickerField"
                Layout.fillWidth: true
                implicitHeight: Theme.touchUi ? 56 : 52
                placeholderText: qsTr("Go to file…")
                font.pixelSize: Theme.textMd
                clearable: true
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                background: Item {}
                Keys.onUpPressed: control.move(-1)
                Keys.onDownPressed: control.move(1)
                Keys.onPressed: (event) => {
                    const key = event.key
                    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                    const page = Math.max(1, Math.floor(list.height / control.rowHeight))
                    if ((key === Qt.Key_Return || key === Qt.Key_Enter) && ctrl) {
                        control.activate(control.currentIndex, true)
                    } else if (key === Qt.Key_PageDown) {
                        control.move(page)
                    } else if (key === Qt.Key_PageUp) {
                        control.move(-page)
                    } else if (ctrl && (key === Qt.Key_N || key === Qt.Key_J)) {
                        control.move(1)
                    } else if (ctrl && (key === Qt.Key_P || key === Qt.Key_K)) {
                        control.move(-1)
                    } else {
                        return
                    }
                    event.accepted = true
                }
                onAccepted: control.activate(control.currentIndex, false)
            }
            BSpinner {
                visible: files.building
                size: Theme.iconSize
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        ListView {
            id: list
            objectName: "codeFilePickerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(list.contentHeight + 12, control.listRoom)
            visible: control.resultCount > 0
            clip: true
            topMargin: 6
            bottomMargin: 6
            leftMargin: 6
            rightMargin: 6
            model: files.rows
            currentIndex: control.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            T.ScrollBar.vertical: BScrollBar {}
            delegate: BListItem {
                id: row
                required property int index
                required property string path
                required property string nameMarkup
                required property string folderMarkup
                required property string folder
                required property string name
                required iconName
                objectName: "codeFilePickerRow"
                width: list.width - 12
                selected: row.index === control.currentIndex
                subtitleInline: !control.fullScreen
                focusPolicy: Qt.NoFocus
                text: row.name
                titleMarkup: row.nameMarkup
                subtitle: row.folder
                subtitleMarkup: row.folderMarkup
                Accessible.name: row.path
                onHoveredChanged: if (hovered) control.currentIndex = row.index
                onClicked: control.activate(row.index, false)
            }
        }
        BEmptyState {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: control.fullScreen
            Layout.topMargin: Theme.space5
            Layout.bottomMargin: Theme.space5
            visible: control.resultCount === 0
            compact: true
            iconName: files.building && !files.ready ? "hourglass" : "search"
            title: files.building && !files.ready ? qsTr("Reading the project's files…")
                 : files.ready && files.fileCount === 0 ? qsTr("No files here") : qsTr("No files found")
            description: files.ready && files.truncated ? qsTr("The project has more files than Brulion lists (200,000).") : ""
        }
        Item {
            Layout.fillHeight: true
            visible: control.fullScreen && control.resultCount > 0
        }
        Item {
            id: hints
            Layout.fillWidth: true
            visible: !Theme.touchUi
            implicitHeight: 34
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.space4
                spacing: Theme.space4
                Repeater {
                    model: control.hintRows
                    delegate: Row {
                        id: hint
                        required property string keys
                        required property string text
                        spacing: 5
                        BKbd {
                            anchors.verticalCenter: parent.verticalCenter
                            sequence: hint.keys
                            small: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: hint.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            color: Theme.fgMuted
                        }
                    }
                }
            }
            BText {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: Theme.space4
                visible: files.ready
                text: qsTr("%n file(s)", "", files.fileCount)
                variant: "faint"
                font.pixelSize: Theme.textXs
            }
        }
    }

    background: Item {
        BShadow {
            target: panelBackground
            level: 3
        }
        Rectangle {
            id: panelBackground
            anchors.fill: parent
            radius: control.fullScreen ? 0 : Theme.radiusLg
            color: Theme.bgElevated
            border.width: control.fullScreen ? 0 : 1
            border.color: Theme.dark ? Theme.borderStrong : Theme.border
        }
    }
    T.Overlay.modal: Rectangle {
        color: control.fullScreen ? Theme.scrim : Theme.scrimModal
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant }
    }
}
