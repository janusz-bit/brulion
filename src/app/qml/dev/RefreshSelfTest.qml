pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
//   a calculation's results beside their lines after the window narrowed (the lines wrap again).
// The window narrows step by step until the text in question wraps again: which width does that
// depends on the fonts. Then on until the page's header changes its height (smaller paddings in a
// compact window, the title wrapping again): the view, moved down the page by the test as the
// caret moved with the keys moves it (no drag or wheel), does not go back to the top.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property real heightBefore: 0
    property int rowsBefore: 0
    property string nowBefore: ""
    property string languageBefore: ""
    /// The weekday buttons' short names before the language changed, joined by "|".
    property string namesBefore: ""
    property real widthBefore: 0
    property int calcRow: -1
    property real calcLinesBefore: 0
    property Item calcDelegate: null
    property real headerBefore: 0
    /// A caret's rectangle (read through a typed property).
    property rect caret

    function check(ok: bool, what: string): void { window.check(ok, what) }
    function near(a: real, b: real): bool { return Math.abs(a - b) < 0.5 }
    function delegateAt(r: int): BlockDelegate { return window.delegateAt(r) }
    function info(r: int): DevBlock { return window.info(r) }
    function namedAt(item: Item, name: string, index: int): Item { return window.namedAt(item, name, index) }
    function findItem(item: Item, name: string): Item { return window.findItem(item, name) }
    function list(): ListView { return editor.listView }
    function dialogs(): EditorDialogs { return editor.dialogs as EditorDialogs }

    // ---- The query's results -------------------------------------------------------------------
    readonly property string longTask: "Kupić fugę epoksydową w kolorze piaskowym, dwa opakowania po pięć kilogramów, "
                                      + "i silikon sanitarny do narożników, sprawdzić też, czy sklep przywiezie "
                                      + "wszystko w piątek przed południem razem z płytkami i klejem"
    function resultList(): Column {
        const d = delegateAt(window.viewRow("query"))
        return d ? findItem(d, "queryResultList") as Column : null
    }
    /// The result rows of the list (its children with a `kind`): how many, the `index`-th.
    function resultRowCount(): int {
        let n = 0
        for (let i = 0; i < window.childCount(resultList()); ++i) {
            if (DevTest.hasMember(window.childAt(resultList(), i), "kind"))
                ++n
        }
        return n
    }
    function resultRowAt(index: int): Item {
        let n = 0
        for (let i = 0; i < window.childCount(resultList()); ++i) {
            const child = window.childAt(resultList(), i)
            if (DevTest.hasMember(child, "kind") && n++ === index)
                return child
        }
        return null
    }
    /// The rows' heights stacked with the list's spacing between them (a Column's height).
    function resultRowsHeight(): real {
        let h = 0
        for (let i = 0; i < resultRowCount(); ++i)
            h += resultRowAt(i).height + (i > 0 ? resultList().spacing : 0)
        return h
    }
    function resultShows(needle: string): bool {
        for (let i = 0; i < resultRowCount(); ++i) {
            if (Js.indexOf("" + DevTest.property(resultRowAt(i), "text"), needle) >= 0)
                return true
        }
        return false
    }
    function resultsStacked(): bool {
        const list = resultList()
        return list !== null && near(list.height, resultRowsHeight())
    }

    // ---- The embedded section ("Budżet na październik": ![[Budżet domowy#Październik]]) --------
    function transclusion(): TransclusionView {
        const d = delegateAt(window.rowOf("Budżet na październik") + 1)
        return d ? DevTest.objectProperty(d, "transclusionView") as TransclusionView : null
    }
    /// The frames of the embed (items with `entries` under it): how many, the `index`-th.
    function frameCount(): int { return DevTest.countWithMember(transclusion(), "entries", false) }
    function frameAt(index: int): Item { return DevTest.withMemberAt(transclusion(), "entries", false, index) }
    /// The entry rows of a frame (items with `nested`).
    function entryRowCount(frame: Item): int { return DevTest.countWithMember(frame, "nested", true) }
    function entryRowAt(frame: Item, index: int): Item { return DevTest.withMemberAt(frame, "nested", true, index) }
    function framesHeight(spacing: real): real {
        let h = 0
        for (let i = 0; i < frameCount(); ++i)
            h += frameAt(i).height + (i > 0 ? spacing : 0)
        return h
    }
    /// Every frame holds all its rows, the view all its frames, the block the view.
    function embedFits(): bool {
        const view = transclusion()
        if (!view || frameCount() === 0 || !near(view.height, framesHeight(view.spacing)))
            return false
        for (let f = 0; f < frameCount(); ++f) {
            if (!frameFits(frameAt(f)))
                return false
        }
        const block = delegateAt(window.rowOf("Budżet na październik") + 1)
        return block.height >= view.y + view.height
    }
    function frameFits(frame: Item): bool {
        const column = entryRowCount(frame) > 0 ? entryRowAt(frame, 0).parent : null
        if (!column || frame.height < column.height)
            return false
        for (let i = 0; i < entryRowCount(frame); ++i) {
            const r = entryRowAt(frame, i)
            if (r.y + r.height > column.height + 0.5)
                return false
        }
        return true
    }

    // ---- The reminder dialog and its date picker ----------------------------------------------
    function dialog(): BReminderDialog { return dialogs().reminderDialog }
    function picker(): BDatePicker { return dialog() ? findItem(dialog().contentItem, "reminderDatePicker") as BDatePicker : null }
    function weekdays(): Item { return dialog() ? findItem(dialog().contentItem, "reminderWeekdays") : null }
    /// The weekday buttons' names (`which`: shortNames, fullNames) joined by `separator`.
    function names(which: string, separator: string): string {
        const row = weekdays()
        let out = ""
        for (let i = 0; i < DevTest.lengthAt(row, which); ++i)
            out += (i > 0 ? separator : "") + DevTest.valueAt(row, which + "." + i)
        return out
    }
    function todayCell(): string {
        const p = picker()
        for (let i = 0; p && i < DevTest.lengthAt(p.cells, ""); ++i) {
            if (DevTest.valueAt(p.cells, i + ".isToday") === true)
                return "" + DevTest.valueAt(p.cells, i + ".date")
        }
        return ""
    }
    function weekdayNames(form: string): list<string> { return DatesHelper.weekdayNames(form) }
    /// The weekday labels the picker should show (from its week's start), joined by "|".
    function pickerLabels(): string {
        const p = picker()
        let out = ""
        for (let i = 0; i < 7; ++i)
            out += (i > 0 ? "|" : "") + weekdayNames("short")[(p.weekStart - 1 + i) % 7]
        return out
    }
    function pickerTitle(): string {
        const p = picker()
        return capitalized(DatesHelper.monthTitle(p.displayYear, p.displayMonth))
    }
    function capitalized(t: string): string { return Js.toUpperCase(Js.charAt(t, 0)) + Js.sliceFrom(t, 1) }
    function joined(names: list<string>): string { return names.join("|") }
    /// The picker's weekday labels joined by `separator`.
    function pickerShown(separator: string): string {
        const p = picker()
        let out = ""
        for (let i = 0; i < DevTest.lengthAt(p, "weekdayLabels"); ++i)
            out += (i > 0 ? separator : "") + DevTest.valueAt(p, "weekdayLabels." + i)
        return out
    }
    /// Everything the dialog shows in the language's own words, as computed now.
    function dialogInLanguage(): bool {
        const p = picker()
        const row = weekdays()
        const title = p ? findItem(p, "datePickerMonthTitle") as Text : null
        if (!p || !row || !title)
            return false
        if (names("shortNames", "|") !== joined(weekdayNames("short")) || names("fullNames", "|") !== joined(weekdayNames("full")))
            return false
        if ("" + dialog().ruleLabel !== "" + DatesHelper.formatRule(dialog().rule))
            return false
        return pickerShown("|") === pickerLabels() && title.text === pickerTitle()
    }

    // ---- A calculation with a long first line --------------------------------------------------
    readonly property string calcSource: "```calc\n"
        + "materiały = 1200 zł + 340 zł + 89 zł * 6,5 // płytki, klej, fuga, hydroizolacja, listwy wykończeniowe, "
        + "silikon sanitarny i transport z marketu budowlanego na trzecie piętro bez windy\n"
        + "zapas = 200 zł\n"
        + "suma\n"
        + "```\n"
    function lastCalcRow(): int {
        for (let r = page.blocks.count - 1; r >= 0; --r) {
            if (info(r).view === "calc")
                return r
        }
        return -1
    }
    function calcInput(): TextEdit {
        const d = delegateAt(calcRow)
        return d ? findItem(d, "calcInput") as TextEdit : null
    }
    /// The visible results of the calculation: how many, the `index`-th.
    function calcResultCount(): int {
        const d = delegateAt(calcRow)
        let n = 0
        for (let i = 0; i < window.countNamed(d, "calcResult"); ++i) {
            if (namedAt(d, "calcResult", i).visible)
                ++n
        }
        return n
    }
    function calcResultAt(index: int): Item {
        const d = delegateAt(calcRow)
        let n = 0
        for (let i = 0; i < window.countNamed(d, "calcResult"); ++i) {
            const r = namedAt(d, "calcResult", i)
            if (r.visible && n++ === index)
                return r
        }
        return null
    }
    /// Each result on its line: as high as the line's start in the (wrapped) input.
    function calcAligned(): bool {
        const input = calcInput()
        if (input === null || calcResultCount() !== 3)
            return false
        for (let i = 0; i < 3; ++i) {
            if (!resultOnLine(calcResultAt(i), input))
                return false
        }
        return true
    }
    function resultOnLine(r: Item, input: TextEdit): bool {
        const offset = Js.toNumber("" + DevTest.property(r, "offset"))
        return near(r.y, input.y + lineTop(input, offset))
    }
    function lineTop(input: TextEdit, offset: int): real {
        if (offset > input.length)
            return 0
        test.caret = input.positionToRectangle(offset)
        return test.caret.y
    }
    /// How far down the input the second line starts (the first one wraps).
    function secondLineY(): real {
        const input = calcInput()
        if (!input)
            return 0
        test.caret = input.positionToRectangle(Js.indexOf(input.text, "\n") + 1)
        return test.caret.y
    }
    /// The calculation's block as it was when the window narrowed (a new one would place its
    /// results from scratch: the check is that the same one follows).
    function sameCalc(): bool { return calcDelegate !== null && delegateAt(calcRow) === calcDelegate }
    function resultYs(): string {
        let ys = ""
        for (let i = 0; i < calcResultCount(); ++i)
            ys += (i > 0 ? ", " : "") + calcResultAt(i).y
        return ys
    }
    /// Where the calculation stands now (the checks' messages).
    function calcState(): string {
        return "window " + window.width + " px; second line at " + secondLineY() + " px, was " + calcLinesBefore
               + "; results at " + resultYs() + (sameCalc() ? "" : "; another block")
    }
    function listHeader(): Item { return list().headerItem }
    /// The list's header (the page's title, its folder).
    function headerHeight(): real { return listHeader() ? listHeader().height : 0 }
    /// The view shows the top of the page.
    function atTop(): bool { return list().contentY <= list().originY + 1 }
    /// A step narrowing the window by a tenth, again on the next steps while `more` (at most down
    /// to 160 px): the width at which text wraps again depends on the fonts.
    function narrowWhile(more: bool): void {
        if (more && window.width > 160) {
            window.width = Math.max(160, Math.round(window.width * 0.9))
            window.stepIndex -= 1
        }
    }

    Component.onCompleted: window.runSteps("refreshtest")

    function steps(): void {
        if (window.at()) window.waitStep(page.loaded && window.viewRow("query") > 0, "the sample page is open")
        // ---- A query's results: as many rows again, one of them taller -----------------------------
        if (window.at()) list().positionViewAtIndex(window.viewRow("query"), ListView.Beginning)
        if (window.at()) window.waitStep(resultRowCount() > 0 && resultsStacked(), "the query shows the open #zakupy tasks")
        if (window.at()) {
            rowsBefore = resultRowCount()
            host.externalWrite("Fuga.md", "- [ ] Kupić fugę #zakupy\n")
        }
        if (window.at()) window.waitStep(resultRowCount() === rowsBefore + 1 && resultsStacked(), "a task written in another note joins the results")
        if (window.at()) {
            heightBefore = resultList().height
            host.externalWrite("Fuga.md", "- [ ] " + test.longTask + " #zakupy\n")
        }
        if (window.at()) window.waitStep(resultShows("epoksydową"), "the task changed in its note shows changed")
        if (window.at() && !window.waiting(resultRowCount() === rowsBefore + 1 && resultList().height > heightBefore + 10 && resultsStacked()))
            window.checkWait("the results grow with a row that got taller, as many rows as before (list "
                   + resultList().height + " px, rows " + resultRowsHeight() + " px)")
        // ---- An embedded section changed in its own note --------------------------------------------
        if (window.at()) list().positionViewAtIndex(window.rowOf("Budżet na październik"), ListView.Beginning)
        if (window.at()) window.waitStep(frameCount() === 1 && entryRowCount(frameAt(0)) === 3 && embedFits(), "the embedded section shows its three blocks")
        if (window.at()) {
            heightBefore = transclusion().height
            host.externalWrite("Budżet domowy.md", "# Budżet domowy\n\n## Październik\n\n"
                               + "- Remont: **12 000 zł** #finanse ^bud01\n- Jedzenie: 1 800 zł\n"
                               + "- Rachunki: prąd, gaz, woda i internet — razem około 640 zł, płatne do "
                               + "dziesiątego dnia miesiąca przelewem z konta wspólnego\n- Bilety: 230 zł\n"
                               + "- [ ] Opłacić czynsz 📅 2026-10-20 #finanse\n\n## Listopad\n\n- Prezenty: 400 zł\n")
        }
        if (window.at() && !window.waiting(frameCount() === 1 && entryRowCount(frameAt(0)) === 5 && embedFits()
                   && transclusion().height > heightBefore))
            window.checkWait("the embed follows its source in another note: its frame and the block grow (view "
                   + (transclusion() ? transclusion().height : -1) + " px, was " + heightBefore + ")")
        // ---- The date picker after midnight -----------------------------------------------------------
        if (window.at()) {
            nowBefore = DatesHelper.fixedNow
            dialogs().createReminder(window.rowOf("Zadzwonić do hydraulika"), false)
        }
        if (window.at()) window.waitStep(dialog() !== null && dialog().opened && picker() !== null, "the reminder dialog opens")
        if (window.at()) check(todayCell() === DatesHelper.today, "its date picker marks today (" + todayCell() + ")")
        if (window.at()) DatesHelper.fixedNow = DatesHelper.addDays(DatesHelper.today, 1) + " 00:05"
        if (window.at() && !window.waiting(todayCell() === DatesHelper.today))
            window.checkWait("after midnight the date picker marks the new day (" + todayCell() + ", today is "
                   + DatesHelper.today + ")")
        if (window.at()) DatesHelper.fixedNow = nowBefore
        if (window.at()) window.waitStep(todayCell() === DatesHelper.today, "and the day before again when the clock is set back")
        // ---- A language change while the dialog exists ------------------------------------------------
        if (window.at()) dialog().repeatKind = "weekly"
        if (window.at()) window.waitStep(weekdays() !== null && weekdays().visible && dialogInLanguage(), "a weekly repeat: the weekday buttons, the rule and the picker in the app's language")
        if (window.at()) {
            languageBefore = DevTools.language
            namesBefore = names("shortNames", "|")
            DevTools.setLanguage(languageBefore === "en" ? "pl" : "en")
        }
        if (window.at() && !window.waiting(DatesHelper.locale !== languageBefore && dialogInLanguage() && names("shortNames", "|") !== namesBefore))
            window.checkWait("the dialog follows a language change: weekdays " + names("shortNames", " ") + ", rule „"
                   + dialog().ruleLabel + "”, picker " + pickerShown(" "))
        if (window.at()) DevTools.setLanguage(languageBefore)
        if (window.at()) window.waitStep(DatesHelper.locale === languageBefore && dialogInLanguage() && names("shortNames", "|") === namesBefore, "and back")
        if (window.at()) dialog().close()
        if (window.at()) window.waitStep(!dialog().visible, "the dialog closes")
        // ---- A calculation's results after the window narrowed ----------------------------------------
        if (window.at()) {
            page.save()
            host.externalWrite(page.path, host.file(page.path) + "\n" + test.calcSource)
        }
        if (window.at()) {
            calcRow = lastCalcRow()
            window.waitStep(calcRow > window.viewRow("calc"), "a second calculation merged in from another device")
        }
        if (window.at()) list().positionViewAtIndex(calcRow, ListView.Beginning)
        if (window.at()) window.waitStep(calcAligned(), "its results sit on their lines")
        if (window.at()) {
            calcLinesBefore = secondLineY()
            calcDelegate = delegateAt(calcRow)
            widthBefore = window.width
        }
        if (window.at()) narrowWhile(secondLineY() <= calcLinesBefore + 1)
        if (window.at() && !window.waiting(sameCalc() && secondLineY() > calcLinesBefore + 1 && calcAligned()))
            window.checkWait("the window narrowed, the first line wraps again: the results follow their lines (" + calcState() + ")")
        if (window.at()) window.width = widthBefore
        if (window.at() && !window.waiting(sameCalc() && near(secondLineY(), calcLinesBefore) && calcAligned()))
            window.checkWait("and back at the old width (" + calcState() + ")")
        // ---- The page's header changing its height while the view is further down ---------------------
        if (window.at()) {
            headerBefore = headerHeight()
            check(!atTop(), "the view is down the page, at the calculation")
        }
        if (window.at()) narrowWhile(near(headerHeight(), headerBefore))
        if (window.at() && !window.waiting(!near(headerHeight(), headerBefore) && !atTop()))
            window.checkWait("the window narrowed to " + window.width + " px, the header changed its height ("
                   + headerHeight() + " px, was " + headerBefore + "): the view stays down the page ("
                   + list().contentY + ", top " + list().originY + ")")
        if (window.at()) window.width = widthBefore
        if (window.at()) window.waitStep(near(headerHeight(), headerBefore) && !atTop(), "and back at the old width")
    }
}
