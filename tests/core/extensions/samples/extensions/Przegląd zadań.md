---
tags: [rozszerzenie]
uprawnienia: [zapis]
opis: Zaległe i najbliższe zadania w jednym widoku — odhaczanie i przesuwanie terminów.
---
# Przegląd zadań

Widok „Przegląd zadań” pokazuje otwarte zadania z terminem (`📅`): zaległe, dzisiejsze i te z
najbliższych dni. Zadanie można w nim odhaczyć albo przesunąć na jutro lub o tydzień. Polecenia:
„Przesuń zaległe zadania na dziś” i „Ustaw termin zadania” (z kursorem w zadaniu).

## Daty

Daty to tekst `RRRR-MM-DD`; dni dodaje `os.time` (w południe czasu UTC, więc zmiana czasu niczego
nie przesuwa).

```luau
local DUE = "📅 %d%d%d%d%-%d%d%-%d%d"
local FIELDS = { "📅", "⏳", "🛫", "➕", "✅" }

local function addDays(date: string, n: number): string
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", noon)
end

-- The task's text with the due date `due`: its 📅 replaced, or added at the end.
local function withDue(text: string, due: string): string
    local changed, n = text:gsub(DUE, "📅 " .. due, 1)
    if n == 0 then
        return `{text} 📅 {due}`
    end
    return changed
end

-- The task's text without its dates, for showing.
local function label(text: string): string
    for _, field in FIELDS do
        text = text:gsub("%s*" .. field .. " %d%d%d%d%-%d%d%-%d%d", "")
    end
    return text
end

-- "jutro", "dziś", "3" or "+3" (days from today), or a date.
local function parseDue(answer: string): string?
    local s = b.fold(answer)
    if s == "dzis" or s == "dzisiaj" then
        return b.today()
    elseif s == "jutro" then
        return addDays(b.today(), 1)
    end
    local days = s:match("^%+?(%d%d?%d?)$")
    if days then
        return addDays(b.today(), tonumber(days))
    end
    if s:match("^%d%d%d%d%-%d%d%-%d%d$") and pcall(b.daysBetween, b.today(), s) then
        return s
    end
    return nil
end
```

## Widok

Każdy przycisk dostaje w `args` odnośnik (`ref`) i tekst zadania z chwili rysowania: jeżeli wiersz
zmienił się w międzyczasie, zapis jest odrzucany (`stale:`), a nie nadpisuje cudzej zmiany.

```luau
local horizon = 7 -- how many days ahead the view shows; kept until the code is loaded again

local function section(lines, title: string, query: string): number
    local result = b.query(query, 200)
    if #result.items == 0 then
        return 0
    end
    table.insert(lines, b.ui.heading(`{title} ({result.total})`))
    for _, task in result.items do
        table.insert(lines, b.ui.row({
            b.ui.check(false, "zrobione", { ref = task.ref, text = task.text }),
            b.ui.text(label(task.text)),
            b.ui.text(task.due, true),
            b.ui.link(task.page),
            b.ui.button("jutro", "przesun", { ref = task.ref, text = task.text, days = 1 }),
            b.ui.button("+7 dni", "przesun", { ref = task.ref, text = task.text, days = 7 }),
        }))
    end
    return result.total
end

b.view("przeglad", function(params)
    local lines = {}
    local shown = section(lines, "Zaległe", "type:tasks status:open due:overdue sort:due")
        + section(lines, "Na dziś", "type:tasks status:open due:today sort:-priority")
        + section(lines, `Najbliższe dni: {horizon}`, `type:tasks status:open due:tomorrow..+{horizon}d sort:due`)
    if shown == 0 then
        table.insert(lines, b.ui.empty("Nic pilnego", "Żadne otwarte zadanie nie ma terminu w tych dniach."))
    end
    table.insert(lines, b.ui.row({
        b.ui.text("Dni naprzód:", true),
        b.ui.stepper({ value = horizon, step = 1, unit = "dni", action = "horyzont" }),
    }))
    return b.ui.column(lines)
end, {
    zrobione = function(args)
        b.replaceBlock(args.ref, args.text, "x")
    end,
    przesun = function(args)
        local due = addDays(b.today(), args.days)
        b.replaceBlock(args.ref, withDue(args.text, due))
        return `Nowy termin: {due}`
    end,
    horyzont = function(args)
        horizon = math.clamp(math.floor(args.value), 1, 60)
    end,
}, { label = "Przegląd zadań", icon = "list-todo" })
```

## Polecenia

```luau
b.command("na-dzis", function(ctx)
    local overdue = b.query("type:tasks status:open due:overdue sort:due", 1000)
    if overdue.total == 0 then
        return "Nie ma zaległych zadań."
    end
    local answer = b.choose(`Przesunąć zaległe zadania ({overdue.total}) na dziś?`, { "Przesuń", "Zostaw" })
    if answer ~= "Przesuń" then
        return nil
    end
    local today = b.today()
    local moved, skipped = 0, 0
    for _, task in overdue.items do
        -- A date written another way (a Logseq DEADLINE line) or a line that changed: left alone.
        local found = task.text:find(DUE) ~= nil
        if found and pcall(b.replaceBlock, task.ref, withDue(task.text, today)) then
            moved += 1
        else
            skipped += 1
        end
    end
    if skipped > 0 then
        return `Przesunięte na dziś: {moved}, pominięte: {skipped}`
    end
    return `Przesunięte na dziś: {moved}`
end, { label = "Przesuń zaległe zadania na dziś" })

b.command("termin", function(ctx)
    local block = ctx.block
    if not block or not block.task then
        return "Ustaw kursor w zadaniu."
    end
    local answer = b.ask("Termin: RRRR-MM-DD, „jutro” albo liczba dni od dziś")
    if answer == "" then
        return nil
    end
    local due = parseDue(answer)
    if not due then
        return `Nie rozumiem terminu „{answer}”.`
    end
    b.replaceBlock(block.ref, withDue(block.text, due))
    return `Termin: {due}`
end, { label = "Ustaw termin zadania", block = true })
```
