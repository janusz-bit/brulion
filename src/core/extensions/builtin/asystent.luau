--!strict
-- Asystent AI (AI assistant): asks the model the user chose in Settings → Extensions → AI.
--
-- "chat" is one turn of a conversation in the app's chat view (docs/EXTENSIONS.md "AI"): the app
-- keeps the conversation as a page of the notebook (AI/…) and calls this command with its messages
-- (ctx.messages, the user's last) and the page it is about (ctx.page); what the command returns is
-- the AI's message, which the app writes into the conversation. It is an agent: the model has tools
-- to search and read the whole notebook (b.search, b.query, b.pages, b.read) and to change it
-- (append to a page or a day, write a page, change a block). It calls them as it needs, a turn at
-- a time (b.ai with tools), until it answers, and says what it is doing meanwhile (b.status); every
-- change waits for the user's yes (b.choose: yes, yes to every change of this question, no — the
-- chat shows the question in the conversation). It looks at the notebook's pictures — a photo, a
-- scan, a screenshot — with a request of their own (b.ai with `images`: its answer, as text, is
-- the tool's result; a model that sees is needed). It can also write extensions: it reads the
-- extensions' reference (b.data("reference"): docs/EXTENSIONS.md), checks a page in a sandbox
-- (b.checkExtension) and writes it; the app runs that code only once the user approves it on the
-- page, like code from a sync. The palette does not offer the command (palette = false): only the
-- chat has a conversation to give it.
--
-- The model that answers (b.aiModel()) decides what it is given: a provider on the network and the
-- model trained on this very script (Brulion Assistant: docs/FINETUNE.md) get the tools and the
-- instructions below as they are; another model on the device gets shorter tool results (4 000
-- characters, read_page reading on from where one stopped), and a small one fewer tools and
-- shorter instructions too (the setting "Fewer tools for small models", off by default).
--
-- Written with the extensions' public API only (docs/EXTENSIONS.md "AI"); its source can become a
-- page of the notebook and be changed there, like any built-in extension: its tools, their
-- descriptions and the instructions below are the agent.

local MAX_PAGE = 30000 -- characters of the page on screen sent along
local MAX_RESULT = 12000 -- characters of one tool's result
local MAX_DEVICE_RESULT = 4000 -- … for a model on the device (all of it is read on its processor)
local MAX_ROUNDS = 16 -- the model's turns for one question
local MAX_IDLE = 2 -- turns in a row after a change that only repeat what was done: then the question ends
local MAX_REFERENCE = 40000 -- characters of the extensions' reference in one result
local MAX_PREVIEW = 600 -- characters of a change shown in its confirmation
local MAX_PREVIEW_LINES = 40 -- and lines: the dialog has room for every one of them
local CONTEXT_HITS = 5 -- the first hits of a search shown with their page around them …
local CONTEXT_BYTES = 3000 -- … until this much of it is shown
local SHORT_PAGE_LINES = 12 -- a page of at most this many lines with text …
local SHORT_PAGE_BYTES = 600 -- … and bytes goes whole under its hit
local NEXT_LINES = 3 -- of a longer page: the lines after the hit's, in its block or section …
local OTHER_LINES = 3 -- … and its other lines with the search's words
local MAX_SCANNED = 400 -- lines of a page looked through for them (each folded: time) …
local MAX_SCAN_BYTES = 32000 -- … and bytes, from where they start
local MAX_CONTEXT_LINE = 200 -- bytes of one such line
local MAX_NAMED_PAGES = 20 -- pages named when a page asked for does not exist

local WRITTEN = {
    pl = {
        changed = "✏️ Zmienione: %s",
        system = "Jesteś asystentem w aplikacji do notatek Brulion: przeszukujesz, czytasz i zmieniasz notatnik użytkownika swoimi narzędziami.\n\nNotatki to Markdown: punkty \"- \", zadania \"- [ ]\" (zrobione \"- [x]\", w toku \"- [/]\"), terminy \"📅 RRRR-MM-DD\", przypomnienia \"⏰ RRRR-MM-DD GG:MM\", linki [[Strona]], tagi #tag. Dziennik to strony dni, nazwane datą (2026-11-12).\n\nZasady:\n- Prośba o wpis albo zmianę („przypomnij mi…”, „zanotuj…”, „dopisz… do…”, „odhacz…”): zrób ją; szukaj tylko tego, co trzeba zmienić (ref zadania, nazwy strony).\n- Pytanie o notatki: najpierw w nich poszukaj (search_notes; query_notes dla zadań, terminów, tagów i przypomnień). Gdy wynik już odpowiada na pytanie, odpowiedz; stronę czytaj tylko wtedy, gdy wynik nie wystarcza. Nie powtarzaj szukania, które nic nie dało: spróbuj raz innymi słowami, potem powiedz, czego nie znalazłeś.\n- Odpowiadaj tylko z tego, co dały narzędzia: nazwy, liczby i daty przepisuj dokładnie, nigdy ich nie zmyślaj. Wynik tego samego rodzaju pod inną nazwą (kod, numer, data) podaj i powiedz, jak go nazywa notatka. Gdy notatki tego nie mówią, powiedz to.\n- Gdy prośba nie mówi, czego dotyczy („odhacz to”, „przesuń to”), a nic wcześniej tego nie mówi, zadaj krótkie pytanie zamiast wołać narzędzie.\n- Na pytania ogólne — tłumaczenia, rachunki, przeliczenia, wiedzę, pogawędkę — odpowiadaj od razu, bez narzędzi.\n- Zmieniaj notatki tylko na prośbę: strona albo lista, którą użytkownik nazywa (zakupy, lista lektur, projekt) → append_to_page; notatka dnia, coś zrobionego albo do pamiętania na dany dzień, przypomnienie → append_to_journal; status albo termin zadania → znajdź jego ref przez query_notes, potem update_block (sam status zostawia tekst). Raczej dopisuj, niż przepisuj; niczego nie usuwaj, o ile nie poproszono.\n- Każdą zmianę użytkownik zatwierdza; gdy odmówi, nie ponawiaj jej. Nie mów, że coś zmieniłeś, zanim wynik narzędzia tego nie potwierdzi.\n- Obrazek w notatce to ![[nazwa.jpg]]: gdy pytanie go dotyczy, obejrzyj go (look_at_picture).\n- Nowe polecenie, widok albo automatyzacja w Brulionie: napisz rozszerzenie — najpierw przeczytaj opis (extension_reference: Quick start i części, których potrzebujesz), sprawdź stronę (check_extension), potem ją zapisz (write_extension) i powiedz użytkownikowi, że zadziała po naciśnięciu „Uruchom” na stronie rozszerzenia.\n- Odpowiadaj zwięźle, po polsku, chyba że notatki są w innym języku: zwykłym tekstem albo krótkimi punktami, z linkami [[Strona]] do stron, o których mówisz.",
        systemShort = "Jesteś asystentem w aplikacji do notatek Brulion: przeszukujesz, czytasz i zmieniasz notatnik użytkownika swoimi narzędziami.\n\nNotatki to Markdown: zadania \"- [ ]\" (zrobione \"- [x]\"), terminy \"📅 RRRR-MM-DD\", przypomnienia \"⏰ RRRR-MM-DD GG:MM\", linki [[Strona]], tagi #tag. Dziennik ma stronę na każdy dzień, nazwaną datą.\n\nZasady:\n- Pytanie o notatki: najpierw poszukaj (search_notes; query_notes dla zadań, terminów i przypomnień), potem odpowiedz z wyników. Nie zmyślaj nazw, liczb ani dat; gdy notatki tego nie mówią, powiedz to.\n- Prośba o wpis albo zmianę: zrób ją narzędziem — append_to_page do strony, którą użytkownik nazywa, append_to_journal do dnia albo przypomnienia, update_block (z ref z query_notes) dla statusu albo tekstu zadania.\n- Każdą zmianę użytkownik zatwierdza. Nie mów, że coś zmieniłeś, zanim wynik narzędzia tego nie potwierdzi.\n- Na pytania ogólne odpowiadaj od razu, bez narzędzi.\n- Odpowiadaj zwięźle, po polsku, chyba że notatki są w innym języku, z linkami [[Strona]].",
        today = "Dziś jest %s, %s.\n\n",
        weekdays = { "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota" },
        about = "Rozmowa dotyczy strony [[%s]]:\n\n%s\n\n---\n\n",
        lookAsk = "Opisz krótko, co jest na tym obrazku, i przepisz widoczny na nim tekst.",
        cut = "_(Strona [[%s]] poszła do AI przycięta do %d znaków.)_",
        omitted = "(Początek tej rozmowy — %d wcześniejszych wiadomości — nie zmieścił się w modelu i go tu nie ma.)\n\n",
        tooLong = "_(AI nie skończyło po %d krokach. Napisz, co dalej.)_",
        done = "Gotowe.",
    },
    en = {
        changed = "✏️ Changed: %s",
        system = "You are the assistant in the notes app Brulion: you search, read and change the user's notebook with your tools.\n\nNotes are Markdown: items \"- \", tasks \"- [ ]\" (done \"- [x]\", in progress \"- [/]\"), due dates \"📅 YYYY-MM-DD\", reminders \"⏰ YYYY-MM-DD HH:MM\", links [[Page]], tags #tag. The journal is one page per day, named by its date (2026-11-12).\n\nRules:\n- A request to add or change something (\"remind me…\", \"note down…\", \"add … to …\", \"tick off…\"): make the change; search only for what it changes (a task's ref, a page's name).\n- A question about the notes: search them first (search_notes; query_notes for tasks, dates, tags and reminders). When a result already answers it, answer; read a page only when the result is not enough. Do not repeat a search that found nothing: try other words once, then say what you did not find.\n- Answer only from what the tools gave: copy names, numbers and dates exactly, never invent one. Give a result of the kind asked for under another name (a code, a number, a date) and say what the note calls it. When the notes do not say it, say so.\n- When the request does not say what it is about (\"tick it off\", \"move that\") and nothing before says it, ask a short question instead of calling a tool.\n- Answer general questions — translations, arithmetic, conversions, facts, small talk — directly, without tools.\n- Change notes only when the user asks: a page or list the user names (shopping, reading list, a project) → append_to_page; a day's note, something done or to remember on a day, a reminder → append_to_journal; a task's status or date → find its ref with query_notes, then update_block (a status alone keeps the text). Prefer adding to rewriting; never delete anything unless asked.\n- The user approves every change; when they decline one, do not try it again. Never say a change is made before its tool's result says so.\n- A picture in a note is ![[name.jpg]]: when the question is about it, look at it (look_at_picture).\n- A new command, view or automation for Brulion: write an extension — read the reference first (extension_reference: its Quick start and the sections you need), check the page (check_extension), then write it (write_extension) and tell the user it runs once they press Run on the extension's page.\n- Answer briefly, in English unless the notes are in another language: plain text or short points, with [[Page]] links to the pages you mention.",
        systemShort = "You are the assistant in the notes app Brulion: you search, read and change the user's notebook with your tools.\n\nNotes are Markdown: tasks \"- [ ]\" (done \"- [x]\"), due dates \"📅 YYYY-MM-DD\", reminders \"⏰ YYYY-MM-DD HH:MM\", links [[Page]], tags #tag. The journal has a page per day, named by its date.\n\nRules:\n- A question about the notes: search first (search_notes; query_notes for tasks, dates and reminders), then answer from the results. Never invent names, numbers or dates; when the notes do not say it, say so.\n- A request to add or change something: make it with its tool — append_to_page for a page the user names, append_to_journal for a day or a reminder, update_block (with the ref from query_notes) for a task's status or text.\n- The user approves every change. Never say a change is made before its tool's result says so.\n- Answer general questions directly, without tools.\n- Answer briefly, in English unless the notes are in another language, with [[Page]] links.",
        today = "Today is %s, %s.\n\n",
        weekdays = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
        about = "The conversation is about the page [[%s]]:\n\n%s\n\n---\n\n",
        lookAsk = "Describe briefly what this picture shows and write out the text on it.",
        cut = "_(The page [[%s]] went to the AI cut to %d characters.)_",
        omitted = "(The start of this conversation — %d earlier messages — did not fit the model and is not here.)\n\n",
        tooLong = "_(The AI did not finish in %d steps. Say how to go on.)_",
        done = "Done.",
    },
}

local INTERFACE = {
    pl = {
        chat = "AI: tura rozmowy",
        chatOnly = "Z AI rozmawia się w czacie: ✨ → Nowa rozmowa.",
        partial = "(Poniżej początek: %d z %d znaków. Reszty tu nie widać.)",
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
        searching = "Szukam w notatkach: „%s”…",
        querying = "Przeglądam notatki: %s…",
        listing = "Przeglądam strony…",
        reading = "Czytam „%s”…",
        looking = "Oglądam obrazek „%s”…",
        referencing = "Czytam, jak się pisze rozszerzenia…",
        checking = "Sprawdzam rozszerzenie „%s”…",
        fewerTools = "Mniej narzędzi dla małych modeli",
        fewerToolsAbout = "Mały model na tym urządzeniu (nie ten wytrenowany dla asystenta) dostaje 8 narzędzi zamiast 12 i krótsze instrukcje: mniej czyta przy pierwszym pytaniu, ale LFM3 1.7B robił tak mniej zadań.",
    },
    en = {
        chat = "AI: a turn of a conversation",
        chatOnly = "Talk to the AI in the chat: ✨ → New conversation.",
        partial = "(Below is the beginning: %d of %d characters. The rest is not shown here.)",
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
        searching = "Searching the notes: “%s”…",
        querying = "Looking through the notes: %s…",
        listing = "Looking through the pages…",
        reading = "Reading “%s”…",
        looking = "Looking at the picture “%s”…",
        referencing = "Reading how extensions are written…",
        checking = "Checking the extension “%s”…",
        fewerTools = "Fewer tools for small models",
        fewerToolsAbout = "A small model on this device (not the one trained for the assistant) gets 8 tools instead of 12 and shorter instructions: less to read at its first question, but LFM3 1.7B did fewer tasks so.",
    },
}

local W = if b.lang() == "pl" then WRITTEN.pl else WRITTEN.en
local T = if b.uiLang() == "pl" then INTERFACE.pl else INTERFACE.en

-- The app's own tools (ctx.appTools: the app runs the assistant and its model is on the network —
-- never the model on the device, whose tools are what it was trained on: docs/FINETUNE.md).
local APP_WRITTEN = {
    pl = {
        rules = "\n- Sama aplikacja, tylko na prośbę: jej ustawienie (motyw jasny albo ciemny, akcent, czcionki, układ, edytor) → app_settings, potem change_setting; własny motyw z kolorami i czcionkami → set_theme; przycisk w aplikacji (górny pasek, pasek boczny, menu strony, dolna nawigacja) → add_button; polecenie aplikacji (otwórz widok, przełącz coś) → app_commands, potem run_app_command. Każdą zmianę też zatwierdza użytkownik.\n- Powiadomienie na tym urządzeniu → notify_user, tylko gdy użytkownik prosił, żeby dać mu o czymś znać, albo gdy w zaplanowanej rozmowie coś wymaga jego uwagi. Sama odpowiedź już go powiadamia: nigdy nie powiadamiaj tylko po to, by powiedzieć, że odpowiedź jest gotowa.",
        changed = "⚙️ W aplikacji: %s",
        themeFolder = "Motywy/",
        themeTag = "motyw",
        themeAbout = "Motyw, który zrobił asystent AI.",
        buttonFolder = "Rozszerzenia/",
        buttonPrefix = "Przycisk – ",
        buttonAbout = "Przycisk „%s”, który dodał asystent AI.",
    },
    en = {
        rules = "\n- The app itself, only when asked: a setting of it (the light or dark theme, the accent, fonts, the layout, the editor) → app_settings, then change_setting; a theme of one's own with colours and fonts → set_theme; a button in the app (the top bar, the sidebar, a page's menu, the bottom navigation) → add_button; a command of the app (open a view, switch something) → app_commands, then run_app_command. The user approves each of these changes too.\n- A notification on this device → notify_user, only when the user asked to be told of something or, in a scheduled conversation, when something needs them. The answer itself already notifies them: never notify just to say the answer is ready.",
        changed = "⚙️ In the app: %s",
        themeFolder = "Themes/",
        themeTag = "theme",
        themeAbout = "A theme the AI assistant made.",
        buttonFolder = "Extensions/",
        buttonPrefix = "Button – ",
        buttonAbout = "The button “%s”, added by the AI assistant.",
    },
}
local APP_INTERFACE = {
    pl = {
        settings = "Przeglądam ustawienia aplikacji…",
        commands = "Przeglądam polecenia aplikacji…",
        changeSetting = "AI chce zmienić ustawienie „%s” z %s na %s",
        runCommand = "AI chce uruchomić polecenie aplikacji „%s”",
        createTheme = "AI chce utworzyć motyw „%s” i użyć go w tym notatniku:\n\n%s",
        replaceTheme = "AI chce zastąpić motyw „%s” nowym i użyć go w tym notatniku:\n\n%s",
        createButton = "AI chce dodać przycisk „%s” jako rozszerzenie „%s” (pokaże się, gdy naciśniesz Uruchom na jego stronie):\n\n%s",
        notifying = "Wysyłam powiadomienie…",
    },
    en = {
        settings = "Looking through the app's settings…",
        commands = "Looking through the app's commands…",
        changeSetting = "The AI wants to change the setting “%s” from %s to %s",
        runCommand = "The AI wants to run the app's command “%s”",
        createTheme = "The AI wants to create the theme “%s” and use it in this notebook:\n\n%s",
        replaceTheme = "The AI wants to replace the theme “%s” with a new one and use it in this notebook:\n\n%s",
        createButton = "The AI wants to add the button “%s” as the extension “%s” (it shows once you press Run on its page):\n\n%s",
        notifying = "Sending a notification…",
    },
}
local WA = if b.lang() == "pl" then APP_WRITTEN.pl else APP_WRITTEN.en
local TA = if b.uiLang() == "pl" then APP_INTERFACE.pl else APP_INTERFACE.en

