--!strict
-- Asystent AI (AI assistant): asks the model the user chose in Settings → Extensions → AI.
--
-- "ask" is an agent: the model has tools to search and read the whole notebook (b.search, b.query,
-- b.pages, b.read) and to change it (append to a page or a day, write a page, change a block). It
-- calls them as it needs, a turn at a time (b.ai with tools), until it answers; every change waits
-- for the user's yes (b.choose: yes, yes to every change of this question, no). It can also write
-- extensions: it reads the extensions' reference (b.data("reference"): docs/EXTENSIONS.md), checks
-- a page in a sandbox (b.checkExtension) and writes it; the app runs that code only once the user
-- approves it on the page, like code from a sync. The answer goes
-- into today's daily note under the question, as an outline, with the pages it changed — readable
-- on a phone and kept with the notes. "summarize" sums up the page on screen in one request.
--
-- Written with the extensions' public API only (docs/EXTENSIONS.md "AI"); its source can become a
-- page of the notebook and be changed there, like any built-in extension: its tools, their
-- descriptions and the instructions below are the agent.

local MAX_PAGE = 30000 -- characters of the page on screen sent along
local MAX_RESULT = 12000 -- characters of one tool's result
local MAX_ROUNDS = 16 -- the model's turns for one question
local MAX_REFERENCE = 40000 -- characters of the extensions' reference in one result
local MAX_PREVIEW = 600 -- characters of a change shown in its confirmation

local WRITTEN = {
    pl = {
        question = "🤖 %s",
        about = " ([[%s]])",
        summary = "🤖 Streszczenie [[%s]]",
        changed = "✏️ Zmienione: %s",
        system = "Jesteś asystentem w aplikacji do notatek Brulion. Masz narzędzia do przeszukiwania i czytania całego notatnika użytkownika i do zmieniania go. Notatki to Markdown: punkty \"- \", zadania \"- [ ]\" i \"- [x]\", terminy \"📅 RRRR-MM-DD\", przypomnienia \"⏰ RRRR-MM-DD GG:MM\", linki [[Strona]], tagi #tag; dziennik to strony dni, nazwane datą (2026-11-12). Dziś jest %s. Zanim odpowiesz na pytanie o notatki, poszukaj w nich (search_notes, query_notes) i przeczytaj, czego potrzebujesz. Zmieniaj notatki tylko wtedy, gdy użytkownik o to prosi; raczej dopisuj, niż przepisuj; niczego nie usuwaj, o ile nie poproszono. Każdą zmianę użytkownik zatwierdza; gdy odmówi, nie ponawiaj jej. Gdy użytkownik chce nowego polecenia, widoku albo automatyzacji w Brulionie, możesz napisać rozszerzenie: najpierw przeczytaj opis (extension_reference: Quick start i części, których potrzebujesz), sprawdź stronę (check_extension), potem ją zapisz (write_extension) i powiedz użytkownikowi, że zadziała po naciśnięciu „Uruchom” na stronie rozszerzenia. Odpowiadasz zwięźle, po polsku, chyba że notatki są w innym języku; zwykłym tekstem albo krótkimi punktami, z linkami [[Strona]] do stron, o których mówisz.",
        summarySystem = "Jesteś asystentem w aplikacji do notatek. Odpowiadasz zwięźle, po polsku, chyba że notatka jest w innym języku. Piszesz zwykłym tekstem albo krótkimi punktami.",
        noteIntro = "Notatka „%s”:\n\n%s\n\n",
        onScreen = "Na ekranie jest strona [[%s]]:\n\n%s\n\n---\n\n",
        summarizeAsk = "Streść tę notatkę w 3–5 zdaniach. Na końcu wypisz najważniejsze zadania, jeśli jakieś są.",
    },
    en = {
        question = "🤖 %s",
        about = " ([[%s]])",
        summary = "🤖 Summary of [[%s]]",
        changed = "✏️ Changed: %s",
        system = "You are an assistant in the notes app Brulion. You have tools to search and read the user's whole notebook and to change it. Notes are Markdown: items \"- \", tasks \"- [ ]\" and \"- [x]\", due dates \"📅 YYYY-MM-DD\", reminders \"⏰ YYYY-MM-DD HH:MM\", links [[Page]], tags #tag; the journal is one page per day, named by its date (2026-11-12). Today is %s. Before you answer a question about the notes, search them (search_notes, query_notes) and read what you need. Change notes only when the user asks you to; prefer adding to rewriting; never delete anything unless asked. The user approves every change; when they decline one, do not try it again. When the user wants a new command, view or automation in Brulion, you can write an extension: read the reference first (extension_reference: its Quick start and the sections you need), check the page (check_extension), then write it (write_extension) and tell the user it works once they press Run on the extension's page. You answer briefly, in English unless the notes are in another language; plain text or short points, with [[Page]] links to the pages you mention.",
        summarySystem = "You are an assistant in a notes app. You answer briefly, in English unless the note is in another language. You write plain text or short points.",
        noteIntro = "The note \"%s\":\n\n%s\n\n",
        onScreen = "The page on screen is [[%s]]:\n\n%s\n\n---\n\n",
        summarizeAsk = "Summarise this note in 3–5 sentences. At the end list the most important tasks, if there are any.",
    },
}

