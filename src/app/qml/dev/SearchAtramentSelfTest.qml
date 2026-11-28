pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// SearchAtramentSelfTest — dev: the Search view's results drawn by Atrament (docs/ATRAMENT.md "The
// views in Atrament (M3)"; qml/content/SearchAtramentList.qml over views/atrament/searchview.h) in
// the real app, headless (`brulion --demo --route search --editor-script search-atrament-selftest
// --atrament-views`, ctest tst_views_qml_atrament_search; SearchView loads it when its results are
// Atrament's). With real keys and clicks through the window: a query typed in the field, its results
// drawn by Atrament (the ListView without rows), ↓ in the field making the next result current, a
// click on a result opening its page.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Registered only where Atrament is built (it names AtramentView).
Item {
    id: test

    /// SearchView's parts: the view itself, its model, the results drawn by Atrament.
    property Item searchView
    property SearchResultModel model
    property AtramentView list

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property int stepTarget: 0
    property int stepCursor: 0
    property bool stepDone: true
    property rect _rect: Qt.rect(0, 0, 0, 0)

    DevEvents { id: events }

    function check(ok: bool, what: string): void {
        if (!ok)
            test.failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    function at(): bool {
        return test.stepCursor++ === test.stepTarget
    }
    function waitFor(ready: bool): void {
        test.stepDone = ready
    }
    function partRect(row: int, part: string): rect {
        test._rect = test.list.viewPartRect(row, part)
        return test._rect
    }
    function current(): int { return QmlCall.integer(QmlCall.get(test.searchView, "currentResult")) }

    function runSteps(): void {
        if (test.at())
            QmlCall.call1(test.searchView, "applyQuery", "remont")
        if (test.at())
            test.waitFor(test.model.count > 1 && test.partRect(1, "row").height > 0)
        if (test.at()) {
            const listView = QmlCall.object(test.searchView, "listView")
            test.check(listView === null || QmlCall.integer(QmlCall.get(listView, "count")) === 0,
                       "the ListView has no rows: the results are Atrament's (" + test.model.count + ")")
            test.check(test.current() === 0, "the first result is current")
            events.keyClick(Qt.Key_Down, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.current() === 1 && test.list.viewCurrentRow() === 1)
        if (test.at()) {
            test.check(test.list.viewCurrentRow() === 1, "↓ in the field makes the next result current, drawn so")
            const r = test.partRect(0, "title")
            events.mouseClick(test.list, r.x + 20, r.y + r.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(AppController.currentView !== "search")
        if (test.at())
            test.check(AppController.currentView !== "search", "a click on a result opens it: " + AppController.currentView)
    }

    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.list !== null && test.model !== null
        onTriggered: {
            if (test.inStep)
                return
            test.inStep = true
            test.stepTarget = test.stepIndex
            test.stepCursor = 0
            test.stepDone = true
            const error = DevTest.callCatching(test, "runSteps")
            test.inStep = false
            if (error !== "")
                test.check(false, "step " + test.stepIndex + " threw: " + error)
            if (test.stepCursor <= test.stepTarget) {
                runner.stop()
                console.log("SELFTEST DONE failures=" + test.failures)
                Qt.exit(test.failures === 0 ? 0 : 1)
                return
            }
            if (!test.stepDone && ++test.waited < 80)
                return
            if (!test.stepDone)
                test.check(false, "step " + test.stepIndex + " timed out")
            test.waited = 0
            test.stepIndex += 1
        }
    }
}
