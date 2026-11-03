pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// SpellingSelfTest — spell checking in the real editor (`brulion --editor --editor-script
// spelltest`; ctest tst_editor_qml_spelling, desktop, and tst_editor_qml_spelling_touch, a phone
// with --touch), on word lists (SpellCheck.useWordLists: the same on every machine, nothing
// stored in the settings): the edited block's misspelled word is marked, a right-click on it
// (a tap with a finger) opens the text menu with its corrections at the top — on touch UIs only
// the spelling's items —; a correction replaces the word (one undo step, Ctrl+Z brings it back),
// "Dodaj do słownika" and "Ignoruj" take the mark away; words in code, links and tags are never
// marked; spell checking switched off marks nothing.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property var events: null
    property int row: -1
    readonly property string gesture: Theme.touchUi ? "a tap" : "a right-click"

    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function edit() { return editor.activeEditor }
    function menuOpen() { return editor.textMenu !== null && editor.textMenu.opened }
    function item(name) { return window.menuItemNamed(editor.textMenu, name) }
    function suggestion(text) {
        const menu = editor.textMenu
        for (let i = 0; menu && i < menu.count; ++i) {
            const it = menu.itemAt(i)
            if (it && it.objectName === "spellSuggestion" && it.text === text)
                return it
        }
        return null
    }
    function line(prefix) { return window.fileLineOf(window.pageText(), prefix) }
    /// The position in the edited text of the middle of `word` (its first occurrence after `from`).
    function posOf(word, from) {
        const e = edit()
        const i = e ? e.plain().indexOf(word, from || 0) : -1
        return i < 0 ? -1 : i + Math.floor(word.length / 2)
    }
    function marked(word, from) {
        const e = edit()
        const p = posOf(word, from)
        return e !== null && p >= 0 && e.misspelledAt(p).word !== undefined
    }
    /// Steps: edit the block starting with `prefix`, the caret at its start (no word being typed).
    function editBlock(prefix) {
        return [
            () => {
                row = window.rowOf(prefix)
                check(row >= 0, "the block “" + prefix + "” is there")
                editor.startEditing(row, 0)
            },
            wait(() => edit() !== null && edit().row === row, "“" + prefix + "” is edited")
        ]
    }
    /// The context gesture on `word` of the edited text.
    function gestureOn(word) {
        return () => {
            const e = edit()
            const r = e.positionToRectangle(posOf(word))
            const x = r.x + 1
            const y = r.y + r.height / 2
            if (Theme.touchUi) {
                events.touchEvent(e).press(0, e, x, y).commit()
                events.touchEvent(e).release(0, e, x, y).commit()
            } else {
                events.mouseClick(e, x, y, Qt.RightButton, Qt.NoModifier, -1)
            }
        }
    }
    function choose(name, it) {
        return [
            () => {
                const target = it ? it() : item(name)
                check(target !== null && target.visible, "the menu has " + name)
                window.tapItem(target)
            },
            wait(() => !menuOpen(), "choosing " + name + " closes the menu")
        ]
    }

    function steps() {
        events = Qt.createQmlObject('import QtQuick; import QtTest; TestEvent {}', test, "spellingEvents")
        window.taskEvents = events // (window.tapItem)
        return [
            () => {
                SpellCheck.useWordLists({
                    "pl": ["ala", "ma", "kota", "kot", "psa", "zapisz", "jest", "fajny", "notatnik"],
                    "en": ["meeting", "team"]
                })
                SpellCheck.languageSetting = "pl,en"
                SpellCheck.enabled = true
                host.setFile("Pisownia.md", "- Ala ma kotta i psa\n- Zapisz `kotta` w [[kotta]] #kotta\n"
                             + "- Brulion jest fajny\n- team meeting\n- psa ma blabla\n")
                page.path = "Pisownia.md"
            },
            wait(() => window.rowOf("Ala ma") >= 0, "the page is open"),
            () => check(SpellCheck.active && SpellCheck.languages.join(",") === "pl,en", "spell checking is on: "
                        + SpellCheck.languages.join(",")),

            // ---- A correction --------------------------------------------------------------------
            ...editBlock("Ala ma"),
            wait(() => marked("kotta"), "“kotta” is marked in the edited block"),
            () => check(!marked("Ala") && !marked("psa"), "known words are not marked"),
            gestureOn("kotta"),
            wait(menuOpen, gesture + " on the marked word opens the text menu"),
            wait(() => suggestion("kota") !== null, "the menu offers “kota”"),
            () => {
                check(window.menuIndexOf(editor.textMenu, "spellSuggestion") === 0, "corrections come first")
                check(item("spellAddWord").visible && item("spellIgnore").visible, "Dodaj do słownika and Ignoruj are there")
                check(item("editTextCut").visible === !Theme.touchUi,
                      Theme.touchUi ? "a tap shows only the spelling's items" : "the text items follow")
            },
            ...choose("the correction “kota”", () => suggestion("kota")),
            wait(() => line("Ala ma").indexOf("Ala ma kota i psa") >= 0, () => "the word is replaced: " + line("Ala ma")),
            () => check(edit() !== null && edit().cursorPosition === "Ala ma kota".length, "the caret is after it"),
            () => window.key(Qt.Key_Z, Qt.ControlModifier),
            wait(() => line("Ala ma").indexOf("Ala ma kotta i psa") >= 0, () => "one undo step brings it back: " + line("Ala ma")),

            // ---- Ignore ---------------------------------------------------------------------------
            ...editBlock("Ala ma"),
            wait(() => marked("kotta"), "“kotta” is marked again"),
            gestureOn("kotta"),
            wait(menuOpen, "the menu opens again"),
            ...choose("spellIgnore"),
            wait(() => !marked("kotta"), "an ignored word is not marked"),
            () => check(SpellCheck.ignoredWords.indexOf("kotta") >= 0 && SpellCheck.personalWords.length === 0,
                        "ignored, not added to the dictionary"),

            // ---- Add to the dictionary ------------------------------------------------------------
            ...editBlock("Brulion jest"),
            wait(() => marked("Brulion"), "“Brulion” is marked"),
            gestureOn("Brulion"),
            wait(menuOpen, "its menu opens"),
            () => check(item("spellAddWord").text.indexOf("Brulion") >= 0, "the item names the word: " + item("spellAddWord").text),
            ...choose("spellAddWord"),
            wait(() => !marked("Brulion"), "a word added to the dictionary is not marked"),
            () => check(SpellCheck.personalWords.indexOf("Brulion") >= 0, "the personal dictionary has it"),

            // ---- Syntax is never checked; English ---------------------------------------------------
            ...editBlock("Zapisz"),
            wait(() => SpellCheck.idle(), "the words of the block are checked"),
            () => {
                const e = edit()
                const text = e.plain()
                let found = false
                for (let i = text.indexOf("kotta"); i >= 0; i = text.indexOf("kotta", i + 1))
                    found = found || e.misspelledAt(i + 2).word !== undefined
                check(!found, "words in code, a link and a tag are not marked")
                check(!marked("Zapisz"), "the known word is not marked")
            },
            ...editBlock("team meeting"),
            wait(() => SpellCheck.idle(), "the English words are checked"),
            () => check(!marked("team") && !marked("meeting"), "English words are right too"),

            // ---- Switched off -------------------------------------------------------------------------
            ...editBlock("psa ma"),
            wait(() => marked("blabla"), "“blabla” is marked"),
            () => { SpellCheck.enabled = false },
            () => check(!edit().spellChecked && !marked("blabla"), "switched off: nothing is marked"),
            () => { SpellCheck.enabled = true },
            wait(() => marked("blabla"), "switched on again: “blabla” is marked again")
        ]
    }

    Component.onCompleted: window.runSteps(steps())
}