local INTERFACE = {
    pl = {
        ask = "AI: zapytaj albo poproś…",
        summarize = "AI: streść tę stronę",
        prompt = "O co zapytać albo co zrobić w notatkach?",
        noPage = "Otwórz stronę, którą streścić.",
        empty = "Ta strona jest pusta.",
        written = "Odpowiedź AI jest w dzisiejszej notatce.",
        cut = "(strona przycięta do %d znaków)",
        yes = "Zgoda",
        yesAll = "Zgoda na wszystkie zmiany tego pytania",
        no = "Nie",
        appendPage = "AI chce dopisać do strony „%s”:\n\n%s",
        appendDay = "AI chce dopisać do dziennika z dnia %s:\n\n%s",
        createPage = "AI chce utworzyć stronę „%s”:\n\n%s",
        replacePage = "AI chce zastąpić całą treść strony „%s” (teraz %d znaków) nową:\n\n%s",
        changeBlock = "AI chce zmienić wiersz na stronie „%s” na:\n\n%s",
        createExtension = "AI chce utworzyć rozszerzenie „%s” (zadziała, gdy naciśniesz Uruchom na jego stronie):\n\n%s",
        replaceExtension = "AI chce zastąpić rozszerzenie „%s” nowym kodem (znowu poczeka na Uruchom):\n\n%s",
        tooLong = "AI nie skończyło po %d krokach; to, co zrobiło, jest w dzisiejszej notatce.",
    },
    en = {
        ask = "AI: ask or ask to do…",
        summarize = "AI: summarise this page",
        prompt = "What do you want to ask, or to do in your notes?",
        noPage = "Open the page to summarise.",
        empty = "This page is empty.",
        written = "The AI's answer is in today's note.",
        cut = "(the page cut to %d characters)",
        yes = "Allow",
        yesAll = "Allow every change of this question",
        no = "No",
        appendPage = "The AI wants to add to the page \"%s\":\n\n%s",
        appendDay = "The AI wants to add to the journal of %s:\n\n%s",
        createPage = "The AI wants to create the page \"%s\":\n\n%s",
        replacePage = "The AI wants to replace the whole page \"%s\" (now %d characters) with:\n\n%s",
        changeBlock = "The AI wants to change a line of the page \"%s\" to:\n\n%s",
        createExtension = "The AI wants to create the extension \"%s\" (it runs once you press Run on its page):\n\n%s",
        replaceExtension = "The AI wants to replace the extension \"%s\" with new code (it waits for Run again):\n\n%s",
        tooLong = "The AI did not finish in %d steps; what it did is in today's note.",
    },
}

local W = if b.lang() == "pl" then WRITTEN.pl else WRITTEN.en
local T = if b.uiLang() == "pl" then INTERFACE.pl else INTERFACE.en

local function trim(s: string): string
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function cut(s: string, n: number): string
    if #s <= n then
        return s
    end
    -- (Whole UTF-8 characters: a cut never splits one.)
    local stop = n
    while stop > 0 and string.byte(s, stop + 1) ~= nil and string.byte(s, stop + 1) >= 0x80
        and string.byte(s, stop + 1) < 0xC0 do
        stop -= 1
    end
    return s:sub(1, stop) .. "\n…"
