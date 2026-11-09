pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BMergeView — resolve a conflict between two versions of a note, hunk by hunk.
//
//   BMergeView {
//       ours: localText; theirs: remoteText
//       oursLabel: qsTr("This phone"); theirsLabel: qsTr("Laptop (Nextcloud)")
//       onResolved: (text) => sync.resolve(path, text)
//   }
//
// For each conflicting hunk the user picks "Mine" (ours), "Theirs" (theirs), "Both" (both:
// ours then theirs) or "Manual" (edit the text by hand, pre-filled with both). Unchanged
// parts are shown collapsed. `hunks` comes from DiffHelper.conflictHunks(ours, theirs) by
// default; a caller with a 3-way merge (core sync merge3) may set `hunks` directly using the
// same shape: [{ kind: "same", text, lines } | { kind: "conflict", ours, theirs }].
// resolvedText is always up to date (unresolved hunks count as "ours"); "Apply" (enabled
// once every conflict has a choice) emits resolved(text). Line endings are preserved.
Item {
    id: control

    property string ours
    property string theirs
    property string oursLabel: qsTr("My version")
    property string theirsLabel: qsTr("Their version")
    property list<var> hunks: DiffHelper.conflictHunks(ours, theirs)
    property string applyText: qsTr("Apply")

    // One entry per conflict: the choice ("" | "ours" | "theirs" | "both" | "manual") and the
    // manual text. Reset whenever `hunks` change (empty before that; plain values, not bindings,
    // so the assignments below never break one).
    property list<string> choiceKinds
    property list<string> choiceTexts

    signal resolved(string text)

    readonly property list<int> conflictIndexes: DiffHelper.conflictIndexes(hunks)
    readonly property int conflictCount: conflictIndexes.length
    readonly property int resolvedCount: {
        let n = 0
        for (let i = 0; i < choiceKinds.length; ++i) {
            if (choiceKinds[i] !== "")
                ++n
        }
        return n
    }
    readonly property bool allResolved: resolvedCount === conflictCount
    readonly property string resolvedText: DiffHelper.resolveHunksWith(hunks, choiceKinds, choiceTexts)
    /// The hunks with every key the hunk delegates read.
    readonly property list<var> hunkRows: UiModels.withDefaults(hunks, { kind: "", text: "", lines: 0, ours: "", theirs: "" })

    /// conflict `n`'s choice and manual text ("" when there is none).
    function choiceOf(n: int): string {
        return n >= 0 && n < choiceKinds.length ? choiceKinds[n] : ""
    }
    function choiceTextOf(n: int): string {
        return n >= 0 && n < choiceTexts.length ? choiceTexts[n] : ""
    }
    function resetChoices(): void {
        chooseAll("")
    }
    /// Conflict `conflict` takes `choice`, keeping its manual text.
    function setChoice(conflict: int, choice: string): void {
        setChoiceText(conflict, choice, choiceTextOf(conflict))
    }
    /// Conflict `conflict` takes `choice` with the manual text `text`.
    function setChoiceText(conflict: int, choice: string, text: string): void {
        if (conflict < 0 || conflict >= choiceKinds.length)
            return
        choiceKinds = Js.insertedStringAt(Js.removedStringAt(choiceKinds, conflict), conflict, choice)
        choiceTexts = Js.insertedStringAt(Js.removedStringAt(choiceTexts, conflict), conflict, text)
    }
    /// Every conflict takes `choice` (no manual text).
    function chooseAll(choice: string): void {
        choiceKinds = Js.filled(conflictCount, choice)
        choiceTexts = Js.filled(conflictCount, "")
    }
    function apply(): void {
        if (allResolved)
            resolved(resolvedText)
    }
    function scrollToConflict(n: int): void {
        if (n >= 0 && n < conflictCount)
            list.positionViewAtIndex(conflictIndexes[n], ListView.Beginning)
    }

    onHunksChanged: control.resetChoices()
    Component.onCompleted: control.resetChoices()

    readonly property list<var> choiceRows: [
        { value: "ours", text: qsTr("Mine") },
        { value: "theirs", text: qsTr("Theirs") },
        { value: "both", text: qsTr("Both") },
        { value: "manual", text: qsTr("Manual") }
    ]

    implicitWidth: 720
    implicitHeight: 520

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Resolve conflicts")

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.space3

        // ---- Header: progress, bulk choices, apply ---------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2

            BIcon {
                name: control.allResolved ? "circle-check" : "git-merge"
                size: Theme.iconSize
                color: control.allResolved ? Theme.successText : Theme.warningText
            }
            Text {
                Layout.fillWidth: true
                text: control.conflictCount === 0 ? qsTr("No conflicts")
                      : qsTr("%1 of %n conflict(s) resolved", "", control.conflictCount).arg(control.resolvedCount)
                font.family: Theme.fontUi
                font.pixelSize: Theme.textBase
                font.weight: Theme.weightMedium
                color: Theme.fg
                elide: Text.ElideRight
            }
            BButton {
                visible: !Theme.compact
                size: "small"
                variant: "ghost"
                text: qsTr("All mine")
                onClicked: control.chooseAll("ours")
            }
            BButton {
                visible: !Theme.compact
                size: "small"
                variant: "ghost"
                text: qsTr("All theirs")
                onClicked: control.chooseAll("theirs")
            }
            BButton {
                text: control.applyText
                iconName: "check"
                variant: "primary"
                size: Theme.compact ? "small" : "normal"
                enabled: control.allResolved && control.conflictCount > 0
                onClicked: control.apply()
            }
        }

        BProgressBar {
            Layout.fillWidth: true
            visible: control.conflictCount > 0
            value: control.conflictCount > 0 ? control.resolvedCount / control.conflictCount : 0
            tone: control.allResolved ? "success" : "default"
            thickness: 3
        }

        // ---- Hunks --------------------------------------------------------------------------
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.space2
            model: control.hunkRows
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 2000
            T.ScrollBar.vertical: BScrollBar {}

            delegate: HunkLoader {
                width: list.width - 2
                x: 1
                sourceComponent: kind === "conflict" ? conflictHunk : sameHunk
            }
        }
    }

    /// A hunk (the delegate): its keys, read by the hunk it loads (its parent).
    component HunkLoader: Loader {
        required property int index
        /// "same" | "conflict"
        required property string kind
        /// same: the text and its line count; conflict: both sides.
        required property string text
        required property int lines
        required property string ours
        required property string theirs
    }

    component MonoText: Text {
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        color: Theme.fg
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        textFormat: Text.PlainText
        lineHeight: 1.15
    }

    Component {
        id: sameHunk
        Rectangle {
            id: same
            readonly property HunkLoader hunk: parent as HunkLoader
            property bool open: false
            readonly property string body: Js.replaceRegExp(hunk ? hunk.text : "", "\\r?\\n$", "", "")
            readonly property list<string> lines: Js.splitRegExp(body, "\\r\\n|\\r|\\n", "")
            width: parent ? parent.width : 0
            implicitHeight: open ? toggle.height + sameColumn.implicitHeight + 8 : toggle.height
            radius: Theme.radius
            color: Theme.bgSunken

            T.AbstractButton {
                id: toggle
                width: parent.width
                height: Theme.touchUi ? Theme.touchTarget : 30
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                text: qsTr("%n unchanged line(s)", "", same.hunk ? same.hunk.lines : 0)
                Accessible.role: Accessible.Button
                Accessible.name: text
                onClicked: same.open = !same.open
                contentItem: Row {
                    leftPadding: 10
                    spacing: 6
                    BIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron-right"
                        size: Theme.iconSizeSm
                        color: Theme.fgFaint
                        rotation: same.open ? 90 : 0
                        Behavior on rotation { NumberAnimation { duration: Theme.durationFast } }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: toggle.text
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textXs
                        color: toggle.hovered ? Theme.fg : Theme.fgMuted
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !same.open
                        width: Math.max(0, same.width - x - 16)
                        text: same.lines.length > 0 ? same.lines[0] : ""
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.textXs
                        color: Theme.fgFaint
                        elide: Text.ElideRight
                    }
                }
                background: Item {
                    BFocusRing { visible: toggle.visualFocus; inset: true; targetRadius: Theme.radius }
                }
            }
            Column {
                id: sameColumn
                visible: same.open
                x: 12
                y: toggle.height
                width: parent.width - 24
                MonoText {
                    width: parent.width
                    text: same.body
                    color: Theme.fgMuted
                }
            }
        }
    }

    Component {
        id: conflictHunk
        BCard {
            id: card
            readonly property HunkLoader hunk: parent as HunkLoader
            readonly property int hunkIndex: hunk ? hunk.index : -1
            readonly property int conflict: control.conflictIndexes.indexOf(hunkIndex)
            /// The conflict's choice and manual text (control.choiceKinds / choiceTexts).
            readonly property string choice: control.choiceOf(conflict)
            readonly property string choiceText: control.choiceTextOf(conflict)
            readonly property string oursText: hunk ? hunk.ours : ""
            readonly property string theirsText: hunk ? hunk.theirs : ""
            /// The two sides: { side, label, body }.
            readonly property list<var> sides: [
                { side: "ours", label: control.oursLabel, body: oursText },
                { side: "theirs", label: control.theirsLabel, body: theirsText }
            ]
            readonly property bool stacked: width < 640
            width: parent ? parent.width : 0
            padding: Theme.space3
            tone: card.choice === "" ? "warning" : "default"

            contentItem: ColumnLayout {
                spacing: Theme.space3

                GridLayout {
                    Layout.fillWidth: true
                    columns: card.stacked ? 2 : 3
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2
                    BIcon {
                        name: card.choice === "" ? "triangle-alert" : "circle-check"
                        size: Theme.iconSize
                        color: card.choice === "" ? Theme.warningText : Theme.successText
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Conflict %1").arg(card.conflict + 1)
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textBase
                        font.weight: Theme.weightSemiBold
                        color: Theme.fg
                        elide: Text.ElideRight
                    }
                    BSegmented {
                        id: choiceSwitch
                        // Own full-width row on narrow cards, next to the title otherwise.
                        Layout.columnSpan: card.stacked ? 2 : 1
                        Layout.fillWidth: card.stacked
                        fill: card.stacked
                        size: "small"
                        accessibleName: qsTr("Keep which version")
                        model: control.choiceRows
                        currentValue: card.choice
                        onActivated: (value) => {
                            if (value === "manual" && card.choiceText === "")
                                control.setChoiceText(card.conflict, value, card.oursText + card.theirsText)
                            else
                                control.setChoice(card.conflict, value)
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    visible: card.choice !== "manual"
                    columns: card.stacked ? 1 : 2
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2

                    Repeater {
                        model: card.sides
                        delegate: T.AbstractButton {
                            id: sideButton
                            /// "ours" | "theirs", its label and its text.
                            required property string side
                            required property string label
                            required property string body
                            readonly property bool chosen: card.choice === sideButton.side || card.choice === "both"
                            readonly property bool rejected: card.choice !== "" && !chosen
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.alignment: Qt.AlignTop
                            implicitHeight: sideColumn.implicitHeight
                            hoverEnabled: !Theme.touchUi
                            focusPolicy: Qt.StrongFocus
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Keep %1").arg(sideButton.label)
                            onClicked: control.setChoice(card.conflict, sideButton.side)

                            background: Rectangle {
                                radius: Theme.radius
                                // Neither side is "wrong": a chosen side is highlighted neutrally.
                                color: sideButton.chosen ? Theme.bgSelected : sideButton.hovered ? Theme.bgHover : Theme.bgSunken
                                border.width: sideButton.chosen ? 1.5 : 1
                                border.color: sideButton.chosen ? Theme.focusRing : sideButton.hovered ? Theme.borderStrong : Theme.borderSubtle
                                opacity: sideButton.rejected ? 0.55 : 1
                                BFocusRing { visible: sideButton.visualFocus; targetRadius: Theme.radius }
                            }
                            contentItem: Column {
                                id: sideColumn
                                padding: 8
                                spacing: 4
                                RowLayout {
                                    width: sideButton.width - 16
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        text: sideButton.label
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.textXs
                                        font.weight: Theme.weightSemiBold
                                        color: Theme.fgMuted
                                        elide: Text.ElideRight
                                    }
                                    BIcon {
                                        name: sideButton.chosen ? "circle-check" : ""
                                        size: Theme.iconSizeSm
                                        color: Theme.link
                                    }
                                }
                                MonoText {
                                    width: sideButton.width - 16
                                    text: sideButton.body === "" ? qsTr("(nothing)") : Js.replaceRegExp(sideButton.body, "\\r?\\n$", "", "")
                                    color: sideButton.body === "" ? Theme.fgFaint : Theme.fg
                                    font.italic: sideButton.body === ""
                                    font.strikeout: sideButton.rejected && sideButton.body !== ""
                                }
                            }
                        }
                    }
                }

                BTextArea {
                    id: manual
                    Layout.fillWidth: true
                    visible: card.choice === "manual"
                    monospace: true
                    text: card.choiceText
                    accessibleName: qsTr("Merged text")
                    onTextChanged: if (manual.activeFocus) control.setChoiceText(card.conflict, "manual", manual.text)
                }
            }
        }
    }
}
