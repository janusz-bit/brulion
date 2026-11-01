pragma ComponentBehavior: Bound
import QtQuick
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

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property var events: null
    property int row: -1
    property point at: Qt.point(0, 0)
    property string before: ""
    property int navigations: 0
    property int navigationsBefore: 0
    property bool lastNewPane: false
    property int rowsBefore: 0
    readonly property string gesture: Theme.touchUi ? "a long press" : "a right-click"
    readonly property string path: "Części linków.md"
    readonly property string content: "---\n"
        + "osoba: \"[[Firma/Adam cośtam]]\"\n"
        + "---\n"
        + "- Zapytać się [[Firma/Adam cośtam]] o to czego się pouczyć\n"
        + "- [[Projekty/Zażółć gęślą jaźń/Notatki]]\n"
        + "- Z tekstem: [[Firma/Adam cośtam|Adam]]\n"
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
        function onNavigated(route, newPane) {
            test.navigations += 1
            test.lastNewPane = newPane
        }
    }

    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function route() { return host.lastRoute }
    function navigated() { return navigations > navigationsBefore }
    function linkMenuOpen() { return editor.linkMenu !== null && editor.linkMenu.opened }
    function linkItem(name) { return window.menuItemNamed(editor.linkMenu, name) }
    /// The BlockText of `r` (its hoveredLink, shownHtml), or null.
    function blockText(r) {
        return window.findWhere(window.delegateAt(r), (it) => it.shownHtml !== undefined && it.hoveredLink !== undefined)
    }
    /// The rendered rich text of `r` holding `needle`, or null.
    function textOf(r, needle) { return window.textContaining(window.delegateAt(r), needle || "part:") }
    /// The box of the links of `text` whose URL starts with `url`, sampled on a 2-px grid; null
    /// when none.
    function linkBox(text, url) {
        let x1 = 1e9, y1 = 1e9, x2 = -1, y2 = -1
        for (let y = 1; text && y < text.height; y += 2) {
            for (let x = 1; x < text.width; x += 2) {
                if (text.linkAt(x, y).indexOf(url) === 0) {
                    x1 = Math.min(x1, x); y1 = Math.min(y1, y); x2 = Math.max(x2, x); y2 = Math.max(y2, y)
                }
            }
        }
        return x2 < 0 ? null : { x: x1, y: y1, width: x2 - x1, height: y2 - y1, cx: (x1 + x2) / 2, cy: (y1 + y2) / 2 }
    }
    /// How many links of `html` are underlined.
    function underlined(html) { return html.split("text-decoration:underline").length - 1 }
    /// The URL of the underlined link of `html` ("" for none).
    function underlinedHref(html) {
        const end = html.indexOf(";text-decoration:underline")
        if (end < 0)
            return ""
        const start = html.lastIndexOf("<a href=\"", end)
        return html.slice(start + 9, html.indexOf("\"", start + 9))
    }
    function mark() { navigationsBefore = navigations }
    /// The first row whose raw text holds `needle` (or of the view `view`), -1 when none.
    function rowWith(needle, view) {
        for (let r = 0; r < page.blocks.count; ++r) {
            const b = page.blocks.get(r)
            if (view ? b.view === view : b.editText.indexOf(needle) >= 0)
                return r
        }
        return -1
    }
    /// A tap (touch) or a left click at the centre of the link of `text` starting with `url`.
    function clickLink(text, url) {
        const box = linkBox(text, url)
        check(box !== null, "the link " + url + " is drawn")
        if (box)
            window.tapPoint(text, box.cx, box.cy)
    }
    /// Steps: the part link of the rendered text `find()` (a block, a cell, an embed, a result, a
    /// property) clicked or tapped: the part's route.
    function partClick(what, find) {
        return [
            wait(() => find() !== null && linkBox(find(), "part:1:") !== null, "the part is drawn in " + what),
            () => {
                mark()
                clickLink(find(), "part:1:")
            },
            wait(() => navigated() && route().linkPart === 1 && route().linkTarget === "Firma/Adam cośtam"
                       && route().target === "Firma" && route().from === test.path,
                 () => "a part in " + what + " sends its route: " + JSON.stringify(route()))
        ]
    }
    /// The context gesture on the part of row `rowOf()`: a right-click, or a finger held until the
    /// menu shows, then lifted.
    function menuGesture(rowOf) {
        const find = () => {
            const r = rowOf()
            const t = textOf(r)
            const box = t ? linkBox(t, "part:1:") : null
            check(box !== null, "the part of row " + r + " is drawn")
            if (box)
                at = Qt.point(box.cx, box.cy)
            return box ? t : null
        }
        if (!Theme.touchUi) {
            return [() => {
                const t = find()
                if (t)
                    events.mouseClick(t, at.x, at.y, Qt.RightButton, Qt.NoModifier, -1)
            }]
        }
        return [
            () => {
                const t = find()
                if (t)
                    events.touchEvent(t).press(0, t, at.x, at.y).commit()
            },
            window.holdStep(linkMenuOpen),
            () => {
                const t = textOf(rowOf())
                if (t)
                    events.touchEvent(t).release(0, t, at.x, at.y).commit()
            }
        ]
    }
    /// Alt(+Shift)+Enter with real keys while row 0 is edited with the caret `offset` characters
    /// after the start of `needle`.
    function altEnter(needle, offset, shift) {
        const caret = () => page.blocks.get(test.row).editText.indexOf(needle) + offset
        return [
            () => page.stopEditing(),
            wait(() => page.editingRow < 0, "not editing"),
            () => editor.startEditing(test.row, caret()),
            wait(() => editor.activeEditor !== null && editor.activeEditor.row === test.row
                       && editor.activeEditor.cursorPosition === caret(),
                 () => "the block is edited with the caret in “" + needle + "”"),
            () => {
                mark()
                before = page.blocks.get(test.row).editText
                rowsBefore = page.blocks.count
                window.realKey(Qt.Key_Return, Qt.AltModifier | (shift ? Qt.ShiftModifier : 0))
            }
        ]
    }

    function steps() {
        events = Qt.createQmlObject('import QtQuick; import QtTest; TestEvent {}', test, "linkPartsEvents")
        window.taskEvents = events // (window.tapItem, window.tapPoint)
        return [
            () => {
                host.setFile("Firma.md", "- Strona firmy\n")
                host.setFile("Firma/Adam cośtam.md", "- Adam\n")
                host.setFile("Projekty/Zażółć gęślą jaźń/Notatki.md", "- notatki\n")
                host.setFile("Strona z linkiem.md", "- Zobacz [[Firma/Adam cośtam]] w osadzeniu\n")
                host.setFile(test.path, test.content)
                editor.setPropertiesExpanded(true)
                page.path = test.path
            },
            wait(() => rowWith("Zapytać") >= 0 && textOf(rowWith("Zapytać")) !== null, "the page is shown"),
            () => {
                row = rowWith("Zapytać")
                editor.listView.positionViewAtIndex(row, ListView.Center)
                const t = textOf(row)
                check(t.text.indexOf("href=\"" + partUrl + "\"") >= 0 && t.text.indexOf("href=\"" + pageUrl + "\"") >= 0,
                      "[[Firma/Adam cośtam]] is drawn as a part and the page")
                const part = linkBox(t, "part:1:")
                const pageBox = linkBox(t, "page:")
                check(part !== null && pageBox !== null && part.x + part.width < pageBox.x && Math.abs(part.cy - pageBox.cy) < 4,
                      "the part left of the page on one line")
                // Between them the separator: no link.
                check(part !== null && pageBox !== null && t.linkAt((part.x + part.width + pageBox.x) / 2, part.cy) === "",
                      "the separator is no link")
            },
            // ---- 1. hover (desktop): the part under the mouse underlined, alone ------------------
            () => {
                if (Theme.touchUi)
                    return
                const t = textOf(row)
                const box = linkBox(t, "part:1:")
                events.mouseMove(t, box.cx, box.cy, -1, Qt.NoButton, Qt.NoModifier)
            },
            wait(() => Theme.touchUi || (blockText(row).hoveredLink === partUrl), "the mouse over “Firma”: its link hovered"),
            () => {
                if (Theme.touchUi)
                    return
                const html = blockText(row).shownHtml
                check(underlined(html) === 1 && underlinedHref(html) === partUrl,
                      "only the part is underlined: " + underlinedHref(html))
                const t = textOf(row)
                const part = linkBox(t, "part:1:")
                const pageBox = linkBox(t, "page:")
                events.mouseMove(t, (part.x + part.width + pageBox.x) / 2, part.cy, -1, Qt.NoButton, Qt.NoModifier)
            },
            wait(() => Theme.touchUi || blockText(row).hoveredLink === "", "over the separator nothing is hovered"),
            () => {
                if (Theme.touchUi)
                    return
                check(underlined(blockText(row).shownHtml) === 0, "and nothing underlined")
                const t = textOf(row)
                const box = linkBox(t, "page:")
                events.mouseMove(t, box.cx, box.cy, -1, Qt.NoButton, Qt.NoModifier)
            },
            wait(() => Theme.touchUi || blockText(row).hoveredLink === pageUrl, "over “Adam cośtam” the page link"),
            // ---- 2. clicks: the part, the page, the separator ---------------------------------------
            () => {
                mark()
                clickLink(textOf(row), "part:1:")
            },
            wait(() => navigated() && route().linkPart === 1 && route().linkTarget === "Firma/Adam cośtam"
                       && route().target === "Firma" && !lastNewPane,
                 () => (Theme.touchUi ? "a tap" : "a click") + " on “Firma” sends the part's route: " + JSON.stringify(route())),
            () => {
                check(page.editingRow < 0, "the block is not edited")
                mark()
                clickLink(textOf(row), "page:")
            },
            wait(() => navigated() && route().path === "Firma/Adam cośtam.md" && route().linkPart === undefined,
                 () => "“Adam cośtam” opens the page: " + JSON.stringify(route())),
            () => {
                mark()
                const t = textOf(row)
                const part = linkBox(t, "part:1:")
                const pageBox = linkBox(t, "page:")
                window.tapPoint(t, (part.x + part.width + pageBox.x) / 2, part.cy)
            },
            wait(() => page.editingRow === row, "a click on “/” edits the block"),
            () => {
                check(!navigated(), "and opens nothing")
                check(page.blocks.get(row).editText === "Zapytać się [[Firma/Adam cośtam]] o to czego się pouczyć",
                      "the raw line as written: " + page.blocks.get(row).editText)
                page.stopEditing()
            },
            wait(() => page.editingRow < 0 && textOf(row) !== null, "editing ends"),
            () => check(window.fileLineOf(window.pageText(), "Zapytać") === firstLine, "the file is unchanged"),
            // ---- 3. a display text: one link ------------------------------------------------------------
            () => {
                const r = rowWith("Z tekstem")
                const t = textOf(r, "page:")
                check(t !== null && t.text.indexOf("part:") < 0, "a link with a display text has no parts")
                const box = t ? linkBox(t, "page:") : null
                check(box !== null && t.linkAt(box.x + 1, box.cy).indexOf("page:Firma%2FAdam") === 0
                      && t.linkAt(box.x + box.width - 1, box.cy).indexOf("page:Firma%2FAdam") === 0,
                      "both of its halves are the page link")
            },
            // ---- 4. the menus of a folder's part and a page's part --------------------------------------
            () => editor.listView.positionViewAtIndex(rowWith("[[Projekty/"), ListView.Center),
            ...menuGesture(() => rowWith("[[Projekty/")),
            wait(linkMenuOpen, gesture + " on “Projekty” opens the link menu"),
            () => {
                check(editor.linkMenu.folder && linkItem("linkMenuOpen").text === qsTranslate("LinkMenu", "Show folder"),
                      "“Pokaż folder”: " + linkItem("linkMenuOpen").text)
                check(linkItem("linkMenuCopy").text === qsTranslate("LinkMenu", "Copy folder path"),
                      "“Kopiuj ścieżkę folderu”: " + linkItem("linkMenuCopy").text)
                check(editor.blockMenu === null || !editor.blockMenu.opened, "no block menu")
                window.lastNotice = ""
                window.tapItem(linkItem("linkMenuCopy"))
            },
            wait(() => !linkMenuOpen() && page.clipboardText() === "Projekty/",
                 () => "the folder's path is copied: " + page.clipboardText()),
            () => check(window.lastNotice.indexOf("Projekty/") >= 0, "and said: " + window.lastNotice),
            () => editor.listView.positionViewAtIndex(row, ListView.Center),
            ...menuGesture(() => test.row),
            wait(linkMenuOpen, gesture + " on “Firma” opens the link menu"),
            () => {
                check(!editor.linkMenu.folder && !editor.linkMenu.unresolved
                      && linkItem("linkMenuOpen").text === qsTranslate("LinkMenu", "Open")
                      && linkItem("linkMenuCopy").text === qsTranslate("LinkMenu", "Copy link"),
                      "a page's part: “Otwórz”, “Kopiuj link”")
                window.tapItem(linkItem("linkMenuCopy"))
            },
            wait(() => !linkMenuOpen() && page.clipboardText() === "[[Firma]]",
                 () => "“Kopiuj link” copies [[Firma]]: " + page.clipboardText()),
            // ---- 5. a table cell, the property, an embed, a query result -------------------------------
            () => editor.listView.positionViewAtIndex(rowWith("", "table"), ListView.Center),
            ...partClick("a table cell", () => textOf(rowWith("", "table"))),
            () => editor.listView.positionViewAtBeginning(),
            ...partClick("a page property", () => textOf(0)),
            () => editor.listView.positionViewAtEnd(),
            ...partClick("an embed", () => textOf(rowWith("![[Strona z linkiem]]"))),
            ...partClick("a query result", () => textOf(rowWith("", "query"))),
            // ---- 6. Alt+Enter on the edited raw line ----------------------------------------------------
            () => editor.listView.positionViewAtIndex(row, ListView.Center),
            ...altEnter("Firma", 2, false),
            wait(() => navigated() && route().linkPart === 1 && !lastNewPane, "Alt+Enter on “Firma” follows the part"),
            () => check(page.blocks.get(test.row).editText === before && page.blocks.count === rowsBefore,
                        "no line break typed"),
            ...altEnter("Adam", 1, false),
            wait(() => navigated() && route().path === "Firma/Adam cośtam.md", "Alt+Enter on “Adam” opens the page"),
            ...altEnter("Firma", 0, true),
            wait(() => navigated() && route().linkPart === 1,
                 "Alt+Shift+Enter follows the part"),
            () => check(lastNewPane === !Theme.narrow, "in the side panel on a wide window: " + lastNewPane),
            ...altEnter("Zapytać", 3, false),
            () => {
                check(!navigated() && window.lastNotice === qsTranslate("BlockEditor", "There is no link at the cursor"),
                      "outside a link: a notice, nothing opened: " + window.lastNotice)
                check(page.blocks.get(test.row).editText === before && page.editingRow === test.row,
                      "the text unchanged, still edited")
                page.stopEditing()
            },
            wait(() => page.editingRow < 0, "editing ends"),
            () => {
                const r = rowWith("", "table")
                editor.listView.positionViewAtIndex(r, ListView.Center)
                const view = window.findWhere(window.delegateAt(r), (it) => typeof it.startCell === "function")
                check(view !== null, "the table view is there")
                if (view)
                    view.startCell(0, 0)
            },
            wait(() => window.findWhere(window.delegateAt(rowWith("", "table")),
                                        (it) => it.cursorPosition !== undefined && it.activeFocus) !== null,
                 "the cell's field has the keyboard"),
            () => {
                const field = window.findWhere(window.delegateAt(rowWith("", "table")),
                                               (it) => it.cursorPosition !== undefined && it.activeFocus)
                field.cursorPosition = 3
                mark()
                EditorInput.sendKey(field, Qt.Key_Return, Qt.AltModifier, "")
            },
            wait(() => navigated() && route().linkPart === 1, "Alt+Enter in a table cell follows the part"),
            () => {
                const field = window.findWhere(window.delegateAt(rowWith("", "table")),
                                               (it) => it.cursorPosition !== undefined && it.activeFocus)
                if (field)
                    EditorInput.sendKey(field, Qt.Key_Escape, 0, "")
            },
            () => check(window.pageText().indexOf("| [[Firma/Adam cośtam]] | biuro |") >= 0, "the cell is unchanged"),
            // ---- 7. a (simulated) screen reader: every link an accessible link item ----------------------
            () => {
                editor.listView.positionViewAtIndex(row, ListView.Center)
                DeviceInfo.simulateScreenReader = true
            },
            wait(() => window.findAll(window.delegateAt(row), "linkTarget").length === 2, "the screen reader sees two links"),
            () => {
                const items = window.findAll(window.delegateAt(row), "linkTarget")
                const t = textOf(row)
                check(items[0].Accessible.name === "Firma" && items[1].Accessible.name === "Adam cośtam",
                      "named by their text: " + items[0].Accessible.name + ", " + items[1].Accessible.name)
                for (const it of items) {
                    const p = it.mapToItem(t, it.width / 2, it.height / 2)
                    check(t.linkAt(p.x, p.y) === it.href, "the item “" + it.Accessible.name + "” lies over its link")
                    check(it.Accessible.role === Accessible.Link, "and is a link")
                }
                items[0].forceActiveFocus()
                check(items[0].children.length > 0 && items[0].children[0].visible, "the focus ring shows")
                mark()
                items[0].activate() // what its press action does
            },
            wait(() => navigated() && route().linkPart === 1, "its press action follows the part"),
            () => DeviceInfo.simulateScreenReader = false,
            wait(() => window.findAll(window.delegateAt(row), "linkTarget").length === 0, "without a screen reader no items"),
            // ---- 8. a phone: the press underline, a flick opens nothing ----------------------------------
            () => {
                if (!Theme.touchUi)
                    return
                const t = textOf(row)
                const box = linkBox(t, "part:1:")
                at = Qt.point(box.cx, box.cy)
                mark()
                events.touchEvent(t).press(0, t, at.x, at.y).commit()
            },
            wait(() => !Theme.touchUi || blockText(row).hoveredLink === partUrl, "while the finger is down the part is hovered"),
            () => {
                if (!Theme.touchUi)
                    return
                const html = blockText(row).shownHtml
                check(underlined(html) === 1 && underlinedHref(html) === partUrl, "and underlined, alone")
                const t = textOf(row)
                for (let k = 1; k <= 6; ++k)
                    events.touchEvent(t).move(0, t, at.x, at.y + 30 * k).commit()
                events.touchEvent(t).release(0, t, at.x, at.y + 180).commit()
            },
            wait(() => !Theme.touchUi || blockText(row).hoveredLink === "", "a flick lifts the underline"),
            () => check(!Theme.touchUi || !navigated(), "and opens nothing"),
            () => page.stopEditing(),
            () => check(window.fileLineOf(window.pageText(), "Zapytać") === firstLine, "the file is still as written")
        ]
    }

    Component.onCompleted: window.runSteps(steps())
}