end

-- The answer as an outline under its question: every line a child item (a list item's own marker
-- dropped, a heading in bold), empty lines and code fences left out.
local function outline(answer: string): string
    local items = {}
    for line in (answer .. "\n"):gmatch("([^\n]*)\n") do
        local text = trim(line)
        if text ~= "" and not text:match("^```") then
            local heading = text:match("^#+%s+(.+)$")
            if heading then
                text = "**" .. heading .. "**"
            else
                text = text:gsub("^[-*+]%s+", ""):gsub("^%d+[.)]%s+", "")
            end
            table.insert(items, "  - " .. text)
        end
    end
    if #items == 0 then
        return "  - …"
    end
    return table.concat(items, "\n")
end

-- The page's text to send (cut when it is long), "" when there is none.
local function pageText(page: string, max: number): (string, string?)
    if page == "" then
        return "", nil
    end
    local text = b.read(page) or ""
    if #text > max then
        return text:sub(1, max), string.format(T.cut, max)
    end
    return text, nil
end

-- ---- The agent's tools ------------------------------------------------------------------------

local TOOLS = {
    {
        name = "search_notes",
        description = "Full-text search over every note of the notebook: words in any form, diacritics optional, the best matches first. Returns each match's page, line, an excerpt and the block's ref (for update_block). Call it first whenever the user asks about something that may be in the notes.",
        input = {
            type = "object",
            properties = {
                query = { type = "string", description = "Words to look for" },
                limit = { type = "integer", description = "At most this many matches (default 15, at most 50)" },
            },
            required = { "query" },
        },
    },
    {
        name = "query_notes",
        description = "Runs a query of Brulion's query language over the notebook and returns the matching tasks, blocks, reminders or pages with their page, text, status (\" \" open, \"x\" done, \"-\" cancelled, \"/\" in progress), dates and ref. Call it for tasks, due dates, tags and reminders. Filters are ANDed: type:tasks|blocks|pages|reminders, status:todo|doing|done|cancelled|open|closed|all, tag:name (or #name), page:\"Title\", path:folder/, ref:[[Page]] (items linking to a page), text or \"a phrase\", due:/scheduled:/done:/created:/remind:/date: with a date condition (today, tomorrow, 2026-11-20, +7d, -2w, this-week, next-month, today..+7d, <today, overdue, none, any), sort:due,-priority, limit:N. Examples: \"type:tasks status:open due:<=+7d\", \"type:tasks status:done done:this-week\", \"#projekt status:open\", \"type:reminders remind:today\".",
        input = {
            type = "object",
            properties = {
                query = { type = "string", description = "The query" },
            },
            required = { "query" },
        },
    },
    {
        name = "list_pages",
        description = "Lists the names of the notebook's pages (daily notes are named by their date), or only of the pages with a tag. Call it to see what pages exist.",
        input = {
            type = "object",
            properties = {
                tag = { type = "string", description = "Only pages with this tag (without #)" },
            },
        },
    },
    {
        name = "read_page",
        description = "Returns the whole Markdown text of a page (a daily note by its date, 2026-11-12). Call it to read what search_notes or query_notes found.",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The page's name, as in [[links]]" },
            },
            required = { "name" },
        },
    },
    {
        name = "append_to_journal",
        description = "Adds Markdown at the end of a day's journal note (today's when no date is given), creating it when needed: items (\"- \"), tasks (\"- [ ] … 📅 2026-11-20\"), reminders (\"⏰ 2026-11-20 09:00\"). The user approves it first. Call it when the user asks to note something down for a day.",
        input = {
            type = "object",
            properties = {
                markdown = { type = "string", description = "What to add (Markdown, usually items)" },
                date = { type = "string", description = "The day, YYYY-MM-DD (default today)" },
            },
            required = { "markdown" },
        },
    },
    {
        name = "append_to_page",
        description = "Adds Markdown at the end of a page, creating the page when it does not exist. The user approves it first. Prefer it to write_page for adding to a page.",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The page's name" },
                markdown = { type = "string", description = "What to add (Markdown)" },
            },
            required = { "name", "markdown" },
        },
    },
    {
        name = "write_page",
        description = "Creates a page, or replaces the whole text of an existing one (read it first: what is not in the new text is lost). The user approves it first. Use it for new pages or when the user asks to rewrite a page.",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The page's name" },
                content = { type = "string", description = "The page's whole Markdown text" },
            },
            required = { "name", "content" },
        },
    },
    {
        name = "update_block",
        description = "Changes one block's first line (the text after its marker and checkbox; its dates and id kept) and/or a task's status, by the block's ref from search_notes or query_notes. The user approves it first. Refused when the line changed since it was read: search again then.",
        input = {
            type = "object",
            properties = {
                ref = { type = "string", description = "The block's ref" },
                text = { type = "string", description = "The new text of its first line" },
                status = { type = "string", enum = { "todo", "doing", "done", "cancelled" }, description = "A task's new status" },
            },
            required = { "ref", "text" },
        },
    },
}

