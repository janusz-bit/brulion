pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import Brulion

// ShellExtensionView — an extension's view drawn with Brulion's own components (docs/UI.md
// "Extension views"): a list over an ExtensionView model whose lines are headings, texts, empty
// states, buttons that open another view of the extension or run one of its commands, links, rows
// of controls (a check, a link, a text, steppers — − number +, the number typed in as well —,
// buttons), a chart (ChartView) and tables. Nothing of the extension is QML: it returns data (b.ui
// nodes), this file draws it. The model changes lines in place when the view is drawn again with
// lines of the same kinds, so a field keeps its focus (and the touch keyboard) across a write.
//
//   ShellExtensionView { lines: gymModel; maximumWidth: 720; header: Item { … } }
ListView {
    id: list

    required property ExtensionView lines
    property int maximumWidth: 720
    /// The side margin of the lines (the column is centred up to maximumWidth).
    readonly property real inset: Math.max(Theme.narrow ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)
    readonly property real lineWidth: width - 2 * inset
    /// The height of a row's controls (each centred in it): the touch target on touch UIs.
    readonly property int controlHeight: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight

    model: lines
    TouchFlick.enabled: true
    clip: true
    spacing: Theme.space2
    boundsBehavior: Flickable.StopAtBounds
    bottomMargin: Theme.space8
    BRowScrollBar { list: list }
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

    delegate: DelegateChooser {
        role: "lineType"

        DelegateChoice {
            roleValue: "heading"
            Item {
                id: heading
                required property string lineText
                width: ListView.view.width
                height: headingText.implicitHeight + Theme.space3
                BText {
                    id: headingText
                    x: list.inset
                    y: Theme.space3
                    width: list.lineWidth
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
            Item {
                id: textLine
                required property string lineText
                required property bool lineMuted
                width: ListView.view.width
                height: lineTextItem.implicitHeight
                BText {
                    id: lineTextItem
                    x: list.inset
                    width: list.lineWidth
                    text: textLine.lineText
                    variant: textLine.lineMuted ? "muted" : "body"
                    wrapMode: Text.Wrap
                }
            }
        }
        DelegateChoice {
            roleValue: "empty"
            Item {
                id: emptyLine
                required property string lineTitle
                required property string lineText
                width: ListView.view.width
                height: emptyState.implicitHeight + Theme.space4
                BEmptyState {
                    id: emptyState
                    objectName: "extensionEmpty"
                    x: (parent.width - width) / 2
                    y: Theme.space4
                    width: Math.min(list.lineWidth, 440)
                    iconName: "dumbbell"
                    title: emptyLine.lineTitle
                    description: emptyLine.lineText
                }
            }
        }
        DelegateChoice {
            roleValue: "run"
            Item {
                id: runLine
                required property int index
                required property string lineText
                width: ListView.view.width
                height: runButton.implicitHeight
                BButton {
                    id: runButton
                    objectName: "extensionRun"
                    x: (parent.width - width) / 2
                    text: runLine.lineText
                    variant: "primary"
                    iconName: "play"
                    onClicked: list.lines.activate(runLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "open"
            Item {
                id: openLine
                required property int index
                required property string lineText
                width: ListView.view.width
                height: openButton.implicitHeight
                BButton {
                    id: openButton
                    x: list.inset - Theme.space2
                    text: openLine.lineText
                    variant: "ghost"
                    size: "small"
                    iconName: "chevron-left"
                    onClicked: list.lines.activate(openLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "link"
            Item {
                id: linkLine
                required property int index
                required property string lineText
                width: ListView.view.width
                height: linkItem.implicitHeight
                LinkText {
                    id: linkItem
                    x: list.inset
                    width: Math.min(implicitWidth, list.lineWidth)
                    text: linkLine.lineText
                    onActivated: list.lines.activate(linkLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "button"
            Item {
                id: buttonLine
                required property int index
                required property string lineText
                width: ListView.view.width
                height: lineButton.implicitHeight
                BButton {
                    id: lineButton
                    x: list.inset
                    text: buttonLine.lineText
                    onClicked: list.lines.activate(buttonLine.index)
                }
            }
        }
        DelegateChoice {
            roleValue: "chart"
            Item {
                id: chartLine
                required property var lineChartData
                required property string lineChartSource
                width: ListView.view.width
                height: chart.height + Theme.space2
                ChartView {
                    id: chart
                    objectName: "extensionChart"
                    x: list.inset
                    width: list.lineWidth
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
            roleValue: "tableHead"
            TableLine { header: true }
        }
        DelegateChoice {
            roleValue: "tableRow"
            TableLine { header: false }
        }
        DelegateChoice {
            roleValue: "row"
            Item {
                id: row
                required property int index
                required property ExtensionControlsModel lineControls
                width: ListView.view.width
                height: card.height
                Rectangle {
                    id: card
                    x: list.inset
                    width: list.lineWidth
                    height: flow.implicitHeight + 2 * Theme.space3
                    radius: Theme.radiusLg
                    color: Theme.bgSunken
                    border.color: Theme.borderSubtle
                    Flow {
                        id: flow
                        x: Theme.space3
                        y: Theme.space3
                        width: parent.width - 2 * Theme.space3
                        spacing: Theme.space2
                        Repeater {
                            model: row.lineControls
                            delegate: DelegateChooser {
                                role: "controlType"
                                DelegateChoice {
                                    roleValue: "check"
                                    Item {
                                        id: check
                                        required property int controlIndex
                                        required property bool controlChecked
                                        width: checkBox.width
                                        height: list.controlHeight
                                        // The tasks' own box: the line is a task of the note.
                                        TaskBox {
                                            id: checkBox
                                            objectName: "extensionCheck"
                                            anchors.verticalCenter: parent.verticalCenter
                                            status: check.controlChecked ? "done" : "todo"
                                            ownMenu: false
                                            Accessible.name: qsTr("Done")
                                            onToggled: list.lines.activateControl(row.index, check.controlIndex, 0)
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
                                        objectName: "extensionOpen"
                                        implicitWidth: Math.min(openControlText.implicitWidth, flow.width)
                                        implicitHeight: Math.max(openControlText.implicitHeight, list.controlHeight)
                                        width: implicitWidth
                                        height: implicitHeight
                                        LinkText {
                                            id: openControlText
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width
                                            text: openControl.controlText
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
                                        implicitWidth: Math.min(textControlItem.implicitWidth, flow.width)
                                        implicitHeight: Math.max(textControlItem.implicitHeight, list.controlHeight)
                                        width: implicitWidth
                                        height: implicitHeight
                                        BText {
                                            id: textControlItem
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width
                                            text: textControl.controlText
                                            variant: textControl.controlMuted ? "muted" : "body"
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
                                DelegateChoice {
                                    roleValue: "button"
                                    Item {
                                        id: controlButton
                                        required property int controlIndex
                                        required property string controlText
                                        width: buttonItem.width
                                        height: Math.max(buttonItem.height, list.controlHeight)
                                        BButton {
                                            id: buttonItem
                                            objectName: "extensionButton"
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: controlButton.controlText
                                            size: "small"
                                            onClicked: list.lines.activateControl(row.index, controlButton.controlIndex, 0)
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
    }

    /// A table's header or row: its cells in proportional columns (the second one widest).
    component TableLine: Item {
        id: tableLine
        required property list<string> lineCells
        property bool header: false
        width: ListView.view.width
        height: cellsRow.implicitHeight + (header ? Theme.space1 : 0)
        Row {
            id: cellsRow
            x: list.inset
            width: list.lineWidth
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
            x: list.inset
            y: parent.height - 1
            width: list.lineWidth
            height: 1
            color: Theme.borderSubtle
        }
    }
}
