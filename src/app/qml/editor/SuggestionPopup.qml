pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// SuggestionPopup — the editor's autocomplete list (pages after `[[`, blocks after `((`, tags
// after `#`, slash commands after `/`, natural-language dates after ⏰/📅/@), placed under the
// caret (above it when there is no room). It never takes the keyboard: the inline editor keeps
// typing and BlockEditor forwards Up/Down/Enter/Tab/Esc (move, move, accept, accept, close).
// Mouse/touch: a click on an item accepts it.
T.Popup {
    id: popup

    property BlockEditor editor
    property SuggestionModel model
    property var trigger: ({})
    property int row: -1
    property InlineEditor target: null
    property int currentIndex: 0
    signal activated(int index)

    readonly property bool slash: trigger.mode === "slash"
    readonly property real rowHeight: Theme.touchUi ? 48 : (slash ? 36 : 40) // two lines (title + folder) need 40

    parent: editor
    focus: false
    modal: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
    padding: Theme.space1
    implicitWidth: Math.min(editor.width - 2 * Theme.space2, slash ? 300 : 360)
    implicitHeight: Math.min(list.contentHeight, rowHeight * (Theme.touchUi ? 5 : 8)) + topPadding + bottomPadding + (headerText.visible ? headerText.height : 0)

    function show() {
        if (!target)
            return
        place()
        if (!visible)
            currentIndex = 0
        currentIndex = Math.min(currentIndex, Math.max(0, model.count - 1))
        open()
    }
    /// Under the caret, above it when there is no room below; always inside the editor's visible
    /// part (above the mobile toolbar). Called again while open: the list scrolls under the caret
    /// (the toolbar appearing, the caret kept visible) and the list of items grows.
    function place() {
        if (!target || !editor)
            return
        const rect = target.cursorRectangle
        const caret = target.mapToItem(editor, rect.x, rect.y)
        const h = implicitHeight
        const bottomLimit = editor.toolbarTop
        const below = caret.y + rect.height + 4
        const above = caret.y - h - 4
        x = Math.max(Theme.space2, Math.min(caret.x - Theme.space3, editor.width - width - Theme.space2))
        const y0 = below + h <= bottomLimit || above < Theme.space1 ? below : above
        y = Math.max(Theme.space1, Math.min(y0, bottomLimit - h - Theme.space1))
    }
    onImplicitHeightChanged: if (visible) place()

    Connections {
        target: popup.visible ? popup.editor.listView : null
        function onContentYChanged() { popup.place() }
        function onHeightChanged() { popup.place() }
    }
    Connections {
        target: popup.visible ? popup.target : null
        function onCursorRectangleChanged() { popup.place() }
    }
    Connections {
        target: popup.visible ? popup.editor : null
        function onToolbarTopChanged() { popup.place() }
        function onHeightChanged() { popup.place() }
    }
    function move(delta) {
        if (model.count === 0)
            return
        currentIndex = (currentIndex + delta + model.count) % model.count
        list.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    Connections {
        target: popup.model
        function onModelReset() { popup.currentIndex = 0 }
    }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationInstant } }

    background: Item {
        BShadow { target: bg; level: 2 }
        Rectangle {
            id: bg
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Theme.bgElevated2
            border.width: 1
            border.color: Theme.border
        }
    }

    contentItem: Column {
        Text {
            id: headerText
            visible: popup.trigger.mode === "date" && popup.model.count > 0
            width: parent.width
            leftPadding: Theme.space3
            topPadding: Theme.space1
            bottomPadding: Theme.space1
            text: popup.trigger.token && popup.trigger.token.indexOf("⏰") === 0 ? qsTr("Reminder at")
                : popup.trigger.token === "@" ? qsTr("Date") : qsTr("Date")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            font.weight: Theme.weightMedium
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.4
            color: Theme.fgFaint
        }
        ListView {
            id: list
            objectName: "suggestionList"
            width: parent.width
            height: popup.availableHeight - (headerText.visible ? headerText.height : 0)
            clip: true
            // No rows built ahead of the viewport: a ListView makes those asynchronously, and a
            // keystroke that resets the model while one is still being made (a slow phone) has
            // Qt cancel it by its old row ("DelegateModel::cancel: index out range 6 5"); removing
            // rows instead leaves the view waiting for it. At most `limit` (12) light rows.
            cacheBuffer: 0
            model: popup.model
            currentIndex: popup.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Suggestions")

            delegate: Rectangle {
                id: item
                required property int index
                required property string title
                required property string subtitle
                required property string icon
                required property string hint
                required property string kind
                readonly property bool current: index === popup.currentIndex
                // Slash commands: the syntax line ("⏰ data godzina") under the name of the row in
                // focus (always on touch screens); the short syntax at the right otherwise.
                readonly property bool showSubtitle: subtitle !== "" && (!popup.slash || Theme.touchUi || current)
                width: list.width
                height: popup.rowHeight
                radius: Theme.radius
                color: current ? Theme.bgSelected : itemHover.hovered ? Theme.bgHover : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: subtitle
                Accessible.selected: current

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space2
                    anchors.rightMargin: Theme.space3
                    spacing: Theme.space2

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: popup.slash ? 26 : 20
                        height: width
                        radius: Theme.radiusSm
                        color: popup.slash ? Theme.bgSunken : "transparent"
                        border.width: popup.slash ? 1 : 0
                        border.color: Theme.borderSubtle
                        BIcon {
                            anchors.centerIn: parent
                            name: item.icon
                            size: Theme.iconSizeSm
                            color: item.kind === "create" ? Theme.link : item.kind === "tag" ? Theme.tagFg : Theme.fgMuted
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - (hintText.visible ? hintText.width + Theme.space2 : 0)
                        Text {
                            width: parent.width
                            text: item.kind === "create" ? qsTr("Create page “%1”").arg(item.title) : item.title
                            textFormat: Text.PlainText
                            font.family: item.kind === "date" || item.kind === "reminder" ? Theme.fontMono : Theme.fontUi
                            font.pixelSize: Theme.textBase
                            font.weight: item.current ? Theme.weightMedium : Theme.weightNormal
                            color: item.kind === "create" ? Theme.link : Theme.fg
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: item.showSubtitle
                            text: item.subtitle
                            textFormat: Text.PlainText
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            color: Theme.fgFaint
                            elide: Text.ElideRight
                        }
                    }
                }
                Text {
                    id: hintText
                    visible: item.hint !== "" && popup.slash && !item.showSubtitle
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    text: item.hint
                    textFormat: Text.PlainText
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.textXs
                    color: Theme.fgFaint
                }
                HoverHandler { id: itemHover; enabled: !Theme.touchUi }
                TapHandler { onTapped: popup.activated(item.index) }
            }
        }
    }
}
