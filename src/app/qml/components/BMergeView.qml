pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// BMergeView — resolve a conflict between two versions of a note, hunk by hunk.
//
//   BMergeView {
//       ours: localText; theirs: remoteText
//       oursLabel: qsTr("Ten telefon"); theirsLabel: qsTr("Laptop (Nextcloud)")
//       onResolved: (text) => sync.resolve(path, text)
//   }
//
// For each conflicting hunk the user picks "Moja" (ours), "Ich" (theirs), "Obie" (both:
// ours then theirs) or "Ręcznie" (edit the text by hand, pre-filled with both). Unchanged
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
    property var hunks: DiffHelper.conflictHunks(ours, theirs)
    property string applyText: qsTr("Apply")

    // One entry per conflict: { choice: "" | "ours" | "theirs" | "both" | "manual", text }.
    // Reset whenever `hunks` change (null before that; a literal, not a binding, so the
    // assignments below never break one).
    property var choices: null

    signal resolved(string text)

    readonly property var conflictIndexes: {
        const out = []
        for (let i = 0; i < (hunks ? hunks.length : 0); ++i)
            if (hunks[i].kind === "conflict")
                out.push(i)
        return out
    }
    readonly property int conflictCount: conflictIndexes.length
    readonly property int resolvedCount: (choices || []).filter(c => c && c.choice !== "").length
    readonly property bool allResolved: resolvedCount === conflictCount
    readonly property string resolvedText: DiffHelper.resolveHunks(hunks, (choices || []).map(c => c && c.choice !== "" ? c : { choice: "ours" }))

    function resetChoices() {
        const out = []
        for (let i = 0; i < conflictCount; ++i)
            out.push({ choice: "", text: "" })
        choices = out
    }
    function setChoice(conflict, choice, text) {
        const next = (choices || []).slice()
        next[conflict] = { choice: choice, text: text === undefined ? (next[conflict] ? next[conflict].text : "") : text }
        choices = next
    }
    function chooseAll(choice) {
        const next = []
        for (let i = 0; i < conflictCount; ++i)
            next.push({ choice: choice, text: "" })
        choices = next
    }
    function apply() {
        if (allResolved)
            resolved(resolvedText)
    }
    function scrollToConflict(n) {
        if (n >= 0 && n < conflictCount)
            list.positionViewAtIndex(conflictIndexes[n], ListView.Beginning)
    }

    onHunksChanged: resetChoices()
    Component.onCompleted: resetChoices()

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
            model: control.hunks
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 2000
            T.ScrollBar.vertical: BScrollBar {}

            delegate: Loader {
                id: hunkLoader
                required property var modelData
                required property int index
                width: list.width - 2
                x: 1
                sourceComponent: modelData.kind === "conflict" ? conflictHunk : sameHunk
                onLoaded: {
                    item.hunk = Qt.binding(() => hunkLoader.modelData)
                    item.hunkIndex = Qt.binding(() => hunkLoader.index)
                }
            }
        }
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
            property var hunk: ({})
            property int hunkIndex: -1
            property bool open: false
            readonly property string body: (hunk.text || "").replace(/\r?\n$/, "")
            readonly property var lines: body.split(/\r\n|\r|\n/)
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
                text: qsTr("%n unchanged line(s)", "", same.hunk.lines || 0)
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
                        text: same.lines[0] || ""
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
            property var hunk: ({})
            property int hunkIndex: -1
            readonly property int conflict: control.conflictIndexes.indexOf(hunkIndex)
            readonly property var choiceState: (control.choices || [])[conflict] || { choice: "", text: "" }
            readonly property bool stacked: width < 640
            width: parent ? parent.width : 0
            padding: Theme.space3
            tone: choiceState.choice === "" ? "warning" : "default"

            contentItem: ColumnLayout {
                spacing: Theme.space3

                GridLayout {
                    Layout.fillWidth: true
                    columns: card.stacked ? 2 : 3
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2
                    BIcon {
                        name: card.choiceState.choice === "" ? "triangle-alert" : "circle-check"
                        size: Theme.iconSize
                        color: card.choiceState.choice === "" ? Theme.warningText : Theme.successText
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
                        model: [
                            { value: "ours", text: qsTr("Mine") },
                            { value: "theirs", text: qsTr("Theirs") },
                            { value: "both", text: qsTr("Both") },
                            { value: "manual", text: qsTr("Manual") }
                        ]
                        currentValue: card.choiceState.choice
                        onActivated: (value) => {
                            if (value === "manual" && card.choiceState.text === "")
                                control.setChoice(card.conflict, value, (card.hunk.ours || "") + (card.hunk.theirs || ""))
                            else
                                control.setChoice(card.conflict, value)
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    visible: card.choiceState.choice !== "manual"
                    columns: card.stacked ? 1 : 2
                    columnSpacing: Theme.space2
                    rowSpacing: Theme.space2

                    Repeater {
                        model: [
                            { side: "ours", label: control.oursLabel, text: card.hunk.ours || "" },
                            { side: "theirs", label: control.theirsLabel, text: card.hunk.theirs || "" }
                        ]
                        delegate: T.AbstractButton {
                            id: side
                            required property var modelData
                            readonly property bool chosen: card.choiceState.choice === modelData.side || card.choiceState.choice === "both"
                            readonly property bool rejected: card.choiceState.choice !== "" && !chosen
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.alignment: Qt.AlignTop
                            implicitHeight: sideColumn.implicitHeight
                            hoverEnabled: !Theme.touchUi
                            focusPolicy: Qt.StrongFocus
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Keep %1").arg(modelData.label)
                            onClicked: control.setChoice(card.conflict, modelData.side)

                            background: Rectangle {
                                radius: Theme.radius
                                // Neither side is "wrong": a chosen side is highlighted neutrally.
                                color: side.chosen ? Theme.bgSelected : side.hovered ? Theme.bgHover : Theme.bgSunken
                                border.width: side.chosen ? 1.5 : 1
                                border.color: side.chosen ? Theme.focusRing : side.hovered ? Theme.borderStrong : Theme.borderSubtle
                                opacity: side.rejected ? 0.55 : 1
                                BFocusRing { visible: side.visualFocus; targetRadius: Theme.radius }
                            }
                            contentItem: Column {
                                id: sideColumn
                                padding: 8
                                spacing: 4
                                RowLayout {
                                    width: side.width - 16
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        text: side.modelData.label
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.textXs
                                        font.weight: Theme.weightSemiBold
                                        color: Theme.fgMuted
                                        elide: Text.ElideRight
                                    }
                                    BIcon {
                                        name: side.chosen ? "circle-check" : ""
                                        size: Theme.iconSizeSm
                                        color: Theme.link
                                    }
                                }
                                MonoText {
                                    width: side.width - 16
                                    text: side.modelData.text === "" ? qsTr("(nothing)") : side.modelData.text.replace(/\r?\n$/, "")
                                    color: side.modelData.text === "" ? Theme.fgFaint : Theme.fg
                                    font.italic: side.modelData.text === ""
                                    font.strikeout: side.rejected && side.modelData.text !== ""
                                }
                            }
                        }
                    }
                }

                BTextArea {
                    id: manual
                    Layout.fillWidth: true
                    visible: card.choiceState.choice === "manual"
                    monospace: true
                    text: card.choiceState.text
                    accessibleName: qsTr("Merged text")
                    onTextChanged: if (activeFocus) control.setChoice(card.conflict, "manual", text)
                }
            }
        }
    }
}
