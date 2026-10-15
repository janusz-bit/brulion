pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentNoteModeChoice — the note mode of a new page (docs/UI.md "Note modes"): two options,
// "Dokument (jak Obsidian)" and "Konspekt (jak Logseq)", one under the other with a line saying
// what each is. One tab stop like a radio group: arrow keys and Space choose; a tap or click
// chooses an option.
//
//   ContentNoteModeChoice { mode: dialog.mode; onActivated: (m) => dialog.mode = m }
//
// `preferred()` gives the mode to preselect: the choice made last time on this device, unless the
// notebook's default for new pages changed since then (Workspace.config.pageMode); remember(mode)
// stores a choice.
FocusScope {
    id: root

    /// "document" | "outline".
    property string mode: "document"
    signal activated(string mode)

    readonly property var options: [
        { value: "document", icon: "file-text", title: qsTr("Document"), hint: qsTr("like Obsidian"),
          description: qsTr("Paragraphs, headings and lists, read like a text.") },
        { value: "outline", icon: "list", title: qsTr("Outline"), hint: qsTr("like Logseq"),
          description: qsTr("Every point is a block: indent, fold, zoom in.") }
    ]

    function choose(value) {
        if (value !== mode) {
            mode = value
            activated(value)
        }
    }
    /// The mode to preselect (see the file comment).
    function preferred() {
        const fallback = Workspace.config.pageMode === "outline" ? "outline" : "document"
        const last = AppSettings.value("ui/newPage/mode", "")
        const underDefault = AppSettings.value("ui/newPage/modeDefault", "")
        return (last === "document" || last === "outline") && underDefault === fallback ? last : fallback
    }
    function remember(value) {
        AppSettings.setValue("ui/newPage/mode", value)
        AppSettings.setValue("ui/newPage/modeDefault", Workspace.config.pageMode === "outline" ? "outline" : "document")
    }

    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Note mode")

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Left: case Qt.Key_Up:
            root.choose("document")
            event.accepted = true
            break
        case Qt.Key_Right: case Qt.Key_Down:
            root.choose("outline")
            event.accepted = true
            break
        case Qt.Key_Space:
            root.choose(root.mode === "document" ? "outline" : "document")
            event.accepted = true
            break
        }
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: Theme.space2

        BText {
            text: qsTr("Note mode")
            variant: "label"
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space2

            Repeater {
                model: root.options
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool selected: root.mode === modelData.value
                    Layout.fillWidth: true
                    implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : 0, cardRow.implicitHeight + 2 * Theme.space2 + 2)
                    radius: Theme.radius
                    color: selected ? Theme.bgSelected : cardHover.hovered ? Theme.bgHover : "transparent"
                    border.width: selected ? 1.5 : 1
                    border.color: selected ? Theme.alpha(Theme.focusRing, 0.7) : Theme.border
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.title + " (" + modelData.hint + ")"
                    Accessible.description: modelData.description
                    Accessible.checked: selected
                    Accessible.onPressAction: root.choose(modelData.value)

                    RowLayout {
                        id: cardRow
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space3
                        anchors.rightMargin: Theme.space3
                        spacing: Theme.space3
                        BIcon {
                            Layout.alignment: Qt.AlignVCenter
                            name: card.modelData.icon
                            size: Theme.iconSize
                            color: card.selected ? Theme.link : Theme.fgMuted
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.StyledText
                                text: TextHelper.escapeHtml(card.modelData.title)
                                      + " <font color=\"" + Theme.fgMuted + "\">(" + TextHelper.escapeHtml(card.modelData.hint) + ")</font>"
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textBase
                                font.weight: Theme.weightMedium
                                color: Theme.fg
                                wrapMode: Text.Wrap
                            }
                            BText {
                                Layout.fillWidth: true
                                text: card.modelData.description
                                variant: "muted"
                                font.pixelSize: Theme.textSm
                                wrapMode: Text.Wrap
                            }
                        }
                        BIcon {
                            Layout.alignment: Qt.AlignVCenter
                            name: "check"
                            size: Theme.iconSizeSm
                            color: Theme.link
                            opacity: card.selected ? 1 : 0
                        }
                    }
                    HoverHandler { id: cardHover; enabled: !Theme.touchUi; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            root.choose(card.modelData.value)
                            root.forceActiveFocus()
                        }
                    }
                    BFocusRing {
                        visible: root.activeFocus && card.selected
                        targetRadius: Theme.radius
                    }
                }
            }
        }
    }
}
