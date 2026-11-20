---
tags: [rozszerzenie]
uprawnienia: [zapis]
opis: Fiszki z powtórkami w odstępach — pytanie >> odpowiedź w dowolnej notatce, termin powtórki w tym samym wierszu.
---
# Fiszki

Fiszka to wiersz z tagiem `#fiszka`: pytanie, `>>`, odpowiedź. Po powtórce rozszerzenie dopisuje na
końcu wiersza, kiedy pokazać ją znowu — wszystko zostaje w notatce, czytelne bez Brulionu:

```markdown
- Stolica Peru >> Lima #fiszka
- Stolica Polski >> Warszawa #fiszka (powtórka 2026-11-18, co 4 dni)
```

Widok „Fiszki” pokazuje po jednej fiszce do powtórki: pytanie, po dotknięciu odpowiedź i ocenę.
„Umiem” podwaja odstęp, „Łatwe” potraja, „Jeszcze raz” wraca do jednego dnia.

## Fiszki z notatek

```luau
local TAG = "fiszka"
local SCHEDULE = "%s*%(powtórka (%d%d%d%d%-%d%d%-%d%d), co (%d+) dni%)"
local LEARNED = 16 -- days: a card repeated this rarely counts as known
local LONGEST = 3650 -- days: no interval grows past ten years, whatever a line says

local function addDays(date: string, n: number): string
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", noon)
end

local function trim(s: string): string
    return (s:match("^%s*(.-)%s*$"))
end

-- A card from a block, or nil when the block only mentions the tag.
local function cardOf(block)
    local text = block.text
    if #text > 2000 then
        return nil
    end
    local due, interval = text:match(SCHEDULE)
    local body = trim((text:gsub(SCHEDULE, ""):gsub("#" .. TAG, "")))
    local stop, resume = body:find(">>", 1, true)
    if not stop then
        return nil
    end
    local question, answer = trim(body:sub(1, stop - 1)), trim(body:sub(resume + 1))
    if question == "" or answer == "" then
        return nil
    end
    return {
        ref = block.ref,
        text = text,
        page = block.page,
        question = question,
        answer = answer,
        due = due, -- nil: never repeated
        interval = math.min(tonumber(interval) or 0, LONGEST),
    }
end

-- Every card; those to repeat today first (new ones, then the longest overdue).
local function cards()
    local today = b.today()
    local all, due = {}, {}
    for _, block in b.blocks({ tag = TAG }) do
        local card = cardOf(block)
        if card then
            table.insert(all, card)
            if not card.due or card.due <= today then
                table.insert(due, card)
            end
        end
    end
    table.sort(due, function(x, y)
        local dx, dy = x.due or "", y.due or ""
        if dx ~= dy then
            return dx < dy
        end
        return x.ref < y.ref
    end)
    return all, due
end
```

## Widok

Którą odpowiedź odsłonić, mówi parametr `show` (odnośnik fiszki) — „Pokaż odpowiedź” otwiera ten sam
widok z tym parametrem. Po ocenie widok rysuje się od nowa: następna fiszka ma inny odnośnik, więc
znów widać samo pytanie.

```luau
local GRADES = {
    again = function(interval: number): number
        return 1
    end,
    good = function(interval: number): number
        return math.max(2, interval * 2)
    end,
    easy = function(interval: number): number
        return math.max(4, interval * 3)
    end,
}

b.view("powtorka", function(params)
    local all, due = cards()
    if #all == 0 then
        return b.ui.empty("Brak fiszek", "Fiszka to wiersz: pytanie >> odpowiedź #fiszka.")
    end
    local learned = 0
    for _, card in all do
        if card.interval >= LEARNED then
            learned += 1
        end
    end
    local nodes = {
        b.ui.heading(`Fiszki: {#due} do powtórki`),
        b.ui.text(`Wszystkich: {#all}, opanowanych: {learned}.`, true),
    }
    local card = due[1]
    if not card then
        table.insert(nodes, b.ui.empty("Na dziś wszystko", "Kolejne powtórki pokażą się w swoim terminie."))
        return b.ui.column(nodes)
    end
    table.insert(nodes, b.ui.heading(card.question))
    if params.show ~= card.ref then
        table.insert(nodes, b.ui.open("Pokaż odpowiedź", "", { show = card.ref }))
    else
        table.insert(nodes, b.ui.text(card.answer))
        local function grade(label: string, name: string)
            return b.ui.button(label, "ocen", { ref = card.ref, text = card.text, grade = name })
        end
        table.insert(nodes, b.ui.row({ grade("Jeszcze raz", "again"), grade("Umiem", "good"), grade("Łatwe", "easy") }))
    end
    table.insert(nodes, b.ui.row({ b.ui.text("Ze strony", true), b.ui.link(card.page) }))
    return b.ui.column(nodes)
end, {
    ocen = function(args)
        local step = GRADES[args.grade]
        if not step then
            error(`Nieznana ocena: {args.grade}`, 0)
        end
        local _, interval = args.text:match(SCHEDULE)
        local days = math.min(step(math.min(tonumber(interval) or 0, LONGEST)), LONGEST)
        local due = addDays(b.today(), days)
        local body = trim((args.text:gsub(SCHEDULE, "")))
        b.replaceBlock(args.ref, `{body} (powtórka {due}, co {days} dni)`)
        return `Następna powtórka: {due}`
    end,
}, { label = "Fiszki", icon = "book-open" })
```

## Dopisywanie

```luau
b.command("dodaj", function(ctx)
    local question = trim(b.ask("Pytanie"))
    if question == "" then
        return nil
    end
    local answer = trim(b.ask("Odpowiedź"))
    if answer == "" then
        return nil
    end
    b.appendToJournal(b.today(), `- {question} >> {answer} #{TAG}`)
    return "Fiszka dopisana do dzisiejszej notatki"
end, { label = "Fiszki: dodaj" })
```
