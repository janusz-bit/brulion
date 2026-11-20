pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// FlashcardsView — route "flashcards" ({tab?, deck?, group?, ahead?, more?}, docs/UI.md
// "Flashcards"): the built-in flashcards extension (src/core/extensions/builtin/fiszki.luau,
// docs/SYNTAX.md "Flashcards") through its views, drawn by ShellExtensionView: "Decks" — what is
// to study now, every deck by tag, page or folder with its counts —, "Review" — a session of the
// deck: the question, Space shows the answer, 1–4 grade it (Again, Hard, Good, Easy with the next
// interval), U undoes the last grade —, "Statistics" — the counts, the grades per day, the
// retention, the forecast. Every grade is written next to its card in its note by the extension.
//
// Route parameters: tab ("decks" | "review" | "stats"), deck ("" every card, "tag:flashcards/…",
// "page:Name", "folder:Path"), group ("tag" | "page" | "folder": the decks' list), ahead (days of
// reviews studied ahead), more (new cards beyond today's limit).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Flashcards")
    readonly property int maximumWidth: 760
    readonly property WorkspaceExtensions extensions: Workspace.extensions
    property string tab: "decks"
    property string deck: ""
    property string group: "tag"
    readonly property list<var> tabs: [
        { value: "decks", text: qsTr("Decks"), icon: "layers" },
        { value: "review", text: qsTr("Review"), icon: "play" },
        { value: "stats", text: qsTr("Statistics"), icon: "chart-column" }
    ]
    readonly property list<var> groups: [
        { value: "tag", text: qsTr("Tags"), icon: "tags" },
        { value: "page", text: qsTr("Pages"), icon: "file-text" },
        { value: "folder", text: qsTr("Folders"), icon: "folder" }
    ]

    function applyRoute(): void {
        const tab = AppController.routeText(root.route, "tab")
        root.tab = tab === "review" || tab === "stats" ? tab : "decks"
        root.deck = AppController.routeText(root.route, "deck")
        const group = AppController.routeText(root.route, "group")
        root.group = group === "page" || group === "folder" ? group : "tag"
        cards.setParam("deck", root.deck)
        cards.setParam("group", root.group)
        cards.setParam("ahead", AppController.routeText(root.route, "ahead"))
        cards.setParam("more", AppController.routeText(root.route, "more"))
    }
    function show(tab: string, deck: string): void {
        AppController.navigate({ view: "flashcards", tab: tab, deck: deck, group: root.group })
    }
    /// The deck's name as the header shows it: "#flashcards/geografia", a page's name, a folder's.
    function deckLabel(deck: string): string {
        if (deck === "" || deck === "all")
            return qsTr("All cards")
        if (Js.startsWith(deck, "tag:"))
            return "#" + Js.sliceFrom(deck, 4)
        if (Js.startsWith(deck, "page:"))
            return Js.sliceFrom(deck, 5)
        if (Js.startsWith(deck, "folder:"))
            return Js.sliceFrom(deck, 7) !== "" ? Js.sliceFrom(deck, 7) : qsTr("Top folder")
        return deck
    }

    // (Another tab or deck: the keys — Space, 1–4, U — go to the view's buttons again, also after
    // a tap on the header's tabs took the focus.)
    onRouteChanged: {
        applyRoute()
        lines.forceActiveFocus()
    }
    Component.onCompleted: {
        applyRoute()
        lines.forceActiveFocus()
    }
    focus: true

    Keys.onPressed: (event) => {
        if (event.modifiers !== Qt.NoModifier || !AppSettings.flashcardsEnabled)
            return
        if (event.key === Qt.Key_Escape && root.tab !== "decks") {
            root.show("decks", "")
            event.accepted = true
        }
    }

    ExtensionView {
        id: cards
        objectName: "flashcardsModel"
        extensions: root.extensions
        extension: "fiszki"
        view: root.tab
        // A card's page ("Edit"), opened at the card's line.
        onPageRequested: (page, line) => AppController.openNamedPage(page, line)
        onJournalRequested: (date) => AppController.openJournal(date)
        // The views' own buttons and menus: another tab, a deck, studying ahead.
        // (Their params carry the decks' grouping on.)
        onViewRequested: (view, params) => AppController.navigate(cards.tabRoute("flashcards", view, params))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ShellExtensionView {
        id: lines
        objectName: "flashcardsList"
        anchors.fill: parent
        visible: AppSettings.flashcardsEnabled
        focus: true
        lines: cards
        maximumWidth: root.maximumWidth
        Accessible.name: root.tab === "review" ? qsTr("Review") : root.tab === "stats" ? qsTr("Statistics") : qsTr("Decks")

        header: Item {
            width: lines.width
            height: headerColumn.y + headerColumn.height + Theme.space3

            Column {
                id: headerColumn
                x: lines.inset
                y: Theme.narrow ? Theme.space3 : Theme.space5
                width: lines.lineWidth
                spacing: Theme.space3

                Item {
                    width: parent.width
                    height: Math.max(titleText.height + 2 + summaryText.height, studyButton.height)

                    BText {
                        id: titleText
                        width: studyButton.x - Theme.space3
                        text: qsTr("Flashcards")
                        variant: "heading"
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    BText {
                        id: summaryText
                        objectName: "flashcardsSummary"
                        y: titleText.height + 2
                        width: studyButton.x - Theme.space3
                        variant: "muted"
                        elide: Text.ElideRight
                        text: root.extensions.flashcardsDue > 0
                              ? qsTr("%n card(s) to study now", "", root.extensions.flashcardsDue)
                              : qsTr("Cards from your notes, repeated just before you would forget them")
                    }
                    BButton {
                        id: studyButton
                        objectName: "flashcardsStudy"
                        visible: root.tab !== "review"
                        x: parent.width - width
                        y: (parent.height - height) / 2
                        text: Theme.narrow ? "" : qsTr("Study")
                        iconName: "play"
                        variant: "primary"
                        Accessible.name: qsTr("Study")
                        onClicked: root.show("review", root.tab === "stats" ? root.deck : "")
                    }
                }

                BSegmented {
                    id: tabBar
                    objectName: "flashcardsTabs"
                    model: root.tabs
                    currentValue: root.tab
                    accessibleName: qsTr("Flashcards' views")
                    onActivated: (value) => root.show("" + value, "" + value === "decks" ? "" : root.deck)
                }

                BSegmented {
                    objectName: "flashcardsGroups"
                    visible: root.tab === "decks"
                    model: root.groups
                    currentValue: root.group
                    accessibleName: qsTr("Decks by")
                    onActivated: (value) => AppController.navigate({ view: "flashcards", tab: "decks", group: "" + value })
                }

                // The deck of the review or the statistics, and a way back to every card.
                Item {
                    objectName: "flashcardsDeck"
                    visible: root.tab !== "decks" && root.deck !== ""
                    width: parent.width
                    height: visible ? deckChip.height : 0
                    Rectangle {
                        id: deckChip
                        width: Math.min(parent.width, deckRow.implicitWidth + Theme.space3 + Theme.space1)
                        height: Math.max(deckLabelText.implicitHeight + Theme.space2, clearDeck.height)
                        radius: height / 2
                        color: Theme.accentBg
                        Row {
                            id: deckRow
                            x: Theme.space3
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space1
                            BIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "layers"
                                size: Theme.iconSizeSm
                                color: Theme.accent
                            }
                            BText {
                                id: deckLabelText
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, deckChip.parent.width - 3 * Theme.space6)
                                text: root.deckLabel(root.deck)
                                variant: "label"
                                elide: Text.ElideRight
                            }
                            BIconButton {
                                id: clearDeck
                                objectName: "flashcardsAllCards"
                                anchors.verticalCenter: parent.verticalCenter
                                size: "small"
                                label: qsTr("All cards")
                                iconName: "x"
                                onClicked: root.show(root.tab, "")
                            }
                        }
                    }
                }

                BText {
                    visible: cards.error !== ""
                    width: parent.width
                    text: qsTr("The view could not be drawn: %1").arg(cards.error)
                    color: Theme.danger
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    BEmptyState {
        objectName: "flashcardsOff"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 440)
        visible: !AppSettings.flashcardsEnabled
        iconName: "layers"
        title: qsTr("Flashcards are off")
        description: qsTr("Cards written in your notes (“Question::Answer” in a note tagged #flashcards, Logseq's #card blocks), repeated with FSRS. Switch them on in Settings → Extensions.")
        actionText: qsTr("Switch on")
        onActionTriggered: AppSettings.flashcardsEnabled = true
    }
}
