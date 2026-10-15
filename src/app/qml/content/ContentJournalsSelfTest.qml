pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalsSelfTest — dev: the journal stream's data safety in the real app, headless
// (`brulion --demo --route journals --editor-script journals-selftest`, ctest
// tst_content_journals_qml; JournalsView loads it next to its list). Every day of the stream is
// its own editor (PageController) inside a lazy ListView, so a day being edited is destroyed when
// it scrolls far enough away. Checked with real key and input-method events:
//   * text typed into today and scrolled away is saved and shown again when scrolled back;
//   * text typed into an older day and scrolled away at once (before the debounced save, its
//     delegate destroyed) is in the file, also the word the keyboard was still composing;
//   * midnight while a day is being edited (the list's today moves on): the new day is listed
//     first, the edited day keeps its editor and caret, is no longer "today", and what is typed
//     after midnight lands in its own file — nothing is written to the new day.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
Item {
    id: test

    /// The journal list (ListView over `days`) and its model.
    property ListView list
    property JournalListModel days

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property var steps: []

    property ContentJournalDay tracked: null
    property string trackedPath: ""
    property string trackedDate: ""
    property string secondPath: ""

    function check(ok, what) {
        if (!ok)
            failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    /// The ContentJournalDay of a row (null when its delegate does not exist).
    function dayAt(row) {
        const item = list.itemAtIndex(row)
        return item ? item.day : null
    }
    function typeKeys(edit, s) {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(edit, 0, 0, s.charAt(i))
    }
    /// Start editing a new block at the end of `day`.
    function editNewBlock(day) {
        const c = day.controller
        const row = c.blocks.count > 0 ? c.insertBlockAfter(c.lastVisibleRow(), "") : c.appendEmptyBlock()
        day.editor.startEditing(row, -1)
    }
    function editing(day) {
        return day !== null && day.editor.activeEditor !== null && day.editor.activeEditor.activeFocus
               && day.controller.editingRow >= 0
    }
    function fileHas(path, text) {
        return Workspace.readFile(path).indexOf(text) >= 0
    }

    Timer {
        interval: 120
        repeat: true
        running: test.list !== null && test.days !== null
        onTriggered: {
            if (test.stepIndex >= test.steps.length) {
                stop()
                console.log("SELFTEST DONE failures=" + test.failures)
                Qt.exit(test.failures === 0 ? 0 : 1)
                return
            }
            let done = true
            try {
                done = test.steps[test.stepIndex]() !== false
            } catch (e) {
                test.check(false, "step " + test.stepIndex + " threw: " + e)
            }
            if (!done && ++test.waited < 80)
                return // a waiting step: again on the next tick (~10 s at most)
            if (!done)
                test.check(false, "step " + test.stepIndex + " timed out")
            test.waited = 0
            test.stepIndex += 1
        }
    }

    Component.onCompleted: {
        steps = [
            // ---- A notebook with two months of days ------------------------------------------
            () => Workspace.ready && test.days.count >= 2 && test.dayAt(0) !== null && test.dayAt(0).controller.loaded,
            () => {
                for (let n = 2; n <= 60; ++n) {
                    const date = DatesHelper.addDays(DatesHelper.today, -n)
                    const path = Workspace.journalPath(date)
                    if (Workspace.exists(path))
                        continue
                    let md = ""
                    for (let b = 0; b < 10; ++b)
                        md += "- Dzień " + n + ", notatka " + b + ": zażółć gęślą jaźń\n"
                    Workspace.writeFile(path, md)
                }
            },
            () => test.days.count >= 55,
            // ---- Today: typed and scrolled away before the save --------------------------------------
            () => {
                test.list.positionViewAtBeginning()
                test.tracked = test.dayAt(0)
                test.check(test.tracked !== null && test.tracked.isToday, "today is the first day")
                test.trackedPath = test.tracked.path
                test.editNewBlock(test.tracked)
            },
            () => test.editing(test.tracked),
            () => {
                test.typeKeys(test.tracked.editor.activeEditor, "Kupić mleko")
                test.check(test.tracked.controller.dirty, "today has unsaved text")
                test.list.positionViewAtEnd()
            },
            () => test.tracked === null, // the delegate (and its editor) is gone
            () => test.check(test.fileHas(test.trackedPath, "Kupić mleko"), "today's text is saved when it scrolls away"),
            () => test.list.positionViewAtBeginning(),
            () => test.dayAt(0) !== null && test.dayAt(0).controller.loaded,
            () => {
                const c = test.dayAt(0).controller
                let found = false
                for (let i = 0; i < c.blocks.count; ++i)
                    found = found || String(c.blocks.get(i).plainText).indexOf("Kupić mleko") >= 0
                test.check(found, "scrolled back, today shows the text")
                c.stopEditing()
            },
            // ---- An older day: typed, composing, scrolled away before the save --------------------
            () => test.list.positionViewAtIndex(20, ListView.Beginning),
            () => test.dayAt(20) !== null && test.dayAt(20).controller.loaded,
            () => {
                test.tracked = test.dayAt(20)
                test.secondPath = test.tracked.path
                test.editNewBlock(test.tracked)
            },
            () => test.editing(test.tracked),
            () => {
                // One step, well inside the editor's save delay (400 ms).
                const edit = test.tracked.editor.activeEditor
                test.typeKeys(edit, "Dopisane później ąę ")
                EditorInput.sendInputMethod(edit, "zażółć")
                test.check(edit.preeditText === "zażółć", "a word is being composed")
                test.check(test.tracked.controller.dirty, "the day has unsaved text")
                test.list.positionViewAtBeginning()
            },
            () => test.tracked === null, // the delegate (and its editor) is gone
            () => {
                const content = Workspace.readFile(test.secondPath)
                test.check(content.indexOf("Dopisane później ąę") >= 0, "an older day's edit is saved when it scrolls away")
                test.check(content.indexOf("zażółć") >= 0, "the word being composed is saved too")
            },
            // ---- Midnight while editing --------------------------------------------------------------
            () => test.dayAt(0) !== null && test.dayAt(0).controller.loaded,
            () => {
                test.tracked = test.dayAt(0)
                test.trackedPath = test.tracked.path
                test.trackedDate = test.tracked.date
                test.editNewBlock(test.tracked)
            },
            () => test.editing(test.tracked),
            () => test.typeKeys(test.tracked.editor.activeEditor, "Przed północą"),
            // The list's clock moves on (the app's DatesHelper.today does that at 00:00).
            () => { test.days.today = DatesHelper.addDays(test.trackedDate, 1) },
            () => test.days.count > 0 && test.days.get(0).date === DatesHelper.addDays(test.trackedDate, 1),
            () => {
                test.check(test.tracked !== null, "the edited day keeps its editor over midnight")
                if (!test.tracked)
                    return
                test.check(test.tracked.date === test.trackedDate && !test.tracked.isToday,
                           "the edited day is no longer today: " + test.tracked.date + " " + test.tracked.isToday)
                const edit = test.tracked.editor.activeEditor
                test.check(test.editing(test.tracked), "the caret stays in the edited day (editingRow "
                           + test.tracked.controller.editingRow + ", editor " + (edit ? edit.activeFocus : "none")
                           + ", focus on " + test.Window.activeFocusItem + ")")
                const first = test.dayAt(0)
                test.check(first !== null && first.isToday && first.date === test.days.today, "the new day comes first")
            },
            () => { if (test.editing(test.tracked)) test.typeKeys(test.tracked.editor.activeEditor, " i po północy") },
            () => { if (test.tracked) test.tracked.controller.save() },
            () => {
                test.check(test.fileHas(test.trackedPath, "Przed północą i po północy"),
                           "text typed over midnight is in the day's own file")
                test.check(!Workspace.exists(Workspace.journalPath(test.days.today)), "nothing was written to the new day")
                let todays = 0
                for (let i = 0; i < 3; ++i) {
                    const d = test.dayAt(i)
                    if (d && d.isToday)
                        todays += 1
                }
                test.check(todays === 1, "one day says today: " + todays)
            },
            // ---- The next midnight: the unwritten day goes, the edited one keeps the caret ---------
            () => {
                test.check(test.editing(test.tracked), "still editing the day before")
                test.days.today = DatesHelper.addDays(test.trackedDate, 2)
            },
            () => test.days.count > 0 && test.days.get(0).date === DatesHelper.addDays(test.trackedDate, 2),
            () => {
                test.check(test.days.exactRowOfDate(DatesHelper.addDays(test.trackedDate, 1)) < 0,
                           "the day nobody wrote in is no longer listed")
                test.check(test.tracked !== null && test.editing(test.tracked), "rows removed above keep the caret in the edited day")
            },
            () => { if (test.editing(test.tracked)) test.typeKeys(test.tracked.editor.activeEditor, " — dalej") },
            () => { if (test.tracked) test.tracked.controller.save() },
            () => test.check(test.fileHas(test.trackedPath, "po północy — dalej"), "typing goes on in the edited day"),
        ]
    }
}
