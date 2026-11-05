pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// IntroView — the intro on the first start (AppController.introPending), before the notebook is
// chosen (OnboardingView). Four steps, each changes its settings at once and every one of them
// stays in Settings:
//   1. Language — English (the default) or Polski; the whole UI switches at once.
//   2. Look — the theme (Black — the default —, Dark blue-grey, Light, System) as previews in
//      their own colours, the accent colour, the text size, reduce motion.
//   3. Permissions — what reminders need on this platform: Android's notifications, exact alarms,
//      alarms over the lock screen and "All files access" (only for a folder on the phone's
//      storage); the browser's notifications; on the desktop the tray and start at sign-in. The
//      reminders switch and a test notification on every platform.
//   4. Notes — the first notebook's settings (journal and page mode, journal file names, first
//      day of the week, default reminder time): AppSettings.newNotebookConfig, written to the
//      next new notebook (AppController::applyIntroNotebookConfig).
// "Skip" ends the intro with the defaults; the last step's "Choose a notebook" ends it too
// (AppController.finishIntro()). Back: the button, Android's back gesture (Main.qml calls
// backStep()), Alt+Left; Continue: the button, Ctrl+Enter.
Rectangle {
    id: root

    property var route: ({})

    readonly property var safe: SafeArea.margins
    property int step: 0
    readonly property var steps: [
        { id: "language", title: qsTr("Language"), icon: "languages" },
        { id: "look", title: qsTr("Look"), icon: "palette" },
        { id: "permissions", title: qsTr("Permissions"), icon: "shield-check" },
        { id: "notes", title: qsTr("Notes"), icon: "notebook-pen" }
    ]
    readonly property bool lastStep: step === steps.length - 1

    color: Theme.bg
    focus: true

    /// One step back; false on the first one (Android's back gesture then leaves the app).
    function backStep() {
        if (step === 0)
            return false
        --step
        return true
    }
    function nextStep() {
        if (lastStep)
            finish()
        else
            ++step
    }
    function finish() {
        AppController.finishIntro()
    }

    onStepChanged: flick.contentY = 0
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back && root.backStep()) {
            event.accepted = true
        } else if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier) && root.backStep()) {
            event.accepted = true
        } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && (event.modifiers & Qt.ControlModifier)) {
            root.nextStep()
            event.accepted = true
        }
    }

    // ---- The first notebook's settings (step 4) ----------------------------------------------
    readonly property var notebookDefaults: ({
        journalMode: "outline",
        pageMode: "document",
        journalFileFormat: "yyyy-MM-dd",
        weekStartsOn: 1,
        defaultReminderTime: "09:00"
    })
    function notebookValue(key) {
        const chosen = AppSettings.newNotebookConfig[key]
        return chosen !== undefined ? chosen : notebookDefaults[key]
    }
    function setNotebookValue(key, value) {
        const config = Object.assign({}, notebookDefaults, AppSettings.newNotebookConfig)
        config[key] = value
        AppSettings.newNotebookConfig = config
    }

    // ---- Pieces ------------------------------------------------------------------------------

    /// A step's heading: an icon in a disc, the title and what the step is for.
    component StepHeader: ColumnLayout {
        id: header
        property string icon
        property string title
        property string text
        Layout.fillWidth: true
        spacing: Theme.space3

        Rectangle {
            implicitWidth: 48
            implicitHeight: 48
            radius: 24
            color: Theme.accentBg
            BIcon {
                anchors.centerIn: parent
                name: header.icon
                size: Theme.iconSizeLg
                color: Theme.accentText
            }
        }
        BText {
            Layout.fillWidth: true
            text: header.title
            variant: "display"
            font.pixelSize: Theme.compact ? Theme.text2xl : Theme.text3xl
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
        }
        BText {
            Layout.fillWidth: true
            visible: header.text !== ""
            text: header.text
            variant: "muted"
            font.pixelSize: Theme.textMd
            lineHeight: Theme.lineHeightPx(Theme.textMd)
            wrapMode: Text.Wrap
        }
    }

    /// Settings rows on a card, divided by hairlines (as on the settings page).
    component Group: BCard {
        id: group
        default property alias rows: groupColumn.data
        Layout.fillWidth: true
        padding: Theme.compact ? Theme.space4 : Theme.space5
        contentWidth: groupColumn.implicitWidth
        contentHeight: groupColumn.implicitHeight
        ColumnLayout {
            id: groupColumn
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8
        }
        ShellSettingDividers { column: groupColumn }
    }

    /// A choice shown as a card: a check when it is the chosen one.
    component ChoiceCard: BCard {
        id: choice
        property string title
        property string subtitle
        property string icon
        property bool chosen: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        clickable: true
        tone: chosen ? "accent" : "default"
        padding: Theme.space4
        accessibleName: title
        Accessible.checkable: true
        Accessible.checked: chosen

        RowLayout {
            anchors.fill: parent
            spacing: Theme.space3
            BIcon {
                visible: choice.icon !== ""
                name: choice.icon
                color: choice.chosen ? Theme.accentText : Theme.fgMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                BText {
                    Layout.fillWidth: true
                    text: choice.title
                    variant: "label"
                    font.pixelSize: Theme.textMd
                    wrapMode: Text.Wrap
                }
                BText {
                    Layout.fillWidth: true
                    visible: choice.subtitle !== ""
                    text: choice.subtitle
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
            }
            BIcon {
                name: choice.chosen ? "circle-check" : ""
                color: Theme.accentText
            }
        }
    }

    /// A theme as a small picture of the app in its own colours.
    component ThemeTile: BCard {
        id: tile
        required property string mode       // "dark" | "light" | "system"
        property string surface: ""         // the dark or light background ("" for "system")
        property string title
        property bool chosen: false
        readonly property var darkColors: ThemePalette.colors(true, AppSettings.lightSurface,
                                                             mode === "dark" ? surface : AppSettings.darkSurface,
                                                             Theme.accentName, Theme.customAccent)
        readonly property var lightColors: ThemePalette.colors(false, AppSettings.lightSurface, AppSettings.darkSurface,
                                                              Theme.accentName, Theme.customAccent)
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        clickable: true
        tone: chosen ? "accent" : "default"
        padding: Theme.space2
        accessibleName: title
        Accessible.checkable: true
        Accessible.checked: chosen

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.space2

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                clip: true

                Rectangle {
                    // "System": half light, half dark.
                    anchors.fill: parent
                    radius: Theme.radius
                    color: tile.mode === "light" ? tile.lightColors.bg : tile.darkColors.bg
                    border.width: 1
                    border.color: Theme.border
                }
                Rectangle {
                    visible: tile.mode === "system"
                    anchors { top: parent.top; bottom: parent.bottom; right: parent.right; margins: 1 }
                    width: parent.width / 2 - 1
                    radius: Theme.radius
                    color: tile.lightColors.bg
                }
                Repeater {
                    // One "page" per half (two on "system").
                    model: tile.mode === "system" ? [tile.darkColors, tile.lightColors] : [tile.mode === "light" ? tile.lightColors : tile.darkColors]
                    delegate: Column {
                        id: page
                        required property var modelData
                        required property int index
                        readonly property real slot: tile.mode === "system" ? parent.width / 2 : parent.width
                        x: index * slot + Theme.space2
                        y: Theme.space2
                        width: slot - 2 * Theme.space2
                        spacing: 5
                        Rectangle { width: 10; height: 10; radius: 5; color: page.modelData.accent }
                        Rectangle { width: page.width * 0.8; height: 5; radius: 2; color: page.modelData.fg }
                        Rectangle { width: page.width * 0.6; height: 5; radius: 2; color: page.modelData.fgMuted }
                        Rectangle {
                            width: page.width
                            height: 16
                            radius: 4
                            color: page.modelData.bgElevated
                            border.width: 1
                            border.color: page.modelData.border
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 2
                spacing: Theme.space1
                BText {
                    Layout.fillWidth: true
                    text: tile.title
                    variant: "label"
                    elide: Text.ElideRight
                }
                BIcon {
                    name: tile.chosen ? "circle-check" : ""
                    size: Theme.iconSizeSm
                    color: Theme.accentText
                }
            }
        }
    }

    /// A permission: what it is for, whether it is given, and the button that asks for it.
    component PermissionCard: BCard {
        id: permission
        required property string name
        property string title
        property string text
        property string icon
        property bool optional: false
        readonly property bool granted: AppController.permissions[name] === true
        objectName: "introPermission_" + name
        Layout.fillWidth: true
        padding: Theme.space4

        RowLayout {
            anchors.fill: parent
            spacing: Theme.space3
            BIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                name: permission.icon
                color: permission.granted ? Theme.successText : Theme.fgMuted
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BText {
                        Layout.fillWidth: true
                        text: permission.title
                        variant: "label"
                        wrapMode: Text.Wrap
                    }
                    BBadge {
                        text: permission.granted ? qsTr("Allowed")
                                                 : permission.optional ? qsTr("Optional") : qsTr("Not allowed")
                        variant: permission.granted ? "success" : permission.optional ? "neutral" : "warning"
                    }
                }
                BText {
                    Layout.fillWidth: true
                    text: permission.text
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    wrapMode: Text.Wrap
                }
                BButton {
                    visible: !permission.granted
                    objectName: "introAllow_" + permission.name
                    size: "small"
                    variant: permission.optional ? "secondary" : "primary"
                    text: qsTr("Allow")
                    iconName: "check"
                    onClicked: AppController.requestPermission(permission.name)
                }
            }
        }
    }

    // ---- Layout ------------------------------------------------------------------------------

    Flickable {
        id: flick
        anchors.top: parent.top
        anchors.topMargin: root.safe.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footer.top
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * margin
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        readonly property int margin: Theme.compact ? Theme.space5 : Theme.space10

        ColumnLayout {
            id: column
            x: Math.round((flick.width - width) / 2)
            y: flick.margin
            width: Math.min(flick.width - 2 * flick.margin, 640)
            spacing: Theme.space6

            // ---- Progress: the steps as dots, "Skip" --------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2

                Image {
                    source: "qrc:/images/logo.svg"
                    sourceSize: Qt.size(28, 28)
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    Accessible.ignored: true
                }
                Row {
                    Layout.leftMargin: Theme.space2
                    spacing: 6
                    Repeater {
                        model: root.steps.length
                        delegate: Rectangle {
                            required property int index
                            width: index === root.step ? 22 : 8
                            height: 8
                            radius: 4
                            color: index <= root.step ? Theme.accent : Theme.borderStrong
                            Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                        }
                    }
                }
                BText {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space2
                    objectName: "introProgress"
                    text: qsTr("Step %1 of %2").arg(root.step + 1).arg(root.steps.length)
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    elide: Text.ElideRight
                }
                BButton {
                    objectName: "introSkip"
                    visible: !root.lastStep
                    variant: "ghost"
                    size: "small"
                    text: qsTr("Skip")
                    onClicked: root.finish()
                }
            }

            StackLayout {
                id: pages
                Layout.fillWidth: true
                currentIndex: root.step
                // Only the shown step's height (StackLayout takes the tallest by default).
                Layout.preferredHeight: children[currentIndex] ? children[currentIndex].implicitHeight : 0

                // ---- 1. Language ---------------------------------------------------------
                ColumnLayout {
                    objectName: "introStep_language"
                    spacing: Theme.space6

                    StepHeader {
                        icon: "languages"
                        title: qsTr("Welcome to Brulion")
                        text: qsTr("Let's set Brulion up in a minute: language, look, permissions and your notes. Everything can be changed later in Settings.")
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Choose your language")
                        variant: "title"
                        Accessible.role: Accessible.Heading
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Theme.compact ? 1 : 2
                        columnSpacing: Theme.space3
                        rowSpacing: Theme.space3
                        // The names in their own language: whoever does not read the current one
                        // still finds theirs.
                        ChoiceCard {
                            objectName: "introLanguage_en"
                            title: "English"
                            // (In English the name says it already.)
                            subtitle: qsTr("English") !== "English" ? qsTr("English") : ""
                            chosen: AppSettings.language === "en"
                            onClicked: AppSettings.language = "en"
                        }
                        ChoiceCard {
                            objectName: "introLanguage_pl"
                            title: "Polski"
                            subtitle: qsTr("Polish")
                            chosen: AppSettings.language === "pl"
                            onClicked: AppSettings.language = "pl"
                        }
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Dates you type are understood in both languages: “tomorrow 9:00” and “jutro 9:00”.")
                        variant: "faint"
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.Wrap
                    }
                }

                // ---- 2. Look -------------------------------------------------------------
                ColumnLayout {
                    objectName: "introStep_look"
                    spacing: Theme.space6

                    StepHeader {
                        icon: "palette"
                        title: qsTr("Choose your look")
                        text: qsTr("Black is easy on the eyes and saves power on OLED screens.")
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Theme.compact ? 2 : 4
                        columnSpacing: Theme.space3
                        rowSpacing: Theme.space3

                        ThemeTile {
                            objectName: "introTheme_black"
                            mode: "dark"
                            surface: "black"
                            title: qsTr("Black")
                            chosen: AppSettings.themeMode === "dark" && AppSettings.darkSurface === "black"
                            onClicked: { AppSettings.themeMode = "dark"; AppSettings.darkSurface = "black" }
                        }
                        ThemeTile {
                            objectName: "introTheme_dark"
                            mode: "dark"
                            surface: "slate"
                            title: qsTr("Dark")
                            chosen: AppSettings.themeMode === "dark" && AppSettings.darkSurface === "slate"
                            onClicked: { AppSettings.themeMode = "dark"; AppSettings.darkSurface = "slate" }
                        }
                        ThemeTile {
                            objectName: "introTheme_light"
                            mode: "light"
                            title: qsTr("Light")
                            chosen: AppSettings.themeMode === "light"
                            onClicked: AppSettings.themeMode = "light"
                        }
                        ThemeTile {
                            objectName: "introTheme_system"
                            mode: "system"
                            title: qsTr("System")
                            chosen: AppSettings.themeMode === "system"
                            onClicked: AppSettings.themeMode = "system"
                        }
                    }
                    Group {
                        ShellSettingRow {
                            title: qsTr("Accent colour")
                            description: qsTr("Marks today, the current tab and highlights.")
                            Flow {
                                Layout.maximumWidth: 6 * (Theme.touchUi ? Theme.touchTarget : 34)
                                spacing: 0
                                Repeater {
                                    model: [
                                        { value: "red", text: qsTr("Red") },
                                        { value: "blue", text: qsTr("Blue") },
                                        { value: "green", text: qsTr("Green") },
                                        { value: "teal", text: qsTr("Teal") },
                                        { value: "violet", text: qsTr("Violet") },
                                        { value: "graphite", text: qsTr("Graphite") }
                                    ]
                                    delegate: BColorSwatch {
                                        required property var modelData
                                        objectName: "introAccent_" + modelData.value
                                        color: ThemePalette.accentSwatch(modelData.value, "", Theme.dark,
                                                                         Theme.dark ? Theme.darkSurface : Theme.lightSurface)
                                        label: modelData.text
                                        selected: Theme.accentName === modelData.value
                                        onClicked: AppSettings.accentColor = modelData.value
                                    }
                                }
                            }
                        }
                        ShellSettingRow {
                            title: qsTr("Text size")
                            BSegmented {
                                objectName: "introFontScale"
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
                        BSwitch {
                            Layout.fillWidth: true
                            text: qsTr("Reduce motion")
                            description: qsTr("Turn off animations.")
                            checked: AppSettings.reducedMotion
                            onToggled: AppSettings.reducedMotion = checked
                        }
                    }
                }

                // ---- 3. Permissions --------------------------------------------------------
                ColumnLayout {
                    objectName: "introStep_permissions"
                    spacing: Theme.space4

                    StepHeader {
                        Layout.bottomMargin: Theme.space2
                        icon: "shield-check"
                        title: qsTr("Permissions")
                        text: AppController.isAndroid
                              ? qsTr("Reminders and alarms ring on time even when Brulion is closed — Android asks you to allow that. Each button opens the system's question or its settings; come back here afterwards.")
                              : AppController.isWasm
                                ? qsTr("The browser shows reminders as notifications once you allow them. They appear while Brulion is open in a tab or installed as an app.")
                                : qsTr("Nothing to allow on this computer: reminders appear as system notifications while Brulion runs. Keep it running in the tray so they ring with the window closed.")
                    }

                    // Android
                    PermissionCard {
                        visible: AppController.isAndroid
                        name: "notifications"
                        icon: "bell"
                        title: qsTr("Notifications")
                        text: qsTr("Shows your reminders, alarms and the quick note.")
                    }
                    PermissionCard {
                        visible: AppController.permissions.exactAlarmsNeeded === true
                        name: "exactAlarms"
                        icon: "alarm-clock"
                        title: qsTr("Exact alarms")
                        text: qsTr("Reminders ring on the minute instead of a few minutes late.")
                    }
                    PermissionCard {
                        visible: AppController.permissions.fullScreenNeeded === true
                        name: "fullScreenAlarms"
                        icon: "bell-ring"
                        title: qsTr("Alarms over the lock screen")
                        text: qsTr("An alarm shows its full screen like a clock app, so you can stop or snooze it at once.")
                    }
                    PermissionCard {
                        visible: AppController.permissions.storagePermissionNeeded === true
                        name: "allFilesAccess"
                        icon: "folder"
                        optional: true
                        title: qsTr("All files access")
                        text: qsTr("Only for notes in a folder on the phone's storage — for example one shared with Syncthing. Notes in the app's own storage do not need it; Brulion asks again when you choose such a folder.")
                    }
                    // The browser
                    PermissionCard {
                        visible: AppController.isWasm && AppController.permissions.notificationsSupported === true
                        name: "notifications"
                        icon: "bell"
                        title: qsTr("Notifications")
                        text: qsTr("The browser asks once. Without it, reminders show only inside the app.")
                    }
                    BCard {
                        Layout.fillWidth: true
                        visible: AppController.isWasm && AppController.permissions.notificationsSupported !== true
                        tone: "warning"
                        padding: Theme.space3
                        BText {
                            width: parent.width
                            text: qsTr("This browser cannot show notifications: reminders show only inside the app.")
                            wrapMode: Text.Wrap
                        }
                    }

                    Group {
                        BSwitch {
                            objectName: "introReminders"
                            Layout.fillWidth: true
                            text: qsTr("Reminders and alarms")
                            description: qsTr("Notify me about ⏰ reminders and ring ⏰! alarms.")
                            iconName: "bell"
                            checked: AppSettings.remindersEnabled
                            onToggled: AppSettings.remindersEnabled = checked
                        }
                        // The desktop: what keeps reminders ringing with the window closed.
                        BSwitch {
                            Layout.fillWidth: true
                            visible: AppController.isDesktop && AppController.trayAvailable
                            text: qsTr("Keep running in the system tray")
                            description: qsTr("Closing the window keeps Brulion in the tray, so reminders still ring.")
                            iconName: "monitor"
                            checked: AppSettings.closeToTray
                            onToggled: AppSettings.closeToTray = checked
                        }
                        BSwitch {
                            Layout.fillWidth: true
                            visible: AppController.autostartAvailability === "available"
                            text: qsTr("Start when you sign in")
                            description: qsTr("Brulion starts hidden in the tray, so reminders ring from the moment you sign in.")
                            iconName: "power"
                            checked: AppController.startAtLogin
                            onToggled: {
                                AppController.startAtLogin = checked
                                checked = Qt.binding(() => AppController.startAtLogin)
                            }
                        }
                        ShellSettingRow {
                            title: qsTr("Try it")
                            description: qsTr("Send a test notification to see how reminders look.")
                            BButton {
                                objectName: "introTestNotification"
                                size: "small"
                                text: qsTr("Test notification")
                                iconName: "bell-ring"
                                enabled: AppSettings.remindersEnabled
                                onClicked: AppController.sendTestNotification()
                            }
                        }
                    }
                }

                // ---- 4. Notes ---------------------------------------------------------------
                ColumnLayout {
                    objectName: "introStep_notes"
                    spacing: Theme.space6

                    StepHeader {
                        icon: "notebook-pen"
                        title: qsTr("How do you like your notes?")
                        text: qsTr("For your new notebook — a notebook you already use keeps its own settings. Change them any time in Settings → Notebook.")
                    }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Daily journal")
                        variant: "title"
                        Accessible.role: Accessible.Heading
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: Theme.compact ? 1 : 2
                        columnSpacing: Theme.space3
                        rowSpacing: Theme.space3
                        ChoiceCard {
                            objectName: "introJournal_outline"
                            icon: "list"
                            title: qsTr("Outline")
                            subtitle: qsTr("Every line is a bullet you can fold, move and link to — like Logseq.")
                            chosen: root.notebookValue("journalMode") === "outline"
                            onClicked: root.setNotebookValue("journalMode", "outline")
                        }
                        ChoiceCard {
                            objectName: "introJournal_document"
                            icon: "file-text"
                            title: qsTr("Document")
                            subtitle: qsTr("Paragraphs, headings and lists, edited in place — like Obsidian.")
                            chosen: root.notebookValue("journalMode") === "document"
                            onClicked: root.setNotebookValue("journalMode", "document")
                        }
                    }
                    Group {
                        ShellSettingRow {
                            title: qsTr("New pages")
                            description: qsTr("The mode a new page starts in; each page can switch later.")
                            BSegmented {
                                objectName: "introPageMode"
                                accessibleName: qsTr("New pages")
                                model: [
                                    { value: "document", text: qsTr("Document"), icon: "file-text" },
                                    { value: "outline", text: qsTr("Outline"), icon: "list" }
                                ]
                                currentValue: root.notebookValue("pageMode")
                                onActivated: (value) => root.setNotebookValue("pageMode", value)
                            }
                        }
                        ShellSettingRow {
                            title: qsTr("Journal file names")
                            description: qsTr("How today's page is named in the folder.")
                            BSegmented {
                                objectName: "introJournalFormat"
                                accessibleName: qsTr("Journal file names")
                                model: ["yyyy-MM-dd", "dd.MM.yyyy", "yyyy_MM_dd"].map(format => ({
                                    value: format,
                                    text: Qt.formatDate(new Date(AppController.today() + "T12:00:00"), format)
                                }))
                                currentValue: root.notebookValue("journalFileFormat")
                                onActivated: (value) => root.setNotebookValue("journalFileFormat", value)
                            }
                        }
                        ShellSettingRow {
                            title: qsTr("First day of the week")
                            BSegmented {
                                objectName: "introWeekStart"
                                accessibleName: qsTr("First day of the week")
                                model: [
                                    { value: 1, text: qsTr("Monday") },
                                    { value: 7, text: qsTr("Sunday") },
                                    { value: 6, text: qsTr("Saturday") }
                                ]
                                currentValue: root.notebookValue("weekStartsOn")
                                onActivated: (value) => root.setNotebookValue("weekStartsOn", value)
                            }
                        }
                        ShellSettingRow {
                            title: qsTr("Default reminder time")
                            description: qsTr("For reminders written without a time.")
                            BButton {
                                objectName: "introReminderTime"
                                size: "small"
                                text: root.notebookValue("defaultReminderTime")
                                iconName: "clock"
                                onClicked: reminderTimeDialog.openFor(root.notebookValue("defaultReminderTime"))
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Footer: Back, Continue ------------------------------------------------------------
    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(root.safe.bottom, KeyboardInsets.overlap)
        implicitHeight: footerRow.implicitHeight + 2 * Theme.space3
        color: Theme.bg

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.borderSubtle
        }
        RowLayout {
            id: footerRow
            anchors.verticalCenter: parent.verticalCenter
            x: Math.round((parent.width - width) / 2)
            width: Math.min(parent.width - 2 * flick.margin, 640)
            spacing: Theme.space2

            BButton {
                objectName: "introBack"
                visible: root.step > 0
                variant: "ghost"
                text: qsTr("Back")
                iconName: "arrow-left"
                onClicked: root.backStep()
            }
            Item { Layout.fillWidth: true }
            BButton {
                objectName: "introContinue"
                variant: "primary"
                text: root.lastStep ? qsTr("Choose a notebook") : qsTr("Continue")
                iconName: "arrow-right"
                iconRight: true
                onClicked: root.nextStep()
            }
        }
    }

    BTimePickerDialog {
        id: reminderTimeDialog
        objectName: "introReminderTimeDialog"
        onPicked: (value) => root.setNotebookValue("defaultReminderTime", value)
    }
}
