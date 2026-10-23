pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// RefreshSelfTest — views that follow a value they read only to know when to compute again, in
// the editor playground (`brulion --editor --editor-script refreshtest`; ctest
// tst_editor_qml_refresh, desktop, and tst_editor_qml_refresh_touch, a phone with --touch). Each
// of these bindings once read that value with a bare `void x` statement, which qmlcachegen
// compiles away together with the binding's dependency (docs/UI.md, "A binding that has to follow
// a value it does not otherwise use…"):
//   a query's results replaced by as many rows of another height (the results' list height);
//   an embedded section changed in its own note (TransclusionView's heights);
//   a date picker's today marker after midnight (BDatePicker.cells);
//   the reminder dialog's weekday buttons and repeat rule after a language change, and its date
//     picker's weekday labels and month title;
//   a calculation's results beside their lines after the window narrowed (the lines wrap again);
//   (touch) the handles of a "Zaznacz tekst" selection after the window narrowed.
// The window narrows step by step until the text in question wraps again: which width does that
// depends on the fonts. Then on until the page's header changes its height (smaller paddings in a
// compact window, the title wrapping again): the view, moved down the page by the test as the
// caret moved with the keys moves it (no drag or wheel), does not go back to the top.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property real heightBefore: 0
    property int rowsBefore: 0
    property string nowBefore: ""
    property string languageBefore: ""
    property var namesBefore: []
    property real widthBefore: 0
    property int calcRow: -1
    property real calcLinesBefore: 0
    property Item calcDelegate: null
    property real headerBefore: 0
    property var handlesBefore: null

    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function near(a, b) { return Math.abs(a - b) < 0.5 }
    function same(a, b) {
        if (!a || !b || a.length !== b.length)
            return false
        for (let i = 0; i < a.length; ++i) {
            if (a[i] !== b[i])
                return false
        }
        return true
    }
    /// Items under `item` (depth first) for which `match` holds.
    function itemsWhere(item, match, out) {
        out = out || []
        if (!item)
            return out
        if (match(item))
            out.push(item)
        for (let i = 0; i < item.children.length; ++i)
            itemsWhere(item.children[i], match, out)
        return out
    }
    /// The heights of `items` stacked with `spacing` between them (a Column's height).
    function stacked(items, spacing) {
        let h = 0
        for (let i = 0; i < items.length; ++i)
            h += items[i].height + (i > 0 ? spacing : 0)
        return h
    }

    // ---- The query's results -------------------------------------------------------------------
    readonly property string longTask: "Kupić fugę epoksydową w kolorze piaskowym, dwa opakowania po pięć kilogramów, "
                                      + "i silikon sanitarny do narożników, sprawdzić też, czy sklep przywiezie "
                                      + "wszystko w piątek przed południem razem z płytkami i klejem"
    function resultList() {
        const d = window.delegateAt(window.viewRow("query"))
        return d ? window.findItem(d, "queryResultList") : null
    }
    function resultRows() {
        const list = resultList()
        const out = []
        for (let i = 0; list && i < list.children.length; ++i) {
            if (list.children[i].kind !== undefined)
                out.push(list.children[i])
        }
        return out
    }
    function resultsStacked() {
        const list = resultList()
        return list !== null && near(list.height, stacked(resultRows(), list.spacing))
    }

    // ---- The embedded section ("Budżet na październik": ![[Budżet domowy#Październik]]) --------
    function transclusion() {
        const d = window.delegateAt(window.rowOf("Budżet na październik") + 1)
        return d ? d.transclusionView : null
    }
    function frames() { return itemsWhere(transclusion(), i => i.entries !== undefined && i !== transclusion()) }
    function entryRows(frame) { return itemsWhere(frame, i => i.nested !== undefined) }
    /// Every frame holds all its rows, the view all its frames, the block the view.
    function embedFits() {
        const view = transclusion()
        const all = frames()
        if (!view || all.length === 0 || !near(view.height, stacked(all, view.spacing)))
            return false
        for (const frame of all) {
            const rows = entryRows(frame)
            const column = rows.length > 0 ? rows[0].parent : null
            if (!column || frame.height < column.height || rows.some(r => r.y + r.height > column.height + 0.5))
                return false
        }
        const block = window.delegateAt(window.rowOf("Budżet na październik") + 1)
        return block.height >= view.y + view.height
    }

    // ---- The reminder dialog and its date picker ----------------------------------------------
    function dialog() { return editor.dialogs.reminderDialog }
    function picker() { return dialog() ? window.findItem(dialog().contentItem, "reminderDatePicker") : null }
    function weekdays() { return dialog() ? window.findItem(dialog().contentItem, "reminderWeekdays") : null }
    function todayCell() {
        const p = picker()
        const cell = p ? p.cells.find(c => c.isToday) : undefined
        return cell ? cell.date : ""
    }
    function pickerLabels() {
        const p = picker()
        const names = DatesHelper.weekdayNames("short")
        const out = []
        for (let i = 0; i < 7; ++i)
            out.push(names[(p.weekStart - 1 + i) % 7])
        return out
    }
    function pickerTitle() {
        const p = picker()
        const t = DatesHelper.monthTitle(p.displayYear, p.displayMonth)
        return t.charAt(0).toLocaleUpperCase() + t.slice(1)
    }
    /// Everything the dialog shows in the language's own words, as computed now.
    function dialogInLanguage() {
        const p = picker()
        const row = weekdays()
        const title = p ? window.findItem(p, "datePickerMonthTitle") : null
        return p && row && title && same(row.shortNames, DatesHelper.weekdayNames("short"))
                && same(row.fullNames, DatesHelper.weekdayNames("full"))
                && dialog().ruleLabel === DatesHelper.formatRule(dialog().rule)
                && same(p.weekdayLabels, pickerLabels()) && title.text === pickerTitle()
    }

    // ---- A calculation with a long first line --------------------------------------------------
    readonly property string calcSource: "```calc\n"
        + "materiały = 1200 zł + 340 zł + 89 zł * 6,5 // płytki, klej, fuga, hydroizolacja, listwy wykończeniowe, "
        + "silikon sanitarny i transport z marketu budowlanego na trzecie piętro bez windy\n"
        + "zapas = 200 zł\n"
        + "suma\n"
        + "```\n"
    function lastCalcRow() {
        for (let r = page.blocks.count - 1; r >= 0; --r) {
            if (page.blocks.get(r).view === "calc")
                return r
        }
        return -1
    }
    function calcInput() {
        const d = window.delegateAt(calcRow)
        return d ? window.findItem(d, "calcInput") : null
    }
    function calcResults() {
        const d = window.delegateAt(calcRow)
        return window.findAll(d, "calcResult").filter(r => r.visible)
    }
    /// Each result on its line: as high as the line's start in the (wrapped) input.
    function calcAligned() {
        const input = calcInput()
        const results = calcResults()
        return input !== null && results.length === 3 && results.every(r => {
            const line = r.offset <= input.length ? input.positionToRectangle(r.offset).y : 0
            return near(r.y, input.y + line)
        })
    }
    /// How far down the input the second line starts (the first one wraps).
    function secondLineY() {
        const input = calcInput()
        const text = input ? String(input.text) : ""
        return input ? input.positionToRectangle(text.indexOf("\n") + 1).y : 0
    }
    /// The calculation's block as it was when the window narrowed (a new one would place its
    /// results from scratch: the check is that the same one follows).
    function sameCalc() { return calcDelegate !== null && window.delegateAt(calcRow) === calcDelegate }
    /// Where the calculation stands now (the checks' messages).
    function calcState() {
        return "window " + window.width + " px; second line at " + secondLineY() + " px, was " + calcLinesBefore
               + "; results at " + calcResults().map(r => r.y).join(", ") + (sameCalc() ? "" : "; another block")
    }
    /// The list's header (the page's title, its folder).
    function headerHeight() { return editor.listView.headerItem ? editor.listView.headerItem.height : 0 }
    /// The view shows the top of the page.
    function atTop() { return editor.listView.contentY <= editor.listView.originY + 1 }
    /// A step narrowing the window by a tenth, again on the next steps while `more()` (at most down
    /// to 160 px): the width at which text wraps again depends on the fonts.
    function narrowWhile(more) {
        return () => {
            if (more() && window.width > 160) {
                window.width = Math.max(160, Math.round(window.width * 0.9))
                window.stepIndex -= 1
            }
        }
    }

    // ---- "Zaznacz tekst" (touch UIs: its handles) ----------------------------------------------
    function selectText() { return window.findItem(editor.dockHost, "selectTextEdit") }
    function handle(name) { return window.findItem(editor.dockHost, name) }
    /// The selection's start is somewhere else than before (the text wrapped again).
    function selectionMoved() {
        const text = selectText()
        const at = text ? text.positionToRectangle(text.selectionStart) : null
        return at !== null && (!near(at.x, handlesBefore.x) || !near(at.y, handlesBefore.y))
    }
    /// Both handles where the selection's ends are, as computed now.
    function handlesOnEnds() {
        const text = selectText()
        const start = handle("selectTextHandleStart")
        const end = handle("selectTextHandleEnd")
        if (!text || !start || !end)
            return false
        const a = text.positionToRectangle(text.selectionStart)
        const b = text.positionToRectangle(text.selectionEnd)
        return near(start.at.x, a.x) && near(start.at.y, a.y) && near(end.at.x, b.x) && near(end.at.y, b.y)
    }

    Component.onCompleted: window.runSteps(steps())

    function steps() {
        const out = [
            wait(() => page.loaded && window.viewRow("query") > 0, "the sample page is open"),

            // ---- A query's results: as many rows again, one of them taller -----------------------------
            () => editor.listView.positionViewAtIndex(window.viewRow("query"), ListView.Beginning),
            wait(() => resultRows().length > 0 && resultsStacked(), "the query shows the open #zakupy tasks"),
            () => {
                rowsBefore = resultRows().length
                host.externalWrite("Fuga.md", "- [ ] Kupić fugę #zakupy\n")
            },
            wait(() => resultRows().length === rowsBefore + 1 && resultsStacked(),
                 "a task written in another note joins the results"),
            () => {
                heightBefore = resultList().height
                host.externalWrite("Fuga.md", "- [ ] " + test.longTask + " #zakupy\n")
            },
            wait(() => resultRows().some(r => r.text.indexOf("epoksydową") >= 0), "the task changed in its note shows changed"),
            wait(() => resultRows().length === rowsBefore + 1 && resultList().height > heightBefore + 10 && resultsStacked(),
                 () => "the results grow with a row that got taller, as many rows as before (list "
                       + resultList().height + " px, rows " + stacked(resultRows(), resultList().spacing) + " px)"),

            // ---- An embedded section changed in its own note --------------------------------------------
            () => editor.listView.positionViewAtIndex(window.rowOf("Budżet na październik"), ListView.Beginning),
            wait(() => frames().length === 1 && entryRows(frames()[0]).length === 3 && embedFits(),
                 "the embedded section shows its three blocks"),
            () => {
                heightBefore = transclusion().height
                host.externalWrite("Budżet domowy.md", "# Budżet domowy\n\n## Październik\n\n"
                                   + "- Remont: **12 000 zł** #finanse ^bud01\n- Jedzenie: 1 800 zł\n"
                                   + "- Rachunki: prąd, gaz, woda i internet — razem około 640 zł, płatne do "
                                   + "dziesiątego dnia miesiąca przelewem z konta wspólnego\n- Bilety: 230 zł\n"
                                   + "- [ ] Opłacić czynsz 📅 2026-10-20 #finanse\n\n## Listopad\n\n- Prezenty: 400 zł\n")
            },
            wait(() => frames().length === 1 && entryRows(frames()[0]).length === 5 && embedFits()
                       && transclusion().height > heightBefore,
                 () => "the embed follows its source in another note: its frame and the block grow (view "
                       + (transclusion() ? transclusion().height : -1) + " px, was " + heightBefore + ")"),

            // ---- The date picker after midnight -----------------------------------------------------------
            () => {
                nowBefore = DatesHelper.fixedNow
                editor.dialogs.createReminder(window.rowOf("Zadzwonić do hydraulika"), false)
            },
            wait(() => dialog() !== null && dialog().opened && picker() !== null, "the reminder dialog opens"),
            () => check(todayCell() === DatesHelper.today, "its date picker marks today (" + todayCell() + ")"),
            () => DatesHelper.fixedNow = DatesHelper.addDays(DatesHelper.today, 1) + " 00:05",
            wait(() => todayCell() === DatesHelper.today,
                 () => "after midnight the date picker marks the new day (" + todayCell() + ", today is "
                       + DatesHelper.today + ")"),
            () => DatesHelper.fixedNow = nowBefore,
            wait(() => todayCell() === DatesHelper.today, "and the day before again when the clock is set back"),

            // ---- A language change while the dialog exists ------------------------------------------------
            () => dialog().repeatKind = "weekly",
            wait(() => weekdays() !== null && weekdays().visible && dialogInLanguage(),
                 "a weekly repeat: the weekday buttons, the rule and the picker in the app's language"),
            () => {
                languageBefore = DevTools.language
                namesBefore = weekdays().shortNames
                DevTools.setLanguage(languageBefore === "en" ? "pl" : "en")
            },
            wait(() => DatesHelper.locale !== languageBefore && dialogInLanguage() && !same(weekdays().shortNames, namesBefore),
                 () => "the dialog follows a language change: weekdays " + weekdays().shortNames.join(" ") + ", rule „"
                       + dialog().ruleLabel + "”, picker " + picker().weekdayLabels.join(" ")),
            () => DevTools.setLanguage(languageBefore),
            wait(() => DatesHelper.locale === languageBefore && dialogInLanguage() && same(weekdays().shortNames, namesBefore),
                 "and back"),
            () => dialog().close(),
            wait(() => !dialog().visible, "the dialog closes"),

            // ---- A calculation's results after the window narrowed ----------------------------------------
            () => {
                page.save()
                host.externalWrite(page.path, host.file(page.path) + "\n" + test.calcSource)
            },
            wait(() => (calcRow = lastCalcRow()) > window.viewRow("calc"), "a second calculation merged in from another device"),
            () => editor.listView.positionViewAtIndex(calcRow, ListView.Beginning),
            wait(() => calcAligned(), "its results sit on their lines"),
            () => {
                calcLinesBefore = secondLineY()
                calcDelegate = window.delegateAt(calcRow)
                widthBefore = window.width
            },
            narrowWhile(() => secondLineY() <= calcLinesBefore + 1),
            wait(() => sameCalc() && secondLineY() > calcLinesBefore + 1 && calcAligned(),
                 () => "the window narrowed, the first line wraps again: the results follow their lines (" + calcState() + ")"),
            () => window.width = widthBefore,
            wait(() => sameCalc() && near(secondLineY(), calcLinesBefore) && calcAligned(),
                 () => "and back at the old width (" + calcState() + ")"),

            // ---- The page's header changing its height while the view is further down ---------------------
            () => {
                headerBefore = headerHeight()
                check(!atTop(), "the view is down the page, at the calculation")
            },
            narrowWhile(() => near(headerHeight(), headerBefore)),
            wait(() => !near(headerHeight(), headerBefore) && !atTop(),
                 () => "the window narrowed to " + window.width + " px, the header changed its height ("
                       + headerHeight() + " px, was " + headerBefore + "): the view stays down the page ("
                       + editor.listView.contentY + ", top " + editor.listView.originY + ")"),
            () => window.width = widthBefore,
            wait(() => near(headerHeight(), headerBefore) && !atTop(), "and back at the old width"),
        ]
        if (Theme.touchUi) {
            out.push(
                () => editor.openSelectText(),
                wait(() => selectText() !== null && selectText().visible, "“Zaznacz tekst” opens"),
                () => {
                    const text = selectText()
                    const at = String(text.getText(0, text.length)).indexOf("inspiracje")
                    text.select(at, at + 10)
                },
                wait(() => handle("selectTextHandleEnd") !== null && handle("selectTextHandleEnd").visible && handlesOnEnds(),
                     "its handles sit on the selection's ends"),
                () => {
                    const text = selectText()
                    handlesBefore = text.positionToRectangle(text.selectionStart)
                    widthBefore = window.width
                },
                narrowWhile(() => !selectionMoved()),
                wait(() => selectionMoved() && handlesOnEnds(),
                     () => "the window narrowed to " + window.width + " px, the text wraps again: the handles follow "
                           + "the selection (" + handle("selectTextHandleStart").at + ")"),
                () => window.width = widthBefore,
                wait(() => handlesOnEnds(), "and back at the old width"),
                () => editor.closeSelectText())
        }
        return out
    }
}
