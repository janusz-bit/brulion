--!strict
-- Asystent AI (AI assistant): asks the model the user chose in Settings → Extensions → AI about
-- the page on screen and writes the answer into today's daily note, under the question, as an
-- outline — readable on a phone and kept with the notes. Written with the extensions' public API
-- only (b.ai, docs/EXTENSIONS.md "AI"); its source can become a page of the notebook and be
-- changed there, like any built-in extension.
--
-- Commands: ask (a question about the page on screen, or any question without one), summarize
-- (the page on screen in a few sentences).

local MAX_PAGE = 60000 -- characters of a page sent along (b.ai takes at most 200 000)

local WRITTEN = {
    pl = {
        question = "🤖 %s",
        about = " ([[%s]])",
        summary = "🤖 Streszczenie [[%s]]",
        system = "Jesteś asystentem w aplikacji do notatek. Odpowiadasz zwięźle, po polsku, chyba że notatka jest w innym języku. Piszesz zwykłym tekstem albo krótkimi punktami.",
        noteIntro = "Notatka „%s”:\n\n%s\n\n",
        summarizeAsk = "Streść tę notatkę w 3–5 zdaniach. Na końcu wypisz najważniejsze zadania, jeśli jakieś są.",
    },
    en = {
        question = "🤖 %s",
        about = " ([[%s]])",
        summary = "🤖 Summary of [[%s]]",
        system = "You are an assistant in a notes app. You answer briefly, in English unless the note is in another language. You write plain text or short points.",
        noteIntro = "The note \"%s\":\n\n%s\n\n",
        summarizeAsk = "Summarise this note in 3–5 sentences. At the end list the most important tasks, if there are any.",
    },
}

local INTERFACE = {
    pl = {
        ask = "AI: zapytaj o tę stronę",
        summarize = "AI: streść tę stronę",
        prompt = "O co zapytać?",
        noPage = "Otwórz stronę, którą streścić.",
        empty = "Ta strona jest pusta.",
        written = "Odpowiedź AI jest w dzisiejszej notatce.",
        cut = "(strona przycięta do %d znaków)",
    },
    en = {
        ask = "AI: ask about this page",
        summarize = "AI: summarise this page",
        prompt = "What do you want to ask?",
        noPage = "Open the page to summarise.",
        empty = "This page is empty.",
        written = "The AI's answer is in today's note.",
        cut = "(the page cut to %d characters)",
    },
}

local W = if b.lang() == "pl" then WRITTEN.pl else WRITTEN.en
local T = if b.uiLang() == "pl" then INTERFACE.pl else INTERFACE.en

local function trim(s: string): string
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
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
local function pageText(page: string): (string, string?)
    if page == "" then
        return "", nil
    end
    local text = b.read(page) or ""
    if #text > MAX_PAGE then
        return text:sub(1, MAX_PAGE), string.format(T.cut, MAX_PAGE)
    end
    return text, nil
end

b.command("ask", function(ctx)
    local question = trim(b.ask(T.prompt))
    if question == "" then
        return nil
    end
    local page: string = ctx.page or ""
    local text, note = pageText(page)
    local prompt = question
    if text ~= "" then
        prompt = string.format(W.noteIntro, page, text) .. question
    end
    local answer = b.ai({ system = W.system, prompt = prompt })
    local title = string.format(W.question, question)
    if page ~= "" then
        title ..= string.format(W.about, page)
    end
    b.appendToJournal(b.today(), "- " .. title .. "\n" .. outline(answer))
    return if note then T.written .. " " .. note else T.written
end, { label = T.ask })

b.command("summarize", function(ctx)
    local page: string = ctx.page or ""
    if page == "" then
        return T.noPage
    end
    local text, note = pageText(page)
    if trim(text) == "" then
        return T.empty
    end
    local answer = b.ai({ system = W.system, prompt = string.format(W.noteIntro, page, text) .. W.summarizeAsk })
    b.appendToJournal(b.today(), "- " .. string.format(W.summary, page) .. "\n" .. outline(answer))
    return if note then T.written .. " " .. note else T.written
end, { label = T.summarize })
