pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SettingsView — route "settings" ({section?}). Sections: General (language, "Run the intro
// again": AppController.restartIntro()), Appearance (theme,
// accent colour with presets and BAccentDialog, light and dark backgrounds, text size, interface
// font, density, reduce motion, back to the default look after a confirmation —
// AppSettings.resetAppearance(), Theme inputs; Layout: sidebar entries and sections, the bottom
// navigation's buttons, the quick note button),
// Editor (font — serif, sans or any installed family —, code font, size, line spacing, width,
// source mode, spell checking — on/off, its languages, the personal dictionary's dialog, shown only
// where SpellCheck.available —, word count, focus mode, typewriter scrolling; font pickers:
// BFontPickerDialog, not in the browser), Keyboard shortcuts (qml/shell/ShellShortcutSettings: the
// commands' shortcuts), Notebook (the vault's shared settings in
// .brulion/config.json: the note modes of new pages and of the journal, journal folder and file
// names, attachments, templates, default reminder time, first day of the week), Reminders (on/off, permissions on Android and in the
// browser, the sounds of reminders and alarms — qml/notify/ReminderSoundSetting —, snooze, alarm
// length, Android's background check of the notes, test notification), Sync (qml/io/SyncSettings: status,
// the sync center, the Nextcloud account), Backups (qml/shell/ShellBackupSettings), Application (close to tray, the event log and its
// detailed mode, version, licence, source code; on the desktop "For developers": the switch
// that lets AI agents drive the running app, AppController.remoteControl).
//
// One section at a time: wide windows list the sections on the left; narrow ones (phones) have
// them as tabs at the top, a bar that scrolls sideways, the route's section scrolled into view.
// Choosing one replaces the route's section (no history entry).
Item {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Settings")
    property string section: AppController.routeText(route, "section") !== "" ? AppController.routeText(route, "section") : "general"
    /// The route's section in `sections` (General for an unknown one, as sectionComponent()).
    readonly property int sectionIndex: indexOfSection(section)
    function indexOfSection(key: string): int {
        for (let i = 0; i < sections.length; ++i) {
            if (sectionKey(i) === key)
                return i
        }
        return 0
    }
    function sectionKey(i: int): string {
        return i >= 0 && i < sections.length ? AppController.routeText(sections[i], "key") : ""
    }
    function openSection(key: string): void {
        if (key !== "" && key !== section)
            AppController.replaceRoute({ view: "settings", section: key })
    }

    /// The sections ({ key, title, iconName }).
    readonly property list<var> sections: [
        { key: "general", title: qsTr("General"), iconName: "settings" },
        { key: "appearance", title: qsTr("Appearance"), iconName: "palette" },
        { key: "editor", title: qsTr("Editor"), iconName: "pencil" },
        { key: "shortcuts", title: qsTr("Keyboard shortcuts"), iconName: "keyboard" },
        { key: "vault", title: qsTr("Notebook"), iconName: "notebook-pen" },
        { key: "reminders", title: qsTr("Reminders"), iconName: "bell" },
        { key: "extensions", title: qsTr("Extensions"), iconName: "puzzle" },
        { key: "sync", title: qsTr("Sync"), iconName: "folder-sync" },
        { key: "backups", title: qsTr("Backups"), iconName: "hard-drive" },
        { key: "app", title: qsTr("Application"), iconName: "info" }
    ]

    function sectionComponent(id: string): Component {
        switch (id) {
        case "appearance": return appearanceSection
        case "editor": return editorSection
        case "shortcuts": return shortcutsSection
        case "vault": return vaultSection
        case "reminders": return remindersSection
        case "extensions": return extensionsSection
        case "sync": return syncSection
        case "backups": return backupsSection
        case "app": return appSection
        default: return generalSection
        }
    }

    /// Font families can be chosen (not in the browser: Qt there has only its bundled fonts).
    readonly property bool fontChoice: DeviceInfo.platform !== "wasm"
    /// The surface name of the theme in use (for the accent swatches).
    readonly property string surface: Theme.dark ? Theme.darkSurface : Theme.lightSurface
    /// Accent presets with their names ({ value, name }).
    readonly property list<var> accents: [
        { value: "coral", name: qsTr("Coral") },
        { value: "red", name: qsTr("Red") },
        { value: "blue", name: qsTr("Blue") },
        { value: "green", name: qsTr("Green") },
        { value: "teal", name: qsTr("Teal") },
        { value: "violet", name: qsTr("Violet") },
        { value: "graphite", name: qsTr("Graphite") }
    ]
    /// The sidebar's navigation entries and sections (AppSettings.sidebarItems(); { key, title }).
    readonly property list<var> sidebarItems: [
        { key: "journals", title: qsTr("Journals") },
        { key: "calendar", title: qsTr("Calendar") },
        { key: "tasks", title: qsTr("Tasks") },
        { key: "habits", title: qsTr("Habits") },
        { key: "gym", title: qsTr("Gym") },
        { key: "allPages", title: qsTr("All pages") },
        { key: "tags", title: qsTr("Tags") },
        { key: "graph", title: qsTr("Graph") },
        { key: "search", title: qsTr("Search") },
        { key: "favorites", title: qsTr("Favorites section") },
        { key: "recent", title: qsTr("Recent section") }
    ]
    /// The sidebar's page sections, whose order can change (AppSettings.sidebarSectionIds()): their
    /// names and icons (an unknown one: its id, no icon).
    function sidebarSectionTitle(id: string): string {
        switch (id) {
        case "favorites": return qsTr("Favorites")
        case "recent": return qsTr("Recent")
        case "pages": return qsTr("Pages (the folder tree)")
        }
        return id
    }
    function sidebarSectionIcon(id: string): string {
        switch (id) {
        case "favorites": return "star"
        case "recent": return "clock"
        case "pages": return "folder-tree"
        }
        return ""
    }
    /// Views the bottom navigation offers (AppSettings.bottomNavViews(); { value, text, iconName }).
    readonly property list<var> bottomNavViews: [
        { value: "journals", text: qsTr("Journal"), iconName: "book-open" },
        { value: "calendar", text: qsTr("Calendar"), iconName: "calendar" },
        { value: "search", text: qsTr("Search"), iconName: "search" },
        { value: "tasks", text: qsTr("Tasks"), iconName: "list-todo" },
        { value: "habits", text: qsTr("Habits"), iconName: "flame" },
        { value: "gym", text: qsTr("Gym"), iconName: "dumbbell" },
        { value: "allPages", text: qsTr("All pages"), iconName: "files" },
        { value: "tags", text: qsTr("Tags"), iconName: "tags" },
        { value: "graph", text: qsTr("Graph"), iconName: "waypoints" }
    ]
    /// The name of a spelling language ("pl", "en") in the UI language.
    function languageName(code: string): string {
        return code === "pl" ? qsTr("Polish") : code === "en" ? qsTr("English") : code
    }
    /// The bottom navigation's view `value`: its name (the value itself when unknown) and icon.
    function viewText(value: string): string {
        for (let i = 0; i < bottomNavViews.length; ++i) {
            if (AppController.routeText(bottomNavViews[i], "value") === value)
                return AppController.routeText(bottomNavViews[i], "text")
        }
        return value
    }
    function viewIcon(value: string): string {
        for (let i = 0; i < bottomNavViews.length; ++i) {
            if (AppController.routeText(bottomNavViews[i], "value") === value)
                return AppController.routeText(bottomNavViews[i], "iconName")
        }
        return ""
    }
    /// The text size step nearest to the current scale.
    function nearestFontScale(): real {
        const steps = [0.9, 1.0, 1.15, 1.3]
        let best = 1.0
        for (let i = 0; i < steps.length; ++i) {
            if (Math.abs(steps[i] - AppSettings.fontScale) < Math.abs(best - AppSettings.fontScale))
                best = steps[i]
        }
        return best
    }
    /// "Polish, English" of the spelling languages checked now.
    function languageNames(codes: list<string>): string {
        let out = ""
        for (let i = 0; i < codes.length; ++i) {
            const code = codes[i]
            out += (i > 0 ? ", " : "") + (code === "pl" ? qsTr("Polish") : code === "en" ? qsTr("English") : code)
        }
        return out
    }
    /// The spelling language choices this device has dictionaries for.
    function spellChoices(have: list<string>): list<var> {
        const pl = have.indexOf("pl") >= 0
        const en = have.indexOf("en") >= 0
        const auto = { value: "auto", text: qsTr("Automatic", "spelling languages") }
        const polish = { value: "pl", text: languageName("pl") }
        const english = { value: "en", text: languageName("en") }
        const both = { value: "pl,en", text: qsTr("Both", "spelling languages") }
        if (pl && en)
            return [auto, polish, english, both]
        if (pl)
            return [auto, polish]
        if (en)
            return [auto, english]
        return [auto]
    }
    /// The names of Obsidian settings taken over (Workspace.configFromObsidian()).
    function obsidianLabels(keys: list<string>): string {
        let out = ""
        for (let i = 0; i < keys.length; ++i) {
            const key = keys[i]
            const label = key === "journalsFolder" ? qsTr("Journal folder")
                        : key === "journalFileFormat" ? qsTr("Journal file names")
                        : key === "journalTemplate" ? qsTr("Journal template")
                        : key === "templatesFolder" ? qsTr("Templates folder")
                        : key === "attachmentsFolder" ? qsTr("Attachments folder") : key
            out += (i > 0 ? ", " : "") + label
        }
        return out
    }
    function adoptedSettings(): list<string> {
        return Workspace.configFromObsidian()
    }
    function configWarnings(): list<string> {
        return Workspace.configWarnings()
    }
    /// The file of the week's or month's note holding today ("" when unknown).
    function periodicPath(period: string): string {
        return AppController.routeText(Workspace.periodicNote(period, AppController.today(0)), "path")
    }
    /// The bottom navigation slot whose menu is open.
    property int navSlot: 0

    BFontPickerDialog {
        id: fontPicker
        /// "ui" | "editor" | "mono"
        property string target
        function openTarget(which: string): void {
            target = which
            fixedOnly = which === "mono"
            title = which === "ui" ? qsTr("Interface font") : which === "mono" ? qsTr("Code font") : qsTr("Font of notes")
            defaultLabel = which === "mono" ? qsTr("JetBrains Mono (default)") : qsTr("Inter (default)")
            defaultFamily = which === "mono" ? DeviceInfo.defaultMonoFamily : DeviceInfo.defaultUiFamily
            openFor(which === "ui" ? AppSettings.uiFontFamily : which === "mono" ? AppSettings.editorMonoFamily
                                                                                : AppSettings.editorFontFamily)
        }
        onChosen: (family) => {
            if (target === "ui")
                AppSettings.uiFontFamily = family
            else if (target === "mono")
                AppSettings.editorMonoFamily = family
            else if (family === "") {
                AppSettings.editorFontFamily = "" // the default row: sans-serif (Inter)
                AppSettings.editorFont = "sans"
            } else {
                AppSettings.editorFontFamily = family
            }
        }
    }
    // The personal dictionary of spell checking (a device setting: AppSettings.spellingWords).
    BDialog {
        id: dictionaryDialog
        objectName: "spellDictionaryDialog"
        title: qsTr("Personal dictionary")
        subtitle: qsTr("Words spell checking takes as right, on this device.")
        iconName: "book-open"
        showReject: false
        acceptText: qsTr("Close")
        initialFocusItem: newWord
        function addTyped(): void {
            const w = Js.trim(newWord.text)
            if (w !== "") {
                SpellCheck.addToDictionary(w)
                newWord.text = ""
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BTextField {
                id: newWord
                objectName: "spellNewWord"
                Layout.fillWidth: true
                placeholderText: qsTr("A word to add")
                onAccepted: dictionaryDialog.addTyped()
            }
            BButton {
                objectName: "spellAddButton"
                text: qsTr("Add")
                iconName: "plus"
                enabled: Js.trim(newWord.text) !== ""
                onClicked: dictionaryDialog.addTyped()
            }
        }
        BText {
            visible: SpellCheck.personalWords.length === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.fgMuted
            text: qsTr("No words yet. Add them here or with “Add to dictionary” in the editor's menu on an underlined word.")
        }
        Repeater {
            model: SpellCheck.personalWords
            delegate: RowLayout {
                id: wordRow
                required property string modelData
                Layout.fillWidth: true
                spacing: Theme.space2
                BText {
                    Layout.fillWidth: true
                    text: wordRow.modelData
                    elide: Text.ElideRight
                }
                BIconButton {
                    objectName: "spellRemoveWord"
                    iconName: "trash-2"
                    //: %1: a word of the personal dictionary
                    label: qsTr("Remove “%1”").arg(wordRow.modelData)
                    onClicked: SpellCheck.removeFromDictionary(wordRow.modelData)
                }
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
            id: navChoice
            required property string value
            required text
            required iconName
            objectName: "bottomNavChoice_" + value
            checkable: true
            Binding on checked { value: AppSettings.bottomNavTabs[root.navSlot] === navChoice.value }
            onTriggered: {
                AppSettings.setBottomNavTab(root.navSlot, navChoice.value)
                // The tap toggled the item itself; the view already in the slot changes nothing
                // in AppSettings, so the binding above would not be applied again.
                checked = AppSettings.bottomNavTabs[root.navSlot] === navChoice.value
            }
        }
        onObjectAdded: (index, object) => navSlotMenu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => navSlotMenu.removeItem(object as Item)
    }

    /// What Settings → Extensions says about an extension page's state.
    function extensionStateText(state: string, message: string, line: int, commands: int, views: int, hooks: int): string {
        switch (state) {
        case "running": {
            const parts = qsTr("%n command(s)", "", commands) + ", " + qsTr("%n view(s)", "", views) + ", "
                          + qsTr("%n reaction(s)", "", hooks)
            return qsTr("Runs: %1").arg(parts) + (message !== "" ? " — " + message : "")
        }
        case "error": return line >= 0 ? qsTr("Error on line %1: %2").arg(line + 1).arg(message) : qsTr("Error: %1").arg(message)
        case "approval": return qsTr("New or changed elsewhere: read the code, then run it on this device.")
        case "off": return qsTr("Off on this device.")
        case "duplicate": return qsTr("Another page has the same name: only the first one runs.")
        case "builtinOff": return qsTr("It replaces a built-in extension that is off.")
        case "problem": return qsTr("No code: add a ```luau block.")
        default: return message
        }
    }

    // ---- Building blocks --------------------------------------------------------------------

    /// A titled group of setting rows: a card, its rows divided by hairlines. Below 600 px the
    /// card is flat (no surface, border or padding): heading, note and rows share the page's
    /// 16-px edge, and only the headings and the gap set the groups apart.
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
            flat: Theme.compact
            padding: Theme.space5
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
        readonly property string configText: AppController.routeText(Workspace.config, key)
        Layout.preferredWidth: Theme.compact ? -1 : 260
        Layout.fillWidth: Theme.compact
        // (A Binding: assigning the text below does not end it.)
        Binding on text { value: field.configText }
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        function commit(value: string): void {
            errorText = Workspace.setConfig(Js.withKey({}, key, value)) ? "" : invalidText
        }
        function save(): void {
            if (text !== configText)
                commit("" + Js.trim(text))
        }
        /// Save a value picked elsewhere (a clock dialog) at once. The binding to the config
        /// shows the value (a value changed later elsewhere shows too).
        function applyValue(value: string): void {
            commit(value)
            // Typed text the pick replaces, when the config already had that value.
            if (text !== configText)
                text = configText
        }
        onEditingFinished: save()
    }

    /// A switch row of a settings card.
    component SettingSwitch: BSwitch {
        Layout.fillWidth: true
    }

    /// Granted / not granted, with a button to ask.
    component PermissionRow: ShellSettingRow {
        id: permission
        required property string name
        readonly property bool granted: AppController.routeFlag(AppController.permissions, name)
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
                        id: sectionItem
                        required property string key
                        required property string title
                        required iconName
                        width: parent.width
                        text: title
                        selected: root.section === key
                        selectionBar: true
                        onClicked: root.openSection(sectionItem.key)
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

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Section tabs (narrow windows), made when the window becomes narrow: their
            // currentIndex is bound from the route at their creation.
            Loader {
                Layout.fillWidth: true
                active: Theme.narrow
                visible: active
                sourceComponent: sectionTabs
            }

            Flickable {
                id: flick
                TouchFlick.enabled: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: content.implicitHeight + 2 * margin
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: BScrollBar {}

                readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8

                ColumnLayout {
                    id: content
                    x: flick.margin
                    y: flick.margin
                    width: Math.min(flick.width - 2 * flick.margin, 760)
                    spacing: Theme.space8

                    // The chosen section, from its top.
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: root.sectionComponent(root.section)
                        onLoaded: flick.contentY = 0
                    }
                }
            }
        }
    }

    Component {
        id: sectionTabs
        ColumnLayout {
            spacing: 0
            BTabBar {
                Layout.fillWidth: true
                leftPadding: Theme.compact ? Theme.space1 : Theme.space6
                fadeColor: Theme.bg
                currentIndex: root.sectionIndex
                onPicked: (index) => root.openSection(root.sectionKey(index))

                Repeater {
                    model: root.sections
                    delegate: BTabButton {
                        required property int index
                        required property string key
                        required property string title
                        objectName: "settingsTab-" + key
                        text: title
                        // Phones: the tabs without icons, as the sync center's (more of them fit).
                        iconName: Theme.compact ? "" : AppController.routeText(root.sections[index], "iconName")
                        leftPadding: Theme.compact ? 9 : (Theme.touchUi ? 14 : 12)
                        rightPadding: leftPadding
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.borderSubtle
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
                    model: [{ value: "en", text: "English" }, { value: "pl", text: "Polski" }]
                    currentValue: AppSettings.language
                    onActivated: (value) => AppSettings.language = value
                }
            }
            ShellSettingRow {
                title: qsTr("Intro")
                description: qsTr("Language, look, permissions and the settings of a new notebook, step by step. The notebook closes and is chosen again at the end.")
                BButton {
                    objectName: "settingsRestartIntro"
                    size: "small"
                    text: qsTr("Run the intro again")
                    iconName: "sparkles"
                    onClicked: AppController.restartIntro()
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
                        objectName: "themeModeSetting"
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
                    id: accentRow
                    title: qsTr("Accent colour")
                    description: qsTr("Marks today, the current tab and highlights. Warnings and deleting stay red.")
                    Flow {
                        objectName: "accentSwatches"
                        // The presets and "Custom…" on one line (8 × 44 = 352 px fit a 412-px
                        // phone), else even lines. (Filling up to that, not a fixed width: a fixed
                        // one wider than the row widened the whole page.)
                        Layout.fillWidth: true
                        Layout.maximumWidth: accentRow.evenWrapWidth(root.accents.length + 1, Theme.touchUi ? Theme.touchTarget : 34)
                        spacing: 0
                        Repeater {
                            model: root.accents
                            delegate: BColorSwatch {
                                id: swatch
                                required property string value
                                required property string name
                                objectName: "accentSwatch_" + value
                                color: "" + ThemePalette.accentSwatch(value, "", Theme.dark, root.surface)
                                label: name
                                // Theme: the setting, or a screenshot's --accent.
                                selected: Theme.accentName === value
                                onClicked: AppSettings.accentColor = swatch.value
                            }
                        }
                        BColorSwatch {
                            objectName: "accentSwatch_custom"
                            readonly property string custom: Theme.accentName === "custom" ? Theme.customAccent
                                                                                           : AppSettings.customAccent
                            color: custom !== "" ? "" + ThemePalette.accentSwatch("custom", custom, Theme.dark, root.surface)
                                                 // (bgSunken is a black hole in the dark.)
                                                 : Theme.dark ? Theme.bgElevated2 : Theme.bgSunken
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
                        objectName: "fontScaleSetting"
                        accessibleName: qsTr("Text size")
                        model: [
                            { value: 0.9, text: qsTr("Small") },
                            { value: 1.0, text: qsTr("Normal") },
                            { value: 1.15, text: qsTr("Large") },
                            { value: 1.3, text: qsTr("Larger") }
                        ]
                        currentValue: root.nearestFontScale()
                        onActivated: (value) => AppSettings.fontScale = value
                    }
                }
                ShellSettingRow {
                    visible: root.fontChoice
                    title: qsTr("Interface font")
                    description: AppSettings.uiFontFamily === "" ? qsTr("Inter, built into Brulion.")
                                 : Theme.fontUi === AppSettings.uiFontFamily ? AppSettings.uiFontFamily
                                 : qsTr("%1 is not installed on this device: Inter is used.").arg(AppSettings.uiFontFamily)
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
                SettingSwitch {
                    text: qsTr("Reduce motion")
                    description: qsTr("Turn off animations.")
                    checked: AppSettings.reducedMotion
                    onToggled: AppSettings.reducedMotion = checked
                }
                // The end of the card whose settings it restores (and the editor's look), asked
                // first: no undo.
                BButton {
                    objectName: "resetAppearance"
                    size: "small"
                    variant: "ghost"
                    iconName: "rotate-ccw"
                    text: qsTr("Restore the default look…")
                    onClicked: resetLookConfirm.open()
                }
            }
            // What AppSettings.resetAppearance() restores, and what it leaves.
            BConfirmDialog {
                id: resetLookConfirm
                objectName: "resetLookConfirm"
                title: qsTr("Restore the default look?")
                message: qsTr("The accent colour, backgrounds, text size, interface font and density, and the editor's fonts, font size and line spacing go back to their defaults. The theme, the layout and the text width stay.")
                confirmText: qsTr("Restore")
                onAccepted: AppSettings.resetAppearance()
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
                            id: sidebarItem
                            required property string key
                            required property string title
                            objectName: "sidebarItem_" + key
                            width: Theme.compact ? parent.width : (parent.width - Theme.space2) / 2
                            text: title
                            checked: AppSettings.sidebarHidden.indexOf(key) < 0
                            onToggled: AppSettings.setSidebarItemShown(sidebarItem.key, checked)
                        }
                    }
                }
                ShellSettingRow {
                    title: qsTr("Order of the sections")
                    description: qsTr("Favorites, recent pages and the folder tree beside the views' icons, top to bottom.")
                }
                Column {
                    objectName: "sidebarSectionOrder"
                    Layout.fillWidth: true
                    spacing: Theme.space1
                    Repeater {
                        model: AppSettings.sidebarSections
                        delegate: Rectangle {
                            id: sectionRow
                            required property string modelData
                            required property int index
                            readonly property string sectionTitle: root.sidebarSectionTitle(modelData)
                            readonly property string sectionIcon: root.sidebarSectionIcon(modelData)
                            objectName: "sidebarSection_" + modelData
                            width: Math.min(parent.width, 420)
                            height: Theme.touchUi ? Theme.touchTarget + 4 : Theme.controlHeight + 4
                            radius: Theme.radius
                            color: Theme.bgSunken
                            border.width: 1
                            border.color: Theme.borderSubtle
                            Accessible.role: Accessible.ListItem
                            Accessible.name: qsTr("%1. %2").arg(index + 1).arg(sectionTitle)
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.space3
                                anchors.rightMargin: Theme.space1
                                spacing: Theme.space2
                                BText {
                                    text: "" + (sectionRow.index + 1)
                                    color: Theme.fgFaint
                                    variant: "small"
                                    Accessible.ignored: true
                                }
                                BIcon {
                                    name: sectionRow.sectionIcon
                                    size: Theme.iconSizeSm
                                    color: Theme.fgMuted
                                }
                                BText {
                                    Layout.fillWidth: true
                                    text: sectionRow.sectionTitle
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }
                                BIconButton {
                                    objectName: "sidebarSectionUp_" + sectionRow.modelData
                                    iconName: "chevron-up"
                                    label: qsTr("Move %1 up").arg(sectionRow.sectionTitle)
                                    size: "small"
                                    enabled: sectionRow.index > 0
                                    onClicked: AppSettings.moveSidebarSection(sectionRow.modelData, -1)
                                }
                                BIconButton {
                                    objectName: "sidebarSectionDown_" + sectionRow.modelData
                                    iconName: "chevron-down"
                                    label: qsTr("Move %1 down").arg(sectionRow.sectionTitle)
                                    size: "small"
                                    enabled: sectionRow.index < AppSettings.sidebarSections.length - 1
                                    onClicked: AppSettings.moveSidebarSection(sectionRow.modelData, 1)
                                }
                            }
                        }
                    }
                }
                ShellSettingRow {
                    title: qsTr("Bottom navigation")
                    description: qsTr("The five views at the bottom of narrow windows and phones.")
                }
                GridLayout {
                    objectName: "bottomNavSlots"
                    Layout.fillWidth: true
                    columns: Theme.compact ? 3 : 5
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2
                    Repeater {
                        model: 5
                        delegate: BButton {
                            id: slotButton
                            required property int index
                            readonly property string view: index < AppSettings.bottomNavTabs.length ? AppSettings.bottomNavTabs[index] : ""
                            objectName: "bottomNavSlot_" + index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1 // equal widths
                            size: "small"
                            iconName: root.viewIcon(view)
                            text: root.viewText(view)
                            Accessible.name: qsTr("Button %1: %2").arg(index + 1).arg(root.viewText(view))
                            onClicked: {
                                root.navSlot = index
                                navSlotMenu.popup(slotButton, 0, slotButton.height)
                            }
                        }
                    }
                }
                SettingSwitch {
                    objectName: "captureButtonSetting"
                    text: qsTr("Quick note button")
                    description: qsTr("The + button in the top bar of narrow windows and phones. Quick capture stays in the command palette and under Ctrl+Shift+N.")
                    checked: AppSettings.captureButtonShown
                    onToggled: AppSettings.captureButtonShown = checked
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
                             ? qsTr("Font of your notes: Inter (sans-serif) or Literata (serif), built into Brulion; the interface font is set in Appearance.")
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
                           ? [{ value: "sans", text: qsTr("Sans-serif") }, { value: "serif", text: qsTr("Serif") },
                              { value: "other", text: qsTr("Other…", "font") }]
                           : [{ value: "sans", text: qsTr("Sans-serif") }, { value: "serif", text: qsTr("Serif") }]
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
                description: AppSettings.editorMonoFamily === "" ? qsTr("Code blocks, Markdown source, formulas and dates; now JetBrains Mono, built into Brulion.")
                             : Theme.fontMono === AppSettings.editorMonoFamily ? AppSettings.editorMonoFamily
                             : qsTr("%1 is not installed on this device: JetBrains Mono is used.").arg(AppSettings.editorMonoFamily)
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
                    model: [{ value: 14, text: "14" }, { value: 16, text: "16" }, { value: 18, text: "18" },
                            { value: 20, text: "20" }, { value: 22, text: "22" }]
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
                    objectName: "editorWidthSetting"
                    accessibleName: qsTr("Text width")
                    model: [
                        { value: 600, text: qsTr("Narrow") },
                        { value: 680, text: qsTr("Normal", "text width") },
                        { value: 960, text: qsTr("Wide") },
                        { value: 0, text: qsTr("Full") }
                    ]
                    currentValue: AppSettings.editorWidth
                    onActivated: (value) => AppSettings.editorWidth = value
                }
            }
            SettingSwitch {
                text: qsTr("Open pages as Markdown source")
                description: qsTr("Show the whole file as text instead of blocks. Ctrl+E switches for one page.")
                checked: AppSettings.editorSourceMode
                onToggled: AppSettings.editorSourceMode = checked
            }
            // Task status history in the task's :LOGBOOK: (docs/SYNTAX.md §5; default off).
            SettingSwitch {
                objectName: "settingsTaskStatusHistory"
                text: qsTr("Log task status changes")
                description: qsTr("Each change is written to the task's LOGBOOK with its time, and “In progress” starts and stops the task's timer, as in Logseq.")
                checked: AppSettings.taskStatusHistory
                onToggled: AppSettings.taskStatusHistory = checked
            }
            // Spell checking (docs/UI.md "Spell checking"): only where this device can check.
            SettingSwitch {
                objectName: "settingsSpellCheck"
                visible: SpellCheck.available
                text: qsTr("Check spelling")
                description: Theme.touchUi
                             ? qsTr("Misspelled words of the block you are editing are underlined; tap one for corrections.")
                             : qsTr("Misspelled words of the block you are editing are underlined; right-click one for corrections.")
                checked: AppSettings.spellCheck
                onToggled: AppSettings.spellCheck = checked
            }
            ShellSettingRow {
                objectName: "spellLanguagesRow"
                visible: SpellCheck.available && AppSettings.spellCheck
                title: qsTr("Spelling languages")
                description: SpellCheck.languages.length === 0
                             ? qsTr("No dictionary on this device for this choice.")
                             : qsTr("Checked now: %1.").arg(root.languageNames(SpellCheck.languages))
                BSegmented {
                    objectName: "spellLanguagesSetting"
                    accessibleName: qsTr("Spelling languages")
                    model: root.spellChoices(SpellCheck.availableLanguages)
                    currentValue: AppSettings.spellLanguages === "en,pl" ? "pl,en" : AppSettings.spellLanguages
                    onActivated: (value) => AppSettings.spellLanguages = value
                }
            }
            ShellSettingRow {
                objectName: "spellDictionaryRow"
                visible: SpellCheck.available && AppSettings.spellCheck
                title: qsTr("Personal dictionary")
                description: SpellCheck.personalWords.length === 0
                             ? qsTr("Words you add are kept on this device for all notebooks.")
                             : qsTr("%n word(s), kept on this device for all notebooks.", "", SpellCheck.personalWords.length)
                BButton {
                    objectName: "spellDictionaryButton"
                    size: "small"
                    text: qsTr("Edit…")
                    iconName: "book-open"
                    onClicked: dictionaryDialog.open()
                }
            }
            SettingSwitch {
                objectName: "settingsWordCount"
                text: qsTr("Word count")
                description: qsTr("Words, characters and reading time in the corner of a page (wide windows); the page's menu always shows them.")
                checked: AppSettings.wordCountShown
                onToggled: AppSettings.wordCountShown = checked
            }
            SettingSwitch {
                objectName: "settingsFocusMode"
                text: qsTr("Focus mode")
                description: qsTr("On a page and in the journal only the text, without side bars and toolbars. %1 or Esc leaves it.")
                             .arg(ShortcutMap.sequences["view.focus"] || qsTr("The command palette"))
                checked: AppSettings.focusMode
                onToggled: AppSettings.focusMode = checked
            }
            SettingSwitch {
                objectName: "settingsReadingMode"
                text: qsTr("Reading mode")
                description: qsTr("Pages and the journal open for reading: a tap follows links and never starts editing, nothing changes a note. %1 or the book in a page's header switches it.")
                             .arg(ShortcutMap.sequences["view.reading"] || qsTr("The command palette"))
                checked: AppSettings.readingMode
                onToggled: AppSettings.readingMode = checked
            }
            SettingSwitch {
                objectName: "settingsTypewriter"
                text: qsTr("Typewriter scrolling")
                description: qsTr("The line you are writing stays in the middle of the screen.")
                checked: AppSettings.typewriterScrolling
                onToggled: AppSettings.typewriterScrolling = checked
            }
            SettingSwitch {
                objectName: "settingsWebPictures"
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
                    anchors.fill: parent
                    anchors.margins: Theme.space4
                    spacing: Theme.space2
                    BText {
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
            // This device's (AppSettings), never .brulion/config.json: a sync peer must not be able
            // to switch it on (Vault::setFollowsOutsideLinks; docs/UI.md, the settings view).
            SettingSwitch {
                objectName: "settingsFollowOutsideLinks"
                text: qsTr("Follow links to folders outside the notebook")
                description: qsTr("On this device only. A synced or copied link can point to any folder of yours: its files would be listed, synced and editable here.")
                checked: AppSettings.followsOutsideLinks
                onToggled: AppSettings.followsOutsideLinks = checked
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
                readonly property list<string> adopted: Workspace.config && Workspace.revision >= 0 && Workspace.isOpen
                                                        ? root.adoptedSettings() : []
                visible: obsidianRow.adopted.length > 0
                title: qsTr("Settings from Obsidian")
                description: qsTr("Taken from the Obsidian vault's settings (.obsidian): %1. They are saved to .brulion/config.json when you change a setting here.")
                             .arg(root.obsidianLabels(obsidianRow.adopted))
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
            // Weekly (ISO week) and monthly notes (docs/SYNTAX.md §1 "Weekly and monthly notes").
            ShellSettingRow {
                title: qsTr("Weekly notes folder")
                description: qsTr("Empty: the top of the notebook. Open this week's note with Ctrl+Alt+W or a week number in the calendar.")
                ConfigField { objectName: "weeklyFolderField"; key: "weeklyFolder"; leadingIcon: "folder"; placeholderText: qsTr("(top level)") }
            }
            ShellSettingRow {
                title: qsTr("Weekly note file names")
                // (revision: the example follows a format just saved)
                description: qsTr("yyyy is the week's year, ww its number (w without a leading zero), text in 'quotes'. This week: %1")
                             .arg(Workspace.revision >= 0 ? root.periodicPath("week") : "")
                ConfigField {
                    objectName: "weeklyFileFormatField"
                    key: "weeklyFileFormat"
                    invalidText: qsTr("The format needs a year (yyyy) and a week (ww), other letters in 'quotes'.")
                }
            }
            ShellSettingRow {
                title: qsTr("Weekly note template")
                description: qsTr("New weekly notes start with this page: {{date}} is the Monday, {{end}} the Sunday, {{week}} the week's number.")
                ConfigField { key: "weeklyTemplate"; leadingIcon: "file-text" }
            }
            ShellSettingRow {
                title: qsTr("Monthly notes folder")
                description: qsTr("Empty: the top of the notebook. Open this month's note with Ctrl+Alt+M or the month's title in the calendar.")
                ConfigField { objectName: "monthlyFolderField"; key: "monthlyFolder"; leadingIcon: "folder"; placeholderText: qsTr("(top level)") }
            }
            ShellSettingRow {
                title: qsTr("Monthly note file names")
                description: qsTr("Date format of the month, e.g. yyyy-MM or MMMM yyyy. This month: %1")
                             .arg(Workspace.revision >= 0 ? root.periodicPath("month") : "")
                ConfigField {
                    objectName: "monthlyFileFormatField"
                    key: "monthlyFileFormat"
                    invalidText: qsTr("The format needs a month and a year, and no day.")
                }
            }
            ShellSettingRow {
                title: qsTr("Monthly note template")
                description: qsTr("New monthly notes start with this page: {{date}} is the 1st, {{end}} the last day, {{month}} the month's name.")
                ConfigField { key: "monthlyTemplate"; leadingIcon: "file-text" }
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
                    onClicked: defaultReminderTimeDialog.openFor(defaultReminderTimeField.configText !== ""
                                                                ? defaultReminderTimeField.configText : "09:00")

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
                    text: Workspace.config && Workspace.revision >= 0 ? root.configWarnings().join("\n") : ""
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: extensionsSection
        ColumnLayout {
            spacing: Theme.space8

            SettingsGroup {
                heading: qsTr("Extensions")
                note: qsTr("Extensions add views, commands and reactions to Brulion. They read and write only the open notebook, in plain Markdown that other apps read too.")

                SettingSwitch {
                    objectName: "gymSetting"
                    text: qsTr("Gym")
                    description: qsTr("Workouts written in the journal, plans that add weight when you are ready, a rest timer and the progress of every exercise (Gym in the sidebar).")
                    iconName: "dumbbell"
                    checked: AppSettings.gymEnabled
                    onToggled: AppSettings.gymEnabled = checked
                }
                // The gym's code as a page of the notebook (TiddlyWiki's shadow tiddlers): edited
                // there, it runs instead of the built-in one.
                ShellSettingRow {
                    id: gymSource
                    objectName: "gymSourceSetting"
                    visible: AppSettings.gymEnabled
                    readonly property string page: Workspace.extensions.revision >= 0 ? Workspace.extensions.overridePage("silownia") : ""
                    title: qsTr("The gym's code")
                    description: gymSource.page !== ""
                                 ? qsTr("The gym runs from its page in this notebook (%1): change the code there. Restore puts the page in the trash, and the built-in code runs again.").arg(Workspace.pageTitle(gymSource.page))
                                 : qsTr("Built into Brulion. Edit it as a page of this notebook: the page then runs instead.")
                    BButton {
                        size: "small"
                        iconName: "code"
                        text: gymSource.page !== "" ? qsTr("Open") : qsTr("Edit the code")
                        onClicked: {
                            const path = gymSource.page !== "" ? gymSource.page : Workspace.extensions.editBuiltinSource("silownia")
                            if (path !== "")
                                AppController.openPage(path)
                        }
                    }
                    BButton {
                        visible: gymSource.page !== ""
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Restore")
                        onClicked: {
                            if (Workspace.extensions.restoreBuiltin("silownia"))
                                AppController.toast(qsTr("The built-in gym runs again; its page is in the trash."), "success")
                        }
                    }
                }
            }

            // AI for extensions (b.ai) on this device: the provider, the key, the model.
            SettingsGroup {
                objectName: "aiSettings"
                heading: qsTr("AI")
                note: qsTr("Extensions with the “ai” permission can ask a model about your notes — also on the phone. Claude and ChatGPT subscriptions cannot be used by other apps: an API key is billed separately, by use (a few notes cost fractions of a cent). A server of your own (Ollama, LM Studio) needs no key. What an extension sends goes to that provider; the key stays on this device.")

                SettingSwitch {
                    objectName: "assistantSetting"
                    text: qsTr("AI assistant")
                    description: qsTr("The ✨ button on the top bar (and “AI: …” in the palette): ask about your notes — the AI searches and reads the whole notebook — or ask it to change them, each change with your yes; or sum up the page on screen. The answer goes into today's note.")
                    iconName: "sparkles"
                    checked: AppSettings.assistantEnabled
                    onToggled: AppSettings.assistantEnabled = checked
                }
                ShellSettingRow {
                    title: qsTr("Provider")
                    description: AppSettings.aiProvider === "openai"
                                 ? qsTr("Any API that speaks OpenAI's chat format: OpenAI, OpenRouter, or Ollama and LM Studio on your own computer.")
                                 : qsTr("Claude by Anthropic: a key from platform.claude.com (Settings → API keys).")
                    BSegmented {
                        objectName: "aiProviderSetting"
                        accessibleName: qsTr("Provider")
                        model: [{ value: "anthropic", text: "Claude" }, { value: "openai", text: qsTr("OpenAI-compatible") }]
                        currentValue: AppSettings.aiProvider
                        onActivated: (value) => AppSettings.aiProvider = "" + value
                    }
                }
                ShellSettingRow {
                    id: aiKeyRow
                    title: qsTr("API key")
                    description: AppSettings.hasAiKey ? qsTr("Saved on this device. Type a new one to replace it.")
                                                      : qsTr("Not set. It is kept only on this device, never in the notebook.")
                    BTextField {
                        id: aiKeyField
                        objectName: "aiKeySetting"
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        echoMode: TextInput.Password
                        placeholderText: AppSettings.hasAiKey ? "••••••••" : (AppSettings.aiProvider === "openai" ? "sk-…" : "sk-ant-…")
                        accessibleName: qsTr("API key")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                        onEditingFinished: {
                            if (aiKeyField.text !== "") {
                                if (AppSettings.setAiKey(aiKeyField.text))
                                    AppController.toast(qsTr("The key is saved on this device."), "success")
                                aiKeyField.text = ""
                            }
                        }
                    }
                    BButton {
                        visible: AppSettings.hasAiKey
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Forget")
                        onClicked: AppSettings.setAiKey("")
                    }
                }
                ShellSettingRow {
                    title: qsTr("Model")
                    description: AppSettings.aiProvider === "openai" ? qsTr("Its name at the provider, e.g. the model you pulled into Ollama.")
                                                                     : qsTr("Empty: claude-opus-5-5. Cheaper and faster: claude-haiku-5-5.")
                    BTextField {
                        objectName: "aiModelSetting"
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        text: AppSettings.aiModel
                        placeholderText: AppSettings.aiProvider === "openai" ? "llama3.2" : "claude-opus-5-5"
                        accessibleName: qsTr("Model")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        onEditingFinished: AppSettings.aiModel = text
                    }
                }
                ShellSettingRow {
                    title: qsTr("Address")
                    description: qsTr("Empty: the provider's own. A server of your own: e.g. http://192.168.1.10:11434/v1 (Ollama).")
                    BTextField {
                        objectName: "aiAddressSetting"
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        text: AppSettings.aiBaseUrl
                        placeholderText: AppSettings.aiProvider === "openai" ? "https://api.openai.com/v1" : "https://api.anthropic.com"
                        accessibleName: qsTr("Address")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                        onEditingFinished: AppSettings.aiBaseUrl = text
                    }
                }
                ShellSettingRow {
                    title: qsTr("Check")
                    description: qsTr("Asks the model “Say OK.” with these settings.")
                    BButton {
                        objectName: "aiTest"
                        size: "small"
                        iconName: "sparkles"
                        busy: Workspace.extensions.aiPending > 0
                        text: qsTr("Check")
                        onClicked: Workspace.extensions.testAi()
                    }
                }
            }

            SettingsGroup {
                heading: qsTr("In this notebook")
                note: qsTr("Your own extensions are pages of the notebook tagged #rozszerzenie (or #extension): the code in their luau code blocks runs, and runs again whenever you save the page. Code that arrives from another device, a sync or an import runs here only after you approve it.")

                ShellSettingRow {
                    objectName: "extensionsTools"
                    title: qsTr("Write an extension")
                    description: qsTr("A new page with an example: a command, a view and a reaction. The Luau console evaluates code and shows what extensions print.")
                    BButton {
                        objectName: "newExtension"
                        size: "small"
                        iconName: "plus"
                        text: qsTr("New extension")
                        onClicked: {
                            const path = Workspace.extensions.createExtension()
                            if (path !== "")
                                AppController.openPage(path)
                        }
                    }
                    BButton {
                        size: "small"
                        variant: "ghost"
                        iconName: "terminal"
                        text: qsTr("Console")
                        onClicked: AppController.navigate({ view: "console" })
                    }
                }

                Repeater {
                    model: Workspace.extensions.list
                    delegate: ShellSettingRow {
                        id: extRow
                        required property string extId
                        required property string extTitle
                        required property string extKind
                        required property string extState
                        required property string extMessage
                        required property string extPath
                        required property string extPermissions
                        required property string extDescription
                        required property int extCommands
                        required property int extViews
                        required property int extHooks
                        required property int extErrorLine
                        objectName: "extensionRow"
                        visible: extRow.extKind === "page"
                        Layout.fillWidth: true
                        title: extRow.extTitle
                        description: root.extensionStateText(extRow.extState, extRow.extMessage, extRow.extErrorLine,
                                                             extRow.extCommands, extRow.extViews, extRow.extHooks)
                                     + (extRow.extPermissions !== "" ? "\n" + qsTr("May: %1").arg(extRow.extPermissions) : "")
                                     + (extRow.extDescription !== "" ? "\n" + extRow.extDescription : "")
                        BButton {
                            size: "small"
                            variant: "ghost"
                            iconName: "code"
                            text: extRow.extState === "error" && extRow.extErrorLine >= 0 ? qsTr("Line %1").arg(extRow.extErrorLine + 1)
                                                                                         : qsTr("Code")
                            onClicked: AppController.openPage(extRow.extPath, extRow.extState === "error" ? extRow.extErrorLine : -1)
                        }
                        BButton {
                            objectName: "approveExtension"
                            visible: extRow.extState === "approval"
                            size: "small"
                            variant: "primary"
                            iconName: "shield-check"
                            text: qsTr("Run")
                            onClicked: Workspace.extensions.approve(extRow.extPath)
                        }
                        BButton {
                            visible: extRow.extState === "running" || extRow.extState === "error" || extRow.extState === "off"
                            size: "small"
                            text: extRow.extState === "off" ? qsTr("Switch on") : qsTr("Switch off")
                            onClicked: Workspace.extensions.setPageOn(extRow.extPath, extRow.extState === "off")
                        }
                    }
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

            SettingSwitch {
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
                    model: [{ value: 5, text: qsTr("%1 min").arg(5) }, { value: 10, text: qsTr("%1 min").arg(10) },
                            { value: 15, text: qsTr("%1 min").arg(15) }, { value: 30, text: qsTr("%1 min").arg(30) }]
                    currentValue: AppSettings.snoozeMinutes
                    onActivated: (value) => AppSettings.snoozeMinutes = value
                }
            }
            ShellSettingRow {
                title: qsTr("Alarms ring for")
                description: qsTr("An alarm stops by itself after this time.")
                BSegmented {
                    accessibleName: qsTr("Alarms ring for")
                    model: [{ value: 1, text: qsTr("%1 min").arg(1) }, { value: 5, text: qsTr("%1 min").arg(5) },
                            { value: 10, text: qsTr("%1 min").arg(10) }, { value: 15, text: qsTr("%1 min").arg(15) }]
                    currentValue: AppSettings.alarmRingMinutes
                    onActivated: (value) => AppSettings.alarmRingMinutes = value
                }
            }
            SettingSwitch {
                text: qsTr("Show missed reminders")
                description: qsTr("Reminders that were due while the device was off appear when Brulion starts.")
                checked: AppSettings.notifyMissed
                onToggled: AppSettings.notifyMissed = checked
            }
            // Android: the periodic background check of the notes (docs/ANDROID.md "Checking notes
            // in the background") — reminders added on another device ring without opening Brulion.
            ShellSettingRow {
                id: backgroundCheckRow
                objectName: "backgroundCheckRow"
                visible: AppController.isAndroid
                property int minutes: AppSettings.value("reminders/backgroundCheckMinutes", 30)
                // The last check (Workspace.reminders.backgroundCheckStatus(): { when, ok, sync,
                // reminders }), read when shown.
                property var last: ({})
                onVisibleChanged: if (visible) last = Workspace.reminders.backgroundCheckStatus()
                Component.onCompleted: if (visible) last = Workspace.reminders.backgroundCheckStatus()
                title: qsTr("Check notes in the background")
                description: {
                    const what = qsTr("While Brulion is closed, Android reads the notebook now and then (after a Nextcloud sync, if set up), so reminders added on another device ring too. Android may postpone it while the phone sleeps.")
                    const when = AppController.routeText(last, "when")
                    if (minutes <= 0 || when === "")
                        return what
                    const count = AppController.routeInt(last, "reminders", 0)
                    let done = ""
                    if (!AppController.routeFlag(last, "ok"))
                        done = qsTr("Last check %1: the notebook could not be read.").arg(when)
                    else if (AppController.routeText(last, "sync") === "failed")
                        done = qsTr("Last check %1: %n reminder(s), the sync failed.", "", count).arg(when)
                    else
                        done = qsTr("Last check %1: %n reminder(s).", "", count).arg(when)
                    return what + "\n" + done
                }
                BSegmented {
                    objectName: "backgroundCheckInterval"
                    accessibleName: qsTr("Check notes in the background")
                    model: [
                        { value: 0, text: qsTr("Off") },
                        { value: 15, text: qsTr("%1 min").arg(15) },
                        { value: 30, text: qsTr("%1 min").arg(30) },
                        { value: 60, text: qsTr("1 h") }
                    ]
                    currentValue: backgroundCheckRow.minutes
                    enabled: AppSettings.remindersEnabled
                    onActivated: (value) => {
                        AppSettings.setValue("reminders/backgroundCheckMinutes", value)
                        backgroundCheckRow.minutes = value
                    }
                }
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
        id: backupsSection
        ShellBackupSettings {}
    }

    Component {
        id: shortcutsSection
        ShellShortcutSettings {}
    }

    Component {
        id: appSection
        ColumnLayout {
            spacing: Theme.space8

            SettingsGroup {
                heading: qsTr("Application")

                SettingSwitch {
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
                // tray calendar (ShellTrayCalendar; the default).
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
                SettingSwitch {
                    objectName: "settingsStartAtLogin"
                    visible: AppController.autostartAvailability !== "unsupported"
                    enabled: AppController.autostartAvailability === "available"
                    text: qsTr("Start when you sign in")
                    description: AppController.autostartAvailability === "profile"
                                 ? qsTr("Not in a separate profile (BRULION_PROFILE): Windows starts the default one.")
                                 : AppController.trayAvailable
                                   ? qsTr("Brulion starts hidden in the tray, so reminders ring from the moment you sign in.")
                                   : qsTr("Brulion starts after you sign in.")
                    iconName: "power"
                    Binding on checked { value: AppController.startAtLogin }
                    onToggled: {
                        AppController.startAtLogin = checked
                        // Refused: show the real state again.
                        checked = AppController.startAtLogin
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
                SettingSwitch {
                    objectName: "settingsDetailedLog"
                    text: qsTr("Detailed log")
                    description: qsTr("Also writes every request to the server (never note contents or passwords). Turn it on when syncing gives trouble.")
                    iconName: "scroll-text"
                    checked: Logs.detailed
                    onToggled: Logs.detailed = checked
                }
                SettingSwitch {
                    objectName: "settingsCheckUpdates"
                    visible: AppController.updates.supported
                    text: qsTr("Check for new versions")
                    description: qsTr("Once a day Brulion asks GitHub for the latest release and shows a message when there is a newer one. GitHub sees this device's address; nothing about your notes is sent.")
                    iconName: "download"
                    checked: AppController.updates.enabled
                    onToggled: AppController.updates.enabled = checked
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
                    description: qsTr("Free software: GNU Affero General Public License 3.0 or later (AGPL-3.0). Icons: Lucide (ISC). Fonts: Inter, Literata, JetBrains Mono; emoji and symbols: Noto Emoji, Noto Sans Math (OFL 1.1).")
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

                SettingSwitch {
                    objectName: "settingsRemoteControl"
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
