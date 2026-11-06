pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Qt.labs.folderlistmodel
import Brulion

// Gallery — every design-system component in its states (`brulion --gallery`).
//
// Light/dark, Polish/English and touch sizes can be switched in the header. With
// `--section <id>` only that section is shown and its popups open by themselves, which is
// what scripts/screenshots.sh uses. Sample texts are Polish on purpose: long words
// ("Przypomnienia", "Synchronizacja", "Powtarzaj co tydzień w poniedziałek" — reminders,
// sync, repeat every week on Monday) must fit.
ApplicationWindow {
    id: window

    width: 1280
    height: 800
    visible: true
    title: "Brulion — galeria komponentów"
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    readonly property list<var> sections: [
        { key: "foundations", title: "Podstawy", icon: "sun", popup: false },
        { key: "icons", title: "Ikony", icon: "image", popup: false },
        { key: "buttons", title: "Przyciski", icon: "square-check-big", popup: false },
        { key: "inputs", title: "Pola tekstowe", icon: "pencil", popup: false },
        { key: "selection", title: "Przełączniki i karty", icon: "list-checks", popup: false },
        { key: "chips", title: "Chipy i plakietki", icon: "tag", popup: false },
        { key: "lists", title: "Listy i karty", icon: "list", popup: false },
        { key: "feedback", title: "Postęp i powiadomienia", icon: "bell", popup: true },
        { key: "menus", title: "Menu", icon: "ellipsis", popup: true },
        { key: "taskstatus", title: "Status zadania", icon: "list-todo", popup: true },
        { key: "dialog", title: "Okno dialogowe", icon: "panel-right", popup: true },
        { key: "confirm", title: "Potwierdzenie", icon: "triangle-alert", popup: true },
        { key: "datepicker", title: "Wybór daty", icon: "calendar", popup: false },
        { key: "timepicker", title: "Wybór godziny", icon: "clock", popup: true },
        { key: "reminder", title: "Przypomnienie", icon: "alarm-clock", popup: true },
        { key: "diff", title: "Porównanie wersji", icon: "file-diff", popup: false },
        { key: "merge", title: "Scalanie konfliktów", icon: "git-merge", popup: false },
        { key: "palette", title: "Paleta poleceń", icon: "command", popup: true },
        { key: "switcher", title: "Szybkie przejście", icon: "file-text", popup: true },
        { key: "split", title: "Podział widoku", icon: "columns-2", popup: false }
    ]
    property string current: DevTools.section !== "" ? DevTools.section : "foundations"
    readonly property bool autoOpen: DevTools.section !== ""
    /// The section shown: its index in `sections` (the first when `current` names none), title.
    readonly property int currentIndex: window.sectionIndex(window.current)
    readonly property string currentTitle: "" + DevTest.valueAt(window.sections, window.currentIndex + ".title")
    function sectionIndex(key: string): int {
        for (let i = 0; i < window.sections.length; ++i) {
            if (DevTest.valueAt(window.sections, i + ".key") === key)
                return i
        }
        return 0
    }

    function accentNames(): list<var> { return DevTest.listOf(ThemePalette.accentNames()) }

    Binding { target: Theme; property: "windowWidth"; value: window.width }
    Binding { target: Theme; property: "windowHeight"; value: window.height }
    Binding { target: DatesHelper; property: "locale"; value: DevTools.language }

    // ---- Header ------------------------------------------------------------------------
    header: Rectangle {
        implicitHeight: Theme.touchUi ? 60 : 52
        color: Theme.bgElevated

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.narrow ? 4 : Theme.space4
            anchors.rightMargin: Theme.narrow ? Theme.space2 : Theme.space4
            spacing: Theme.space3

            BIconButton {
                visible: Theme.narrow
                iconName: "menu"
                label: "Sekcje"
                onClicked: drawer.open()
            }
            Image {
                visible: !Theme.narrow
                source: "qrc:/images/logo.svg"
                sourceSize: Qt.size(28, 28)
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            Text {
                Layout.fillWidth: true
                text: Theme.narrow ? window.currentTitle : "Brulion · system projektowy"
                font.family: Theme.fontUi
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightSemiBold
                color: Theme.fg
                elide: Text.ElideRight
            }
            BSegmented {
                size: "small"
                iconOnly: Theme.narrow
                accessibleName: "Motyw"
                model: [
                    { value: "light", text: "Jasny", icon: "sun" },
                    { value: "dark", text: "Ciemny", icon: "moon" },
                    { value: "system", text: "System", icon: "monitor" }
                ]
                currentValue: Theme.mode
                onActivated: (value) => Theme.mode = value
            }
            // The appearance settings (Settings → Appearance): the "Podstawy" (basics) contrast checks follow.
            BSegmented {
                objectName: "galleryAccent"
                visible: !Theme.narrow
                size: "small"
                accessibleName: "Akcent"
                model: window.accentNames()
                currentValue: Theme.accentName
                onActivated: (value) => Theme.accentName = value
            }
            BSegmented {
                objectName: "gallerySurface"
                visible: !Theme.narrow
                size: "small"
                accessibleName: "Tło"
                model: Theme.dark ? ["slate", "black"] : ["paper", "white"]
                currentValue: Theme.dark ? Theme.darkSurface : Theme.lightSurface
                onActivated: (value) => {
                    if (Theme.dark)
                        Theme.darkSurface = value
                    else
                        Theme.lightSurface = value
                }
            }
            BIconButton {
                visible: !Theme.compact
                iconName: "rows-2"
                label: "Gęstość zwarta"
                checkable: true
                checked: Theme.density === "compact"
                onClicked: Theme.density = Theme.density === "compact" ? "comfortable" : "compact"
            }
            BSegmented {
                visible: !Theme.compact
                size: "small"
                accessibleName: "Język"
                model: [{ value: "pl", text: "PL" }, { value: "en", text: "EN" }]
                currentValue: DevTools.language
                onActivated: (value) => DevTools.setLanguage(value)
            }
            BIconButton {
                visible: !Theme.compact
                iconName: "smartphone"
                label: "Rozmiary dotykowe"
                checkable: true
                checked: Theme.forceTouch
                onClicked: Theme.forceTouch = !Theme.forceTouch
            }
        }
    }

    // ---- Navigation --------------------------------------------------------------------
    component SectionList: ListView {
        id: sectionList
        model: window.sections
        spacing: 1
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        signal chosen()
        header: BSectionHeader {
            width: sectionList.width
            text: "Komponenty"
            count: window.sections.length
        }
        delegate: BListItem {
            required property string key
            required property string title
            required property string icon
            width: sectionList.width
            dense: true
            text: title
            iconName: icon
            selected: window.current === key
            selectionBar: true
            onClicked: {
                window.current = key
                sectionList.chosen()
            }
        }
        T.ScrollBar.vertical: BScrollBar {}
    }

    Drawer {
        id: drawer
        width: Math.min(300, window.width * 0.85)
        height: window.height
        background: Rectangle { color: Theme.bgSunken }
        SectionList {
            anchors.fill: parent
            anchors.margins: Theme.space2
            onChosen: drawer.close()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 248
            visible: !Theme.narrow
            color: Theme.bgSunken
            SectionList {
                anchors.fill: parent
                anchors.margins: Theme.space2
            }
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        Flickable {
            id: page
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: sectionLoader.height + 2 * margin
            boundsBehavior: Flickable.StopAtBounds
            readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8
            T.ScrollBar.vertical: BScrollBar {}

            Loader {
                id: sectionLoader
                x: page.margin
                y: page.margin
                width: Math.min(page.width - 2 * page.margin, 980)
                sourceComponent: {
                    switch (window.current) {
                    case "icons": return iconsSection
                    case "buttons": return buttonsSection
                    case "inputs": return inputsSection
                    case "selection": return selectionSection
                    case "chips": return chipsSection
                    case "lists": return listsSection
                    case "feedback": return feedbackSection
                    case "menus": return menusSection
                    case "taskstatus": return taskStatusSection
                    case "dialog": return dialogSection
                    case "confirm": return confirmSection
                    case "datepicker": return datePickerSection
                    case "timepicker": return timePickerSection
                    case "reminder": return reminderSection
                    case "diff": return diffSection
                    case "merge": return mergeSection
                    case "palette": return paletteSection
                    case "switcher": return switcherSection
                    case "split": return splitSection
                    default: return foundationsSection
                    }
                }
                onLoaded: page.contentY = 0
            }
        }
    }

    BToastHost {
        id: toasts
        parent: T.Overlay.overlay
        anchors.fill: parent
        z: 1000
    }

    // ---- Helpers ------------------------------------------------------------------------
    /// Specimen: a caption above a wrapping row of examples.
    component Specimen: ColumnLayout {
        id: specimen
        property string label
        property string note
        default property alias content: flow.data
        Layout.fillWidth: true
        spacing: Theme.space2
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            Text {
                Layout.fillWidth: specimen.note === ""
                text: specimen.label
                wrapMode: Text.Wrap
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                font.weight: Theme.weightSemiBold
                font.letterSpacing: 0.5
                color: Theme.fgFaint
            }
            Text {
                Layout.fillWidth: true
                visible: specimen.note !== ""
                text: specimen.note
                font.family: Theme.fontUi
                font.pixelSize: Theme.textXs
                color: Theme.fgFaint
                elide: Text.ElideRight
            }
        }
        Flow {
            id: flow
            Layout.fillWidth: true
            spacing: Theme.space2
        }
    }

    function channel(v: real): real {
        return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
    }
    function luminance(r: real, g: real, b: real): real {
        return 0.2126 * window.channel(r) + 0.7152 * window.channel(g) + 0.0722 * window.channel(b)
    }
    function contrast(a: color, b: color): real {
        const ar = a.r
        const ag = a.g
        const ab = a.b
        const br = b.r
        const bg = b.g
        const bb = b.b
        const la = window.luminance(ar, ag, ab)
        const lb = window.luminance(br, bg, bb)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    /// Translucent colours composited over a (solid) background, for contrast checks: what
    /// Qt.tint() gives (its alpha blend).
    function over(c: color, bg: color): color {
        return bg.tint(c)
    }


    // ---- The foundations' specimens (Repeater models; each row has every key its delegate takes) ----
    readonly property list<var> textSwatches: [
        { name: "fg", sample: Theme.fg }, { name: "fgMuted", sample: Theme.fgMuted },
        { name: "fgFaint", sample: Theme.fgFaint }, { name: "link", sample: Theme.link },
        { name: "accentText", sample: Theme.accentText }, { name: "dangerText", sample: Theme.dangerText },
        { name: "warningText", sample: Theme.warningText }, { name: "successText", sample: Theme.successText },
        { name: "tagFg", sample: Theme.tagFg }, { name: "chipReminderFg", sample: Theme.chipReminderFg }
    ]
    readonly property list<var> fillSwatches: [
        { name: "tag", fg: Theme.tagFg, fill: Theme.tagBg },
        { name: "due", fg: Theme.chipDueFg, fill: Theme.chipDueBg },
        { name: "overdue", fg: Theme.chipOverdueFg, fill: Theme.chipOverdueBg },
        { name: "scheduled", fg: Theme.chipScheduledFg, fill: Theme.chipScheduledBg },
        { name: "reminder", fg: Theme.chipReminderFg, fill: Theme.chipReminderBg },
        { name: "alarm", fg: Theme.chipAlarmFg, fill: Theme.chipAlarmBg },
        { name: "recurrence", fg: Theme.chipRecurrenceFg, fill: Theme.chipRecurrenceBg },
        { name: "neutral", fg: Theme.chipNeutralFg, fill: Theme.chipNeutralBg },
        { name: "primaryFg/primary", fg: Theme.primaryFg, fill: Theme.primary },
        { name: "white/danger", fg: Theme.dangerFg, fill: Theme.danger },
        { name: "fgOnAccent/accentSolid", fg: Theme.fgOnAccent, fill: Theme.accentSolid },
        { name: "toast (fgInverse)", fg: Theme.fgInverse, fill: Theme.bgInverse },
        { name: "selected row (fgMuted)", fg: Theme.fgMuted, fill: Theme.bgSelected }
    ]
    /// The controls' states: their mark, the surfaces it lies on and its worst contrast there (the
    /// selected segment's outline only in dark: in light the thumb shows it).
    readonly property list<var> stateSwatchesLight: [
        { name: "przełącznik wyłączony", token: "borderControl", mark: Theme.borderControl, fg: Theme.fg, kind: "switch",
          onNames: "bg, bgElevated, bgSunken",
          worst: Math.min(window.contrast(Theme.borderControl, Theme.bg), window.contrast(Theme.borderControl, Theme.bgElevated),
                          window.contrast(Theme.borderControl, Theme.bgSunken)) },
        { name: "kalendarz: wybrany dzień", token: "primary", mark: Theme.primary, fg: Theme.primaryFg, kind: "disc",
          onNames: "bg", worst: Math.min(99, window.contrast(Theme.primary, Theme.bg)) },
        { name: "kalendarz: dziś", token: "accentSolid", mark: Theme.accentSolid, fg: Theme.fgOnAccent, kind: "disc",
          onNames: "bg", worst: Math.min(99, window.contrast(Theme.accentSolid, Theme.bg)) }
    ]
    readonly property list<var> stateSwatchesDark: [
        { name: "przełącznik wyłączony", token: "borderControl", mark: Theme.borderControl, fg: Theme.fg, kind: "switch",
          onNames: "bg, bgElevated, bgSunken",
          worst: Math.min(window.contrast(Theme.borderControl, Theme.bg), window.contrast(Theme.borderControl, Theme.bgElevated),
                          window.contrast(Theme.borderControl, Theme.bgSunken)) },
        { name: "wybrany segment", token: "borderControl", mark: Theme.borderControl, fg: Theme.fg, kind: "segmented",
          onNames: "bgSunken, bgElevated",
          worst: Math.min(window.contrast(Theme.borderControl, Theme.bgSunken), window.contrast(Theme.borderControl, Theme.bgElevated)) },
        { name: "kalendarz: wybrany dzień", token: "primary", mark: Theme.primary, fg: Theme.primaryFg, kind: "disc",
          onNames: "bg", worst: Math.min(99, window.contrast(Theme.primary, Theme.bg)) },
        { name: "kalendarz: dziś", token: "accentSolid", mark: Theme.accentSolid, fg: Theme.fgOnAccent, kind: "disc",
          onNames: "bg", worst: Math.min(99, window.contrast(Theme.accentSolid, Theme.bg)) }
    ]
    readonly property list<var> surfaceSwatches: [
        { name: "bg", sample: Theme.bg }, { name: "bgElevated", sample: Theme.bgElevated },
        { name: "bgSunken", sample: Theme.bgSunken }, { name: "bgInverse", sample: Theme.bgInverse },
        { name: "border", sample: Theme.border }, { name: "borderStrong", sample: Theme.borderStrong },
        { name: "ink", sample: Theme.ink }, { name: "paper", sample: Theme.paper },
        { name: "marginRed", sample: Theme.marginRed }, { name: "amber", sample: Theme.amber },
        { name: "gridBlue", sample: Theme.gridBlue }, { name: "focusRing", sample: Theme.focusRing }
    ]
    /// BSuggestField's sample: the keys whose title holds `typed` (every key for "").
    readonly property list<var> sampleKeys: [
        { title: "status", subtitle: "12 stron", icon: "type", value: "status" },
        { title: "termin", subtitle: "8 stron", icon: "calendar", value: "termin" },
        { title: "źródło", subtitle: "3 strony", icon: "type", value: "źródło" },
        { title: "koszt", subtitle: "2 strony", icon: "sigma", value: "koszt" }
    ]
    function keySuggestions(typed: string): list<var> {
        return DevTest.rowsContaining(window.sampleKeys, "title", typed)
    }
    /// BDatePicker's sample marks: overdue, due, reminders, scheduled, an alarm and a done task.
    readonly property list<string> overdueMarks: ["overdue"]
    readonly property list<string> todayMarks: ["due", "reminder"]
    readonly property list<string> reminderMarks: ["reminder"]
    readonly property list<string> scheduledMarks: ["scheduled", "due"]
    readonly property list<string> alarmMarks: ["alarm"]
    readonly property list<string> doneMarks: ["done"]
    DevRecord { id: marks }
    function addMark(day: string, kinds: list<string>): void {
        marks.value = Js.withKey(marks.value, day, kinds)
    }
    function sampleMarks(t: string): var {
        marks.value = Js.withKey({}, t, window.todayMarks)
        window.addMark(DatesHelper.addDays(t, -2), window.overdueMarks)
        window.addMark(DatesHelper.addDays(t, 1), window.reminderMarks)
        window.addMark(DatesHelper.addDays(t, 3), window.scheduledMarks)
        window.addMark(DatesHelper.addDays(t, 6), window.alarmMarks)
        window.addMark(DatesHelper.addDays(t, 9), window.doneMarks)
        return marks.value
    }
    readonly property list<int> spaceSizes: [4, 8, 12, 16, 24, 32, 48]
    readonly property list<int> elevations: [0, 1, 2, 3]
    readonly property list<int> iconSizes: [14, 16, 20, 24, 32]
    readonly property list<var> taskSpecimens: [{ status: "todo", text: "Do zrobienia" }, { status: "doing", text: "W toku" },
        { status: "done", text: "Zrobione" }, { status: "deferred", text: "Oczekujące" },
        { status: "cancelled", text: "Anulowane" }]

    // ---- Sections -------------------------------------------------------------------------
    Component {
        id: foundationsSection
        GallerySection {
            title: "Podstawy"
            description: "Kolory z logo: atrament, papier, czerwony margines, bursztyn i niebieska kratka. Każdy kolor tekstu ma kontrast WCAG AA (≥ 4,5:1) na tłach, na których występuje."

            Specimen {
                label: "TEKST NA TŁACH (kontrast)"
                Repeater {
                    model: window.textSwatches
                    delegate: Rectangle {
                        id: swatch
                        required property string name
                        required property color sample
                        width: 172
                        height: 74
                        radius: Theme.radius
                        color: Theme.bgElevated
                        border.width: 1
                        border.color: Theme.border
                        readonly property real worst: Math.min(window.contrast(sample, Theme.bg),
                                                               window.contrast(sample, Theme.bgElevated),
                                                               window.contrast(sample, Theme.bgSunken))
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2
                            Text {
                                text: "Zażółć gęślą jaźń"
                                color: swatch.sample
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textBase
                                font.weight: Theme.weightMedium
                            }
                            Text {
                                text: swatch.name
                                color: Theme.fgMuted
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                            }
                            Text {
                                text: (swatch.worst >= 4.5 ? "AA " : "✗ ") + Js.toFixed(swatch.worst, 1) + ":1"
                                color: swatch.worst >= 4.5 ? Theme.successText : Theme.dangerText
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                                font.weight: Theme.weightSemiBold
                            }
                        }
                    }
                }
            }

            // Text on tinted or solid fills: chips (translucent, so checked over every
            // surface), solid badges, buttons and toasts.
            Specimen {
                label: "TEKST NA WYPEŁNIENIACH (chipy, plakietki, przyciski, powiadomienia)"
                Repeater {
                    model: window.fillSwatches
                    delegate: Rectangle {
                        id: fillSwatch
                        required property string name
                        required property color fg
                        required property color fill
                        width: 172
                        height: 74
                        radius: Theme.radius
                        color: window.over(fill, Theme.bgElevated)
                        border.width: 1
                        border.color: Theme.border
                        readonly property color onBg: window.over(fill, Theme.bg)
                        readonly property color onBgElevated: window.over(fill, Theme.bgElevated)
                        readonly property color onBgSunken: window.over(fill, Theme.bgSunken)
                        readonly property real worst: Math.min(window.contrast(fg, onBg), window.contrast(fg, onBgElevated),
                                                               window.contrast(fg, onBgSunken))
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2
                            Text {
                                text: "Zażółć gęślą jaźń"
                                color: fillSwatch.fg
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textSm
                                font.weight: Theme.weightMedium
                            }
                            Text {
                                text: fillSwatch.name
                                color: fillSwatch.fg
                                opacity: 0.8
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                            }
                            Text {
                                text: (fillSwatch.worst >= 4.5 ? "AA " : "✗ ") + Js.toFixed(fillSwatch.worst, 1) + ":1"
                                color: fillSwatch.fg
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                                font.weight: Theme.weightSemiBold
                            }
                        }
                    }
                }
            }

            // What identifies a control's state (WCAG 1.4.11, ≥ 3:1), worst case over the
            // surfaces it lies on: the off switch's solid track (pages, cards, sunken panels); in
            // dark the segmented control's selection outline, its only frame there, against the
            // track and the card (light: the white thumb); the calendar's discs on a day's number
            // (the selected day, today) on the grids' page.
            Specimen {
                label: "STANY KONTROLEK (≥ 3:1)"
                note: "najgorszy wynik na tłach, na których leżą"
                Repeater {
                    model: Theme.dark ? window.stateSwatchesDark : window.stateSwatchesLight
                    delegate: Rectangle {
                        id: stateSwatch
                        required property string name
                        required property string token
                        required property color mark
                        required property color fg
                        required property string kind
                        required property string onNames
                        required property real worst
                        width: 264
                        height: 104
                        radius: Theme.radius
                        color: Theme.bgElevated
                        border.width: 1
                        border.color: Theme.border
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2
                            Item {
                                width: parent.width
                                height: 32
                                // Both states: the knob's cross and check say it besides the colour.
                                Row {
                                    visible: stateSwatch.kind === "switch"
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.space3
                                    BSwitch {
                                        checked: false
                                        Accessible.name: "wyłączony"
                                    }
                                    BSwitch {
                                        checked: true
                                        Accessible.name: "włączony"
                                    }
                                }
                                BSegmented {
                                    visible: stateSwatch.kind === "segmented"
                                    anchors.verticalCenter: parent.verticalCenter
                                    size: "small"
                                    model: ["Miesiąc", "Tydzień"]
                                    currentValue: "Miesiąc"
                                    accessibleName: "Widok"
                                }
                                Rectangle {
                                    visible: stateSwatch.kind === "disc"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 24
                                    height: 24
                                    radius: 12
                                    color: stateSwatch.mark
                                    Text {
                                        anchors.centerIn: parent
                                        text: "14"
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.textSm
                                        font.weight: Theme.weightBold
                                        color: stateSwatch.fg
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                text: stateSwatch.name + " · " + stateSwatch.token
                                color: Theme.fgMuted
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: "na " + stateSwatch.onNames
                                color: Theme.fgFaint
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                                elide: Text.ElideRight
                            }
                            Text {
                                text: (stateSwatch.worst >= 3 ? "≥ 3:1 " : "✗ ") + Js.toFixed(stateSwatch.worst, 2) + ":1"
                                color: stateSwatch.worst >= 3 ? Theme.successText : Theme.dangerText
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                                font.weight: Theme.weightSemiBold
                            }
                        }
                    }
                }
            }

            Specimen {
                label: "POWIERZCHNIE I MARKA"
                Repeater {
                    model: window.surfaceSwatches
                    delegate: Column {
                        id: surface
                        required property string name
                        required property color sample
                        spacing: 4
                        Rectangle {
                            width: 70
                            height: 52
                            radius: Theme.radius
                            color: surface.sample
                            border.width: 1
                            border.color: Theme.border
                        }
                        Text {
                            text: surface.name
                            color: Theme.fgMuted
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.textXs
                        }
                    }
                }
            }

            Specimen {
                label: "TYPOGRAFIA"
                Column {
                    width: parent.width
                    spacing: Theme.space2
                    BText { variant: "display"; text: "Dziennik — 14 października" }
                    BText { variant: "heading"; text: "Synchronizacja z Nextcloud" }
                    BText { variant: "title"; text: "Powtarzaj co tydzień w poniedziałek" }
                    BText { width: parent.width; wrapMode: Text.Wrap; text: "Treść interfejsu: zażółć gęślą jaźń. Przypomnienia działają offline, a notatki to zwykłe pliki Markdown." }
                    BText { variant: "muted"; text: "Tekst pomocniczy — ostatnia synchronizacja 5 min temu" }
                    BText { variant: "small"; text: "Mały tekst: 12 plików, 3 konflikty" }
                    BText { variant: "mono"; text: "- [ ] Zadzwonić do mamy  2026-10-15 09:00  every week on Monday" }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "Edytor (szeryfowy): Brulion to notatnik w kratkę, w którym myśli układają się w punkty."
                        font.family: Theme.fontEditor
                        font.pixelSize: Theme.textEditor
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightLoose)
                        color: Theme.fg
                    }
                }
            }

            Specimen {
                label: "ODSTĘPY, PROMIENIE, CIENIE"
                Repeater {
                    model: window.spaceSizes
                    delegate: Column {
                        id: spaceSample
                        required property int modelData
                        spacing: 4
                        Rectangle {
                            width: spaceSample.modelData
                            height: 32
                            color: Theme.accentBg
                            border.width: 1
                            border.color: Theme.accent
                        }
                        Text { text: spaceSample.modelData; color: Theme.fgFaint; font.pixelSize: Theme.textXs; font.family: Theme.fontMono }
                    }
                }
                Item { width: Theme.space4; height: 1 }
                Repeater {
                    model: window.elevations
                    delegate: Item {
                        id: elev
                        required property int modelData
                        width: 110
                        height: 78
                        BShadow { target: elevCard; level: elev.modelData }
                        Rectangle {
                            id: elevCard
                            x: 8
                            y: 4
                            width: 94
                            height: 60
                            radius: Theme.radiusLg
                            color: Theme.bgElevated
                            border.width: elev.modelData === 0 ? 1 : 0
                            border.color: Theme.border
                            Text {
                                anchors.centerIn: parent
                                text: "poziom " + elev.modelData
                                color: Theme.fgMuted
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textSm
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iconsSection
        GallerySection {
            title: "Ikony"
            description: "Lucide (licencja ISC), zawsze przez BIcon: dowolny rozmiar i kolor, ostre także w rendererze programowym i w przeglądarce."

            Specimen {
                label: "ROZMIARY I KOLORY"
                Row {
                    spacing: Theme.space3
                    Repeater {
                        model: window.iconSizes
                        delegate: BIcon {
                            required property int modelData
                            anchors.verticalCenter: parent.verticalCenter
                            name: "bell-ring"
                            size: modelData
                            color: Theme.fg
                        }
                    }
                    BIcon { anchors.verticalCenter: parent.verticalCenter; name: "alarm-clock"; size: 24; color: Theme.chipReminderFg }
                    BIcon { anchors.verticalCenter: parent.verticalCenter; name: "repeat"; size: 24; color: Theme.chipRecurrenceFg }
                    BIcon { anchors.verticalCenter: parent.verticalCenter; name: "triangle-alert"; size: 24; color: Theme.dangerText }
                    BIcon { anchors.verticalCenter: parent.verticalCenter; name: "cloud"; size: 24; color: Theme.link }
                }
            }

            Specimen {
                label: "WSZYSTKIE IKONY"
                note: iconModel.count + " plików w assets/icons"
                Repeater {
                    model: FolderListModel {
                        id: iconModel
                        folder: "qrc:/icons"
                        nameFilters: ["*.svg"]
                        showDirs: false
                    }
                    delegate: Rectangle {
                        id: iconCell
                        required property string fileBaseName
                        width: Theme.compact ? 84 : 104
                        height: 72
                        radius: Theme.radius
                        color: iconHover.hovered ? Theme.bgHover : "transparent"
                        HoverHandler { id: iconHover }
                        BIcon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 12
                            name: iconCell.fileBaseName
                            size: 22
                            color: Theme.fg
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 44
                            width: parent.width - 6
                            horizontalAlignment: Text.AlignHCenter
                            text: iconCell.fileBaseName
                            elide: Text.ElideMiddle
                            color: Theme.fgMuted
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                        }
                    }
                }
            }
        }
    }

    Component {
        id: buttonsSection
        GallerySection {
            title: "Przyciski"
            description: "Jeden przycisk główny na widok; drugorzędne obramowane; „duch” w paskach narzędzi; czerwony tylko dla nieodwracalnych akcji."
            // Screenshots show the keyboard focus ring on one button.
            Component.onCompleted: if (window.autoOpen) focusDemoLater.start()
            Later {
                id: focusDemoLater
                onTriggered: focusDemo.forceActiveFocus(Qt.TabFocusReason)
            }

            Specimen {
                label: "WARIANTY"
                BButton { text: "Zapisz"; variant: "primary"; iconName: "check" }
                BButton { id: focusDemo; text: "Synchronizacja"; iconName: "refresh-cw" }
                BButton { text: "Przypomnienia"; variant: "ghost"; iconName: "bell" }
                BButton { text: "Usuń stronę"; variant: "danger"; iconName: "trash-2" }
            }
            Specimen {
                label: "STANY"
                BButton { text: "Wyłączony"; variant: "primary"; enabled: false }
                BButton { text: "Wyłączony"; enabled: false }
                BButton { text: "Synchronizowanie…"; variant: "primary"; busy: true }
                BButton { text: "Zapisywanie"; busy: true }
                BButton { text: "Zaplanowane"; iconName: "chevron-down"; iconRight: true }
            }
            Specimen {
                label: "ROZMIARY"
                BButton { text: "Mały"; size: "small"; iconName: "plus" }
                BButton { text: "Normalny"; iconName: "plus" }
                BButton { text: "Duży"; size: "large"; variant: "primary"; iconName: "plus" }
                BButton { text: "Mały główny"; size: "small"; variant: "primary" }
            }
            Specimen {
                label: "PRZYCISKI Z IKONĄ (z podpowiedzią i etykietą dla czytników ekranu)"
                BIconButton { iconName: "search"; label: "Szukaj"; shortcut: "Ctrl+Shift+F" }
                BIconButton { iconName: "plus"; label: "Nowa strona"; variant: "secondary" }
                BIconButton { iconName: "pin"; label: "Przypnij"; checkable: true; checked: true }
                BIconButton { iconName: "star"; label: "Ulubione"; checkable: true }
                BIconButton { iconName: "check"; label: "Zrobione"; variant: "primary" }
                BIconButton { iconName: "trash-2"; label: "Usuń"; variant: "danger" }
                BIconButton { iconName: "settings"; label: "Ustawienia"; size: "small" }
                BIconButton { iconName: "ellipsis"; label: "Więcej"; enabled: false }
            }
            Specimen {
                label: "PEŁNA SZEROKOŚĆ (telefon)"
                BButton {
                    width: Math.min(parent.width, 420)
                    text: "Synchronizuj teraz z Nextcloud"
                    variant: "primary"
                    iconName: "refresh-cw"
                    size: "large"
                }
            }
        }
    }

    Component {
        id: inputsSection
        GallerySection {
            title: "Pola tekstowe"
            description: "Pola z ikoną, czyszczeniem i komunikatem błędu. Escape czyści pole; pole wyszukiwania pokazuje skrót klawiszowy."
            Component.onCompleted: if (window.autoOpen) focusedFieldLater.start()
            Later {
                id: focusedFieldLater
                onTriggered: focusedField.forceActiveFocus(Qt.TabFocusReason)
            }

            Specimen {
                label: "POLA"
                BTextField { width: 280; placeholderText: "Nazwa strony" }
                BTextField { id: focusedField; width: 280; text: "Plan podróży do Gdańska"; leadingIcon: "file-text"; clearable: true }
                BTextField { width: 280; text: "nextcloud.example"; leadingIcon: "cloud"; errorText: "Adres musi zaczynać się od https://" }
                BTextField { width: 280; text: "Tylko do odczytu"; readOnly: true }
                BTextField { width: 280; placeholderText: "Wyłączone"; enabled: false }
                BTextField { width: 180; size: "small"; placeholderText: "Mały, np. 18:30" }
            }
            Specimen {
                label: "WYSZUKIWANIE"
                BSearchField { width: 340; placeholderText: "Szukaj w notatkach…"; shortcut: "Ctrl+Shift+F" }
                BSearchField { width: 340; text: "żółw"; placeholderText: "Szukaj…" }
            }
            Specimen {
                label: "Z PODPOWIEDZIAMI"
                BSuggestField {
                    width: 280
                    size: "small"
                    placeholderText: "Nazwa właściwości"
                    // ↓ / ↑ choose, Enter or Tab takes the suggestion, Escape closes.
                    id: keySuggest
                    suggesting: true
                    onSuggestionsRequested: (typed) => keySuggest.suggestions = window.keySuggestions(typed)
                }
            }
            Specimen {
                label: "TEKST WIELOWIERSZOWY"
                BTextArea {
                    width: Math.min(parent.width, 560)
                    height: 120
                    placeholderText: "Opis…"
                    text: "Spotkanie z zespołem w czwartek.\nUstalić termin synchronizacji i podział zadań na październik."
                }
                BTextArea {
                    width: Math.min(parent.width, 560)
                    height: 90
                    monospace: true
                    placeholderText: "Kod lub scalony tekst…"
                }
            }
        }
    }

    Component {
        id: selectionSection
        GallerySection {
            title: "Przełączniki i karty"
            description: "Przełączniki dla ustawień działających od razu, pola wyboru dla list, segmenty dla kilku wykluczających się opcji."

            Specimen {
                label: "PRZEŁĄCZNIKI"
                ColumnLayout {
                    width: Math.min(parent.width, 460)
                    spacing: Theme.space2
                    BSwitch {
                        Layout.fillWidth: true
                        text: "Alarm (głośny, do wyłączenia)"
                        description: "Dzwoni jak budzik zamiast cichego powiadomienia."
                        iconName: "alarm-clock"
                        checked: true
                    }
                    BSwitch { Layout.fillWidth: true; text: "Synchronizacja w tle"; iconName: "refresh-cw" }
                    BSwitch { Layout.fillWidth: true; text: "Wyłączone"; enabled: false; checked: true }
                }
            }
            Specimen {
                label: "POLA WYBORU"
                ColumnLayout {
                    width: Math.min(parent.width, 460)
                    spacing: 0
                    BCheckBox { Layout.fillWidth: true; text: "Pokaż ukończone zadania"; checked: true }
                    BCheckBox { Layout.fillWidth: true; text: "Zaplanowane"; description: "Zadania z datą rozpoczęcia w przyszłości." }
                    BCheckBox { Layout.fillWidth: true; text: "Zaznacz wszystkie (część)"; tristate: true; checkState: Qt.PartiallyChecked }
                    BCheckBox { Layout.fillWidth: true; text: "Wyłączone"; enabled: false }
                }
            }
            Specimen {
                label: "SEGMENTY"
                BSegmented {
                    model: [
                        { value: "day", text: "Dzień", icon: "" },
                        { value: "week", text: "Tydzień", icon: "" },
                        { value: "month", text: "Miesiąc", icon: "" }
                    ]
                    currentValue: "week"
                }
                BSegmented {
                    model: [
                        { value: "light", text: "Jasny", icon: "sun" },
                        { value: "dark", text: "Ciemny", icon: "moon" },
                        { value: "system", text: "System", icon: "monitor" }
                    ]
                    currentValue: "system"
                }
                BSegmented {
                    size: "small"
                    iconOnly: true
                    model: [
                        { value: "split", text: "Obok siebie", icon: "columns-2" },
                        { value: "unified", text: "Jeden pod drugim", icon: "rows-2" }
                    ]
                    currentValue: "split"
                }
            }
            Specimen {
                label: "KARTY"
                BTabBar {
                    width: Math.min(parent.width, 560)
                    currentIndex: 0
                    BTabButton { text: "Odnośniki"; iconName: "link"; count: 12 }
                    BTabButton { text: "Spis treści"; iconName: "list" }
                    BTabButton { text: "Przypomnienia"; iconName: "bell"; count: 3 }
                }
            }
        }
    }

    Component {
        id: chipsSection
        GallerySection {
            title: "Chipy i plakietki"
            description: "Chipy w tekście notatki: terminy, przypomnienia, powtarzanie, tagi. Interaktywne chipy służą też jako filtry i szybkie wybory."

            Specimen {
                label: "WARIANTY"
                BChip { variant: "tag"; text: "projekt-dom" }
                BChip { variant: "due"; text: "jutro" }
                BChip { variant: "overdue"; text: "wt 13 paź" }
                BChip { variant: "scheduled"; text: "Zaplanowane: pt 16 paź" }
                BChip { variant: "reminder"; text: "dziś 18:00" }
                BChip { variant: "alarm"; text: "Alarm 07:00" }
                BChip { variant: "recurrence"; text: "Powtarzaj co tydzień w poniedziałek" }
                BChip { variant: "done"; text: "Zrobione 12 paź" }
                BChip { variant: "neutral"; text: "Synchronizacja" }
            }
            Specimen {
                label: "MAŁE (W TEKŚCIE)"
                Text {
                    height: 22
                    verticalAlignment: Text.AlignVCenter
                    text: "Zadzwonić do mamy"
                    font.family: Theme.fontEditor
                    font.pixelSize: Theme.textEditor
                    color: Theme.fg
                }
                BChip { size: "small"; variant: "reminder"; text: "jutro 09:00" }
                BChip { size: "small"; variant: "recurrence"; text: "co tydzień" }
                BChip { size: "small"; variant: "tag"; text: "rodzina" }
            }
            Specimen {
                label: "USUWALNE I WYBIERALNE"
                BChip { variant: "tag"; text: "praca"; removable: true }
                BChip { variant: "tag"; text: "Przypomnienia"; removable: true }
                BChip { text: "za 10 min"; iconName: "clock"; interactive: true }
                BChip { text: "jutro 8:00"; iconName: "clock"; interactive: true; selected: true }
                BChip { text: "w poniedziałek 9:00"; iconName: "clock"; interactive: true }
                BChip { text: "dziś 19:00"; iconName: "clock"; interactive: true; enabled: false }
            }
            Specimen {
                label: "PLAKIETKI"
                Row {
                    spacing: Theme.space2
                    BBadge { anchors.verticalCenter: parent.verticalCenter; count: 3 }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; count: 12; variant: "accent" }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; count: 250; variant: "danger" }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; text: "Nowe"; variant: "info" }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; text: "Offline"; variant: "warning" }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; text: "Zsynchronizowano"; variant: "success" }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; dot: true; variant: "accent" }
                    BBadge { anchors.verticalCenter: parent.verticalCenter; dot: true; variant: "warning" }
                }
            }
            Specimen {
                label: "SKRÓTY KLAWISZOWE"
                note: "(ukryte na ekranach dotykowych)"
                BKbd { sequence: "Ctrl+K"; alwaysVisible: true }
                BKbd { sequence: "Ctrl+Shift+F"; alwaysVisible: true }
                BKbd { sequence: "Alt+←"; alwaysVisible: true }
                BKbd { sequence: "Esc"; alwaysVisible: true }
                BKbd { sequence: "Ctrl++"; alwaysVisible: true }
            }
        }
    }

    Component {
        id: listsSection
        GallerySection {
            title: "Listy i karty"
            description: "Wiersze paska bocznego i wyników, nagłówki grup, ścieżka nawigacji, karty i puste stany."

            GridLayout {
                Layout.fillWidth: true
                columns: Theme.compact ? 1 : 2
                columnSpacing: Theme.space5
                rowSpacing: Theme.space5

                BCard {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    padding: Theme.space2
                    tone: "sunken"
                    contentItem: Column {
                        spacing: 1
                        BListItem { width: parent.width; iconName: "book-open"; text: "Dziennik"; selected: true; selectionBar: true }
                        BListItem { width: parent.width; iconName: "calendar"; text: "Kalendarz" }
                        BListItem { width: parent.width; iconName: "square-check-big"; text: "Zadania"; count: 7 }
                        BListItem { width: parent.width; iconName: "bell"; text: "Przypomnienia"; count: 2 }
                        BListItem { width: parent.width; iconName: "files"; text: "Wszystkie strony" }
                        BListItem { width: parent.width; iconName: "waypoints"; text: "Graf" }
                        BSectionHeader {
                            width: parent.width
                            text: "Ulubione"
                            count: 3
                            collapsible: true
                            actionIcon: "plus"
                            actionLabel: "Dodaj do ulubionych"
                        }
                        BListItem { width: parent.width; iconName: "star"; text: "Plan podróży do Gdańska"; dense: true }
                        BListItem { width: parent.width; iconName: "star"; text: "Synchronizacja — notatki techniczne"; dense: true }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.space4

                    BCard {
                        Layout.fillWidth: true
                        padding: Theme.space2
                        contentItem: Column {
                            spacing: 1
                            BListItem {
                                width: parent.width
                                iconName: "file-text"
                                text: "Powtarzaj co tydzień w poniedziałek"
                                subtitle: "Projekty/Dom/Remont łazienki"
                                trailing: BKbd { sequence: "Ctrl+1"; small: true }
                            }
                            BListItem {
                                width: parent.width
                                iconName: "file-text"
                                text: "Synchronizacja"
                                subtitle: "Notatki/Techniczne"
                                selected: true
                            }
                            BListItem {
                                width: parent.width
                                iconName: "file-text"
                                text: "Wyłączony element"
                                subtitle: "Archiwum"
                                enabled: false
                            }
                        }
                    }

                    BBreadcrumb {
                        Layout.fillWidth: true
                        model: ["Notatki", "Projekty", "Dom", "Remont łazienki", "Zaplanowane"]
                    }
                    BBreadcrumb {
                        Layout.preferredWidth: 300
                        model: ["Notatki", "Projekty", "Dom", "Remont łazienki", "Przypomnienia i zadania"]
                    }

                    BCard {
                        Layout.fillWidth: true
                        tone: "warning"
                        contentItem: RowLayout {
                            spacing: Theme.space3
                            BIcon { name: "cloud-off"; color: Theme.warningText; size: Theme.iconSizeLg }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                BText { variant: "label"; text: "Brak połączenia z Nextcloud" }
                                BText { Layout.fillWidth: true; variant: "muted"; wrapMode: Text.Wrap; text: "Zmiany są zapisane na tym urządzeniu i wyślą się same." }
                            }
                        }
                    }
                    BCard {
                        Layout.fillWidth: true
                        elevation: 1
                        clickable: true
                        contentItem: RowLayout {
                            spacing: Theme.space3
                            BIcon { name: "history"; size: Theme.iconSizeLg; color: Theme.fgMuted }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                BText { variant: "label"; text: "Historia wersji" }
                                BText { variant: "muted"; text: "14 wersji · ostatnia 5 min temu" }
                            }
                            BIcon { name: "chevron-right"; color: Theme.fgFaint }
                        }
                    }
                }
            }

            BCard {
                Layout.fillWidth: true
                implicitHeight: Math.max(280, emptyDemo.implicitHeight + 2 * Theme.space6)
                contentItem: Item {
                    BEmptyState {
                        id: emptyDemo
                        anchors.centerIn: parent
                        iconName: "list-todo"
                        title: "Brak zadań na dziś"
                        description: "Dodaj zadanie w dzienniku, wpisując „- [ ]”, albo zaplanuj je na inny dzień."
                        actionText: "Otwórz dziennik"
                        actionIcon: "book-open"
                        secondaryActionText: "Pokaż zaplanowane"
                    }
                }
            }
        }
    }

    Component {
        id: feedbackSection
        GallerySection {
            id: feedback
            title: "Postęp i powiadomienia"
            description: "Wskaźniki aktywności, paski postępu, dymki podpowiedzi i powiadomienia (toast) z akcją cofnięcia."

            Component.onCompleted: {
                if (window.autoOpen) {
                    toasts.showToast("Zsynchronizowano 12 plików z Nextcloud", "success", "", 0)
                    toasts.showToast("Przeniesiono „Plan podróży” do kosza", "", "Cofnij", 0)
                    tipTimer.start()
                }
            }
            Timer {
                id: tipTimer
                interval: 50
                onTriggered: tipButton.forceTip = true
            }

            Specimen {
                label: "WSKAŹNIKI"
                Row {
                    spacing: Theme.space4
                    BSpinner { anchors.verticalCenter: parent.verticalCenter; size: 16 }
                    BSpinner { anchors.verticalCenter: parent.verticalCenter; size: 24 }
                    BSpinner { anchors.verticalCenter: parent.verticalCenter; size: 32; color: Theme.link }
                    BButton { anchors.verticalCenter: parent.verticalCenter; text: "Synchronizowanie…"; busy: true }
                }
            }
            Specimen {
                label: "POSTĘP"
                Column {
                    width: Math.min(parent.width, 420)
                    spacing: Theme.space3
                    BText { variant: "small"; text: "Wysyłanie: 12 z 40 plików" }
                    BProgressBar { width: parent.width; value: 0.3 }
                    BText { variant: "small"; text: "Łączenie z serwerem…" }
                    BProgressBar { width: parent.width; indeterminate: true }
                    BText { variant: "small"; text: "Gotowe" }
                    BProgressBar { width: parent.width; value: 1; tone: "success" }
                }
            }
            Specimen {
                label: "PODPOWIEDŹ"
                Item {
                    width: 260
                    height: 84
                    BIconButton {
                        id: tipButton
                        property bool forceTip: false
                        anchors.bottom: parent.bottom
                        x: 40
                        iconName: "refresh-cw"
                        label: "Synchronizuj teraz"
                        shortcut: "Ctrl+R"
                        variant: "secondary"
                        showToolTip: !forceTip
                        BToolTip {
                            visible: tipButton.forceTip
                            delay: 0
                            text: tipButton.label
                            shortcut: tipButton.shortcut
                        }
                    }
                }
            }
            Specimen {
                label: "POWIADOMIENIA (statyczne)"
                Column {
                    width: Math.min(parent.width, 480)
                    spacing: Theme.space2
                    BToast { width: parent.width; text: "Zapisano zmiany"; kind: "info" }
                    BToast { width: parent.width; text: "Synchronizacja zakończona — 3 pliki zaktualizowane"; kind: "success" }
                    BToast { width: parent.width; text: "Konflikt w „Lista zakupów” — zachowano obie wersje"; kind: "warning"; actionText: "Rozwiąż" }
                    BToast { width: parent.width; text: "Nie udało się połączyć z serwerem: przekroczono limit czasu. Spróbujemy ponownie za 5 minut."; kind: "error" }
                }
            }
            Specimen {
                label: "NA ŻYWO"
                BButton {
                    text: "Pokaż powiadomienie"
                    onClicked: window.undoToast = toasts.showToast("Przeniesiono do kosza", "", "Cofnij", -1)
                }
                BButton {
                    text: "Błąd"
                    variant: "ghost"
                    onClicked: toasts.showToast("Nie można zapisać pliku", "error", "", -1)
                }
            }
        }
    }

    Component {
        id: menusSection
        GallerySection {
            title: "Menu"
            description: "Menu kontekstowe z ikonami, skrótami, opcjami do zaznaczenia i akcją niebezpieczną. Strzałki, Enter i Escape działają z klawiatury."
            Component.onCompleted: if (window.autoOpen) pageMenuLater.start()
            Later {
                id: pageMenuLater
                onTriggered: pageMenu.popup(moreButton, 0, moreButton.height + 4)
            }

            Specimen {
                label: "MENU STRONY"
                Item {
                    width: 320
                    height: 380
                    BButton {
                        id: moreButton
                        text: "Więcej"
                        iconName: "ellipsis"
                        onClicked: pageMenu.popup(moreButton, 0, moreButton.height + 4)
                    }
                    BMenu {
                        id: pageMenu
                        BMenuItem { text: "Zmień nazwę"; iconName: "pencil"; shortcut: "F2" }
                        BMenuItem { text: "Kopiuj odnośnik"; iconName: "link"; shortcut: "Ctrl+Shift+C" }
                        BMenuItem { text: "Dodaj przypomnienie…"; iconName: "bell" }
                        BMenuItem { text: "Historia wersji"; iconName: "history" }
                        BMenuSeparator {}
                        BMenuItem { text: "Przypnij do ulubionych"; checkable: true; checked: true }
                        BMenuItem { text: "Tryb źródła"; checkable: true; shortcut: "Ctrl+E" }
                        BMenuSeparator {}
                        BMenuItem { text: "Przenieś do kosza"; iconName: "trash-2"; danger: true; shortcut: "Del" }
                    }
                }
            }
        }
    }

    Component {
        id: taskStatusSection
        GallerySection {
            title: "Status zadania"
            description: "Pole zadania: kliknięcie odhacza, prawy przycisk myszy albo przytrzymanie palcem otwiera menu statusów (w edytorze także Ctrl+Shift+Enter i „/” z poleceniem). „Bez statusu” usuwa pole wyboru, tekst zostaje."
            Component.onCompleted: if (window.autoOpen) doingBoxLater.start()
            Later {
                id: doingBoxLater
                onTriggered: doingBox.openMenu()
            }

            Specimen {
                label: "POLA ZADAŃ"
                note: "TaskBox"
                Repeater {
                    model: window.taskSpecimens
                    delegate: Row {
                        id: specimenRow
                        required property string status
                        required property string text
                        spacing: Theme.space1
                        TaskBox {
                            id: specimenBox
                            anchors.verticalCenter: parent.verticalCenter
                            status: specimenRow.status
                            onToggled: toasts.showToast("Kliknięcie: " + specimenRow.text, "info", "", -1)
                            onStatusChosen: (status) => specimenBox.status = status === "" ? "todo" : status
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: specimenRow.text
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textBase
                            color: Theme.fg
                        }
                    }
                }
            }
            Specimen {
                label: "MENU STATUSU"
                note: "TaskStatusMenu"
                Item {
                    width: 320
                    height: 360
                    Row {
                        spacing: Theme.space1
                        TaskBox {
                            id: doingBox
                            anchors.verticalCenter: parent.verticalCenter
                            status: "doing"
                            onStatusChosen: (status) => doingBox.status = status === "" ? "todo" : status
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Pomalować ścianę w łazience"
                            font.family: Theme.fontEditor
                            font.pixelSize: Theme.textEditor
                            color: Theme.fg
                        }
                    }
                }
            }
        }
    }

    Component {
        id: dialogSection
        GallerySection {
            title: "Okno dialogowe"
            description: "Na szerokim oknie karta na środku; na wąskim panel od dołu z uchwytem (przeciągnij w dół, aby zamknąć). Escape i kliknięcie obok anulują, Ctrl+Enter zatwierdza."
            Component.onCompleted: if (window.autoOpen) renameDialog.open()

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Zmień nazwę strony"; iconName: "pencil"; onClicked: renameDialog.open() }
            }
            BDialog {
                id: renameDialog
                title: "Zmień nazwę strony"
                subtitle: "Odnośniki do tej strony zostaną zaktualizowane we wszystkich notatkach."
                acceptText: "Zmień nazwę"
                acceptEnabled: Js.trim(nameField.text) !== ""
                initialFocusItem: nameField
                BTextField {
                    id: nameField
                    Layout.fillWidth: true
                    text: "Plan podróży do Gdańska"
                    leadingIcon: "file-text"
                    clearable: true
                }
                BCheckBox {
                    Layout.fillWidth: true
                    text: "Zaktualizuj odnośniki (14 stron)"
                    checked: true
                }
                onAccepted: toasts.showToast("Zmieniono nazwę na „" + nameField.text + "”", "success", "", -1)
            }
        }
    }

    Component {
        id: confirmSection
        GallerySection {
            title: "Potwierdzenie"
            description: "Przed nieodwracalnymi akcjami. Wariant niszczący ma czerwony przycisk i ikonę ostrzeżenia."
            Component.onCompleted: if (window.autoOpen) confirm.open()

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Usuń stronę"; variant: "danger"; iconName: "trash-2"; onClicked: confirm.open() }
            }
            BConfirmDialog {
                id: confirm
                title: "Usunąć stronę „Plan podróży”?"
                message: "Strona trafi do kosza. Możesz ją przywrócić przez 30\u00a0dni."
                detail: "Na tę stronę wskazuje 14 odnośników."
                confirmText: "Przenieś do kosza"
                destructive: true
                checkText: "Nie pytaj ponownie"
            }
        }
    }

    Component {
        id: datePickerSection
        GallerySection {
            title: "Wybór daty"
            description: "Kalendarz z polskimi nazwami, tydzień od poniedziałku, znaczniki terminów i przypomnień, wpisywanie daty słowami z podglądem na żywo."

            GridLayout {
                Layout.fillWidth: true
                columns: Theme.compact ? 1 : 2
                columnSpacing: Theme.space6
                rowSpacing: Theme.space6

                BCard {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: Theme.compact
                    elevation: 1
                    contentItem: BDatePicker {
                        id: fullPicker
                        withTime: true
                        selectedDate: DatesHelper.addDays(DatesHelper.today, 1)
                        time: "09:00"
                        marks: window.sampleMarks(DatesHelper.today)
                        Component.onCompleted: naturalText = "w piątek o 18"
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    spacing: Theme.space3
                    BText { variant: "label"; text: "Wybrana wartość" }
                    BChip { variant: "due"; text: fullPicker.value !== "" ? DatesHelper.formatShort(fullPicker.value) : "—" }
                    BText { variant: "mono"; text: fullPicker.value }
                    BText {
                        Layout.fillWidth: true
                        variant: "muted"
                        wrapMode: Text.Wrap
                        text: "Spróbuj: „jutro o 9”, „za 2 godziny”, „15 października”, „w przyszły wtorek wieczorem”, „next friday 6pm”."
                    }
                    BCard {
                        Layout.alignment: Qt.AlignTop
                        tone: "sunken"
                        contentItem: BDatePicker {
                            showNaturalInput: false
                            selectedDate: DatesHelper.today
                        }
                    }
                }
            }
        }
    }

    Component {
        id: timePickerSection
        GallerySection {
            title: "Wybór godziny"
            description: "Zegar 24-godzinny do klikania i przeciągania, jak w Androidzie. Godziny 00–11 są na zewnętrznym pierścieniu, 12–23 na wewnętrznym; po wybraniu godziny zegar przechodzi do minut. U góry można przełączać godziny i minuty, a strzałki zmieniają wartość z klawiatury."
            Component.onCompleted: if (window.autoOpen) timeDialog.openFor(galleryClock.time)

            GridLayout {
                Layout.fillWidth: true
                columns: Theme.compact ? 1 : 2
                columnSpacing: Theme.space6
                rowSpacing: Theme.space6

                BCard {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: Theme.compact
                    contentItem: BTimePicker {
                        id: galleryClock
                        time: "18:30"
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    spacing: Theme.space3
                    BText { variant: "label"; text: "Wybrana godzina" }
                    BChip { variant: "reminder"; text: galleryClock.displayTime }
                    BText { variant: "mono"; text: galleryClock.displayTime }
                    BButton {
                        text: "Otwórz zegar w oknie"
                        iconName: "clock"
                        onClicked: timeDialog.openFor(galleryClock.time)
                    }
                    BText {
                        Layout.fillWidth: true
                        variant: "muted"
                        wrapMode: Text.Wrap
                        text: "Strzałki ↑/↓ zmieniają wartość, PageUp/PageDown o pięć, ←/→ przełączają godziny i minuty. Zegar otwiera się też przyciskiem „Zegar” w wyborze daty i w oknie przypomnienia."
                    }
                }
            }

            BTimePickerDialog {
                id: timeDialog
                onPicked: (value) => galleryClock.time = value
            }
        }
    }

    Component {
        id: reminderSection
        GallerySection {
            title: "Przypomnienie"
            description: "Szybkie wybory, data słowami lub z kalendarza, godzina w formacie 24 h, alarm i powtarzanie. Wynik: { at, alarm, recurrence } z regułą w kanonicznym języku angielskim."
            Component.onCompleted: if (window.autoOpen) reminder.editWith(DatesHelper.addDays(DatesHelper.today, 1) + " 09:00", true,
                                                                       "every week on Monday, Thursday",
                                                                       "Zadzwonić do mamy w sprawie urodzin")

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Nowe przypomnienie"; iconName: "bell"; onClicked: reminder.create("Kupić bilety na pociąg") }
                BButton {
                    text: "Edytuj przypomnienie"
                    iconName: "pencil"
                    onClicked: reminder.editWith(DatesHelper.today + " 18:00", false, "every 2 weeks on Friday", "Podlać kwiaty")
                }
            }
            Specimen {
                label: "OSTATNI WYNIK"
                BText { id: reminderResult; variant: "mono"; text: "—" }
            }
            BReminderDialog {
                id: reminder
                onReminderAccepted: (r) => reminderResult.text = Js.stringify(r)
                onDeleteRequested: toasts.showToast("Usunięto przypomnienie", "", "Cofnij", -1)
            }
        }
    }

    /// The toast whose "Cofnij" (undo) shows "Przywrócono" (restored) — the feedback section's live
    /// toast, with the gallery's Polish sample texts.
    property int undoToast: -1
    Connections {
        target: toasts
        function onActionTriggered(toastId: int) {
            if (toastId === window.undoToast)
                toasts.showToast("Przywrócono", "success", "", -1)
        }
    }

    readonly property string sampleOld: "# Plan podróży\n\n- Pociąg do Gdańska w piątek o 7:15\n- Zarezerwować nocleg na dwie noce\n- Zabrać ładowarkę i parasol\n- Kupić prezent dla babci\n\n## Zwiedzanie\n\n- Długi Targ\n- Westerplatte\n- Muzeum II Wojny Światowej\n- Spacer po Oliwie\n- Molo w Sopocie\n\n## Budżet\n\nNocleg: 380 zł\nBilety: 2 × 89 zł"
    readonly property string sampleNew: "# Plan podróży\n\n- Pociąg do Gdańska w czwartek o 7:15\n- Zarezerwować nocleg na trzy noce\n- Zabrać ładowarkę i parasol\n\n## Zwiedzanie\n\n- Długi Targ\n- Westerplatte\n- Muzeum II Wojny Światowej\n- Spacer po Oliwie\n- Molo w Sopocie\n- Europejskie Centrum Solidarności\n\n## Budżet\n\nNocleg: 570 zł\nBilety: 2 × 89 zł"

    Component {
        id: diffSection
        GallerySection {
            title: "Porównanie wersji"
            description: "Różnice wierszami z podświetleniem zmienionych słów; widok obok siebie na szerokim oknie, jeden pod drugim na telefonie. Długie niezmienione fragmenty są zwinięte."

            BDiffView {
                Layout.fillWidth: true
                Layout.preferredHeight: 440
                oldText: window.sampleOld
                newText: window.sampleNew
                oldLabel: "Wersja z 12:40"
                newLabel: "Obecna"
                context: 2
            }
            BDiffView {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                mode: "unified"
                oldText: window.sampleOld
                newText: window.sampleNew
                oldLabel: "Laptop"
                newLabel: "Ten telefon"
                context: 1
            }
        }
    }

    Component {
        id: mergeSection
        GallerySection {
            title: "Scalanie konfliktów"
            description: "Gdy plik zmienił się na dwóch urządzeniach: dla każdego konfliktu wybierz „Moja”, „Ich”, „Obie” albo popraw tekst ręcznie."

            BMergeView {
                id: merge
                Layout.fillWidth: true
                Layout.preferredHeight: 620
                ours: window.sampleOld
                theirs: window.sampleNew
                oursLabel: "Ten telefon"
                theirsLabel: "Laptop (Nextcloud)"
                applyText: "Zapisz scalony"
                Component.onCompleted: mergeLater.start()
                onResolved: (text) => toasts.showToast("Scalono (" + text.length + " znaków)", "success", "", -1)
                Later {
                    id: mergeLater
                    onTriggered: {
                        merge.setChoice(0, "theirs")
                        merge.setChoice(2, "both")
                    }
                }
            }
        }
    }

    readonly property list<var> sampleCommands: [
        { id: "0", title: "Dzisiejszy dziennik", icon: "book-open", shortcut: "Ctrl+J" },
        { id: "1", title: "Nowa strona", icon: "square-pen", shortcut: "Ctrl+N" },
        { id: "2", title: "Synchronizuj teraz", subtitle: "Nextcloud · ostatnio 5 min temu", icon: "refresh-cw", shortcut: "Ctrl+R" },
        { id: "3", title: "Synchronizacja — ustawienia", subtitle: "Ustawienia", icon: "settings" },
        { id: "4", title: "Przypomnienia", subtitle: "Widok", icon: "bell" },
        { id: "5", title: "Kalendarz", subtitle: "Widok", icon: "calendar" },
        { id: "6", title: "Pokaż graf powiązań", icon: "waypoints" },
        { id: "7", title: "Przełącz tryb źródła", icon: "code", shortcut: "Ctrl+E" },
        { id: "8", title: "Motyw: ciemny", icon: "moon" },
        { id: "9", title: "Importuj z Obsidiana…", icon: "import" },
        { id: "10", title: "Eksportuj do ZIP…", icon: "download" },
        { id: "11", title: "Kosz", icon: "trash-2" },
        { id: "12", title: "Zaplanowane zadania", subtitle: "Widok zadań", icon: "list-todo" }
    ]
    readonly property list<var> samplePages: [
        { title: "Plan podróży do Gdańska", subtitle: "Projekty/Podróże" },
        { title: "Lista zakupów", subtitle: "Dom" },
        { title: "Przepis na pierogi z kapustą", subtitle: "Kuchnia" },
        { title: "Synchronizacja — notatki techniczne", subtitle: "Notatki/Techniczne" },
        { title: "Planowanie budżetu 2027", subtitle: "Finanse" },
        { title: "Książki do przeczytania", subtitle: "" },
        { title: "Żółw Fryderyk — weterynarz", subtitle: "Dom/Zwierzęta" },
        { title: "Spotkanie z zespołem", subtitle: "Praca/Spotkania" },
        { title: "Pomysły na prezent", subtitle: "Rodzina" }
    ]

    Component {
        id: paletteSection
        GallerySection {
            title: "Paleta poleceń"
            description: "Ctrl+K: wyszukiwanie przybliżone, bez względu na polskie znaki („zolw” znajduje „Żółw”). Strzałki, Enter, Escape."
            Component.onCompleted: if (window.autoOpen) paletteLater.start()
            Later {
                id: paletteLater
                onTriggered: {
                    palette.open()
                    palette.query = "syn"
                }
            }

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Paleta poleceń"; iconName: "command"; onClicked: palette.open() }
                BKbd { sequence: "Ctrl+K" }
            }
            BCommandPalette {
                id: palette
                commands: window.sampleCommands
                onCommandIdActivated: (id) => toasts.showToast("" + DevTest.valueAt(window.sampleCommands, id + ".title"), "", "", -1)
            }
        }
    }

    Component {
        id: switcherSection
        GallerySection {
            title: "Szybkie przejście"
            description: "Ctrl+O: przejdź do strony albo utwórz nową, gdy nic nie pasuje dokładnie."
            Component.onCompleted: if (window.autoOpen) switcherLater.start()
            Later {
                id: switcherLater
                onTriggered: {
                    switcher.open()
                    switcher.query = "pla"
                }
            }

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Przejdź do strony"; iconName: "file-text"; onClicked: switcher.open() }
            }
            BQuickSwitcher {
                id: switcher
                pages: window.samplePages
                onPageChosen: (p) => toasts.showToast("Otwieram „" + DevTest.valueAt(p, "title") + "”", "", "", -1)
                onCreateRequested: (t) => toasts.showToast("Tworzę stronę „" + t + "”", "success", "", -1)
            }
        }
    }

    Component {
        id: splitSection
        GallerySection {
            title: "Podział widoku"
            description: "Uchwyt SplitView: 1 px linii, szeroki obszar chwytania, podświetlenie przy najechaniu i przeciąganiu."

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 360
                radius: Theme.radiusLg
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.border
                clip: true

                SplitView {
                    anchors.fill: parent
                    anchors.margins: 1
                    orientation: Qt.Horizontal
                    handle: BSplitHandle { orientation: Qt.Horizontal }

                    Rectangle {
                        SplitView.preferredWidth: Theme.compact ? 150 : 220
                        SplitView.minimumWidth: 160
                        color: Theme.bgSunken
                        Column {
                            anchors.fill: parent
                            anchors.margins: Theme.space2
                            spacing: 1
                            BListItem { width: parent.width; iconName: "book-open"; text: "Dziennik"; selected: true; dense: true }
                            BListItem { width: parent.width; iconName: "calendar"; text: "Kalendarz"; dense: true }
                            BListItem { width: parent.width; iconName: "list-todo"; text: "Zadania"; dense: true; count: 4 }
                        }
                    }
                    SplitView {
                        orientation: Qt.Vertical
                        SplitView.fillWidth: true
                        handle: BSplitHandle { orientation: Qt.Vertical }
                        Item {
                            SplitView.fillHeight: true
                            BText {
                                anchors.fill: parent
                                anchors.margins: Theme.space5
                                wrapMode: Text.Wrap
                                text: "Główny obszar: edytor bloków. Przeciągnij uchwyty, aby zmienić podział."
                            }
                        }
                        Item {
                            SplitView.preferredHeight: 120
                            BText {
                                anchors.fill: parent
                                anchors.margins: Theme.space5
                                variant: "muted"
                                wrapMode: Text.Wrap
                                text: "Panel odnośników (12)"
                            }
                        }
                    }
                }
            }
        }
    }
}
