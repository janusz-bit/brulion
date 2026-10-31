pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PropertiesSelfTest — the page properties panel with real input, in the editor playground
// (`brulion --editor --editor-script propstest`; ctest tst_editor_qml_properties, desktop, and
// tst_editor_qml_properties_touch, a phone with --touch): the folded panel's preview, its header
// tapped open (and staying open through edits: the delegate is not made again); a text value
// typed with real keys ("Zażółć gęślą jaźń") — only that line of the file changes, its comment
// kept —; a number typed "3,5" (written 3.5), "abc" refused with the field's error and the file
// untouched, Ctrl+Z; the checkbox toggled; a date from the dialog's natural language ("jutro");
// a list item added with Enter and taken away with Backspace in the empty field, a chip's ×;
// renaming through the row's menu (an existing name refused); a type changed in the menu's "Typ"
// (text → list); a removal undone from the toast's "Cofnij"; a property added from the key
// suggestions (another page's key) with its value typed at once — a key the page has is refused,
// its value kept —; a field keeps what is typed while the notebook changes, and writes it before
// another page is shown; on a page without properties "Dodaj właściwość" (Ctrl+;) — the draft
// above the blocks writes the frontmatter, also when only a hidden note-mode frontmatter and
// `key::` lines are there; values opened and left unchanged keep their form (`|` text, `1.50`);
// a Logseq text becomes a list of links; an alias no link can use is refused under the field and
// in a toast; a map's row menu offers "Edytuj jako tekst". The key stays beside the value (phones
// too) — a narrow window stacks it over the value —, its whole cell a tap area, and on a phone every
// control and tap area is a 44 px target.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property var events: null
    /// The host's index changes seen (the rows' roles are rebuilt on each).
    property int indexChanges: 0
    property Connections indexWatch: Connections {
        target: test.host
        function onIndexChanged() { test.indexChanges += 1 }
    }
    property int indexBefore: 0
    property Item frontmatter: null
    property string original: ""
    property real widthBefore: 0
    readonly property string path: "Projekty/Remont garażu.md"
    readonly property string content: "---\n"
        + "status: w toku # wybrany\n"
        + "koszt: 1200\n"
        + "gotowe: false\n"
        + "termin: 2026-10-28\n"
        + "tags: [projekt, dom]\n"
        + "autorzy:\n"
        + "  - \"[[Jan Kowalski]]\"\n"
        + "  - Anna\n"
        + "---\n"
        + "# Remont garażu\n\n- Kupić płytki\n- Zamówić hydraulika\n"

    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function text() { return window.pageText() }
    function fileLines() { return text().split("\n") }
    function line(prefix) { return window.fileLineOf(text(), prefix) }
    function view() { return window.findItem(window.delegateAt(0), "frontmatterView") }
    function panel() { return window.findItem(view(), "propertiesPanel") }
    function row(key) { return window.findItem(panel(), "propertyRow_" + key) }
    function inRow(key, name) { return window.findItem(row(key), name) }
    /// The same for the properties block at row `blockRow` (a page with YAML and `key::` lines).
    function panelAt(blockRow) { return window.findItem(window.findItem(window.delegateAt(blockRow), "frontmatterView"), "propertiesPanel") }
    function inRowAt(blockRow, key, name) { return window.findItem(window.findItem(panelAt(blockRow), "propertyRow_" + key), name) }
    function menuOpenAt(blockRow) {
        const p = panelAt(blockRow)
        return p !== null && p.menu !== null && p.menu.opened
    }
    /// The "Typ" item of the row menu `m`.
    function typeItem(m) {
        for (let i = 0; i < m.count; ++i) {
            const it = m.itemAt(i)
            if (it && it.subMenu && it.subMenu.objectName === "propertyTypeMenu")
                return it
        }
        return null
    }
    readonly property string notePath: "Notatka.md"
    readonly property string noteContent: "---\n"
        + "notatka: |\n  linia 1\n  linia 2\n"
        + "aliases: [Dom]\n"
        + "meta:\n  a: 1\n"
        + "---\n"
        + "ilosc:: 1.50\n"
        + "autorzy:: a, b\n\n"
        + "- tekst\n"
    readonly property string hiddenPath: "Logseq.md"
    readonly property string hiddenContent: "---\nbrulion: document\n---\nstatus:: w toku\n- a\n"
    function focused(item) { return item !== null && item.activeFocus }
    /// Type `s` into `item` with real key events (it has the keyboard).
    function type(item, s) {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(item, 0, 0, s.charAt(i))
    }
    function key(item, k, modifiers) { EditorInput.sendKey(item, k, modifiers || 0, "") }
    /// The keyboard to a text input outside the editor (a tap elsewhere): fields lose the focus.
    property Item probe: null
    function focusElsewhere() {
        if (!probe)
            probe = Qt.createQmlObject('import QtQuick; TextInput { width: 10; height: 10 }', window.contentItem,
                                       "propertiesFocusProbe")
        probe.forceActiveFocus()
    }
    /// The file differs from `before` only in the lines starting with `prefixes`.
    function onlyLinesChanged(before, prefixes) {
        const a = before.split("\n").filter(l => !prefixes.some(p => l.startsWith(p)))
        const b = fileLines().filter(l => !prefixes.some(p => l.startsWith(p)))
        return a.join("\n") === b.join("\n")
    }
    /// The item of the row's menu named `name`.
    function menuItem(name) {
        const p = panel()
        return p && p.menu ? window.menuItemNamed(p.menu, name) : null
    }
    function menuOpen() {
        const p = panel()
        return p !== null && p.menu !== null && p.menu.opened
    }
    /// The row's menu from its type icon (a tap or a click).
    function openMenu(key) {
        return [
            () => window.tapItem(inRow(key, "propertyTypeButton")),
            wait(menuOpen, "the type icon of “" + key + "” opens its menu")
        ]
    }
    function choose(name) {
        return [
            () => {
                check(menuItem(name) !== null && menuItem(name).enabled, "the menu has " + name)
                window.tapItem(menuItem(name))
            },
            wait(() => !menuOpen(), "choosing " + name + " closes the menu")
        ]
    }

    function steps() {
        events = Qt.createQmlObject('import QtQuick; import QtTest; TestEvent {}', test, "propertiesEvents")
        window.taskEvents = events // (window.tapItem)
        return [
            () => {
                host.setFile(path, content)
                host.setFile("Ogród.md", "---\nźródło: Wikipedia\nstatus: gotowe\n---\n- grządki\n")
                host.setFile("Bez właściwości.md", "- pierwszy\n- drugi\n")
                host.setFile(notePath, noteContent)
                host.setFile(hiddenPath, hiddenContent)
                editor.setPropertiesExpanded(false)
                page.path = path
            },
            wait(() => page.loaded && page.path === path && view() !== null
                       && view().properties.some(p => p.key === "koszt"), "the page with properties is open"),
            // ---- folded: the preview; the header's tap opens the panel --------------------------------
            () => {
                original = text()
                const preview = window.findItem(view(), "propertiesPreview")
                check(preview !== null && preview.visible && preview.text.indexOf("status: w toku") === 0
                      && preview.text.indexOf("koszt: 1200") > 0, "folded, the header previews the values: "
                      + (preview ? preview.text : "-"))
                check(panel() === null, "folded: no panel made")
                window.tapItem(window.findItem(view(), "propertiesHeader"))
            },
            wait(() => editor.propertiesExpanded && panel() !== null && row("status") !== null,
                 "a tap on the header opens the panel"),
            () => {
                frontmatter = window.delegateAt(0)
                const p = panel()
                check(p.stacked === (p.width - p.iconWidth - p.keyWidth - Theme.space2 < 168),
                      "the layout follows the value column's width: stacked " + p.stacked)
                check(!p.stacked && inRow("status", "propertyValue").mapToItem(row("status"), 0, 0).x >= p.iconWidth + p.keyWidth,
                      "the value is in a column beside the key (" + p.width + " px)")
                const keyCell = inRow("status", "propertyKeyCell")
                check(keyCell !== null && keyCell.height >= row("status").height && keyCell.width >= p.keyWidth,
                      "the key's tap area is its whole cell: " + (keyCell ? keyCell.width + "×" + keyCell.height : "-"))
                if (Theme.touchUi) {
                    const button = inRow("status", "propertyTypeButton")
                    check(button.height >= Theme.touchTarget && button.width >= Theme.touchTarget,
                          "the type icon is a 44 px target: " + button.width + "×" + button.height)
                    check(keyCell !== null && keyCell.height >= Theme.touchTarget, "the key's cell is a 44 px target")
                    const date = inRow("termin", "propertyDateButton")
                    check(date !== null && date.height >= Theme.touchTarget, "the date button is a 44 px target: "
                          + (date ? date.height : "-"))
                    check(inRow("gotowe", "propertyCheckBox").height >= Theme.touchTarget, "the checkbox is a 44 px target")
                    check(window.findItem(p, "propertiesAdd").height >= Theme.touchTarget, "“Dodaj właściwość” is a 44 px target")
                }
                // ---- one row rhythm: a list's chip line is centred in the row's height as a text
                //      line is — one line is the row's height (touch 44, mouse 32), more lines whole
                //      chip lines apart (touch 44 / 92) —; the chips and "+" keep their targets.
                check(Math.abs(row("status").height - p.rowHeight) < 0.5, "a text row is the row's height: "
                      + row("status").height + " / " + p.rowHeight)
                for (const key of ["tags", "autorzy"]) {
                    const r = row(key)
                    const line = p.rowHeight - 2 * r.valuePadding
                    const lines = (r.height - 2 * r.valuePadding + Theme.space1) / (line + Theme.space1)
                    check(line > 0 && lines >= 1 && Math.abs(lines - Math.round(lines)) < 0.01,
                          key + ": whole chip lines on the row's grid: " + r.height + " px, padding " + r.valuePadding)
                    if (Theme.touchUi) {
                        check(r.valuePadding === 0, key + ": on touch UIs the chips' 44-px line is the row's height")
                        const chip = inRow(key, "propertyChip_0")
                        const add = inRow(key, "propertyListAdd")
                        check(chip !== null && chip.height >= Theme.touchTarget && add !== null && add.height >= Theme.touchTarget
                              && add.width >= Theme.touchTarget, key + ": the chips and “+” stay 44 px targets")
                    } else {
                        check(Math.abs(r.height - p.rowHeight) < 0.5, key + ": one chip line, the row's height: "
                              + r.height + " / " + p.rowHeight)
                    }
                }
                // ---- a narrow window: the key over the value -----------------------------------------
                widthBefore = window.width
                window.width = 300
            },
            wait(() => panel() !== null && panel().stacked, "a narrow panel stacks the key over the value"),
            () => {
                const r = row("status")
                check(inRow("status", "propertyValue").mapToItem(r, 0, 0).y >= r.keyLineHeight,
                      "a stacked row holds the key over the value")
                if (Theme.touchUi)
                    check(r.keyLineHeight >= Theme.touchTarget && inRow("status", "propertyKeyCell").height >= Theme.touchTarget,
                          "the stacked key's line is a 44 px target")
                window.width = widthBefore
            },
            wait(() => panel() !== null && !panel().stacked, "wide again: the key beside the value"),
            () => {
                // ---- text: real keys, only that line changes -----------------------------------------
                window.tapItem(inRow("status", "propertyValue"))
            },
            wait(() => focused(inRow("status", "propertyValueField")), "a tap on the text opens its field"),
            () => {
                const field = inRow("status", "propertyValueField")
                field.selectAll()
                type(field, "Zażółć gęślą jaźń")
                check(field.text === "Zażółć gęślą jaźń", "typed into the field: " + field.text)
                key(field, Qt.Key_Return)
            },
            wait(() => line("status:") === "status: Zażółć gęślą jaźń # wybrany",
                 () => "Enter writes the text, the comment kept: " + line("status:")),
            () => {
                check(onlyLinesChanged(original, ["status:"]), "every other line of the file is as it was")
                check(window.delegateAt(0) === frontmatter && editor.propertiesExpanded,
                      "the panel stays open: its block was not made again")
                check(inRow("status", "propertyValueField") === null, "the field closed")
                // ---- number ----------------------------------------------------------------------------
                window.tapItem(inRow("koszt", "propertyValue"))
            },
            wait(() => focused(inRow("koszt", "propertyNumberField")), "a tap on the number opens its field"),
            () => {
                const field = inRow("koszt", "propertyNumberField")
                field.selectAll()
                type(field, "3,5")
                key(field, Qt.Key_Return)
            },
            wait(() => line("koszt:") === "koszt: 3.5", () => "“3,5” is written as 3.5: " + line("koszt:")),
            () => {
                check(focused(row("koszt")), "the row has the keyboard after Enter")
                key(row("koszt"), Qt.Key_Z, Qt.ControlModifier)
            },
            wait(() => line("koszt:") === "koszt: 1200", () => "Ctrl+Z in the row undoes it: " + line("koszt:")),
            () => window.tapItem(inRow("koszt", "propertyValue")),
            wait(() => focused(inRow("koszt", "propertyNumberField")), "the number's field again"),
            () => {
                const field = inRow("koszt", "propertyNumberField")
                field.selectAll()
                type(field, "abc")
                key(field, Qt.Key_Return)
            },
            () => {
                const field = inRow("koszt", "propertyNumberField")
                check(field !== null && field.errorText !== "", "“abc” is refused in the field: " + (field ? field.errorText : "closed"))
                check(line("koszt:") === "koszt: 1200", "the file is untouched")
                key(field, Qt.Key_Escape)
            },
            wait(() => inRow("koszt", "propertyNumberField") === null, "Escape closes the field"),
            // ---- checkbox ----------------------------------------------------------------------------------
            () => window.tapItem(inRow("gotowe", "propertyCheckBox")),
            wait(() => line("gotowe:") === "gotowe: true", () => "the checkbox writes true: " + line("gotowe:")),
            // ---- date: the dialog, natural language ---------------------------------------------------------
            () => window.tapItem(inRow("termin", "propertyDateButton")),
            wait(() => editor.dialogs.dateDialog !== null && editor.dialogs.dateDialog.opened, "the date opens the date dialog"),
            () => {
                const dialog = editor.dialogs.dateDialog
                check(dialog.title === "termin" && dialog.picker.selectedDate === "2026-10-28" && !dialog.picker.withTime,
                      "titled with the key, on its date: " + dialog.title + " " + dialog.picker.selectedDate)
                dialog.picker.naturalText = "jutro"
                dialog.tryAccept()
            },
            wait(() => !editor.dialogs.dateDialog.opened, "the dialog closes"),
            () => check(line("termin:") === "termin: 2026-10-16", "“jutro” is written as a date: " + line("termin:")),
            // ---- list: Enter adds, Backspace in the empty field takes the last, × removes --------------
            () => window.tapItem(inRow("autorzy", "propertyListAdd")),
            wait(() => focused(inRow("autorzy", "propertyListField")), "“+” opens the list's field"),
            () => {
                const field = inRow("autorzy", "propertyListField")
                type(field, "Łucja")
                key(field, Qt.Key_Return)
            },
            wait(() => text().indexOf("autorzy:\n  - \"[[Jan Kowalski]]\"\n  - Anna\n  - Łucja\n") > 0,
                 () => "Enter adds the item, the others as written: " + text().split("---")[1]),
            () => {
                const field = inRow("autorzy", "propertyListField")
                check(focused(field) && field.text === "", "the field stays open, empty, for the next one")
                key(field, Qt.Key_Backspace)
            },
            wait(() => text().indexOf("autorzy:\n  - \"[[Jan Kowalski]]\"\n  - Anna\n---") > 0,
                 "Backspace in the empty field removes the last item"),
            () => {
                key(inRow("autorzy", "propertyListField"), Qt.Key_Escape)
                const chip = inRow("autorzy", "propertyChip_1")
                check(chip !== null && chip.removable, "the chips can be removed")
                chip.removeClicked()
            },
            wait(() => text().indexOf("autorzy:\n  - \"[[Jan Kowalski]]\"\n---") > 0, "× removes an item"),
            () => page.undo(),
            // ---- rename through the menu ----------------------------------------------------------------
            ...openMenu("koszt"),
            ...choose("propertyRename"),
            wait(() => focused(inRow("koszt", "propertyRenameField")), "“Zmień nazwę” opens the key's field"),
            () => {
                const field = inRow("koszt", "propertyRenameField")
                field.selectAll()
                type(field, "status")
                key(field, Qt.Key_Return)
            },
            () => {
                const field = inRow("koszt", "propertyRenameField")
                check(field !== null && field.errorText !== "", "an existing name is refused: " + (field ? field.errorText : "-"))
                field.selectAll()
                type(field, "budżet")
                key(field, Qt.Key_Return)
            },
            wait(() => line("budżet:") === "budżet: 1200" && line("koszt:") === "", () => "renamed: " + line("budżet:")),
            // ---- the type menu: text → list -----------------------------------------------------------------
            ...openMenu("status"),
            () => {
                const m = panel().menu
                let sub = null
                for (let i = 0; i < m.count; ++i) {
                    const it = m.itemAt(i)
                    if (it && it.subMenu && it.subMenu.objectName === "propertyTypeMenu")
                        sub = it
                }
                check(sub !== null && sub.enabled, "the menu has “Typ”")
                window.tapItem(sub)
            },
            wait(() => panel().menu.typeMenu.opened, "“Typ” opens its submenu"),
            () => {
                const item = window.menuItemNamed(panel().menu.typeMenu, "propertyType_list")
                check(item !== null && !item.checked, "“Lista” is there, not the current type")
                window.tapItem(item)
            },
            wait(() => text().indexOf("status: # wybrany\n  - Zażółć gęślą jaźń\n") > 0,
                 () => "the text becomes a list: " + text().split("---")[1]),
            () => page.undo(),
            // ---- remove, "Cofnij" in the toast ----------------------------------------------------------------
            ...openMenu("gotowe"),
            ...choose("propertyRemove"),
            wait(() => line("gotowe:") === "", "“Usuń” removes the property"),
            () => {
                check(window.lastNoticeAction !== null && window.lastNotice.indexOf("gotowe") >= 0,
                      "a toast with “Cofnij”: " + window.lastNotice)
                window.lastNoticeAction.run()
            },
            wait(() => line("gotowe:") === "gotowe: true", "“Cofnij” brings it back"),
            // ---- add a property from the suggestions -------------------------------------------------------
            () => window.tapItem(window.findItem(panel(), "propertiesAdd")),
            wait(() => focused(window.findItem(panel(), "propertiesKeyField")), "“Dodaj właściwość” opens the key's field"),
            () => type(window.findItem(panel(), "propertiesKeyField"), "zro"),
            wait(() => {
                const f = window.findItem(panel(), "propertiesKeyField")
                return f !== null && f.listShown && f.suggestions.length === 1 && f.suggestions[0].title === "źródło"
            }, "another page's key is suggested (diacritics ignored), not the page's own"),
            // The field moves under its list (scrolled above the keyboard, a flick): the list follows.
            () => {
                editor.listView.topMargin += 60
                editor.listView.contentY -= 60
            },
            wait(() => {
                const f = window.findItem(panel(), "propertiesKeyField")
                const p = f ? f.suggestionPopup : null
                if (!p || !p.opened)
                    return false
                const top = f.mapToItem(p.parent, 0, 0).y + f.topInset
                const bottom = f.mapToItem(p.parent, 0, f.height).y - f.bottomInset
                return Math.abs(p.y - (bottom + 4)) <= 1 || Math.abs(p.y + p.height - (top - 4)) <= 1
            }, "the suggestions follow their field when it moves"),
            () => {
                editor.listView.contentY += 60
                editor.listView.topMargin -= 60
            },
            () => {
                const f = window.findItem(panel(), "propertiesKeyField")
                key(f, Qt.Key_Down)
                key(f, Qt.Key_Return)
            },
            wait(() => line("źródło:") === "źródło:" && focused(inRow("źródło", "propertyValueField")),
                 () => "the key is written at once, its value has the keyboard: " + line("źródło:")),
            () => {
                const field = inRow("źródło", "propertyValueField")
                type(field, "Wikipedia")
                key(field, Qt.Key_Return)
            },
            wait(() => line("źródło:") === "źródło: Wikipedia", () => "the value: " + line("źródło:")),
            // ---- adding a key the page has: refused, its value kept ---------------------------------------------
            () => {
                original = text()
                window.tapItem(window.findItem(panel(), "propertiesAdd"))
            },
            wait(() => focused(window.findItem(panel(), "propertiesKeyField")), "the key's field again"),
            () => {
                const f = window.findItem(panel(), "propertiesKeyField")
                type(f, "status")
                key(f, Qt.Key_Return)
            },
            () => {
                const f = window.findItem(panel(), "propertiesKeyField")
                check(f !== null && focused(f) && f.errorText !== "",
                      "a key the page has is refused in the field: " + (f ? f.errorText : "closed"))
                check(text() === original, "its value is kept: " + line("status:"))
                f.selectAll()
                type(f, "TAGS")
                key(f, Qt.Key_Return)
            },
            () => {
                const f = window.findItem(panel(), "propertiesKeyField")
                check(f !== null && f.errorText !== "" && text() === original, "in any spelling: the tags are kept")
                key(f, Qt.Key_Escape)
            },
            wait(() => window.findItem(panel(), "propertiesKeyField") === null, "Escape closes the add field"),
            // ---- a field keeps what is typed while the notebook changes ------------------------------------------
            () => window.tapItem(inRow("status", "propertyValue")),
            wait(() => focused(inRow("status", "propertyValueField")), "the text's field"),
            () => {
                const field = inRow("status", "propertyValueField")
                field.selectAll()
                type(field, "WYC")
                // Another page changes (sync, another pane): the notebook's index and the rows' roles.
                indexBefore = indexChanges
                host.externalWrite("Ogród.md", "---\nźródło: Wikipedia\nstatus: gotowe\netap: nowy\n---\n- grządki\n")
            },
            wait(() => indexChanges > indexBefore && page.notebookPropertyType("etap") === "text",
                 "the notebook knows the other page's change"),
            () => {
                const field = inRow("status", "propertyValueField")
                check(field !== null && focused(field) && field.text === "WYC",
                      "the field keeps what is typed: " + (field ? field.text : "closed"))
                key(field, Qt.Key_Escape)
            },
            wait(() => inRow("status", "propertyValueField") === null && text() === original,
                 "Escape leaves the value as it was"),
            // ---- another page shown while a field holds a value: it is written first --------------------------
            () => window.tapItem(inRow("status", "propertyValue")),
            wait(() => focused(inRow("status", "propertyValueField")), "the text's field once more"),
            () => {
                const field = inRow("status", "propertyValueField")
                field.selectAll()
                type(field, "WYCIEK")
                page.path = "Bez właściwości.md"
            },
            wait(() => page.loaded && page.path === "Bez właściwości.md", "another page"),
            () => check(window.fileLineOf(host.file(path), "status:") === "status: WYCIEK # wybrany"
                        && host.file("Bez właściwości.md") === "- pierwszy\n- drugi\n",
                        "the typed value went to its page before the other one was shown: "
                        + window.fileLineOf(host.file(path), "status:")),
            // ---- a page without properties: the draft above the blocks ----------------------------------------
            wait(() => page.loaded && page.path === "Bez właściwości.md" && page.pageProperties.visibleRow < 0,
                 "a page without properties"),
            () => editor.addProperty(),
            wait(() => focused(window.findItem(editor.listView.headerItem, "propertiesKeyField")),
                 "“Dodaj właściwość” (Ctrl+;) opens the draft above the blocks"),
            () => {
                const f = window.findItem(editor.listView.headerItem, "propertiesKeyField")
                type(f, "status")
                key(f, Qt.Key_Return)
            },
            wait(() => text() === "---\nstatus:\n---\n- pierwszy\n- drugi\n" && focused(inRow("status", "propertyValueField")),
                 () => "the first property writes the frontmatter, its value has the keyboard: " + JSON.stringify(text())),
            () => {
                check(!editor.propertyDraft && window.findItem(editor.listView.headerItem, "propertiesKeyField") === null,
                      "the draft is gone")
                key(inRow("status", "propertyValueField"), Qt.Key_Escape)
                page.undo()
            },
            wait(() => text() === "- pierwszy\n- drugi\n", "one undo step takes it back"),
            // ---- a hidden note-mode frontmatter and shown `key::` lines: the draft adds to the frontmatter --
            () => page.path = hiddenPath,
            wait(() => page.loaded && page.path === hiddenPath && page.pageProperties.visibleRow === 1
                       && page.pageProperties.addRow === -1, "a page with only `key::` lines shown"),
            () => editor.addProperty(),
            wait(() => focused(window.findItem(editor.listView.headerItem, "propertiesKeyField")),
                 "“Dodaj właściwość” opens the draft (the frontmatter new keys go to is hidden)"),
            () => {
                const f = window.findItem(editor.listView.headerItem, "propertiesKeyField")
                type(f, "etap")
                key(f, Qt.Key_Return)
            },
            wait(() => text() === "---\nbrulion: document\netap:\n---\nstatus:: w toku\n- a\n"
                       && focused(inRowAt(0, "etap", "propertyValueField")),
                 () => "the key goes into the frontmatter, now shown; its value has the keyboard: " + JSON.stringify(text())),
            () => {
                check(editor.propertyFocus === null && !editor.propertyDraft, "no request is left over")
                key(inRowAt(0, "etap", "propertyValueField"), Qt.Key_Escape)
                page.undo()
            },
            wait(() => text() === hiddenContent, "one undo step takes it back"),
            // ---- values left unchanged keep their form; a Logseq text as a list; a bad alias; a map --------
            () => page.path = notePath,
            wait(() => page.loaded && page.path === notePath && inRowAt(0, "notatka", "propertyValue") !== null
                       && inRowAt(1, "ilosc", "propertyValue") !== null, "a page with both kinds of properties"),
            () => window.tapItem(inRowAt(0, "notatka", "propertyValue")),
            wait(() => focused(inRowAt(0, "notatka", "propertyValueArea")), "a text of several lines opens a text area"),
            () => {
                const area = inRowAt(0, "notatka", "propertyValueArea")
                check(area.text === "linia 1\nlinia 2\n", "the area holds the text: " + JSON.stringify(area.text))
                focusElsewhere()
            },
            wait(() => inRowAt(0, "notatka", "propertyValueArea") === null, "leaving the area closes it"),
            () => {
                check(text() === noteContent, "left unchanged, the `|` text keeps its form: " + JSON.stringify(text()))
                check(!page.canUndo, "no undo step")
                window.tapItem(inRowAt(1, "ilosc", "propertyValue"))
            },
            wait(() => focused(inRowAt(1, "ilosc", "propertyNumberField")), "the `key::` number's field"),
            () => {
                const field = inRowAt(1, "ilosc", "propertyNumberField")
                check(field.text === "1.50", "the field holds the number as written: " + field.text)
                key(field, Qt.Key_Return)
            },
            wait(() => inRowAt(1, "ilosc", "propertyNumberField") === null, "Enter closes the field"),
            () => {
                check(text() === noteContent, "Enter without a change keeps `ilosc:: 1.50`: " + JSON.stringify(text()))
                window.tapItem(inRowAt(1, "autorzy", "propertyTypeButton"))
            },
            wait(() => menuOpenAt(1), "the menu of `autorzy::`"),
            () => {
                const sub = typeItem(panelAt(1).menu)
                check(sub !== null && sub.enabled, "the menu has “Typ”")
                window.tapItem(sub)
            },
            wait(() => panelAt(1).menu.typeMenu.opened, "“Typ” opens its submenu"),
            () => window.tapItem(window.menuItemNamed(panelAt(1).menu.typeMenu, "propertyType_list")),
            wait(() => line("autorzy::") === "autorzy:: [[a]], [[b]]",
                 () => "a Logseq text becomes a list of links: " + line("autorzy::")),
            () => {
                check(inRowAt(1, "autorzy", "propertyChip_1") !== null, "shown as chips")
                window.tapItem(inRowAt(0, "aliases", "propertyListAdd"))
            },
            wait(() => focused(inRowAt(0, "aliases", "propertyListField")), "the aliases' field"),
            () => {
                original = text()
                const field = inRowAt(0, "aliases", "propertyListField")
                type(field, "a|b")
                key(field, Qt.Key_Return)
            },
            () => {
                const field = inRowAt(0, "aliases", "propertyListField")
                check(field !== null && focused(field) && field.errorText !== "" && field.text === "a|b",
                      "an alias no link can use is refused under the field: " + (field ? field.errorText : "closed"))
                check(text() === original, "nothing written")
                window.lastNotice = ""
                focusElsewhere()
            },
            wait(() => inRowAt(0, "aliases", "propertyListField") === null && window.lastNotice !== "",
                 () => "leaving the field closes it, a toast says why: " + window.lastNotice),
            () => {
                check(text() === original, "still nothing written")
                window.tapItem(inRowAt(0, "meta", "propertyTypeButton"))
            },
            wait(() => menuOpenAt(0), "the menu of a map's row"),
            () => {
                const item = window.menuItemNamed(panelAt(0).menu, "propertyEditAsText")
                check(item !== null && item.visible && item.enabled, "a map's row menu offers “Edytuj jako tekst”")
                const rename = window.menuItemNamed(panelAt(0).menu, "propertyRename")
                check(rename !== null && !rename.enabled, "renaming is not offered for it")
                window.tapItem(item)
            },
            wait(() => page.editingRow === 0, "“Edytuj jako tekst” edits the frontmatter's text"),
            () => {
                page.stopEditing()
                check(text() === original, "opening the text changes nothing")
                const item = window.menuItemNamed(panelAt(1).menu, "propertyEditAsText")
                check(item === null || !item.visible, "editable rows do not offer it")
            },
            () => {
                if (probe)
                    probe.destroy()
                probe = null
                editor.setPropertiesExpanded(false)
                page.path = host.samplePath
            }
        ]
    }

    Component.onCompleted: window.runSteps(steps())
}
