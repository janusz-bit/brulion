pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentYearAgo — the side panel's "A year ago" of a journal day (docs/UI.md "Side panel"): the
// journal of the same day a year before `date` (29 February: the 28th), its first blocks rendered
// like a link preview (LinkPreview: InlineRenderer's escaped rich text, never a note's own HTML),
// the date as a link that opens it; a line saying there is none otherwise.
//
//   ContentYearAgo { date: "2026-10-14" }
Item {
    id: root

    property string date
    /// The blocks shown at most.
    property int limit: 6
    readonly property string yearAgo: root.date !== "" ? DatesHelper.addMonths(root.date, -12) : ""
    readonly property string journalPath: {
        if (root.yearAgo === "" || Workspace.revision < 0 || root.journalDays(root.yearAgo).length === 0)
            return ""
        return tools.journalPathFor(root.yearAgo)
    }
    function journalDays(date: string): list<string> {
        return Workspace.journalDates(date, date)
    }
    readonly property bool found: root.journalPath !== ""
    readonly property list<var> shownEntries: root.found ? root.entries : []
    readonly property list<var> entries: !root.found || !Js.hasKey(preview.preview, "blocks") ? []
        : UiModels.withDefaults(UiModels.field(preview.preview, "blocks"), { kind: "", depth: 0, html: "", task: "" })

    implicitHeight: column.implicitHeight

    PageTools { id: tools; workspace: Workspace }
    LinkPreview {
        id: preview
        host: root.found ? Workspace : null
        href: root.found ? "page:" + Workspace.pageTitle(root.journalPath) : ""
        sourcePath: root.journalPath
        palette: ({
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
        locale: DatesHelper.locale
        now: DatesHelper.fixedNow
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.space1

        // The date: a link to that day's journal.
        BText {
            objectName: "yearAgoDate"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            text: root.yearAgo !== "" ? DatesHelper.formatLong(root.yearAgo) : ""
            font.family: Theme.fontMono
            font.pixelSize: Theme.textXs
            font.underline: root.found && dateHover.hovered
            color: root.found ? Theme.link : Theme.fgFaint
            elide: Text.ElideRight
            Accessible.role: root.found ? Accessible.Link : Accessible.StaticText
            Accessible.onPressAction: if (root.found) AppController.openJournal(root.yearAgo)
            HoverHandler { id: dateHover; enabled: root.found; cursorShape: Qt.PointingHandCursor }
            TapHandler { enabled: root.found; onTapped: AppController.openJournal(root.yearAgo) }
        }
        BText {
            visible: !root.found
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            text: qsTr("No journal on that day.")
            variant: "muted"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
        Repeater {
            model: root.shownEntries
            delegate: BRichText {
                id: entry
                required property int index
                required property var modelData
                visible: entry.index < root.limit && AppController.routeText(entry.modelData, "kind") !== "header"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space2 + AppController.routeInt(entry.modelData, "depth", 0) * Theme.space3
                Layout.preferredHeight: visible ? implicitHeight : 0
                html: AppController.routeText(entry.modelData, "html")
                font.family: Theme.fontEditor
                font.pixelSize: Theme.textSm
                color: Theme.fgMuted
                maximumLineCount: 3
                lineHeight: Theme.lineHeightPx(Theme.textSm, Theme.lineHeight)
                // (A click anywhere opens that day: the excerpt is a teaser, not a place to work.)
                TapHandler { onTapped: AppController.openJournal(root.yearAgo) }
            }
        }
    }
}