table.insert(TOOLS, {
    name = "extension_reference",
    description = "Brulion's reference for writing extensions: Luau code on a page tagged #extension that adds palette commands, views and hooks through the API table b. Without a section it lists the sections; ask for the ones you need by words of their heading (\"Quick start\", \"The API\", \"Commands\", \"Views\", \"Hooks\", \"Questions\", \"Recipes\", \"Errors\", \"Extensions in the notebook\"). Read it before you write an extension: never guess the functions of b.",
    input = {
        type = "object",
        properties = {
            section = { type = "string", description = "Words of a section's heading; none: the list of sections" },
        },
    },
})
table.insert(TOOLS, {
    name = "check_extension",
    description = "Loads the whole Markdown text of an extension page in a sandbox, as the app would load it, and writes nothing: whether it loads, an error with the page's line, the commands, views and hooks it registers. Call it before write_extension and fix what it reports.",
    input = {
        type = "object",
        properties = {
            name = { type = "string", description = "The page's name (title)" },
            content = { type = "string", description = "The page's whole Markdown text" },
        },
        required = { "name", "content" },
    },
})
table.insert(TOOLS, {
    name = "write_extension",
    description = "Creates or replaces an extension page with its whole Markdown text: frontmatter with tags: [extension] (permissions: [write] or [ai] only when the code needs them; description: one line), a sentence on what it does, the code in ```luau blocks. It is checked first and refused when it does not load. A new page goes to the folder Extensions/ unless the name has a folder. The user approves the write, and the app runs the code only after they press Run on the page: tell them so, and what the extension does.",
    input = {
        type = "object",
        properties = {
            name = { type = "string", description = "The page's name (title)" },
            content = { type = "string", description = "The page's whole Markdown text" },
        },
        required = { "name", "content" },
    },
})

local STATUS = { todo = " ", doing = "/", done = "x", cancelled = "-" }

local function str(v: any): string
    return if type(v) == "string" then v elseif v == nil then "" else tostring(v)
end

-- A search's or a query's items as lines for the model.
local function itemLines(items: { any }): string
    local lines = {}
    for _, item in items do
        local parts = { "[[" .. str(item.page) .. "]]" }
        if item.line then
            table.insert(parts, "line " .. str(item.line))
        end
        if item.task then
            table.insert(parts, "[" .. str(item.task) .. "]")
        end
        for _, key in { "due", "scheduled", "start", "reminder" } do
            if item[key] then
                table.insert(parts, key .. " " .. str(item[key]))
            end
        end
        local text = str(item.text)
        if text == "" then
            text = str(item.excerpt)
        end
        local line = table.concat(parts, ", ") .. ": " .. trim(text)
        if item.ref then
            line ..= " (ref " .. str(item.ref) .. ")"
        end
        table.insert(lines, "- " .. line)
    end
    return table.concat(lines, "\n")
end

-- The page a ref names: its path's last part without ".md" ("Dziennik/2026-11-11.md#L3@…").
local function pageOfRef(ref: string): string
    local path = ref:match("^(.-)#L%d+") or ref
    return (path:match("([^/]+)$") or path):gsub("%.md$", "")
end

-- One question's agent: its yes to every change, the pages it changed.
type Session = { all: boolean, changed: { string } }

