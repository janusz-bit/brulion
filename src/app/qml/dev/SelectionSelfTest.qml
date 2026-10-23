pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import Brulion

// SelectionSelfTest — selecting and copying blocks with real input in the editor playground
// (`brulion --editor --editor-script selectiontest`; ctest tst_editor_qml_selection, desktop, and
// tst_editor_qml_selection_touch, a phone with --touch). QtTest's TestEvent sends keys, mouse and
// touch through the window like a real device; the clipboard is read back.
//   desktop: Esc then Shift+↓ twice and Ctrl+C (three blocks with their children), Shift+↑ from an
//     edited block's first line, Shift+↓ from inside one, Ctrl+A twice, Shift+click, Delete and
//     Ctrl+Z, Alt+↓ on a range, Ctrl+V over a selection, in-block Ctrl+C / Ctrl+X keeping no-break
//     spaces, the edited text's right-click menu, the block menu of a selected and of another
//     block, the floating tools, a mouse drag over three paragraphs of a document (the list does
//     not scroll), a drag to the view's edge (the list scrolls, the selection follows), the band
//     after scrolling far away and back, and "Zaznacz tekst" (a mouse selection across paragraphs,
//     Ctrl+C, "Kopiuj wszystko", Esc);
//   touch: a long press on a block's text (the selection mode, its tools docked), a tap extending
//     it, Kopiuj, the end grip dragged, a long press dragged to the view's edge (autoscroll), Back,
//     the editing toolbar's "Zaznacz", the bullet menu's "Zaznacz", and "Zaznacz tekst" (a long
//     press on a word, its handle dragged, Kopiuj, Back).
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property TestEvent events: TestEvent {}
    property int rowA: -1
    property int rowB: -1
    property int rowC: -1
    property real contentBefore: 0
    property string textBefore: ""
    property int countBefore: 0

    // ---- Helpers ------------------------------------------------------------------------------------
    readonly property ListView list: editor.listView
    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function rowOf(prefix) { return window.rowOf(prefix) }
    function info(row) { return page.blocks.get(row) }
    function textOf(row) { return window.renderedOf(row) }
    function delegateAt(row) { return window.delegateAt(row) }
    function selected() {
        const out = []
        for (const r of page.selectedRows())
            out.push(r)
        return out
    }
    /// The clipboard's text exactly (no-break spaces kept): this app's copies are read as written.
    function clip() { return page.clipboardText() }
    /// The same with no-break spaces as spaces (the renderer joins one-letter Polish words to the
    /// next one with them: "w\u00a0domu" — the text shown, and copied, as it is shown).
    function clipSpaces() { return clip().replace(/\u00a0/g, " ") }
    /// `s` with every character outside printable ASCII as \uXXXX (failure messages).
    function codes(s) {
        let out = ""
        for (let i = 0; i < s.length; ++i) {
            const c = s.charCodeAt(i)
            out += c < 32 || c > 126 ? "\\u" + c.toString(16).padStart(4, "0") : s.charAt(i)
        }
        return out
    }
    function key(k, modifiers) { EditorInput.sendKey(list, k, modifiers || 0, "") }
    function editKey(k, modifiers) { window.realKey(k, modifiers || 0, "") }
    function edgeOf(row) {
        const item = delegateAt(row)
        return item ? item.selectionEdge : "?"
    }
    function center(row) {
        editor.listView.positionViewAtIndex(row, ListView.Center)
    }
    /// Visible rows that can be selected (not the page's properties).
    function selectableCount() {
        let n = 0
        for (let r = 0; r < page.blocks.count; ++r) {
            const b = info(r)
            if (b.visible && b.view !== "frontmatter")
                ++n
        }
        return n
    }
    /// A finger held on (x, y) of `target()` for about a second, then lifted: steps.
    function longPress(target, x, y, then, releasedByThen) {
        const out = [() => {
            const item = target()
            check(item !== null, "something to hold")
            if (item)
                events.touchEvent(item).press(0, item, x(item), y(item)).commit()
        }]
        for (let i = 0; i < 9; ++i)
            out.push(() => {})
        if (then)
            out.push(...then)
        if (!releasedByThen) {
            out.push(() => {
                const item = target()
                if (item)
                    events.touchEvent(item).release(0, item, x(item), y(item)).commit()
            })
        }
        return out
    }
    function fingerTap(item, x, y) {
        events.touchEvent(item).press(0, item, x, y).commit()
        events.touchEvent(item).release(0, item, x, y).commit()
    }
    function tap(item) {
        if (!item)
            return
        if (Theme.touchUi)
            fingerTap(item, item.width / 2, item.height / 2)
        else
            events.mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
    }
    function rightClick(item, x, y) {
        if (item)
            events.mouseClick(item, x, y, Qt.RightButton, Qt.NoModifier, -1)
    }
    function toolbar() { return window.findItem(editor, "editToolbarSlot") }
    function shown(item, name) { return window.findShown(item, name) }
    /// Scroll the docked toolbar's row so that `button` is on screen.
    function revealTool(button) {
        const flick = window.findItem(editor, "editToolbarFlick")
        if (!flick || !button)
            return
        const x = button.mapToItem(flick.contentItem, 0, 0).x
        flick.contentX = Math.max(0, Math.min(x - Theme.space8, flick.contentWidth - flick.width))
    }
    function selectTextView() { return window.findItem(editor.dockHost, "selectTextEdit") }

    Component.onCompleted: window.runSteps(Theme.touchUi ? touchSteps() : desktopSteps())

    // ---- Desktop ------------------------------------------------------------------------------------
    function desktopSteps() {
        return [
            wait(() => page.loaded && rowOf("Przygotowanie") > 0, "the outline is open"),
            // ---- Esc, Shift+↓ twice, Ctrl+C -----------------------------------------------------------
            () => {
                test.rowA = rowOf("Przygotowanie")
                center(test.rowA)
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "a block is edited"),
            () => editKey(Qt.Key_Escape),
            () => {
                check(page.selectedRow === test.rowA && page.editingRow < 0, "Esc selects the block")
                check(selected().length === 7, "with its children (folded or not): " + selected().length)
                key(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => {
                check(page.selectedRow === rowOf("Prace") && page.selectionAnchorRow === test.rowA,
                      "Shift+↓ jumps over the selected children to the next block: " + page.selectedRow)
                key(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => {
                check(page.selectedRow === rowOf("Notatki z rozmowy"), "Shift+↓ again: " + page.selectedRow)
                check(edgeOf(test.rowA) === "first" && edgeOf(rowOf("Prace")) === "middle"
                      && edgeOf(rowOf("Ekipa może")) === "last", "one band over the range")
                window.lastNotice = ""
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip().startsWith("- Przygotowanie ^przyg\n\t- [x] Zmierzyć ściany") && clip().indexOf("\n- Prace\n") > 0
                       && clip().endsWith("- Notatki z rozmowy z ekipą — zobacz ((przyg))\n\t- Ekipa może zacząć 20 października, cena ==do negocjacji=="),
                 () => "Ctrl+C copies the three blocks with their children as Markdown: " + JSON.stringify(clip().slice(0, 60))),
            () => {
                check(window.lastNotice !== "", "and says so: " + window.lastNotice)
                key(Qt.Key_Escape)
            },
            () => check(page.selectedRow < 0 && page.selectionCount === 0, "Esc clears the selection"),
            // ---- Shift+↑ from the first line of an edited block; Shift+↓ from inside one ---------------
            () => editor.startEditing(rowOf("Skuć"), 0),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Skuć is edited"),
            () => editKey(Qt.Key_Up, Qt.ShiftModifier),
            () => check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Prace"),
                        "Shift+↑ at the start of the first line selects the block and the one above"),
            () => editor.startEditing(rowOf("Położyć"), 3),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Położyć is edited"),
            () => editKey(Qt.Key_Down, Qt.ShiftModifier),
            () => {
                const edit = editor.activeEditor
                check(page.editingRow === rowOf("Położyć") && edit !== null && edit.selectionStart === 3 && edit.selectionEnd === edit.length,
                      "Shift+↓ on the last line first selects the text to the block's end")
                editKey(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => check(page.editingRow < 0 && page.selectionAnchorRow === rowOf("Położyć") && page.selectedRow === rowOf("Podlać"),
                        "then the next block"),
            // ---- Ctrl+A twice -------------------------------------------------------------------------
            () => editor.startEditing(rowOf("Kolejność"), 2),
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Kolejność is edited"),
            () => editKey(Qt.Key_A, Qt.ControlModifier),
            () => {
                const edit = editor.activeEditor
                check(page.editingRow === rowOf("Kolejność") && edit && edit.selectionStart === 0 && edit.selectionEnd === edit.length,
                      "the first Ctrl+A selects the block's text")
                editKey(Qt.Key_A, Qt.ControlModifier)
            },
            () => check(page.editingRow < 0 && page.selectionCount === selectableCount(),
                        "the second Ctrl+A selects every block: " + page.selectionCount + " of " + selectableCount()),
            () => key(Qt.Key_Escape),
            // ---- Shift+click; Delete and Ctrl+Z ---------------------------------------------------------
            () => {
                test.rowA = rowOf("Zmierzyć")
                center(test.rowA)
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "Zmierzyć is edited"),
            () => {
                const text = textOf(rowOf("Wywieźć"))
                events.mouseClick(text, 30, text.height / 2, Qt.LeftButton, Qt.ShiftModifier, -1)
            },
            () => {
                check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === rowOf("Wywieźć"),
                      "Shift+click selects from the edited block to the clicked one")
                test.textBefore = window.pageText()
                key(Qt.Key_Delete)
            },
            wait(() => window.pageText().indexOf("Zadzwonić do hydraulika") < 0, "Delete removes the selected blocks"),
            () => {
                check(page.selectedRow === rowOf("Przygotowanie"), "the block before them is selected")
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => window.pageText() === test.textBefore, "Ctrl+Z brings them back byte for byte"),
            () => check(page.selectionAnchorRow === rowOf("Zmierzyć") && page.selectedRow === rowOf("Wywieźć"),
                        "selected again"),
            // ---- Alt+↓ and Tab on a range ------------------------------------------------------------
            () => {
                page.selectRange(rowOf("Skuć"), rowOf("Położyć"))
                list.forceActiveFocus()
                test.textBefore = window.pageText()
                key(Qt.Key_Down, Qt.AltModifier)
            },
            () => {
                check(rowOf("Podlać") < rowOf("Skuć") && rowOf("Skuć") + 1 === rowOf("Położyć"),
                      "Alt+↓ moves the range below its next sibling (with that one's child)")
                check(page.selectionAnchorRow === rowOf("Skuć") && page.selectedRow === rowOf("Położyć"), "the range stays selected")
                key(Qt.Key_Tab)
            },
            () => {
                check(info(rowOf("Skuć")).depth === 2 && info(rowOf("Położyć")).depth === 2, "Tab indents the range")
                key(Qt.Key_Z, Qt.ControlModifier)
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => window.pageText() === test.textBefore, "undone"),
            // ---- Ctrl+V over a selection ---------------------------------------------------------------
            () => {
                page.selectRange(rowOf("Skuć"), rowOf("Skuć"))
                list.forceActiveFocus()
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip() === "- [ ] Skuć stare płytki 📅 2026-10-13", "Ctrl+C copies one block"),
            () => {
                page.selectedRow = rowOf("Położyć")
                key(Qt.Key_V, Qt.ControlModifier)
            },
            () => {
                check(rowOf("Położyć") < 0 && window.pageText().split("Skuć stare płytki").length === 3,
                      "Ctrl+V replaces the selected block with the copied one")
                key(Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => window.pageText() === test.textBefore, "undone"),
            // ---- In-block Ctrl+C / Ctrl+X keep no-break spaces ------------------------------------------
            () => {
                test.rowA = page.insertBlockAfter(rowOf("Ekipa może"), "Jadę w\u00a0góry z\u00a0Anią")
            },
            wait(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && editor.activeEditor.row === test.rowA,
                 "a block with no-break spaces is edited"),
            () => {
                editor.activeEditor.select(5, 13)
                editKey(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip() === "w\u00a0góry z", () => "Ctrl+C copies the exact characters: " + JSON.stringify(clip())),
            () => editKey(Qt.Key_X, Qt.ControlModifier),
            wait(() => window.pageText().indexOf("- Jadę \u00a0Anią\n") > 0 && clip() === "w\u00a0góry z", "Ctrl+X cuts them"),
            // ---- The edited text's menu -------------------------------------------------------------------
            () => {
                const edit = editor.activeEditor
                edit.select(0, 4)
                const r = edit.positionToRectangle(2)
                events.mouseClick(edit, r.x, r.y + r.height / 2, Qt.RightButton, Qt.NoModifier, -1)
            },
            wait(() => editor.textMenu !== null && editor.textMenu.opened, "a right-click in the edited text opens its menu"),
            () => {
                check(editor.activeEditor.selectionStart === 0 && editor.activeEditor.selectionEnd === 4,
                      "the selection stays while the menu is open")
                check(!editor.blockMenu || !editor.blockMenu.opened, "and no block menu")
                tap(window.menuItemNamed(editor.textMenu, "editTextCopy"))
            },
            wait(() => clip() === "Jadę" && !editor.textMenu.opened, "its Kopiuj copies the selected text"),
            () => {
                page.undo()
                page.stopEditing()
            },
            // ---- The block menu: of another block, of a selected one --------------------------------------
            () => {
                center(rowOf("Plan remontu"))
                const text = textOf(rowOf("Plan remontu"))
                rightClick(text, text.width / 2, 10)
            },
            wait(() => editor.blockMenu !== null && editor.blockMenu.opened, "a right-click on a paragraph opens the block menu"),
            () => {
                const copy = window.menuItemNamed(editor.blockMenu, "blockMenuCopy")
                check(copy !== null && !editor.blockMenu.inSelection, "with Kopiuj")
                tap(copy)
            },
            wait(() => clip().indexOf("Plan remontu na październik.") === 0, "Kopiuj copies that block"),
            () => {
                page.selectRange(rowOf("Zadzwonić"), rowOf("Prace"))
                const text = textOf(rowOf("Wywieźć"))
                rightClick(text, 20, text.height / 2)
            },
            wait(() => editor.blockMenu !== null && editor.blockMenu.opened && editor.blockMenu.inSelection,
                 "the menu of a selected block acts on the selection"),
            () => tap(window.menuItemNamed(editor.blockMenu, "blockMenuCopyText")),
            wait(() => clip().indexOf("☐ Zadzwonić do hydraulika") === 0 && clip().indexOf("\n☐ Wywieźć gruz") > 0
                       && clip().indexOf("\n• Prace\n  ☐ Skuć") > 0,
                 () => "Kopiuj jako tekst copies the selection as text: " + JSON.stringify(clip().slice(0, 80))),
            // ---- The floating tools ---------------------------------------------------------------------
            () => {
                const tools = window.findShown(editor.dockHost, "floatingSelectionTools")
                check(tools !== null, "two or more blocks selected: the floating tools")
                tap(window.findShown(tools, "selectionCopy"))
            },
            wait(() => clip().indexOf("- [/] Zadzwonić do hydraulika") === 0, "their Kopiuj copies the selection as Markdown"),
            // ---- The band after scrolling far away and back -----------------------------------------------
            () => list.positionViewAtEnd(),
            () => list.positionViewAtBeginning(),
            () => center(rowOf("Wywieźć")),
            () => check(window.findShown(delegateAt(rowOf("Wywieźć")), "selectionBand") !== null && edgeOf(rowOf("Prace")) !== "",
                        "the band is there again after its blocks were scrolled away"),
            () => key(Qt.Key_Escape),
            // ---- A mouse drag from one block into another; at the view's edge the list scrolls -------------
            () => list.positionViewAtBeginning(),
            () => {
                test.contentBefore = list.contentY
                test.rowA = rowOf("Zmierzyć")
                const text = textOf(test.rowA)
                events.mousePress(text, 40, text.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
                events.mouseMove(text, 60, text.height / 2 + 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const text = textOf(rowOf("Zadzwonić"))
                events.mouseMove(text, 60, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                check(page.editingRow < 0 && page.selectionAnchorRow === test.rowA && page.selectedRow === rowOf("Zadzwonić"),
                      "dragging into another block selects the range")
                check(Math.abs(list.contentY - test.contentBefore) < 1, "a mouse drag does not scroll the list")
                events.mouseMove(list, list.width / 2, list.height - 4, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {},
            () => {
                check(list.contentY > test.contentBefore + 100, "at the view's bottom edge the list scrolls: " + list.contentY)
                check(page.selectedRow > rowOf("Notatki z rozmowy"), "and the selection follows the pointer: " + page.selectedRow)
                const y = list.height - 4
                events.mouseRelease(list, list.width / 2, y, Qt.LeftButton, Qt.NoModifier, -1)
            },
            () => {
                check(!editor.dragSelecting && page.editingRow < 0 && page.selectionAnchorRow === test.rowA,
                      "the release ends the drag, nothing is edited")
                key(Qt.Key_Escape)
            },
            // ---- A document: a drag over three paragraphs --------------------------------------------------
            () => { page.path = host.documentPath },
            wait(() => page.loaded && page.documentMode && rowOf("Pierwszego dnia") > 0, "the document is open"),
            () => list.positionViewAtBeginning(),
            () => {
                test.rowA = rowOf("Trzy dni")
                test.rowB = rowOf("Pierwszego dnia")
                test.contentBefore = list.contentY
                const text = textOf(test.rowA)
                events.mousePress(text, 30, 10, Qt.LeftButton, Qt.NoModifier, -1)
                events.mouseMove(text, 50, 14, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const text = textOf(rowOf("Plan"))
                events.mouseMove(text, 40, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const text = textOf(test.rowB)
                events.mouseMove(text, 80, 10, -1, Qt.LeftButton, Qt.NoModifier)
                events.mouseRelease(text, 80, 10, Qt.LeftButton, Qt.NoModifier, -1)
            },
            () => {
                check(page.selectionAnchorRow === test.rowA && page.selectedRow === test.rowB && page.selectionCount === 3,
                      "a drag over three paragraphs selects them: " + page.selectionCount)
                check(edgeOf(test.rowA) === "first" && edgeOf(rowOf("Plan")) === "middle" && edgeOf(test.rowB) === "last",
                      "on one band")
                check(Math.abs(list.contentY - test.contentBefore) < 1 && page.editingRow < 0, "nothing scrolled, nothing edited")
                key(Qt.Key_C, Qt.ControlModifier)
            },
            wait(() => clip().indexOf("Trzy dni w Zakopanem") === 0 && clip().indexOf("\n\n## Plan\n\nPierwszego dnia") > 0,
                 "copied as Markdown with its blank lines"),
            // ---- "Zaznacz tekst" -------------------------------------------------------------------------
            () => editor.openSelectText(),
            wait(() => selectTextView() !== null && selectTextView().activeFocus, "“Zaznacz tekst” opens with the page as text"),
            () => {
                const text = selectTextView()
                const all = text.getText(0, text.length)
                const a = text.positionToRectangle(all.indexOf("Budżet liczymy"))
                const b = text.positionToRectangle(all.indexOf("Krupówki, Gubałówka"))
                events.mousePress(text, a.x + 1, a.y + a.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
                events.mouseMove(text, a.x + 30, a.y + a.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
                events.mouseMove(text, b.x + 2, b.y + b.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
                events.mouseRelease(text, b.x + 2, b.y + b.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            },
            () => {
                const text = selectTextView()
                check(text.selectedText.indexOf("Budżet liczymy") === 0 && text.selectedText.indexOf("Plan") > 0,
                      "a mouse drag selects across paragraphs: " + JSON.stringify(text.selectedText.slice(0, 40)))
                EditorInput.sendKey(text, Qt.Key_C, Qt.ControlModifier, "")
            },
            wait(() => clipSpaces().indexOf("Budżet liczymy w Budżet domowy. #podróże\nPlan\nPierwszego dnia") === 0
                       && clip() === EditorInput.textRange(selectTextView().textDocument, selectTextView().selectionStart,
                                                           selectTextView().selectionEnd),
                 () => "Ctrl+C copies the selected text as plain text: " + codes(clip()) + " vs "
                       + codes(EditorInput.textRange(selectTextView().textDocument, selectTextView().selectionStart,
                                                     selectTextView().selectionEnd))),
            () => {
                selectTextView().deselect()
                tap(window.findShown(editor.dockHost, "selectTextCopy"))
            },
            wait(() => clipSpaces().indexOf("Wyjazd w Tatry") === 0 && clipSpaces().indexOf("\n• Piątek: Gubałówka i Krupówki\n") > 0
                       && clipSpaces().indexOf("\n☑ Buty trekkingowe ✅ pn 12 paź\n") > 0 && clipSpaces().indexOf("\n1. Pociąg") > 0,
                 () => "with nothing selected Kopiuj copies the whole page, bullets and checkboxes included: "
                       + codes(clip().slice(0, 120)) + " … " + codes(clip().slice(clip().indexOf("Niedziela"), clip().indexOf("Niedziela") + 200))),
            () => EditorInput.sendKey(selectTextView(), Qt.Key_Escape, 0, ""),
            wait(() => !editor.selectTextOpen && list.activeFocus, "Esc closes it"),
        ]
    }

    // ---- Touch --------------------------------------------------------------------------------------
    function touchSteps() {
        const textAt = (row) => () => textOf(row())
        return [
            wait(() => page.loaded && rowOf("Przygotowanie") > 0, "the outline is open"),
            () => center(rowOf("Zadzwonić")),
            // ---- A long press on a block's text: the selection mode ----------------------------------------
            ...longPress(textAt(() => rowOf("Zadzwonić")), (t) => t.width / 2, (t) => t.height / 2),
            wait(() => editor.selectionMode && editor.toolbarMode === "selection" && page.selectedRow === rowOf("Zadzwonić"),
                 "a long press on text selects the block (the selection mode)"),
            () => {
                check(page.editingRow < 0, "nothing is edited")
                check(shown(toolbar(), "toolbarSelectionTools") !== null, "the selection tools are docked")
                check(window.findShown(delegateAt(rowOf("Zadzwonić")), "selectionGripStart") !== null, "with grips")
                const text = textOf(rowOf("Położyć"))
                fingerTap(text, text.width / 3, text.height / 2)
            },
            () => check(page.selectionAnchorRow === rowOf("Zadzwonić") && page.selectedRow === rowOf("Położyć")
                        && page.editingRow < 0 && editor.selectionMode,
                        "a tap on another block extends the selection (no editing)"),
            () => tap(shown(toolbar(), "selectionCopy")),
            wait(() => clip().indexOf("- [/] Zadzwonić do hydraulika") === 0 && clip().indexOf("- [ ] Położyć hydroizolację") > 0,
                 "Kopiuj copies it"),
            // ---- The end grip -------------------------------------------------------------------------------
            () => {
                const grip = window.findShown(delegateAt(page.selectionLastRow), "selectionGripEnd")
                check(grip !== null, "the band's end has a grip")
                events.touchEvent(grip).press(0, grip, grip.width / 2, grip.height / 2).commit()
                events.touchEvent(grip).move(0, grip, grip.width / 2, grip.height / 2 + 12).commit()
            },
            () => {
                const text = textOf(rowOf("Kolejność"))
                events.touchEvent(text).move(0, text, 30, text.height / 2).commit()
            },
            () => {
                const text = textOf(rowOf("Kolejność"))
                events.touchEvent(text).release(0, text, 30, text.height / 2).commit()
            },
            () => check(page.selectedRow === rowOf("Kolejność") && page.selectionAnchorRow === rowOf("Zadzwonić"),
                        "dragging the end grip moves the selection's end: " + page.selectedRow),
            // ---- Back leaves the selection mode -----------------------------------------------------------
            () => key(Qt.Key_Back),
            () => check(!editor.selectionMode && page.selectionCount === 0 && editor.toolbarMode === "",
                        "Back leaves the selection mode"),
            // ---- A long press dragged to the view's edge: autoscroll ----------------------------------------
            () => list.positionViewAtBeginning(),
            () => { test.contentBefore = list.contentY },
            ...longPress(textAt(() => rowOf("Zmierzyć")), (t) => t.width / 2, (t) => t.height / 2, [
                () => check(editor.selectionMode, "the long press selected the block"),
                () => {
                    const text = textOf(rowOf("Kupić płytki"))
                    events.touchEvent(text).move(0, text, text.width / 2, text.height / 2).commit()
                },
                () => check(page.selectedRow === rowOf("Kupić płytki") && Math.abs(list.contentY - test.contentBefore) < 1,
                            "moving the finger without lifting it extends the selection (no scrolling)"),
                () => events.touchEvent(list).move(0, list, list.width / 2, list.height - 6).commit(),
                () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {},
                () => {
                    check(list.contentY > test.contentBefore + 100, "at the view's bottom edge the list scrolls: " + list.contentY)
                    check(page.selectedRow > rowOf("Prace"), "the selection follows the finger: " + page.selectedRow)
                    events.touchEvent(list).release(0, list, list.width / 2, list.height - 6).commit()
                },
            ], true),
            () => check(editor.selectionMode && list.interactive && page.selectionAnchorRow === rowOf("Zmierzyć"),
                        "the lifted finger leaves the selection in the selection mode"),
            () => key(Qt.Key_Back),
            // ---- The editing toolbar's "Zaznacz" ------------------------------------------------------------
            () => {
                test.rowA = rowOf("Skuć")
                center(test.rowA)
                editor.startEditing(test.rowA, -1)
            },
            wait(() => editor.toolbarMode === "block" && shown(toolbar(), "toolbarSelect") !== null, "the editing toolbar has Zaznacz"),
            () => revealTool(shown(toolbar(), "toolbarSelect")),
            () => tap(shown(toolbar(), "toolbarSelect")),
            wait(() => editor.selectionMode && page.selectedRow === test.rowA && page.editingRow < 0,
                 "Zaznacz selects the edited block (the selection mode)"),
            () => {
                const done = shown(toolbar(), "selectionDone")
                check(done !== null && done.mapToItem(null, 0, 0).x < 60, "Gotowe is the bar's first tool")
                tap(done)
            },
            () => check(!editor.selectionMode && page.selectionCount === 0, "Gotowe leaves the selection mode"),
            // ---- The bullet's menu: Zaznacz -----------------------------------------------------------------
            ...longPress(() => delegateAt(rowOf("Kolejność")), (d) => d.bulletX, (d) => d.firstLineCenter),
            wait(() => editor.blockMenu !== null && editor.blockMenu.opened, "a bullet's long press opens the block menu"),
            () => tap(window.menuItemNamed(editor.blockMenu, "blockMenuSelect")),
            wait(() => editor.selectionMode && page.selectedRow === rowOf("Kolejność"), "its Zaznacz selects the block"),
            () => key(Qt.Key_Back),
            // ---- "Zaznacz tekst" with a finger -------------------------------------------------------------
            () => editor.openSelectText(),
            wait(() => selectTextView() !== null, "“Zaznacz tekst” opens"),
            ...longPress(() => selectTextView(), (t) => {
                const r = t.positionToRectangle(t.getText(0, t.length).indexOf("Przygotowanie") + 3)
                return r.x
            }, (t) => {
                const r = t.positionToRectangle(t.getText(0, t.length).indexOf("Przygotowanie") + 3)
                return r.y + r.height / 2
            }),
            wait(() => selectTextView().selectedText === "Przygotowanie", () => "a long press selects the word: " + selectTextView().selectedText),
            () => {
                const handle = window.findShown(editor.dockHost, "selectTextHandleEnd")
                check(handle !== null && window.findShown(editor.dockHost, "selectTextHandleStart") !== null, "with two handles")
                events.touchEvent(handle).press(0, handle, handle.width / 2, handle.height / 2).commit()
                events.touchEvent(handle).move(0, handle, handle.width / 2, handle.height / 2 + 30).commit()
            },
            () => {
                const handle = window.findShown(editor.dockHost, "selectTextHandleEnd")
                events.touchEvent(handle).move(0, handle, handle.width / 2, handle.height / 2 + 60).commit()
            },
            () => {
                const handle = window.findShown(editor.dockHost, "selectTextHandleEnd")
                events.touchEvent(handle).release(0, handle, handle.width / 2, handle.height / 2).commit()
            },
            () => check(selectTextView().selectedText.indexOf("Przygotowanie") === 0 && selectTextView().selectedText.length > 20,
                        "dragging the end handle extends it: " + JSON.stringify(selectTextView().selectedText.slice(0, 40))),
            () => {
                test.textBefore = EditorInput.textRange(selectTextView().textDocument, selectTextView().selectionStart,
                                                        selectTextView().selectionEnd)
                tap(window.findShown(editor.dockHost, "selectTextCopy"))
            },
            wait(() => clip() === test.textBefore && clip().indexOf("\n") > 0, "Kopiuj copies the selected text"),
            () => EditorInput.sendKey(selectTextView(), Qt.Key_Back, 0, ""),
            wait(() => !editor.selectTextOpen, "Back closes it"),
        ]
    }
}
