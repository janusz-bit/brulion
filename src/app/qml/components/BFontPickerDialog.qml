pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BFontPickerDialog — choose an installed font family (Ustawienia → Wygląd: the interface font;
// Edytor: the font of notes, of code). A search field over DeviceInfo.fontFamilies() (no emoji,
// symbol or private families; TextHelper.fuzzyRank: "zrodlo" finds "Źródło Serif"); every row
// is drawn in its own font; the first row is the default (`defaultLabel`, "" as the family). A
// tap chooses at once (`chosen(family)`). A bottom sheet on phones. Not offered in the browser
// (Qt there knows only its bundled fonts).
//
//   BFontPickerDialog {
//       id: picker
//       title: qsTr("Interface font"); defaultLabel: qsTr("System font")
//       onChosen: (family) => AppSettings.uiFontFamily = family
//   }
//   picker.openFor(AppSettings.uiFontFamily)
BDialog {
    id: dialog

    /// Monospaced families only (code).
    property bool fixedOnly: false
    /// The family chosen now ("" = the default), highlighted.
    property string current
    /// The first row: the default font ("" as the family).
    property string defaultLabel: qsTr("Default")
    /// Font family of the default row (drawn in it).
    property string defaultFamily: Theme.fontUi
    property string query

    /// A row was tapped: its family, "" for the default.
    signal chosen(string family)

    function openFor(family) {
        current = family || ""
        open()
    }
    function choose(row) {
        if (!row)
            return
        dialog.chosen(row.family)
        dialog.close()
    }

    readonly property var families: visible ? DeviceInfo.fontFamilies(fixedOnly) : []
    readonly property var rows: {
        const q = query.trim()
        const out = []
        if (q === "" || TextHelper.fuzzyScore(q, defaultLabel) > 0)
            out.push({ family: "", title: defaultLabel })
        for (const r of TextHelper.fuzzyRank(q, families))
            out.push({ family: families[r.index], title: families[r.index] })
        return out
    }

    objectName: "fontPickerDialog"
    iconName: "type"
    showFooter: false
    initialFocusItem: search
    preferredWidth: Theme.dialogWidthSm

    onAboutToShow: {
        search.text = ""
        query = ""
        list.currentIndex = -1
        Qt.callLater(() => {
            const i = dialog.rows.findIndex(r => r.family === dialog.current)
            if (i >= 0)
                list.positionViewAtIndex(i, ListView.Center)
        })
    }

    BSearchField {
        id: search
        objectName: "fontPickerSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search fonts…")
        onSearch: (text) => {
            dialog.query = text
            list.currentIndex = -1
        }
        Keys.onDownPressed: {
            list.currentIndex = 0
            list.forceActiveFocus()
        }
        onAccepted: if (dialog.rows.length > 0) dialog.choose(dialog.rows[0])
    }

    ListView {
        id: list
        objectName: "fontPickerList"
        TouchFlick.enabled: true
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, Theme.listItemHeight * 3), 380)
        clip: true
        model: dialog.rows
        currentIndex: -1
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        Keys.onReturnPressed: dialog.choose(dialog.rows[currentIndex])
        Keys.onEnterPressed: dialog.choose(dialog.rows[currentIndex])
        delegate: BListItem {
            required property var modelData
            objectName: "fontPickerRow"
            width: ListView.view.width
            text: modelData.title
            font.family: modelData.family === "" ? dialog.defaultFamily : modelData.family
            font.pixelSize: Theme.textMd
            iconName: modelData.family === dialog.current ? "check" : ""
            selected: modelData.family === dialog.current
            onClicked: dialog.choose(modelData)
        }
        BText {
            anchors.centerIn: parent
            visible: list.count === 0
            text: qsTr("No fonts found.")
            variant: "muted"
        }
    }
}
