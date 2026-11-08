pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// FrontmatterView — page properties (YAML frontmatter or Logseq `key:: value` lines at the top)
// as the "Properties" panel (docs/UI.md "Page properties"). Folded: a header with the number of
// properties and a one-line preview of their values ("status: in progress · due: Wed, Oct 28"; on a
// page, without the tags and aliases its header shows as chips).
// Expanded (a device setting, BlockEditor.propertiesExpanded — the header's tap switches it, so
// a page that is edited or synced keeps it open): the properties as typed fields
// (PropertiesPanel), with "+" (add a property) and "⋯" ("Edit as text": the raw text in the
// block's inline editor, "Collapse") in the header. The panel's height is a binding (docs/UI.md
// "Scrolling performance"): a block created expanded has its final height at once.
Rectangle {
    id: root
    objectName: "frontmatterView"

    /// The delegate (an Item: BlockDelegate passes itself, and a file cannot store itself into
    /// a property of its own type — compiled, a module-qualified cast crashes).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate
    readonly property BlockEditor editor: block.editor
    readonly property bool expanded: editor.propertiesExpanded && !block.editing
    readonly property bool open: expanded || block.editing
    readonly property list<var> properties: root.block.rowData.properties
    readonly property var info: root.block.rowData.propertiesInfo
    /// `info` ({ source, editable, problem, canAdd }) with its keys readable.
    readonly property MapHolder infoRec: MapHolder { map: root.block.rowData.propertiesInfo }
    readonly property real firstLineCenter: header.height / 2
    readonly property real innerPad: Theme.space2
    /// The tags and aliases properties the page header shows as chips while the panel is folded
    /// ("source:key"; none where the editor has no such header — a journal day in the stream).
    readonly property list<string> headerListKeys: root.listChipKeys()
    function listChipKeys(): list<string> {
        if (!root.editor.headerShowsListChips || !root.editor.controller)
            return []
        return BlockViews.headerListKeys(root.editor.controller.pageProperties)
    }
    /// The folded header's preview of the values: without the tags and aliases the page header
    /// shows right above (never twice), dates as the open panel shows them ("Thu, Oct 29").
    readonly property string preview: {
        // (Each list read fresh: a call in the loop could change one held in a local.)
        let out = ""
        const count = root.fields("key").length
        for (let i = 0; i < count; ++i) {
            if (out.length > 160)
                break
            const type = root.fields("type")[i]
            const key = root.fields("key")[i]
            if ((type === "tags" || type === "aliases") && root.headerListKeys.indexOf(root.fields("source")[i] + ":" + key) >= 0)
                continue
            const empty = root.fields("empty")[i] === "true"
            let value = root.fields("value")[i]
            if (!empty && (type === "date" || type === "datetime")) {
                const shown = DatesHelper.formatShort(Js.replace(root.fields("raw")[i], "T", " "))
                if (shown !== "")
                    value = shown
            }
            out += (out !== "" ? " · " : "") + key + ": " + (empty ? "–" : value)
        }
        return out
    }
    /// String(p[field]) of each property.
    function fields(field: string): list<string> {
        return BlockViews.fieldTexts(root.properties, field)
    }

    // Folded (as it is created): the header's own height. Expanded: the header and the panel
    // (an inactive Loader keeps the size of the item it had: counted only while active).
    implicitHeight: header.height + (block.editing ? editorLoader.implicitHeight + Theme.space3
                                     : expanded && panelLoader.active ? panelLoader.implicitHeight + Theme.space2 : 0)
    height: implicitHeight
    radius: Theme.radius
    // Open: a sunken surface in light, a raised one in dark (lighter reads as raised there; the
    // sunken fill barely parted from the page), framed only while its text is edited (the focus
    // border).
    color: !root.open ? "transparent" : Theme.dark ? Theme.bgElevated : Theme.bgSunken
    border.width: root.block.editing ? 1 : 0
    border.color: Theme.alpha(Theme.focusRing, 0.5)

    function toggle(): void {
        if (block.editing) {
            editor.stopEditing(true)
            return
        }
        editor.setPropertiesExpanded(!editor.propertiesExpanded)
    }
    function openHeaderMenu(): void {
        if (!headerMenu)
            headerMenu = headerMenuComponent.createObject(root) as BMenu
        headerMenu.bottomMargin = editor.menuBottomMargin()
        headerMenu.popup(moreButton, moreButton.width - headerMenu.width, moreButton.height)
    }
    property BMenu headerMenu: null

    Item {
        id: header
        objectName: "propertiesHeader"
        width: parent.width
        height: Theme.touchUi ? 44 : 32

        TapHandler { onTapped: root.toggle() }
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Properties")
        Accessible.description: root.expanded ? qsTr("Collapse") : root.preview

        BIcon {
            id: chevron
            x: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            size: Theme.iconSizeSm
            color: Theme.fgMuted
            rotation: root.open ? 90 : 0
            Behavior on rotation { NumberAnimation { duration: Theme.durationFast } }
        }
        Text {
            id: title
            x: chevron.x + chevron.width + Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Properties")
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: Theme.weightMedium
            color: Theme.fgMuted
        }
        BBadge {
            id: badge
            x: title.x + title.width + Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            count: root.properties.length
        }
        Text {
            id: previewText
            objectName: "propertiesPreview"
            visible: !root.open && text !== ""
            x: badge.x + badge.width + Theme.space3
            width: Math.max(0, header.width - x - Theme.space2)
            anchors.verticalCenter: parent.verticalCenter
            text: root.preview
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            color: Theme.fgFaint
        }
        BIconButton {
            id: addButton
            objectName: "propertiesHeaderAdd"
            visible: root.expanded && root.infoRec.map.canAdd !== false && root.infoRec.map.editable !== false
            anchors.right: moreButton.left
            anchors.verticalCenter: parent.verticalCenter
            size: "small"
            iconName: "plus"
            label: qsTr("Add property")
            shortcut: ShortcutMap.sequences["page.addProperty"] || ""
            focusPolicy: Qt.NoFocus
            onClicked: if (panelLoader.item) (panelLoader.item as PropertiesPanel).openAdd()
        }
        BIconButton {
            id: moreButton
            objectName: "propertiesHeaderMore"
            visible: root.open
            anchors.right: parent.right
            anchors.rightMargin: Theme.space1
            anchors.verticalCenter: parent.verticalCenter
            size: "small"
            iconName: "ellipsis"
            label: qsTr("More")
            focusPolicy: Qt.NoFocus
            onClicked: root.openHeaderMenu()
        }
    }

    Loader {
        id: panelLoader
        x: Theme.space1
        y: header.height
        width: parent.width - Theme.space1 - Theme.space2
        active: root.expanded
        visible: active
        sourceComponent: PropertiesPanel {
            objectName: "propertiesPanel"
            editorItem: root.editor
            controller: root.block.controller
            blockRow: root.block.index
            properties: root.properties
            info: root.info
        }
    }
    Loader {
        id: editorLoader
        x: Theme.space8
        y: header.height
        width: parent.width - Theme.space8 - Theme.space3
        active: root.block.editing
        visible: active
        sourceComponent: InlineEditor {
            width: editorLoader.width
            block: root.block
            multiline: true
            highlightMode: "code"
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            lineHeightPx: Theme.lineHeightPx(Theme.textSm, 1.5)
        }
    }

    Component {
        id: headerMenuComponent
        BMenu {
            BMenuItem {
                objectName: "propertiesEditAsText"
                text: qsTr("Edit as text")
                iconName: "code"
                enabled: !root.block.editing
                onTriggered: root.editor.startEditing(root.block.index, -1)
            }
            BMenuItem {
                text: root.block.editing ? qsTr("Done") : qsTr("Collapse")
                iconName: "chevrons-down-up"
                onTriggered: root.toggle()
            }
        }
    }
}