b.settings({
    { key = "fewerTools", label = T.fewerTools, value = false, description = T.fewerToolsAbout },
})

local function trim(s: string): string
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- The first `n` bytes of `s` or fewer, in whole UTF-8 characters: a cut never splits one.
local function whole(s: string, n: number): string
    if #s <= n then
        return s
    end
    local stop = n
    while stop > 0 and string.byte(s, stop + 1) ~= nil and string.byte(s, stop + 1) >= 0x80
        and string.byte(s, stop + 1) < 0xC0 do
        stop -= 1
    end
    return s:sub(1, stop)
end

local function cut(s: string, n: number): string
    if #s <= n then
        return s
    end
    return whole(s, n) .. "\n…"
end

-- A tool's result cut for a model on the device, saying how much is left and how to read on: a
-- page with read_page from where it stopped, anything else by asking for less.
local function cutPaged(s: string, n: number, tool: string, input: any): string
    if #s <= n then
        return s
    end
    local kept = whole(s, n)
    local shown = utf8.len(kept) or #kept
    local left = (utf8.len(s) or #s) - shown
    if tool == "read_page" and type(input) == "table" then
        local from = math.max(0, math.floor(tonumber(input.from) or 0))
        return kept .. string.format("\n[… %d more characters: read_page with from = %d reads on.]", left, from + shown)
    end
    return kept .. string.format("\n[… %d more characters not shown: ask for less — other words, a narrower query.]", left)
end

-- A change as its confirmation shows it: all of it when it is short; else its beginning, under a
-- line that says so and how much there is — a yes is given to what was read, and what the dialog
-- has no room for must not look as if it were not there.
local function preview(s: string): string
    local seen = whole(s, MAX_PREVIEW)
    local at, lines = 0, 0
    while true do
        local lineEnd = seen:find("\n", at + 1, true)
        if lineEnd == nil then
            break
        end
        lines += 1
        if lines >= MAX_PREVIEW_LINES then
            seen = seen:sub(1, lineEnd - 1)
            break
        end
        at = lineEnd
    end
    if #seen == #s then
        return s
    end
    return string.format(T.partial, utf8.len(seen) or #seen, utf8.len(s) or #s) .. "\n\n" .. seen .. "\n…"
end

-- The page's text to send (cut when it is long: whole characters, "…" at the end), "" when there
-- is none; true when it was cut.
local function pageText(page: string, max: number): (string, boolean)
    if page == "" then
        return "", false
    end
    local text = b.read(page) or ""
    if #text > max then
        return cut(text, max), true
    end
    return text, false
end

-- The day `n` days after `date` ("2026-11-13" and 1: "2026-11-14"; day 32 rolls into the next
-- month) and its weekday, 1 Sunday … 7 Saturday. (Luau's os.time reads the table as UTC, at noon:
-- daylight saving never moves a day.)
local function dayAfter(date: string, n: number): (string, number)
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n, hour = 12 })
    return os.date("!%Y-%m-%d", noon), os.date("!*t", noon).wday
end

-- What the first message starts with: today with its weekday. (Not a calendar of the days after
-- it: a small model took those dates for what the user said — "Put it where it usually goes." went
-- into the journal as the calendar's first line — and asked back no more.)
local function todayLine(): string
    local today = b.today()
    local _, weekday = dayAfter(today, 0)
    return string.format(W.today, W.weekdays[weekday], today)
end

-- ---- The agent's tools ------------------------------------------------------------------------

local TOOLS = {
    {
        name = "search_notes",
        description = "Full-text search over every note of the notebook: words in any form, diacritics optional, the best matches first. Returns each match's page, line, an excerpt and the block's ref (for update_block); under the first matches more of their page: a short page whole (a person's page, a day), of a longer one the lines after the match and other lines with the words. Call it first whenever the user asks about something that may be in the notes.",
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
        description = "Finds tasks, blocks, reminders or pages with a query of Brulion's query language and returns each match's page, status (open, in progress, done, cancelled), dates, text and ref (for update_block). Its result answers the question: answer from it. Queries for the usual questions — overdue: type:tasks status:open due:overdue; due today: type:tasks status:open due:today; due in the coming week: type:tasks status:open due:today..+7d; in progress: type:tasks status:doing; unfinished (not done yet): type:tasks status:open; done this week: type:tasks status:done done:this-week; done in the last 7 days: type:tasks status:done done:-7d..today; open tasks with a tag (#name): type:tasks status:open tag:name; open tasks of a page: type:tasks status:open page:\"Title\"; a task by its words: type:tasks \"its words\"; reminders today: type:reminders remind:today. Filters, separated by spaces, all must hold: type:tasks|blocks|pages|reminders, status:todo|doing|done|cancelled|open|closed|all (open = unfinished: todo and in progress; closed = done and cancelled), tag:name (a #tag), page:\"Title\" (a page's name), path:folder/, words or \"a phrase\", due:/scheduled:/done:/created:/remind: with a date condition (today, tomorrow, 2026-11-20, +7d, -2w, this-week, next-month, today..+7d, <today, overdue, none), sort:due, limit:N.",
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
        description = "Adds Markdown at the end of a day's journal note (today's when no date is given), creating it when needed: items (\"- …\"), tasks (\"- [ ] what to do 📅 YYYY-MM-DD\"), reminders: a task with the time (\"- [ ] what to remember ⏰ YYYY-MM-DD HH:MM\"). The user approves it first. Call it when the user asks to note something down for a day or to be reminded of something.",
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
        description = "Adds Markdown at the end of a page that exists: a page or list the user names (shopping, a reading list, a project). Refused for a page that does not exist: a new page is write_page. The user approves it first. Prefer it to write_page for adding to a page.",
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
                text = { type = "string", description = "The new text of its first line, without the list marker and checkbox; leave it out to change only the status" },
                status = { type = "string", enum = { "todo", "doing", "done", "cancelled" }, description = "A task's new status" },
            },
            required = { "ref" },
        },
    },
}

table.insert(TOOLS, {
    name = "look_at_picture",
    description = "Looks at a picture of the notebook — a photo, a scan, a screenshot, embedded in notes as ![[name.jpg]] — and answers a question about it: what it shows, the text written on it. Returns the answer as text. Call it when the user asks about a picture, or when a picture in a note may hold what they ask about.",
    input = {
        type = "object",
        properties = {
            name = { type = "string", description = "The picture's file name, as in ![[name.jpg]]" },
            question = { type = "string", description = "What to find out about it (default: what it shows and the text on it)" },
        },
        required = { "name" },
    },
})
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


