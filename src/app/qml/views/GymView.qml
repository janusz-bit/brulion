pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GymView — route "gym" ({tab?, exercise?}, docs/UI.md "Gym"): the built-in gym extension
// (src/core/extensions/builtin/silownia.luau, docs/SYNTAX.md §5 "Gym") through its views, drawn by
// ShellExtensionView: "Today" — today's workout (the #trening block of today's daily note) with a
// check, the last set's weight and reps as steppers, "+ set" / "− set" for each exercise, each change
// one line of the note; "Progress" — every exercise with its record, one exercise's chart of the
// estimated one-rep max and its sessions. "Start a workout" writes a plan (a page tagged
// #plan-treningowy) into today's note, weights from the last session (the extension's "start"
// command, its question in ShellExtensionQuestion). Adding a set starts the rest timer (the banner;
// a notification when it ends).
//
// Route parameters: tab ("today" | "progress"), exercise (one exercise's progress), open ("start" =
// "Start a workout" at once — screenshots).
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Gym")
    readonly property int maximumWidth: 720
    readonly property WorkspaceExtensions extensions: Workspace.extensions
    property string tab: "today"
    property string exercise: ""
    readonly property list<var> tabs: [
        { value: "today", text: qsTr("Today"), icon: "dumbbell" },
        { value: "progress", text: qsTr("Progress"), icon: "chart-column" }
    ]

    function applyRoute(): void {
        const exercise = AppController.routeText(root.route, "exercise")
        const tab = AppController.routeText(root.route, "tab")
        root.exercise = exercise
        root.tab = tab === "progress" || exercise !== "" ? "progress" : "today"
        root.startPending = AppController.routeText(root.route, "open") === "start"
        if (root.startPending)
            startLater.restart()
    }
    /// open: "start" — as soon as the notebook is read (the plans come from its index).
    property bool startPending: false
    Timer {
        id: startLater
        interval: 300 // the view draws today's workout first
        onTriggered: {
            if (!root.startPending || Workspace.indexing)
                return
            root.startPending = false
            root.extensions.runCommand("silownia", "start", "", -1)
        }
    }
    Connections {
        target: Workspace
        function onIndexingChanged() {
            if (root.startPending)
                startLater.restart()
        }
    }
    function showTab(tab: string): void {
        AppController.navigate({ view: "gym", tab: tab })
    }
    /// An exercise's page (a [[link]] target): opened, or made when it does not exist yet.
    function openPage(name: string): void {
        let path = Workspace.resolveLink(name, "")
        if (path === "")
            path = Workspace.createPage(name)
        if (path !== "")
            AppController.openPage(path)
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: {
        applyRoute()
        lines.forceActiveFocus()
    }
    focus: true

    ExtensionView {
        id: gym
        objectName: "gymModel"
        extensions: root.extensions
        extension: "silownia"
        view: root.tab
        paramKey: "exercise"
        paramValue: root.exercise
        onPageRequested: (page) => root.openPage(page)
        onViewRequested: (view, params) => AppController.navigate({ view: "gym", tab: view,
                                                                    exercise: AppController.routeText(params, "exercise") })
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    ShellExtensionView {
        id: lines
        objectName: "gymLines"
        anchors.fill: parent
        visible: AppSettings.gymEnabled
        focus: true
        lines: gym
        maximumWidth: root.maximumWidth
        Accessible.name: root.tab === "today" ? qsTr("Today's workout") : qsTr("Progress")

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
                    height: Math.max(titleText.height + 2 + summaryText.height, startButton.height)

                    BText {
                        id: titleText
                        width: startButton.x - Theme.space3
                        text: qsTr("Gym")
                        variant: "heading"
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    BText {
                        id: summaryText
                        y: titleText.height + 2
                        width: startButton.x - Theme.space3
                        variant: "muted"
                        elide: Text.ElideRight
                        text: qsTr("Workouts in your journal, plans, progress")
                    }
                    BButton {
                        id: startButton
                        objectName: "gymStart"
                        x: parent.width - width
                        y: (parent.height - height) / 2
                        text: Theme.narrow ? "" : qsTr("Start a workout")
                        iconName: "play"
                        variant: "primary"
                        Accessible.name: qsTr("Start a workout")
                        onClicked: root.extensions.runCommand("silownia", "start", "", -1)
                    }
                }

                BSegmented {
                    id: tabBar
                    objectName: "gymTabs"
                    model: root.tabs
                    currentValue: root.tab
                    accessibleName: qsTr("Gym views")
                    onActivated: (value) => root.showTab("" + value)
                }

                // The rest timer (b.timer): the time left, a bar, Skip.
                Rectangle {
                    objectName: "gymRest"
                    visible: root.extensions.restRemaining > 0
                    width: parent.width
                    height: visible ? restRow.height + 2 * Theme.space3 : 0
                    radius: Theme.radiusLg
                    color: Theme.accentBg
                    border.color: Theme.accent
                    Accessible.role: Accessible.StatusBar
                    Accessible.name: qsTr("%1: %2 left").arg(root.extensions.restLabel).arg(root.extensions.restText)

                    Item {
                        id: restRow
                        x: Theme.space3
                        y: Theme.space3
                        width: parent.width - 2 * Theme.space3
                        height: Math.max(restTime.height, skipButton.height) + Theme.space2 + restBar.height

                        BIcon {
                            id: restIcon
                            name: "timer"
                            y: (restTime.height - height) / 2
                            color: Theme.accent
                        }
                        BText {
                            id: restTime
                            x: restIcon.width + Theme.space2
                            text: root.extensions.restLabel + " · " + root.extensions.restText
                            variant: "title"
                            font.weight: Font.DemiBold
                        }
                        BButton {
                            id: skipButton
                            x: parent.width - width
                            text: qsTr("Skip")
                            size: "small"
                            variant: "ghost"
                            onClicked: root.extensions.stopRest()
                        }
                        BProgressBar {
                            id: restBar
                            y: parent.height - height
                            width: parent.width
                            from: 0
                            to: Math.max(1, root.extensions.restTotal)
                            value: root.extensions.restRemaining
                            tone: "accent"
                        }
                    }
                }

                BText {
                    visible: gym.error !== ""
                    width: parent.width
                    text: qsTr("The view could not be drawn: %1").arg(gym.error)
                    color: Theme.danger
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    BEmptyState {
        objectName: "gymOff"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.space6, 440)
        visible: !AppSettings.gymEnabled
        iconName: "dumbbell"
        title: qsTr("The gym is off")
        description: qsTr("Workouts written in the journal (“- [[Squat]] 100 kg 5×5”), plans, rest timer and progress. Switch it on in Settings → Extensions.")
        actionText: qsTr("Switch on")
        onActionTriggered: AppSettings.gymEnabled = true
    }
}
