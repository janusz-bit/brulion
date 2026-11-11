pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PlanningTokenMenu — the token and link menus of the planning views (Tasks, the calendar): a
// right-click or long press on a task's date, reminder, 🔁 or priority chip (TasksRow), on a
// calendar entry (PlanningItemRow, the week grid) opens DateChipMenu for that token; on a #tag or
// a [[link]] of a task's text LinkMenu. The change is written to the task's file like a hand edit
// (PlanningActions.editToken: only the token's bytes) and undone with PlanningActions.undo() —
// changed() lets the view show a toast with "Undo". "Change…" opens the editor's dialogs
// (EditorDialogs, without an editor). On a date of a recurring series (the calendar's other
// dates than the written one) the date moves only as a whole series ("Change the whole series…"), and a
// reminder's "An hour later" / "Tomorrow at this time" snooze that one time once it has rung (a
// one-off child reminder, like the notification's "Snooze": PlanningActions.snoozeReminder) — a
// time still to come offers no snooze, it would ring as well. A value that is no token of the
// file (a Logseq SCHEDULED: line) opens no menu (PlanningActions.tokenMenu()).
//
//   PlanningTokenMenu { id: tokenMenu; actions: planningActions
//                       onChanged: (text) => …a toast with "Undo": planningActions.undo()
//                       onFilterRequested: (tag) => taskModel.tagFilter = tag }
//   tokenMenu.openToken({ path, line, text, kind, value, alarm, rule, priority, isTask, occurrence, date, time }, item, x, y)
//   tokenMenu.openLink(url, fromPath, item, x, y, filterable)   // filterable: "Filter by tag" first
//   tokenMenu.openAddDate(path, line, text, item, x, y)   // a task without a date: QuickDateMenu
//
// "Add date" (a Tasks row's chip on a task without a date): Today, Tomorrow and Next week write the
// task's 📅 due date at once, "Pick a date…" opens the date dialog (PlanningActions.setTaskDate;
// changed() with "Undo" as for the token menus).
Item {
    id: root

    property PlanningActions actions
    /// A token was changed (`text`: what, for the toast); PlanningActions.undo() puts it back.
    signal changed(string text)
    /// Something to say without an undo (a copied link, why nothing changed).
    signal message(string text, string kind)
    /// "Filter by tag" of a link menu opened with `filterable`: filter the view by `tag`.
    signal filterRequested(string tag)

    // The token whose menu is open (see openToken()).
    property string targetPath
    property int targetLine: 0
    property string targetText
    property string targetKind
    property string targetValue
    property bool targetAlarm: false
    property string targetRule
    property string targetPriority
    property bool targetOccurrence: false
    property string targetDate
    property string targetTime
    /// The time of an occurrence (a calendar entry of a recurring reminder): the reminder's, 09:00 for none.
    readonly property string occurrenceTime: targetTime !== "" ? targetTime : "09:00"
    property DateChipMenu dateMenu
    property QuickDateMenu quickDateMenu
    property LinkMenu linkMenu
    // The link whose menu is open (see openLink()).
    property string linkUrl
    property string linkFromPath
    property bool linkFilterable: false
    property string linkTag

    EditorDialogs {
        id: dialogs
        editor: null
        controller: null
    }
    // openToken()'s token and PlanningActions.tokenMenu()'s answer, read by key.
    MapHolder { id: tokenHolder }
    MapHolder { id: infoHolder }
    // The dialogs' answers to the requests made here ("planning").
    Connections {
        target: dialogs
        function onApplied(target: string, action: string, a: var, b: var) {
            if (target === "planning")
                root.run(action, a, b)
            else if (target === "planningAdd" && action === "set")
                root.addDate("" + a)
        }
    }

    /// Open the token menu for `t` at (x, y) of `item`: { path, line, text (the task's plain text:
    /// finds it again when its line moved), kind ("due" | "scheduled" | "start" | "done" |
    /// "reminder" | "recurrence" | "priority"), value (the token's value as the models give it),
    /// alarm, rule, priority, isTask, occurrence (a series' other date), date, time }.
    function openToken(t: var, item: Item, x: real, y: real): bool {
        // (Read into the target's properties first: compiled code passes no value read before
        // another call.)
        tokenHolder.map = t
        if (!actions || AppController.routeText(tokenHolder.map, "path") === ""
                || AppController.routeText(tokenHolder.map, "value") === ""
                || AppController.routeText(tokenHolder.map, "kind") === "journal")
            return false
        targetPath = AppController.routeText(tokenHolder.map, "path")
        targetLine = AppController.routeInt(tokenHolder.map, "line", 0)
        targetText = AppController.routeText(tokenHolder.map, "text")
        targetKind = AppController.routeText(tokenHolder.map, "kind")
        targetValue = AppController.routeText(tokenHolder.map, "value")
        // The token as the file has it (the block's rule, a Logseq priority); none written as a
        // token (a Logseq SCHEDULED: line, a .+1w repeater): no menu.
        infoHolder.map = actions.tokenMenu(targetPath, targetLine, targetText, targetKind, targetValue)
        if (infoHolder.map.rule === undefined)
            return false
        targetAlarm = tokenHolder.map.alarm === true
        targetRule = AppController.routeText(infoHolder.map, "rule")
        targetPriority = AppController.routeText(infoHolder.map, "priority")
        targetOccurrence = tokenHolder.map.occurrence === true
        targetDate = AppController.routeText(tokenHolder.map, "date")
        targetTime = AppController.routeText(tokenHolder.map, "time")
        if (!dateMenu)
            dateMenu = dateMenuComponent.createObject(root) as DateChipMenu
        dateMenu.kind = targetKind
        dateMenu.alarm = targetAlarm
        dateMenu.rule = targetRule
        dateMenu.priority = targetPriority
        dateMenu.logseq = infoHolder.map.logseq === true
        dateMenu.isTask = tokenHolder.map.isTask === true
        dateMenu.occurrence = targetOccurrence
        // A later time of a recurring reminder that has not come yet would still ring: no snooze.
        dateMenu.snoozable = !(targetOccurrence && targetKind === "reminder"
                               && DatesHelper.compare(occurrenceAt(), DatesHelper.now()) > 0)
        dateMenu.popup(item, x, y)
        return true
    }
    /// When the open token's occurrence (a calendar entry of a recurring reminder) rings:
    /// "YYYY-MM-DD HH:mm".
    function occurrenceAt(): string {
        return targetDate + " " + occurrenceTime
    }

    /// The quick date menu for the task on `line` of `path` (`text`: its plain text, finds it again
    /// when the line moved) at (x, y) of `item`.
    function openAddDate(path: string, line: int, text: string, item: Item, x: real, y: real): bool {
        if (!actions || path === "")
            return false
        targetPath = path
        targetLine = line
        targetText = text
        if (!quickDateMenu)
            quickDateMenu = quickDateComponent.createObject(root) as QuickDateMenu
        quickDateMenu.popup(item, x, y)
        return true
    }
    /// The open "Add date" task gets `date` ("YYYY-MM-DD[ HH:mm]") as its due date.
    function addDate(date: string): void {
        if (actions.setTaskDate(targetPath, targetLine, targetText, "due", date))
            changed(qsTr("Due date set: %1").arg(DatesHelper.formatHuman(date)))
    }
    /// The link menu of `url` (a tag:, page:, part: — a path part of a link —, block: or ext: link
    /// of a task's text written on `fromPath`); `filterable` makes "Filter by tag" the first item of
    /// a tag's menu.
    function openLink(url: string, fromPath: string, item: Item, x: real, y: real, filterable: bool): bool {
        if (!actions)
            return false
        const part = Js.startsWith(url, "part:")
        const kind = Js.startsWith(url, "tag:") ? "tag" : Js.startsWith(url, "page:") || part ? "page"
                   : Js.startsWith(url, "block:") ? "block" : Js.startsWith(url, "ext:") ? "ext" : ""
        if (kind === "")
            return false
        const partTarget = part ? actions.linkPartTarget(url, fromPath) : ""
        if (part && partTarget === "")
            return false // a malformed part: link
        const partFolder = partTarget === "folder"
        linkUrl = url
        linkFromPath = fromPath
        linkFilterable = filterable
        linkTag = kind === "tag" ? Js.decodeURIComponent(Js.sliceFrom(url, 4)) : ""
        if (!linkMenu)
            linkMenu = linkMenuComponent.createObject(root) as LinkMenu
        linkMenu.kind = kind
        // A [[link]] to a page that does not exist offers "Create page", as in the editor; a
        // path part naming a folder "Show folder".
        linkMenu.unresolved = kind === "page" && actions.linkMissing(url, fromPath)
        linkMenu.folder = partFolder
        linkMenu.filterFirst = kind === "tag" && filterable
        linkMenu.popup(item, x, y)
        return true
    }

    /// Write `action` on the open menu's token; says what changed.
    function run(action: string, a: var, b: var): void {
        if (!actions.editToken(targetPath, targetLine, targetText, targetKind, targetValue, action, a, b))
            return
        changed(action === "recurrence" || action === "blockRecurrence" ? qsTr("Repeat changed")
                : action === "priority" || action === "blockPriority" ? qsTr("Priority changed")
                : action === "remove" ? qsTr("Removed")
                : action === "alarm" || action === "reminder" ? qsTr("Reminder changed")
                : qsTr("Date changed"))
    }
    function snooze(at: string): void {
        if (actions.snoozeReminder(targetPath, targetLine, targetText, targetValue, at))
            changed(qsTr("Reminder snoozed until %1").arg(DatesHelper.formatHuman(at)))
    }
    function choose(action: string, a: var, b: var): void {
        switch (action) {
        case "change":
            if (targetKind === "reminder")
                dialogs.editReminderWith("planning", targetValue, targetAlarm, targetRule, targetText)
            else if (targetKind === "recurrence")
                dialogs.pickRecurrenceWith("planning", targetRule, "recurrence")
            else
                dialogs.pickDateWith("planning", targetKind, targetValue)
            break
        case "blockRecurrenceChange":
            dialogs.pickRecurrenceWith("planning", targetRule, "blockRecurrence")
            break
        default:
            // One time of a recurring reminder (the calendar's other dates): snoozed, the series stays.
            if ((action === "shift" || action === "move") && targetKind === "reminder" && targetOccurrence)
                snoozeOccurrence(action === "shift", 0 + a, 0 + b)
            else
                run(action, a, b)
            break
        }
    }
    /// One time of a recurring reminder moved by `minutes` (`shift`) or to `days` from today.
    function snoozeOccurrence(shift: bool, days: real, minutes: real): void {
        if (shift)
            snooze(DatesHelper.addMinutes(occurrenceAt(), minutes))
        else
            snooze(DatesHelper.addDays(DatesHelper.today, days) + " " + occurrenceTime)
    }
    /// The open link in a new tab or window (OpenIn).
    function openLinkIn(target: string): void {
        OpenIn.begin(target)
        actions.openLink(linkUrl, linkFromPath, false)
        OpenIn.end()
    }
    function chooseLink(action: string): void {
        switch (action) {
        case "filter": if (linkFilterable) root.filterRequested(linkTag); break
        case "open": actions.openLink(linkUrl, linkFromPath, false); break
        case "openSide": actions.openLink(linkUrl, linkFromPath, true); break
        case "openTab": root.openLinkIn("tab"); break
        case "openWindow": root.openLinkIn("window"); break
        case "overview": AppController.navigate({ view: "tag", tag: linkTag, overview: true }); break
        case "tasks": if (linkMenu) AppController.navigate({ view: "tasks", query: linkMenu.tasksQuery(linkTag) }); break
        case "copy": root.copyLink(actions.linkText(linkUrl, linkFromPath)); break
        }
    }
    /// Copy a link's text (its menu's "Copy link").
    function copyLink(text: string): void {
        if (actions.copyText(text))
            message(qsTr("Copied %1").arg(text), "success")
        else
            message(qsTr("Could not copy: the clipboard is not available"), "error")
    }

    Component {
        id: dateMenuComponent
        DateChipMenu {
            onChosen: (action, a, b) => root.choose(action, a, b)
        }
    }
    Component {
        id: quickDateComponent
        QuickDateMenu {
            onDateChosen: (date) => root.addDate(date)
            onPickRequested: dialogs.pickNewDateWith("planningAdd", "due")
        }
    }
    Component {
        id: linkMenuComponent
        LinkMenu {
            onChosen: (action) => root.chooseLink(action)
        }
    }
}