local function confirm(session: Session, what: string): boolean
    if session.all then
        return true
    end
    local choice = b.choose(what, { T.yes, T.yesAll, T.no })
    if choice == T.yesAll then
        session.all = true
    end
    return choice == T.yes or choice == T.yesAll
end

local function noteChanged(session: Session, page: string)
    local link = "[[" .. page .. "]]"
    for _, known in session.changed do
        if known == link then
            return
        end
    end
    table.insert(session.changed, link)
end

local DECLINED = "The user declined this change."

-- ---- Extensions ---------------------------------------------------------------------------------

local REFERENCE: string = str(b.data("reference"))

type Heading = { level: number, title: string, line: number }

-- The reference, or the sections whose heading has `section`'s words (with their subsections).
local function reference(section: string): (string, boolean)
    if REFERENCE == "" then
        return "The reference is not available.", true
    end
    local lines = {}
    for line in (REFERENCE .. "\n"):gmatch("([^\n]*)\n") do
        table.insert(lines, line)
    end
    local heads: { Heading } = {}
    local fence: string? = nil -- the open code block's fence ("```", "````", "~~~")
    for i, line in lines do
        local run, rest = line:match("^(```+)(.*)$")
        if not run then
            run, rest = line:match("^(~~~+)(.*)$")
        end
        -- A fence (CommonMark): no backtick after a backtick run ("```` ```luau ````" is prose).
        if run and fence == nil and not (run:sub(1, 1) == "`" and rest:find("`", 1, true)) then
            fence = run
        elseif run and fence ~= nil and run:sub(1, 1) == fence:sub(1, 1) and #run >= #fence and rest:match("^%s*$") then
            fence = nil
        elseif fence == nil then
            local hashes, title = line:match("^(#+) (.+)$")
            if hashes and #hashes >= 2 and #hashes <= 3 then
                table.insert(heads, { level = #hashes, title = title, line = i })
            end
        end
    end
    local wanted = b.fold(section)
    if wanted == "" then
        local names = {}
        for _, h in heads do
            table.insert(names, string.rep("  ", h.level - 2) .. "- " .. h.title)
        end
        return "Sections of the reference (ask for one by words of its heading):\n" .. table.concat(names, "\n"), false
    end
    local parts = {}
    local k = 1
    while k <= #heads do
        local h = heads[k]
        if b.fold(h.title):find(wanted, 1, true) then
            local n = k + 1
            while n <= #heads and heads[n].level > h.level do
                n += 1
            end
            local finish = if n <= #heads then heads[n].line - 1 else #lines
            table.insert(parts, table.concat(lines, "\n", h.line, finish))
            k = n
        else
            k += 1
        end
    end
    if #parts == 0 then
        return "No section has \"" .. section .. "\" in its heading: call extension_reference without a section for the list.", true
    end
    return table.concat(parts, "\n\n"), false
end

-- b.checkExtension's report for the model; whether the extension does not load.
local function checkReport(r: any): (string, boolean)
    if not r.ok then
        local where = if r.line then " (line " .. str(r.line) .. " of the page)" else ""
        return "It does not load" .. where .. ": " .. str(r.error), true
    end
    local parts = {}
    for _, c in r.commands or {} do
        table.insert(parts, "command " .. str(c.name) .. (if str(c.label) ~= str(c.name) then " \"" .. str(c.label) .. "\"" else ""))
    end
    for _, v in r.views or {} do
        table.insert(parts, "view " .. str(v.name) .. (if str(v.label) ~= "" then " \"" .. str(v.label) .. "\"" else " (not in the palette)"))
    end
    for _, event in r.hooks or {} do
        table.insert(parts, "hook " .. str(event))
    end
    local text = "It loads and registers " .. (if #parts > 0 then table.concat(parts, ", ") else "nothing") .. "."
    if r.permissions and #r.permissions > 0 then
        text ..= " Permissions: " .. table.concat(r.permissions, ", ") .. "."
    end
    if r.problems and #r.problems > 0 then
        text ..= " Problems: " .. table.concat(r.problems, "; ") .. "."
    end
    if r.writes then
        text ..= " Its top-level code writes to notes each time it loads (every save): do that in a command or a hook instead."
    end
    if r.printed and #r.printed > 0 then
        text ..= " It printed: " .. cut(table.concat(r.printed, " | "), 400)
    end
    return text, false
end

-- Is the page named `page` tagged as an extension?
local function isExtensionPage(page: string): boolean
    local info = b.page(page)
    for _, tag in if info then info.tags else {} do
        if tag == "extension" or tag == "rozszerzenie" then
            return true
        end
    end
    return false
end

-- Runs a tool: its result's text and whether it failed.
local function runTool(session: Session, name: string, input: any): (string, boolean)
    input = if type(input) == "table" then input else {}
    if name == "search_notes" then
        local query = trim(str(input.query))
        if query == "" then
            return "No query given.", true
        end
        local limit = math.clamp(tonumber(input.limit) or 15, 1, 50)
        local hits = b.search(query, limit)
        if #hits == 0 then
            return "Nothing found for \"" .. query .. "\".", false
        end
        return itemLines(hits), false
    elseif name == "query_notes" then
        local ok, result = pcall(b.query, str(input.query), 60)
        if not ok then
            return tostring(result), true
        end
        if #result.items == 0 then
            return "No items (" .. str(result.total) .. " in total).", false
        end
        return str(result.total) .. " items" .. (if result.total > #result.items then ", the first " .. #result.items else "")
            .. ":\n" .. itemLines(result.items), false
    elseif name == "list_pages" then
        local tag = trim(str(input.tag))
        local pages = b.pages(if tag ~= "" then { tag = tag } else {})
        if #pages == 0 then
            return "No pages.", false
        end
        local shown = {}
        for i = 1, math.min(#pages, 400) do
            table.insert(shown, pages[i])
        end
        local more = if #pages > #shown then "\n… and " .. (#pages - #shown) .. " more" else ""
        return #pages .. " pages: " .. table.concat(shown, "; ") .. more, false
    elseif name == "read_page" then
        local text = b.read(str(input.name))
        if text == nil then
            return "No page named \"" .. str(input.name) .. "\".", true
        end
        return if text == "" then "(The page is empty.)" else text, false
    elseif name == "append_to_journal" then
        local markdown = trim(str(input.markdown))
        local date = trim(str(input.date))
        if date == "" then
            date = b.today()
        end
        if markdown == "" then
            return "Nothing to add.", true
        end
        if not confirm(session, string.format(T.appendDay, date, cut(markdown, MAX_PREVIEW))) then
            return DECLINED, true
        end
        local ok, err = pcall(b.appendToJournal, date, markdown)
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, date)
        return "Added to the journal of " .. date .. ".", false
    elseif name == "append_to_page" then
        local page = trim(str(input.name))
        local markdown = trim(str(input.markdown))
        if page == "" or markdown == "" then
            return "A page's name and what to add are needed.", true
        end
        if not confirm(session, string.format(T.appendPage, page, cut(markdown, MAX_PREVIEW))) then
            return DECLINED, true
        end
        local before = b.read(page) or ""
        local separator = if before == "" or before:sub(-1) == "\n" then "" else "\n"
        local ok, err = pcall(b.write, page, before .. separator .. markdown .. "\n")
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, page)
        return "Added to [[" .. page .. "]].", false
    elseif name == "write_page" then
        local page = trim(str(input.name))
        local content = str(input.content)
        if page == "" then
            return "A page's name is needed.", true
        end
        local before = b.read(page)
        local what = if before == nil then string.format(T.createPage, page, cut(content, MAX_PREVIEW))
            else string.format(T.replacePage, page, utf8.len(before) or #before, cut(content, MAX_PREVIEW))
        if not confirm(session, what) then
            return DECLINED, true
        end
        local ok, err = pcall(b.write, page, content)
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, page)
        return (if before == nil then "Created [[" else "Rewrote [[") .. page .. "]].", false
    elseif name == "update_block" then
        local ref = trim(str(input.ref))
        local text = str(input.text)
        local status = if input.status ~= nil then STATUS[str(input.status)] else nil
        if ref == "" then
            return "A block's ref is needed.", true
        end
        if input.status ~= nil and status == nil then
            return "Unknown status \"" .. str(input.status) .. "\".", true
        end
        local page = pageOfRef(ref)
        local shown = if status ~= nil then "[" .. status .. "] " .. text else text
        if not confirm(session, string.format(T.changeBlock, page, cut(shown, MAX_PREVIEW))) then
            return DECLINED, true
        end
        local ok, result = pcall(b.replaceBlock, ref, text, status)
        if not ok then
            return tostring(result), true
        end
        noteChanged(session, page)
        return "Changed; its ref is now " .. str(result) .. ".", false
    elseif name == "extension_reference" then
        return reference(trim(str(input.section)))
    elseif name == "check_extension" then
        local page = trim(str(input.name))
        if page == "" then
            return "A page's name is needed.", true
        end
        return checkReport(b.checkExtension(page, str(input.content)))
    elseif name == "write_extension" then
        local page = trim(str(input.name))
        local content = str(input.content)
        if page == "" or trim(content) == "" then
            return "A page's name and its whole text are needed.", true
        end
        local before = b.read(page)
        if before == nil and not page:find("/", 1, true) then
            page = (if b.lang() == "pl" then "Rozszerzenia/" else "Extensions/") .. page
        elseif before ~= nil and not isExtensionPage(page) then
            return "[[" .. page .. "]] is a page that is no extension: choose another name.", true
        end
        local report, failed = checkReport(b.checkExtension(page, content))
        if failed then
            return "Not written. " .. report .. " Fix it and check again.", true
        end
        local what = string.format(if before == nil then T.createExtension else T.replaceExtension, page, cut(content, MAX_PREVIEW))
        if not confirm(session, what) then
            return DECLINED, true
        end
        local ok, err = pcall(b.write, page, content)
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, page)
        return "Wrote the extension [[" .. page .. "]]. " .. report
            .. " It runs once the user presses Run on its page (or in Settings → Extensions): tell them, and what it does.", false
    end
    return "No tool named \"" .. name .. "\".", true
end

-- ---- Commands -----------------------------------------------------------------------------------

b.command("ask", function(ctx)
    local question = trim(b.ask(T.prompt))
    if question == "" then
        return nil
    end
    local page: string = ctx.page or ""
    local text, note = pageText(page, MAX_PAGE)
    local first = question
    if text ~= "" then
        first = string.format(W.onScreen, page, text) .. question
    end
    local session: Session = { all = false, changed = {} }
    local messages: { any } = { { role = "user", content = first } }
    local system = string.format(W.system, b.today())
    local answer: string? = nil
    for _ = 1, MAX_ROUNDS do
        local said, reply = b.ai({ system = system, messages = messages, tools = TOOLS, maxTokens = 8000 })
        table.insert(messages, { role = "assistant", raw = reply.raw })
        if not reply.toolCalls then
            answer = said
            break
        end
        -- Every call of the turn, their results in one message.
        local results = {}
        for _, call in reply.toolCalls do
            local result, failed = runTool(session, str(call.name), call.input)
            local limit = if str(call.name) == "extension_reference" then MAX_REFERENCE else MAX_RESULT
            table.insert(results, { id = call.id, content = cut(result, limit), isError = failed })
        end
        table.insert(messages, { role = "user", results = results })
    end
    local title = string.format(W.question, question)
    if page ~= "" then
        title ..= string.format(W.about, page)
    end
    local entry = "- " .. title .. "\n" .. outline(answer or "")
    if #session.changed > 0 then
        entry ..= "\n  - " .. string.format(W.changed, table.concat(session.changed, ", "))
    end
    b.appendToJournal(b.today(), entry)
    if answer == nil then
        return string.format(T.tooLong, MAX_ROUNDS)
    end
    return if note then T.written .. " " .. note else T.written
end, { label = T.ask })

b.command("summarize", function(ctx)
    local page: string = ctx.page or ""
    if page == "" then
        return T.noPage
    end
    local text, note = pageText(page, 60000)
    if trim(text) == "" then
        return T.empty
    end
    local answer = b.ai({ system = W.summarySystem, prompt = string.format(W.noteIntro, page, text) .. W.summarizeAsk })
    b.appendToJournal(b.today(), "- " .. string.format(W.summary, page) .. "\n" .. outline(answer))
    return if note then T.written .. " " .. note else T.written
end, { label = T.summarize })
