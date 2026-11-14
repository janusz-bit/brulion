---
tags: [rozszerzenie]
uprawnienia: [zapis]
opis: Dziennik samopoczucia — nastrój i energia w skali 1–5, wykres dwóch tygodni, średnie dni tygodnia.
---
# Nastrój

Polecenie „Zapisz nastrój” pyta o nastrój i energię (1–5) i dopisuje do dzisiejszej notatki blok z
tagiem `#nastrój`; liczby są jego właściwościami, więc da się je też wpisać ręcznie:

```markdown
- Samopoczucie: las i słońce #nastrój
  nastroj:: 5
  energia:: 4
```

Widok „Nastrój” rysuje z tych bloków wykres ostatnich 14 dni i średnie dla dni tygodnia.

## Wpisy

```luau
local TAG = "nastrój"
local DAYS = 14 -- the chart
local WEEKS = 8 -- the weekday table
local WEEKDAYS = { "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota" }

local function addDays(date: string, n: number): string
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", noon)
end

-- 1 = Sunday … 7 = Saturday.
local function weekday(date: string): number
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    return os.date("!*t", os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) })).wday
end

-- A property's number: "4", "4,5" and "4.5" are numbers; anything else is not, and neither are
-- "nan" and "inf", which tonumber would take (notes come from anywhere).
local function number(value: string?): number?
    local n = value and tonumber((value:gsub(",", ".")))
    if n and n == n and math.abs(n) ~= math.huge then
        return n
    end
    return nil
end

local function scale(value: number): number
    return math.clamp(math.floor(value + 0.5), 1, 5)
end

-- The entries since `from`, by day: the day's last one counts.
local function entries(from: string)
    local byDay = {}
    for _, block in b.blocks({ tag = TAG, from = from, to = b.today() }) do
        local mood = number(block.props.nastroj)
        if mood then
            byDay[block.date] = { mood = scale(mood), energy = scale(number(block.props.energia) or mood) }
        end
    end
    return byDay
end
```

## Polecenie

`b.form` pyta o liczby, `b.ask` o tekst. Okno pytania nie przyjmuje pustej odpowiedzi, więc notatkę
pomija się przyciskiem „Anuluj” (`b.ask` zwraca wtedy `""`).

```luau
b.command("zapisz", function(ctx)
    local answer = b.form("Jak się dziś czujesz?", {
        { key = "nastroj", label = "Nastrój (1–5)", value = 3, step = 1 },
        { key = "energia", label = "Energia (1–5)", value = 3, step = 1 },
    })
    if answer == nil then
        return nil
    end
    local mood, energy = scale(answer.nastroj), scale(answer.energia)
    local note = b.ask("Co na to wpłynęło? (Anuluj: bez notatki)")
    local text = if note == "" then "Samopoczucie" else `Samopoczucie: {note}`
    b.appendToJournal(b.today(), `- {text} #{TAG}\n  nastroj:: {mood}\n  energia:: {energy}`)
    return `Zapisano: nastrój {mood}/5, energia {energy}/5`
end, { label = "Zapisz nastrój" })
```

## Widok

Dzień bez wpisu to `nil` w serii: wykres zostawia w tym miejscu przerwę.

```luau
b.view("wykres", function(params)
    local today = b.today()
    local byDay = entries(addDays(today, 1 - WEEKS * 7))
    local labels, mood, energy = {}, {}, {}
    local days, moodSum, energySum = 0, 0, 0
    for i = 1, DAYS do
        local date = addDays(today, i - DAYS)
        local entry = byDay[date]
        labels[i] = date:sub(6)
        if entry then
            mood[i], energy[i] = entry.mood, entry.energy
            days += 1
            moodSum += entry.mood
            energySum += entry.energy
        end
    end
    if next(byDay) == nil then
        return b.ui.column({
            b.ui.empty("Brak wpisów", "Polecenie „Zapisz nastrój” dopisze pierwszy do dzisiejszej notatki."),
            b.ui.run("Zapisz nastrój", "zapisz"),
        })
    end

    -- Averages by weekday, Monday first.
    local sums = {}
    for date, entry in byDay do
        local day = weekday(date)
        local sum = sums[day] or { mood = 0, energy = 0, n = 0 }
        sum.mood += entry.mood
        sum.energy += entry.energy
        sum.n += 1
        sums[day] = sum
    end
    local rows = {}
    for _, day in { 2, 3, 4, 5, 6, 7, 1 } do
        local sum = sums[day]
        if sum then
            table.insert(rows, {
                tostring(sum.n),
                WEEKDAYS[day],
                b.formatNumber(sum.mood / sum.n, 1),
                b.formatNumber(sum.energy / sum.n, 1),
            })
        end
    end

    local nodes = { b.ui.heading("Nastrój") }
    if days > 0 then
        local averageMood = b.formatNumber(moodSum / days, 1)
        local averageEnergy = b.formatNumber(energySum / days, 1)
        table.insert(nodes, b.ui.text(`Ostatnie {DAYS} dni: nastrój {averageMood}, energia {averageEnergy} (dni z wpisem: {days}).`))
    else
        table.insert(nodes, b.ui.text(`W ostatnich {DAYS} dniach nie ma wpisu.`, true))
    end
    table.insert(nodes, b.ui.chart({
        kind = "line",
        title = `Ostatnie {DAYS} dni`,
        labels = labels,
        series = { { name = "Nastrój", values = mood }, { name = "Energia", values = energy } },
    }))
    table.insert(nodes, b.ui.heading(`Dni tygodnia (ostatnie {WEEKS} tygodni)`))
    table.insert(nodes, b.ui.table({ "Wpisy", "Dzień", "Nastrój", "Energia" }, rows))
    table.insert(nodes, b.ui.run("Zapisz nastrój", "zapisz"))
    return b.ui.column(nodes)
end, nil, { label = "Nastrój", icon = "heart" })
```
