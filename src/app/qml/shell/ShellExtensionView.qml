pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import Brulion

// ShellExtensionView — an extension's view drawn with Brulion's own components (docs/UI.md
// "Extension views"): a list over an ExtensionView model whose lines are headings, texts (with an
// icon, a tone), empty states, buttons that open another view of the extension or run one of its
// commands, links, menus (a "⋯" button), rows of controls (a check — done, open or skipped, with a
// menu on a right-click or a long press —, a link, a text, steppers — − number +, the number typed
// in as well —, buttons, menus), month grids (a cell per day, tinted by its state; a tap is the
// day's action, a right-click or a long press its menu), a chart (ChartView), tables, Markdown
// rendered as a note shows it (links open their pages), progress bars, numbers side by side
// (stats), dividers, pictures side by side (the notebook's, and those from the internet from the
// device's cache — "Show the pictures" until they may be fetched, as a note's), and the controls that take what is typed or chosen — a text field (one line,
// or several: Ctrl+Enter or its button gives the text), a select (a button whose menu lists the
// options), a slider, a date (a button that opens a calendar), a switch —, in rows or on a line of
// their own (`lineBare`: a row without a card; so is a progress bar alone). A button may have a
// keyboard shortcut (`key`: a key pressed while the view has the focus taps it — the flashcards'
// Space and 1–4), be the primary one, have a tone and a second line (`hint`: a grade's next
// interval). The lines
// of a card stay lines of the list: each draws its part of the card (LineFrame). Nothing of the
// extension is QML: it returns data (b.ui nodes), this file draws it. The model changes lines in
// place when the view is drawn again with lines of the same kinds, so a field keeps its focus (and
// the touch keyboard) across a write.
//
//   ShellExtensionView { lines: gymModel; maximumWidth: 720; header: Item { … } }
//   ShellExtensionView { lines: blockModel; embedded: true }   // a ```view block, a side panel's section
//
// `embedded`: the list is part of a page or a panel — as tall as its lines (no scrolling of its own,
// no scroll bar, no margins: the wheel and flicks go to what holds it), every line made at once.
ListView {
    id: list

    required property ExtensionView lines
    property int maximumWidth: 720
    property bool embedded: false
    /// The side margin of the lines (the column is centred up to maximumWidth).
    readonly property real inset: list.embedded ? 0
                                                : Math.max(Theme.narrow ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    readonly property real lineWidth: width - 2 * inset
    /// The height of a row's controls (each centred in it): the touch target on touch UIs.
    readonly property int controlHeight: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
    /// A card's padding around its lines.
    readonly property int cardPad: Theme.narrow ? Theme.space3 : Theme.space4
    /// A month grid's day cell and the gap between cells (as the habits' cards had them).
    readonly property int dayCell: Theme.touchUi ? 34 : 26
    readonly property int dayGap: 4
    /// A heat map's largest cell (smaller when its columns need it) and the gap between cells.
    readonly property int heatCell: Theme.touchUi ? 26 : 22
    readonly property int heatGap: 3

    /// A text's colour by its tone ("success" | "danger" | "accent"), else by `muted`.
    function toneColor(tone: string, muted: bool): color {
        switch (tone) {
        case "success": return Theme.successText
        case "danger": return Theme.dangerText
        case "accent": return Theme.accent
        case "warning": return Theme.warningText
        case "info": return Theme.infoText
        }
        return muted ? Theme.fgMuted : Theme.fg
    }
    /// A day's state for screen readers.
    function dayStateLabel(state: string): string {
        switch (state) {
        case "done": return qsTr("done")
        case "skipped": return qsTr("skipped")
        case "missed": return qsTr("missed")
        case "due": return qsTr("due")
        case "future": return qsTr("future")
        case "outside": return qsTr("before the start")
        }
        return qsTr("nothing due")
    }
    /// A check's state for screen readers.
    function checkStateLabel(state: string): string {
        switch (state) {
        case "done": return qsTr("done")
        case "skipped": return qsTr("skipped")
        }
        return qsTr("not done")
    }

    // ---- The menus (one, made the first time one opens; its items from the model) --------------
    /// What the menu shown belongs to: a row's control, a month line's day, a menu line.
    property int menuRow: -1
    property int menuControl: -1
    property string menuDate: ""
    property list<string> menuLabels: []
    property list<string> menuIcons: []
    property BMenu menuPopup: null
    /// Where openMenu() opens (mapToItem's result is read through a typed property).
    property point menuAt
    /// Opens the menu of the control `control` of line `row` (-1: the day `date` of a month line,
    /// or a menu line) at `x`, `y` of `item`.
    function openMenu(row: int, control: int, date: string, item: Item, x: real, y: real): void {
        list.menuLabels = list.lines.menuLabels(row, control, date)
        if (list.menuLabels.length === 0)
            return
        list.menuRow = row
        list.menuControl = control
        list.menuDate = date
        list.menuIcons = list.lines.menuIcons(row, control, date)
        if (!list.menuPopup)
            list.menuPopup = menuComponent.createObject(list) as BMenu
        list.menuAt = item.mapToItem(list, x, y)
        list.menuPopup.popup(list, list.menuAt.x, list.menuAt.y)
    }
    Component {
        id: menuComponent
        BMenu {
            id: extensionMenu
            objectName: "extensionMenuPopup"
            Instantiator {
                model: list.menuLabels
                delegate: BMenuItem {
                    id: menuItem
                    required property string modelData
                    required property int index
                    objectName: "extensionMenuItem"
                    text: menuItem.modelData
                    iconName: menuItem.index < list.menuIcons.length ? list.menuIcons[menuItem.index] : ""
                    onTriggered: list.lines.activateMenuItem(list.menuRow, list.menuControl, list.menuDate, menuItem.index)
                }
                onObjectAdded: (index, object) => extensionMenu.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => extensionMenu.removeItem(object as Item)
            }
        }
    }

    /// The name of a key as a view's button names its shortcut: "space", "enter", "escape",
    /// "backspace", "0" … "9", a letter ("a" … "z", from the text typed), "ctrl+z"; "" for others.
    function keyName(key: int, modifiers: int, text: string): string {
        if ((modifiers & (Qt.AltModifier | Qt.MetaModifier)) !== 0)
            return ""
        const ctrl = (modifiers & Qt.ControlModifier) !== 0
        if (ctrl)
            return key === Qt.Key_Z ? "ctrl+z" : ""
        if (key === Qt.Key_Space)
            return "space"
        if (key === Qt.Key_Return || key === Qt.Key_Enter)
            return "enter"
        if (key === Qt.Key_Escape)
            return "escape"
        if (key === Qt.Key_Backspace)
            return "backspace"
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return Js.numberToString(key - Qt.Key_0)
        if (key >= Qt.Key_A && key <= Qt.Key_Z && text.length === 1)
            return Js.toLowerCase(text)
        return ""
    }
    /// A key's caption beside a button's label ("Space", "1"); "" on touch screens.
    function keyCaption(key: string): string {
        if (Theme.touchUi || key === "")
            return ""
        switch (key) {
        case "space": return qsTr("Space")
        case "enter": return qsTr("Enter")
        case "escape": return qsTr("Esc")
        case "backspace": return qsTr("Backspace")
        case "ctrl+z": return qsTr("Ctrl+Z")
        }
        return Js.toUpperCase(key)
    }
    Keys.onPressed: (event) => {
        const name = list.keyName(event.key, event.modifiers, event.text)
        if (name !== "" && list.lines.activateKey(name))
            event.accepted = true
    }

    // Links of rendered Markdown open their pages (and journals) as the notes' do; its colours are
    // the theme's (b.ui.markdown in every extension's view).
    PageTools {
        id: linkTools
        workspace: Workspace
    }
    Binding {
        target: list.lines
        property: "palette"
        value: ({
            link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
            codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
            tagFg: Theme.tagFg, tagBg: Theme.tagBg,
            dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
            overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
            scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
            reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
            alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
            recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
            doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
            neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
        })
    }

    // ---- The date picker of a date control (one, made the first time it opens) ---------------
    property int dateRow: -1
    property int dateControl: -1
    property BDialog datePopup: null
    property string dateValue: ""
    /// Opens the calendar of the date control `control` of line `row` on `date` ("" today).
    function openDate(row: int, control: int, date: string): void {
        list.dateRow = row
        list.dateControl = control
        list.dateValue = date !== "" ? date : DatesHelper.today
        if (!list.datePopup)
            list.datePopup = dateComponent.createObject(list) as BDialog
        if (list.datePopup)
            list.datePopup.open()
    }
    /// The calendar's day (or "" for none) goes to the date control's action.
    function pickDate(date: string): void {
        const row = list.dateRow
        const control = list.dateControl
        list.lines.submitText(row, control, date)
    }
    Component {
        id: dateComponent
        BDialog {
            id: dateDialog
            objectName: "extensionDateDialog"
            title: qsTr("Pick a day")
            preferredWidth: Theme.dialogWidthSm
            acceptText: qsTr("Choose")
            onAccepted: {
                picker.commitPending()
                list.pickDate(list.dateValue)
            }
            footerLeading: BButton {
                text: qsTr("No date")
                variant: "ghost"
                onClicked: {
                    dateDialog.close()
                    list.pickDate("")
                }
            }
            BDatePicker {
                id: picker
                objectName: "extensionDatePicker"
                Layout.fillWidth: true
                selectedDate: list.dateValue
                onPicked: (value) => { list.dateValue = DatesHelper.specDate(value) }
            }
        }
    }

    model: lines
    TouchFlick.enabled: !list.embedded
    clip: !list.embedded
    spacing: Theme.space2
    boundsBehavior: Flickable.StopAtBounds
    bottomMargin: list.embedded ? 0 : Theme.space8
    interactive: !list.embedded
    // Embedded: as tall as its lines (a view on its own is sized by what holds it).
    Binding on height {
        when: list.embedded
        value: list.contentHeight
    }
    BRowScrollBar { list: list; visible: !list.embedded }
    Accessible.role: Accessible.List

    /// A link-like text (an exercise's page, another view).
    component LinkText: BText {
        id: link
        signal activated()
        color: Theme.link
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        Accessible.role: Accessible.Link
        Accessible.name: text
        Accessible.onPressAction: link.activated()
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: link.activated()
        }
    }

    /// A button of the view (b.ui.button): framed, `primary` in the accent's ink; with a `hint` two
    /// lines — the label and the hint under it, tinted by its tone (a grade: Again, Hard, Good,
    /// Easy and the next interval); its keyboard shortcut beside the label where there is a
    /// keyboard.
    component ViewButton: Rectangle {
        id: viewButton
        property string label: ""
        property string hint: ""
        property string tone: ""
        property string key: ""
        property bool primary: false
        property string icon: ""
        signal activated()
        readonly property string caption: list.keyCaption(viewButton.key)
        readonly property color ink: viewButton.primary ? Theme.fgOnAccent
                                   : viewButton.tone === "danger" ? Theme.dangerText
                                   : viewButton.tone === "success" ? Theme.successText
                                   : viewButton.tone === "warning" ? Theme.warningText
                                   : viewButton.tone === "info" ? Theme.infoText
                                   : viewButton.tone === "accent" ? Theme.accent : Theme.fg
        readonly property color tint: viewButton.primary ? Theme.accentSolid
                                    : viewButton.tone === "danger" ? Theme.dangerBg
                                    : viewButton.tone === "success" ? Theme.successBg
                                    : viewButton.tone === "warning" ? Theme.warningBg
                                    : viewButton.tone === "info" ? Theme.infoBg
                                    : viewButton.tone === "accent" ? Theme.accentBg : Theme.bgElevated
        implicitWidth: Math.max(Theme.touchUi ? Theme.touchTarget : Theme.controlHeight,
                                Math.max(buttonLabel.implicitWidth + (buttonIcon.visible ? buttonIcon.width + Theme.space2 : 0)
                                                 + (keyText.visible ? keyText.implicitWidth + Theme.space2 : 0),
                                         buttonHint.visible ? buttonHint.implicitWidth : 0) + 2 * Theme.space3)
        implicitHeight: Math.max(Theme.touchUi ? Theme.touchTarget : Theme.controlHeight,
                                 buttonLabel.implicitHeight + (buttonHint.visible ? buttonHint.implicitHeight : 0) + 2 * Theme.space2)
        radius: Theme.radius
        color: viewButton.tint
        border.width: viewButton.primary ? 0 : 1
        border.color: viewButton.activeFocus ? Theme.focusRing : viewButton.tone !== "" ? viewButton.ink.alpha(0.35) : Theme.border
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: viewButton.hint !== "" ? qsTr("%1, %2").arg(viewButton.label).arg(viewButton.hint) : viewButton.label
        Accessible.description: viewButton.caption !== "" ? qsTr("Shortcut: %1").arg(viewButton.caption) : ""
        Accessible.onPressAction: viewButton.activated()
        Keys.onSpacePressed: viewButton.activated()
        Keys.onReturnPressed: viewButton.activated()

        // Hover and press: the usual translucent tints over the button's own colour.
        Rectangle {
            anchors.fill: parent
            radius: viewButton.radius
            color: buttonTap.pressed ? Theme.bgPressed : Theme.bgHover
            visible: buttonTap.pressed || buttonHover.hovered
        }
        Item {
            id: buttonContent
            anchors.centerIn: parent
            width: Math.max(labelRow.width, buttonHint.visible ? buttonHint.implicitWidth : 0)
            height: buttonLabel.implicitHeight + (buttonHint.visible ? buttonHint.implicitHeight : 0)
            Row {
                id: labelRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.space2
                BIcon {
                    id: buttonIcon
                    visible: viewButton.icon !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    name: viewButton.icon
                    size: Theme.iconSizeSm
                    color: viewButton.ink
                }
                BText {
                    id: buttonLabel
                    text: viewButton.label
                    color: viewButton.ink
                    font.weight: Font.DemiBold
                }
                BText {
                    id: keyText
                    visible: viewButton.caption !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    text: viewButton.caption
                    variant: "caption"
                    color: viewButton.primary ? Theme.fgOnAccent.alpha(0.75) : Theme.fgFaint
                }
            }
            BText {
                id: buttonHint
                visible: viewButton.hint !== ""
                anchors.horizontalCenter: parent.horizontalCenter
                y: buttonLabel.implicitHeight
                text: viewButton.hint
                variant: "small"
                color: viewButton.primary ? Theme.fgOnAccent.alpha(0.85) : viewButton.ink.alpha(0.8)
            }
        }
        HoverHandler {
            id: buttonHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            id: buttonTap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: viewButton.activated()
        }
    }

    /// A line of the list: as wide as the list (ListView places its delegates at x 0), its content
    /// at contentX within contentWidth from topPad on. Inside a card (lineCard) it draws its part of
    /// the card — the top with its corners for "first", the sides for "middle", the bottom for
    /// "last", all of it for "only" — over the gap above it too, so the parts join; the card's
    /// padding is inside the first and the last line. The height is set by bindings
    /// (contentHeight): final at creation (docs/UI.md "Scrolling performance").
    component LineFrame: Item {
        id: frame
        required property string lineCard
        readonly property bool inCard: frame.lineCard !== ""
        readonly property bool cardTop: frame.lineCard === "first" || frame.lineCard === "only"
        readonly property bool cardBottom: frame.lineCard === "last" || frame.lineCard === "only"
        readonly property real padX: frame.inCard ? list.cardPad : 0
        readonly property real contentX: list.inset + frame.padX
        readonly property real contentWidth: list.lineWidth - 2 * frame.padX
        readonly property real topPad: frame.cardTop ? list.cardPad : 0
        readonly property real bottomPad: frame.cardBottom ? list.cardPad : 0
        /// The content's height: the line is topPad + it + bottomPad.
        property real contentHeight: 0
        width: ListView.view.width
        height: frame.topPad + frame.contentHeight + frame.bottomPad

        Item {
            visible: frame.inCard
            x: list.inset
            y: frame.cardTop ? 0 : -list.spacing
            width: list.lineWidth
            height: frame.height - y
            clip: frame.lineCard !== "only"
            Rectangle {
                readonly property real overlap: 2 * Theme.radiusLg
                y: frame.cardTop ? 0 : -overlap
                width: parent.width
                height: parent.height - y + (frame.cardBottom ? 0 : overlap)
                radius: Theme.radiusLg
                color: Theme.bgElevated
                border.width: 1
                border.color: Theme.borderSubtle
            }
        }
    }

    delegate: DelegateChooser {
        role: "lineType"

        DelegateChoice {
            roleValue: "heading"
            LineFrame {
                id: heading
                required property string lineText
                contentHeight: headingText.implicitHeight + (heading.inCard ? 0 : Theme.space3)
                BText {
                    id: headingText
                    x: heading.contentX
                    y: heading.topPad + (heading.inCard ? 0 : Theme.space3)
                    width: heading.contentWidth
                    text: heading.lineText
                    variant: "title"
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                }
            }
        }
        DelegateChoice {
            roleValue: "text"
            LineFrame {
                id: textLine
                required property string lineText
                required property bool lineMuted
                required property string lineIcon
                required property string lineTone
                readonly property color textColor: list.toneColor(textLine.lineTone, textLine.lineMuted)
                contentHeight: Math.max(lineTextItem.implicitHeight, textLine.lineIcon !== "" ? Theme.iconSizeSm : 0)
                Loader {
                    id: textIcon
                    active: textLine.lineIcon !== ""
                    visible: active
                    x: textLine.contentX
                    // (Beside the first line of a text that wraps.)
                    y: textLine.topPad + (lineTextItem.implicitHeight / Math.max(1, lineTextItem.lineCount) - Theme.iconSizeSm) / 2
                    width: Theme.iconSizeSm
                    height: Theme.iconSizeSm
                    sourceComponent: BIcon {
                        name: textLine.lineIcon
                        size: Theme.iconSizeSm
                        color: textLine.lineTone !== "" ? textLine.textColor : Theme.fgMuted
                    }
                }
                BText {
                    id: lineTextItem
                    readonly property real indent: textLine.lineIcon !== "" ? Theme.iconSizeSm + Theme.space1 : 0
                    x: textLine.contentX + indent
                    y: textLine.topPad
                    width: textLine.contentWidth - indent
                    text: textLine.lineText
                    variant: textLine.lineMuted ? "muted" : "body"
                    color: textLine.textColor
                    wrapMode: Text.Wrap
                }
            }
        }
        DelegateChoice {
            roleValue: "empty"
            LineFrame {
                id: emptyLine
                required property string lineTitle
                required property string lineText
                required property string lineIcon
                contentHeight: emptyState.implicitHeight + Theme.space4
                BEmptyState {
                    id: emptyState
                    objectName: "extensionEmpty"
                    x: emptyLine.contentX + (emptyLine.contentWidth - width) / 2
                    y: emptyLine.topPad + Theme.space4
                    width: Math.min(emptyLine.contentWidth, 440)
                    iconName: emptyLine.lineIcon !== "" ? emptyLine.lineIcon : "dumbbell"
                    title: emptyLine.lineTitle
                    description: emptyLine.lineText
                }
            }
        }
        DelegateChoice {
            roleValue: "run"
            LineFrame {
                id: runLine
                required property int index
                required property string lineText
                contentHeight: runButton.implicitHeight
                BButton {
                    id: runButton
                    objectName: "extensionRun"
                    x: runLine.contentX + (runLine.contentWidth - width) / 2
                    y: runLine.topPad
                    text: runLine.lineText
                    variant: "primary"
                    iconName: "play"
                    onClicked: list.lines.activate(runLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "open"
            LineFrame {
                id: openLine
                required property int index
                required property string lineText
                required property bool lineStyled
                required property string lineKey
                required property string lineTone
                required property string lineIcon
                required property bool linePrimary
                contentHeight: openLine.lineStyled ? openStyled.implicitHeight : openButton.implicitHeight
                BButton {
                    id: openButton
                    visible: !openLine.lineStyled
                    x: openLine.contentX - Theme.space2
                    y: openLine.topPad
                    text: openLine.lineText
                    variant: "ghost"
                    size: "small"
                    iconName: "chevron-left"
                    onClicked: list.lines.activate(openLine.index)
                }
                // With b.ui.open's options: a button like b.ui.button's (the flashcards' "Study").
                ViewButton {
                    id: openStyled
                    objectName: "extensionOpenButton"
                    visible: openLine.lineStyled
                    x: openLine.contentX
                    y: openLine.topPad
                    width: Math.min(implicitWidth, openLine.contentWidth)
                    height: implicitHeight
                    label: openLine.lineText
                    key: openLine.lineKey
                    tone: openLine.lineTone
                    icon: openLine.lineIcon
                    primary: openLine.linePrimary
                    onActivated: list.lines.activate(openLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "link"
            LineFrame {
                id: linkLine
                required property int index
                required property string lineText
                contentHeight: linkItem.implicitHeight
                LinkText {
                    id: linkItem
                    objectName: "extensionLink"
                    x: linkLine.contentX
                    y: linkLine.topPad
                    width: Math.min(implicitWidth, linkLine.contentWidth)
                    text: linkLine.lineText
                    onActivated: list.lines.activate(linkLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "button"
            LineFrame {
                id: buttonLine
                required property int index
                required property string lineText
                required property string lineKey
                required property string lineTone
                required property string lineHint
                required property string lineIcon
                required property bool linePrimary
                contentHeight: lineButton.implicitHeight
                ViewButton {
                    id: lineButton
                    objectName: "extensionButton"
                    x: buttonLine.contentX
                    y: buttonLine.topPad
                    width: Math.min(implicitWidth, buttonLine.contentWidth)
                    height: implicitHeight
                    label: buttonLine.lineText
                    key: buttonLine.lineKey
                    tone: buttonLine.lineTone
                    hint: buttonLine.lineHint
                    icon: buttonLine.lineIcon
                    primary: buttonLine.linePrimary
                    onActivated: list.lines.activate(buttonLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "markdown"
            LineFrame {
                id: markdownLine
                required property string lineHtml
                required property string lineTitle
                required property string lineSize
                required property string lineAlign
                required property bool lineMuted
                property string hoveredLink: ""
                contentHeight: markdownText.height
                BRichText {
                    id: markdownText
                    objectName: "extensionMarkdown"
                    x: markdownLine.contentX
                    y: markdownLine.topPad
                    width: markdownLine.contentWidth
                    html: markdownLine.lineHtml
                    hoveredLink: markdownLine.hoveredLink
                    horizontalAlignment: markdownLine.lineAlign === "center" ? Text.AlignHCenter : Text.AlignLeft
                    font.family: Theme.fontEditor
                    font.pixelSize: markdownLine.lineSize === "large" ? Theme.textXl
                                    : markdownLine.lineSize === "small" ? Theme.textSm : Theme.textBase
                    color: markdownLine.lineMuted ? Theme.fgMuted : Theme.fg
                    accessibleName: markdownLine.lineTitle
                }
                HoverHandler {
                    id: markdownHover
                    enabled: !Theme.touchUi
                    cursorShape: markdownLine.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onPointChanged: {
                        const at = markdownText.mapFromItem(markdownLine, markdownHover.point.position.x, markdownHover.point.position.y)
                        const link = markdownText.linkAt(at.x, at.y)
                        markdownLine.hoveredLink = Js.startsWith(link, "task:") ? "" : link
                    }
                    onHoveredChanged: if (!hovered) markdownLine.hoveredLink = ""
                }
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: (eventPoint, button) => {
                        const at = markdownText.mapFromItem(markdownLine, eventPoint.position.x, eventPoint.position.y)
                        const link = markdownText.linkAt(at.x, at.y)
                        if (link !== "" && !Js.startsWith(link, "task:"))
                            linkTools.openLink(link, "", false)
                    }
                }
            }
        }
        DelegateChoice {
            roleValue: "stats"
            LineFrame {
                id: statsLine
                required property list<var> lineItems
                contentHeight: statsFlow.implicitHeight
                Flow {
                    id: statsFlow
                    x: statsLine.contentX
                    y: statsLine.topPad
                    width: statsLine.contentWidth
                    spacing: Theme.space2
                    Repeater {
                        model: statsLine.lineItems
                        delegate: Rectangle {
                            id: stat
                            required property var modelData
                            readonly property string statTone: AppController.routeText(stat.modelData, "tone")
                            readonly property string statValue: AppController.routeText(stat.modelData, "value")
                            readonly property string statLabel: AppController.routeText(stat.modelData, "label")
                            objectName: "extensionStat"
                            // Two side by side on a phone, four on a desktop's line.
                            width: Math.floor((statsFlow.width - (Theme.narrow ? 1 : 3) * statsFlow.spacing) / (Theme.narrow ? 2 : 4))
                            height: statValueText.implicitHeight + statLabelText.implicitHeight + 2 * Theme.space2
                            radius: Theme.radiusLg
                            color: Theme.bgSunken
                            border.color: Theme.borderSubtle
                            Accessible.role: Accessible.StaticText
                            Accessible.name: qsTr("%1: %2").arg(stat.statLabel).arg(stat.statValue)
                            BText {
                                id: statValueText
                                x: Theme.space3
                                y: Theme.space2
                                width: parent.width - 2 * Theme.space3
                                text: stat.statValue
                                variant: "heading"
                                font.weight: Font.DemiBold
                                color: list.toneColor(stat.statTone, false)
                                elide: Text.ElideRight
                            }
                            BText {
                                id: statLabelText
                                x: Theme.space3
                                y: statValueText.y + statValueText.implicitHeight
                                width: parent.width - 2 * Theme.space3
                                text: stat.statLabel
                                variant: "caption"
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
        DelegateChoice {
            roleValue: "pictures"
            LineFrame {
                id: picturesLine
                objectName: "extensionPictures"
                required property int index
                required property list<var> lineItems
                required property string lineTitle
                /// "ask" while a picture from the internet waits to be allowed, "failed" when one
                /// could not be fetched (Show / Try again), else "".
                required property string lineHint
                /// Side by side: two on a phone, up to four on a wider line; each a 3:2 box the
                /// picture fits in (the line keeps its height while they load).
                readonly property int perLine: Math.max(1, Math.min(picturesLine.lineItems.length, Theme.narrow ? 2 : 4))
                readonly property real tileWidth: Math.floor((picturesLine.contentWidth
                                                              - (picturesLine.perLine - 1) * Theme.space2) / picturesLine.perLine)
                readonly property real tileHeight: Math.min(360, Math.round(picturesLine.tileWidth * 2 / 3))
                contentHeight: picturesColumn.implicitHeight
                Column {
                    id: picturesColumn
                    x: picturesLine.contentX
                    y: picturesLine.topPad
                    width: picturesLine.contentWidth
                    spacing: Theme.space2
                    BText {
                        visible: picturesLine.lineTitle !== ""
                        width: parent.width
                        text: picturesLine.lineTitle
                        variant: "caption"
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.space2
                        Repeater {
                            model: picturesLine.lineItems
                            delegate: Item {
                                id: tile
                                required property var modelData
                                readonly property string pictureState: AppController.routeText(tile.modelData, "state")
                                readonly property string caption: AppController.routeText(tile.modelData, "caption")
                                readonly property string alt: AppController.routeText(tile.modelData, "alt")
                                readonly property bool ready: tile.pictureState === "ready" && tileImage.status === Image.Ready
                                objectName: "extensionPicture"
                                width: picturesLine.tileWidth
                                height: tileBox.height + (tile.caption !== "" ? Theme.space1 + tileCaption.implicitHeight : 0)
                                Accessible.role: Accessible.Graphic
                                Accessible.name: tile.alt !== "" ? tile.alt : tile.caption
                                Rectangle {
                                    id: tileBox
                                    width: parent.width
                                    height: picturesLine.tileHeight
                                    radius: Theme.radiusLg
                                    color: Theme.bgSunken
                                    border.color: Theme.borderSubtle
                                    clip: true
                                    Image {
                                        id: tileImage
                                        objectName: "extensionPictureImage"
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        source: tile.pictureState === "ready" ? AppController.routeText(tile.modelData, "url") : ""
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectFit
                                        sourceSize.width: Math.ceil(Math.max(1, tileBox.width) * Screen.devicePixelRatio)
                                    }
                                    // Not shown (yet): why.
                                    Column {
                                        anchors.centerIn: parent
                                        width: parent.width - 2 * Theme.space3
                                        spacing: Theme.space2
                                        visible: !tile.ready
                                        BSpinner {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            visible: tile.pictureState === "loading"
                                                     || (tile.pictureState === "ready" && tileImage.status === Image.Loading)
                                            running: visible
                                        }
                                        BText {
                                            objectName: "extensionPictureMessage"
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            visible: text !== ""
                                            text: tile.pictureState === "missing"
                                                  ? qsTr("No picture “%1” in the notebook").arg(AppController.routeText(tile.modelData, "src"))
                                                  : tile.pictureState === "ask"
                                                    ? qsTr("Picture from %1").arg(AppController.routeText(tile.modelData, "host"))
                                                  : tile.pictureState === "failed" && AppController.routeText(tile.modelData, "error") !== ""
                                                    ? AppController.routeText(tile.modelData, "error")
                                                  : tile.pictureState === "failed" || (tile.pictureState === "ready" && tileImage.status === Image.Error)
                                                    ? qsTr("The picture could not be loaded")
                                                  : ""
                                            variant: "muted"
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                BText {
                                    id: tileCaption
                                    visible: tile.caption !== ""
                                    y: tileBox.height + Theme.space1
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: tile.caption
                                    variant: "caption"
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    // Pictures from the internet are fetched only once allowed (as a note's):
                    // one button for the line's.
                    BButton {
                        objectName: "extensionPicturesShow"
                        visible: picturesLine.lineHint !== ""
                        text: picturesLine.lineHint === "failed" ? qsTr("Try again") : qsTr("Show the pictures")
                        iconName: picturesLine.lineHint === "failed" ? "refresh-cw" : "eye"
                        size: "small"
                        onClicked: list.lines.showPictures(picturesLine.index)
                    }
                }
            }
        }
        DelegateChoice {
            roleValue: "divider"
            LineFrame {
                id: dividerLine
                contentHeight: Theme.space3
                Rectangle {
                    x: dividerLine.contentX
                    y: dividerLine.topPad + Theme.space3 / 2
                    width: dividerLine.contentWidth
                    height: 1
                    color: Theme.borderSubtle
                }
            }
        }
        DelegateChoice {
            roleValue: "menu"
            LineFrame {
                id: menuLine
                required property int index
                required property string lineText
                contentHeight: menuLineButton.height
                BIconButton {
                    id: menuLineButton
                    objectName: "extensionMenu"
                    x: menuLine.contentX + menuLine.contentWidth - width
                    y: menuLine.topPad
                    label: menuLine.lineText !== "" ? menuLine.lineText : qsTr("More")
                    iconName: "ellipsis"
                    onClicked: list.openMenu(menuLine.index, -1, "", menuLineButton, 0, menuLineButton.height)
                }
            }
        }
        DelegateChoice {
            roleValue: "month"
            LineFrame {
                id: monthLine
                objectName: "extensionMonth"
                required property int index
                required property string lineMonth
                required property string lineTitle
                required property list<var> lineDays
                /// The weekday of the month's first day from the week's first (DatesHelper.weekStart).
                readonly property int offset: (DatesHelper.dayOfWeek(monthLine.lineMonth + "-01") - DatesHelper.weekStart + 7) % 7
                readonly property int weeks: Math.ceil((monthLine.offset + monthLine.lineDays.length) / 7)
                readonly property list<string> heads: DatesHelper.weekdayNamesFrom(DatesHelper.weekStart, "short", "")
                readonly property real titleHeight: monthLine.lineTitle !== "" ? monthTitle.implicitHeight + Theme.space1 : 0
                readonly property real headsHeight: Theme.textXs + Theme.space2
                contentHeight: monthLine.titleHeight + monthLine.headsHeight + monthLine.weeks * (list.dayCell + list.dayGap)
                Accessible.role: Accessible.Table
                Accessible.name: monthLine.lineTitle !== "" ? monthLine.lineTitle : monthLine.lineMonth

                BText {
                    id: monthTitle
                    visible: monthLine.lineTitle !== ""
                    x: monthLine.contentX
                    y: monthLine.topPad
                    width: monthLine.contentWidth
                    text: monthLine.lineTitle
                    variant: "label"
                    elide: Text.ElideRight
                }
                Item {
                    id: grid
                    x: monthLine.contentX
                    y: monthLine.topPad + monthLine.titleHeight
                    width: 7 * list.dayCell + 6 * list.dayGap
                    height: monthLine.headsHeight + monthLine.weeks * (list.dayCell + list.dayGap)

                    Repeater {
                        model: monthLine.heads
                        delegate: Text {
                            id: head
                            required property int index
                            required property string modelData
                            x: head.index * (list.dayCell + list.dayGap)
                            width: list.dayCell
                            horizontalAlignment: Text.AlignHCenter
                            textFormat: Text.PlainText
                            text: head.modelData
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.textXs
                            color: Theme.fgMuted
                        }
                    }
                    Repeater {
                        model: monthLine.lineDays
                        delegate: Rectangle {
                            id: dayCell
                            required property int index
                            required property var modelData
                            // (Item has a `state` of its own: the day's fields are read from the map.)
                            readonly property string date: AppController.routeText(dayCell.modelData, "date")
                            readonly property string dayState: AppController.routeText(dayCell.modelData, "state")
                            readonly property bool tappable: AppController.routeFlag(dayCell.modelData, "tappable")
                            readonly property bool hasMenu: AppController.routeFlag(dayCell.modelData, "menu")
                            readonly property bool isToday: dayCell.date === DatesHelper.today
                            readonly property bool faint: dayCell.dayState === "future" || dayCell.dayState === "outside"
                            readonly property int slot: monthLine.offset + dayCell.index
                            objectName: "extensionDay_" + dayCell.date
                            x: (dayCell.slot % 7) * (list.dayCell + list.dayGap)
                            y: monthLine.headsHeight + Math.floor(dayCell.slot / 7) * (list.dayCell + list.dayGap)
                            width: list.dayCell
                            height: list.dayCell
                            radius: Theme.radiusSm
                            color: dayCell.dayState === "done" ? Theme.successBg : dayCell.dayState === "missed" ? Theme.dangerBg
                                 : dayCell.dayState === "skipped" ? Theme.chipDoneBg : "transparent"
                            border.width: dayCell.isToday ? 2 : dayCell.dayState === "due" ? 1 : 0
                            border.color: Theme.accent
                            Accessible.role: dayCell.tappable ? Accessible.Button : Accessible.Cell
                            Accessible.name: qsTr("%1: %2").arg(dayCell.date).arg(list.dayStateLabel(dayCell.dayState))
                            Accessible.onPressAction: if (dayCell.tappable) list.lines.activateDay(monthLine.index, dayCell.date)

                            Text {
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: AppController.routeText(dayCell.modelData, "day")
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.textXs
                                font.strikeout: dayCell.dayState === "skipped"
                                color: dayCell.dayState === "done" ? Theme.successText
                                     : dayCell.dayState === "missed" ? Theme.dangerText
                                     : dayCell.dayState === "due" || dayCell.isToday ? Theme.fg
                                     : dayCell.faint ? Theme.fgFaint : Theme.fgMuted
                            }
                            TapHandler {
                                enabled: dayCell.tappable || dayCell.hasMenu
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: if (dayCell.tappable) list.lines.activateDay(monthLine.index, dayCell.date)
                                onLongPressed: list.openMenu(monthLine.index, -1, dayCell.date, dayCell, dayCell.width / 2, dayCell.height)
                            }
                            TapHandler {
                                enabled: dayCell.hasMenu
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                                acceptedButtons: Qt.RightButton
                                onTapped: (eventPoint) => list.openMenu(monthLine.index, -1, dayCell.date, dayCell,
                                                                         eventPoint.position.x, eventPoint.position.y)
                            }
                            HoverHandler {
                                enabled: dayCell.tappable
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                    }
                }
            }
        }
        DelegateChoice {
            roleValue: "chart"
            LineFrame {
                id: chartLine
                required property var lineChartData
                required property string lineChartSource
                contentHeight: chart.height + Theme.space2
                ChartView {
                    id: chart
                    objectName: "extensionChart"
                    x: chartLine.contentX
                    y: chartLine.topPad
                    width: chartLine.contentWidth
                    height: implicitHeight
                    source: chartLine.lineChartSource
                    chartData: chartLine.lineChartData
                    english: Workspace.extensions.language === "en"
                    fontFamily: Theme.fontUi
                    fontPixelSize: Theme.textSm
                    textColor: Theme.fg
                    mutedColor: Theme.fgMuted
                    backgroundColor: Theme.bgSunken
                    borderColor: Theme.border
                    accentColor: Theme.accent
                    dark: Theme.dark
                    Accessible.role: Accessible.Graphic
                    Accessible.name: typeName
                    Accessible.description: description
                }
            }
        }
        DelegateChoice {
            roleValue: "heatmapHead"
            HeatLine { header: true }
        }
        DelegateChoice {
            roleValue: "heatmapRow"
            HeatLine { header: false }
        }
        DelegateChoice {
            roleValue: "tableHead"
            TableLine { header: true }
        }
        DelegateChoice {
            roleValue: "tableRow"
            TableLine { header: false }
        }
        DelegateChoice {
            roleValue: "row"
            LineFrame {
                id: row
                objectName: "extensionRow"
                required property int index
                required property ExtensionControlsModel lineControls
                required property bool lineBare
                // Inside a card the controls are part of it; on their own they make a card of their
                // own — unless it is one control on a line of its own (a text field, a slider).
                readonly property real rowPad: row.inCard || row.lineBare ? 0 : Theme.space3
                contentHeight: flow.implicitHeight + 2 * row.rowPad
                Rectangle {
                    id: card
                    visible: !row.inCard && !row.lineBare
                    x: row.contentX
                    y: row.topPad
                    width: row.contentWidth
                    height: row.contentHeight
                    radius: Theme.radiusLg
                    color: Theme.bgSunken
                    border.color: Theme.borderSubtle
                }
                Flow {
                    id: flow
                    x: row.contentX + row.rowPad
                    y: row.topPad + row.rowPad
                    width: row.contentWidth - 2 * row.rowPad
                    spacing: Theme.space2
                    Repeater {
                        id: controlsRepeater
                        model: row.lineControls
                        delegate: DelegateChooser {
                            role: "controlType"
                            DelegateChoice {
                                roleValue: "check"
                                Item {
                                    id: check
                                    required property int controlIndex
                                    required property string controlState
                                    required property string controlLabel
                                    required property bool controlHasMenu
                                    width: checkBox.width
                                    height: list.controlHeight
                                    // The tasks' own box: the line is a task of the note (skipped:
                                    // a cancelled task's look).
                                    TaskBox {
                                        id: checkBox
                                        objectName: "extensionCheck"
                                        anchors.verticalCenter: parent.verticalCenter
                                        status: check.controlState === "done" ? "done"
                                              : check.controlState === "skipped" ? "cancelled" : "todo"
                                        ownMenu: false
                                        Accessible.name: check.controlLabel !== ""
                                                         ? qsTr("%1: %2").arg(check.controlLabel).arg(list.checkStateLabel(check.controlState))
                                                         : qsTr("Done")
                                        Accessible.description: check.controlHasMenu ? qsTr("Right-click or press and hold for more") : ""
                                        onToggled: list.lines.activateControl(row.index, check.controlIndex, 0)
                                        onMenuRequested: {
                                            if (check.controlHasMenu)
                                                list.openMenu(row.index, check.controlIndex, "", checkBox, 0, checkBox.height)
                                        }
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "menu"
                                Item {
                                    id: menuControl
                                    required property int controlIndex
                                    required property string controlLabel
                                    width: menuButton.width
                                    height: Math.max(menuButton.height, list.controlHeight)
                                    BIconButton {
                                        id: menuButton
                                        objectName: "extensionMenu"
                                        anchors.verticalCenter: parent.verticalCenter
                                        label: menuControl.controlLabel !== "" ? menuControl.controlLabel : qsTr("More")
                                        iconName: "ellipsis"
                                        onClicked: list.openMenu(row.index, menuControl.controlIndex, "", menuButton, 0, menuButton.height)
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "link"
                                Item {
                                    id: linkControl
                                    required property int controlIndex
                                    required property string controlText
                                    implicitWidth: Math.min(linkControlText.implicitWidth, flow.width)
                                    implicitHeight: Math.max(linkControlText.implicitHeight, list.controlHeight)
                                    width: implicitWidth
                                    height: implicitHeight
                                    LinkText {
                                        id: linkControlText
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        text: linkControl.controlText
                                        onActivated: list.lines.activateControl(row.index, linkControl.controlIndex, 0)
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "open"
                                Item {
                                    id: openControl
                                    required property int controlIndex
                                    required property string controlText
                                    required property bool controlStyled
                                    required property string controlKey
                                    required property string controlTone
                                    required property string controlIcon
                                    required property bool controlPrimary
                                    objectName: "extensionOpen"
                                    implicitWidth: openControl.controlStyled ? openControlButton.implicitWidth
                                                                             : Math.min(openControlText.implicitWidth, flow.width)
                                    implicitHeight: Math.max(openControl.controlStyled ? openControlButton.implicitHeight
                                                                                       : openControlText.implicitHeight, list.controlHeight)
                                    width: implicitWidth
                                    height: implicitHeight
                                    LinkText {
                                        id: openControlText
                                        visible: !openControl.controlStyled
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        text: openControl.controlText
                                        onActivated: list.lines.activateControl(row.index, openControl.controlIndex, 0)
                                    }
                                    ViewButton {
                                        id: openControlButton
                                        objectName: "extensionOpenButton"
                                        visible: openControl.controlStyled
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: implicitWidth
                                        height: implicitHeight
                                        label: openControl.controlText
                                        key: openControl.controlKey
                                        tone: openControl.controlTone
                                        icon: openControl.controlIcon
                                        primary: openControl.controlPrimary
                                        onActivated: list.lines.activateControl(row.index, openControl.controlIndex, 0)
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "text"
                                Item {
                                    id: textControl
                                    required property string controlText
                                    required property bool controlMuted
                                    required property string controlIcon
                                    required property string controlTone
                                    readonly property real indent: textControl.controlIcon !== "" ? Theme.iconSizeSm + Theme.space1 : 0
                                    implicitWidth: Math.min(textControlItem.implicitWidth + textControl.indent, flow.width)
                                    implicitHeight: Math.max(textControlItem.implicitHeight, list.controlHeight)
                                    width: implicitWidth
                                    height: implicitHeight
                                    Loader {
                                        active: textControl.controlIcon !== ""
                                        visible: active
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.iconSizeSm
                                        height: Theme.iconSizeSm
                                        sourceComponent: BIcon {
                                            name: textControl.controlIcon
                                            size: Theme.iconSizeSm
                                            color: textControl.controlTone !== "" ? textControlItem.color : Theme.fgMuted
                                        }
                                    }
                                    BText {
                                        id: textControlItem
                                        x: textControl.indent
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - textControl.indent
                                        text: textControl.controlText
                                        variant: textControl.controlMuted ? "muted" : "body"
                                        color: list.toneColor(textControl.controlTone, textControl.controlMuted)
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                            // − number +: the number is a field — a tap selects it, what is typed
                            // ("82,5", "82.5") is written by the stepper's action on Enter (the
                            // touch keyboard's Done) or when the field is left, also for − / +
                            // or "+ set" tapped next (the view changes in place, the field stays);
                            // Escape brings the number back, ↑ / ↓ step.
                            DelegateChoice {
                                roleValue: "stepper"
                                Item {
                                    id: stepper
                                    required property int controlIndex
                                    required property string controlDisplay
                                    required property string controlNumber
                                    required property string controlUnit
                                    objectName: "extensionStepper"
                                    width: stepperBox.width
                                    height: Math.max(stepperBox.height, list.controlHeight)

                                    /// The field shows the line's number (after a change of the
                                    /// line, or when what was typed is not written).
                                    function showValue(): void {
                                        numberField.text = stepper.controlNumber
                                    }
                                    /// What was typed goes to the stepper's action when it is a
                                    /// number of at least 0 that differs from the shown one
                                    /// (ExtensionView.enterValue); then the field shows the line's
                                    /// number, drawn again by then.
                                    function enter(): void {
                                        const rowIndex = row.index
                                        const controlIndex = stepper.controlIndex
                                        const typed = numberField.typedText
                                        list.lines.enterValue(rowIndex, controlIndex, typed)
                                        stepper.showValue()
                                    }
                                    /// One step from the number — what was typed first.
                                    function step(direction: int): void {
                                        stepper.enter()
                                        const rowIndex = row.index
                                        const controlIndex = stepper.controlIndex
                                        const value = list.lines.stepped(rowIndex, controlIndex, direction)
                                        list.lines.activateControl(rowIndex, controlIndex, value)
                                    }
                                    onControlNumberChanged: stepper.showValue()
                                    Component.onCompleted: stepper.showValue()

                                  Rectangle {
                                    id: stepperBox
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: stepperRow.implicitWidth
                                    height: stepperRow.implicitHeight
                                    radius: Theme.radius
                                    color: Theme.bg
                                    // The box is the number field's frame, in BTextField's colours.
                                    border.color: numberField.activeFocus ? Theme.focusRing
                                                : numberField.hovered ? Theme.borderControl : Theme.border
                                    Rectangle {
                                        visible: numberField.activeFocus
                                        x: -3
                                        y: -3
                                        width: parent.width + 6
                                        height: parent.height + 6
                                        radius: stepperBox.radius + 3
                                        color: "transparent"
                                        border.width: 3
                                        border.color: Theme.focusRing.alpha(0.28)
                                    }
                                    Row {
                                        id: stepperRow
                                        BIconButton {
                                            label: qsTr("Less: %1").arg(stepper.controlDisplay)
                                            iconName: "minus"
                                            size: "small"
                                            onClicked: stepper.step(-1)
                                        }
                                        BTextField {
                                            id: numberField
                                            objectName: "extensionStepperField"
                                            anchors.verticalCenter: parent.verticalCenter
                                            // As wide as the number with its unit was (at least 52):
                                            // narrow paddings, the unit in the field's trailing slot
                                            // (8 from the edge in a small field, 4 from the number).
                                            width: Math.max(52, contentWidth + leftPadding + rightPadding)
                                            leftPadding: 4
                                            rightPadding: unitLabel.visible ? unitLabel.implicitWidth + 12 : 4
                                            size: "small"
                                            horizontalAlignment: TextInput.AlignHCenter
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            font.weight: Theme.weightMedium
                                            accessibleName: stepper.controlUnit !== "" ? qsTr("Number (%1)").arg(stepper.controlUnit)
                                                                                      : qsTr("Number")
                                            background: Item {}
                                            trailingItem: BText {
                                                id: unitLabel
                                                visible: stepper.controlUnit !== ""
                                                text: stepper.controlUnit
                                                variant: "muted"
                                            }
                                            /// It had the focus when pressed: the tap places the caret.
                                            property bool hadFocus: false
                                            // A tap that gives the focus selects the number, so what
                                            // is typed replaces it — after the release, which moves
                                            // the caret on touch screens (and gives the focus there).
                                            onPressed: numberField.hadFocus = numberField.activeFocus
                                            onReleased: if (!numberField.hadFocus) selectLater.restart()
                                            onActiveFocusChanged: if (numberField.activeFocus) numberField.selectAll()
                                            Later {
                                                id: selectLater
                                                onTriggered: if (numberField.activeFocus) numberField.selectAll()
                                            }
                                            onEditingFinished: stepper.enter()
                                            onEscapePressed: stepper.showValue()
                                            Keys.onUpPressed: stepper.step(1)
                                            Keys.onDownPressed: stepper.step(-1)
                                        }
                                        BIconButton {
                                            label: qsTr("More: %1").arg(stepper.controlDisplay)
                                            iconName: "plus"
                                            size: "small"
                                            onClicked: stepper.step(1)
                                        }
                                    }
                                  }
                                }
                            }
                            // A text field: Enter (Ctrl+Enter in one of several lines) or its button
                            // gives the text to the action; it is emptied then, unless `keep`.
                            DelegateChoice {
                                roleValue: "input"
                                Item {
                                    id: inputControl
                                    required property int controlIndex
                                    required property string controlText
                                    required property string controlPlaceholder
                                    required property string controlLabel
                                    required property string controlSubmit
                                    required property bool controlMultiline
                                    required property bool controlKeep
                                    objectName: "extensionInput"
                                    // Alone on its line: the line's width; beside other controls 260 px at most.
                                    width: row.lineBare ? flow.width : Math.min(flow.width, 260)
                                    height: Math.max(inputColumn.implicitHeight, list.controlHeight)

                                    function typed(): string {
                                        return inputControl.controlMultiline ? inputArea.typedText : inputField.typedText
                                    }
                                    function showValue(): void {
                                        inputField.text = inputControl.controlText
                                        inputArea.text = inputControl.controlText
                                    }
                                    function submit(): void {
                                        const rowIndex = row.index
                                        const controlIndex = inputControl.controlIndex
                                        const text = inputControl.typed()
                                        if (Js.trim(text) === "")
                                            return
                                        if (list.lines.submitText(rowIndex, controlIndex, text) && !inputControl.controlKeep) {
                                            inputField.text = ""
                                            inputArea.text = ""
                                        }
                                    }
                                    // The script's value when it changes (not while typing: a view drawn
                                    // again in place keeps what is being typed).
                                    onControlTextChanged: if (!inputField.activeFocus && !inputArea.activeFocus) inputControl.showValue()
                                    Component.onCompleted: inputControl.showValue()

                                    Column {
                                        id: inputColumn
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        spacing: Theme.space1
                                        BText {
                                            visible: inputControl.controlLabel !== ""
                                            width: parent.width
                                            text: inputControl.controlLabel
                                            variant: "caption"
                                            color: Theme.fgMuted
                                            elide: Text.ElideRight
                                        }
                                        Item {
                                            width: parent.width
                                            height: inputControl.controlMultiline ? inputArea.implicitHeight : inputField.implicitHeight
                                            BTextField {
                                                id: inputField
                                                objectName: "extensionInputField"
                                                visible: !inputControl.controlMultiline
                                                width: parent.width - submitButton.width - Theme.space2
                                                placeholderText: inputControl.controlPlaceholder
                                                accessibleName: inputControl.controlLabel !== "" ? inputControl.controlLabel
                                                                                                 : inputControl.controlPlaceholder
                                                onAccepted: inputControl.submit()
                                            }
                                            BTextArea {
                                                id: inputArea
                                                objectName: "extensionInputArea"
                                                visible: inputControl.controlMultiline
                                                width: parent.width - submitButton.width - Theme.space2
                                                height: Math.max(implicitHeight, 3 * Theme.controlHeight / 2)
                                                placeholderText: inputControl.controlPlaceholder
                                                accessibleName: inputControl.controlLabel !== "" ? inputControl.controlLabel
                                                                                                 : inputControl.controlPlaceholder
                                                Keys.onReturnPressed: (event) => {
                                                    if (event.modifiers & Qt.ControlModifier) {
                                                        event.accepted = true
                                                        inputControl.submit()
                                                    } else {
                                                        event.accepted = false
                                                    }
                                                }
                                            }
                                            BButton {
                                                id: submitButton
                                                objectName: "extensionInputSubmit"
                                                x: parent.width - width
                                                y: inputControl.controlMultiline ? parent.height - height : (parent.height - height) / 2
                                                text: inputControl.controlSubmit !== "" ? inputControl.controlSubmit : qsTr("Add")
                                                size: "small"
                                                variant: "primary"
                                                onClicked: inputControl.submit()
                                            }
                                        }
                                    }
                                }
                            }
                            // A select: the chosen option on a button whose menu lists them all.
                            DelegateChoice {
                                roleValue: "select"
                                Item {
                                    id: selectControl
                                    required property int controlIndex
                                    required property string controlText
                                    required property string controlLabel
                                    required property bool controlHasMenu
                                    objectName: "extensionSelect"
                                    width: Math.min(selectColumn.implicitWidth, flow.width)
                                    height: Math.max(selectColumn.implicitHeight, list.controlHeight)
                                    Column {
                                        id: selectColumn
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.space1
                                        BText {
                                            visible: selectControl.controlLabel !== ""
                                            text: selectControl.controlLabel
                                            variant: "caption"
                                            color: Theme.fgMuted
                                        }
                                        BButton {
                                            id: selectButton
                                            objectName: "extensionSelectButton"
                                            text: selectControl.controlText !== "" ? selectControl.controlText : qsTr("Choose…")
                                            size: "small"
                                            iconName: "chevron-down"
                                            enabled: selectControl.controlHasMenu
                                            Accessible.name: selectControl.controlLabel !== ""
                                                             ? qsTr("%1: %2").arg(selectControl.controlLabel).arg(selectButton.text)
                                                             : selectButton.text
                                            onClicked: list.openMenu(row.index, selectControl.controlIndex, "", selectButton, 0, selectButton.height)
                                        }
                                    }
                                }
                            }
                            // A slider: its value beside it, the action when a change is over.
                            DelegateChoice {
                                roleValue: "slider"
                                Item {
                                    id: sliderControl
                                    required property int controlIndex
                                    required property real controlValue
                                    required property real controlMin
                                    required property real controlMax
                                    required property real controlStep
                                    required property string controlUnit
                                    required property string controlLabel
                                    objectName: "extensionSlider"
                                    width: row.lineBare ? flow.width : Math.min(flow.width, 240)
                                    height: sliderColumn.implicitHeight
                                    function shown(value: real): string {
                                        const number = list.lines.formatNumber(value)
                                        return sliderControl.controlUnit !== "" ? number + " " + sliderControl.controlUnit : number
                                    }
                                    function commit(value: real): void {
                                        const rowIndex = row.index
                                        const controlIndex = sliderControl.controlIndex
                                        const snappedValue = list.lines.snapped(rowIndex, controlIndex, value)
                                        if (snappedValue !== sliderControl.controlValue)
                                            list.lines.activateControl(rowIndex, controlIndex, snappedValue)
                                    }
                                    Column {
                                        id: sliderColumn
                                        width: parent.width
                                        spacing: 0
                                        BText {
                                            visible: sliderControl.controlLabel !== ""
                                            width: parent.width
                                            text: sliderControl.controlLabel
                                            variant: "caption"
                                            color: Theme.fgMuted
                                            elide: Text.ElideRight
                                        }
                                        BSlider {
                                            id: slider
                                            objectName: "extensionSliderControl"
                                            width: parent.width
                                            from: sliderControl.controlMin
                                            to: sliderControl.controlMax
                                            stepSize: sliderControl.controlStep
                                            value: sliderControl.controlValue
                                            valueText: sliderControl.shown(slider.value)
                                            accessibleName: sliderControl.controlLabel
                                            onCommitted: (value) => sliderControl.commit(value)
                                        }
                                    }
                                }
                            }
                            // A date: a button with the day; its calendar picks another or none.
                            DelegateChoice {
                                roleValue: "date"
                                Item {
                                    id: dateControl
                                    required property int controlIndex
                                    required property string controlText
                                    required property string controlLabel
                                    objectName: "extensionDate"
                                    width: Math.min(dateColumn.implicitWidth, flow.width)
                                    height: Math.max(dateColumn.implicitHeight, list.controlHeight)
                                    Column {
                                        id: dateColumn
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.space1
                                        BText {
                                            visible: dateControl.controlLabel !== ""
                                            text: dateControl.controlLabel
                                            variant: "caption"
                                            color: Theme.fgMuted
                                        }
                                        BButton {
                                            id: dateButton
                                            objectName: "extensionDateButton"
                                            text: dateControl.controlText !== "" ? DatesHelper.formatShort(dateControl.controlText) : qsTr("No date")
                                            size: "small"
                                            iconName: "calendar"
                                            Accessible.name: dateControl.controlLabel !== ""
                                                             ? qsTr("%1: %2").arg(dateControl.controlLabel).arg(dateButton.text)
                                                             : dateButton.text
                                            onClicked: list.openDate(row.index, dateControl.controlIndex, dateControl.controlText)
                                        }
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "toggle"
                                Item {
                                    id: toggleControl
                                    required property int controlIndex
                                    required property bool controlChecked
                                    required property string controlLabel
                                    objectName: "extensionToggle"
                                    width: row.lineBare ? flow.width : Math.min(toggle.implicitWidth, flow.width)
                                    height: Math.max(toggle.implicitHeight, list.controlHeight)
                                    BSwitch {
                                        id: toggle
                                        objectName: "extensionToggleSwitch"
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        text: toggleControl.controlLabel
                                        // The switch shows what the script says: a tap (Space) asks it to
                                        // change, and the view drawn again shows the answer.
                                        checkable: false
                                        checked: toggleControl.controlChecked
                                        onClicked: {
                                            const rowIndex = row.index
                                            const controlIndex = toggleControl.controlIndex
                                            const on = !toggleControl.controlChecked
                                            list.lines.activateControl(rowIndex, controlIndex, on ? 1 : 0)
                                        }
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "progress"
                                // b.ui.progress: its label above a bar in the accent (or its tone's
                                // colour). In a row as tall as the controls beside it; on a line of
                                // its own (a review session's progress) just the label and the bar.
                                Item {
                                    id: progressControl
                                    required property real controlValue
                                    required property real controlMax
                                    required property string controlLabel
                                    required property string controlTone
                                    objectName: "extensionProgress"
                                    width: row.lineBare ? flow.width : Math.min(flow.width, 200)
                                    height: row.lineBare ? progressColumn.implicitHeight
                                                         : Math.max(progressColumn.implicitHeight, list.controlHeight)
                                    Accessible.role: Accessible.ProgressBar
                                    Accessible.name: progressControl.controlLabel
                                    Column {
                                        id: progressColumn
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        spacing: Theme.space1
                                        BText {
                                            visible: progressControl.controlLabel !== ""
                                            width: parent.width
                                            text: progressControl.controlLabel
                                            variant: "caption"
                                            color: list.toneColor(progressControl.controlTone, true)
                                            elide: Text.ElideRight
                                        }
                                        BProgressBar {
                                            width: parent.width
                                            from: 0
                                            to: progressControl.controlMax
                                            value: progressControl.controlValue
                                            tone: progressControl.controlTone === "success" || progressControl.controlTone === "danger"
                                                  || progressControl.controlTone === "warning" ? progressControl.controlTone : "accent"
                                            thickness: 6
                                        }
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "button"
                                Item {
                                    id: controlButton
                                    required property int controlIndex
                                    required property string controlText
                                    required property string controlKey
                                    required property string controlTone
                                    required property string controlHint
                                    required property string controlIcon
                                    required property bool controlPrimary
                                    /// A button with a second line or a tone is a view button (a
                                    /// grade's); a plain one the small BButton it always was.
                                    readonly property bool styled: controlButton.controlHint !== "" || controlButton.controlTone !== ""
                                                                   || controlButton.controlPrimary || controlButton.controlKey !== ""
                                    width: controlButton.styled ? styledButton.width : buttonItem.width
                                    height: Math.max(controlButton.styled ? styledButton.height : buttonItem.height, list.controlHeight)
                                    BButton {
                                        id: buttonItem
                                        objectName: "extensionButton"
                                        visible: !controlButton.styled
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: controlButton.controlText
                                        iconName: controlButton.controlIcon
                                        size: "small"
                                        onClicked: list.lines.activateControl(row.index, controlButton.controlIndex, 0)
                                    }
                                    ViewButton {
                                        id: styledButton
                                        objectName: "extensionViewButton"
                                        visible: controlButton.styled
                                        anchors.verticalCenter: parent.verticalCenter
                                        // A row's only button, a primary one, takes the row (the
                                        // flashcards' "Show answer"); buttons with a second line
                                        // share it evenly, four to a line (the grades).
                                        width: controlButton.controlPrimary && controlsRepeater.count === 1 ? flow.width
                                               : controlButton.controlHint !== ""
                                                 ? Math.max(implicitWidth, Math.floor((flow.width - 3 * flow.spacing) / 4))
                                                 : implicitWidth
                                        height: implicitHeight
                                        label: controlButton.controlText
                                        key: controlButton.controlKey
                                        tone: controlButton.controlTone
                                        hint: controlButton.controlHint
                                        icon: controlButton.controlIcon
                                        primary: controlButton.controlPrimary
                                        onActivated: list.lines.activateControl(row.index, controlButton.controlIndex, 0)
                                    }
                                }
                            }
                            DelegateChoice {
                                roleValue: "run"
                                BButton {
                                    id: runControl
                                    required property int controlIndex
                                    required property string controlText
                                    text: runControl.controlText
                                    size: "small"
                                    variant: "primary"
                                    onClicked: list.lines.activateControl(row.index, runControl.controlIndex, 0)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /// A table's header or row: its cells in proportional columns (the second one widest).
    component TableLine: LineFrame {
        id: tableLine
        required property list<string> lineCells
        property bool header: false
        contentHeight: cellsRow.implicitHeight + (header ? Theme.space1 : 0)
        Row {
            id: cellsRow
            x: tableLine.contentX
            y: tableLine.topPad
            width: tableLine.contentWidth
            spacing: Theme.space2
            Repeater {
                model: tableLine.lineCells
                delegate: BText {
                    id: cell
                    required property int index
                    required property string modelData
                    width: (cellsRow.width - 3 * cellsRow.spacing) * (cell.index === 1 ? 0.37 : 0.21)
                    text: cell.modelData
                    variant: tableLine.header ? "caption" : "small"
                    color: tableLine.header ? Theme.fgMuted : Theme.fg
                    horizontalAlignment: cell.index >= 2 ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle {
            visible: tableLine.header
            x: tableLine.contentX
            y: tableLine.topPad + tableLine.contentHeight - 1
            width: tableLine.contentWidth
            height: 1
            color: Theme.borderSubtle
        }
    }
    /// A heat map's line (b.ui.heatmap: the model's heatmapHead and heatmapRow): the label column,
    /// a cell per column tinted by its level (0 nothing … 4 the most: the accent ever stronger), the
    /// note column when the rows have notes. The head writes the columns' names — every so many
    /// when the cells are narrow, the last (this week) always. A cell's text shows as a tooltip on
    /// hover or a tap and is its name for screen readers.
    component HeatLine: LineFrame {
        id: heatLine
        required property list<string> lineCells
        required property list<var> lineItems
        required property string lineTitle
        required property string lineHint
        required property string lineSize
        property bool header: false
        readonly property int columns: heatLine.header ? heatLine.lineCells.length : heatLine.lineItems.length
        readonly property real noteWidth: heatLine.lineSize === "note" ? Math.min(Math.round(heatLine.contentWidth * 0.18), 88) : 0
        readonly property real labelWidth: Math.min(Math.round(heatLine.contentWidth * (Theme.narrow ? 0.36 : 0.3)), 168)
        readonly property real gridX: heatLine.labelWidth + Theme.space2
        readonly property real gridWidth: heatLine.contentWidth - heatLine.gridX
                                          - (heatLine.noteWidth > 0 ? heatLine.noteWidth + Theme.space2 : 0)
        readonly property real step: heatLine.columns > 0
                                     ? Math.max(5, Math.min(list.heatCell + list.heatGap, Math.floor(heatLine.gridWidth / heatLine.columns)))
                                     : 0
        readonly property real cell: heatLine.step - list.heatGap
        /// A name is written every `labelEvery` columns (about 34 px each).
        readonly property int labelEvery: heatLine.step > 0 ? Math.max(1, Math.ceil(34 / heatLine.step)) : 1
        /// The cell whose tooltip shows (hovered or tapped), -1: none.
        property int tip: heatHover.hovered && heatLine.step > 0
                          ? Math.min(heatLine.columns - 1, Math.floor(heatHover.point.position.x / heatLine.step)) : heatLine.tapped
        property int tapped: -1
        readonly property string tipText: heatLine.tip >= 0 && !heatLine.header
                                          ? AppController.routeText(heatLine.lineItems[heatLine.tip], "text") : ""
        contentHeight: heatLine.header ? Theme.textXs + Theme.space3 : Math.max(heatLine.cell, heatLabel.implicitHeight) + list.heatGap
        Accessible.role: heatLine.header ? Accessible.StaticText : Accessible.Row
        Accessible.name: heatLine.header ? heatLine.lineTitle
                                         : heatLine.lineHint !== "" ? qsTr("%1: %2").arg(heatLine.lineTitle).arg(heatLine.lineHint)
                                                                    : heatLine.lineTitle

        BText {
            id: heatLabel
            x: heatLine.contentX
            y: heatLine.topPad + (heatLine.header ? 0 : Math.max(0, (heatLine.cell - implicitHeight) / 2))
            width: heatLine.labelWidth
            text: heatLine.lineTitle
            variant: heatLine.header ? "caption" : "small"
            color: heatLine.header ? Theme.fgMuted : Theme.fg
            elide: Text.ElideRight
        }
        Item {
            id: heatGrid
            objectName: heatLine.header ? "extensionHeatHead" : "extensionHeatRow"
            x: heatLine.contentX + heatLine.gridX
            y: heatLine.topPad
            width: heatLine.columns * heatLine.step
            height: heatLine.header ? heatLine.contentHeight : heatLine.cell
            Repeater {
                model: heatLine.lineCells // (a row's are empty)
                delegate: BText {
                    id: heatHead
                    required property int index
                    required property string modelData
                    visible: (heatLine.columns - 1 - heatHead.index) % heatLine.labelEvery === 0
                    x: heatHead.index * heatLine.step + heatLine.cell / 2 - implicitWidth / 2
                    text: heatHead.modelData
                    variant: "caption"
                    color: Theme.fgMuted
                }
            }
            Repeater {
                model: heatLine.lineItems // (the head's are empty)
                delegate: Rectangle {
                    id: heatCellItem
                    required property int index
                    required property var modelData
                    readonly property string level: AppController.routeText(heatCellItem.modelData, "level")
                    objectName: "extensionHeatCell"
                    x: heatCellItem.index * heatLine.step
                    width: heatLine.cell
                    height: heatLine.cell
                    radius: Math.min(4, heatLine.cell / 4)
                    color: heatCellItem.level === "4" ? Theme.accent
                           : heatCellItem.level === "3" ? Theme.accent.alpha(0.7)
                           : heatCellItem.level === "2" ? Theme.accent.alpha(0.45)
                           : heatCellItem.level === "1" ? Theme.accent.alpha(0.22) : Theme.bgSunken
                    border.width: heatCellItem.level === "0" || heatLine.tip === heatCellItem.index ? 1 : 0
                    border.color: heatLine.tip === heatCellItem.index ? Theme.fg : Theme.borderSubtle
                    Accessible.role: Accessible.Cell
                    Accessible.name: AppController.routeText(heatCellItem.modelData, "text")
                }
            }
            HoverHandler {
                id: heatHover
                enabled: !heatLine.header
            }
            TapHandler {
                enabled: !heatLine.header
                onTapped: (eventPoint, button) => {
                    const column = Math.floor(eventPoint.position.x / Math.max(1, heatLine.step))
                    heatLine.tapped = heatLine.tapped === column ? -1 : column
                }
            }
            BToolTip {
                parent: heatGrid
                visible: heatLine.tipText !== ""
                text: heatLine.tipText
                x: Math.max(-heatGrid.x, heatLine.tip * heatLine.step + heatLine.cell / 2 - implicitWidth / 2)
                onVisibleChanged: if (!visible) heatLine.tapped = -1
            }
        }
        BText {
            // right after the cells (they need not fill the line), numbers aligned at its right
            visible: heatLine.noteWidth > 0 && !heatLine.header
            x: heatGrid.x + heatGrid.width + Theme.space2
            y: heatLabel.y
            width: heatLine.noteWidth
            text: heatLine.lineHint
            variant: "small"
            color: Theme.fgMuted
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }
}