-- The app's own tools: offered only with ctx.appTools (above), after the notes' tools.
local APP_TOOLS = {
    {
        name = "app_settings",
        description = "Lists the app's settings that may be changed, with their values: the theme (theme: light, dark or system; accent; density; fontScale; uiFont …), the layout, the editor (fonts, width, line spacing, focus and reading modes …), the built-in extensions, reminders; and the open notebook's own (journal folder and file names, templates, the week's first day, its theme page). Each line: key = value — its label (group, kind), the values it takes. Give words to narrow it (\"theme\", \"font\", \"journal\"). Call it before change_setting.",
        input = {
            type = "object",
            properties = {
                words = { type = "string", description = "Words of the setting's key, label or group; none: all of them" },
            },
        },
    },
    {
        name = "change_setting",
        description = "Changes one setting of the app (a key app_settings listed) to a value it takes: a choice one of its options, a toggle true or false, a number within its range, a colour \"#rrggbb\", a list of its options. Applied at once — a device's setting on this device, the notebook's for every device of the notebook. The user approves it first.",
        input = {
            type = "object",
            properties = {
                key = { type = "string", description = "The setting's key, as app_settings gives it" },
                value = { type = "string", description = "Its new value (true / false for a toggle, a number, a colour, one of its options; a list as values separated by commas)" },
            },
            required = { "key", "value" },
        },
    },
    {
        name = "app_commands",
        description = "Lists the commands of the app's command palette whose id or title has the words given: their id, title and shortcut — opening a view (calendar, tasks, graph …), switching the theme, the side panel, focus mode, and the extensions' commands. Call it before run_app_command.",
        input = {
            type = "object",
            properties = {
                words = { type = "string", description = "Words of the command's title or id" },
            },
            required = { "words" },
        },
    },
    {
        name = "run_app_command",
        description = "Runs one command of the app's command palette by its id (from app_commands), as the palette runs it, once this answer is written. The user approves it first.",
        input = {
            type = "object",
            properties = {
                id = { type = "string", description = "The command's id" },
            },
            required = { "id" },
        },
    },
    {
        name = "set_theme",
        description = "Makes a theme of the app — colours, corner radii, spacing, fonts — as a theme page of the notebook and picks it for the notebook (Settings → Appearance → Themes lists it; it syncs with the notebook). Tokens: the surfaces bg, bgElevated, bgSunken, bgHover, bgSelected; text fg, fgMuted, fgFaint; lines border, borderSubtle; accent, accentText, accentBg, primary, link, tagBg, tagFg; danger, warning, success, info; selection, mark, focusRing; the radii radiusSm (0–24), radius (0–32), radiusLg (0–40); space (3–6); the fonts fontUi, fontSans, fontSerif, fontMono (a family on the device). Colours as \"#rrggbb\". Tokens for one mode only go in light or dark. The user approves it first; an unknown token or a wrong value is refused with what is wrong.",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The theme's name (its page's title)" },
                tokens = { type = "object", description = "Tokens for both modes: {\"accent\": \"#1f8a80\", \"radius\": 9}" },
                light = { type = "object", description = "Tokens of the light mode only: {\"bg\": \"#f4ecd8\", \"fg\": \"#3b2f20\"}" },
                dark = { type = "object", description = "Tokens of the dark mode only: {\"bg\": \"#14110c\"}" },
                description = { type = "string", description = "One sentence on the theme" },
            },
            required = { "name" },
        },
    },
    {
        name = "add_button",
        description = "Adds a button to the app's UI that runs one of the app's commands (an id from app_commands) or opens a page: written as a small extension, which shows the button once the user presses Run on its page (tell them). Places: toolbar (the top bar; on phones the More menu), sidebar, statusbar, pageMenu, tabMenu, bottomNav (the phone's bottom navigation). The user approves it first.",
        input = {
            type = "object",
            properties = {
                label = { type = "string", description = "The button's name (its tooltip too)" },
                place = { type = "string", enum = { "toolbar", "sidebar", "statusbar", "pageMenu", "tabMenu", "bottomNav" }, description = "Where it goes (default toolbar)" },
                icon = { type = "string", description = "One of the app's icons: star, sun, moon, calendar, timer, bookmark, zap, palette, sparkles, house, list-todo, notebook-pen … (default star)" },
                command = { type = "string", description = "The id of an app command it runs (from app_commands)" },
                page = { type = "string", description = "Or the page it opens (made when missing)" },
            },
            required = { "label" },
        },
    },
    {
        name = "notify_user",
        description = "Shows the user a notification on this device (a system notification, or a message in the app while it is in front). Only when the user asked to be told of something, or in a scheduled conversation when something needs them; the answer itself already notifies them, so never notify just to say the answer is ready. Changes nothing, needs no approval; the app may refuse it (notifications turned off, too many).",
        input = {
            type = "object",
            properties = {
                text = { type = "string", description = "What it says: one or two short sentences" },
                title = { type = "string", description = "Its title, a few words (default the assistant's name)" },
                page = { type = "string", description = "A page a tap on it opens (its name, as [[links]] name it)" },
            },
            required = { "text" },
        },
    },
}
local APP_TOOL_NAMES: { [string]: boolean } = {}
for _, tool in APP_TOOLS do
    APP_TOOL_NAMES[tool.name] = true
end
-- The notes' tools and then the app's: what a turn with ctx.appTools offers.
local TOOLS_WITH_APP: { any } = table.clone(TOOLS)
for _, tool in APP_TOOLS do
    table.insert(TOOLS_WITH_APP, tool)
end

-- For a model on the device that was not trained on the tools above: read_page also reads a long
-- page on from where a result cut short stopped (its results are shorter: MAX_DEVICE_RESULT).
local READ_FROM = {
    type = "integer",
    description = "Start at this character of the page (a result cut short says where to go on); default the start",
}
local function withPaging(tools: { any }): { any }
    local out = {}
    for _, tool in tools do
        if tool.name == "read_page" then
            local properties = table.clone(tool.input.properties)
            properties.from = READ_FROM
            local input = table.clone(tool.input)
            input.properties = properties
            local paged = table.clone(tool)
            paged.input = input
            table.insert(out, paged)
        else
            table.insert(out, tool)
        end
    end
    return out
end
local PAGED_TOOLS = withPaging(TOOLS)

-- … and a small one (b.aiModel().small, with the setting "fewerTools", off by default: LFM3 1.7B did
-- fewer tasks of the bench so, docs/PERFORMANCE.md) fewer of them, described in
-- fewer words: what its first question reads (some 2 500 tokens with all of them) before its
-- state is kept on disk, and what it has to choose from. No list of pages (a search finds them),
-- no extensions (a small model writes none that loads).
local COMPACT_TOOLS = {
    {
        name = "search_notes",
        description = "Full-text search of every note: words in any form. Returns matching lines with their page and ref (for update_block), and more of the first pages. Call it first for any question about the notes.",
        input = {
            type = "object",
            properties = { query = { type = "string", description = "Words to look for" } },
            required = { "query" },
        },
    },
    {
        name = "query_notes",
        description = "Finds tasks and reminders with a query; returns each one's page, status, dates, text and ref. Queries: overdue: type:tasks status:open due:overdue; due in the coming week: type:tasks status:open due:today..+7d; in progress: type:tasks status:doing; unfinished: type:tasks status:open; done this week: type:tasks status:done done:this-week; with a tag: type:tasks status:open tag:name; of a page: type:tasks status:open page:\"Title\"; a task by its words: type:tasks \"its words\"; reminders today: type:reminders remind:today.",
        input = {
            type = "object",
            properties = { query = { type = "string", description = "The query" } },
            required = { "query" },
        },
    },
    {
        name = "read_page",
        description = "Returns a page's Markdown text (a day by its date, 2026-11-12).",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The page's name, as in [[links]]" },
                from = READ_FROM,
            },
            required = { "name" },
        },
    },
    {
        name = "append_to_journal",
        description = "Adds Markdown to a day's journal note (today's by default): items \"- …\", tasks \"- [ ] … 📅 YYYY-MM-DD\", reminders \"- [ ] … ⏰ YYYY-MM-DD HH:MM\". The user approves it.",
        input = {
            type = "object",
            properties = {
                markdown = { type = "string", description = "What to add (Markdown)" },
                date = { type = "string", description = "The day, YYYY-MM-DD (default today)" },
            },
            required = { "markdown" },
        },
    },
    {
        name = "append_to_page",
        description = "Adds Markdown at the end of a page that exists: a page or list the user names. The user approves it.",
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
        name = "update_block",
        description = "Changes a task's status and/or its line's text, by its ref from search_notes or query_notes. The user approves it.",
        input = {
            type = "object",
            properties = {
                ref = { type = "string", description = "The block's ref" },
                text = { type = "string", description = "The line's new text, without its marker and checkbox; leave it out to change only the status" },
                status = { type = "string", enum = { "todo", "doing", "done", "cancelled" }, description = "A task's new status" },
            },
            required = { "ref" },
        },
    },
    {
        name = "write_page",
        description = "Creates a new page with its whole Markdown text. The user approves it.",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The page's name" },
                content = { type = "string", description = "The page's Markdown text" },
            },
            required = { "name", "content" },
        },
    },
    {
        name = "look_at_picture",
        description = "Looks at a picture of the notes (![[name.jpg]]) and answers a question about it: what it shows, the text on it.",
        input = {
            type = "object",
            properties = {
                name = { type = "string", description = "The picture's file name, as in ![[name.jpg]]" },
                question = { type = "string", description = "What to find out about it" },
            },
            required = { "name" },
        },
    },
}

-- What the model that answers gets (b.aiModel(): nil where none is set up — the command line;
-- `appTools`: ctx.appTools):
-- its tools, its instructions, how long a tool's result may be, and whether a result cut short
-- says how to read on.
type Serving = { tools: { any }, system: string, result: number, reference: number, paged: boolean, kind: string }

local function serving(appTools: boolean): Serving
    local model: any = b.aiModel()
    if model == nil or not model.onDevice or model.trained then
        -- A provider on the network, or the model trained on exactly this: as it always was — and
        -- the app's own tools after the notes' where the app runs this and the model is on the
        -- network (ctx.appTools: never the model on the device, docs/FINETUNE.md).
        if appTools and (model == nil or not model.onDevice) then
            return { tools = TOOLS_WITH_APP, system = W.system .. WA.rules, result = MAX_RESULT,
                reference = MAX_REFERENCE, paged = false, kind = "network" }
        end
        return { tools = TOOLS, system = W.system, result = MAX_RESULT, reference = MAX_REFERENCE, paged = false,
            kind = if model ~= nil and model.trained then "trained" else "network" }
    end
    if model.small and b.setting("fewerTools") == true then
        return { tools = COMPACT_TOOLS, system = W.systemShort, result = MAX_DEVICE_RESULT,
            reference = MAX_DEVICE_RESULT, paged = true, kind = "compact" }
    end
    return { tools = PAGED_TOOLS, system = W.system, result = MAX_DEVICE_RESULT, reference = MAX_RESULT, paged = true,
        kind = "device" }
end

local STATUS = { todo = " ", doing = "/", done = "x", cancelled = "-" }

local function str(v: any): string
    return if type(v) == "string" then v elseif v == nil then "" else tostring(v)
end

-- A task's status as a word: what a small model reads without knowing the checkboxes.
local TASK_WORDS = { [" "] = "open task", x = "done task", X = "done task", ["/"] = "task in progress",
    ["-"] = "cancelled task" }

-- A search's or a query's items as lines for the model.
local function itemLines(items: { any }): string
    local lines = {}
    for _, item in items do
        local parts = { "[[" .. str(item.page) .. "]]" }
        if item.line then
            table.insert(parts, "line " .. str(item.line))
        end
        if item.task then
            table.insert(parts, TASK_WORDS[str(item.task)] or ("task [" .. str(item.task) .. "]"))
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

-- The Obsidian Tasks dates (a time after a due, scheduled or start date too) and the block id that
-- b.replaceBlock keeps when the new text leaves them out.
local FIELDS = { "📅", "⏳", "🛫", "✅", "❌", "➕" }

local function hasFields(s: string): boolean
    for _, mark in FIELDS do
        if s:find(mark, 1, true) then
            return true
        end
    end
    return s:find("%s%^[%w%-]+%s*$") ~= nil
end

local function withoutFields(s: string): string
    for _, mark in FIELDS do
        s = s:gsub("%s*" .. mark .. "%s*%d%d%d%d%-%d%d%-%d%d%s+%d%d?:%d%d", ""):gsub("%s*" .. mark .. "%s*%d%d%d%d%-%d%d%-%d%d", "")
    end
    return (s:gsub("%s+%^[%w%-]+%s*$", ""))
end

local function squashed(s: string): string
    return trim((s:gsub("%s+", " ")))
end

-- Would a block whose line reads `old` read the same with the new text `new` (b.replaceBlock
-- keeping its dates and id when `new` has none)?
local function sameLine(new: string, old: string): boolean
    local wanted = squashed(new)
    return wanted == squashed(old) or (not hasFields(wanted) and wanted == squashed(withoutFields(old)))
end

