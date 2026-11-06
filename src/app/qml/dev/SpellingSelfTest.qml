pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// SpellingSelfTest — spell checking in the real editor (`brulion --editor --editor-script
// spelltest`; ctest tst_editor_qml_spelling, desktop, and tst_editor_qml_spelling_touch, a phone
// with --touch), on word lists (SpellCheck.useWordLists: the same on every machine, nothing
// stored in the settings): the edited block's misspelled word is marked, a right-click on it
// (a tap with a finger) opens the text menu with its corrections at the top — on touch UIs only
// the spelling's items —; a correction replaces the word (one undo step, Ctrl+Z brings it back),
// "Add to dictionary" and "Ignore" take the mark away; words in code, links and tags are never
// marked; spell checking switched off marks nothing.
// Steps run on the playground's step runner (EditorPlayground.runSteps(), steps()).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property int row: -1
    /// The rectangle of a word's position (positionToRectangle(), read through a typed property).
    property rect caret
    /// The word lists spell checking runs on (SpellCheck.useWordLists()).
    readonly property list<string> polishWords: ["ala", "ma", "kota", "kot", "psa", "zapisz", "jest", "fajny", "notatnik"]
    readonly property list<string> englishWords: ["meeting", "team"]
    readonly property string gesture: Theme.touchUi ? "a tap" : "a right-click"

    function check(ok: bool, what: string): void { test.window.check(ok, what) }
    function edit(): InlineEditor { return test.editor.activeEditor }
    function textMenu(): BMenu { return test.editor.textMenu }
    function menuOpen(): bool { return test.textMenu() !== null && test.textMenu().opened }
    function item(name: string): T.MenuItem { return test.window.menuItemNamed(test.textMenu(), name) }
    function suggestion(text: string): T.MenuItem {
        const menu = test.textMenu()
        for (let i = 0; menu && i < menu.count; ++i) {
            const it = menu.itemAt(i) as T.MenuItem
            if (it && it.objectName === "spellSuggestion" && it.text === text)
                return it
        }
        return null
    }
    function line(prefix: string): string { return test.window.fileLineOf(test.window.pageText(), prefix) }
    /// The position in the edited text of the middle of `word` (its first occurrence after `from`).
    function posOf(word: string, from: int): int {
        const e = test.edit()
        const i = e ? Js.indexOfFrom(e.plain(), word, from) : -1
        return i < 0 ? -1 : i + Math.floor(word.length / 2)
    }
    /// `word` (its first occurrence after `from`) is marked as misspelled in the edited text.
    function marked(word: string, from: int): bool {
        const e = test.edit()
        const p = test.posOf(word, from)
        return e !== null && p >= 0 && test.markedAt(e, p)
    }
    function markedAt(e: InlineEditor, position: int): bool {
        return DevTest.valueAt(e.misspelledAt(position), "word") !== undefined
    }
    /// Steps: edit the block starting with `prefix`, the caret at its start (no word being typed).
    function editBlock(prefix: string): void {
        if (test.window.at()) {
            test.row = test.window.rowOf(prefix)
            check(test.row >= 0, "the block “" + prefix + "” is there")
            test.editor.startEditing(test.row, 0)
        }
        if (test.window.at())
            test.window.waitStep(test.edit() !== null && test.edit().row === test.row, "“" + prefix + "” is edited")
    }
    /// The context gesture on `word` of the edited text.
    function gestureOn(word: string): void {
        const e = test.edit()
        test.caret = e.positionToRectangle(test.posOf(word, 0))
        const x = test.caret.x + 1
        const y = test.caret.y + test.caret.height / 2
        if (Theme.touchUi) {
            test.window.taskEvents.touchPress(e, x, y)
            test.window.taskEvents.touchRelease(e, x, y)
        } else {
            test.window.taskEvents.mouseClick(e, x, y, Qt.RightButton, Qt.NoModifier, -1)
        }
    }
    /// Steps: choose `target` (`name` in the messages) in the menu, which then closes.
    function choose(name: string, target: T.MenuItem): void {
        if (test.window.at()) {
            check(target !== null && target.visible, "the menu has " + name)
            test.window.tapItem(target)
        }
        if (test.window.at())
            test.window.waitStep(!test.menuOpen(), "choosing " + name + " closes the menu")
    }
    /// No word in code, a link or a tag of the edited block is marked.
    function syntaxUnmarked(): bool {
        for (let i = Js.indexOf(test.edit().plain(), "kotta"); i >= 0; i = Js.indexOfFrom(test.edit().plain(), "kotta", i + 1)) {
            if (test.markedAt(test.edit(), i + 2))
                return false
        }
        return true
    }

    function steps(): void {
        if (test.window.at()) {
            SpellCheck.useWordLists(Js.withKey(Js.withKey({}, "pl", test.polishWords), "en", test.englishWords))
            SpellCheck.languageSetting = "pl,en"
            SpellCheck.enabled = true
            test.host.setFile("Pisownia.md", "- Ala ma kotta i psa\n- Zapisz `kotta` w [[kotta]] #kotta\n"
                         + "- Brulion jest fajny\n- team meeting\n- psa ma blabla\n")
            test.page.path = "Pisownia.md"
        }
        if (test.window.at())
            test.window.waitStep(test.window.rowOf("Ala ma") >= 0, "the page is open")
        if (test.window.at())
            check(SpellCheck.active && SpellCheck.languages.join(",") === "pl,en", "spell checking is on: "
                  + SpellCheck.languages.join(","))

        // ---- A correction --------------------------------------------------------------------
        test.editBlock("Ala ma")
        if (test.window.at())
            test.window.waitStep(test.marked("kotta", 0), "“kotta” is marked in the edited block")
        if (test.window.at())
            check(!test.marked("Ala", 0) && !test.marked("psa", 0), "known words are not marked")
        if (test.window.at())
            test.gestureOn("kotta")
        if (test.window.at())
            test.window.waitStep(test.menuOpen(), test.gesture + " on the marked word opens the text menu")
        if (test.window.at())
            test.window.waitStep(test.suggestion("kota") !== null, "the menu offers “kota”")
        if (test.window.at()) {
            check(test.window.menuIndexOf(test.textMenu(), "spellSuggestion") === 0, "corrections come first")
            check(test.item("spellAddWord").visible && test.item("spellIgnore").visible, "Dodaj do słownika and Ignoruj are there")
            check(test.item("editTextCut").visible === !Theme.touchUi,
                  Theme.touchUi ? "a tap shows only the spelling's items" : "the text items follow")
        }
        test.choose("the correction “kota”", test.suggestion("kota"))
        if (test.window.at() && !test.window.waiting(Js.indexOf(test.line("Ala ma"), "Ala ma kota i psa") >= 0))
            test.window.checkWait("the word is replaced: " + test.line("Ala ma"))
        if (test.window.at())
            check(test.edit() !== null && test.edit().cursorPosition === 11 /* "Ala ma kota".length */, "the caret is after it")
        if (test.window.at())
            test.window.key(Qt.Key_Z, Qt.ControlModifier)
        if (test.window.at() && !test.window.waiting(Js.indexOf(test.line("Ala ma"), "Ala ma kotta i psa") >= 0))
            test.window.checkWait("one undo step brings it back: " + test.line("Ala ma"))

        // ---- Ignore ---------------------------------------------------------------------------
        test.editBlock("Ala ma")
        if (test.window.at())
            test.window.waitStep(test.marked("kotta", 0), "“kotta” is marked again")
        if (test.window.at())
            test.gestureOn("kotta")
        if (test.window.at())
            test.window.waitStep(test.menuOpen(), "the menu opens again")
        test.choose("spellIgnore", test.item("spellIgnore"))
        if (test.window.at())
            test.window.waitStep(!test.marked("kotta", 0), "an ignored word is not marked")
        if (test.window.at())
            check(SpellCheck.ignoredWords.indexOf("kotta") >= 0 && SpellCheck.personalWords.length === 0,
                  "ignored, not added to the dictionary")

        // ---- Add to the dictionary ------------------------------------------------------------
        test.editBlock("Brulion jest")
        if (test.window.at())
            test.window.waitStep(test.marked("Brulion", 0), "“Brulion” is marked")
        if (test.window.at())
            test.gestureOn("Brulion")
        if (test.window.at())
            test.window.waitStep(test.menuOpen(), "its menu opens")
        if (test.window.at())
            check(Js.indexOf(test.item("spellAddWord").text, "Brulion") >= 0, "the item names the word: " + test.item("spellAddWord").text)
        test.choose("spellAddWord", test.item("spellAddWord"))
        if (test.window.at())
            test.window.waitStep(!test.marked("Brulion", 0), "a word added to the dictionary is not marked")
        if (test.window.at())
            check(SpellCheck.personalWords.indexOf("Brulion") >= 0, "the personal dictionary has it")

        // ---- Syntax is never checked; English ---------------------------------------------------
        test.editBlock("Zapisz")
        if (test.window.at())
            test.window.waitStep(SpellCheck.idle(), "the words of the block are checked")
        if (test.window.at()) {
            check(test.syntaxUnmarked(), "words in code, a link and a tag are not marked")
            check(!test.marked("Zapisz", 0), "the known word is not marked")
        }
        test.editBlock("team meeting")
        if (test.window.at())
            test.window.waitStep(SpellCheck.idle(), "the English words are checked")
        if (test.window.at())
            check(!test.marked("team", 0) && !test.marked("meeting", 0), "English words are right too")

        // ---- Switched off -------------------------------------------------------------------------
        test.editBlock("psa ma")
        if (test.window.at())
            test.window.waitStep(test.marked("blabla", 0), "“blabla” is marked")
        if (test.window.at()) { SpellCheck.enabled = false }
        if (test.window.at())
            check(!test.edit().spellChecked && !test.marked("blabla", 0), "switched off: nothing is marked")
        if (test.window.at()) { SpellCheck.enabled = true }
        if (test.window.at())
            test.window.waitStep(test.marked("blabla", 0), "switched on again: “blabla” is marked again")
    }

    Component.onCompleted: test.window.runSteps("spelltest")
}
