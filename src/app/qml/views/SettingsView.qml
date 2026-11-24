pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SettingsView — route "settings" ({section?}). Sections: General (language, "Run the intro
// again": AppController.restartIntro()), Appearance (theme,
// accent colour with presets and BAccentDialog, light and dark backgrounds, text size, interface
// font, density, reduce motion, back to the default look after a confirmation —
// AppSettings.resetAppearance(), Theme inputs; Themes: the notebook's theme pages —
// qml/shell/ShellThemePages, ThemePages; Layout: sidebar entries and sections, the bottom
// navigation's buttons, the quick note button),
// Editor (font — serif, sans or any installed family —, code font, size, line spacing, width,
// source mode, spell checking — on/off, in the whole page, its languages, the personal dictionary's dialog, shown only
// where SpellCheck.available —, word count, focus mode, typewriter scrolling, Vim keys; font pickers:
// BFontPickerDialog, not in the browser), Keyboard shortcuts (qml/shell/ShellShortcutSettings: the
// commands' shortcuts), Notebook (the vault's shared settings in
// .brulion/config.json: the note modes of new pages and of the journal, journal folder and file
// names, attachments, templates, default reminder time, first day of the week), Search
// (qml/attachments/AttachmentSettings: PDFs and pictures in the search, OCR and its languages, the
// device's cache; qml/shell/ShellSemanticSettings: search by meaning and its model), Reminders (on/off, permissions on Android and in the
// browser, the sounds of reminders and alarms — qml/notify/ReminderSoundSetting —, snooze, alarm
// length, Android's background check of the notes, test notification), Sync (qml/io/SyncSettings: status,
// the sync center, the Nextcloud account), Backups (qml/shell/ShellBackupSettings), Privacy
// (qml/shell/ShellPrivacySettings: the app lock, the encrypted notes' session), Automation
// (qml/shell/ShellAutomationSettings: brulion:// links, the notebook's key), Application (close to tray, the event log and its
// detailed mode, version, licence, source code; on the desktop "For developers": the switch
// that lets AI agents drive the running app, AppController.remoteControl).
//
// One section at a time: wide windows list the sections on the left, choosing one replaces the
// route's section (no history entry). Narrow ones (phones) start on a list of the sections in
// groups drawn as the page's menu sheet — the notebook's row on top, each section a row with its
// line icon, a thin line between groups; a row opens its section as a step of the history, so
// Back returns to the list.
Item {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Settings")
    property string section: AppController.routeText(route, "section") !== "" ? AppController.routeText(route, "section") : "general"
    /// Narrow windows without a section in the route: the list of the sections.
    readonly property bool home: Theme.narrow && AppController.routeText(route, "section") === ""
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
    /// From the list of sections (narrow windows): a step of the history.
    function enterSection(key: string): void {
        AppController.navigate({ view: "settings", section: key })
    }
    /// The list's groups of sections (narrow windows): their sections' keys (comma-separated, in
    /// order). Keyboard shortcuts only with a keyboard (not on touch UIs).
    readonly property list<string> homeGroupKeys: ["appearance,editor", "reminders,search,extensions", "sync,backups,privacy",
                                                  Theme.touchUi ? "general,automation,app" : "general,shortcuts,automation,app"]
    /// Section `i`'s `field` ("" for none).
    function sectionField(i: int, field: string): string {
        return i >= 0 && i < sections.length ? AppController.routeText(sections[i], field) : ""
    }
    /// A section's title by its key.
    function sectionTitle(key: string): string {
        return root.sectionField(root.indexOfSection(key), "title")
    }
    /// A section's icon by its key.
    function sectionIcon(key: string): string {
        return root.sectionField(root.indexOfSection(key), "iconName")
    }
    /// The sections ({ key, title, iconName }).
    readonly property list<var> sections: [
        { key: "general", title: qsTr("General"), iconName: "settings" },
        { key: "appearance", title: qsTr("Appearance"), iconName: "palette" },
        { key: "editor", title: qsTr("Editor"), iconName: "pencil" },
        { key: "shortcuts", title: qsTr("Keyboard shortcuts"), iconName: "keyboard" },
        { key: "vault", title: qsTr("Notebook"), iconName: "notebook-pen" },
        { key: "search", title: qsTr("Search"), iconName: "search" },
        { key: "reminders", title: qsTr("Reminders"), iconName: "bell" },
        { key: "extensions", title: qsTr("Extensions"), iconName: "puzzle" },
        { key: "sync", title: qsTr("Sync"), iconName: "folder-sync" },
        { key: "backups", title: qsTr("Backups"), iconName: "hard-drive" },
        { key: "privacy", title: qsTr("Privacy"), iconName: "shield-check" },
        { key: "automation", title: qsTr("Automation"), iconName: "zap" },
        { key: "app", title: qsTr("Application"), iconName: "info" }
    ]

    function sectionComponent(id: string): Component {
        switch (id) {
        case "appearance": return appearanceSection
        case "editor": return editorSection
        case "shortcuts": return shortcutsSection
        case "vault": return vaultSection
        case "search": return searchSection
        case "reminders": return remindersSection
        case "extensions": return extensionsSection
        case "sync": return syncSection
        case "backups": return backupsSection
        case "privacy": return privacySection
        case "automation": return automationSection
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
        { key: "flashcards", title: qsTr("Flashcards") },
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
        { value: "flashcards", text: qsTr("Flashcards"), iconName: "layers" },
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
        case "live": return qsTr("A live version runs instead (sent by an agent).")
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

    /// A text field that saves one key of the notebook's flashcard settings (config.json
    /// "flashcards") when editing finishes; its text follows the config, as ConfigField's.
    component FlashcardsField: BTextField {
        id: fcField
        required property string key
        readonly property string configText: AppController.routeText(Workspace.config.flashcards, key)
        Layout.preferredWidth: Theme.compact ? -1 : 160
        Layout.fillWidth: Theme.compact
        enabled: Workspace.isOpen
        Binding on text { value: fcField.configText }
        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        onEditingFinished: {
            if (text !== configText)
                errorText = Workspace.setConfig({ flashcards: Js.withKey({}, key, text) }) ? "" : qsTr("This value cannot be used.")
        }
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

    /// A built-in extension's code as a page of the notebook (TiddlyWiki's shadow tiddlers): "Edit
    /// the code" writes it into a page, which then runs instead of the built-in code; "Restore"
    /// puts that page in the trash. `pageText` says so with the page's title (%1), `restoredText`
    /// after a restore.
    component BuiltinSourceRow: ShellSettingRow {
        id: sourceRow
        required property string builtin
        property string pageText
        property string restoredText
        readonly property string page: Workspace.extensions.revision >= 0 ? Workspace.extensions.overridePage(sourceRow.builtin) : ""
        description: sourceRow.page !== ""
                     ? sourceRow.pageText.arg(Workspace.pageTitle(sourceRow.page))
                     : qsTr("Built into Brulion. Edit it as a page of this notebook: the page then runs instead.")
        BButton {
            size: "small"
            iconName: "code"
            text: sourceRow.page !== "" ? qsTr("Open") : qsTr("Edit the code")
            onClicked: {
                const path = sourceRow.page !== "" ? sourceRow.page : Workspace.extensions.editBuiltinSource(sourceRow.builtin)
                if (path !== "")
                    AppController.openPage(path)
            }
        }
        BButton {
            visible: sourceRow.page !== ""
            size: "small"
            variant: "ghost"
            text: qsTr("Restore")
            onClicked: {
                if (Workspace.extensions.restoreBuiltin(sourceRow.builtin))
                    AppController.toast(sourceRow.restoredText, "success")
            }
        }
        // Its own settings (b.settings), when it declares some.
        BButton {
            objectName: "builtinSettings"
            visible: Workspace.extensions.revision >= 0 && Workspace.extensions.settingsCount(sourceRow.builtin) > 0
            size: "small"
            variant: "ghost"
            iconName: "settings"
            text: qsTr("Settings")
            onClicked: AppController.requestUiAction("extensionSettings:" + sourceRow.builtin)
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

                    // The chosen section (or, on narrow windows, the list of them), from its top.
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: root.home ? settingsHome : root.sectionComponent(root.section)
                        onLoaded: flick.contentY = 0
                    }
                }
            }
        }
    }

    // The list of sections (narrow windows), drawn as the page's menu sheet (BMenu.sheet, its
    // BMenuItems): plain rows with a line icon, the notebook's row first, the groups apart by
    // a thin line — no cards, no coloured tiles.
    component HomeRow: T.AbstractButton {
        id: homeRow
        /// The row's section (its objectName), title and icon.
        property string key
        property string label
        property string iconName
        /// A muted text before the arrow ("" = none).
        property string hint
        objectName: "settingsHomeRow-" + key
        Layout.fillWidth: true
        implicitHeight: Math.max(homeRowContent.implicitHeight + topPadding + bottomPadding,
                                 Theme.touchUi ? Theme.touchTarget : 36)
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.Button
        Accessible.name: label
        Accessible.description: hint
        background: Rectangle {
            radius: Theme.radius
            color: homeRow.pressed ? Theme.bgPressed : homeRow.hovered ? Theme.bgHover : "transparent"
        }
        contentItem: RowLayout {
            id: homeRowContent
            spacing: 10
            BIcon {
                name: homeRow.iconName
                size: Theme.iconSize
                color: Theme.fgMuted
            }
            BText {
                Layout.fillWidth: true
                text: homeRow.label
                font.pixelSize: Theme.textControl
                elide: Text.ElideRight
            }
            BText {
                visible: homeRow.hint !== ""
                Layout.maximumWidth: homeRow.width / 2
                text: homeRow.hint
                variant: "muted"
                font.pixelSize: Theme.textSm
                elide: Text.ElideRight
            }
            BIcon {
                name: "chevron-right"
                size: Theme.iconSizeSm
                color: Theme.fgMuted
            }
        }
    }
    Component {
        id: settingsHome
        ColumnLayout {
            objectName: "settingsHome"
            spacing: 0

            // The notebook's own settings, its name beside.
            HomeRow {
                key: "vault"
                label: root.sectionTitle("vault")
                iconName: root.sectionIcon("vault")
                hint: Workspace.name
                onClicked: root.enterSection("vault")
            }

            Repeater {
                model: root.homeGroupKeys.length
                delegate: ColumnLayout {
                    id: homeGroup
                    required property int index
                    readonly property list<string> keys: Js.split(root.homeGroupKeys[homeGroup.index], ",")
                    Layout.fillWidth: true
                    spacing: 0
                    // The line between groups (as BMenuSeparator).
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        implicitHeight: 1
                        color: Theme.border
                    }
                    Repeater {
                        model: homeGroup.keys
                        delegate: HomeRow {
                            id: homeSectionRow
                            required property string modelData
                            key: modelData
                            label: root.sectionTitle(modelData)
                            iconName: root.sectionIcon(modelData)
                            onClicked: root.enterSection(homeSectionRow.modelData)
                        }
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
                    model: [{ value: "en", text: "English" }, { value: "pl", text: "Polski" }]
                    currentValue: AppSettings.language
                    onActivated: (value) => AppSettings.language = value
                }
            }
            ShellSettingRow {
                title: qsTr("Intro")
                description: qsTr("Language, look and permissions, step by step. The notebook closes until the end.")
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
                            { value: "graphite", text: qsTr("Graphite", "dark background") },
                            { value: "ink", text: qsTr("Ink", "dark background") },
                            { value: "slate", text: qsTr("Blue-grey") },
                            { value: "black", text: qsTr("Black", "background") }
                        ]
                        currentValue: Theme.darkSurface
                        onActivated: (value) => AppSettings.darkSurface = value
                    }
                }
                ShellSettingRow {
                    // Desktop windows only (AppController.interfaceScaleSupported, ui/uiscale.h):
                    // the whole window larger or smaller; phones keep their sizes.
                    visible: AppController.interfaceScaleSupported
                    title: qsTr("Interface size")
                    description: qsTr("Text, buttons and icons together. Also Ctrl+Alt+= and Ctrl+Alt+-.")
                    BSegmented {
                        objectName: "interfaceScaleSetting"
                        accessibleName: qsTr("Interface size")
                        model: [
                            { value: 0, text: qsTr("Auto · %1 %").arg(Math.round(AppController.autoInterfaceScale * 100)) },
                            { value: 0.9, text: qsTr("%1 %").arg(90) },
                            { value: 1.0, text: qsTr("%1 %").arg(100) },
                            { value: 1.15, text: qsTr("%1 %").arg(115) },
                            { value: 1.25, text: qsTr("%1 %").arg(125) },
                            { value: 1.5, text: qsTr("%1 %").arg(150) },
                            { value: 1.75, text: qsTr("%1 %").arg(175) }
                        ]
                        currentValue: AppSettings.interfaceScale
                        onActivated: (value) => AppSettings.interfaceScale = value
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
            // The notebook's theme pages (qml/shell/ShellThemePages: ThemePages).
            ShellThemePages {}

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
                    objectName: "railExpandsSetting"
                    visible: !Theme.touchUi
                    text: qsTr("Names beside the views' icons")
                    description: qsTr("The rail of views at the window's edge widens with their names when the pointer rests on it. Off: the names show in tooltips.")
                    checked: AppSettings.railExpands
                    onToggled: AppSettings.railExpands = checked
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
                description: qsTr("Each change is written with its time, and “In progress” runs the task's timer.")
                checked: AppSettings.taskStatusHistory
                onToggled: AppSettings.taskStatusHistory = checked
            }
            // Spell checking (docs/UI.md "Spell checking"): only where this device can check.
            SettingSwitch {
                objectName: "settingsSpellCheck"
                visible: SpellCheck.available
                text: qsTr("Check spelling")
                description: Theme.touchUi
                             ? qsTr("Misspelled words of the block you are editing and of the Markdown source are underlined; tap one for corrections.")
                             : qsTr("Misspelled words of the block you are editing and of the Markdown source are underlined; right-click one for corrections.")
                checked: AppSettings.spellCheck
                onToggled: AppSettings.spellCheck = checked
            }
            SettingSwitch {
                objectName: "settingsSpellWholePage"
                visible: SpellCheck.available && AppSettings.spellCheck
                text: qsTr("Underline in the whole page")
                description: Theme.touchUi
                             ? qsTr("Also in the blocks you are not editing.")
                             : qsTr("Also in the blocks you are not editing; right-click a word there for corrections.")
                checked: AppSettings.spellWholePage
                onToggled: AppSettings.spellWholePage = checked
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
                description: qsTr("Pages open for reading: a tap follows links and never starts editing. %1 switches it.")
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
                objectName: "settingsVimMode"
                text: qsTr("Vim keys")
                description: qsTr("Vim's normal, insert and visual modes, for a hardware keyboard. On this device only.")
                checked: AppSettings.vimMode
                onToggled: AppSettings.vimMode = checked
            }
            SettingSwitch {
                objectName: "settingsWebPictures"
                text: qsTr("Show pictures from the internet without asking")
                description: qsTr("Off: a picture from a web address waits for “Show”, so no site learns that you opened the note.")
                checked: AppSettings.webPicturesShown
                onToggled: AppSettings.webPicturesShown = checked
            }
            SettingSwitch {
                objectName: "settingsPictureFrames"
                text: qsTr("Frame around pictures")
                description: qsTr("A thin line around each picture in a note. Off: pictures sit right on the page.")
                checked: AppSettings.pictureFrames
                onToggled: AppSettings.pictureFrames = checked
            }
            SettingSwitch {
                objectName: "settingsLightPictureBacking"
                text: qsTr("Light background under pictures in a dark theme")
                description: qsTr("Transparent pictures with dark lines (diagrams, scans) stay readable. Photos look the same.")
                checked: AppSettings.lightPictureBacking
                onToggled: AppSettings.lightPictureBacking = checked
            }
            SettingSwitch {
                objectName: "settingsCodeLineNumbers"
                text: qsTr("Line numbers in code blocks")
                description: qsTr("Each line of a code block numbered on its left. The numbers are not part of the text: copying the code leaves them out.")
                checked: AppSettings.codeLineNumbers
                onToggled: AppSettings.codeLineNumbers = checked
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
            note: qsTr("These settings travel with the notebook to all your devices.")

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
                description: qsTr("On this device only. The files of a linked folder are listed, synced and editable here.")
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
                    objectName: "habitsSetting"
                    text: qsTr("Habits")
                    description: qsTr("Daily points ticked in the journal, with streaks and a month grid.")
                    iconName: "flame"
                    checked: AppSettings.habitsEnabled
                    onToggled: AppSettings.habitsEnabled = checked
                }
                BuiltinSourceRow {
                    objectName: "habitsSourceSetting"
                    visible: AppSettings.habitsEnabled
                    builtin: "nawyki"
                    title: qsTr("The habits' code")
                    pageText: qsTr("Habits run from their page in this notebook (%1): change the code there. Restore puts the page in the trash, and the built-in code runs again.")
                    restoredText: qsTr("The built-in habits run again; their page is in the trash.")
                }
                SettingSwitch {
                    objectName: "gymSetting"
                    text: qsTr("Gym")
                    description: qsTr("Workouts in the journal, plans that add weight, a rest timer and your progress.")
                    iconName: "dumbbell"
                    checked: AppSettings.gymEnabled
                    onToggled: AppSettings.gymEnabled = checked
                }
                BuiltinSourceRow {
                    objectName: "gymSourceSetting"
                    visible: AppSettings.gymEnabled
                    builtin: "silownia"
                    title: qsTr("The gym's code")
                    pageText: qsTr("The gym runs from its page in this notebook (%1): change the code there. Restore puts the page in the trash, and the built-in code runs again.")
                    restoredText: qsTr("The built-in gym runs again; its page is in the trash.")
                }
                SettingSwitch {
                    objectName: "flashcardsSetting"
                    text: qsTr("Flashcards")
                    description: qsTr("Cards written in your notes (“Question::Answer”, clozes, #card), shown again just before you would forget them.")
                    iconName: "layers"
                    checked: AppSettings.flashcardsEnabled
                    onToggled: AppSettings.flashcardsEnabled = checked
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("New cards a day")
                    description: qsTr("How many cards never studied a session brings each day (at the end of a session you can learn more).")
                    BSegmented {
                        objectName: "flashcardsNewSetting"
                        accessibleName: qsTr("New cards a day")
                        model: [{ value: 5, text: "5" }, { value: 10, text: "10" }, { value: 20, text: "20" },
                                { value: 30, text: "30" }, { value: 50, text: "50" }]
                        currentValue: AppSettings.flashcardsNewPerDay
                        onActivated: (value) => AppSettings.flashcardsNewPerDay = value
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("Desired retention")
                    description: qsTr("The chance of remembering a card when it comes back: higher means more reviews.")
                    BSegmented {
                        objectName: "flashcardsRetentionSetting"
                        accessibleName: qsTr("Desired retention")
                        model: [{ value: 80, text: "80 %" }, { value: 85, text: "85 %" }, { value: 90, text: "90 %" },
                                { value: 95, text: "95 %" }]
                        currentValue: AppSettings.flashcardsRetention
                        onActivated: (value) => AppSettings.flashcardsRetention = value
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("Tags of decks")
                    description: qsTr("A note with one of these tags (or a tag under one, #flashcards/spanish) is a deck. Kept in this notebook; one without tags uses this device's.")
                    BTextField {
                        id: flashcardsTagsField
                        objectName: "flashcardsTagsSetting"
                        readonly property string notebookTags: AppController.routeText(Workspace.config.flashcards, "tags")
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        Binding on text { value: flashcardsTagsField.notebookTags !== "" ? flashcardsTagsField.notebookTags : AppSettings.flashcardsTags }
                        placeholderText: "flashcards, fiszki"
                        accessibleName: qsTr("Tags of decks")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        onEditingFinished: {
                            if (Workspace.isOpen)
                                errorText = Workspace.setConfig({ flashcards: { tags: text } }) ? "" : qsTr("This value cannot be used.")
                            else
                                AppSettings.flashcardsTags = text
                        }
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("Card separators")
                    description: qsTr("Single-line cards: the question, the separator, the answer; the longer one asks both ways. Kept in this notebook. Empty switches one off.")
                    ColumnLayout {
                        spacing: Theme.space2
                        FlashcardsField {
                            objectName: "flashcardsSingleSetting"
                            key: "singleLineSeparator"
                            placeholderText: "::"
                            accessibleName: qsTr("Single-line separator")
                        }
                        FlashcardsField {
                            objectName: "flashcardsSingleReversedSetting"
                            key: "singleLineReversedSeparator"
                            placeholderText: ":::"
                            accessibleName: qsTr("Single-line separator, both ways")
                        }
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("Multi-line separators")
                    description: qsTr("A line that is only this text splits a multi-line card into the question above and the answer below; the second one asks both ways. Empty switches one off.")
                    ColumnLayout {
                        spacing: Theme.space2
                        FlashcardsField {
                            objectName: "flashcardsMultiSetting"
                            key: "multilineSeparator"
                            placeholderText: "?"
                            accessibleName: qsTr("Multi-line separator")
                        }
                        FlashcardsField {
                            objectName: "flashcardsMultiReversedSetting"
                            key: "multilineReversedSeparator"
                            placeholderText: "??"
                            accessibleName: qsTr("Multi-line separator, both ways")
                        }
                    }
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("End of a multi-line card")
                    description: qsTr("A line that is only this text ends a multi-line card or a cloze card, so that it can hold blank lines (for example ---). Empty: a blank line ends it.")
                    FlashcardsField {
                        objectName: "flashcardsEndMarkerSetting"
                        key: "multilineEndMarker"
                        accessibleName: qsTr("End of a multi-line card")
                    }
                }
                SettingSwitch {
                    objectName: "flashcardsHighlightsSetting"
                    visible: AppSettings.flashcardsEnabled
                    text: qsTr("Clozes from ==highlights==")
                    description: qsTr("A line with ==an answer== (or ==1;;an answer;;a hint==) is a cloze card. Kept in this notebook.")
                    checked: AppController.routeFlag(Workspace.config.flashcards, "clozeHighlights")
                    enabled: Workspace.isOpen
                    onToggled: Workspace.setConfig({ flashcards: { clozeHighlights: checked } })
                }
                SettingSwitch {
                    objectName: "flashcardsBoldSetting"
                    visible: AppSettings.flashcardsEnabled
                    text: qsTr("Clozes from **bold text**")
                    description: qsTr("A line with **an answer** (or **1;;an answer;;a hint**) is a cloze card. Kept in this notebook.")
                    checked: AppController.routeFlag(Workspace.config.flashcards, "clozeBold")
                    enabled: Workspace.isOpen
                    onToggled: Workspace.setConfig({ flashcards: { clozeBold: checked } })
                }
                SettingSwitch {
                    objectName: "flashcardsCurlySetting"
                    visible: AppSettings.flashcardsEnabled
                    text: qsTr("Clozes from {{curly brackets}}")
                    description: qsTr("A line with {{an answer}} (or {{1;;an answer;;a hint}}) is a cloze card. Anki's {{c1::answer::hint}} always is. Kept in this notebook.")
                    checked: AppController.routeFlag(Workspace.config.flashcards, "clozeCurly")
                    enabled: Workspace.isOpen
                    onToggled: Workspace.setConfig({ flashcards: { clozeCurly: checked } })
                }
                SettingSwitch {
                    objectName: "flashcardsFoldersSetting"
                    visible: AppSettings.flashcardsEnabled
                    text: qsTr("Folders as decks")
                    description: qsTr("Every note in a folder is read for cards, tagged or not, and its cards are in the deck of its folder (sub-folders are sub-decks), whatever their tags say. Notes outside any folder keep their tags. Kept in this notebook.")
                    checked: AppController.routeFlag(Workspace.config.flashcards, "foldersAsDecks")
                    enabled: Workspace.isOpen
                    onToggled: Workspace.setConfig({ flashcards: { foldersAsDecks: checked } })
                }
                ShellSettingRow {
                    visible: AppSettings.flashcardsEnabled
                    title: qsTr("Tags of notes to review")
                    description: qsTr("A note with one of these tags (or a tag under one) is reviewed as a whole: its state goes into its header (sr-due, sr-interval, sr-ease), as in the Spaced Repetition plugin. Kept in this notebook; empty: #review.")
                    FlashcardsField {
                        objectName: "flashcardsReviewTagsSetting"
                        key: "reviewTags"
                        placeholderText: "review"
                        accessibleName: qsTr("Tags of notes to review")
                    }
                }
                BuiltinSourceRow {
                    objectName: "flashcardsSourceSetting"
                    visible: AppSettings.flashcardsEnabled
                    builtin: "fiszki"
                    title: qsTr("The flashcards' code")
                    pageText: qsTr("Flashcards run from their page in this notebook (%1): change the code there. Restore puts the page in the trash, and the built-in code runs again.")
                    restoredText: qsTr("The built-in flashcards run again; their page is in the trash.")
                }
            }

            // AI for extensions (b.ai) on this device: the provider, the key, the model — or a
            // model that runs on the device itself (AppController.localAi), in builds that have one.
            SettingsGroup {
                id: aiGroup
                objectName: "aiSettings"
                heading: qsTr("AI")
                note: qsTr("Extensions with the “ai” permission can ask a model about your notes. An API key is billed by use; your own server or the model on this device needs none. The key stays on this device.") + (AppSettings.acpProgramsAvailable ? " " + qsTr("A subscription works with an AI agent on this computer: see “AI agents” below.") : AppSettings.acpAvailable ? " " + qsTr("A subscription works with an AI agent on a computer of yours: see “AI agents” below.") : "")

                readonly property bool local: AppSettings.aiProvider === "local"
                readonly property list<var> providers: [{ value: "anthropic", text: "Claude" }, { value: "openai", text: qsTr("OpenAI-compatible") }]
                readonly property list<var> providersWithLocal: [{ value: "anthropic", text: "Claude" }, { value: "openai", text: qsTr("OpenAI-compatible") }, { value: "local", text: qsTr("On this device") }]
                readonly property list<var> notifyModes: [{ value: "always", text: qsTr("Always") }, { value: "background", text: qsTr("In the background") }, { value: "never", text: qsTr("Never") }]

                SettingSwitch {
                    objectName: "assistantSetting"
                    text: qsTr("AI assistant")
                    description: qsTr("The ✨ button: ask about your notes or ask for changes, each with your yes. Answers go into today's note.")
                    iconName: "sparkles"
                    checked: AppSettings.assistantEnabled
                    onToggled: AppSettings.assistantEnabled = checked
                }
                BuiltinSourceRow {
                    objectName: "assistantSourceSetting"
                    visible: AppSettings.assistantEnabled
                    builtin: "asystent"
                    title: qsTr("The assistant's code")
                    pageText: qsTr("The AI assistant runs from its page in this notebook (%1): change its tools and instructions there. Restore puts the page in the trash, and the built-in code runs again.")
                    restoredText: qsTr("The built-in AI assistant runs again; its page is in the trash.")
                }
                ShellSettingRow {
                    title: qsTr("Provider")
                    description: aiGroup.local
                                 ? qsTr("A small model that runs on this device: no key, no account, and nothing you ask leaves the device. Slower and less able than Claude.")
                                 : AppSettings.aiProvider === "openai"
                                   ? qsTr("Any API that speaks OpenAI's chat format: OpenAI, OpenRouter, or Ollama and LM Studio on your own computer.")
                                   : qsTr("Claude by Anthropic: a key from platform.claude.com (Settings → API keys).")
                    BSegmented {
                        objectName: "aiProviderSetting"
                        accessibleName: qsTr("Provider")
                        model: AppController.localAi.available ? aiGroup.providersWithLocal : aiGroup.providers
                        currentValue: AppSettings.aiProvider
                        onActivated: (value) => AppSettings.aiProvider = "" + value
                    }
                }
                // The model on the device: which one, its files (downloaded here), its license.
                ShellSettingRow {
                    objectName: "aiLocalModelRow"
                    visible: aiGroup.local
                    title: qsTr("Model")
                    // (An adapted model: the model under it and its maker, who trained it further.
                    // A model that reads text only: which one looks at pictures in its place.)
                    description: {
                        const about = AppController.localAi.modelAdapted
                                    ? qsTr("%1 is %2 by %3, trained further for Brulion's assistant: the best at working through your notes. Answers at once and reads text only.").arg(AppController.localAi.modelName).arg(AppController.localAi.modelBase).arg(AppController.localAi.modelBaseMaker)
                                    : AppController.localAi.modelSees
                                      ? qsTr("%1 by %2 sees pictures — photos, scans, screenshots — and reads the text on them. It is the weakest of these models at working through a notebook step by step.").arg(AppController.localAi.modelName).arg(AppController.localAi.modelMaker)
                                      : qsTr("%1 by %2 reads text only. It searches the notebook and changes notes better than a model that sees pictures, not as well as the one trained for Brulion's assistant.").arg(AppController.localAi.modelName).arg(AppController.localAi.modelMaker)
                        const seer = AppController.localAi.pictureModel
                        if (seer === "")
                            return about
                        return about + "\n" + (AppController.localAi.pictureModelHere
                                               ? qsTr("Pictures in notes are looked at by %1, which is on this device too: Brulion loads it for them in this one's place, which takes a few seconds more.").arg(seer)
                                               : qsTr("To have pictures in notes looked at, download %1 too (choose it above): Brulion then loads it for them in this one's place.").arg(seer))
                    }
                    BSegmented {
                        objectName: "aiLocalModelSetting"
                        accessibleName: qsTr("Model")
                        model: AppController.localAi.choices
                        currentValue: AppController.localAi.model
                        onActivated: (value) => AppSettings.aiLocalModel = "" + value
                    }
                }
                ColumnLayout {
                    objectName: "aiLocalFiles"
                    visible: aiGroup.local
                    Layout.fillWidth: true
                    spacing: Theme.space3
                    ShellSettingRow {
                        Layout.fillWidth: true
                        title: qsTr("The model's files")
                        // (What is left to download when part of it is here — a file another model
                        // shares, a download that stopped —, and with which models it shares one.)
                        description: {
                            const files = AppController.localAi.modelHere
                                        ? qsTr("On this device (%1). The model answers without the internet.").arg(AppController.localAi.modelSize)
                                        : AppController.localAi.checking
                                          ? qsTr("Checking the downloaded files…")
                                          : AppController.localAi.downloading
                                            ? qsTr("Downloading: %1. You can leave this page; a download that stops goes on from where it was.").arg(AppController.localAi.progressText)
                                            : AppController.localAi.downloadError !== ""
                                              ? qsTr("The download stopped (%1). Download again: it goes on from where it was.").arg(AppController.localAi.downloadError)
                                              : AppController.localAi.modelLeft === ""
                                                ? qsTr("Its files are already on this device: nothing left to download.")
                                                : AppController.localAi.modelLeft !== AppController.localAi.modelSize
                                                  ? qsTr("Partly on this device: %1 of %2 left to download from Hugging Face, best over Wi-Fi.").arg(AppController.localAi.modelLeft).arg(AppController.localAi.modelSize)
                                                  : qsTr("Not on this device yet: %1 to download from Hugging Face, best over Wi-Fi.").arg(AppController.localAi.modelSize)
                            const shared = AppController.localAi.modelSharesWith
                            if (shared === "")
                                return files
                            return files + "\n" + qsTr("Shares a file with %1: it is downloaded once and kept while either of them is on this device.").arg(shared)
                        }
                        BButton {
                            objectName: "aiLocalDownload"
                            visible: !AppController.localAi.modelHere && !AppController.localAi.downloading
                            enabled: !AppController.localAi.downloadingOther
                            size: "small"
                            variant: "primary"
                            iconName: "download"
                            text: qsTr("Download")
                            onClicked: AppController.localAi.download()
                        }
                        BButton {
                            objectName: "aiLocalCancel"
                            visible: AppController.localAi.downloading
                            size: "small"
                            text: qsTr("Stop")
                            onClicked: AppController.localAi.cancelDownload()
                        }
                        BButton {
                            objectName: "aiLocalRemove"
                            visible: AppController.localAi.modelHere
                            size: "small"
                            variant: "ghost"
                            text: qsTr("Remove")
                            onClicked: AppController.localAi.remove()
                        }
                    }
                    BProgressBar {
                        objectName: "aiLocalProgress"
                        Layout.fillWidth: true
                        visible: AppController.localAi.downloading
                        indeterminate: AppController.localAi.checking
                        value: AppController.localAi.progress
                    }
                }
                ShellSettingRow {
                    objectName: "aiLocalLicense"
                    visible: aiGroup.local
                    title: qsTr("Whose model")
                    // (An adapted model is its maker's model still, under its licence — not made by them.)
                    description: AppController.localAi.modelAdapted
                                 ? qsTr("%1 made %2 (licence: %3) and does not endorse this version, which Brulion trained further for its assistant. Downloaded only when you ask.").arg(AppController.localAi.modelBaseMaker).arg(AppController.localAi.modelBase).arg(AppController.localAi.modelLicense)
                                 : qsTr("%1 made it (licence: %2). Downloaded only when you ask.").arg(AppController.localAi.modelMaker).arg(AppController.localAi.modelLicense)
                    BButton {
                        size: "small"
                        variant: "ghost"
                        text: qsTr("Licence")
                        onClicked: Workspace.openUrl(AppController.localAi.modelLicenseUrl)
                    }
                    BButton {
                        size: "small"
                        variant: "ghost"
                        text: qsTr("About the model")
                        onClicked: Workspace.openUrl(AppController.localAi.modelPageUrl)
                    }
                }
                ShellSettingRow {
                    id: aiKeyRow
                    visible: !aiGroup.local
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
                    visible: !aiGroup.local
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
                    visible: !aiGroup.local
                    title: qsTr("Address")
                    description: AppSettings.aiUnencrypted
                                 ? qsTr("Not encrypted: the notes sent to the AI and the key can be read on the network on the way to this address. Fine for a server at home; anywhere else use https.")
                                 : qsTr("Empty: the provider's own. A server of your own: e.g. http://192.168.1.10:11434/v1 (Ollama).")
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
                    description: aiGroup.local ? qsTr("Asks the model “Say OK.”. The first answer takes longer: the model is loaded into memory.")
                                               : qsTr("Asks the model “Say OK.” with these settings.")
                    BButton {
                        objectName: "aiTest"
                        size: "small"
                        iconName: "sparkles"
                        busy: Workspace.extensions.aiPending > 0
                        text: qsTr("Check")
                        onClicked: Workspace.extensions.testAi()
                    }
                }
                // The AI chat's notifications (WorkspaceChats::setNotifyMode; docs/UI.md "AI chat").
                ShellSettingRow {
                    title: qsTr("Notify when the AI answers")
                    description: qsTr("A notification when a conversation is answered, fails or asks you something — never for one on screen. A click opens it.")
                    BSegmented {
                        objectName: "aiChatNotifySetting"
                        accessibleName: qsTr("Notify when the AI answers")
                        model: aiGroup.notifyModes
                        currentValue: AppSettings.aiChatNotify
                        onActivated: (value) => AppSettings.aiChatNotify = "" + value
                    }
                }
            }

            // AI agents of the user's own (the Agent Client Protocol: shell/workspaceagents.h) that
            // answer in the AI chat: the programs this device may start, kept in its settings.
            SettingsGroup {
                id: agentsGroup
                objectName: "acpAgentsSettings"
                visible: AppSettings.acpAvailable
                heading: qsTr("AI agents")
                readonly property list<var> toolCallChoices: [{ value: "all", text: qsTr("Show all") }, { value: "hideFinished", text: qsTr("Hide finished") }, { value: "hideAll", text: qsTr("Hide all") }]

                // A brulion://pair link (the QR code of acp pair, opened by the camera): the pairing
                // dialog filled in, once — the user pairs.
                function takePairing(): void {
                    const url = AppController.routeText(root.route, "pairUrl")
                    if (url === "" || !AppSettings.acpAvailable)
                        return
                    hostAddressField.text = url
                    hostCodeField.text = AppController.routeText(root.route, "pairCode")
                    AppController.replaceRoute({ view: "settings", section: "extensions" })
                    hostDialog.open()
                    pairScroll.start() // (the agents in view: the one paired appears there)
                }
                Component.onCompleted: agentsGroup.takePairing()
                Timer {
                    id: pairScroll
                    interval: 50
                    onTriggered: flick.contentY = Math.max(0, Math.min(agentsGroup.mapToItem(content, 0, 0).y + flick.margin,
                                                                       flick.contentHeight - flick.height))
                }
                Connections {
                    target: root
                    function onRouteChanged(): void {
                        agentsGroup.takePairing()
                    }
                }
                note: AppSettings.acpProgramsAvailable
                      ? qsTr("An AI agent on this computer (Claude Code, Codex, Gemini CLI …) can answer in the AI chat, with its own subscription. It asks before it changes a file or runs a command. Shared, it answers on your phone too.")
                      : qsTr("An AI agent on a computer of yours (Claude Code, Codex …) can answer here, with its own subscription: turn on Share agents with the phone there. In your tailnet it shows up below by itself; else pair it once with a code. Each change and command it makes waits for your yes.")

                Repeater {
                    model: AppSettings.acpAgents
                    delegate: ShellSettingRow {
                        id: agentRow
                        required property var modelData
                        readonly property string agentId: AppController.routeText(agentRow.modelData, "id")
                        readonly property string command: AppController.routeText(agentRow.modelData, "command")
                        readonly property bool remote: AppController.routeFlag(agentRow.modelData, "remote")
                        readonly property bool appAccess: AppController.routeFlag(agentRow.modelData, "app")
                        objectName: "acpAgentRow"
                        Layout.fillWidth: true
                        title: AppController.routeText(agentRow.modelData, "name")
                        description: AppController.routeText(agentRow.modelData, "summary")
                        // Claude Code's and Codex's subscription limits.
                        ChatAgentLimits {
                            objectName: "acpAgentLimits"
                            partner: agentRow.remote ? "" : "acp:" + agentRow.agentId
                        }
                        BButton {
                            objectName: "acpAgentCheck"
                            size: "small"
                            variant: "secondary"
                            iconName: "sparkles"
                            text: qsTr("Check")
                            enabled: agentRow.remote || agentRow.command !== ""
                            onClicked: Workspace.chats.checkAgent(agentRow.agentId)
                        }
                        BButton {
                            objectName: "acpAgentEdit"
                            size: "small"
                            variant: "ghost"
                            iconName: "pencil"
                            text: qsTr("Edit…")
                            onClicked: agentDialog.edit(agentRow.agentId, agentRow.title, agentRow.command,
                                                        AppController.routeFlag(agentRow.modelData, "tools"),
                                                        agentRow.remote, agentRow.appAccess)
                        }
                        BIconButton {
                            objectName: "acpAgentRemove"
                            label: qsTr("Remove")
                            iconName: "trash-2"
                            onClicked: AppSettings.removeAcpAgent(agentRow.agentId)
                        }
                    }
                }
                // (Android: the notification of a foreground service while an agent on another
                // computer answers — docs/ANDROID.md "An AI agent on another computer".)
                PermissionRow {
                    objectName: "acpAgentNotifications"
                    visible: AppController.isAndroid
                    name: "notifications"
                    title: qsTr("Notifications")
                    description: qsTr("While an agent on another computer answers, a notification says so, with Stop; Brulion keeps fetching its answer with the screen off.")
                }
                // How an agent's tool calls show in the AI chat (AppSettings.chatToolCalls; ChatActivity).
                ShellSettingRow {
                    objectName: "chatToolCallsRow"
                    title: qsTr("Tool calls in chat")
                    description: qsTr("What an agent reads, runs and changes, under its answer. Hide finished keeps the failed ones; questions, failures and its plan always show.")
                    BSegmented {
                        objectName: "chatToolCallsSetting"
                        accessibleName: qsTr("Tool calls in chat")
                        model: agentsGroup.toolCallChoices
                        currentValue: AppSettings.chatToolCalls
                        onActivated: (value) => AppSettings.chatToolCalls = "" + value
                    }
                }
                ShellSettingRow {
                    title: qsTr("Add an agent")
                    description: qsTr("Who answers is chosen in each conversation of the AI chat.")
                    BButton {
                        id: addAgentButton
                        objectName: "acpAgentAdd"
                        size: "small"
                        iconName: "plus"
                        text: qsTr("Add")
                        busy: Workspace.chats.agentPairing
                        onClicked: addAgentMenu.popup(addAgentButton, 0, addAgentButton.height)
                    }
                }
                BMenu {
                    id: addAgentMenu
                    objectName: "acpAgentAddMenu"
                    Instantiator {
                        model: AppSettings.acpPresets()
                        delegate: BMenuItem {
                            id: presetItem
                            required property var modelData
                            objectName: "acpAgentPreset"
                            text: AppController.routeText(presetItem.modelData, "name")
                            hint: AppController.routeText(presetItem.modelData, "command")
                            onTriggered: AppSettings.addAcpAgent(AppController.routeText(presetItem.modelData, "id"))
                        }
                        onObjectAdded: (index, object) => addAgentMenu.insertItem(index, object as Item)
                        onObjectRemoved: (index, object) => addAgentMenu.removeItem(object as Item)
                    }
                    BMenuSeparator {
                        visible: AppSettings.acpProgramsAvailable
                    }
                    BMenuItem {
                        objectName: "acpAgentOwn"
                        visible: AppSettings.acpProgramsAvailable
                        text: qsTr("Another agent…")
                        hint: qsTr("Your own command")
                        onTriggered: {
                            const id = AppSettings.addAcpAgent("")
                            if (id !== "")
                                agentDialog.edit(id, qsTr("My agent"), "", true, false, false)
                        }
                    }
                    BMenuItem {
                        objectName: "acpAgentRemote"
                        text: qsTr("An agent on another computer…")
                        hint: qsTr("brulion-cli acp serve")
                        onTriggered: hostDialog.open()
                    }
                }
                // The agents other computers of the user offer in this notebook (AgentShare.offers:
                // their address in the tailnet, from .brulion/config.json): one tap adds one.
                Repeater {
                    model: Workspace.chats.share.offers
                    delegate: ShellSettingRow {
                        id: offerRow
                        required property var modelData
                        readonly property string url: AppController.routeText(offerRow.modelData, "url")
                        readonly property string agent: AppController.routeText(offerRow.modelData, "agent")
                        readonly property string host: AppController.routeText(offerRow.modelData, "host")
                        objectName: "acpOfferRow"
                        Layout.fillWidth: true
                        title: AppController.routeText(offerRow.modelData, "name")
                        description: qsTr("On %1 (%2)").arg(offerRow.host).arg(offerRow.url)
                        BButton {
                            objectName: "acpOfferAdd"
                            size: "small"
                            iconName: "plus"
                            text: qsTr("Add")
                            enabled: !Workspace.chats.agentPairing
                            onClicked: Workspace.chats.addOfferedAgent(offerRow.url, offerRow.agent)
                        }
                    }
                }
                // "Share agents with the phone" (AgentShare): this computer runs its agents for the
                // user's other devices while the app runs.
                SettingSwitch {
                    id: shareSwitch
                    objectName: "acpShareSwitch"
                    visible: Workspace.chats.share.available
                    text: qsTr("Share agents with the phone")
                    description: Workspace.chats.share.status
                    iconName: "smartphone"
                    checked: AppSettings.acpShare
                    onToggled: AppSettings.acpShare = shareSwitch.checked
                }
                ShellSettingRow {
                    objectName: "acpSharePairRow"
                    visible: Workspace.chats.share.available && AppSettings.acpShare
                    title: qsTr("Pair a device")
                    description: Workspace.chats.share.devices > 0
                                 ? qsTr("%n device(s) paired. Another one scans a code.", "", Workspace.chats.share.devices)
                                 : qsTr("A device outside your tailnet scans a code.")
                    BButton {
                        objectName: "acpSharePair"
                        size: "small"
                        iconName: "qr-code"
                        text: qsTr("Show code")
                        busy: Workspace.chats.share.pairing
                        enabled: Workspace.chats.share.state === "running"
                        onClicked: Workspace.chats.share.newPairing()
                    }
                    BButton {
                        objectName: "acpShareUnpair"
                        size: "small"
                        variant: "ghost"
                        visible: Workspace.chats.share.devices > 0
                        text: qsTr("Unpair all")
                        onClicked: Workspace.chats.share.unpairAll()
                    }
                }
                ShellSettingRow {
                    objectName: "acpShareAddressRow"
                    visible: Workspace.chats.share.available && AppSettings.acpShare
                    title: qsTr("Address")
                    description: Workspace.chats.share.unencrypted
                                 ? qsTr("Not encrypted on your network: fine at home. Tailscale encrypts it.")
                                 : qsTr("Empty: found by itself (Tailscale, else this computer's address).")
                    BTextField {
                        objectName: "acpShareAddress"
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        text: AppSettings.acpShareAddress
                        placeholderText: Workspace.chats.share.automaticAddress !== ""
                                         ? Workspace.chats.share.automaticAddress : "http://192.168.1.10:8790"
                        accessibleName: qsTr("Address")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                        onEditingFinished: AppSettings.acpShareAddress = text
                    }
                }
                SettingSwitch {
                    objectName: "acpShareTrust"
                    visible: Workspace.chats.share.available && AppSettings.acpShare
                             && (Workspace.chats.share.tailnet || !AppSettings.acpShareTrust)
                    text: qsTr("Pair my devices without a code")
                    description: Workspace.chats.share.owner !== ""
                                 ? qsTr("Devices of %1 in the tailnet: Tailscale vouches for them.").arg(Workspace.chats.share.owner)
                                 : qsTr("Your devices in the tailnet: Tailscale vouches for them.")
                    checked: AppSettings.acpShareTrust
                    onToggled: AppSettings.acpShareTrust = checked
                }
                Connections {
                    target: Workspace.chats.share
                    function onPairingChanged(): void {
                        if (Workspace.chats.share.pairCode !== "" && !shareCodeDialog.opened)
                            shareCodeDialog.open()
                    }
                }
                // A pairing code of this computer: its brulion://pair link as a QR code for the phone's
                // camera (Brulion opens the pairing filled in there), the code to type, until when.
                BDialog {
                    id: shareCodeDialog
                    objectName: "acpShareCodeDialog"
                    title: qsTr("Pair a device")
                    subtitle: qsTr("Scan it with the phone's camera: Brulion opens the pairing.")
                    iconName: "qr-code"
                    showReject: false
                    acceptText: qsTr("Close")
                    onClosed: Workspace.chats.share.closePairing()
                    QrCodeView {
                        objectName: "acpShareQr"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Math.min(260, shareCodeDialog.availableWidth)
                        Layout.preferredHeight: Math.min(260, shareCodeDialog.availableWidth)
                        text: Workspace.chats.share.pairLink
                    }
                    BText {
                        objectName: "acpShareCode"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Code %1 · valid until %2, once").arg(Workspace.chats.share.pairCode)
                                                                    .arg(Workspace.chats.share.pairExpires)
                    }
                    BText {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: Workspace.chats.share.url
                        variant: "caption"
                        wrapMode: Text.WrapAnywhere
                    }
                }
                // A pairing without a code the computer did not take (not in its tailnet, another
                // user's device): the dialog with its address, for its code.
                Connections {
                    target: Workspace.chats
                    function onAgentHostNeedsCode(address: string): void {
                        hostAddressField.text = address
                        hostCodeField.text = ""
                        hostDialog.needsCode = true
                        hostDialog.open()
                    }
                }
                // Pairing with a computer that runs `brulion-cli acp serve` (WorkspaceAgents::pair):
                // its address and a code of its `acp pair` — none in the user's own tailnet; the
                // result comes as a toast.
                BDialog {
                    id: hostDialog
                    objectName: "acpHostDialog"
                    property bool needsCode: false
                    title: qsTr("An agent on another computer")
                    subtitle: hostDialog.needsCode
                              ? qsTr("That computer wants its code: Settings → AI agents → Pair a device there.")
                              : qsTr("On the computer: Settings → AI agents → Share agents with the phone. In your tailnet no code is needed.")
                    iconName: "sparkles"
                    acceptText: qsTr("Pair")
                    acceptEnabled: Js.trim(hostAddressField.text) !== "" && !Workspace.chats.agentPairing
                                   && (!hostDialog.needsCode || Js.trim(hostCodeField.text) !== "")
                    initialFocusItem: hostAddressField.text === "" ? hostAddressField : hostCodeField
                    onClosed: hostDialog.needsCode = false
                    onAccepted: {
                        Workspace.chats.pairAgentHost(hostAddressField.text, hostCodeField.text)
                        hostCodeField.text = ""
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Address")
                        variant: "label"
                    }
                    BTextField {
                        id: hostAddressField
                        objectName: "acpHostAddress"
                        Layout.fillWidth: true
                        placeholderText: "https://agent.example.com"
                        accessibleName: qsTr("Address")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    }
                    BText {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.space2
                        text: qsTr("Pairing code")
                        variant: "label"
                    }
                    BTextField {
                        id: hostCodeField
                        objectName: "acpHostCode"
                        Layout.fillWidth: true
                        placeholderText: hostDialog.needsCode ? "K7QM-2XRP" : qsTr("None in your tailnet")
                        accessibleName: qsTr("Pairing code")
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhPreferUppercase
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Reach the computer only in your own network (a tailnet, a VPN): its agents run commands there for whoever is paired.")
                        variant: "caption"
                        wrapMode: Text.Wrap
                    }
                }
                BDialog {
                    id: agentDialog
                    objectName: "acpAgentDialog"
                    property string agentId
                    property bool remote
                    title: qsTr("AI agent")
                    subtitle: agentDialog.remote
                              ? qsTr("An agent on another computer: its program, its sign-in and Brulion's tools are that computer's.")
                              : qsTr("A program that speaks the Agent Client Protocol: it runs in the notebook's folder and signs in by itself.")
                    iconName: "sparkles"
                    acceptText: qsTr("Save")
                    acceptEnabled: Js.trim(agentNameField.text) !== ""
                    initialFocusItem: agentDialog.remote ? agentNameField : agentCommandField
                    function edit(id: string, name: string, command: string, tools: bool, remote: bool,
                                  app: bool): void {
                        agentDialog.agentId = id
                        agentDialog.remote = remote
                        agentNameField.text = name
                        agentCommandField.text = command
                        agentToolsSwitch.checked = tools
                        agentAppSwitch.checked = app
                        agentDialog.open()
                    }
                    onAccepted: {
                        AppSettings.setAcpAgentName(agentDialog.agentId, agentNameField.text)
                        if (!agentDialog.remote) {
                            AppSettings.setAcpAgentCommand(agentDialog.agentId, agentCommandField.text)
                            AppSettings.setAcpAgentTools(agentDialog.agentId, agentToolsSwitch.checked)
                            AppSettings.setAcpAgentApp(agentDialog.agentId, agentAppSwitch.checked)
                        }
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Name")
                        variant: "label"
                    }
                    BTextField {
                        id: agentNameField
                        objectName: "acpAgentName"
                        Layout.fillWidth: true
                        accessibleName: qsTr("Name")
                    }
                    BText {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.space2
                        visible: !agentDialog.remote
                        text: qsTr("Command")
                        variant: "label"
                    }
                    BTextField {
                        id: agentCommandField
                        objectName: "acpAgentCommand"
                        visible: !agentDialog.remote
                        Layout.fillWidth: true
                        placeholderText: "npx -y @agentclientprotocol/claude-agent-acp"
                        accessibleName: qsTr("Command")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    }
                    BText {
                        Layout.fillWidth: true
                        visible: !agentDialog.remote
                        text: qsTr("The program and its arguments, as in a terminal: npx -y @agentclientprotocol/claude-agent-acp, npx -y @agentclientprotocol/codex-acp, gemini --acp, opencode acp.")
                        variant: "caption"
                        wrapMode: Text.Wrap
                    }
                    SettingSwitch {
                        id: agentToolsSwitch
                        objectName: "acpAgentTools"
                        visible: !agentDialog.remote
                        Layout.topMargin: Theme.space2
                        text: qsTr("Brulion's tools")
                        description: qsTr("Its sessions get Brulion's MCP server (brulion-cli mcp): search, queries, tasks and journals of this notebook.")
                    }
                    // Live changes (docs/CLI.md "Live changes"): its tools get --app-control and a
                    // grant of this running app, which ends when the agent stops.
                    SettingSwitch {
                        id: agentAppSwitch
                        objectName: "acpAgentApp"
                        visible: !agentDialog.remote
                        enabled: agentToolsSwitch.checked
                        text: qsTr("Let it change the app while it runs")
                        description: qsTr("With Brulion's tools it may also change this running app live (Luau, extensions for this run, the theme, settings, the window's items); the top bar shows what it changed, and you can undo all of it.")
                    }
                }
            }

            // Scheduled conversations (shell/chatschedules.h): kept on this device per notebook.
            ShellChatSchedules {}

            // The remote MCP server for claude.ai and ChatGPT (desktop: AppController.mcpServer,
            // shell/mcpserversupervisor.h; docs/CLI.md "Remote MCP server"): `brulion-cli mcp
            // serve` in the background while Brulion runs, for the notebook open now; its password
            // stays in the app's secrets.
            SettingsGroup {
                id: mcpGroup
                objectName: "mcpServerSettings"
                visible: AppController.mcpServer.available
                heading: qsTr("claude.ai and ChatGPT")
                note: qsTr("Their apps can search and change this notebook while Brulion runs.")

                readonly property string serverState: AppController.mcpServer.state
                readonly property string endpoint: AppController.mcpServer.endpoint

                SettingSwitch {
                    objectName: "mcpServerSetting"
                    text: qsTr("Connector server")
                    description: mcpGroup.serverState === "running" ? qsTr("Running.")
                                 : mcpGroup.serverState === "starting" ? qsTr("Starting…")
                                 : mcpGroup.serverState === "waiting" ? qsTr("Stopped: %1 Starting it again…").arg(AppController.mcpServer.error)
                                 : mcpGroup.serverState === "failed" ? qsTr("Not running: %1").arg(AppController.mcpServer.error)
                                 : qsTr("Off.")
                    iconName: "server"
                    checked: AppSettings.mcpServerEnabled
                    onToggled: AppSettings.mcpServerEnabled = checked
                }
                ShellSettingRow {
                    objectName: "mcpServerAddressRow"
                    visible: mcpGroup.endpoint !== ""
                    title: qsTr("Connector URL")
                    description: AppController.mcpServer.reachable
                                 ? qsTr("claude.ai: Settings → Connectors → Add custom connector. ChatGPT: Developer mode → Create app.")
                                 : qsTr("Only this computer reaches it: set a public https address below.")
                    BText {
                        objectName: "mcpServerEndpoint"
                        Layout.maximumWidth: Theme.compact ? -1 : 320
                        text: mcpGroup.endpoint
                        variant: "mono"
                        elide: Text.ElideMiddle
                    }
                    BButton {
                        objectName: "mcpServerCopy"
                        size: "small"
                        variant: "secondary"
                        iconName: "copy"
                        text: qsTr("Copy")
                        onClicked: {
                            if (AppController.mcpServer.copy(mcpGroup.endpoint))
                                AppController.toast(qsTr("Copied."), "success")
                        }
                    }
                }
                ShellSettingRow {
                    title: qsTr("Password")
                    description: AppSettings.hasMcpServerPassword ? qsTr("Set. You type it when an app connects.")
                                                                  : qsTr("Needed: at least 8 characters, typed when an app connects.")
                    BTextField {
                        id: mcpPasswordField
                        objectName: "mcpServerPassword"
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        echoMode: TextInput.Password
                        placeholderText: AppSettings.hasMcpServerPassword ? "••••••••" : qsTr("New password")
                        accessibleName: qsTr("Password")
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                        onEditingFinished: {
                            if (mcpPasswordField.text === "")
                                return
                            if (AppSettings.setMcpServerPassword(mcpPasswordField.text)) {
                                mcpPasswordField.errorText = ""
                                AppController.toast(qsTr("The password is saved on this device."), "success")
                            } else {
                                mcpPasswordField.errorText = qsTr("At least 8 characters.")
                            }
                            mcpPasswordField.text = ""
                        }
                    }
                }
                ShellSettingRow {
                    title: qsTr("Public address")
                    description: qsTr("An https address that reaches the port, e.g. from tailscale funnel %1.").arg(AppSettings.mcpServerPort)
                    BTextField {
                        objectName: "mcpServerPublicUrl"
                        Layout.preferredWidth: Theme.compact ? -1 : 260
                        Layout.fillWidth: Theme.compact
                        text: AppSettings.mcpServerPublicUrl
                        placeholderText: "https://notes.example.com"
                        accessibleName: qsTr("Public address")
                        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                        onEditingFinished: AppSettings.mcpServerPublicUrl = text
                    }
                }
                ShellSettingRow {
                    title: qsTr("Port")
                    description: qsTr("Where it listens on this computer (127.0.0.1).")
                    BTextField {
                        id: mcpPortField
                        objectName: "mcpServerPort"
                        Layout.preferredWidth: Theme.compact ? -1 : 120
                        Layout.fillWidth: Theme.compact
                        text: "" + AppSettings.mcpServerPort
                        accessibleName: qsTr("Port")
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1024; top: 65535 }
                        onEditingFinished: {
                            AppSettings.mcpServerPort = Js.parseInt(mcpPortField.text)
                            mcpPortField.text = "" + AppSettings.mcpServerPort
                        }
                    }
                }
                SettingSwitch {
                    objectName: "mcpServerReadOnly"
                    text: qsTr("Only reading")
                    description: qsTr("The apps can search and read, never change a note.")
                    checked: AppSettings.mcpServerReadOnly
                    onToggled: AppSettings.mcpServerReadOnly = checked
                }
                // What it did lately: one line per request (the event log has them too).
                ShellSettingRow {
                    objectName: "mcpServerLogRow"
                    visible: AppController.mcpServer.log.length > 0
                    title: qsTr("Requests")
                    description: qsTr("Its last lines; also in the event log.")
                    BButton {
                        size: "small"
                        variant: "ghost"
                        iconName: mcpLog.visible ? "chevron-up" : "chevron-down"
                        text: mcpLog.visible ? qsTr("Hide") : qsTr("Show")
                        onClicked: mcpLog.visible = !mcpLog.visible
                    }
                }
                BText {
                    id: mcpLog
                    objectName: "mcpServerLog"
                    Layout.fillWidth: true
                    visible: false
                    variant: "mono"
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.WrapAnywhere
                    textFormat: Text.PlainText
                    text: Js.joinNonEmpty(AppController.mcpServer.log, "\n")
                }
            }

            // Speech recognition on this device for voice notes (qml/media/MediaSpeechSettings).
            MediaSpeechSettings {}

            SettingsGroup {
                heading: qsTr("In this notebook")
                note: qsTr("Your extensions are pages tagged #rozszerzenie (or #extension). Code from another device, a sync or an import runs only after you approve it.")

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
                        required property int extSettings
                        required property string extModules
                        required property int extTypes
                        required property string extTypeMessage
                        /// Live code an AI agent (a live client) loaded for this run of the app
                        /// (docs/EXTENSIONS.md "Live code"): its description is who sent it.
                        readonly property bool live: extRow.extKind === "live"
                        objectName: extRow.live ? "extensionLiveRow" : "extensionRow"
                        visible: extRow.extKind === "page" || extRow.live
                        Layout.fillWidth: true
                        title: extRow.extTitle
                        description: root.extensionStateText(extRow.extState, extRow.extMessage, extRow.extErrorLine,
                                                             extRow.extCommands, extRow.extViews, extRow.extHooks)
                                     + (extRow.extPermissions !== "" ? "\n" + qsTr("May: %1").arg(extRow.extPermissions) : "")
                                     + (extRow.extModules !== "" ? "\n" + qsTr("Uses the code of: %1").arg(extRow.extModules) : "")
                                     + (extRow.extTypes > 0 ? "\n" + qsTr("Type check: %n warning(s), %1", "", extRow.extTypes).arg(extRow.extTypeMessage) : "")
                                     + (extRow.extDescription === "" ? ""
                                        : extRow.live ? "\n" + qsTr("From %1 — for this run of the app only").arg(extRow.extDescription)
                                        : "\n" + extRow.extDescription)
                        BBadge {
                            objectName: "extensionLiveBadge"
                            visible: extRow.live
                            text: qsTr("Live")
                            variant: "accent"
                        }
                        BButton {
                            objectName: "extensionLiveRemove"
                            visible: extRow.live
                            size: "small"
                            iconName: "x"
                            text: qsTr("Remove")
                            onClicked: Workspace.extensions.unloadLive(extRow.extId)
                        }
                        BButton {
                            visible: !extRow.live
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
                            text: qsTr("Read and run")
                            onClicked: AppController.requestUiAction("approveExtension:" + extRow.extPath)
                        }
                        BButton {
                            objectName: "extensionSettingsButton"
                            visible: extRow.extSettings > 0
                            size: "small"
                            variant: "ghost"
                            iconName: "settings"
                            text: qsTr("Settings")
                            onClicked: AppController.requestUiAction("extensionSettings:" + extRow.extId)
                        }
                        BIconButton {
                            objectName: "extensionCopyButton"
                            visible: !extRow.live
                            size: "small"
                            iconName: "copy"
                            label: qsTr("Copy to another notebook")
                            onClicked: AppController.requestUiAction("copyExtension:" + extRow.extPath)
                        }
                        BButton {
                            visible: !extRow.live && (extRow.extState === "running" || extRow.extState === "error" || extRow.extState === "off")
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
                    const what = qsTr("While Brulion is closed, Android checks the notebook now and then, so reminders added on your other devices ring too.")
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
        id: privacySection
        ShellPrivacySettings {}
    }

    Component {
        id: shortcutsSection
        ShellShortcutSettings {}
    }

    Component {
        id: searchSection
        ColumnLayout {
            spacing: Theme.space8
            AttachmentSettings {}
            // Search by meaning (qml/shell/ShellSemanticSettings: the model on this device).
            ShellSemanticSettings {}
        }
    }

    Component {
        id: automationSection
        ShellAutomationSettings {}
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
                    description: qsTr("Once a day Brulion asks GitHub for a newer version. Nothing about your notes is sent.")
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

            // The Web Clipper browser extension (desktop: src/app/shell/webclippersetup.h,
            // docs/CLIPPER.md): which browsers reach Brulion's native messaging host; connecting
            // them writes the host manifests as `brulion-cli clip install` does.
            SettingsGroup {
                objectName: "settingsWebClipper"
                visible: WebClipperSetup.available
                heading: qsTr("Browser extension (Web Clipper)")

                BText {
                    Layout.fillWidth: true
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                    text: qsTr("Save articles, selections, whole pages and screenshots from Chrome, Chromium, Brave, Edge and Firefox into this notebook, with their pictures. Install the extension in the browser, then connect the browsers here — once.")
                }
                // One block of rows: the card's hairlines go around it (the list is built anew on
                // every refresh, which the dividers between the card's own rows would not follow).
                ColumnLayout {
                    objectName: "settingsWebClipperBrowsers"
                    Layout.fillWidth: true
                    spacing: Theme.space4
                    Repeater {
                        model: WebClipperSetup.browsers
                        delegate: ShellSettingRow {
                            id: browserRow
                            required property string browserId
                            required property string browserName
                            required property string statusText
                            required property bool connected
                            required property bool broken
                            objectName: "settingsWebClipper_" + browserRow.browserId
                            Layout.fillWidth: true
                            title: browserRow.browserName
                            description: browserRow.statusText
                            BBadge {
                                visible: browserRow.connected || browserRow.broken
                                text: browserRow.broken ? qsTr("Broken") : qsTr("Connected")
                                variant: browserRow.broken ? "warning" : "success"
                            }
                        }
                    }
                }
                // A Flow: on a phone's width the buttons wrap instead of leaving the card.
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BButton {
                        objectName: "settingsWebClipperConnect"
                        text: WebClipperSetup.connectedCount > 0 ? qsTr("Connect again") : qsTr("Connect browsers")
                        iconName: "link"
                        size: "small"
                        variant: WebClipperSetup.connectedCount > 0 ? "secondary" : "primary"
                        onClicked: WebClipperSetup.connectBrowsers()
                    }
                    BButton {
                        objectName: "settingsWebClipperDisconnect"
                        visible: WebClipperSetup.connectedCount > 0
                        text: qsTr("Disconnect")
                        size: "small"
                        variant: "ghost"
                        onClicked: WebClipperSetup.disconnectBrowsers()
                    }
                    BButton {
                        text: qsTr("How to install the extension")
                        iconName: "external-link"
                        size: "small"
                        variant: "ghost"
                        onClicked: Workspace.openUrl("https://github.com/janusz-bit/brulion/blob/main/docs/CLIPPER.md")
                    }
                }
                BText {
                    objectName: "settingsWebClipperMessage"
                    Layout.fillWidth: true
                    visible: WebClipperSetup.message !== ""
                    color: WebClipperSetup.messageIsError ? Theme.dangerText : Theme.fgMuted
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                    text: WebClipperSetup.message
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
                    description: qsTr("AI agents and scripts can then see and use Brulion (brulion-cli app). Any program you run could read your notes too: turn it on only while you need it.")
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
