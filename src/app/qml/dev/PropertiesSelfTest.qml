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
// suggestions (another page's key) with its value typed at once; and on a page without
// properties "Dodaj właściwość" (Ctrl+;) — the draft above the blocks writes the frontmatter. On a
// phone the rows stack the key over the value and every control is a 44 px target.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property var events: null
    property Item frontmatter: null
    property string original: ""
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
    function focused(item) { return item !== null && item.activeFocus }
    /// Type `s` into `item` with real key events (it has the keyboard).
    function type(item, s) {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(item, 0, 0, s.charAt(i))
    }
    function key(item, k, modifiers) { EditorInput.sendKey(item, k, modifiers || 0, "") }
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
                check(p.stacked === (Theme.compact || p.width < 480), "the layout follows the width: stacked " + p.stacked)
                if (Theme.touchUi) {
                    check(p.stacked, "a phone stacks the key over the value")
                    const button = inRow("status", "propertyTypeButton")
                    check(button.height >= Theme.touchTarget && button.width >= Theme.touchTarget,
                          "the type icon is a 44 px target: " + button.width + "×" + button.height)
                    check(row("status").height >= 2 * Theme.touchTarget, "a stacked row holds the key and the value")
                    check(window.findItem(p, "propertiesAdd").height >= Theme.touchTarget, "“Dodaj właściwość” is a 44 px target")
                }
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
            // ---- a page without properties: the draft above the blocks ----------------------------------------
            () => page.path = "Bez właściwości.md",
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
            () => {
                editor.setPropertiesExpanded(false)
                page.path = host.samplePath
            }
        ]
    }

    Component.onCompleted: window.runSteps(steps())
}
