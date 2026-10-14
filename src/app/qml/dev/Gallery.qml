pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Templates as T
import Qt.labs.folderlistmodel
import Brulion

// Gallery — every design-system component in its states (`brulion --gallery`).
//
// Light/dark, Polish/English and touch sizes can be switched in the header. With
// `--section <id>` only that section is shown and its popups open by themselves, which is
// what scripts/screenshots.sh uses. Sample texts are Polish on purpose: long words
// ("Przypomnienia", "Synchronizacja", "Powtarzaj co tydzień w poniedziałek") must fit.
ApplicationWindow {
    id: window

    width: 1280
    height: 800
    visible: true
    title: "Brulion — galeria komponentów"
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    readonly property var sections: [
        { id: "foundations", title: "Podstawy", icon: "sun", popup: false },
        { id: "icons", title: "Ikony", icon: "image", popup: false },
        { id: "buttons", title: "Przyciski", icon: "square-check-big", popup: false },
        { id: "inputs", title: "Pola tekstowe", icon: "pencil", popup: false },
        { id: "selection", title: "Przełączniki i karty", icon: "list-checks", popup: false },
        { id: "chips", title: "Chipy i plakietki", icon: "tag", popup: false },
        { id: "lists", title: "Listy i karty", icon: "list", popup: false },
        { id: "feedback", title: "Postęp i powiadomienia", icon: "bell", popup: true },
        { id: "menus", title: "Menu", icon: "ellipsis", popup: true },
        { id: "dialog", title: "Okno dialogowe", icon: "panel-right", popup: true },
        { id: "confirm", title: "Potwierdzenie", icon: "triangle-alert", popup: true },
        { id: "datepicker", title: "Wybór daty", icon: "calendar", popup: false },
        { id: "reminder", title: "Przypomnienie", icon: "alarm-clock", popup: true },
        { id: "diff", title: "Porównanie wersji", icon: "file-diff", popup: false },
        { id: "merge", title: "Scalanie konfliktów", icon: "git-merge", popup: false },
        { id: "palette", title: "Paleta poleceń", icon: "command", popup: true },
        { id: "switcher", title: "Szybkie przejście", icon: "file-text", popup: true },
        { id: "split", title: "Podział widoku", icon: "columns-2", popup: false }
    ]
    property string current: DevTools.section !== "" ? DevTools.section : "foundations"
    readonly property bool autoOpen: DevTools.section !== ""
    readonly property var currentInfo: sections.find(s => s.id === current) || sections[0]

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
                text: Theme.narrow ? window.currentInfo.title : "Brulion · system projektowy"
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
            required property var modelData
            width: sectionList.width
            dense: true
            text: modelData.title
            iconName: modelData.icon
            selected: window.current === modelData.id
            selectionBar: true
            onClicked: {
                window.current = modelData.id
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
                    case "dialog": return dialogSection
                    case "confirm": return confirmSection
                    case "datepicker": return datePickerSection
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

    function luminance(c) {
        const f = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b)
    }
    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    /// Translucent colours composited over a background (for contrast checks).
    function over(c, bg) {
        return Qt.rgba(c.r * c.a + bg.r * (1 - c.a), c.g * c.a + bg.g * (1 - c.a), c.b * c.a + bg.b * (1 - c.a), 1)
    }

    // ---- Sections -------------------------------------------------------------------------
    Component {
        id: foundationsSection
        GallerySection {
            title: "Podstawy"
            description: "Kolory z logo: atrament, papier, czerwony margines, bursztyn i niebieska kratka. Każdy kolor tekstu ma kontrast WCAG AA (≥ 4,5:1) na tłach, na których występuje."

            Specimen {
                label: "TEKST NA TŁACH (kontrast)"
                Repeater {
                    model: [
                        { name: "fg", color: Theme.fg }, { name: "fgMuted", color: Theme.fgMuted },
                        { name: "fgFaint", color: Theme.fgFaint }, { name: "link", color: Theme.link },
                        { name: "accentText", color: Theme.accentText }, { name: "dangerText", color: Theme.dangerText },
                        { name: "warningText", color: Theme.warningText }, { name: "successText", color: Theme.successText },
                        { name: "tagFg", color: Theme.tagFg }, { name: "chipReminderFg", color: Theme.chipReminderFg }
                    ]
                    delegate: Rectangle {
                        id: swatch
                        required property var modelData
                        width: 172
                        height: 74
                        radius: Theme.radius
                        color: Theme.bgElevated
                        border.width: 1
                        border.color: Theme.border
                        readonly property real worst: Math.min(window.contrast(modelData.color, Theme.bg),
                                                               window.contrast(modelData.color, Theme.bgElevated),
                                                               window.contrast(modelData.color, Theme.bgSunken))
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2
                            Text {
                                text: "Zażółć gęślą jaźń"
                                color: swatch.modelData.color
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textBase
                                font.weight: Theme.weightMedium
                            }
                            Text {
                                text: swatch.modelData.name
                                color: Theme.fgMuted
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                            }
                            Text {
                                text: (swatch.worst >= 4.5 ? "AA " : "✗ ") + swatch.worst.toFixed(1) + ":1"
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
                    model: [
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
                        { name: "white/accentSolid", fg: Qt.color("#ffffff"), fill: Theme.accentSolid },
                        { name: "toast (fgInverse)", fg: Theme.fgInverse, fill: Theme.bgInverse },
                        { name: "selected row (fgMuted)", fg: Theme.fgMuted, fill: Theme.bgSelected }
                    ]
                    delegate: Rectangle {
                        id: fillSwatch
                        required property var modelData
                        width: 172
                        height: 74
                        radius: Theme.radius
                        color: window.over(modelData.fill, Theme.bgElevated)
                        border.width: 1
                        border.color: Theme.border
                        readonly property real worst: Math.min(window.contrast(modelData.fg, window.over(modelData.fill, Theme.bg)),
                                                               window.contrast(modelData.fg, window.over(modelData.fill, Theme.bgElevated)),
                                                               window.contrast(modelData.fg, window.over(modelData.fill, Theme.bgSunken)))
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 2
                            Text {
                                text: "Zażółć gęślą jaźń"
                                color: fillSwatch.modelData.fg
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textSm
                                font.weight: Theme.weightMedium
                            }
                            Text {
                                text: fillSwatch.modelData.name
                                color: fillSwatch.modelData.fg
                                opacity: 0.8
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.textXs
                            }
                            Text {
                                text: (fillSwatch.worst >= 4.5 ? "AA " : "✗ ") + fillSwatch.worst.toFixed(1) + ":1"
                                color: fillSwatch.modelData.fg
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
                    model: [
                        { name: "bg", color: Theme.bg }, { name: "bgElevated", color: Theme.bgElevated },
                        { name: "bgSunken", color: Theme.bgSunken }, { name: "bgInverse", color: Theme.bgInverse },
                        { name: "border", color: Theme.border }, { name: "borderStrong", color: Theme.borderStrong },
                        { name: "ink", color: Theme.ink }, { name: "paper", color: Theme.paper },
                        { name: "marginRed", color: Theme.marginRed }, { name: "amber", color: Theme.amber },
                        { name: "gridBlue", color: Theme.gridBlue }, { name: "focusRing", color: Theme.focusRing }
                    ]
                    delegate: Column {
                        id: surface
                        required property var modelData
                        spacing: 4
                        Rectangle {
                            width: 70
                            height: 52
                            radius: Theme.radius
                            color: surface.modelData.color
                            border.width: 1
                            border.color: Theme.border
                        }
                        Text {
                            text: surface.modelData.name
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
                    model: [4, 8, 12, 16, 24, 32, 48]
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
                    model: [0, 1, 2, 3]
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
                        model: [14, 16, 20, 24, 32]
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
            Component.onCompleted: if (window.autoOpen) Qt.callLater(() => focusDemo.forceActiveFocus(Qt.TabFocusReason))

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
            Component.onCompleted: if (window.autoOpen) Qt.callLater(() => focusedField.forceActiveFocus(Qt.TabFocusReason))

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
                    toasts.show("Zsynchronizowano 12 plików z Nextcloud", { kind: "success", timeout: 0 })
                    toasts.show("Przeniesiono „Plan podróży” do kosza", { actionText: "Cofnij", timeout: 0 })
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
                    onClicked: toasts.show("Przeniesiono do kosza", { actionText: "Cofnij", onAction: () => toasts.show("Przywrócono", { kind: "success" }) })
                }
                BButton {
                    text: "Błąd"
                    variant: "ghost"
                    onClicked: toasts.show("Nie można zapisać pliku", { kind: "error" })
                }
            }
        }
    }

    Component {
        id: menusSection
        GallerySection {
            title: "Menu"
            description: "Menu kontekstowe z ikonami, skrótami, opcjami do zaznaczenia i akcją niebezpieczną. Strzałki, Enter i Escape działają z klawiatury."
            Component.onCompleted: if (window.autoOpen) Qt.callLater(() => pageMenu.popup(moreButton, 0, moreButton.height + 4))

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
                acceptEnabled: nameField.text.trim() !== ""
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
                onAccepted: toasts.show("Zmieniono nazwę na „" + nameField.text + "”", { kind: "success" })
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
                        marks: {
                            const t = DatesHelper.today
                            const m = {}
                            m[DatesHelper.addDays(t, -2)] = ["overdue"]
                            m[t] = ["due", "reminder"]
                            m[DatesHelper.addDays(t, 1)] = ["reminder"]
                            m[DatesHelper.addDays(t, 3)] = ["scheduled", "due"]
                            m[DatesHelper.addDays(t, 6)] = ["alarm"]
                            m[DatesHelper.addDays(t, 9)] = ["done"]
                            return m
                        }
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
        id: reminderSection
        GallerySection {
            title: "Przypomnienie"
            description: "Szybkie wybory, data słowami lub z kalendarza, godzina w formacie 24 h, alarm i powtarzanie. Wynik: { at, alarm, recurrence } z regułą w kanonicznym języku angielskim."
            Component.onCompleted: if (window.autoOpen) reminder.edit({
                at: DatesHelper.addDays(DatesHelper.today, 1) + " 09:00",
                alarm: true,
                recurrence: "every week on Monday, Thursday",
                context: "Zadzwonić do mamy w sprawie urodzin"
            })

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Nowe przypomnienie"; iconName: "bell"; onClicked: reminder.create("Kupić bilety na pociąg") }
                BButton {
                    text: "Edytuj przypomnienie"
                    iconName: "pencil"
                    onClicked: reminder.edit({ at: DatesHelper.today + " 18:00", alarm: false, recurrence: "every 2 weeks on Friday", context: "Podlać kwiaty" })
                }
            }
            Specimen {
                label: "OSTATNI WYNIK"
                BText { id: reminderResult; variant: "mono"; text: "—" }
            }
            BReminderDialog {
                id: reminder
                onReminderAccepted: (r) => reminderResult.text = JSON.stringify(r)
                onDeleteRequested: toasts.show("Usunięto przypomnienie", { actionText: "Cofnij" })
            }
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
                Component.onCompleted: Qt.callLater(() => {
                    setChoice(0, "theirs")
                    setChoice(2, "both")
                })
                onResolved: (text) => toasts.show("Scalono (" + text.length + " znaków)", { kind: "success" })
            }
        }
    }

    readonly property var sampleCommands: [
        { title: "Dzisiejszy dziennik", icon: "book-open", shortcut: "Ctrl+J" },
        { title: "Nowa strona", icon: "square-pen", shortcut: "Ctrl+N" },
        { title: "Synchronizuj teraz", subtitle: "Nextcloud · ostatnio 5 min temu", icon: "refresh-cw", shortcut: "Ctrl+R" },
        { title: "Synchronizacja — ustawienia", subtitle: "Ustawienia", icon: "settings" },
        { title: "Przypomnienia", subtitle: "Widok", icon: "bell" },
        { title: "Kalendarz", subtitle: "Widok", icon: "calendar" },
        { title: "Pokaż graf powiązań", icon: "waypoints" },
        { title: "Przełącz tryb źródła", icon: "code", shortcut: "Ctrl+E" },
        { title: "Motyw: ciemny", icon: "moon" },
        { title: "Importuj z Obsidiana…", icon: "import" },
        { title: "Eksportuj do ZIP…", icon: "download" },
        { title: "Kosz", icon: "trash-2" },
        { title: "Zaplanowane zadania", subtitle: "Widok zadań", icon: "list-todo" }
    ]
    readonly property var samplePages: [
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
            Component.onCompleted: if (window.autoOpen) Qt.callLater(() => { palette.open(); palette.query = "syn" })

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Paleta poleceń"; iconName: "command"; onClicked: palette.open() }
                BKbd { sequence: "Ctrl+K" }
            }
            BCommandPalette {
                id: palette
                commands: window.sampleCommands.map(c => Object.assign({ action: () => toasts.show(c.title) }, c))
            }
        }
    }

    Component {
        id: switcherSection
        GallerySection {
            title: "Szybkie przejście"
            description: "Ctrl+O: przejdź do strony albo utwórz nową, gdy nic nie pasuje dokładnie."
            Component.onCompleted: if (window.autoOpen) Qt.callLater(() => { switcher.open(); switcher.query = "pla" })

            Specimen {
                label: "OTWÓRZ"
                BButton { text: "Przejdź do strony"; iconName: "file-text"; onClicked: switcher.open() }
            }
            BQuickSwitcher {
                id: switcher
                pages: window.samplePages
                onPageChosen: (p) => toasts.show("Otwieram „" + p.title + "”")
                onCreateRequested: (t) => toasts.show("Tworzę stronę „" + t + "”", { kind: "success" })
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
