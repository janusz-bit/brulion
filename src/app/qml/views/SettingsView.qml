pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SettingsView — route "settings" ({section?}). Sections: Ogólne (language), Wygląd (theme,
// accent colour with presets and BAccentDialog, light and dark backgrounds, text size, interface
// font, density, reduce motion; Układ: sidebar entries and sections, the bottom navigation's
// four buttons, the quick note button; back to the default look — AppSettings, Theme inputs),
// Edytor (font — serif, sans or any installed family —, code font, size, line spacing, width,
// source mode; font pickers: BFontPickerDialog, not in the browser), Notatnik (the vault's shared settings in
// .brulion/config.json: the note modes of new pages and of the journal, journal folder and file
// names, attachments, templates, default reminder time, first day of the week), Przypomnienia (on/off, permissions on Android and in the
// browser, the sounds of reminders and alarms — qml/notify/ReminderSoundSetting —, snooze, alarm
// length, test notification), Synchronizacja (qml/io/SyncSettings: status,
// the sync center, the Nextcloud account), Aplikacja (close to tray, the event log and its
// detailed mode, version, licence, source code; on the desktop "Dla programistów": the switch
// that lets AI agents drive the running app, AppController.remoteControl).
//
// Wide windows: section list on the left, one section on the right. Narrow: all sections in
// one scrolling column.
Item {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Settings")
    property string section: route.section || "general"
    onSectionChanged: scrollToSection()

    // Narrow windows show every section in one list: the route's section is brought to the top,
    // also while the sections above it are still being laid out (until the user scrolls).
    property bool _scrollPending: false
    function scrollToSection() {
        _scrollPending = Theme.narrow
        if (_scrollPending)
            scrollSettle.restart()
        for (let i = 0; i < narrowSections.count; ++i) {
            const item = narrowSections.itemAt(i)
            if (item && root.sections[i].id === root.section)
                scrollTo(i === 0 ? 0 : item.y)
        }
    }
    function scrollTo(y) {
        if (_scrollPending)
            flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
    }
    Timer {
        id: scrollSettle
        interval: 1000 // the sections are laid out by then; later changes are the user's
        onTriggered: root._scrollPending = false
    }

    readonly property var sections: [
        { id: "general", title: qsTr("General"), icon: "settings" },
        { id: "appearance", title: qsTr("Appearance"), icon: "palette" },
        { id: "editor", title: qsTr("Editor"), icon: "pencil" },
        { id: "vault", title: qsTr("Notebook"), icon: "notebook-pen" },
        { id: "reminders", title: qsTr("Reminders"), icon: "bell" },
        { id: "sync", title: qsTr("Sync"), icon: "folder-sync" },
        { id: "app", title: qsTr("Application"), icon: "info" }
    ]

    function sectionComponent(id) {
        switch (id) {
        case "appearance": return appearanceSection
        case "editor": return editorSection
        case "vault": return vaultSection
        case "reminders": return remindersSection
        case "sync": return syncSection
        case "app": return appSection
        default: return generalSection
        }
    }

    /// Font families can be chosen (not in the browser: Qt there has only its bundled fonts).
    readonly property bool fontChoice: DeviceInfo.platform !== "wasm"
    /// Accent presets with their names.
    readonly property var accents: [
        { value: "red", text: qsTr("Red") },
        { value: "blue", text: qsTr("Blue") },
        { value: "green", text: qsTr("Green") },
        { value: "teal", text: qsTr("Teal") },
        { value: "violet", text: qsTr("Violet") },
        { value: "graphite", text: qsTr("Graphite") }
    ]
    /// The sidebar's navigation entries and sections (AppSettings.sidebarItems()).
    readonly property var sidebarItems: [
        { id: "journals", title: qsTr("Journals") },
        { id: "calendar", title: qsTr("Calendar") },
        { id: "tasks", title: qsTr("Tasks") },
        { id: "allPages", title: qsTr("All pages") },
        { id: "tags", title: qsTr("Tags") },
        { id: "graph", title: qsTr("Graph") },
        { id: "search", title: qsTr("Search") },
        { id: "favorites", title: qsTr("Favorites section") },
        { id: "recent", title: qsTr("Recent section") }
    ]
    /// Views the bottom navigation offers (AppSettings.bottomNavViews()).
    readonly property var bottomNavViews: [
        { value: "journals", text: qsTr("Journal"), icon: "book-open" },
        { value: "calendar", text: qsTr("Calendar"), icon: "calendar" },
        { value: "search", text: qsTr("Search"), icon: "search" },
        { value: "tasks", text: qsTr("Tasks"), icon: "list-todo" },
        { value: "allPages", text: qsTr("All pages"), icon: "files" },
        { value: "tags", text: qsTr("Tags"), icon: "tags" },
        { value: "graph", text: qsTr("Graph"), icon: "waypoints" }
    ]
    function viewInfo(value) {
        return bottomNavViews.find(v => v.value === value) || { value: value, text: value, icon: "" }
    }
    /// The bottom navigation slot whose menu is open.
    property int navSlot: 0

    BFontPickerDialog {
        id: fontPicker
        /// "ui" | "editor" | "mono"
        property string target
        function openTarget(which) {
            target = which
            fixedOnly = which === "mono"
            title = which === "ui" ? qsTr("Interface font") : which === "mono" ? qsTr("Code font") : qsTr("Font of notes")
            defaultLabel = which === "ui" ? qsTr("System font") : which === "mono" ? qsTr("System monospaced font")
                                                                                    : qsTr("Serif (default)")
            defaultFamily = which === "ui" ? DeviceInfo.systemFontFamily : which === "mono" ? DeviceInfo.fixedFontFamily
                                                                                            : Theme.fontSerif
            openFor(which === "ui" ? AppSettings.uiFontFamily : which === "mono" ? AppSettings.editorMonoFamily
                                                                                : AppSettings.editorFontFamily)
        }
        onChosen: (family) => {
            if (target === "ui")
                AppSettings.uiFontFamily = family
            else if (target === "mono")
                AppSettings.editorMonoFamily = family
            else if (family === "") {
                AppSettings.editorFontFamily = "" // the default row: serif
                AppSettings.editorFont = "serif"
            } else {
                AppSettings.editorFontFamily = family
            }
        }
    }
    BAccentDialog {
        id: accentDialog
        onChosen: (color) => {
            AppSettings.customAccent = color
            AppSettings.accentColor = "custom"
        }
    }
    BMenu {
        id: navSlotMenu
        objectName: "bottomNavSlotMenu"
    }
    Instantiator {
        model: root.bottomNavViews
        delegate: BMenuItem {
            required property var modelData
            objectName: "bottomNavChoice_" + modelData.value
            text: modelData.text
            iconName: modelData.icon
            checkable: true
            checked: AppSettings.bottomNavTabs[root.navSlot] === modelData.value
            onTriggered: {
                AppSettings.setBottomNavTab(root.navSlot, modelData.value)
                // The tap toggled the item itself; the view already in the slot changes nothing
                // in AppSettings, so the binding would not be evaluated again.
                checked = Qt.binding(() => AppSettings.bottomNavTabs[root.navSlot] === modelData.value)
            }
        }
        onObjectAdded: (index, object) => navSlotMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => navSlotMenu.removeItem(object)
    }

    // ---- Building blocks --------------------------------------------------------------------

    /// A titled group of setting rows: a card, its rows divided by hairlines.
    component SettingsGroup: ColumnLayout {
        id: group
        property string heading
        property string note
        default property alias rows: cardColumn.data
        Layout.fillWidth: true
        spacing: Theme.space2

        BText {
            Layout.fillWidth: true
            visible: group.heading !== ""
            text: group.heading
            variant: "heading"
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
        }
        BText {
            Layout.fillWidth: true
            visible: group.note !== ""
            text: group.note
            variant: "muted"
            wrapMode: Text.Wrap
        }
        BCard {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            padding: Theme.compact ? Theme.space4 : Theme.space5
            // Two children: the content size is the rows' (a pane sizes itself by a lone child only).
            contentWidth: cardColumn.implicitWidth
            contentHeight: cardColumn.implicitHeight
            ColumnLayout {
                id: cardColumn
                anchors.fill: parent
                // Room for the hairline between the rows: 16 / 12 px on each side of it.
                spacing: Theme.compact ? Theme.space6 : Theme.space8
            }
            ShellSettingDividers { column: cardColumn }
        }
    }

    /// A text field that saves one key of the vault config when editing finishes. Its text stays
    /// bound to the config: a change made on another device (sync) shows.
    component ConfigField: BTextField {
        id: field
        required property string key
        property string invalidText: qsTr("This value cannot be used.")
        readonly property string configText: Workspace.config[key] !== undefined ? String(Workspace.config[key]) : ""
        Layout.preferredWidth: Theme.compact ? -1 : 260
        Layout.fillWidth: Theme.compact
        text: configText
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        function save() {
            if (text === configText)
                return
            const change = {}
            change[key] = text.trim()
            errorText = Workspace.setConfig(change) ? "" : invalidText
        }
        /// Save a value picked elsewhere (a clock dialog) at once. The text is not assigned —
        /// that would end its binding to the config, and a value changed later elsewhere would
        /// no longer show (and be written back on the next edit): the binding shows the value.
        function applyValue(value) {
            const change = {}
            change[key] = value
            errorText = Workspace.setConfig(change) ? "" : invalidText
            // Typed text the pick replaces, when the config already had that value.
            if (text !== configText)
                text = Qt.binding(() => field.configText)
        }
        onEditingFinished: save()
    }

    /// Granted / not granted, with a button to ask.
    component PermissionRow: ShellSettingRow {
        id: permission
        required property string name
        readonly property bool granted: AppController.permissions[name] === true
        BBadge {
            Layout.alignment: Qt.AlignVCenter
            text: permission.granted ? qsTr("Allowed") : qsTr("Not allowed")
            variant: permission.granted ? "success" : "warning"
        }
        BButton {
            visible: !permission.granted
            size: "small"
            text: qsTr("Allow")
            onClicked: AppController.requestPermission(permission.name)
        }
    }

    // ---- Layout -----------------------------------------------------------------------------

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Section list (wide windows).
        Rectangle {
            visible: !Theme.narrow
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            color: Theme.bg

            Column {
                anchors.fill: parent
                anchors.margins: Theme.space3
                anchors.topMargin: Theme.space5
                spacing: 2

                Repeater {
                    model: root.sections
                    delegate: BListItem {
                        required property var modelData
                        width: parent.width
                        text: modelData.title
                        iconName: modelData.icon
                        selected: root.section === modelData.id
                        selectionBar: true
                        onClicked: AppController.replaceRoute({ view: "settings", section: modelData.id })
                    }
                }
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.borderSubtle
            }
        }

        Flickable {
            id: flick
            TouchFlick.enabled: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            // Narrow: room below the last row for the floating capture button (56 + margin).
            contentHeight: content.implicitHeight + 2 * margin + (Theme.narrow ? 56 + Theme.space4 : 0)
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: BScrollBar {}

            readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8
            onMovementStarted: root._scrollPending = false
            onContentHeightChanged: if (root._scrollPending) root.scrollToSection()

            ColumnLayout {
                id: content
                x: flick.margin
                y: flick.margin
                width: Math.min(flick.width - 2 * flick.margin, 760)
                spacing: Theme.space8

                // Wide: the chosen section.
                Loader {
                    Layout.fillWidth: true
                    visible: !Theme.narrow
                    active: !Theme.narrow
                    sourceComponent: root.sectionComponent(root.section)
                    onLoaded: flick.contentY = 0
                }
                // Narrow: everything, scrolled to the route's section.
                Repeater {
                    id: narrowSections
                    model: Theme.narrow ? root.sections : []
                    delegate: Loader {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        sourceComponent: root.sectionComponent(modelData.id)
                        onLoaded: if (modelData.id === root.section) root.scrollToSection()
                        onYChanged: if (modelData.id === root.section) root.scrollTo(index === 0 ? 0 : y)
                    }
                }
            }
        }
    }

    // ---- Sections -----------------------------------------------------------------------------

    Component {
        id: generalSection
        SettingsGroup {
            heading: qsTr("General")

            ShellSettingRow {
                title: qsTr("Language")
                description: qsTr("The language of menus and messages.")
                BSegmented {
                    accessibleName: qsTr("Language")
                    model: [{ value: "pl", text: "Polski" }, { value: "en", text: "English" }]
                    currentValue: AppSettings.language
                    onActivated: (value) => AppSettings.language = value
                }
            }
        }
    }

    Component {
        id: appearanceSection
        ColumnLayout {
            spacing: Theme.space8

            SettingsGroup {
                objectName: "settingsAppearance"
                heading: qsTr("Appearance")

                ShellSettingRow {
                    title: qsTr("Theme")
                    description: qsTr("Light like paper, dark for the evening, or following the system.")
                    BSegmented {
                        accessibleName: qsTr("Theme")
                        model: [
                            { value: "system", text: qsTr("System"), icon: "monitor" },
                            { value: "light", text: qsTr("Light"), icon: "sun" },
                            { value: "dark", text: qsTr("Dark"), icon: "moon" }
                        ]
                        currentValue: AppSettings.themeMode
                        onActivated: (value) => AppSettings.themeMode = value
                    }
                }
                ShellSettingRow {
                    title: qsTr("Accent colour")
                    description: qsTr("Marks today, the current tab and highlights. Warnings and deleting stay red.")
                    Flow {
                        objectName: "accentSwatches"
                        Layout.maximumWidth: 7 * (Theme.touchUi ? Theme.touchTarget : 34)
                        spacing: 0
                        Repeater {
                            model: root.accents
                            delegate: BColorSwatch {
                                required property var modelData
                                objectName: "accentSwatch_" + modelData.value
                                color: ThemePalette.accentSwatch(modelData.value, "", Theme.dark,
                                                                 Theme.dark ? Theme.darkSurface : Theme.lightSurface)
                                label: modelData.text
                                // Theme: the setting, or a screenshot's --accent.
                                selected: Theme.accentName === modelData.value
                                onClicked: AppSettings.accentColor = modelData.value
                            }
                        }
                        BColorSwatch {
                            objectName: "accentSwatch_custom"
                            readonly property string custom: Theme.accentName === "custom" ? Theme.customAccent
                                                                                           : AppSettings.customAccent
                            color: custom !== "" ? ThemePalette.accentSwatch("custom", custom, Theme.dark,
                                                                             Theme.dark ? Theme.darkSurface : Theme.lightSurface)
                                                 : Theme.bgSunken
                            iconName: custom !== "" ? "" : "plus"
                            label: qsTr("Custom…")
                            selected: Theme.accentName === "custom"
                            onClicked: accentDialog.openWith(AppSettings.customAccent)
                        }
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.themeMode !== "dark"
                    title: qsTr("Light background")
                    BSegmented {
                        objectName: "lightSurfaceSetting"
                        accessibleName: qsTr("Light background")
                        model: [
                            { value: "paper", text: qsTr("Paper") },
                            { value: "white", text: qsTr("White", "background") }
                        ]
                        currentValue: Theme.lightSurface
                        onActivated: (value) => AppSettings.lightSurface = value
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.themeMode !== "light"
                    title: qsTr("Dark background")
                    description: qsTr("Black saves power on OLED screens.")
                    BSegmented {
                        objectName: "darkSurfaceSetting"
                        accessibleName: qsTr("Dark background")
                        model: [
                            { value: "slate", text: qsTr("Blue-grey") },
                            { value: "black", text: qsTr("Black", "background") }
                        ]
                        currentValue: Theme.darkSurface
                        onActivated: (value) => AppSettings.darkSurface = value
                    }
                }
                ShellSettingRow {
                    title: qsTr("Text size")
                    description: qsTr("Size of all text in the app.")
                    BSegmented {
                        accessibleName: qsTr("Text size")
                        model: [
                            { value: 0.9, text: qsTr("Small") },
                            { value: 1.0, text: qsTr("Normal") },
                            { value: 1.15, text: qsTr("Large") },
                            { value: 1.3, text: qsTr("Larger") }
                        ]
                        currentValue: [0.9, 1.0, 1.15, 1.3].reduce((best, v) =>
                            Math.abs(v - AppSettings.fontScale) < Math.abs(best - AppSettings.fontScale) ? v : best, 1.0)
                        onActivated: (value) => AppSettings.fontScale = value
                    }
                }
                ShellSettingRow {
                    visible: root.fontChoice
                    title: qsTr("Interface font")
                    description: AppSettings.uiFontFamily === "" ? qsTr("The system font.")
                                 : Theme.fontUi === AppSettings.uiFontFamily ? AppSettings.uiFontFamily
                                 : qsTr("%1 is not installed on this device: the system font is used.").arg(AppSettings.uiFontFamily)
                    BButton {
                        objectName: "uiFontButton"
                        size: "small"
                        text: qsTr("Choose…")
                        iconName: "type"
                        onClicked: fontPicker.openTarget("ui")
                    }
                }
                ShellSettingRow {
                    // Touch UIs keep their 44 px targets.
                    visible: !Theme.touchUi
                    title: qsTr("Density")
                    description: qsTr("Compact fits more on the screen: smaller buttons and rows.")
                    BSegmented {
                        objectName: "densitySetting"
                        accessibleName: qsTr("Density")
                        model: [
                            { value: "comfortable", text: qsTr("Comfortable") },
                            { value: "compact", text: qsTr("Compact", "density") }
                        ]
                        currentValue: Theme.density
                        onActivated: (value) => AppSettings.density = value
                    }
                }
                BSwitch {
                    Layout.fillWidth: true
                    text: qsTr("Reduce motion")
                    description: qsTr("Turn off animations.")
                    checked: AppSettings.reducedMotion
                    onToggled: AppSettings.reducedMotion = checked
                }
            }

            SettingsGroup {
                objectName: "settingsLayout"
                heading: qsTr("Layout")
                note: qsTr("What the sidebar and the phone's navigation show. Hidden views stay in the command palette and their shortcuts.")

                ShellSettingRow {
                    title: qsTr("Sidebar")
                    description: qsTr("Entries and sections of the sidebar (and the phone's menu).")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    Repeater {
                        model: root.sidebarItems
                        delegate: BCheckBox {
                            required property var modelData
                            objectName: "sidebarItem_" + modelData.id
                            width: Theme.compact ? parent.width : (parent.width - Theme.space2) / 2
                            text: modelData.title
                            checked: AppSettings.sidebarHidden.indexOf(modelData.id) < 0
                            onToggled: AppSettings.setSidebarItemShown(modelData.id, checked)
                        }
                    }
                }
                ShellSettingRow {
                    title: qsTr("Bottom navigation")
                    description: qsTr("The four buttons at the bottom of narrow windows and phones; Menu comes last.")
                }
                GridLayout {
                    objectName: "bottomNavSlots"
                    Layout.fillWidth: true
                    columns: Theme.compact ? 2 : 4
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2
                    Repeater {
                        model: 4
                        delegate: BButton {
                            id: slotButton
                            required property int index
                            readonly property var view: root.viewInfo(AppSettings.bottomNavTabs[index])
                            objectName: "bottomNavSlot_" + index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1 // equal widths
                            size: "small"
                            iconName: view.icon
                            text: view.text
                            Accessible.name: qsTr("Button %1: %2").arg(index + 1).arg(view.text)
                            onClicked: {
                                root.navSlot = index
                                navSlotMenu.popup(slotButton, 0, slotButton.height)
                            }
                        }
                    }
                }
                BSwitch {
                    objectName: "captureButtonSetting"
                    Layout.fillWidth: true
                    text: qsTr("Quick note button")
                    description: qsTr("The round + button over the views of narrow windows and phones. Quick capture stays in the command palette and under Ctrl+Shift+N.")
                    checked: AppSettings.captureButtonShown
                    onToggled: AppSettings.captureButtonShown = checked
                }
                BButton {
                    objectName: "resetAppearance"
                    size: "small"
                    variant: "ghost"
                    iconName: "rotate-ccw"
                    text: qsTr("Restore the default look")
                    onClicked: AppSettings.resetAppearance()
                }
            }
        }
    }

    Component {
        id: editorSection
        SettingsGroup {
            heading: qsTr("Editor")

            ShellSettingRow {
                objectName: "editorFontRow"
                title: qsTr("Font")
                // A missing family: Theme.fontEditor falls back to the serif / sans-serif choice.
                description: AppSettings.editorFontFamily === "" || !root.fontChoice
                             ? qsTr("Font of your notes; the interface font is set in Appearance.")
                             : Theme.fontEditor === AppSettings.editorFontFamily ? AppSettings.editorFontFamily
                             : AppSettings.editorFont === "sans"
                             ? qsTr("%1 is not installed on this device: sans-serif is used.").arg(AppSettings.editorFontFamily)
                             : qsTr("%1 is not installed on this device: serif is used.").arg(AppSettings.editorFontFamily)
                BSegmented {
                    objectName: "editorFontSetting"
                    // "Other…" only opens the picker, which can be cancelled.
                    controlled: true
                    accessibleName: qsTr("Font")
                    model: root.fontChoice
                           ? [{ value: "serif", text: qsTr("Serif") }, { value: "sans", text: qsTr("Sans-serif") },
                              { value: "other", text: qsTr("Other…", "font") }]
                           : [{ value: "serif", text: qsTr("Serif") }, { value: "sans", text: qsTr("Sans-serif") }]
                    currentValue: root.fontChoice && AppSettings.editorFontFamily !== "" ? "other" : AppSettings.editorFont
                    onActivated: (value) => {
                        if (value === "other") {
                            fontPicker.openTarget("editor")
                        } else {
                            AppSettings.editorFontFamily = ""
                            AppSettings.editorFont = value
                        }
                    }
                }
                BIconButton {
                    visible: root.fontChoice && AppSettings.editorFontFamily !== ""
                    iconName: "pencil"
                    label: qsTr("Choose another font")
                    onClicked: fontPicker.openTarget("editor")
                }
            }
            ShellSettingRow {
                visible: root.fontChoice
                title: qsTr("Code font")
                description: AppSettings.editorMonoFamily === "" ? qsTr("Code blocks, Markdown source and formulas; now the system's monospaced font.")
                             : Theme.fontMono === AppSettings.editorMonoFamily ? AppSettings.editorMonoFamily
                             : qsTr("%1 is not installed on this device: the system's monospaced font is used.").arg(AppSettings.editorMonoFamily)
                BButton {
                    objectName: "monoFontButton"
                    size: "small"
                    text: qsTr("Choose…")
                    iconName: "code"
                    onClicked: fontPicker.openTarget("mono")
                }
            }
            ShellSettingRow {
                title: qsTr("Font size")
                BSegmented {
                    accessibleName: qsTr("Font size")
                    model: [14, 16, 18, 20, 22].map(v => ({ value: v, text: String(v) }))
                    currentValue: AppSettings.editorFontSize
                    onActivated: (value) => AppSettings.editorFontSize = value
                }
            }
            ShellSettingRow {
                title: qsTr("Line spacing")
                BSegmented {
                    objectName: "lineSpacingSetting"
                    accessibleName: qsTr("Line spacing")
                    model: [
                        { value: "compact", text: qsTr("Tight", "line spacing") },
                        { value: "normal", text: qsTr("Normal", "line spacing") },
                        { value: "relaxed", text: qsTr("Relaxed", "line spacing") }
                    ]
                    currentValue: AppSettings.editorLineSpacing
                    onActivated: (value) => AppSettings.editorLineSpacing = value
                }
            }
            ShellSettingRow {
                title: qsTr("Text width")
                description: qsTr("Long lines are hard to read; the editor keeps text within this width.")
                BSegmented {
                    accessibleName: qsTr("Text width")
                    model: [
                        { value: 640, text: qsTr("Narrow") },
                        { value: 760, text: qsTr("Normal", "text width") },
                        { value: 960, text: qsTr("Wide") },
                        { value: 0, text: qsTr("Full") }
                    ]
                    currentValue: AppSettings.editorWidth
                    onActivated: (value) => AppSettings.editorWidth = value
                }
            }
            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Open pages as Markdown source")
                description: qsTr("Show the whole file as text instead of blocks. Ctrl+E switches for one page.")
                checked: AppSettings.editorSourceMode
                onToggled: AppSettings.editorSourceMode = checked
            }
            BSwitch {
                objectName: "settingsWebPictures"
                Layout.fillWidth: true
                text: qsTr("Show pictures from the internet without asking")
                description: qsTr("Off: a picture with a web address waits for “Show”, so opening a synced or imported note tells no site that you read it. Pictures saved to the notebook are always shown.")
                checked: AppSettings.webPicturesShown
                onToggled: AppSettings.webPicturesShown = checked
            }
            // Preview.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: previewColumn.implicitHeight + 2 * Theme.space4
                radius: Theme.radius
                color: Theme.bgSunken
                Column {
                    id: previewColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.space4
                    spacing: Theme.space2
                    BText {
                        id: preview
                        objectName: "editorPreview"
                        width: parent.width
                        text: qsTr("Zażółć gęślą jaźń — a sample of your notes in this font and size, with a second line to show the line spacing.")
                        font.family: Theme.fontEditor
                        font.pixelSize: Theme.textEditor // exactly as in the editor
                        lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightLoose)
                        wrapMode: Text.Wrap
                    }
                    BText {
                        width: parent.width
                        text: "print(\"Zażółć\")  # 0O 1lI {}[]"
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textSm
                        color: Theme.fgMuted
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Component {
        id: vaultSection
        SettingsGroup {
            heading: qsTr("Notebook")
            note: qsTr("These settings are saved in the notebook (.brulion/config.json) and shared by all your devices.")

            ShellSettingRow {
                title: Workspace.name
                description: Workspace.rootPath
                BButton {
                    text: qsTr("Open another…")
                    iconName: "folder-open"
                    size: "small"
                    onClicked: AppController.closeVault()
                }
            }
            ShellSettingRow {
                title: qsTr("Default mode of new pages")
                description: qsTr("For pages that do not choose a mode themselves. The new-page dialog starts with it.")
                BSegmented {
                    objectName: "pageModeSetting"
                    accessibleName: qsTr("Default mode of new pages")
                    model: [
                        { value: "document", text: qsTr("Document"), icon: "file-text" },
                        { value: "outline", text: qsTr("Outline"), icon: "list" }
                    ]
                    currentValue: Workspace.config.pageMode
                    onActivated: (value) => Workspace.setConfig({ pageMode: value })
                }
            }
            ShellSettingRow {
                title: qsTr("Journal mode")
                description: qsTr("Daily notes as an outline (like Logseq) or as documents. A day switched on its own keeps its mode.")
                BSegmented {
                    objectName: "journalModeSetting"
                    accessibleName: qsTr("Journal mode")
                    // The same order as the row above (and the new-page dialog): the two choices
                    // line up, whichever is selected.
                    model: [
                        { value: "document", text: qsTr("Document"), icon: "file-text" },
                        { value: "outline", text: qsTr("Outline"), icon: "list" }
                    ]
                    currentValue: Workspace.config.journalMode
                    onActivated: (value) => Workspace.setConfig({ journalMode: value })
                }
            }
            // An Obsidian vault without .brulion/config.json: the settings below come from its
            // .obsidian/ folder (in memory) until a setting here is changed or they are saved.
            ShellSettingRow {
                id: obsidianRow
                objectName: "obsidianSettingsRow"
                // (config: re-read when Obsidian's settings change; revision: after a save)
                readonly property var adopted: Workspace.config && Workspace.revision >= 0 && Workspace.isOpen
                                               ? Workspace.configFromObsidian() : []
                readonly property var labels: ({
                    journalsFolder: qsTr("Journal folder"),
                    journalFileFormat: qsTr("Journal file names"),
                    journalTemplate: qsTr("Journal template"),
                    templatesFolder: qsTr("Templates folder"),
                    attachmentsFolder: qsTr("Attachments folder")
                })
                visible: obsidianRow.adopted.length > 0
                title: qsTr("Settings from Obsidian")
                description: qsTr("Taken from the Obsidian vault's settings (.obsidian): %1. They are saved to .brulion/config.json when you change a setting here.")
                             .arg(obsidianRow.adopted.map(key => obsidianRow.labels[key] || key).join(", "))
                BButton {
                    objectName: "saveObsidianSettings"
                    text: qsTr("Save to the notebook")
                    iconName: "check"
                    size: "small"
                    onClicked: Workspace.saveAdoptedConfig()
                }
            }
            ShellSettingRow {
                title: qsTr("Journal folder")
                description: qsTr("Where the daily pages are kept. Empty: the top of the notebook.")
                ConfigField { key: "journalsFolder"; leadingIcon: "folder" }
            }
            ShellSettingRow {
                title: qsTr("Journal file names")
                description: qsTr("Date format, e.g. yyyy-MM-dd or dd.MM.yyyy. Today: %1")
                             .arg(Workspace.journalPath(AppController.today()))
                ConfigField {
                    key: "journalFileFormat"
                    invalidText: qsTr("The format needs a day, a month and a year.")
                }
            }
            ShellSettingRow {
                title: qsTr("Journal template")
                description: qsTr("New daily pages start with this page.")
                ConfigField { key: "journalTemplate"; leadingIcon: "file-text" }
            }
            ShellSettingRow {
                title: qsTr("Templates folder")
                ConfigField { key: "templatesFolder"; leadingIcon: "folder" }
            }
            ShellSettingRow {
                title: qsTr("Attachments folder")
                description: qsTr("Pasted images and dropped files go here.")
                ConfigField { key: "attachmentsFolder"; leadingIcon: "folder" }
            }
            ShellSettingRow {
                title: qsTr("Folder for new pages")
                description: qsTr("Empty: the top of the notebook.")
                ConfigField { key: "newPagesFolder"; leadingIcon: "folder"; placeholderText: qsTr("(top level)") }
            }
            ShellSettingRow {
                title: qsTr("Default reminder time")
                description: qsTr("For reminders written without a time (⏰ 2026-10-15).")
                ConfigField {
                    id: defaultReminderTimeField
                    objectName: "defaultReminderTimeField" // (the shell's self-test)
                    key: "defaultReminderTime"
                    leadingIcon: "clock"
                    invalidText: qsTr("Use HH:mm, e.g. 09:00.")
                    Layout.preferredWidth: 112
                    inputMethodHints: Qt.ImhPreferNumbers
                }
                BIconButton {
                    objectName: "defaultReminderTimeClock"
                    iconName: "clock"
                    label: qsTr("Pick a time on a clock")
                    variant: "secondary"
                    onClicked: defaultReminderTimeDialog.openFor(Workspace.config.defaultReminderTime
                                                                || "09:00")

                    BTimePickerDialog {
                        id: defaultReminderTimeDialog
                        objectName: "defaultReminderTimeDialog" // (the shell's self-test)
                        onPicked: (value) => defaultReminderTimeField.applyValue(value)
                    }
                }
            }
            ShellSettingRow {
                title: qsTr("First day of the week")
                BSegmented {
                    accessibleName: qsTr("First day of the week")
                    model: [
                        { value: 1, text: qsTr("Monday") },
                        { value: 7, text: qsTr("Sunday") },
                        { value: 6, text: qsTr("Saturday") }
                    ]
                    currentValue: Workspace.config.weekStartsOn
                    onActivated: (value) => Workspace.setConfig({ weekStartsOn: value })
                }
            }
            BCard {
                Layout.fillWidth: true
                visible: warnings.text !== ""
                tone: "warning"
                padding: Theme.space3
                BText {
                    id: warnings
                    width: parent.width
                    // (config: also Obsidian's settings, which no file revision follows)
                    text: Workspace.config && Workspace.revision >= 0 ? Workspace.configWarnings().join("\n") : ""
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: remindersSection
        SettingsGroup {
            heading: qsTr("Reminders")
            note: AppController.isWasm
                  ? qsTr("In the browser, reminders appear only while Brulion is open in a tab or as an installed app.")
                  : AppController.isAndroid
                    ? qsTr("Reminders and alarms are scheduled in Android and ring even when Brulion is closed.")
                    : qsTr("Reminders appear as system notifications while Brulion runs (also in the tray).")

            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Reminders and alarms")
                description: qsTr("Notify me about ⏰ reminders and ring ⏰! alarms.")
                iconName: "bell"
                checked: AppSettings.remindersEnabled
                onToggled: AppSettings.remindersEnabled = checked
            }
            PermissionRow {
                visible: AppController.isAndroid || (AppController.isWasm && AppController.permissions.notificationsSupported)
                name: "notifications"
                title: qsTr("Notifications")
                description: qsTr("Needed to show reminders.")
            }
            PermissionRow {
                visible: AppController.permissions.exactAlarmsNeeded === true
                name: "exactAlarms"
                title: qsTr("Exact alarms")
                description: qsTr("Without it Android may deliver reminders a few minutes late.")
            }
            PermissionRow {
                visible: AppController.permissions.fullScreenNeeded === true
                name: "fullScreenAlarms"
                title: qsTr("Alarms over the lock screen")
                description: qsTr("Lets an alarm show its full screen like a clock app.")
            }
            ReminderSoundSetting {
                kind: "reminder"
                title: qsTr("Reminder sound")
            }
            ReminderSoundSetting {
                kind: "alarm"
                title: qsTr("Alarm sound")
            }
            ShellSettingRow {
                visible: ReminderSounds.systemSettings
                title: qsTr("System notification settings")
                description: qsTr("Sound, vibration and Do Not Disturb of Brulion's notifications in Android — when a sound does not play as chosen here.")
                BButton {
                    objectName: "reminderSystemSettings"
                    size: "small"
                    text: qsTr("Reminders")
                    onClicked: ReminderSounds.openSystemSettings("reminder")
                }
                BButton {
                    objectName: "alarmSystemSettings"
                    size: "small"
                    text: qsTr("Alarms")
                    onClicked: ReminderSounds.openSystemSettings("alarm")
                }
            }
            ShellSettingRow {
                title: qsTr("Snooze")
                description: qsTr("How long “Snooze” postpones a reminder.")
                BSegmented {
                    accessibleName: qsTr("Snooze")
                    model: [5, 10, 15, 30].map(v => ({ value: v, text: qsTr("%1 min").arg(v) }))
                    currentValue: AppSettings.snoozeMinutes
                    onActivated: (value) => AppSettings.snoozeMinutes = value
                }
            }
            ShellSettingRow {
                title: qsTr("Alarms ring for")
                description: qsTr("An alarm stops by itself after this time.")
                BSegmented {
                    accessibleName: qsTr("Alarms ring for")
                    model: [1, 5, 10, 15].map(v => ({ value: v, text: qsTr("%1 min").arg(v) }))
                    currentValue: AppSettings.alarmRingMinutes
                    onActivated: (value) => AppSettings.alarmRingMinutes = value
                }
            }
            BSwitch {
                Layout.fillWidth: true
                text: qsTr("Show missed reminders")
                description: qsTr("Reminders that were due while the device was off appear when Brulion starts.")
                checked: AppSettings.notifyMissed
                onToggled: AppSettings.notifyMissed = checked
            }
            ShellSettingRow {
                title: qsTr("Test")
                description: qsTr("Check that notifications get through.")
                BButton {
                    text: qsTr("Send a test notification")
                    iconName: "bell-ring"
                    onClicked: AppController.sendTestNotification()
                }
            }
        }
    }

    Component {
        id: syncSection
        // The sync area's section (qml/io/SyncSettings.qml): status of both ways, the sync
        // center, the Nextcloud account form.
        SyncSettings {}
    }

    Component {
        id: appSection
        ColumnLayout {
            spacing: Theme.space8

            SettingsGroup {
                heading: qsTr("Application")

                BSwitch {
                    Layout.fillWidth: true
                    visible: AppController.isDesktop
                    enabled: AppController.trayAvailable
                    text: qsTr("Keep running in the system tray")
                    description: AppController.trayAvailable
                                 ? qsTr("Closing the window keeps Brulion in the tray, so reminders still ring.")
                                 : qsTr("No system tray was found on this desktop.")
                    iconName: "monitor"
                    checked: AppSettings.closeToTray
                    onToggled: AppSettings.closeToTray = checked
                }
                // What a single click on the tray icon does (DesktopTray): the window, or the
                // tray calendar (ShellTrayCalendar; Windows' default).
                ShellSettingRow {
                    objectName: "settingsTrayClick"
                    visible: AppController.isDesktop
                    enabled: AppController.trayAvailable
                    title: qsTr("Click on the tray icon")
                    description: qsTr("A double click always shows the window. The calendar shows the month with its tasks and reminders; it is also in the icon's menu.")
                    BSegmented {
                        accessibleName: qsTr("Click on the tray icon")
                        model: [
                            { value: "window", text: qsTr("Window"), icon: "monitor" },
                            { value: "calendar", text: qsTr("Calendar"), icon: "calendar" }
                        ]
                        currentValue: AppSettings.trayClick
                        onActivated: (value) => AppSettings.trayClick = value
                    }
                }
                // Windows: the Run key (src/platform/autostart.h); with a separate
                // BRULION_PROFILE shown, but disabled.
                BSwitch {
                    objectName: "settingsStartAtLogin"
                    Layout.fillWidth: true
                    visible: AppController.autostartAvailability !== "unsupported"
                    enabled: AppController.autostartAvailability === "available"
                    text: qsTr("Start when you sign in")
                    description: AppController.autostartAvailability === "profile"
                                 ? qsTr("Not in a separate profile (BRULION_PROFILE): Windows starts the default one.")
                                 : AppController.trayAvailable
                                   ? qsTr("Brulion starts hidden in the tray, so reminders ring from the moment you sign in.")
                                   : qsTr("Brulion starts after you sign in.")
                    iconName: "power"
                    checked: AppController.startAtLogin
                    onToggled: {
                        AppController.startAtLogin = checked
                        // Refused: follow the real state again.
                        checked = Qt.binding(() => AppController.startAtLogin)
                    }
                }
                ShellSettingRow {
                    title: qsTr("Event log")
                    description: qsTr("What the app and the sync did, step by step. It stays on this device; share it when something does not work.")
                    BBadge {
                        visible: Logs.problemCount > 0
                        count: Logs.problemCount
                        variant: "warning"
                        Accessible.name: qsTr("%n warning(s) or error(s)", "", Logs.problemCount)
                    }
                    BButton {
                        objectName: "settingsOpenLog"
                        text: qsTr("Open")
                        iconName: "scroll-text"
                        size: "small"
                        onClicked: AppController.navigate({ view: "logs" })
                    }
                }
                BSwitch {
                    objectName: "settingsDetailedLog"
                    Layout.fillWidth: true
                    text: qsTr("Detailed log")
                    description: qsTr("Also writes every request to the server (never note contents or passwords). Turn it on when syncing gives trouble.")
                    iconName: "scroll-text"
                    checked: Logs.detailed
                    onToggled: Logs.detailed = checked
                }
                ShellSettingRow {
                    title: qsTr("Version")
                    description: qsTr("Brulion %1 · %2").arg(AppController.version).arg(AppController.platform)
                    BButton {
                        text: qsTr("About")
                        iconName: "info"
                        size: "small"
                        variant: "ghost"
                        onClicked: AppController.requestUiAction("about")
                    }
                }
                ShellSettingRow {
                    title: qsTr("Licence")
                    description: qsTr("Free software: GNU Affero General Public License 3.0 or later (AGPL-3.0). Icons: Lucide (ISC). Emoji and symbols: Noto Emoji, Noto Sans Math (OFL 1.1).")
                    BButton {
                        text: qsTr("Read")
                        iconName: "external-link"
                        size: "small"
                        variant: "ghost"
                        onClicked: Workspace.openUrl("https://www.gnu.org/licenses/agpl-3.0.html")
                    }
                }
                ShellSettingRow {
                    title: qsTr("Source code")
                    description: "github.com/janusz-bit/brulion"
                    BButton {
                        text: qsTr("Open")
                        iconName: "external-link"
                        size: "small"
                        variant: "ghost"
                        onClicked: Workspace.openUrl("https://github.com/janusz-bit/brulion")
                    }
                }
            }

            // AI agents and scripts driving the running app (desktop: src/app/shell/remotecontrol.h,
            // docs/CLI.md "Remote control"). The setting is this device's; --remote-control turns it
            // on for one run.
            SettingsGroup {
                objectName: "settingsDeveloper"
                visible: AppController.remoteControl.available === true
                heading: qsTr("For developers")

                BSwitch {
                    objectName: "settingsRemoteControl"
                    Layout.fillWidth: true
                    text: qsTr("Allow agents to control the app (local socket)")
                    description: qsTr("AI agents and scripts of your user account can then see what Brulion shows, click, type and take screenshots (brulion-cli app …). Any program you run can then read your notes too — turn it on only while you need it.")
                    iconName: "terminal"
                    checked: AppController.remoteControl.setting === true
                    onToggled: AppSettings.setValue("devtools/remoteControl", checked)
                }
                BText {
                    objectName: "settingsRemoteControlStatus"
                    Layout.fillWidth: true
                    visible: text !== ""
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                    text: {
                        const rc = AppController.remoteControl
                        if (rc.error)
                            return qsTr("The socket could not be opened: %1").arg(rc.error)
                        if (!rc.active)
                            return ""
                        return (rc.forced ? qsTr("On for this run (started with --remote-control).") + " " : "")
                                + qsTr("Listening at %1").arg(rc.socket)
                    }
                }
            }
        }
    }
}
