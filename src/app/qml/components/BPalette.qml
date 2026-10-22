pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BPalette — searchable popup list, the base of BCommandPalette and BQuickSwitcher.
//
//   BPalette {
//       items: [{ title: "Dziennik", subtitle: "", icon: "book-open", shortcut: "Ctrl+J" }, …]
//       placeholderText: qsTr("Szukaj…")
//       onItemActivated: (item) => …
//   }
//
// items: array of { title, subtitle?, icon?, shortcut?, … } (extra fields are passed back).
// Filtering is fuzzy and diacritic-insensitive (TextHelper.fuzzyRank: "zolw" finds "Żółw";
// subtitles match at half weight); matched characters are highlighted. `extraResults` are
// appended after the matches (e.g. "Create page …"). Keyboard: Up/Down (wrapping),
// PageUp/PageDown, Home/End with Ctrl, Enter activates, Escape closes. Wide windows: a
// floating panel in the upper third; compact: full screen with a back button.
T.Popup {
    id: control

    property var items: []
    property var extraResults: []
    property string placeholderText: qsTr("Search…")
    property string emptyTitle: qsTr("No results")
    property string emptyDescription
    property string defaultIcon
    property int maxResults: 60
    property bool closeOnActivate: true
    property alias query: field.text
    property int currentIndex: 0

    signal itemActivated(var item)

    // Titles/subtitles are extracted once per `items` change, not on every keystroke (a vault
    // can have thousands of pages).
    readonly property var _titles: (items || []).map(i => i.title || "")
    readonly property var _subtitles: (items || []).map(i => i.subtitle || "")
    readonly property var results: {
        const list = items || []
        const ranked = TextHelper.fuzzyRank(field.text, _titles, maxResults, _subtitles)
        const mark = Theme.link.toString()
        return ranked.map(r => ({
            item: list[r.index],
            markup: r.positions.length > 0 ? TextHelper.markMatches(list[r.index].title || "", r.positions, mark) : ""
        }))
    }
    readonly property var allResults: results.concat((extraResults || []).map(i => ({ item: i, markup: "" })))

    function activate(index) {
        const entry = allResults[index]
        if (!entry)
            return
        if (closeOnActivate)
            close()
        itemActivated(entry.item)
    }
    function move(delta) {
        const n = allResults.length
        if (n === 0)
            return
        currentIndex = (currentIndex + delta + n) % n
        list.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    onAllResultsChanged: currentIndex = 0

    readonly property real overlayWidth: parent ? parent.width : Theme.windowWidth
    readonly property real overlayHeight: parent ? parent.height : Theme.windowHeight
    readonly property bool fullScreen: Theme.compact

    parent: T.Overlay.overlay
    modal: true
    dim: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    width: fullScreen ? overlayWidth : Math.min(640, overlayWidth - 2 * Theme.space6)
    // Its results end above the on-screen keyboard.
    height: fullScreen ? overlayHeight - KeyboardInsets.overlap
                       : Math.max(0, Math.min(implicitHeight, overlayHeight * 0.72,
                                              overlayHeight - KeyboardInsets.overlap - topEdge - Theme.space2))
    x: Math.round((overlayWidth - width) / 2)
    y: topEdge
    /// Its top edge (not `y`, which the popup keeps inside the window by its height).
    readonly property real topEdge: fullScreen ? 0 : Math.round(overlayHeight * 0.12)
    padding: 0

    implicitHeight: searchRow.implicitHeight + 1 + Math.min(list.contentHeight + 12, 420)
                    + (allResults.length === 0 ? 150 : 0) + (hints.visible ? hints.implicitHeight : 0)

    onAboutToShow: {
        field.text = ""
        currentIndex = 0
    }
    onOpened: field.forceActiveFocus(Qt.PopupFocusReason)

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            id: searchRow
            Layout.fillWidth: true
            Layout.leftMargin: control.fullScreen ? 4 : Theme.space3
            Layout.rightMargin: Theme.space2
            Layout.topMargin: control.fullScreen ? (control.parent && control.parent.SafeArea ? control.parent.SafeArea.margins.top : 0) : 0
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
                Layout.fillWidth: true
                implicitHeight: Theme.touchUi ? 56 : 52
                placeholderText: control.placeholderText
                font.pixelSize: Theme.textMd
                clearable: true
                inputMethodHints: Qt.ImhNoPredictiveText
                background: Item {}
                Accessible.role: Accessible.EditableText
                Keys.onUpPressed: control.move(-1)
                Keys.onDownPressed: control.move(1)
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_PageDown) {
                        control.move(Math.max(1, Math.floor(list.height / 44)))
                        event.accepted = true
                    } else if (event.key === Qt.Key_PageUp) {
                        control.move(-Math.max(1, Math.floor(list.height / 44)))
                        event.accepted = true
                    } else if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_N || event.key === Qt.Key_J)) {
                        control.move(1)
                        event.accepted = true
                    } else if ((event.modifiers & Qt.ControlModifier) && (event.key === Qt.Key_P || event.key === Qt.Key_K)) {
                        control.move(-1)
                        event.accepted = true
                    }
                }
                onAccepted: control.activate(control.currentIndex)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(contentHeight + 12, 420)
            visible: control.allResults.length > 0
            clip: true
            topMargin: 6
            bottomMargin: 6
            leftMargin: 6
            rightMargin: 6
            model: control.allResults
            currentIndex: control.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            T.ScrollBar.vertical: BScrollBar {}

            delegate: BListItem {
                id: row
                required property var modelData
                required property int index
                width: list.width - 12
                text: modelData.item.title || ""
                titleMarkup: modelData.markup
                subtitle: modelData.item.subtitle || ""
                iconName: modelData.item.icon || control.defaultIcon
                selected: index === control.currentIndex
                focusPolicy: Qt.NoFocus
                trailing: BKbd {
                    sequence: row.modelData.item.shortcut || ""
                    small: true
                }
                onHoveredChanged: if (hovered) control.currentIndex = index
                onClicked: control.activate(index)
            }
        }

        BEmptyState {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: control.fullScreen
            Layout.topMargin: Theme.space5
            Layout.bottomMargin: Theme.space5
            visible: control.allResults.length === 0
            compact: true
            iconName: "search"
            title: control.emptyTitle
            description: control.emptyDescription
        }

        Item {
            Layout.fillHeight: true
            visible: control.fullScreen && control.allResults.length > 0
        }

        Rectangle {
            id: hints
            Layout.fillWidth: true
            visible: !Theme.touchUi
            implicitHeight: 34
            color: Theme.bgSunken
            radius: Theme.radiusLg
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Rectangle { y: 1; width: parent.width; height: parent.radius; color: parent.color }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.space4
                spacing: Theme.space4
                Repeater {
                    model: [
                        { keys: "↑+↓", text: qsTr("navigate") },
                        { keys: "Enter", text: qsTr("open") },
                        { keys: "Esc", text: qsTr("close") }
                    ]
                    delegate: Row {
                        required property var modelData
                        spacing: 5
                        BKbd {
                            anchors.verticalCenter: parent.verticalCenter
                            sequence: parent.modelData.keys
                            small: true
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.modelData.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            color: Theme.fgMuted
                        }
                    }
                }
            }
        }
    }

    background: Item {
        BShadow {
            target: panel
            level: 3
        }
        Rectangle {
            id: panel
            anchors.fill: parent
            radius: control.fullScreen ? 0 : Theme.radiusLg
            color: Theme.bgElevated
            border.width: control.fullScreen ? 0 : 1
            border.color: Theme.border
        }
    }

    T.Overlay.modal: Rectangle {
        color: Theme.scrim
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
            NumberAnimation { property: "scale"; from: control.fullScreen ? 1 : 0.98; to: 1; duration: Theme.duration; easing.type: Theme.easingEnter }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant }
    }
}
