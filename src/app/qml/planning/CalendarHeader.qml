pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CalendarHeader — the bar above the calendar: previous / next period, "Today", the period title
// ("Październik 2026", "12–18 października 2026") and the Month / Week / Agenda switch. On
// narrow views the switch moves to a second row and stretches.
//
//   CalendarHeader { title: cal.title; mode: cal.mode; showingToday: cal.showingToday
//                    onPrevious: cal.previous(); onNext: cal.next(); onToday: cal.goToday()
//                    onModeRequested: (mode) => root.setMode(mode) }
Item {
    id: root

    property string title
    /// Title for narrow headers (CalendarModel.shortTitle).
    property string shortTitle: title
    property string mode: "month"
    property bool showingToday: true
    property int overdueCount: 0
    /// The vault is still being indexed (entries are still coming in).
    property bool busy: false
    /// Two rows (narrow views).
    readonly property bool stacked: width < 620

    signal previous()
    signal next()
    signal today()
    signal modeRequested(string mode)
    signal overdueRequested()

    readonly property string previousLabel: mode === "week" ? qsTr("Previous week")
                                          : mode === "agenda" ? qsTr("Earlier") : qsTr("Previous month")
    readonly property string nextLabel: mode === "week" ? qsTr("Next week")
                                      : mode === "agenda" ? qsTr("Later") : qsTr("Next month")

    implicitHeight: column.implicitHeight + 2 * Theme.space2

    readonly property var modes: [
        { value: "month", text: qsTr("Month"), icon: "calendar-days" },
        { value: "week", text: qsTr("Week"), icon: "columns-2" },
        { value: "agenda", text: qsTr("Agenda"), icon: "list" }
    ]

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.topMargin: Theme.space2
        anchors.bottomMargin: Theme.space2
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space1

            BIconButton {
                iconName: "chevron-left"
                label: root.previousLabel
                shortcut: "PgUp"
                onClicked: root.previous()
            }
            BIconButton {
                iconName: "chevron-right"
                label: root.nextLabel
                shortcut: "PgDown"
                onClicked: root.next()
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2
                text: root.stacked ? root.shortTitle : root.title
                font.family: Theme.fontUi
                font.pixelSize: root.stacked ? Theme.textLg : Theme.textXl
                font.weight: Theme.weightSemiBold
                color: Theme.fg
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            BSpinner {
                visible: root.busy
                running: visible
                size: Theme.iconSizeSm
            }
            BButton {
                visible: root.overdueCount > 0
                size: "small"
                variant: "ghost"
                iconName: "triangle-alert"
                textColor: Theme.dangerText
                text: root.stacked ? String(root.overdueCount) : qsTr("%n overdue", "", root.overdueCount)
                Accessible.name: qsTr("%n overdue", "", root.overdueCount)
                onClicked: root.overdueRequested()
            }
            BButton {
                size: "small"
                text: root.stacked ? "" : qsTr("Today")
                Accessible.name: qsTr("Today")
                iconName: "calendar-check"
                enabled: !root.showingToday
                onClicked: root.today()
            }
            BSegmented {
                visible: !root.stacked
                Layout.leftMargin: Theme.space2
                model: root.modes
                size: Theme.touchUi ? "normal" : "small"
                currentValue: root.mode
                accessibleName: qsTr("Calendar view")
                onActivated: (value) => root.modeRequested(value)
            }
        }

        BSegmented {
            visible: root.stacked
            Layout.fillWidth: true
            fill: true
            model: root.modes
            currentValue: root.mode
            accessibleName: qsTr("Calendar view")
            onActivated: (value) => root.modeRequested(value)
        }
    }
}
