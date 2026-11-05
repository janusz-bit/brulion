pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// PropertiesSelfTest — the page properties panel with real input, in the editor playground
// (`brulion --editor --editor-script propstest`; ctest tst_editor_qml_properties, desktop, and
// tst_editor_qml_properties_touch, a phone with --touch): the folded panel's preview, its header
// tapped open (and staying open through edits: the delegate is not made again); a text value
// typed with real keys ("Zażółć gęślą jaźń") — only that line of the file changes, its comment
// kept —; a number typed "3,5" (written 3.5), "abc" refused with the field's error and the file
// untouched, Ctrl+Z; the checkbox toggled; a date from the dialog's natural language ("jutro");
// a list item added with Enter and taken away with Backspace in the empty field, a chip's × (on a
// phone the chip's menu — a tap on a plain item, a press and hold, never the row's menu —; the
// "+" never alone at the start of a line, the list's width swept px by px);
// renaming through the row's menu (an existing name refused); a type changed in the menu's "Typ"
// (text → list); a removal undone from the toast's "Cofnij"; a property added from the key
// suggestions (another page's key) with its value typed at once — a key the page has is refused,
// its value kept —; a field keeps what is typed while the notebook changes, and writes it before
// another page is shown; on a page without properties "Dodaj właściwość" (Ctrl+;) — the draft
// above the blocks writes the frontmatter, also when only a hidden note-mode frontmatter and
// `key::` lines are there; values opened and left unchanged keep their form (`|` text, `1.50`);
// a Logseq text becomes a list of links; an alias no link can use is refused under the field and
// in a toast; a map's row menu offers "Edytuj jako tekst". The key stays beside the value (phones
// too) — a narrow window, or a key too long for its column ("ostatnia aktualizacja" on a phone),
// stacks it over the value, so no key is cut —, its whole cell a tap area, and on a phone every
// control and tap area is a 44 px target.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

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
    /// A point mapped to another item (read through a typed property).
    property point mapped
    readonly property list<string> longKeys: ["data utworzenia", "ostatnia aktualizacja", "odpowiedzialna osoba"]
    readonly property string path: "Projekty/Remont garażu.md"
    readonly property string longKeysPath: "Długie klucze.md"
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
    function check(ok: bool, what: string): void { window.check(ok, what) }
    function text(): string { return window.pageText() }
    function splitText(t: string, separator: string): list<string> { return Js.split(t, separator) }
    function line(prefix: string): string { return window.fileLineOf(text(), prefix) }
    function delegateAt(r: int): BlockDelegate { return window.delegateAt(r) }
    function findItem(item: Item, name: string): Item { return window.findItem(item, name) }
    function list(): ListView { return editor.listView }
    function dialogs(): EditorDialogs { return editor.dialogs as EditorDialogs }
    function dateDialog(): BDialog { return dialogs().dateDialog }
    function datePicker(): BDatePicker { return DevTest.objectProperty(dateDialog(), "picker") as BDatePicker }
    function view(): FrontmatterView { return findItem(delegateAt(0), "frontmatterView") as FrontmatterView }
    function panel(): PropertiesPanel { return findItem(view(), "propertiesPanel") as PropertiesPanel }
    function row(key: string): PropertyRow { return findItem(panel(), "propertyRow_" + key) as PropertyRow }
    function inRow(key: string, name: string): Item { return findItem(row(key), name) }
    function fieldIn(key: string, name: string): BTextField { return inRow(key, name) as BTextField }
    /// The same for the properties block at row `blockRow` (a page with YAML and `key::` lines).
    function panelAt(blockRow: int): PropertiesPanel {
        return findItem(findItem(delegateAt(blockRow), "frontmatterView"), "propertiesPanel") as PropertiesPanel
    }
    function inRowAt(blockRow: int, key: string, name: string): Item {
        return findItem(findItem(panelAt(blockRow), "propertyRow_" + key), name)
    }
    function fieldInAt(blockRow: int, key: string, name: string): BTextField { return inRowAt(blockRow, key, name) as BTextField }
    function panelMenu(): PropertyMenu { return panel().menu }
    function panelMenuAt(blockRow: int): PropertyMenu { return panelAt(blockRow).menu }
    function menuOpenAt(blockRow: int): bool {
        const p = panelAt(blockRow)
        return p !== null && panelMenuAt(blockRow) !== null && panelMenuAt(blockRow).opened
    }
    function typeMenuOf(m: PropertyMenu): T.Menu { return m.typeMenu }
    function menuItemIn(m: T.Menu, name: string): T.MenuItem { return window.menuItemNamed(m, name) }
    /// The "Typ" item of the row menu `m`.
    function typeItem(m: T.Menu): T.MenuItem {
        for (let i = 0; i < m.count; ++i) {
            const it = m.itemAt(i) as T.MenuItem
            if (it && it.subMenu && it.subMenu.objectName === "propertyTypeMenu")
                return it
        }
        return null
    }
    function focused(item: Item): bool { return item !== null && item.activeFocus }
    /// Type `s` into `item` with real key events (it has the keyboard).
    function type(item: Item, s: string): void {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(item, 0, 0, s[i])
    }
    function key(item: Item, k: int, modifiers: int): void { EditorInput.sendKey(item, k, modifiers, "") }
    /// The keyboard to a text input outside the editor (a tap elsewhere): fields lose the focus.
    property Item probe: null
    property Component probeComponent: Component {
        TextInput { width: 10; height: 10 }
    }
    function focusElsewhere(): void {
        if (!probe)
            probe = probeComponent.createObject(window.contentItem) as Item
        probe.forceActiveFocus()
    }
    /// The file differs from `before` only in the lines starting with "status:".
    readonly property list<string> statusLines: ["status:"]
    function onlyStatusChanged(before: string): bool {
        return DevTest.sameWithoutLines(before, text(), test.statusLines)
    }
    /// The item of the row's menu named `name`.
    function menuItem(name: string): T.MenuItem {
        const p = panel()
        return p && panelMenu() ? menuItemIn(panelMenu(), name) : null
    }
    function menuOpen(): bool {
        const p = panel()
        return p !== null && panelMenu() !== null && panelMenu().opened
    }
    function listValue(key: string): PropertyListValue { return inRow(key, "propertyListValue") as PropertyListValue }
    function chipMenuOf(key: string): PropertyChipMenu { return listValue(key).chipMenu }
    /// The chip menu of the list `key` is open.
    function chipMenuOpen(key: string): bool {
        const list = listValue(key)
        return list !== null && chipMenuOf(key) !== null && chipMenuOf(key).opened
    }
    /// Steps: the row's menu from its type icon (a tap or a click).
    function openMenu(key: string): void {
        if (window.at())
            window.tapItem(inRow(key, "propertyTypeButton"))
        if (window.at())
            window.waitStep(menuOpen(), "the type icon of “" + key + "” opens its menu")
    }
    function choose(name: string): void {
        if (window.at()) {
            check(menuItem(name) !== null && menuItem(name).enabled, "the menu has " + name)
            window.tapItem(menuItem(name))
        }
        if (window.at())
            window.waitStep(!menuOpen(), "choosing " + name + " closes the menu")
    }
    /// The page's properties (the frontmatter view's) have the key `key`.
    function hasKey(key: string): bool {
        for (let i = 0; i < DevTest.lengthAt(view().properties, ""); ++i) {
            if (DevTest.valueAt(view().properties, i + ".key") === key)
                return true
        }
        return false
    }
    /// The keys among `keys` whose label is cut, joined by ", ".
    function cutKeys(): string {
        let cut = ""
        for (let i = 0; i < test.longKeys.length; ++i) {
            const key = test.longKeys[i]
            if (keyCut(key))
                cut += (cut === "" ? "" : ", ") + key
        }
        return cut
    }
    function keyCut(key: string): bool { return (inRow(key, "propertyKey") as Text).truncated }
    /// The panel's keys (every property's) whose label is cut, joined by ", ".
    function cutPanelKeys(): string {
        let cut = ""
        const p = panel()
        for (let i = 0; i < DevTest.lengthAt(p.properties, ""); ++i) {
            const key = "" + DevTest.valueAt(p.properties, i + ".key")
            if ((inRow(key, "propertyKey") as Text).truncated)
                cut += (cut === "" ? "" : ", ") + key
        }
        return cut
    }
    function mappedY(item: Item, target: Item): real {
        test.mapped = item.mapToItem(target, 0, 0)
        return test.mapped.y
    }
    function mappedX(item: Item, target: Item): real {
        test.mapped = item.mapToItem(target, 0, 0)
        return test.mapped.x
    }
    function stackedLayout(p: PropertiesPanel): bool {
        return p.widestKey + 12 > p.width * 0.4 || p.width - p.iconWidth - p.keyWidth - Theme.space2 < 168
    }
    /// The panel's layout: stacked or not as the keys need, every key whole, the value beside it,
    /// the key's cell a tap area (44 px targets on touch UIs).
    function checkLayout(): void {
        const p = panel()
        check(p.stacked === stackedLayout(p), "the layout follows the keys and the value column's width: stacked " + p.stacked)
        const cut = cutPanelKeys()
        check(cut === "", "beside the values every key is whole: " + cut)
        check(!p.stacked && mappedX(inRow("status", "propertyValue"), row("status")) >= p.iconWidth + p.keyWidth,
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
            check(findItem(p, "propertiesAdd").height >= Theme.touchTarget, "“Dodaj właściwość” is a 44 px target")
        }
        check(Math.abs(row("status").height - p.rowHeight) < 0.5, "a text row is the row's height: "
              + row("status").height + " / " + p.rowHeight)
    }
    /// A list's rows: whole chip lines on the row's grid, the chips' and "+"'s targets.
    function checkRhythm(key: string): void {
        const p = panel()
        const r = row(key)
        const line = p.rowHeight - 2 * r.valuePadding
        const lines = (r.height - 2 * r.valuePadding + Theme.space1) / (line + Theme.space1)
        check(line > 0 && lines >= 1 && Math.abs(lines - Math.round(lines)) < 0.01,
              key + ": whole chip lines on the row's grid: " + r.height + " px, padding " + r.valuePadding)
        if (Theme.touchUi) {
            check(r.valuePadding === 0, key + ": on touch UIs the chips' 44-px line is the row's height")
            const add = inRow(key, "propertyListAdd")
            check(add !== null && add.height >= Theme.touchTarget && add.width >= Theme.touchTarget,
                  key + ": “+” stays a 44 px target")
            for (let i = 0; inRow(key, "propertyChip_" + i) !== null; ++i) {
                const chip = inRow(key, "propertyChip_" + i) as BChip
                check(chip.height >= Theme.touchTarget && chip.width >= Theme.touchTarget && chip.interactive
                      && !chip.removable, key + ": chip " + i + " is a 44 × 44 target without an ×: "
                      + chip.width + "×" + chip.height)
            }
        } else {
            check(Math.abs(r.height - p.rowHeight) < 0.5, key + ": one chip line, the row's height: "
                  + r.height + " / " + p.rowHeight)
            check((inRow(key, "propertyChip_0") as BChip).removable, key + ": with a mouse a chip has its ×")
        }
    }
    /// The place of `item` in a list placement (PropertyListValue.placementAt()), in placeX/placeY.
    property real placeX: 0
    property real placeY: 0
    property var placement
    function placeOf(item: Item): bool {
        const at = DevTest.indexWithObject(DevTest.valueAt(test.placement, "items"), "item", item)
        test.placeX = Js.toNumber("" + DevTest.valueAt(test.placement, "items." + at + ".x"))
        test.placeY = Js.toNumber("" + DevTest.valueAt(test.placement, "items." + at + ".y"))
        return at >= 0
    }
    function itemsOf(list: PropertyListValue): list<string> { return list.items }
    /// The "+" never starts a line alone: at every width the chip before it comes down with it
    /// (when both fit) — the list's places for its width swept px by px.
    function checkPlacement(key: string): void {
        const list = listValue(key)
        const add = inRow(key, "propertyListAdd")
        const count = itemsOf(list).length
        const last = inRow(key, "propertyChip_" + (count - 1))
        const first = inRow(key, "propertyChip_0")
        test.placement = list.placementAt(list.width)
        placeOf(add)
        const addOk = Math.abs(test.placeX - add.x) < 0.5 && Math.abs(test.placeY - add.y) < 0.5
        placeOf(last)
        const lastOk = Math.abs(test.placeX - last.x) < 0.5
        const heightOk = Math.abs(Js.toNumber("" + DevTest.valueAt(test.placement, "height")) - list.implicitHeight) < 0.5
        check(addOk && lastOk && heightOk, key + ": the items stand where the list places them")
        let widest = 0
        let all = add.width
        for (let i = 0; i < count; ++i) {
            const c = inRow(key, "propertyChip_" + i)
            widest = Math.max(widest, c.width)
            all += c.width + Theme.space1
        }
        let pulled = 0
        let wrong = ""
        for (let w = Math.ceil(widest + Theme.space1 + add.width); w <= all + 2; ++w) {
            test.placement = list.placementAt(w)
            placeOf(add)
            const ax = test.placeX
            const ay = test.placeY
            placeOf(last)
            const lx = test.placeX
            const ly = test.placeY
            placeOf(first)
            const fy = test.placeY
            const sameLine = Math.abs(ly - ay) < 0.5
            if (ax < 0.5 && ay > fy && lx > 0.5 && last.width + Theme.space1 + add.width <= w)
                wrong += (wrong === "" ? "" : ", ") + w
            if (!sameLine && ax > 0.5)
                wrong += (wrong === "" ? "" : ", ") + w
            if (ax + add.width > w + 0.5 || lx + last.width > w + 0.5)
                wrong += (wrong === "" ? "" : ", ") + w
            if (count > 1 && sameLine && lx < 0.5 && ly > fy)
                pulled += 1
        }
        check(wrong === "", key + ": “+” never alone at the start of a line, nothing past the width: " + wrong)
        check(count < 2 || pulled > 0, key + ": the last chip came down with “+” where it would wrap alone ("
              + pulled + " widths)")
    }
    /// The add field of the panel, and the draft's above the blocks.
    function keyField(): BSuggestField { return findItem(panel(), "propertiesKeyField") as BSuggestField }
    function listHeader(): Item { return list().headerItem }
    function draftKeyField(): BSuggestField { return findItem(listHeader(), "propertiesKeyField") as BSuggestField }
    /// Another page's key is the add field's one suggestion.
    function keySuggested(): bool {
        const f = keyField()
        return f !== null && f.listShown && DevTest.lengthAt(f, "suggestions") === 1 && DevTest.valueAt(f, "suggestions.0.title") === "źródło"
    }
    /// The suggestions sit right below (or above) their field.
    function suggestionsFollow(): bool {
        const f = keyField()
        const p = f ? f.suggestionPopup : null
        if (!p || !p.opened)
            return false
        const top = mappedY(f, p.parent) + f.topInset
        test.mapped = f.mapToItem(p.parent, 0, f.height)
        const bottom = test.mapped.y - f.bottomInset
        return Math.abs(p.y - (bottom + 4)) <= 1 || Math.abs(p.y + p.height - (top - 4)) <= 1
    }

    function steps(): void {
        if (window.at()) {
            host.setFile(path, content)
            host.setFile("Ogród.md", "---\nźródło: Wikipedia\nstatus: gotowe\n---\n- grządki\n")
            host.setFile("Bez właściwości.md", "- pierwszy\n- drugi\n")
            host.setFile(notePath, noteContent)
            host.setFile(hiddenPath, hiddenContent)
            editor.setPropertiesExpanded(false)
            page.path = path
        }
        if (window.at()) window.waitStep(page.loaded && page.path === path && view() !== null && hasKey("koszt"),
                                         "the page with properties is open")
        // ---- folded: the preview; the header's tap opens the panel --------------------------------
        if (window.at()) {
            original = text()
            const preview = findItem(view(), "propertiesPreview") as Text
            check(preview !== null && preview.visible && Js.indexOf(preview.text, "status: w toku") === 0
                  && Js.indexOf(preview.text, "koszt: 1200") > 0, "folded, the header previews the values: "
                  + (preview ? preview.text : "-"))
            check(panel() === null, "folded: no panel made")
            window.tapItem(findItem(view(), "propertiesHeader"))
        }
        if (window.at()) window.waitStep(editor.propertiesExpanded && panel() !== null && row("status") !== null, "a tap on the header opens the panel")
        if (window.at()) {
            frontmatter = delegateAt(0)
            checkLayout()
            // ---- one row rhythm: a list's chip line is centred in the row's height as a text
            //      line is — one line is the row's height (touch 44, mouse 32), more lines whole
            //      chip lines apart (touch 44 / 92) —; the chips and "+" keep their targets.
            checkRhythm("tags")
            checkRhythm("autorzy")
            // ---- the "+" never starts a line alone: at every width the chip before it comes down
            //      with it (when both fit) — the list's places for its width swept px by px.
            checkPlacement("tags")
            checkPlacement("autorzy")
            // ---- a narrow window: the key over the value -----------------------------------------
            widthBefore = window.width
            window.width = 300
        }
        if (window.at()) window.waitStep(panel() !== null && panel().stacked, "a narrow panel stacks the key over the value")
        if (window.at()) {
            const r = row("status")
            check(mappedY(inRow("status", "propertyValue"), r) >= r.keyLineHeight,
                  "a stacked row holds the key over the value")
            if (Theme.touchUi)
                check(r.keyLineHeight >= Theme.touchTarget && inRow("status", "propertyKeyCell").height >= Theme.touchTarget,
                      "the stacked key's line is a 44 px target")
            window.width = widthBefore
        }
        if (window.at()) window.waitStep(panel() !== null && !panel().stacked, "wide again: the key beside the value")
        if (window.at()) {
            // ---- text: real keys, only that line changes -----------------------------------------
            window.tapItem(inRow("status", "propertyValue"))
        }
        if (window.at()) window.waitStep(focused(inRow("status", "propertyValueField")), "a tap on the text opens its field")
        if (window.at()) {
            const field = fieldIn("status", "propertyValueField")
            field.selectAll()
            type(field, "Zażółć gęślą jaźń")
            check(field.text === "Zażółć gęślą jaźń", "typed into the field: " + field.text)
            key(field, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(line("status:") === "status: Zażółć gęślą jaźń # wybrany"))
            window.checkWait("Enter writes the text, the comment kept: " + line("status:"))
        if (window.at()) {
            check(onlyStatusChanged(original), "every other line of the file is as it was")
            check(window.delegateAt(0) === frontmatter && editor.propertiesExpanded,
                  "the panel stays open: its block was not made again")
            check(inRow("status", "propertyValueField") === null, "the field closed")
            // ---- number ----------------------------------------------------------------------------
            window.tapItem(inRow("koszt", "propertyValue"))
        }
        if (window.at()) window.waitStep(focused(inRow("koszt", "propertyNumberField")), "a tap on the number opens its field")
        if (window.at()) {
            const field = fieldIn("koszt", "propertyNumberField")
            field.selectAll()
            type(field, "3,5")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(line("koszt:") === "koszt: 3.5"))
            window.checkWait("“3,5” is written as 3.5: " + line("koszt:"))
        if (window.at()) {
            check(focused(row("koszt")), "the row has the keyboard after Enter")
            key(row("koszt"), Qt.Key_Z, Qt.ControlModifier)
        }
        if (window.at() && !window.waiting(line("koszt:") === "koszt: 1200"))
            window.checkWait("Ctrl+Z in the row undoes it: " + line("koszt:"))
        if (window.at()) window.tapItem(inRow("koszt", "propertyValue"))
        if (window.at()) window.waitStep(focused(inRow("koszt", "propertyNumberField")), "the number's field again")
        if (window.at()) {
            const field = fieldIn("koszt", "propertyNumberField")
            field.selectAll()
            type(field, "abc")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at()) {
            const field = fieldIn("koszt", "propertyNumberField")
            check(field !== null && field.errorText !== "", "“abc” is refused in the field: " + (field ? field.errorText : "closed"))
            check(line("koszt:") === "koszt: 1200", "the file is untouched")
            key(field, Qt.Key_Escape, 0)
        }
        if (window.at()) window.waitStep(inRow("koszt", "propertyNumberField") === null, "Escape closes the field")
        // ---- checkbox ----------------------------------------------------------------------------------
        if (window.at()) window.tapItem(inRow("gotowe", "propertyCheckBox"))
        if (window.at() && !window.waiting(line("gotowe:") === "gotowe: true"))
            window.checkWait("the checkbox writes true: " + line("gotowe:"))
        // ---- date: the dialog, natural language ---------------------------------------------------------
        if (window.at()) window.tapItem(inRow("termin", "propertyDateButton"))
        if (window.at()) window.waitStep(dateDialog() !== null && dateDialog().opened, "the date opens the date dialog")
        if (window.at()) {
            const dialog = dateDialog()
            check(dialog.title === "termin" && datePicker().selectedDate === "2026-10-28" && !datePicker().withTime,
                  "titled with the key, on its date: " + dialog.title + " " + datePicker().selectedDate)
            datePicker().naturalText = "jutro"
            dialog.tryAccept()
        }
        if (window.at()) window.waitStep(!dateDialog().opened, "the dialog closes")
        if (window.at()) check(line("termin:") === "termin: 2026-10-16", "“jutro” is written as a date: " + line("termin:"))
        // ---- list: Enter adds, Backspace in the empty field takes the last, × removes --------------
        if (window.at()) window.tapItem(inRow("autorzy", "propertyListAdd"))
        if (window.at()) window.waitStep(focused(inRow("autorzy", "propertyListField")), "“+” opens the list's field")
        if (window.at()) {
            const field = fieldIn("autorzy", "propertyListField")
            type(field, "Łucja")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(Js.indexOf(text(), "autorzy:\n  - \"[[Jan Kowalski]]\"\n  - Anna\n  - Łucja\n") > 0))
            window.checkWait("Enter adds the item, the others as written: " + splitText(text(), "---")[1])
        if (window.at()) {
            const field = fieldIn("autorzy", "propertyListField")
            check(focused(field) && field.text === "", "the field stays open, empty, for the next one")
            key(field, Qt.Key_Backspace, 0)
        }
        if (window.at()) window.waitStep(Js.indexOf(text(), "autorzy:\n  - \"[[Jan Kowalski]]\"\n  - Anna\n---") > 0, "Backspace in the empty field removes the last item")
        if (window.touchRun) {
            // Touch UIs: no ×; a tap on a chip that opens nothing opens its menu, a press and
            // hold on any chip too — not the row's menu —, and "Usuń" there removes it (a toast
            // with "Cofnij").
            if (window.at()) {
                key(inRow("autorzy", "propertyListField"), Qt.Key_Escape, 0)
                window.tapItem(inRow("autorzy", "propertyChip_1"))
            }
            if (window.at()) window.waitStep(chipMenuOpen("autorzy"), "a tap on a plain item opens its menu")
            if (window.at()) {
                const m = chipMenuOf("autorzy")
                check(menuItemIn(m, "propertyChipCopy").visible
                      && !menuItemIn(m, "propertyChipOpen").visible, "a plain item: Kopiuj, no Otwórz")
                m.close()
            }
            if (window.at()) window.waitStep(!chipMenuOpen("autorzy"), "the menu closes")
            window.menuGesture(inRow("autorzy", "propertyChip_1"), chipMenuOpen("autorzy"))
            if (window.at()) {
                check(chipMenuOpen("autorzy") && !menuOpen(), "a press and hold opens the chip's menu, not the row's")
                const item = menuItemIn(chipMenuOf("autorzy"), "propertyChipRemove")
                check(item !== null && item.visible && Js.indexOf(item.text, "Anna") >= 0, "the menu removes “Anna”: "
                      + (item ? item.text : "-"))
                window.tapItem(item)
            }
            if (window.at()) window.waitStep(Js.indexOf(text(), "autorzy:\n  - \"[[Jan Kowalski]]\"\n---") > 0, "“Usuń” in the chip's menu removes an item")
            if (window.at()) check(window.lastNoticeAction !== null && Js.indexOf(window.lastNotice, "Anna") >= 0,
                        "a toast says it, with “Cofnij”: " + window.lastNotice)
            window.menuGesture(inRow("tags", "propertyChip_0"), chipMenuOpen("tags"))
            if (window.at()) {
                check(!menuOpen(), "a held tag opens its own menu, not the row's")
                const m = chipMenuOf("tags")
                check(menuItemIn(m, "propertyChipOpen").visible && menuItemIn(m, "propertyChipRemove").visible,
                      "a tag's menu: its page and Usuń")
                m.close()
            }
            if (window.at()) window.waitStep(!chipMenuOpen("tags"), "the tag's menu closes")
        } else {
            if (window.at()) {
                key(inRow("autorzy", "propertyListField"), Qt.Key_Escape, 0)
                const chip = inRow("autorzy", "propertyChip_1") as BChip
                check(chip !== null && chip.removable, "the chips can be removed")
                chip.removeClicked()
            }
            if (window.at()) window.waitStep(Js.indexOf(text(), "autorzy:\n  - \"[[Jan Kowalski]]\"\n---") > 0, "× removes an item")
        }
        if (window.at()) page.undo()
        // ---- rename through the menu ----------------------------------------------------------------
        openMenu("koszt")
        choose("propertyRename")
        if (window.at()) window.waitStep(focused(inRow("koszt", "propertyRenameField")), "“Zmień nazwę” opens the key's field")
        if (window.at()) {
            const field = fieldIn("koszt", "propertyRenameField")
            field.selectAll()
            type(field, "status")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at()) {
            const field = fieldIn("koszt", "propertyRenameField")
            check(field !== null && field.errorText !== "", "an existing name is refused: " + (field ? field.errorText : "-"))
            field.selectAll()
            type(field, "budżet")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(line("budżet:") === "budżet: 1200" && line("koszt:") === ""))
            window.checkWait("renamed: " + line("budżet:"))
        // ---- the type menu: text → list -----------------------------------------------------------------
        openMenu("status")
        if (window.at()) {
            const sub = typeItem(panelMenu())
            check(sub !== null && sub.enabled, "the menu has “Typ”")
            window.tapItem(sub)
        }
        if (window.at()) window.waitStep(typeMenuOf(panelMenu()).opened, "“Typ” opens its submenu")
        if (window.at()) {
            const item = menuItemIn(typeMenuOf(panelMenu()), "propertyType_list")
            check(item !== null && !item.checked, "“Lista” is there, not the current type")
            window.tapItem(item)
        }
        if (window.at() && !window.waiting(Js.indexOf(text(), "status: # wybrany\n  - Zażółć gęślą jaźń\n") > 0))
            window.checkWait("the text becomes a list: " + splitText(text(), "---")[1])
        if (window.at()) page.undo()
        // ---- remove, "Cofnij" in the toast ----------------------------------------------------------------
        openMenu("gotowe")
        choose("propertyRemove")
        if (window.at()) window.waitStep(line("gotowe:") === "", "“Usuń” removes the property")
        if (window.at()) {
            check(window.lastNoticeAction !== null && Js.indexOf(window.lastNotice, "gotowe") >= 0,
                  "a toast with “Cofnij”: " + window.lastNotice)
            window.lastNoticeAction.run()
        }
        if (window.at()) window.waitStep(line("gotowe:") === "gotowe: true", "“Cofnij” brings it back")
        // ---- add a property from the suggestions -------------------------------------------------------
        if (window.at()) window.tapItem(findItem(panel(), "propertiesAdd"))
        if (window.at()) window.waitStep(focused(keyField()), "“Dodaj właściwość” opens the key's field")
        if (window.at()) type(keyField(), "zro")
        if (window.at()) window.waitStep(keySuggested(), "another page's key is suggested (diacritics ignored), not the page's own")
        // The field moves under its list (scrolled above the keyboard, a flick): the list follows.
        if (window.at()) {
            list().topMargin += 60
            list().contentY -= 60
        }
        if (window.at()) window.waitStep(suggestionsFollow(), "the suggestions follow their field when it moves")
        if (window.at()) {
            list().contentY += 60
            list().topMargin -= 60
        }
        if (window.at()) {
            const f = keyField()
            key(f, Qt.Key_Down, 0)
            key(f, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(line("źródło:") === "źródło:" && focused(inRow("źródło", "propertyValueField"))))
            window.checkWait("the key is written at once, its value has the keyboard: " + line("źródło:"))
        if (window.at()) {
            const field = fieldIn("źródło", "propertyValueField")
            type(field, "Wikipedia")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(line("źródło:") === "źródło: Wikipedia"))
            window.checkWait("the value: " + line("źródło:"))
        // ---- adding a key the page has: refused, its value kept ---------------------------------------------
        if (window.at()) {
            original = text()
            window.tapItem(findItem(panel(), "propertiesAdd"))
        }
        if (window.at()) window.waitStep(focused(keyField()), "the key's field again")
        if (window.at()) {
            const f = keyField()
            type(f, "status")
            key(f, Qt.Key_Return, 0)
        }
        if (window.at()) {
            const f = keyField()
            check(f !== null && focused(f) && f.errorText !== "",
                  "a key the page has is refused in the field: " + (f ? f.errorText : "closed"))
            check(text() === original, "its value is kept: " + line("status:"))
            f.selectAll()
            type(f, "TAGS")
            key(f, Qt.Key_Return, 0)
        }
        if (window.at()) {
            const f = keyField()
            check(f !== null && f.errorText !== "" && text() === original, "in any spelling: the tags are kept")
            key(f, Qt.Key_Escape, 0)
        }
        if (window.at()) window.waitStep(keyField() === null, "Escape closes the add field")
        // ---- a field keeps what is typed while the notebook changes ------------------------------------------
        if (window.at()) window.tapItem(inRow("status", "propertyValue"))
        if (window.at()) window.waitStep(focused(inRow("status", "propertyValueField")), "the text's field")
        if (window.at()) {
            const field = fieldIn("status", "propertyValueField")
            field.selectAll()
            type(field, "WYC")
            // Another page changes (sync, another pane): the notebook's index and the rows' roles.
            indexBefore = indexChanges
            host.externalWrite("Ogród.md", "---\nźródło: Wikipedia\nstatus: gotowe\netap: nowy\n---\n- grządki\n")
        }
        if (window.at()) window.waitStep(indexChanges > indexBefore && page.notebookPropertyType("etap") === "text", "the notebook knows the other page's change")
        if (window.at()) {
            const field = fieldIn("status", "propertyValueField")
            check(field !== null && focused(field) && field.text === "WYC",
                  "the field keeps what is typed: " + (field ? field.text : "closed"))
            key(field, Qt.Key_Escape, 0)
        }
        if (window.at()) window.waitStep(inRow("status", "propertyValueField") === null && text() === original, "Escape leaves the value as it was")
        // ---- another page shown while a field holds a value: it is written first --------------------------
        if (window.at()) window.tapItem(inRow("status", "propertyValue"))
        if (window.at()) window.waitStep(focused(inRow("status", "propertyValueField")), "the text's field once more")
        if (window.at()) {
            const field = fieldIn("status", "propertyValueField")
            field.selectAll()
            type(field, "WYCIEK")
            page.path = "Bez właściwości.md"
        }
        if (window.at()) window.waitStep(page.loaded && page.path === "Bez właściwości.md", "another page")
        if (window.at()) check(window.fileLineOf(host.file(path), "status:") === "status: WYCIEK # wybrany"
                    && host.file("Bez właściwości.md") === "- pierwszy\n- drugi\n",
                    "the typed value went to its page before the other one was shown: "
                    + window.fileLineOf(host.file(path), "status:"))
        // ---- a page without properties: the draft above the blocks ----------------------------------------
        if (window.at()) window.waitStep(page.loaded && page.path === "Bez właściwości.md" && page.pageProperties.visibleRow < 0, "a page without properties")
        if (window.at()) editor.addProperty()
        if (window.at()) window.waitStep(focused(draftKeyField()), "“Dodaj właściwość” (Ctrl+;) opens the draft above the blocks")
        if (window.at()) {
            const f = draftKeyField()
            type(f, "status")
            key(f, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(text() === "---\nstatus:\n---\n- pierwszy\n- drugi\n" && focused(inRow("status", "propertyValueField"))))
            window.checkWait("the first property writes the frontmatter, its value has the keyboard: " + Js.stringify(text()))
        if (window.at()) {
            check(!editor.propertyDraft && draftKeyField() === null,
                  "the draft is gone")
            key(inRow("status", "propertyValueField"), Qt.Key_Escape, 0)
            page.undo()
        }
        if (window.at()) window.waitStep(text() === "- pierwszy\n- drugi\n", "one undo step takes it back")
        // ---- a hidden note-mode frontmatter and shown `key::` lines: the draft adds to the frontmatter --
        if (window.at()) page.path = hiddenPath
        if (window.at()) window.waitStep(page.loaded && page.path === hiddenPath && page.pageProperties.visibleRow === 1
                   && page.pageProperties.addRow === -1, "a page with only `key::` lines shown")
        if (window.at()) editor.addProperty()
        if (window.at()) window.waitStep(focused(draftKeyField()), "“Dodaj właściwość” opens the draft (the frontmatter new keys go to is hidden)")
        if (window.at()) {
            const f = draftKeyField()
            type(f, "etap")
            key(f, Qt.Key_Return, 0)
        }
        if (window.at() && !window.waiting(text() === "---\nbrulion: document\netap:\n---\nstatus:: w toku\n- a\n"
                   && focused(inRowAt(0, "etap", "propertyValueField"))))
            window.checkWait("the key goes into the frontmatter, now shown; its value has the keyboard: " + Js.stringify(text()))
        if (window.at()) {
            check(editor.propertyFocus === null && !editor.propertyDraft, "no request is left over")
            key(inRowAt(0, "etap", "propertyValueField"), Qt.Key_Escape, 0)
            page.undo()
        }
        if (window.at()) window.waitStep(text() === hiddenContent, "one undo step takes it back")
        // ---- values left unchanged keep their form; a Logseq text as a list; a bad alias; a map --------
        if (window.at()) page.path = notePath
        if (window.at()) window.waitStep(page.loaded && page.path === notePath && inRowAt(0, "notatka", "propertyValue") !== null
                   && inRowAt(1, "ilosc", "propertyValue") !== null, "a page with both kinds of properties")
        if (window.at()) window.tapItem(inRowAt(0, "notatka", "propertyValue"))
        if (window.at()) window.waitStep(focused(inRowAt(0, "notatka", "propertyValueArea")), "a text of several lines opens a text area")
        if (window.at()) {
            const area = inRowAt(0, "notatka", "propertyValueArea") as TextEdit
            check(area.text === "linia 1\nlinia 2\n", "the area holds the text: " + Js.stringify(area.text))
            focusElsewhere()
        }
        if (window.at()) window.waitStep(inRowAt(0, "notatka", "propertyValueArea") === null, "leaving the area closes it")
        if (window.at()) {
            check(text() === noteContent, "left unchanged, the `|` text keeps its form: " + Js.stringify(text()))
            check(!page.canUndo, "no undo step")
            window.tapItem(inRowAt(1, "ilosc", "propertyValue"))
        }
        if (window.at()) window.waitStep(focused(inRowAt(1, "ilosc", "propertyNumberField")), "the `key::` number's field")
        if (window.at()) {
            const field = fieldInAt(1, "ilosc", "propertyNumberField")
            check(field.text === "1.50", "the field holds the number as written: " + field.text)
            key(field, Qt.Key_Return, 0)
        }
        if (window.at()) window.waitStep(inRowAt(1, "ilosc", "propertyNumberField") === null, "Enter closes the field")
        if (window.at()) {
            check(text() === noteContent, "Enter without a change keeps `ilosc:: 1.50`: " + Js.stringify(text()))
            window.tapItem(inRowAt(1, "autorzy", "propertyTypeButton"))
        }
        if (window.at()) window.waitStep(menuOpenAt(1), "the menu of `autorzy::`")
        if (window.at()) {
            const sub = typeItem(panelMenuAt(1))
            check(sub !== null && sub.enabled, "the menu has “Typ”")
            window.tapItem(sub)
        }
        if (window.at()) window.waitStep(typeMenuOf(panelMenuAt(1)).opened, "“Typ” opens its submenu")
        if (window.at()) window.tapItem(menuItemIn(typeMenuOf(panelMenuAt(1)), "propertyType_list"))
        if (window.at() && !window.waiting(line("autorzy::") === "autorzy:: [[a]], [[b]]"))
            window.checkWait("a Logseq text becomes a list of links: " + line("autorzy::"))
        if (window.at()) {
            check(inRowAt(1, "autorzy", "propertyChip_1") !== null, "shown as chips")
            window.tapItem(inRowAt(0, "aliases", "propertyListAdd"))
        }
        if (window.at()) window.waitStep(focused(inRowAt(0, "aliases", "propertyListField")), "the aliases' field")
        if (window.at()) {
            original = text()
            const field = fieldInAt(0, "aliases", "propertyListField")
            type(field, "a|b")
            key(field, Qt.Key_Return, 0)
        }
        if (window.at()) {
            const field = fieldInAt(0, "aliases", "propertyListField")
            check(field !== null && focused(field) && field.errorText !== "" && field.text === "a|b",
                  "an alias no link can use is refused under the field: " + (field ? field.errorText : "closed"))
            check(text() === original, "nothing written")
            window.lastNotice = ""
            focusElsewhere()
        }
        if (window.at() && !window.waiting(inRowAt(0, "aliases", "propertyListField") === null && window.lastNotice !== ""))
            window.checkWait("leaving the field closes it, a toast says why: " + window.lastNotice)
        if (window.at()) {
            check(text() === original, "still nothing written")
            window.tapItem(inRowAt(0, "meta", "propertyTypeButton"))
        }
        if (window.at()) window.waitStep(menuOpenAt(0), "the menu of a map's row")
        if (window.at()) {
            const item = menuItemIn(panelMenuAt(0), "propertyEditAsText")
            check(item !== null && item.visible && item.enabled, "a map's row menu offers “Edytuj jako tekst”")
            const rename = menuItemIn(panelMenuAt(0), "propertyRename")
            check(rename !== null && !rename.enabled, "renaming is not offered for it")
            window.tapItem(item)
        }
        if (window.at()) window.waitStep(page.editingRow === 0, "“Edytuj jako tekst” edits the frontmatter's text")
        if (window.at()) {
            page.stopEditing()
            check(text() === original, "opening the text changes nothing")
            const item = menuItemIn(panelMenuAt(1), "propertyEditAsText")
            check(item === null || !item.visible, "editable rows do not offer it")
        }
        // ---- long keys: stacked over their values, never cut ----------------------------------------
        if (window.at()) {
            host.setFile(longKeysPath, "---\ndata utworzenia: 2026-10-01\nostatnia aktualizacja: 2026-10-28\n"
                         + "odpowiedzialna osoba: Żaneta Łęcka\n---\n- treść\n")
            editor.setPropertiesExpanded(true)
            page.path = longKeysPath
        }
        if (window.at()) window.waitStep(page.loaded && page.path === longKeysPath && row("ostatnia aktualizacja") !== null, "a page with long keys")
        if (window.at()) {
            const p = panel()
            const cut = cutKeys()
            check(p.stacked || p.widestKey + 12 <= p.width * 0.4, "a key too wide for its column stacks the rows ("
                  + p.width + " px panel)")
            check(cut === "", "long keys are whole: " + cut)
            const r = row("odpowiedzialna osoba")
            check(!p.stacked || mappedY(inRow("odpowiedzialna osoba", "propertyValue"), r) >= r.keyLineHeight,
                  "stacked, the value is under its key")
        }
        if (window.at()) {
            if (probe)
                probe.destroy()
            probe = null
            editor.setPropertiesExpanded(false)
            page.path = host.samplePath
        }
    }

    Component.onCompleted: window.runSteps("propstest")
}