-- A line's words, folded, as a task's line and a note of it are compared: without the marker and
-- the checkbox, the dates, the tags and the signs between words ("- [x] Kupić prezent dla mamy 📅
-- 2027-02-19 #auto (odhaczono)" → "kupic prezent dla mamy odhaczono").
local function lineWords(s: string): string
    local text = withoutFields((s:gsub("^%s*[-*+]%s+", ""):gsub("^%[.%]%s*", "")))
    local words = {}
    for word in b.fold(text):gsub("#%S+", " "):gmatch("%w+") do
        table.insert(words, word)
    end
    return table.concat(words, " ")
end

-- One question's agent: its yes to every change, the pages it changed, the page of the
-- conversation it answers in (the chat's: never one of its tools' finds — it matches the question
-- word for word, and a small model reads it instead of the notes; "" none), the reading calls made
-- since the last change, how many changes were made and the page of the last one, each change
-- made, by its call (changeKey), with its page (folded) and its result, the tasks changed with
-- update_block (their page, their text and its words: lineWords, before and after) and the user's
-- last message, folded; the lines update_block changed, by their ref after it, with its result
-- and what that call asked (its text and status); whether the last call met such a line again (`again`), a
-- change was declined, a change found its line so already, the answer was asked for again
-- (`nudged`); the changes refused as changing nothing, by changeKey.
type Made = { page: string, result: string }
type TaskChanged = { page: string, text: string, words: string }
type LineChanged = { result: string, asked: string }
type Session = { all: boolean, changed: { string }, conversation: string, calls: { [string]: boolean },
    writes: number, lastPage: string, made: { [string]: Made }, tasks: { TaskChanged }, asked: string,
    lines: { [string]: LineChanged }, again: boolean, declined: boolean, already: boolean, nudged: boolean,
    refused: { [string]: boolean }, appTools: boolean, app: { string } }

-- Is `name` the session's conversation: its path ("AI/2026-11-16 Co mam jutro", ".md" or not, a
-- ref's "#L…" after it) or, without a folder, its title (b.search names pages by their titles)?
local function isConversation(session: Session, name: string): boolean
    if session.conversation == "" or name == "" then
        return false
    end
    local own = b.fold(session.conversation)
    local page = b.fold((name:gsub("#.*$", ""):gsub("%.md$", "")))
    if page:find("/", 1, true) then
        return page == own
    end
    return page == (own:match("([^/]+)$") or own)
end

-- Does an item's line link to the conversation (the day's "🤖 [[AI/…]]", which holds the
-- question's words)?
local function linksToConversation(session: Session, item: any): boolean
    return session.conversation ~= "" and b.fold(str(item.text) .. " " .. str(item.excerpt))
        :find("[[" .. b.fold(session.conversation) .. "]]", 1, true) ~= nil
end

-- `items` (b.query's) without the conversation's own, nor a line that only links to it.
local function withoutConversation(session: Session, items: { any }): { any }
    local kept = {}
    for _, item in items do
        if not (isConversation(session, str(item.ref)) or (item.ref == nil and isConversation(session, str(item.page)))
            or linksToConversation(session, item)) then
            table.insert(kept, item)
        end
    end
    return kept
end

-- The first of `blocks` (b.blocks's), their children too, for which `wanted` is true.
local function findBlock(blocks: { any }, wanted: (any) -> boolean): any?
    for _, block in blocks do
        if wanted(block) then
            return block
        end
        local inner = findBlock(block.children or {}, wanted)
        if inner ~= nil then
            return inner
        end
    end
    return nil
end

-- The block `ref` names, as the page has it now (b.blocks's: its `text` the first line after the
-- marker and the checkbox, dates and tags in it, its `task`); nil when no block has that ref any
-- more (its line changed).
local function blockOf(ref: string): any?
    local path = ref:match("^(.-)#L%d+")
    if path == nil then
        return nil
    end
    return findBlock(b.blocks({ page = (path:gsub("%.md$", "")) }), function(block)
        return block.ref == ref
    end)
end

-- ---- Pages a model names --------------------------------------------------------------------

-- A page's name as a model may write it: in a link's brackets, with ".md" ("[[Zakupy]]").
local function pageName(name: string): string
    local page = trim(name):gsub("^%[%[", ""):gsub("%]%]$", ""):gsub("%.md$", "")
    return trim(page)
end

local function isDay(name: string): boolean
    return name:match("^%d%d%d%d%-%d%d%-%d%d$") ~= nil
end

-- The titles of the conversations with the AI, folded: the pages in AI/ (docs/SYNTAX.md §14),
-- neither pages to offer nor pages to add to.
local function conversationTitles(): { [string]: boolean }
    local titles = {}
    local ok, result = pcall(b.query, "type:pages path:AI/", 1000)
    if ok then
        for _, item in result.items do
            titles[b.fold(str(item.page))] = true
        end
    end
    return titles
end

-- Is the page `name` names (a title, a path) a conversation with the AI?
local function isAConversation(session: Session, name: string): boolean
    local page = pageName(name)
    if isConversation(session, page) or b.fold(page):sub(1, 3) == "ai/" then
        return true
    end
    local info = b.page(page)
    return info ~= nil and conversationTitles()[b.fold(str(info.name))] == true
end

-- Do two folded names share a word's beginning ("listy", "lista lektur")? Not by digits: "2026"
-- would make every day close to every other.
local function shareAWord(a: string, other: string): boolean
    for x in a:gmatch("%w+") do
        if #x >= 4 and x:sub(1, 4):find("%a") then
            for y in other:gmatch("%w+") do
                if #y >= 4 and x:sub(1, 4) == y:sub(1, 4) then
                    return true
                end
            end
        end
    end
    return false
end

-- The pages to offer for `name`, which names none, in brackets: those of a close name, or else some
-- of the notebook's — never a day of the journal (another day is no close name for a day: that is
-- append_to_journal's with its date) nor a conversation with the AI; "" no pages.
local function pagesFor(session: Session, name: string): string
    local conversations = conversationTitles()
    local wanted = b.fold(name)
    local close, some = {}, {}
    local more = false
    for _, page in b.pages() do
        local folded = b.fold(page)
        if not isDay(page) and not conversations[folded] and not isConversation(session, page) then
            if wanted ~= "" and #close < 5 and ((#folded >= 3 and wanted:find(folded, 1, true))
                or folded:find(wanted, 1, true) or shareAWord(folded, wanted)) then
                table.insert(close, page)
            end
            if #some < MAX_NAMED_PAGES then
                table.insert(some, page)
            else
                more = true
            end
        end
    end
    if #close > 0 then
        return " (pages with a close name: " .. table.concat(close, ", ") .. ")"
    elseif #some > 0 then
        return " (pages: " .. table.concat(some, ", ") .. (if more then ", …" else "") .. ")"
    end
    return ""
end

-- "No page named "X"", with the pages to offer instead.
local function noPage(session: Session, name: string): string
    local said = "No page named \"" .. name .. "\""
    if isDay(name) then
        return said .. " (the journal has no note of that day)."
    end
    return said .. pagesFor(session, name) .. "."
end

-- ---- What a model writes as a query -------------------------------------------------------------

local PAGE_KEYS = { page = true, strona = true }
local THIS_PAGE = { this = true, current = true, ta = true, ten = true, biezaca = true, tutaj = true }
local TAG_KEYS = { tag = true, tags = true, tagi = true }

local function tagInUse(tag: string): boolean
    local ok, result = pcall(b.query, "tag:" .. tag, 1)
    return ok and (tonumber(result.total) or 0) > 0
end

-- A query's tokens and the spaces between them, in order: a quoted phrase is one token, with the
-- filter it belongs to (page:"Moving house", "page:x in a phrase").
local function queryTokens(query: string): { string }
    local out = {}
    local i = 1
    while i <= #query do
        local space = query:match("^%s+", i)
        if space ~= nil then
            table.insert(out, space)
            i += #space
        else
            local j = i
            while j <= #query and not query:sub(j, j):find("%s") do
                if query:sub(j, j) == "\"" then
                    local close = query:find("\"", j + 1, true)
                    j = if close then close + 1 else #query + 1
                else
                    j += 1
                end
            end
            table.insert(out, query:sub(i, j - 1))
            i = j
        end
    end
    return out
end

-- A query as a small model writes it, read as the notebook answers it (docs/SYNTAX.md §10): a tag
-- asked for as a page (page:"#home" → tag:home), the user's full stop after a page's name
-- (page:"Dom." → page:"Dom"), a name that no page has but a tag in use has (page:"pilne" →
-- tag:pilne), a tag with its "#" (tag:#auto → tag:auto). Returns the query to run, what was read
-- otherwise (the result says so) and the pages it asks for that do not exist (not those it leaves
-- out: -page:).
local function readQuery(query: string): (string, { string }, { string })
    local notes, missing = {}, {}
    local function filter(neg: string, key: string, value: string, written: string): string?
        if TAG_KEYS[key:lower()] then
            local tag = trim(value)
            if tag:sub(1, 1) ~= "#" or trim(tag:sub(2)) == "" then
                return nil
            end
            tag = trim(tag:sub(2))
            local read = key .. ":" .. (if tag:find("[%s\"]") then "\"" .. tag .. "\"" else tag)
            table.insert(notes, written .. " read as " .. read)
            return neg .. read
        end
        if not PAGE_KEYS[key:lower()] then
            return nil
        end
        local name = trim(value)
        local bare = trim((name:gsub("[%.,;:!?]+$", "")))
        if name:sub(1, 1) == "#" then
            local tag = trim(bare:sub(2))
            if tag == "" or tag:find("%s") then
                return nil
            end
            table.insert(notes, written .. " read as tag:" .. tag .. ", a tag")
            return neg .. "tag:" .. tag
        end
        if name == "" or name:find("*", 1, true) or THIS_PAGE[b.fold(name)] or b.page(name) ~= nil then
            return nil
        end
        if bare ~= "" and bare ~= name and b.page(bare) ~= nil then
            table.insert(notes, written .. " read as " .. key .. ":\"" .. bare .. "\"")
            return neg .. key .. ":\"" .. bare .. "\""
        end
        if bare ~= "" and not bare:find("[%s\"]") and tagInUse(bare) then
            table.insert(notes, written .. " read as tag:" .. bare .. ": no page has that name, a tag has")
            return neg .. "tag:" .. bare
        end
        if neg == "" then
            table.insert(missing, if bare ~= "" then bare else name)
        end
        return nil
    end
    local parts = queryTokens(query)
    for k, token in parts do
        local neg, key, value = token:match("^(%-?)(%a+):\"([^\"]*)\"$")
        if neg == nil then
            neg, key, value = token:match("^(%-?)(%a+):([^\"]+)$")
        end
        if neg ~= nil then
            parts[k] = filter(neg, key, value, token:sub(#neg + 1)) or token
        end
    end
    return table.concat(parts), notes, missing
end

-- A query that fails or matches nothing, read once more as a small model meant it: the words of a
-- filter the query language does not have ("task:"Chłopi"", "words:"olej"" → "Chłopi", "olej"; a
-- bare "task:" left out, "task:tasks" → type:tasks), the label of the tool description's example
-- copied before it ("in progress: type:tasks status:doing"), OR and AND between its terms (every
-- filter and word must hold), a tag of joined words that no note has (tag:umówić-przegląd-pieca
-- → "umówić przegląd pieca") or the description's placeholder tag:name, a "?" or "." stuck to a
-- token or brackets around it, a filter or phrase given twice. Returns the query and what was read
-- otherwise; nil when there is nothing to read otherwise. (Only for a query that fails or finds
-- nothing as written: what a right query finds never changes.)
local WORD_KEYS = { task = true, tasks = true, word = true, words = true, query = true, keyword = true,
    keywords = true, phrase = true, search = true, title = true, name = true, zadanie = true, zadania = true,
    slowo = true, slowa = true, fraza = true, nazwa = true, tytul = true, szukaj = true }
local JOINS = { ["or"] = true, ["and"] = true, lub = true, albo = true }

local function looseQuery(query: string): (string?, { string })
    local tokens = {}
    for _, token in queryTokens(query) do
        if token:find("%S") then
            table.insert(tokens, token)
        end
    end
    local notes = {}
    local function note(said: string)
        for _, known in notes do
            if known == said then
                return
            end
        end
        table.insert(notes, said)
    end
    local hasType = false
    for _, token in tokens do
        if b.fold(token):match("^typ?e?:") then
            hasType = true
        end
    end
    -- The example's label, words up to one with a colon and nothing after it, before the query.
    local first = 1
    for k, token in tokens do
        if token:find("[\":#]") == nil and token:sub(1, 1) ~= "-" then
            continue
        end
        if k < #tokens and token:match("^[^\":#%-][^\":]*:$") and not WORD_KEYS[b.fold(token:sub(1, -2))] then
            local label = table.concat(tokens, " ", 1, k)
            note(label .. " left out, a label")
            first = k + 1
        end
        break
    end
    local out, seen = {}, {}
    for k = first, #tokens do
        local token = tokens[k]
        if token:sub(1, 1) ~= "\"" and not token:find("\"", 1, true) then
            local bare = token:gsub("^%(+", ""):gsub("[%)%?,;]+$", "")
            if bare:match("[^%.]%.$") then
                bare = bare:sub(1, -2) -- (a full stop, not a range's "..")
            end
            if bare ~= token and bare ~= "" then
                note(token .. " read as " .. bare)
                token = bare
            end
        end
        local neg, key, value = token:match("^(%-?)([^%s:\"]+):(.*)$")
        local folded = if key ~= nil then b.fold(key) else ""
        if JOINS[b.fold(token)] and k > first and k < #tokens then
            note(token .. " left out: every filter and word must hold")
            token = ""
        elseif key ~= nil and WORD_KEYS[folded] then
            local words = trim(value)
            if words == "" then
                note(token .. " left out")
                token = ""
            elseif neg == "" and (b.fold(words) == "tasks" or b.fold(words) == "task" or b.fold(words) == "zadania") then
                token = if hasType then "" else "type:tasks"
                note(tokens[k] .. (if hasType then " left out" else " read as type:tasks"))
                hasType = true
            else
                token = neg .. words
                note(tokens[k] .. " read as " .. token)
            end
        elseif key ~= nil and TAG_KEYS[folded] and neg == "" then
            local tag = trim((value:gsub("^\"", ""):gsub("\"$", ""):gsub("^#", "")))
            if b.fold(tag) == "name" and not tagInUse(tag) then
                note(tokens[k] .. " left out: no tag has that name")
                token = ""
            elseif b.fold(tag):find("%w[-_]+%w") and not tagInUse(tag) then
                local words = trim((tag:gsub("[-_]+", " ")))
                token = "\"" .. words .. "\""
                note(tokens[k] .. " read as " .. token .. ": no tag has that name")
            end
        end
        if token ~= "" then
            if seen[token] then
                if token:find(":", 1, true) then
                    note(token .. " given once")
                end
            else
                seen[token] = true
                table.insert(out, token)
            end
        end
    end
    if #notes == 0 or #out == 0 then
        return nil, notes
    end
    return table.concat(out, " "), notes
end

-- ---- What a search shows of a page ---------------------------------------------------------------

-- A search's words, folded, as a page's lines may have them in another form: a long word by its
-- beginning ("telefonu" → "telefo"), a short one whole ("tel"); none of one or two letters, nor a
-- word too common to tell lines apart.
type Stem = { text: string, prefix: boolean }

local STOP_WORDS: { [string]: boolean } = {}
for word in ("the and for are was has have had does did what when where which who how why with from that this "
    .. "your you not jak jaki jaka jakie gdzie kiedy czy nie sie jest mam moj moja moje mojego dla oraz albo lub "
    .. "ten tego ile kto ktory ktora przy pod nad jeszcze"):gmatch("%S+") do
    STOP_WORDS[word] = true
end

local function stems(query: string): { Stem }
    local out = {}
    for word in b.fold(query):gmatch("%w+") do
        if #word <= 2 or STOP_WORDS[word] then
            continue
        elseif #word > 4 then
            table.insert(out, { text = word:sub(1, math.max(4, #word - 2)), prefix = true })
        else
            table.insert(out, { text = word, prefix = false })
        end
    end
    return out
end

-- How many of the search's words a line has.
local function wordsIn(line: string, words: { Stem }): number
    local tokens = {}
    for token in b.fold(line):gmatch("%w+") do
        table.insert(tokens, token)
    end
    local n = 0
    for _, word in words do
        for _, token in tokens do
            if token == word.text or (word.prefix and token:sub(1, #word.text) == word.text) then
                n += 1
                break
            end
        end
    end
    return n
end

-- Up to `count` lines of `text` from its line `first` (from 1) on, within MAX_SCAN_BYTES of that
-- line's start, without their "\r": a long page is never split whole.
local function someLines(text: string, first: number, count: number): { string }
    local at = 1
    for _ = 2, first do
        local stop = text:find("\n", at, true)
        if stop == nil then
            return {}
        end
        at = stop + 1
    end
    local out = {}
    local limit = at + MAX_SCAN_BYTES
    while #out < count and at <= #text and at < limit do
        local stop = text:find("\n", at, true) or #text + 1
        table.insert(out, (text:sub(at, stop - 1):gsub("\r$", "")))
        at = stop + 1
    end
    return out
end

-- The last line of the frontmatter that a page's `lines` (from its first) start with; 0: none.
local function frontmatterEnd(lines: { string }): number
    if lines[1] == nil or trim(lines[1]) ~= "---" then
        return 0
    end
    for i = 2, #lines do
        if trim(lines[i]) == "---" then
            return i
        end
    end
    return 0
end

-- A page's line as a search shows more of the page: a task's checkbox in words, as a query's
-- result has it ("- [x] Buy milk" → "- done task: Buy milk"); its indentation kept, a child under
-- its parent.
local function shownLine(line: string): string
    local indent, marker, box, rest = line:match("^(%s*)([-*+])%s+%[(.)%]%s*(.-)%s*$")
    if indent == nil then
        return (line:gsub("%s+$", ""))
    end
    return indent .. marker .. " " .. (TASK_WORDS[box] or ("task [" .. box .. "]")) .. ": " .. rest
end

-- The text of a search hit's page, and its file ("journals/2026-11-11.md") when the hit has a ref.
local function hitPage(item: any): (string?, string?)
    local file = str(item.ref):match("^(.-)#L%d+")
    local text = (if file then b.read((file:gsub("%.md$", ""))) else nil) or b.read(str(item.page))
    return text, file
end

-- Is `line` one a search shows of a page: not blank, not the page's title (its folded name
-- `title`), not one that links to the conversation?
local function worthShowing(session: Session, line: string, title: string): boolean
    if not line:find("%S") then
        return false
    end
    local heading = line:match("^#%s+(.-)%s*$")
    if heading ~= nil and b.fold(heading) == title then
        return false
    end
    return session.conversation == "" or not line:find("[[", 1, true)
        or not b.fold(line):find("[[" .. b.fold(session.conversation) .. "]]", 1, true)
end

-- A search hit whose line links to the conversation (today's "🤖 [[AI/…]]" holds the question's
-- words, so the search finds it first) as its page's best other line with the search's `words`:
-- the day's own notes stay found. nil: the page has no other.
local function otherHit(session: Session, item: any, words: { Stem }): any?
    local text, file = hitPage(item)
    if text == nil or #words == 0 then
        return nil
    end
    local lines = someLines(text, 1, MAX_SCANNED)
    local title = b.fold(str(item.page))
    local best, most = nil, 0
    for k = frontmatterEnd(lines) + 1, #lines do
        if worthShowing(session, lines[k], title) then
            local n = wordsIn(lines[k], words)
            if n > most then
                best, most = k, n
            end
        end
    end
    if best == nil then
        return nil
    end
    local found: any = { page = item.page, date = item.date, line = best, excerpt = trim(lines[best]) }
    if file ~= nil then
        local prefix = file .. "#L" .. best .. "@"
        local block = findBlock(b.blocks({ page = (file:gsub("%.md$", "")) }), function(block)
            return str(block.ref):sub(1, #prefix) == prefix
        end)
        if block ~= nil then
            found.ref, found.text, found.task = block.ref, block.text, block.task
        end
    end
    return found
end

-- More of a search hit's page (b.search gives one line of a page, its best): a short page whole —
-- a person's page with the phone under "Vet", a day with both of its runs —, without its
-- frontmatter and title; of a longer one the lines after the hit's up to a blank line or a heading
-- (a heading's: the first of its section), and its other lines with the search's `words`, a child
-- with its parent. Tasks as words, never a line that links to the conversation; nil: nothing more.
local function hitContext(session: Session, item: any, words: { Stem }): string?
    local text = hitPage(item)
    if text == nil then
        return nil
    end
    local title = b.fold(str(item.page))
    if #text <= SHORT_PAGE_BYTES then
        local lines = someLines(text, 1, SHORT_PAGE_BYTES)
        local out = {}
        local filled = 0
        for i = frontmatterEnd(lines) + 1, #lines do
            if lines[i]:find("%S") then
                filled += 1
            end
            if worthShowing(session, lines[i], title) then
                table.insert(out, "    " .. shownLine(lines[i]))
            end
        end
        if filled <= SHORT_PAGE_LINES then
            if #out <= 1 then
                return nil -- (the hit's line is all there is)
            end
            return "  The whole page:\n" .. table.concat(out, "\n")
        end
    end
    local head = someLines(text, 1, MAX_SCANNED)
    local body = frontmatterEnd(head) + 1
    local at = tonumber(item.line)
    local picked: { [number]: string } = {}
    if at ~= nil and at >= body then
        local after = someLines(text, at, NEXT_LINES + 20) -- (the hit's line first)
        local k = 2
        if after[1] ~= nil and after[1]:match("^#+%s") then -- a heading: its section, after the blank lines
            while after[k] ~= nil and not after[k]:find("%S") do
                k += 1
            end
        end
        local count = 0
        while count < NEXT_LINES and after[k] ~= nil and after[k]:find("%S") and not after[k]:match("^#+%s") do
            if worthShowing(session, after[k], title) then
                picked[at + k - 1] = after[k]
                count += 1
            end
            k += 1
        end
    end
    local others = {}
    for k = body, if #words > 0 then #head else 0 do
        if k ~= at and picked[k] == nil and worthShowing(session, head[k], title) then
            local n = wordsIn(head[k], words)
            if n > 0 then
                table.insert(others, { line = k, words = n })
            end
        end
    end
    table.sort(others, function(x, y)
        return x.words > y.words or (x.words == y.words and x.line < y.line)
    end)
    for n = 1, math.min(#others, OTHER_LINES) do
        local k = others[n].line
        picked[k] = head[k]
        -- A child with its parent: the nearest line above it that is indented less.
        local indent = #(head[k]:match("^%s*") or "")
        local p = k - 1
        while indent > 0 and p >= body do
            if head[p]:find("%S") and #(head[p]:match("^%s*") or "") < indent then
                if p ~= at and picked[p] == nil and worthShowing(session, head[p], title) then
                    picked[p] = head[p]
                end
                break
            end
            p -= 1
        end
    end
    local numbers = {}
    for k in picked do
        table.insert(numbers, k)
    end
    if #numbers == 0 then
        return nil
    end
    table.sort(numbers)
    local out = {}
    for _, k in numbers do
        local line = shownLine(picked[k])
        local shortened = whole(line, MAX_CONTEXT_LINE)
        table.insert(out, "    line " .. k .. ": " .. shortened .. (if #shortened < #line then "…" else ""))
    end
    return "  More of the page:\n" .. table.concat(out, "\n")
end

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
    session.writes += 1
    session.lastPage = page
    local link = "[[" .. page .. "]]"
    for _, known in session.changed do
        if known == link then
            return
        end
    end
    table.insert(session.changed, link)
end

local DECLINED = "The user declined this change."

-- What of a journal entry repeats a task this question changed with update_block: a line that
-- starts with its words — a small model ticks a task off, then notes it in the day's journal too
-- ("- [x] Kupić prezent dla mamy (odhaczono)", the user's "Już to zrobiłem" read as "something
-- done → append_to_journal"), and the day gets a second, wrong copy of the task. Not when the
-- user's message speaks of the journal: then the note is asked for. nil: nothing repeated.
local JOURNAL_WORDS = { "%f[%w]dzienni", "journal", "diary", "notatk%a* dnia", "daily note", "day's note", "day’s note" }

local function repeatedTask(session: Session, markdown: string): TaskChanged?
    for _, pattern in JOURNAL_WORDS do
        if session.asked:find(pattern) then
            return nil
        end
    end
    for line in (markdown .. "\n"):gmatch("([^\n]*)\n") do
        local words = lineWords(line)
        for _, task in session.tasks do
            if words == task.words or words:sub(1, #task.words + 1) == task.words .. " " then
                return task
            end
        end
    end
    return nil
end

-- A reminder asked for ("przypomnij mi jutro o 8:30…", "remind me…") that a change writes as a
-- due date with a time ("📅 2026-11-12 08:30"), which rings nothing: written as the reminder
-- "⏰ 2026-11-12 08:30" instead (docs/SYNTAX.md §6) — a small model wrote reminders so. Only when
-- that is plain: the user's message asks for a reminder, not for none, and names no due date
-- (termin, due, deadline, 📅), and the change has no ⏰ of its own. Returns the text and the
-- reminders written so (none: the text as it was).
local REMIND_WORDS = { "%f[%w]przypomnij", "%f[%w]przypomnie", "%f[%w]przypominaj", "%f[%w]remind" }
local NO_REMIND_WORDS = { "%f[%w]nie przypomin", "%f[%w]nie przypomn", "%f[%w]bez przypomn", "don't remind",
    "don’t remind", "%f[%w]dont remind", "do not remind", "%f[%w]no reminder", "without a reminder" }
local DUE_WORDS = { "%f[%w]termin", "%f[%w]deadline", "%f[%w]due%f[%W]", "📅" }

local function asksForAReminder(session: Session): boolean
    local asked = false
    for _, pattern in REMIND_WORDS do
        asked = asked or session.asked:find(pattern) ~= nil
    end
    for _, pattern in NO_REMIND_WORDS do
        asked = asked and session.asked:find(pattern) == nil
    end
    for _, pattern in DUE_WORDS do
        asked = asked and session.asked:find(pattern) == nil
    end
    return asked
end

local function asReminders(session: Session, text: string): (string, { string })
    local written: { string } = {}
    if text:find("⏰", 1, true) or not text:find("📅", 1, true) or not asksForAReminder(session) then
        return text, written
    end
    local out = text:gsub("(📅%s*(%d%d%d%d%-%d%d%-%d%d)%s+(%d%d?):(%d%d))%f[%D]",
        function(due: string, date: string, hour: string, minute: string): string
            local h, m = tonumber(hour) or 99, tonumber(minute) or 99
            if h > 23 or m > 59 then
                return due -- (no time of day: it stays as it is)
            end
            local reminder = "⏰ " .. date .. " " .. string.format("%02d", h) .. ":" .. minute
            table.insert(written, reminder)
            return reminder
        end)
    return out, written
end

-- What a change's result adds when it wrote reminders so.
local function remindersNote(written: { string }): string
    if #written == 0 then
        return ""
    end
    return " The time went in as a reminder, " .. table.concat(written, ", ") .. ": 📅 is a due date and rings no alarm."
end

-- ---- Extensions ---------------------------------------------------------------------------------

-- (Read when first asked for: a few hundred kilobytes that most conversations never need.)
local REFERENCE: string? = nil
local function referenceText(): string
    if REFERENCE == nil then
        REFERENCE = str(b.data("reference"))
    end
    return REFERENCE :: string
end

type Heading = { level: number, title: string, line: number }

-- The reference, or the sections whose heading has `section`'s words (with their subsections).
local function reference(section: string): (string, boolean)
    local text = referenceText()
    if text == "" then
        return "The reference is not available.", true
    end
    local lines = {}
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
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
    -- What Luau's type checker found: a misspelt function of b, an unknown name (where the app
    -- has the checker; warnings: the code runs all the same, but they are mistakes to fix).
    if r.types and #r.types > 0 then
        local warnings = {}
        for i, t in r.types do
            if i > 5 then
                break
            end
            table.insert(warnings, "line " .. str(t.line) .. ": " .. str(t.message))
        end
        text ..= " Type check: " .. table.concat(warnings, "; ") .. (if #r.types > 5 then " …" else "") .. ". Fix them."
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

-- What the model is told of a call it made before, word for word: a read, a change made (its
-- result); and after half its steps.
local REPEATED = "The same call as before, with the same input: its result is above. Answer with what you have found, or try something else."
local MADE = "This change was made already, by the same call above (%s). Do not make it again: tell the user what you did."
local HURRY = "\n\n(Step %d of %d: answer soon with what you have found.)"
-- … a call that would change nothing on a line its own change made so (on the line's new ref: the
-- same change again, its status or text again — not "it is so already": a small model then told the
-- user nothing needed changing, right after it changed it) …
local MADE_LINE = "This line was changed already, by your call above (%s): it reads \"%s\" now. Do not change it again: tell the user what you did."
-- … and an answer that says a change was made when none was: asked for again, once, after this.
local NOT_YET = "\n\n(Nothing is changed yet: no tool made a change for this question. If the user asked for one, make it now with its tool; otherwise answer without saying that you changed anything.)"

-- The tools that only read: the same call twice gives the same result.
local READING = { search_notes = true, query_notes = true, list_pages = true, read_page = true,
    look_at_picture = true, extension_reference = true }
-- The tools that change notes: the same call twice makes the change twice (the same line added
-- again, a task's line written over with what it said before).
local CHANGING = { append_to_journal = true, append_to_page = true, write_page = true, update_block = true,
    write_extension = true }
-- (… and the app's own tools so.)
READING.app_settings = true
READING.app_commands = true
CHANGING.change_setting = true
CHANGING.run_app_command = true
CHANGING.set_theme = true
CHANGING.add_button = true
-- (A notification changes nothing — no confirmation, no "In the app" line —, and the same one twice
-- tells the user nothing new: the same call again, as a read.)
READING.notify_user = true

-- A call as a key: its tool and its input, the input's keys in order.
local function callKey(name: string, input: any): string
    local parts = { name }
    if type(input) == "table" then
        local keys = {}
        for k in input do
            table.insert(keys, str(k))
        end
        table.sort(keys)
        for _, k in keys do
            table.insert(parts, k .. "=" .. str(input[k]))
        end
    end
    return table.concat(parts, "\0")
end

-- An update_block's text and status as the tool takes them. A small model writes the line with its
-- marker and checkbox ("- [x] Buy milk"): the checkbox is the status (unless one is given), the
-- rest the text.
local function blockChange(input: any): (string, string?)
    local text = str(input.text)
    local status = if input.status ~= nil then STATUS[str(input.status)] else nil
    local box, rest = text:match("^%s*[-*+]?%s*%[([ xX/%-])%]%s*(.*)$")
    if box ~= nil then
        return rest or "", status or (if box == "X" then "x" else box)
    end
    return (text:gsub("^%s*[-*+]%s+", "")), status
end

-- Markdown as a change writes it, for comparing: one kind of line ending, no blank around it.
local function plain(s: string): string
    return trim((s:gsub("\r\n?", "\n")))
end

-- A due date with a time as the reminder asReminders writes for it, for comparing: the reminder
-- written so, then again as a reminder, is the same change.
local function sameTimes(s: string): string
    return (s:gsub("📅%s*(%d%d%d%d%-%d%d%-%d%d)%s+(%d%d?):(%d%d)%f[%D]", function(date: string, hour: string, minute: string): string
        return "⏰ " .. date .. " " .. string.format("%02d", tonumber(hour) or 0) .. ":" .. minute
    end))
end

-- A change as a key: what it would write where, as the tool takes it — a page by the page it names
-- ("[[Zakupy]]", "Zakupy.md", "zakupy": the same), today's journal when no day is given, the text
-- without the line endings and blanks around it, a due date with a time as a reminder — so that
-- the same change written a little otherwise is the same change.
local function changeKey(name: string, input: any): string
    input = if type(input) == "table" then input else {}
    local function page(): string
        local named = pageName(str(input.name))
        local info = b.page(named)
        return b.fold(if info ~= nil then str(info.name) else named)
    end
    local parts = { name }
    if name == "append_to_journal" then
        local date = trim(str(input.date))
        parts = { name, if date == "" then b.today() else date, sameTimes(plain(str(input.markdown))) }
    elseif name == "append_to_page" then
        parts = { name, page(), sameTimes(plain(str(input.markdown))) }
    elseif name == "write_page" or name == "write_extension" then
        parts = { name, page(), plain(str(input.content)) }
    elseif name == "update_block" then
        local text, status = blockChange(input)
        parts = { name, trim(str(input.ref)), sameTimes(squashed(text)), str(status) }
    else
        return callKey(name, input)
    end
    return table.concat(parts, "\0")
end

-- ---- The app's own tools (ctx.appTools) ------------------------------------------------------------

-- A setting's or a token's value as the model reads it: a text quoted, a list in brackets.
local function valueText(v: any): string
    if type(v) == "table" then
        local parts = {}
        for _, item in v do
            table.insert(parts, str(item))
        end
        return "[" .. table.concat(parts, ", ") .. "]"
    elseif type(v) == "string" then
        return "\"" .. v .. "\""
    end
    return str(v)
end

-- One setting of b.app.settings() as a line: key = value — label (group, kind), what it takes.
local function settingLine(s: any): string
    local line = str(s.key) .. " = " .. valueText(s.value) .. " — " .. str(s.label) .. " (" .. str(s.group) .. ", "
        .. str(s.kind) .. (if s.scope == "notebook" then ", the notebook's: every device of it" else "") .. ")"
    if type(s.options) == "table" and #s.options > 0 then
        local options = {}
        for _, o in s.options do
            table.insert(options, str(o))
        end
        line ..= "; one of: " .. table.concat(options, ", ")
    end
    if str(s.kind) == "number" and s.min ~= nil then
        line ..= "; " .. str(s.min) .. " … " .. str(s.max)
    end
    return line
end

-- Do the folded words of `words` (all of them) appear in `text`?
local function hasWords(text: string, words: string): boolean
    local hay = b.fold(text)
    for word in b.fold(words):gmatch("%S+") do
        if not hay:find(word, 1, true) then
            return false
        end
    end
    return true
end

-- The value `given` (as the model wrote it: a text, a number, a bool, a list) in the kind of the
-- setting `s`; nil when it is none of its values.
local function settingValue(s: any, given: any): any
    local kind = str(s.kind)
    if kind == "toggle" then
        if type(given) == "boolean" then
            return given
        end
        local word = b.fold(trim(str(given)))
        if word == "true" or word == "on" or word == "yes" or word == "tak" or word == "1" then
            return true
        elseif word == "false" or word == "off" or word == "no" or word == "nie" or word == "0" then
            return false
        end
        return nil
    elseif kind == "number" then
        return tonumber(given)
    elseif kind == "list" then
        if type(given) == "table" then
            return given
        end
        local items = {}
        for part in str(given):gmatch("[^,]+") do
            if trim(part) ~= "" then
                table.insert(items, trim(part))
            end
        end
        return items
    elseif kind == "choice" and type(s.options) == "table" then
        -- (An option as the model wrote it, letter case aside.)
        local wanted = b.fold(trim(str(given)))
        for _, o in s.options do
            if b.fold(str(o)) == wanted then
                return str(o)
            end
        end
        return nil
    end
    return trim(str(given))
end

-- A theme page's text (docs/SYNTAX.md §13 "Theme pages"): its tag, a sentence, the tokens as a
-- ```theme block — both modes' first, then light: and dark:, each in the order of their names.
local function themeLines(tokens: any, indent: string, out: { string })
    local names = {}
    for name in tokens do
        if name ~= "light" and name ~= "dark" then
            table.insert(names, str(name))
        end
    end
    table.sort(names)
    for _, name in names do
        local v = tokens[name]
        local text = str(v)
        if type(v) == "string" and (text:find("%s") or text:find("//", 1, true)) then
            text = "\"" .. text:gsub("\"", "") .. "\""
        end
        table.insert(out, indent .. name .. ": " .. text)
    end
end

local function themePageText(about: string, tokens: any): string
    local lines = { "---", "tags: [" .. WA.themeTag .. "]", "---", "", about, "", "```theme" }
    themeLines(tokens, "", lines)
    for _, mode in { "light", "dark" } do
        if type(tokens[mode]) == "table" and next(tokens[mode]) ~= nil then
            table.insert(lines, mode .. ":")
            themeLines(tokens[mode], "  ", lines)
        end
    end
    table.insert(lines, "```")
    return table.concat(lines, "\n") .. "\n"
end

-- Is the page named `page` a theme page (tagged theme or motyw)?
local function isThemePage(page: string): boolean
    local info = b.page(page)
    for _, tag in if info then info.tags else {} do
        if tag == "theme" or tag == "motyw" then
            return true
        end
    end
    return false
end

-- b.app.settings() or b.app.commands(), or the reason it has none ("" when it has).
local function appList(list: () -> { any }): ({ any }, string)
    local ok, result = pcall(list)
    if not ok then
        return {}, tostring(result)
    end
    return result, ""
end

-- What the app's change was, for the message's "⚙️ In the app: …" line.
local function appChanged(session: Session, what: string)
    session.writes += 1
    table.insert(session.app, what)
end

local BUTTON_PLACES = { toolbar = true, sidebar = true, statusbar = true, pageMenu = true, tabMenu = true,
    bottomNav = true }

-- Runs one of the app's tools (only offered with ctx.appTools): its result and whether it failed.
local function runAppTool(session: Session, name: string, input: any): (string, boolean)
    if name == "app_settings" then
        b.status(TA.settings)
        local words = trim(str(input.words))
        local settings, why = appList(b.app.settings)
        if why ~= "" then
            return why, true
        end
        local lines = {}
        for _, s in settings do
            if words == "" or hasWords(str(s.key) .. " " .. str(s.label) .. " " .. str(s.group), words) then
                table.insert(lines, settingLine(s))
            end
        end
        if #lines == 0 then
            return "No setting matches \"" .. words .. "\": call app_settings without words for all of them.", false
        end
        return table.concat(lines, "\n"), false
    elseif name == "change_setting" then
        local key = trim(str(input.key))
        local settings, why = appList(b.app.settings)
        if why ~= "" then
            return why, true
        end
        local setting = nil
        for _, s in settings do
            if s.key == key then
                setting = s
                break
            end
        end
        if setting == nil then
            return "No setting \"" .. key .. "\": app_settings lists them.", true
        end
        local value = settingValue(setting, input.value)
        if value == nil then
            return "Not changed: " .. valueText(input.value) .. " is no value of " .. settingLine(setting), true
        end
        if str(setting.kind) == "number" and setting.min ~= nil
            and (value < tonumber(setting.min) or value > tonumber(setting.max)) then
            return "Not changed: " .. str(value) .. " is outside " .. settingLine(setting), true
        end
        if valueText(value) == valueText(setting.value) then
            return "Not changed: " .. key .. " is " .. valueText(value) .. " already. Tell the user it is so.", false
        end
        if not confirm(session, string.format(TA.changeSetting, str(setting.label), valueText(setting.value),
            valueText(value))) then
            return DECLINED, true
        end
        local ok, err = pcall(b.app.set, key, value)
        if not ok then
            return "Not changed: " .. tostring(err), true
        end
        appChanged(session, key .. " → " .. valueText(value))
        return "Changed " .. key .. " (" .. str(setting.label) .. ") to " .. valueText(value) .. ".", false
    elseif name == "app_commands" then
        b.status(TA.commands)
        local words = trim(str(input.words))
        local commands, why = appList(b.app.commands)
        if why ~= "" then
            return why, true
        end
        local lines = {}
        for _, c in commands do
            if #lines >= 40 then
                break
            end
            local id = str(c.id)
            if c.enabled ~= false and id:sub(1, 13) ~= "ext.asystent." and hasWords(id .. " " .. str(c.title) .. " "
                .. str(c.subtitle), words) then
                local line = id .. " — " .. str(c.title)
                if str(c.subtitle) ~= "" then
                    line ..= " (" .. str(c.subtitle) .. ")"
                end
                if str(c.shortcut) ~= "" then
                    line ..= " [" .. str(c.shortcut) .. "]"
                end
                table.insert(lines, line)
            end
        end
        if #lines == 0 then
            return "No command of the app matches \"" .. words .. "\": try other words.", false
        end
        return table.concat(lines, "\n"), false
    elseif name == "run_app_command" then
        local id = trim(str(input.id))
        local commands, why = appList(b.app.commands)
        if why ~= "" then
            return why, true
        end
        local command = nil
        for _, c in commands do
            if c.id == id then
                command = c
                break
            end
        end
        if command == nil or id:sub(1, 13) == "ext.asystent." then
            return "No command \"" .. id .. "\": app_commands lists them.", true
        elseif command.enabled == false then
            return "The command \"" .. str(command.title) .. "\" cannot run now.", true
        end
        if not confirm(session, string.format(TA.runCommand, str(command.title))) then
            return DECLINED, true
        end
        local ok, err = pcall(b.app.run, id)
        if not ok then
            return tostring(err), true
        end
        appChanged(session, str(command.title))
        return "The command \"" .. str(command.title) .. "\" runs once this answer is written.", false
    elseif name == "set_theme" then
        local title = trim(str(input.name))
        if title == "" then
            return "A theme's name is needed.", true
        end
        local tokens: { [string]: any } = {}
        if type(input.tokens) == "table" then
            for k, v in input.tokens do
                tokens[str(k)] = v
            end
        end
        for _, mode in { "light", "dark" } do
            if type(input[mode]) == "table" and next(input[mode]) ~= nil then
                tokens[mode] = input[mode]
            end
        end
        if next(tokens) == nil then
            return "No tokens given: give the colours, radii or fonts to set (tokens, light, dark).", true
        end
        -- Checked as b.app.theme checks a theme (an unknown token, a wrong kind of value, one out of
        -- range), then taken back at once: the theme is the page's, picked for the notebook.
        local fine, err = pcall(b.app.theme, tokens)
        pcall(b.app.theme, nil)
        if not fine then
            return "Not written: " .. tostring(err):gsub("^.-b%.app%.theme: ", "") .. ". Fix the tokens and call it again.", true
        end
        local page = if title:find("/", 1, true) then title else WA.themeFolder .. title
        local before = b.read(page)
        if before ~= nil and not isThemePage(page) then
            return "[[" .. page .. "]] is a page that is no theme: choose another name.", true
        end
        local about = trim(str(input.description))
        local content = themePageText(if about ~= "" then about else WA.themeAbout, tokens)
        local what = string.format(if before == nil then TA.createTheme else TA.replaceTheme, title, preview(content))
        if not confirm(session, what) then
            return DECLINED, true
        end
        local ok, written = pcall(b.write, page, content)
        if not ok then
            return tostring(written), true
        end
        noteChanged(session, page)
        local path = (page:gsub("%.md$", "")) .. ".md"
        local picked, why = pcall(b.app.set, "themePage", path)
        if not picked then
            return "Wrote the theme page [[" .. page .. "]], but it could not be picked (" .. tostring(why)
                .. "): tell the user to pick it in Settings → Appearance → Themes.", false
        end
        table.insert(session.app, "themePage → " .. path)
        return "Wrote the theme [[" .. page .. "]] and picked it for this notebook (Settings → Appearance → Themes lists it).", false
    elseif name == "add_button" then
        local label = trim(str(input.label)):gsub("%s+", " ")
        local place = trim(str(input.place))
        if place == "" then
            place = "toolbar"
        end
        local icon = trim(str(input.icon))
        if icon == "" then
            icon = "star"
        end
        local command, page = trim(str(input.command)), trim(str(input.page))
        if label == "" then
            return "A button's label is needed.", true
        elseif not BUTTON_PLACES[place] then
            return "No place \"" .. place .. "\": toolbar, sidebar, statusbar, pageMenu, tabMenu or bottomNav.", true
        elseif (command == "") == (page == "") then
            return "Give either the command it runs (an id from app_commands) or the page it opens.", true
        end
        if command ~= "" then
            local commands, why = appList(b.app.commands)
            if why ~= "" then
                return why, true
            end
            local known = false
            for _, c in commands do
                if c.id == command then
                    known = true
                    break
                end
            end
            if not known then
                return "No command \"" .. command .. "\": app_commands lists them.", true
            end
        end
        label = whole(label, 100)
        local what = if command ~= "" then string.format("run = %q", command) else string.format("page = %q", page)
        local code = string.format("b.app.contribute(%q, { id = \"button\", label = %q, icon = %q, %s })", place, label, icon, what)
        local about = string.format(WA.buttonAbout, label)
        local content = table.concat({ "---", "tags: [extension]", "permissions: [app]",
            "description: " .. (about:gsub("[\r\n]", " ")), "---", "", about, "", "```luau", code, "```", "" }, "\n")
        local pageName = WA.buttonFolder .. WA.buttonPrefix .. (label:gsub("[/\\#%[%]|^:]", " "))
        local before = b.read(pageName)
        if before ~= nil and not isExtensionPage(pageName) then
            return "[[" .. pageName .. "]] is a page that is no extension: give the button another label.", true
        end
        local report, failed = checkReport(b.checkExtension(pageName, content))
        if failed then
            return "Not written. " .. report, true
        end
        if not confirm(session, string.format(TA.createButton, label, pageName, preview(content))) then
            return DECLINED, true
        end
        local ok, err = pcall(b.write, pageName, content)
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, pageName)
        return "Wrote the extension [[" .. pageName .. "]] with the button. It shows once the user presses Run on its page (or in Settings → Extensions): tell them.", false
    elseif name == "notify_user" then
        local text = trim(str(input.text))
        if text == "" then
            return "A notification's text is needed.", true
        end
        b.status(TA.notifying)
        local options: { [string]: string } = {}
        if trim(str(input.title)) ~= "" then
            options.title = trim(str(input.title))
        end
        if pageName(str(input.page)) ~= "" then
            options.page = pageName(str(input.page))
        end
        local shown, why = b.notify(text, options)
        if not shown then
            -- (The app's no is no failure of the model's: it says so to the user.)
            return "Not notified: " .. str(why) .. ".", false
        end
        return "Notified.", false
    end
    return "No tool named \"" .. name .. "\".", true
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
        b.status(string.format(T.searching, query))
        local words = stems(query)
        local hits = {}
        for _, hit in b.search(query, limit + 2) do
            if #hits >= limit then
                break
            elseif isConversation(session, str(hit.ref)) or (hit.ref == nil and isConversation(session, str(hit.page))) then
                continue -- (the conversation itself)
            elseif linksToConversation(session, hit) then
                -- The day's line that links to the conversation holds the question's words: the
                -- day's own notes are what was looked for.
                local other = otherHit(session, hit, words)
                if other ~= nil then
                    table.insert(hits, other)
                end
            else
                table.insert(hits, hit)
            end
        end
        if #hits == 0 then
            return "Nothing found for \"" .. query .. "\".", false
        end
        -- Each hit with more of its page under it (the first ones, as long as they fit): what a
        -- small model asks for is often the line after the one that matched — the phone under "Vet".
        local shown = {}
        local budget = CONTEXT_BYTES
        for i, hit in hits do
            local line = itemLines({ hit })
            if i <= CONTEXT_HITS then
                local more = hitContext(session, hit, words)
                if more ~= nil and #more + 1 <= budget then
                    line ..= "\n" .. more
                    budget -= #more + 1
                end
            end
            table.insert(shown, line)
        end
        return table.concat(shown, "\n"), false
    elseif name == "query_notes" then
        local asked, notes, missing = readQuery(trim(str(input.query)))
        b.status(string.format(T.querying, asked))
        local ok, result = pcall(b.query, asked, 60)
        if not ok or #withoutConversation(session, result.items) == 0 then
            -- Read once more as a small model meant it; only what then matches is said so.
            local loose, read = looseQuery(asked)
            if loose ~= nil then
                local found, other = pcall(b.query, loose, 60)
                if found and #withoutConversation(session, other.items) > 0 then
                    asked, ok, result = loose, found, other
                    for _, said in read do
                        table.insert(notes, said)
                    end
                end
            end
        end
        if not ok then
            return tostring(result), true
        end
        local items = withoutConversation(session, result.items)
        local total = tonumber(result.total) or #items
        total -= #result.items - #items
        local said = if #notes > 0 then "(" .. table.concat(notes, "; ") .. ".)\n" else ""
        if #items == 0 then
            -- A page that does not exist matches nothing: said so, with the pages there are.
            local why = if #missing > 0 then noPage(session, missing[1]) .. " " else ""
            return said .. why .. "Nothing matches \"" .. asked .. "\".", false
        end
        return said .. str(total) .. (if total == 1 then " match" else " matches") .. " of \"" .. asked .. "\""
            .. (if total > #items then ", the first " .. #items else "") .. ":\n" .. itemLines(items), false
    elseif name == "list_pages" then
        local tag = trim(str(input.tag))
        b.status(T.listing)
        local pages = {}
        for _, page in b.pages(if tag ~= "" then { tag = tag } else {}) do
            if not isConversation(session, page) then
                table.insert(pages, page)
            end
        end
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
        if isConversation(session, trim(str(input.name))) then
            return "That page is this conversation itself: its messages are the ones above. Look in the other notes.", true
        end
        b.status(string.format(T.reading, trim(str(input.name))))
        local text = b.read(str(input.name))
        if text == nil then
            return "No page named \"" .. str(input.name) .. "\".", true
        end
        -- (A long page read on from where a result cut short stopped: `from` characters in.)
        local from = math.max(0, math.floor(tonumber(input.from) or 0))
        if from > 0 then
            local at = utf8.offset(text, from + 1)
            if at == nil or at > #text then
                return string.format("The page has %d characters: nothing from %d on.", utf8.len(text) or #text, from), true
            end
            text = text:sub(at)
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
        local task = repeatedTask(session, markdown)
        if task ~= nil then
            return "Not added: this is the task changed above in [[" .. task.page .. "]] (\"" .. task.text
                .. "\"), whose line says it already. Tell the user what you changed.", true
        end
        local reminders
        markdown, reminders = asReminders(session, markdown)
        if not confirm(session, string.format(T.appendDay, date, preview(markdown))) then
            return DECLINED, true
        end
        local ok, err = pcall(b.appendToJournal, date, markdown)
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, date)
        return "Added to the journal of " .. date .. "." .. remindersNote(reminders), false
    elseif name == "append_to_page" then
        local page = pageName(str(input.name))
        local markdown = trim(str(input.markdown))
        if page == "" or markdown == "" then
            return "A page's name and what to add are needed.", true
        end
        -- A page or list the user names is one the notebook has: a new page here is a small
        -- model's guess at a list's name (the book's title for "add it to my reading list"). A
        -- day is append_to_journal's; a conversation with the AI is the chat's.
        if isDay(page) then
            local said = if b.page(page) == nil then noPage(session, page) .. " " else ""
            return said .. "A day's journal is append_to_journal's, with its date.", true
        elseif isAConversation(session, page) then
            return "[[" .. page .. "]] is a conversation with the AI, not a page to add to"
                .. pagesFor(session, page) .. ".", true
        elseif b.page(page) == nil then
            return noPage(session, page) .. " append_to_page adds to a page that exists: when the user meant one of them, add to it; otherwise ask the user which page.", true
        end
        local reminders
        markdown, reminders = asReminders(session, markdown)
        if not confirm(session, string.format(T.appendPage, page, preview(markdown))) then
            return DECLINED, true
        end
        local before = b.read(page) or "" -- (read after the yes: what came meanwhile stays)
        local separator = if before == "" or before:sub(-1) == "\n" then "" else "\n"
        local ok, err = pcall(b.write, page, before .. separator .. markdown .. "\n")
        if not ok then
            return tostring(err), true
        end
        noteChanged(session, page)
        return "Added to [[" .. page .. "]]." .. remindersNote(reminders), false
    elseif name == "write_page" then
        local page = trim(str(input.name))
        local content = str(input.content)
        if page == "" then
            return "A page's name is needed.", true
        end
        local before = b.read(page)
        local what = if before == nil then string.format(T.createPage, page, preview(content))
            else string.format(T.replacePage, page, utf8.len(before) or #before, preview(content))
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
        if ref == "" then
            return "A block's ref is needed.", true
        end
        if input.status ~= nil and STATUS[str(input.status)] == nil then
            return "Unknown status \"" .. str(input.status) .. "\".", true
        end
        local text, status = blockChange(input)
        local asked = squashed(text) .. "\0" .. str(status) -- (what the call asks, as a line's change keeps it)
        local reminders
        text, reminders = asReminders(session, text)
        -- No text: the line keeps its own, only the status changes.
        local current = blockOf(ref)
        if trim(text) == "" then
            if status == nil then
                return "Nothing to change: give the line's new text, a status, or both.", true
            end
            if current == nil then
                return "stale: the block changed since it was read (" .. ref .. "): search again.", true
            end
            text = str(current.text)
        end
        -- A change that changes nothing: the line already says it (a task ticked off again).
        local task = if current ~= nil and current.task == "X" then "x" elseif current ~= nil then current.task else nil
        if current ~= nil and sameLine(text, str(current.text)) and (status == nil or status == task) then
            local box = if task ~= nil then "[" .. str(task) .. "] " else ""
            local own = session.lines[ref]
            if own ~= nil then
                -- The line reads so because this question changed it: the change is made.
                session.again = true
                if own.asked == asked then
                    return string.format(MADE, own.result), false
                end
                return string.format(MADE_LINE, own.result, box .. str(current.text)), false
            end
            session.already = true
            local kind = if task ~= nil then " (" .. (TASK_WORDS[str(task)] or "task") .. ")" else ""
            local reads = "the line already reads \"" .. box .. str(current.text) .. "\"" .. kind
            if status == "x" or status == "-" then
                -- The status asked for is the line's: a task ticked off again.
                return "Not changed: " .. reads .. ", the status given. Tell the user it is so already.", true
            elseif status ~= nil then
                -- Open again, in progress again: maybe all that was asked, maybe a move without its date.
                return "Not changed: " .. reads .. ", the status given, and the text is the line's own. If that"
                    .. " status is all the user asked for, tell them it is so already; to move the task, give its text"
                    .. " with the new date (📅 YYYY-MM-DD).", true
            elseif task ~= nil then
                -- No status and the line's own text: a small model left out what changes.
                return "Not changed: the call gives no status, and its text is the line's own: " .. reads
                    .. ". To change the task, give what changes: its status (todo, doing, done, cancelled), or its"
                    .. " text with the new date (📅 YYYY-MM-DD).", true
            end
            return "Not changed: the call's text is the line's own: " .. reads .. ". To change it, give its new text.", true
        end
        local page = pageOfRef(ref)
        local shown = if status ~= nil then "[" .. status .. "] " .. text else text
        if not confirm(session, string.format(T.changeBlock, page, preview(shown))) then
            return DECLINED, true
        end
        local ok, result = pcall(b.replaceBlock, ref, text, status)
        if not ok then
            return tostring(result), true
        end
        noteChanged(session, page)
        if task ~= nil and current ~= nil then
            -- A task changed: no journal entry of it after this (repeatedTask).
            local before = lineWords(str(current.text))
            for i, said in { str(current.text), text } do
                local words = lineWords(said)
                if words ~= "" and (i == 1 or words ~= before) then
                    table.insert(session.tasks, { page = page, text = trim(withoutFields(said)), words = words })
                end
            end
        end
        local changed = "Changed; its ref is now " .. str(result) .. "." .. remindersNote(reminders)
        session.lines[str(result)] = { result = changed, asked = asked }
        return changed, false
    elseif name == "look_at_picture" then
        -- The name as notes write it: without the embed's brackets and its size ("![[a.jpg|300]]").
        local picture = trim(str(input.name)):gsub("^!?%[%[", ""):gsub("%]%]$", ""):gsub("|.*$", "")
        picture = trim(picture)
        if picture == "" then
            return "A picture's name is needed.", true
        end
        local question = trim(str(input.question))
        if question == "" then
            question = W.lookAsk
        end
        b.status(string.format(T.looking, picture))
        -- A request of its own, without the tools: the model's answer about the picture is the
        -- result (text, for any provider). "low": no long thinking over a picture.
        local ok, seen = pcall(b.ai, { prompt = question, images = { picture }, maxTokens = 1500, effort = "low" })
        if not ok then
            return tostring(seen), true
        end
        seen = trim(str(seen))
        return if seen == "" then "(The model said nothing about the picture.)" else seen, false
    elseif name == "extension_reference" then
        b.status(T.referencing)
        return reference(trim(str(input.section)))
    elseif name == "check_extension" then
        local page = trim(str(input.name))
        if page == "" then
            return "A page's name is needed.", true
        end
        b.status(string.format(T.checking, page))
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
        local what = string.format(if before == nil then T.createExtension else T.replaceExtension, page, preview(content))
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
    elseif session.appTools and APP_TOOL_NAMES[name] then
        return runAppTool(session, name, input)
    end
    return "No tool named \"" .. name .. "\".", true
end

-- ---- An answer that says a change was made --------------------------------------------------------

-- Does the answer start by saying it made a change ("Odhaczyłam „Zły” w [[Książki]].", "Jasne,
-- dodałem mleko.", "I've added milk…")? Its first words only — after a word like "Gotowe!" or
-- "Jasne," — in the first person: never a quote or a list's item, which may be a note of the
-- user's own ("- Zapisałam się na basen"), nor a word after "nie".
local CLAIMS: { [string]: boolean } = {}
for stem in ("dodal dopisal odhaczyl oznaczyl zaznaczyl zapisal zanotowal przelozyl ustawil zmienil utworzyl "
    .. "stworzyl wpisal uzupelnil zaktualizowal"):gmatch("%S+") do
    CLAIMS[stem .. "am"] = true
    CLAIMS[stem .. "em"] = true
end
for word in ("przesunelam przesunalem przenioslam przenioslem added"):gmatch("%S+") do
    CLAIMS[word] = true
end
local CLAIMS_EN: { [string]: boolean } = {} -- after "I", "I've", "I have"
for word in ("added marked noted saved ticked crossed moved set created updated changed put scheduled rescheduled "
    .. "logged recorded appended"):gmatch("%S+") do
    CLAIMS_EN[word] = true
end
local CLAIM_FILLERS: { [string]: boolean } = {}
for word in ("ok okej okay dobrze jasne gotowe juz wlasnie super swietnie oczywiscie sure done great alright "
    .. "certainly"):gmatch("%S+") do
    CLAIM_FILLERS[word] = true
end
local BEFORE_EN = { ve = true, have = true, also = true, just = true, now = true, already = true }
-- What may come before the first word: spaces, emphasis, a heading's hashes and these signs.
local LEAD_SIGNS = { "✅", "✔️", "✔", "☑️", "👍", "👌", "📝", "✏️", "⏰", "🎉" }

local function claimsAChange(answer: string): boolean
    local s = answer
    repeat
        local before = s
        s = s:gsub("^[%s%*_#]+", "")
        for _, sign in LEAD_SIGNS do
            if s:sub(1, #sign) == sign then
                s = s:sub(#sign + 1)
            end
        end
    until s == before
    if not s:match("^%a") then
        return false -- (a quote, a list's item, a number: nothing the answer says it did)
    end
    local words = {}
    for word in b.fold(whole(s, 200)):gmatch("%w+") do
        table.insert(words, word)
        if #words >= 6 then
            break
        end
    end
    local k = if CLAIM_FILLERS[words[1] or ""] then 2 else 1
    local first = words[k] or ""
    if CLAIMS[first] then
        return true
    elseif first ~= "i" then
        return false
    end
    k += 1
    while BEFORE_EN[words[k] or ""] do
        k += 1
    end
    return CLAIMS_EN[words[k] or ""] == true
end

-- Is the user's message a question only ("Co mam jutro?", "Did I pay the rent?"), not a request
-- ("Możesz odhaczyć …?", "Czy możesz dopisać …?")? By its first word.
local QUESTION_WORDS: { [string]: boolean } = {}
for word in ("co czy jaki jaka jakie jakich jakiej jakim ile kiedy gdzie kto komu ktory ktora ktore ktorych jak "
    .. "dlaczego czemu what which when where who whom whose how why did do does is are was were have has had"):gmatch("%S+") do
    QUESTION_WORDS[word] = true
end
local ASKS_TO_DO = { mozesz = true, moglabys = true, moglbys = true, mozna = true, dasz = true, dalabys = true,
    dalbys = true }

local function asksOnly(session: Session): boolean
    local first, second = session.asked:match("^(%w+)%W*(%w*)")
    if first == nil or not QUESTION_WORDS[first] then
        return false
    end
    return not (first == "czy" and ASKS_TO_DO[second]) -- ("Czy możesz dopisać …?" asks for a change)
end

-- Did the conversation's last answer before this question change notes (its "✏️ Changed: …")?
-- An answer may then speak of that change.
local function changedBefore(history: { any }): boolean
    for i = #history - 1, 1, -1 do
        if str(history[i].role) == "assistant" then
            return str(history[i].content):find("✏️", 1, true) ~= nil
        end
    end
    return false
end

-- Should the model's answer `said` be asked for again: it says a change was made, and in this
-- question none was — none made, none declined, none that found its line so already —, nor was it
-- asked for again before?
local function claimsUnmade(session: Session, history: { any }, said: string): boolean
    if session.nudged or session.writes > 0 or session.declined or session.already then
        return false
    end
    return claimsAChange(said) and not asksOnly(session) and not changedBefore(history)
end

-- ---- Commands -----------------------------------------------------------------------------------

-- One turn of a conversation of the chat: ctx.messages ({role, content, images?}, the user's last),
-- ctx.page (the page it is about, "" none: its text goes along before the first message, read again
-- each turn), ctx.conversation (the conversation's own page, which its tools do not find),
-- ctx.omitted (the earlier messages left out: they do not fit the model). Returns the AI's message:
-- its answer, the pages it changed, a note on a page cut short.
b.command("chat", function(ctx)
    local history = ctx.messages
    if type(history) ~= "table" or #history == 0 then
        return T.chatOnly -- (run by hand: there is no conversation to answer)
    end
    local page: string = ctx.page or ""
    local text, wasCut = pageText(page, MAX_PAGE)
    -- The day goes in the first message, not in the system text: the system text and the tools
    -- stay the same from day to day, and a model on the device keeps what it read of them on
    -- disk (docs/EXTENSIONS.md "AI"). The page the conversation is about goes there too.
    -- A conversation too long for the model came without its first turns (ctx.omitted of them):
    -- the model is told so, before the rest.
    local omitted = if type(ctx.omitted) == "number" then ctx.omitted else 0
    local messages: { any } = {}
    for i, m in history do
        local content = str(m.content)
        if i == 1 then
            content = todayLine() .. (if omitted > 0 then string.format(W.omitted, omitted) else "")
                .. (if text ~= "" then string.format(W.about, page, text) else "") .. content
        end
        local message: any = { role = if str(m.role) == "assistant" then "assistant" else "user", content = content }
        -- The pictures sent with a user's message (the chat's attachments): the model sees them.
        if message.role == "user" and type(m.images) == "table" and #m.images > 0 then
            message.images = m.images
        end
        table.insert(messages, message)
    end
    local session: Session = { all = false, changed = {}, conversation = str(ctx.conversation), calls = {},
        writes = 0, lastPage = "", made = {}, tasks = {}, asked = b.fold(str(history[#history].content)),
        lines = {}, again = false, declined = false, already = false, nudged = false, refused = {},
        appTools = ctx.appTools == true, app = {} }
    -- The app's own tools only where the app runs this and the model is on the network (ctx.appTools):
    -- the model on the device gets the tools and the system text it was trained on (docs/FINETUNE.md).
    local served = serving(session.appTools)
    local system: string = served.system
    local answer: string? = nil
    local idle = 0 -- turns in a row, after a change, whose every call only repeated what was done
    for round = 1, MAX_ROUNDS do
        b.status("") -- (the model thinks: the chat's dots alone)
        local said, reply = b.ai({ system = system, messages = messages, tools = served.tools, maxTokens = 8000,
            thoughts = true })
        table.insert(messages, { role = "assistant", raw = reply.raw })
        if not reply.toolCalls then
            if round < MAX_ROUNDS and claimsUnmade(session, history, str(said)) then
                -- A small model said "Odhaczyłam …" and called nothing: asked once more, without
                -- that answer, the message it answered saying that nothing is changed yet (as a
                -- late step's result says to answer soon).
                session.nudged = true
                table.remove(messages)
                local last = messages[#messages]
                if last.results ~= nil and #last.results > 0 then
                    last.results[#last.results].content ..= NOT_YET
                else
                    last.content = str(last.content) .. NOT_YET
                end
                continue
            end
            answer = said
            break
        end
        -- Every call of the turn, their results in one message.
        local results = {}
        local repeats = 0 -- calls that only repeat: a read or a change made, a change refused again
        for _, call in reply.toolCalls do
            local result, failed
            local key = callKey(str(call.name), call.input)
            local change = if CHANGING[str(call.name)] then changeKey(str(call.name), call.input) else nil
            if READING[str(call.name)] and session.calls[key] then
                -- A small model asks the same again and again, in circles until its steps are up.
                result, failed = REPEATED, false
                repeats += 1
            elseif change ~= nil and session.made[change] ~= nil then
                -- … and makes the same change again (sixteen times the same reminder). A change
                -- declined is no change made: tried again, the user is asked again.
                result, failed = string.format(MADE, session.made[change].result), false
                repeats += 1
            else
                session.calls[key] = true
                session.again = false
                local writes = session.writes
                result, failed = runTool(session, str(call.name), call.input)
                if result == DECLINED then
                    session.declined = true
                end
                if session.again then
                    repeats += 1 -- (the line this question changed, the change again)
                elseif change ~= nil and failed and (result:sub(1, 12) == "Not changed:" or result:sub(1, 10) == "Not added:") then
                    if session.refused[change] then
                        repeats += 1
                    end
                    session.refused[change] = true
                end
                if session.writes > writes then
                    session.calls = { [key] = true } -- (the notes changed: what was read may read otherwise now)
                    if change ~= nil then
                        -- What was made on that page before may be undone now: made again, it
                        -- is a change again.
                        local info = b.page(session.lastPage)
                        local page = b.fold(if info ~= nil then str(info.name) else session.lastPage)
                        for made, was in session.made do
                            if was.page == page then
                                session.made[made] = nil
                            end
                        end
                        session.made[change] = { page = page, result = result }
                    end
                end
            end
            local limit = if str(call.name) == "extension_reference" then served.reference else served.result
            local content = if served.paged then cutPaged(result, limit, str(call.name), call.input) else cut(result, limit)
            table.insert(results, { id = call.id, content = content, isError = failed })
        end
        -- After a change, a small model may call it again and again, each result saying it is made
        -- (the same reminder fifteen times, the task ticked off again), until its steps are up, and
        -- never answer: after two such turns the question ends here, with the change made.
        idle = if session.writes > 0 and repeats > 0 and repeats == #results then idle + 1 else 0
        if idle >= MAX_IDLE then
            answer = W.done
            break
        end
        if round >= MAX_ROUNDS // 2 and #results > 0 then
            local last = results[#results]
            last.content ..= string.format(HURRY, round, MAX_ROUNDS)
        end
        table.insert(messages, { role = "user", results = results })
    end
    b.status("")
    local said = if answer == nil then string.format(W.tooLong, MAX_ROUNDS) else trim(answer)
    local parts = { if said == "" then "…" else said }
    if #session.changed > 0 then
        table.insert(parts, string.format(W.changed, table.concat(session.changed, ", ")))
    end
    if #session.app > 0 then
        table.insert(parts, string.format(WA.changed, table.concat(session.app, ", ")))
    end
    if wasCut and #history == 1 then
        table.insert(parts, string.format(W.cut, page, MAX_PAGE))
    end
    return table.concat(parts, "\n\n")
end, { label = T.chat, palette = false, chat = true })
