pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PlanningTokenMenu — the token and link menus of the planning views (Tasks, the calendar): a
// right-click or long press on a task's date, reminder, 🔁 or priority chip (TasksRow), on a
// calendar entry (PlanningItemRow, the week grid) opens DateChipMenu for that token; on a #tag or
// a [[link]] of a task's text LinkMenu. The change is written to the task's file like a hand edit
// (PlanningActions.editToken: only the token's bytes) and undone with PlanningActions.undo() —
// changed() lets the view show a toast with "Cofnij". "Zmień…" opens the editor's dialogs
// (EditorDialogs, without an editor). On a date of a recurring series (the calendar's other
// dates than the written one) the date moves only as a whole series ("Zmień całą serię…"), and a
// reminder's "Godzinę później" / "Jutro o tej porze" snooze that one time once it has rung (a
// one-off child reminder, like the notification's "Drzemka": PlanningActions.snoozeReminder) — a
// time still to come offers no snooze, it would ring as well. A value that is no token of the
// file (a Logseq SCHEDULED: line) opens no menu (PlanningActions.tokenMenu()).
//
//   PlanningTokenMenu { id: tokenMenu; actions: planningActions
//                       onChanged: (text) => toasts.show(text, { actionText: qsTr("Undo"), onAction: () => planningActions.undo() }) }
//   tokenMenu.openToken({ path, line, text, kind, value, alarm, rule, priority, isTask, occurrence, date, time }, item, x, y)
//   tokenMenu.openLink(url, fromPath, item, x, y, filter)   // filter: function(tag) or null
Item {
    id: root

    property PlanningActions actions
    /// A token was changed (`text`: what, for the toast); PlanningActions.undo() puts it back.
    signal changed(string text)
    /// Something to say without an undo (a copied link, why nothing changed).
    signal message(string text, string kind)

    /// The token whose menu is open (see openToken()).
    property var target: ({})
    property DateChipMenu dateMenu: null
    property LinkMenu linkMenu: null
    property var linkTarget: ({})

    EditorDialogs {
        id: dialogs
        editor: null
        controller: null
    }

    /// Open the token menu for `t` at (x, y) of `item`: { path, line, text (the task's plain text:
    /// finds it again when its line moved), kind ("due" | "scheduled" | "start" | "done" |
    /// "reminder" | "recurrence" | "priority"), value (the token's value as the models give it),
    /// alarm, rule, priority, isTask, occurrence (a series' other date), date, time }.
    function openToken(t, item, x, y) {
        if (!actions || !t || !t.path || !t.value || t.kind === "journal")
            return false
        // The token as the file has it (the block's rule, a Logseq priority); none written as a
        // token (a Logseq SCHEDULED: line, a .+1w repeater): no menu.
        const info = actions.tokenMenu(t.path, t.line, t.text || "", t.kind, t.value)
        if (info.rule === undefined)
            return false
        target = Object.assign({}, t, { rule: info.rule, priority: info.priority })
        if (!dateMenu)
            dateMenu = dateMenuComponent.createObject(root) as DateChipMenu
        dateMenu.kind = t.kind
        dateMenu.alarm = t.alarm === true
        dateMenu.rule = info.rule
        dateMenu.priority = info.priority
        dateMenu.logseq = info.logseq === true
        dateMenu.isTask = t.isTask === true
        dateMenu.occurrence = t.occurrence === true
        // A later time of a recurring reminder that has not come yet would still ring: no snooze.
        dateMenu.snoozable = !(t.occurrence === true && t.kind === "reminder"
                               && DatesHelper.compare(occurrenceAt(t), DatesHelper.now()) > 0)
        dateMenu.popup(item, x, y)
        return true
    }
    /// When the occurrence `t` (a calendar entry of a recurring reminder) rings: "YYYY-MM-DD HH:mm".
    function occurrenceAt(t) {
        return t.date + " " + (t.time || "09:00")
    }

    /// The link menu of `url` (a tag:, page:, block: or ext: link of a task's text written on
    /// `fromPath`); `filter(tag)`, when given, makes "Filtruj po tagu" its first item.
    function openLink(url, fromPath, item, x, y, filter) {
        if (!actions)
            return false
        const kind = url.startsWith("tag:") ? "tag" : url.startsWith("page:") ? "page"
                   : url.startsWith("block:") ? "block" : url.startsWith("ext:") ? "ext" : ""
        if (kind === "")
            return false
        linkTarget = { url: url, fromPath: fromPath || "", filter: filter || null,
                       tag: kind === "tag" ? decodeURIComponent(url.slice(4)) : "" }
        if (!linkMenu)
            linkMenu = linkMenuComponent.createObject(root) as LinkMenu
        linkMenu.kind = kind
        // A [[link]] to a page that does not exist offers "Utwórz stronę", as in the editor.
        linkMenu.unresolved = kind === "page" && actions.linkMissing(url, fromPath || "")
        linkMenu.filterFirst = kind === "tag" && !!filter
        linkMenu.popup(item, x, y)
        return true
    }

    /// Write `action` on the open menu's token; says what changed.
    function run(action, a, b) {
        const t = target
        if (!actions.editToken(t.path, t.line, t.text || "", t.kind, t.value, action, a, b))
            return
        changed(action === "recurrence" || action === "blockRecurrence" ? qsTr("Repeat changed")
                : action === "priority" || action === "blockPriority" ? qsTr("Priority changed")
                : action === "remove" ? qsTr("Removed")
                : action === "alarm" || action === "reminder" ? qsTr("Reminder changed")
                : qsTr("Date changed"))
    }
    function snooze(at) {
        const t = target
        if (actions.snoozeReminder(t.path, t.line, t.text || "", t.value, at))
            changed(qsTr("Reminder snoozed until %1").arg(DatesHelper.formatHuman(at)))
    }
    function choose(action, a, b) {
        const t = target
        switch (action) {
        case "change":
            if (t.kind === "reminder")
                dialogs.editReminderWith(root.run, { at: t.value, alarm: t.alarm === true, rule: t.rule || "",
                                                     context: t.text || "" })
            else if (t.kind === "recurrence")
                dialogs.pickRecurrenceWith((act, rule) => root.run(act, rule, 0), t.rule || "", "recurrence")
            else
                dialogs.pickDateWith(root.run, t.kind, t.value)
            break
        case "blockRecurrenceChange":
            dialogs.pickRecurrenceWith((act, rule) => root.run(act, rule, 0), t.rule || "", "blockRecurrence")
            break
        case "shift":
        case "move":
            // One time of a recurring reminder (the calendar's other dates): snoozed, the series stays.
            if (t.kind === "reminder" && t.occurrence) {
                const at = occurrenceAt(t)
                snooze(action === "shift" ? DatesHelper.addMinutes(at, b) : DatesHelper.addDays(DatesHelper.today, a) + " " + (t.time || "09:00"))
                break
            }
            run(action, a, b)
            break
        default:
            run(action, a, b)
            break
        }
    }
    function chooseLink(action) {
        const l = linkTarget
        switch (action) {
        case "filter": if (l.filter) l.filter(l.tag); break
        case "open": actions.openLink(l.url, l.fromPath, false); break
        case "openSide": actions.openLink(l.url, l.fromPath, true); break
        case "overview": AppController.navigate({ view: "tag", tag: l.tag, overview: true }); break
        case "tasks": if (linkMenu) AppController.navigate({ view: "tasks", query: linkMenu.tasksQuery(l.tag) }); break
        case "copy": {
            const text = actions.linkText(l.url, l.fromPath)
            if (actions.copyText(text))
                message(qsTr("Copied %1").arg(text), "success")
            else
                message(qsTr("Could not copy: the clipboard is not available"), "error")
            break
        }
        }
    }

    Component {
        id: dateMenuComponent
        DateChipMenu {
            onChosen: (action, a, b) => root.choose(action, a, b)
        }
    }
    Component {
        id: linkMenuComponent
        LinkMenu {
            onChosen: (action) => root.chooseLink(action)
        }
    }
}
