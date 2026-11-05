pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// LinkPartsSelfTest — the path parts of a link to a page in a folder (docs/SYNTAX.md §2 "Path
// parts") with real input, in the editor playground (`brulion --editor --editor-script
// linkpartstest`; ctest tst_editor_qml_linkparts, desktop, and tst_editor_qml_linkparts_touch, a
// phone with --touch): [[Firma/Adam cośtam]] is drawn as two links with a plain separator; the
// mouse over a part underlines that part only (a pointing hand), over the separator nothing (an
// I-beam); a click on "Firma" sends the part's route, on "Adam cośtam" the page's, on "/" edits
// the block (its raw line unchanged); a link with a display text stays one link; a right-click or
// long press on a folder's part opens LinkMenu with "Pokaż folder" / "Kopiuj ścieżkę folderu"
// (the clipboard read back), on a page's "Otwórz" / "Kopiuj link"; parts in a table cell, an
// embed, a query result and a page property; Alt+Enter on the edited raw line (a part, the page,
// the side panel with Shift, nothing outside a link — no line break typed), also in a table
// cell's field; with a (simulated) screen reader each link an accessible link item; on a phone
// the press underline, a long press's menu and a flick that opens nothing.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property int row: -1
    property int tagRow: -1
    property point at: Qt.point(0, 0)
    property string before: ""
    property int navigations: 0
    property int navigationsBefore: 0
    property bool lastNewPane: false
    property int rowsBefore: 0
    /// linkBox(): the box of the links found; boxes(): the part's and the page's.
    property rect box
    property rect partBox
    property rect pageBox
    /// exactLink(): the middle of the link found.
    property real linkCx: 0
    /// A point mapped into a text (read through a typed property).
    property point mapped
    readonly property string gesture: Theme.touchUi ? "a long press" : "a right-click"
    readonly property string path: "Części linków.md"
    readonly property string content: "---\n"
        + "osoba: \"[[Firma/Adam cośtam]]\"\n"
        + "---\n"
        + "- Zapytać się [[Firma/Adam cośtam]] o to czego się pouczyć\n"
        + "- [[Projekty/Zażółć gęślą jaźń/Notatki]]\n"
        + "- Z tekstem: [[Firma/Adam cośtam|Adam]]\n"
        + "- Etykieta #praca/raporty dziś\n"
        + "\n"
        + "| Kto | Gdzie |\n"
        + "| --- | --- |\n"
        + "| [[Firma/Adam cośtam]] | biuro |\n"
        + "\n"
        + "![[Strona z linkiem]]\n"
        + "\n"
        + "```query\n\"w osadzeniu\"\n```\n"
    readonly property string firstLine: "- Zapytać się [[Firma/Adam cośtam]] o to czego się pouczyć"
    readonly property string partUrl: "part:1:Firma%2FAdam%20co%C5%9Btam"
    readonly property string pageUrl: "page:Firma%2FAdam%20co%C5%9Btam"

    property Connections navigationWatch: Connections {
        target: test.host
        function onNavigated(route: var, newPane: bool) {
            test.navigations += 1
            test.lastNewPane = newPane
        }
    }

    function check(ok: bool, what: string): void { window.check(ok, what) }
    function info(r: int): DevBlock { return window.info(r) }
    function list(): ListView { return editor.listView }
    function linkMenu(): LinkMenu { return editor.linkMenu }
    function navigated(): bool { return navigations > navigationsBefore }
    function linkMenuOpen(): bool { return linkMenu() !== null && linkMenu().opened }
    function linkItem(name: string): T.MenuItem { return window.menuItemNamed(linkMenu(), name) }
    function delegateAt(r: int): BlockDelegate { return window.delegateAt(r) }
    /// The BlockText of `r` (its hoveredLink, shownHtml), or null.
    function blockText(r: int): BlockText {
        return window.findWith(delegateAt(r), window.blockTextMembers) as BlockText
    }
    /// The rendered rich text of `r` holding `needle` ("part:" for "").
    function textOf(r: int, needle: string): Text {
        return window.textContaining(delegateAt(r), needle !== "" ? needle : "part:") as Text
    }
    function partText(r: int): Text { return textOf(r, "") }
    /// The box of the links of `text` whose URL starts with `url`, sampled on a 2-px grid, into
    /// `box`; false when there is none.
    function linkBox(text: Text, url: string): bool {
        let x1 = 1e9
        let y1 = 1e9
        let x2 = -1
        let y2 = -1
        for (let y = 1; text && y < text.height; y += 2) {
            for (let x = 1; x < text.width; x += 2) {
                if (Js.indexOf(text.linkAt(x, y), url) === 0) {
                    x1 = Math.min(x1, x)
                    y1 = Math.min(y1, y)
                    x2 = Math.max(x2, x)
                    y2 = Math.max(y2, y)
                }
            }
        }
        if (x2 < 0)
            return false
        box = Qt.rect(x1, y1, x2 - x1, y2 - y1)
        return true
    }
    /// The part's ("part:1:") and the page's ("page:") boxes of `text` (partBox, pageBox).
    function boxes(text: Text): bool {
        const part = linkBox(text, "part:1:")
        partBox = box
        const page = linkBox(text, "page:")
        pageBox = box
        return part && page
    }
    function boxCx(): real { return box.x + box.width / 2 }
    function boxCy(): real { return box.y + box.height / 2 }
    /// The middle of the link of `text` whose URL is exactly `url`, on the height of `y` (linkCx);
    /// false when there is none.
    function exactLink(text: Text, url: string, y: real): bool {
        let x1 = -1
        let x2 = -1
        for (let x = 1; text && x < text.width; ++x) {
            if (text.linkAt(x, y) === url) {
                if (x1 < 0)
                    x1 = x
                x2 = x
            }
        }
        if (x2 < 0)
            return false
        linkCx = (x1 + x2) / 2
        return true
    }
    /// How many links of `html` are underlined.
    function underlined(html: string): int { return underlineParts(html).length - 1 }
    function underlineParts(html: string): list<string> { return window.splitText(html, "text-decoration:underline") }
    /// The URL of the underlined link of `html` ("" for none).
    function underlinedHref(html: string): string {
        return DevTest.underlinedHref(html)
    }
    function shownHtmlOf(r: int): string { return blockText(r).shownHtml }
    /// Only the part of row `row` is underlined.
    function partAloneUnderlined(): bool {
        const html = shownHtmlOf(row)
        if (underlined(html) !== 1)
            return false
        return underlinedHref(html) === partUrl
    }
    function mark(): void { navigationsBefore = navigations }
    /// The first row whose raw text holds `needle` (or of the view `view`), -1 when none.
    function rowWith(needle: string, view: string): int {
        for (let r = 0; r < page.blocks.count; ++r) {
            if (view !== "" ? info(r).view === view : Js.indexOf(info(r).editText, needle) >= 0)
                return r
        }
        return -1
    }
    /// A tap (touch) or a left click at the centre of the link of `text` starting with `url`.
    function clickLink(text: Text, url: string): void {
        const found = linkBox(text, url)
        check(found, "the link " + url + " is drawn")
        if (found)
            window.tapPoint(text, boxCx(), boxCy())
    }
    /// Steps: the part link of the rendered text of row `r` (a block, a cell, an embed, a result, a
    /// property) clicked or tapped: the part's route.
    function partClick(what: string, r: int): void {
        if (window.at())
            window.waitStep(partText(r) !== null && linkBox(partText(r), "part:1:"), "the part is drawn in " + what)
        if (window.at()) {
            mark()
            clickLink(partText(r), "part:1:")
        }
        if (window.at() && !window.waiting(partRouted()))
            window.checkWait("a part in " + what + " sends its route: " + Js.stringify(host.lastRoute))
    }
    function partRouted(): bool {
        return navigated() && host.lastRoute.linkPart === 1 && host.lastRoute.linkTarget === "Firma/Adam cośtam"
               && host.lastRoute.target === "Firma" && host.lastRoute.from === test.path
    }
    /// The rendered text of row `r` with its part drawn (`at`: the part's middle), or null.
    function findPart(r: int): Text {
        const t = partText(r)
        const found = t !== null && linkBox(t, "part:1:")
        check(found, "the part of row " + r + " is drawn")
        if (found)
            at = Qt.point(boxCx(), boxCy())
        return found ? t : null
    }
    /// Steps: the context gesture on the part of row `r`: a right-click, or a finger held until the
    /// menu shows, then lifted.
    function menuGesture(r: int): void {
        if (!Theme.touchUi) {
            if (window.at()) {
                const t = findPart(r)
                if (t)
                    window.taskEvents.mouseClick(t, at.x, at.y, Qt.RightButton, Qt.NoModifier, -1)
            }
            return
        }
        if (window.at()) {
            const t = findPart(r)
            if (t)
                window.taskEvents.touchPress(t, at.x, at.y)
        }
        if (window.at())
            window.waiting(linkMenuOpen())
        if (window.at()) {
            const t = partText(r)
            if (t)
                window.taskEvents.touchRelease(t, at.x, at.y)
        }
    }
    function edit(): InlineEditor { return editor.activeEditor }
    /// Steps: Alt(+Shift)+Enter with real keys while row 0 is edited with the caret `offset`
    /// characters after the start of `needle`.
    function altEnter(needle: string, offset: int, shift: bool): void {
        if (window.at())
            page.stopEditing()
        if (window.at())
            window.waitStep(page.editingRow < 0, "not editing")
        if (window.at())
            editor.startEditing(test.row, Js.indexOf(info(test.row).editText, needle) + offset)
        if (window.at())
            window.waitStep(edit() !== null && edit().row === test.row
                            && edit().cursorPosition === Js.indexOf(info(test.row).editText, needle) + offset,
                            "the block is edited with the caret in “" + needle + "”")
        if (window.at()) {
            mark()
            before = info(test.row).editText
            rowsBefore = page.blocks.count
            window.realKey(Qt.Key_Return, shift ? Qt.AltModifier | Qt.ShiftModifier : Qt.AltModifier, "")
        }
    }
    /// The table cell's field with the keyboard under `item`, or null.
    function focusedField(item: Item): TextInput {
        if (!item)
            return null
        if (item.activeFocus && DevTest.hasMember(item, "cursorPosition"))
            return item as TextInput
        for (let i = 0; i < window.childCount(item); ++i) {
            const found = focusedField(window.childAt(item, i))
            if (found)
                return found
        }
        return null
    }
    /// The screen reader's link items of row `r`: how many, the `i`-th.
    function linkItemCount(r: int): int { return window.countNamed(delegateAt(r), "linkTarget") }
    function linkItemAt(r: int, i: int): Item { return window.namedAt(delegateAt(r), "linkTarget", i) }
    /// The link item `it` lies over its link of `t`.
    function overItsLink(it: Item, t: Text): bool {
        test.mapped = it.mapToItem(t, it.width / 2, it.height / 2)
        return t.linkAt(test.mapped.x, test.mapped.y) === "" + DevTest.property(it, "href")
    }

    /// What tells a table view (TableBlockView) from the items around it.
    readonly property list<string> tableMembers: ["startCell"]

    function steps(): void {
        if (window.at()) {
            host.setFile("Firma.md", "- Strona firmy\n")
            host.setFile("Firma/Adam cośtam.md", "- Adam\n")
            host.setFile("Projekty/Zażółć gęślą jaźń/Notatki.md", "- notatki\n")
            host.setFile("Strona z linkiem.md", "- Zobacz [[Firma/Adam cośtam]] w osadzeniu\n")
            host.setFile(test.path, test.content)
            editor.setPropertiesExpanded(true)
            page.path = test.path
        }
        if (window.at()) window.waitStep(rowWith("Zapytać", "") >= 0 && textOf(rowWith("Zapytać", ""), "") !== null, "the page is shown")
        if (window.at()) {
            row = rowWith("Zapytać", "")
            list().positionViewAtIndex(row, ListView.Center)
            const t = textOf(row, "")
            check(Js.indexOf(t.text, "href=\"" + partUrl + "\"") >= 0 && Js.indexOf(t.text, "href=\"" + pageUrl + "\"") >= 0,
                  "[[Firma/Adam cośtam]] is drawn as a part and the page")
            const both = boxes(t)
            check(both && partBox.x + partBox.width < pageBox.x
                  && Math.abs(partBox.y + partBox.height / 2 - (pageBox.y + pageBox.height / 2)) < 4,
                  "the part left of the page on one line")
            // Between them the separator: no link.
            check(both && t.linkAt((partBox.x + partBox.width + pageBox.x) / 2, partBox.y + partBox.height / 2) === "",
                  "the separator is no link")
        }
        // ---- 1. hover (desktop): the part under the mouse underlined, alone ------------------
        if (window.at()) {
            if (Theme.touchUi)
                return
            const t = textOf(row, "")
            linkBox(t, "part:1:")
            window.taskEvents.mouseMove(t, boxCx(), boxCy(), -1, Qt.NoButton, Qt.NoModifier)
        }
        if (window.at()) window.waitStep(Theme.touchUi || (blockText(row).hoveredLink === partUrl), "the mouse over “Firma”: its link hovered")
        if (window.at()) {
            if (Theme.touchUi)
                return
            check(partAloneUnderlined(), "only the part is underlined: " + underlinedHref(blockText(row).shownHtml))
            const t = textOf(row, "")
            boxes(t)
            window.taskEvents.mouseMove(t, (partBox.x + partBox.width + pageBox.x) / 2, partBox.y + partBox.height / 2, -1,
                                        Qt.NoButton, Qt.NoModifier)
        }
        if (window.at()) window.waitStep(Theme.touchUi || blockText(row).hoveredLink === "", "over the separator nothing is hovered")
        if (window.at()) {
            if (Theme.touchUi)
                return
            check(underlined(blockText(row).shownHtml) === 0, "and nothing underlined")
            const t = textOf(row, "")
            linkBox(t, "page:")
            window.taskEvents.mouseMove(t, boxCx(), boxCy(), -1, Qt.NoButton, Qt.NoModifier)
        }
        if (window.at()) window.waitStep(Theme.touchUi || blockText(row).hoveredLink === pageUrl, "over “Adam cośtam” the page link")
        // ---- 2. clicks: the part, the page, the separator ---------------------------------------
        if (window.at()) {
            mark()
            clickLink(textOf(row, ""), "part:1:")
        }
        if (window.at() && !window.waiting(navigated() && host.lastRoute.linkPart === 1 && host.lastRoute.linkTarget === "Firma/Adam cośtam"
                   && host.lastRoute.target === "Firma" && !lastNewPane))
            window.checkWait((Theme.touchUi ? "a tap" : "a click") + " on “Firma” sends the part's route: " + Js.stringify(host.lastRoute))
        if (window.at()) {
            check(page.editingRow < 0, "the block is not edited")
            mark()
            clickLink(textOf(row, ""), "page:")
        }
        if (window.at() && !window.waiting(navigated() && host.lastRoute.path === "Firma/Adam cośtam.md" && host.lastRoute.linkPart === undefined))
            window.checkWait("“Adam cośtam” opens the page: " + Js.stringify(host.lastRoute))
        if (window.at()) {
            mark()
            const t = textOf(row, "")
            boxes(t)
            window.tapPoint(t, (partBox.x + partBox.width + pageBox.x) / 2, partBox.y + partBox.height / 2)
        }
        if (window.at()) window.waitStep(page.editingRow === row, "a click on “/” edits the block")
        if (window.at()) {
            check(!navigated(), "and opens nothing")
            check(info(row).editText === "Zapytać się [[Firma/Adam cośtam]] o to czego się pouczyć",
                  "the raw line as written: " + info(row).editText)
            page.stopEditing()
        }
        if (window.at()) window.waitStep(page.editingRow < 0 && textOf(row, "") !== null, "editing ends")
        if (window.at()) check(window.fileLineOf(window.pageText(), "Zapytać") === firstLine, "the file is unchanged")
        // ---- 2b. a nested tag: one chip, its parent a link of its own ---------------------------------
        if (window.at()) {
            tagRow = rowWith("Etykieta", "")
            list().positionViewAtIndex(tagRow, ListView.Center)
        }
        if (window.at()) window.waitStep(linkBox(textOf(tagRow, "tag:praca"), "tag:praca%2Fraporty"), "the nested tag is drawn")
        if (window.at()) {
            mark()
            const t = textOf(tagRow, "tag:praca")
            linkBox(t, "tag:praca%2Fraporty")
            const y = boxCy()
            const found = exactLink(t, "tag:praca", y)
            check(found, "“#praca” is a link of its own")
            if (found)
                window.tapPoint(t, linkCx, y)
        }
        if (window.at() && !window.waiting(navigated() && host.lastRoute.view === "tag" && host.lastRoute.tag === "praca"))
            window.checkWait("“#praca” opens the parent tag: " + Js.stringify(host.lastRoute))
        if (window.at()) {
            mark()
            clickLink(textOf(tagRow, "tag:praca"), "tag:praca%2Fraporty")
        }
        if (window.at() && !window.waiting(navigated() && host.lastRoute.view === "tag" && host.lastRoute.tag === "praca/raporty"))
            window.checkWait("“raporty” opens the whole tag: " + Js.stringify(host.lastRoute))
        // ---- 3. a display text: one link ------------------------------------------------------------
        if (window.at()) {
            const r = rowWith("Z tekstem", "")
            const t = textOf(r, "page:")
            check(t !== null && Js.indexOf(t.text, "part:") < 0, "a link with a display text has no parts")
            const found = t !== null && linkBox(t, "page:")
            check(found && Js.indexOf(t.linkAt(box.x + 1, boxCy()), "page:Firma%2FAdam") === 0
                  && Js.indexOf(t.linkAt(box.x + box.width - 1, boxCy()), "page:Firma%2FAdam") === 0,
                  "both of its halves are the page link")
        }
        // ---- 4. the menus of a folder's part and a page's part --------------------------------------
        if (window.at()) list().positionViewAtIndex(rowWith("[[Projekty/", ""), ListView.Center)
        menuGesture(rowWith("[[Projekty/", ""))
        if (window.at()) window.waitStep(linkMenuOpen(), gesture + " on “Projekty” opens the link menu")
        if (window.at()) {
            const showFolder = qsTranslate("LinkMenu", "Show folder")
            const copyPath = qsTranslate("LinkMenu", "Copy folder path")
            const open = linkItem("linkMenuOpen")
            const copy = linkItem("linkMenuCopy")
            check(linkMenu().folder && open.text === showFolder, "“Pokaż folder”: " + open.text)
            check(copy.text === copyPath, "“Kopiuj ścieżkę folderu”: " + copy.text)
            check(window.blockMenuGone(), "no block menu")
            window.lastNotice = ""
            window.tapItem(linkItem("linkMenuCopy"))
        }
        if (window.at() && !window.waiting(!linkMenuOpen() && page.clipboardText() === "Projekty/"))
            window.checkWait("the folder's path is copied: " + page.clipboardText())
        if (window.at()) check(Js.indexOf(window.lastNotice, "Projekty/") >= 0, "and said: " + window.lastNotice)
        if (window.at()) list().positionViewAtIndex(row, ListView.Center)
        menuGesture(test.row)
        if (window.at()) window.waitStep(linkMenuOpen(), gesture + " on “Firma” opens the link menu")
        if (window.at()) {
            check(!linkMenu().folder && !linkMenu().unresolved
                  && linkItem("linkMenuOpen").text === qsTranslate("LinkMenu", "Open")
                  && linkItem("linkMenuCopy").text === qsTranslate("LinkMenu", "Copy link"),
                  "a page's part: “Otwórz”, “Kopiuj link”")
            window.tapItem(linkItem("linkMenuCopy"))
        }
        if (window.at() && !window.waiting(!linkMenuOpen() && page.clipboardText() === "[[Firma]]"))
            window.checkWait("“Kopiuj link” copies [[Firma]]: " + page.clipboardText())
        // ---- 5. a table cell, the property, an embed, a query result -------------------------------
        if (window.at()) list().positionViewAtIndex(rowWith("", "table"), ListView.Center)
        partClick("a table cell", rowWith("", "table"))
        if (window.at()) list().positionViewAtBeginning()
        partClick("a page property", 0)
        if (window.at()) list().positionViewAtEnd()
        partClick("an embed", rowWith("![[Strona z linkiem]]", ""))
        partClick("a query result", rowWith("", "query"))
        // ---- 6. Alt+Enter on the edited raw line ----------------------------------------------------
        if (window.at()) list().positionViewAtIndex(row, ListView.Center)
        altEnter("Firma", 2, false)
        if (window.at()) window.waitStep(navigated() && host.lastRoute.linkPart === 1 && !lastNewPane, "Alt+Enter on “Firma” follows the part")
        if (window.at()) check(info(test.row).editText === before && page.blocks.count === rowsBefore,
                    "no line break typed")
        altEnter("Adam", 1, false)
        if (window.at()) window.waitStep(navigated() && host.lastRoute.path === "Firma/Adam cośtam.md", "Alt+Enter on “Adam” opens the page")
        altEnter("Firma", 0, true)
        if (window.at()) window.waitStep(navigated() && host.lastRoute.linkPart === 1, "Alt+Shift+Enter follows the part")
        if (window.at()) check(lastNewPane === !Theme.narrow, "in the side panel on a wide window: " + lastNewPane)
        altEnter("Zapytać", 3, false)
        if (window.at()) {
            check(!navigated() && window.lastNotice === qsTranslate("BlockEditor", "There is no link at the cursor"),
                  "outside a link: a notice, nothing opened: " + window.lastNotice)
            check(info(test.row).editText === before && page.editingRow === test.row,
                  "the text unchanged, still edited")
            page.stopEditing()
        }
        if (window.at()) window.waitStep(page.editingRow < 0, "editing ends")
        if (window.at()) {
            const r = rowWith("", "table")
            list().positionViewAtIndex(r, ListView.Center)
            const view = window.findWith(delegateAt(r), test.tableMembers) as TableBlockView
            check(view !== null, "the table view is there")
            if (view)
                view.startCell(0, 0)
        }
        if (window.at()) window.waitStep(focusedField(delegateAt(rowWith("", "table"))) !== null, "the cell's field has the keyboard")
        if (window.at()) {
            const field = focusedField(delegateAt(rowWith("", "table")))
            field.cursorPosition = 3
            mark()
            EditorInput.sendKey(field, Qt.Key_Return, Qt.AltModifier, "")
        }
        if (window.at()) window.waitStep(navigated() && host.lastRoute.linkPart === 1, "Alt+Enter in a table cell follows the part")
        if (window.at()) {
            const field = focusedField(delegateAt(rowWith("", "table")))
            if (field)
                EditorInput.sendKey(field, Qt.Key_Escape, 0, "")
        }
        if (window.at()) check(Js.indexOf(window.pageText(), "| [[Firma/Adam cośtam]] | biuro |") >= 0, "the cell is unchanged")
        // ---- 7. a (simulated) screen reader: every link an accessible link item ----------------------
        if (window.at()) {
            list().positionViewAtIndex(row, ListView.Center)
            // Without a screen reader no text (a block, a table cell, a query result, an
            // embed) carries a link layer: nothing lays the text out again.
            check(window.countNamed(editor, "linkLayer") === 0, "no link layers without a screen reader")
            DeviceInfo.simulateScreenReader = true
        }
        if (window.at()) window.waitStep(linkItemCount(row) === 2, "the screen reader sees two links")
        if (window.at()) {
            const t = textOf(row, "")
            const first = linkItemAt(row, 0)
            const second = linkItemAt(row, 1)
            const firstName = DevTest.accessibleName(first)
            const secondName = DevTest.accessibleName(second)
            check(firstName === "Firma" && secondName === "Adam cośtam", "named by their text: " + firstName + ", " + secondName)
            for (let i = 0; i < linkItemCount(row); ++i) {
                const it = linkItemAt(row, i)
                check(overItsLink(it, t), "the item “" + DevTest.accessibleName(it) + "” lies over its link")
                check(DevTest.accessibleRole(it) === Accessible.Link, "and is a link")
            }
            first.forceActiveFocus()
            check(window.childCount(first) > 0 && (window.childAt(first, 0) as Item).visible, "the focus ring shows")
            mark()
            DevTest.callCatching(first, "activate") // what its press action does
        }
        if (window.at()) window.waitStep(navigated() && host.lastRoute.linkPart === 1, "its press action follows the part")
        if (window.at()) DeviceInfo.simulateScreenReader = false
        if (window.at()) window.waitStep(linkItemCount(row) === 0, "without a screen reader no items")
        if (window.at()) check(window.countNamed(editor, "linkLayer") === 0, "and no link layers")
        // ---- 8. a phone: the press underline, a flick opens nothing ----------------------------------
        if (window.at()) {
            if (!Theme.touchUi)
                return
            const t = textOf(row, "")
            linkBox(t, "part:1:")
            at = Qt.point(boxCx(), boxCy())
            mark()
            window.taskEvents.touchPress(t, at.x, at.y)
        }
        if (window.at()) window.waitStep(!Theme.touchUi || blockText(row).hoveredLink === partUrl, "while the finger is down the part is hovered")
        if (window.at()) {
            if (!Theme.touchUi)
                return
            check(partAloneUnderlined(), "and underlined, alone")
            const t = textOf(row, "")
            for (let k = 1; k <= 6; ++k)
                window.taskEvents.touchMove(t, at.x, at.y + 30 * k)
            window.taskEvents.touchRelease(t, at.x, at.y + 180)
        }
        if (window.at()) window.waitStep(!Theme.touchUi || blockText(row).hoveredLink === "", "a flick lifts the underline")
        if (window.at()) check(!Theme.touchUi || !navigated(), "and opens nothing")
        if (window.at()) page.stopEditing()
        if (window.at()) check(window.fileLineOf(window.pageText(), "Zapytać") === firstLine, "the file is still as written")
    }

    Component.onCompleted: window.runSteps("linkpartstest")
}
