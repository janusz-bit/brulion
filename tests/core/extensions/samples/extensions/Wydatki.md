---
tags: [rozszerzenie]
uprawnienia: [zapis]
opis: Wydatki z notatek dziennika — sumy miesiąca według kategorii, wykres półrocza, dopisywanie.
waluta: zł
---
# Wydatki

Wydatek to wiersz dziennika z tagiem `#wydatek` (albo `#expense`): kwota, waluta, kategoria i — po
dwukropku — opis.

```markdown
- 12,50 zł jedzenie: obiad w barze #wydatek
- 1 250 zł mieszkanie: czynsz #wydatek
```

Widok „Wydatki” sumuje miesiąc według kategorii i pokazuje ostatnie pół roku; „Wydatki: dodaj”
dopisuje wydatek do dzisiejszej notatki. Rozszerzenie mówi po polsku i po angielsku: napisy bierze z
języka interfejsu (`b.uiLang`), a słowa wpisywane do notatek z języka notatnika (`b.lang`).

## Ustawienia i słowa

Właściwość `waluta` tej strony (albo `currency`) to waluta nowych wpisów: rozszerzenie czyta ją z
własnej strony, więc zmienia się ją bez dotykania kodu.

```luau
local polish = b.uiLang() == "pl"
local function t(pl: string, en: string): string
    return polish and pl or en
end

local TAGS = { "wydatek", "expense" } -- both are read
local WRITTEN_TAG = b.lang() == "pl" and "wydatek" or "expense"
local OTHER = t("inne", "other")
local MONTHS = polish
        and { "styczeń", "luty", "marzec", "kwiecień", "maj", "czerwiec", "lipiec", "sierpień", "wrzesień",
            "październik", "listopad", "grudzień" }
    or { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
        "November", "December" }

local function currency(): string
    for _, name in { "Wydatki", "Expenses" } do
        local page = b.page(name)
        local value = page and (page.props.waluta or page.props.currency)
        if value and value ~= "" then
            return value
        end
    end
    return b.lang() == "pl" and "zł" or "USD"
end
```

## Czytanie wpisów

Wzorce Luau cofają się, a notatki przychodzą skądkolwiek: wiersz dłuższy niż 300 bajtów nie jest
wydatkiem, a wzorce zaczynają się od `^`.

```luau
local function trim(s: string): string
    return (s:match("^%s*(.-)%s*$"))
end

-- "12,50 zł jedzenie: obiad" → 12.5, "zł", "jedzenie", "obiad"; nil when the line has no amount.
local function parse(text: string)
    if #text > 300 then
        return nil
    end
    text = text:gsub("#[^%s#]+", "")
    local digits, rest = text:match("^%s*(%d[%d ]*[.,]?%d*)%s*(.*)$")
    if not digits then
        return nil
    end
    local amount = tonumber((digits:gsub(" ", ""):gsub(",", ".")))
    local unit, tail = rest:match("^(%S+)%s*(.*)$")
    if not amount or amount > 1e12 or not unit then -- (hundreds of digits read as infinity)
        return nil
    end
    local category, note = tail:match("^([^:]*):%s*(.*)$")
    if not category then
        category, note = tail, ""
    end
    category = trim(category)
    return { amount = amount, unit = unit, category = category ~= "" and category or OTHER, note = trim(note) }
end

-- "2026-11" and 1 → "2026-12"; month 13 rolls into the next year.
local function shiftMonth(month: string, n: number): string
    local y, m = month:match("^(%d+)-(%d+)$")
    return os.date("!%Y-%m", os.time({ year = tonumber(y), month = tonumber(m) + n, day = 1 }))
end

local function lastDay(month: string): string
    local y, m = month:match("^(%d+)-(%d+)$")
    return os.date("!%Y-%m-%d", os.time({ year = tonumber(y), month = tonumber(m) + 1, day = 0 }))
end

local function monthName(month: string): string
    local y, m = month:match("^(%d+)-(%d+)$")
    return `{MONTHS[tonumber(m)]} {y}`
end

-- The expenses of the daily notes from `from` to `to`, oldest first.
local function expenses(from: string, to: string)
    local list, seen = {}, {}
    for _, tag in TAGS do
        for _, block in b.blocks({ tag = tag, from = from, to = to }) do
            local expense = not seen[block.ref] and parse(block.text)
            if expense then
                seen[block.ref] = true
                expense.date = block.date
                expense.page = block.page
                table.insert(list, expense)
            end
        end
    end
    table.sort(list, function(x, y)
        return x.date < y.date
    end)
    return list
end

local function money(amount: number, unit: string): string
    return `{b.formatNumber(amount, 2)} {unit}`
end
```

## Widok miesiąca

Miesiąc przychodzi w `params` — strzałki to `b.ui.open` z innym miesiącem; z palety jest nim bieżący.

```luau
-- "2026-11" when the text is a month of a year os.time knows, else nil.
local function validMonth(text): string?
    if type(text) ~= "string" then
        return nil
    end
    local y, m = text:match("^(%d%d%d%d)%-(%d%d)$")
    if y and tonumber(y) >= 1971 and tonumber(m) >= 1 and tonumber(m) <= 12 then
        return text
    end
    return nil
end

b.view("miesiac", function(params)
    local month = validMonth(params.month) or b.today():sub(1, 7)
    local first = shiftMonth(month, -5)
    local list = expenses(first .. "-01", lastDay(month))

    local unit = currency()
    local byMonth, byCategory, names = {}, {}, {}
    local total, count = 0, 0
    for _, expense in list do
        local key = expense.date:sub(1, 7)
        byMonth[key] = (byMonth[key] or 0) + expense.amount
        if key == month then
            local folded = b.fold(expense.category)
            local sum = byCategory[folded]
            if not sum then
                sum = { name = expense.category, amount = 0, n = 0 }
                byCategory[folded] = sum
                table.insert(names, folded)
            end
            sum.amount += expense.amount
            sum.n += 1
            total += expense.amount
            count += 1
            unit = expense.unit
        end
    end

    local previous, following = shiftMonth(month, -1), shiftMonth(month, 1)
    local nodes = {
        b.ui.heading(t("Wydatki: ", "Expenses: ") .. monthName(month)),
        b.ui.row({
            b.ui.open("← " .. monthName(previous), "", { month = previous }),
            b.ui.open(monthName(following) .. " →", "", { month = following }),
        }),
    }
    if count == 0 then
        table.insert(nodes, b.ui.empty(
            t("Brak wydatków", "No expenses"),
            t("W tym miesiącu nie ma wiersza z tagiem #wydatek.", "No line of this month is tagged #expense.")
        ))
    else
        table.sort(names, function(x, y)
            return byCategory[x].amount > byCategory[y].amount
        end)
        local rows = {}
        for _, folded in names do
            local sum = byCategory[folded]
            local share = b.formatNumber(100 * sum.amount / total, 0)
            table.insert(rows, { `{share}%`, sum.name, money(sum.amount, unit), tostring(sum.n) })
        end
        table.insert(nodes, b.ui.text(t("Razem: ", "Total: ") .. money(total, unit)))
        table.insert(nodes, b.ui.table(
            { t("Udział", "Share"), t("Kategoria", "Category"), t("Kwota", "Amount"), t("Wpisy", "Entries") },
            rows
        ))
    end

    local labels, totals = {}, {}
    for i = 0, 5 do
        local key = shiftMonth(first, i)
        table.insert(labels, key)
        table.insert(totals, byMonth[key] or 0)
    end
    table.insert(nodes, b.ui.chart({
        kind = "bar",
        title = t("Ostatnie pół roku", "The last six months"),
        labels = labels,
        series = { { name = t("Wydatki", "Expenses"), values = totals } },
    }))
    table.insert(nodes, b.ui.run(t("Dodaj wydatek", "Add an expense"), "dodaj"))
    return b.ui.column(nodes)
end, nil, { label = t("Wydatki", "Expenses"), icon = "calculator" })
```

## Dopisywanie

Kategorie do wyboru to te z ostatnich trzech miesięcy, najczęstsze najpierw.

```luau
local function categories(): { string }
    local today = b.today()
    local counts, names, order = {}, {}, {}
    for _, expense in expenses(shiftMonth(today:sub(1, 7), -2) .. "-01", today) do
        local folded = b.fold(expense.category)
        if not counts[folded] then
            counts[folded] = 0
            names[folded] = expense.category
            table.insert(order, folded)
        end
        counts[folded] += 1
    end
    table.sort(order, function(x, y)
        if counts[x] ~= counts[y] then
            return counts[x] > counts[y]
        end
        return x < y
    end)
    local out = {}
    for i = 1, math.min(#order, 6) do
        table.insert(out, names[order[i]])
    end
    return out
end

b.command("dodaj", function(ctx)
    local unit = currency()
    local form = b.form(t("Nowy wydatek", "A new expense"), {
        { key = "amount", label = t("Kwota", "Amount"), value = 0, step = 1, unit = unit },
    })
    if form == nil then
        return nil
    end
    if form.amount <= 0 then
        return t("Kwota musi być większa od zera.", "The amount must be more than zero.")
    end
    local another = t("Inna…", "Another…")
    local options = categories()
    table.insert(options, another)
    local category = b.choose(t("Kategoria", "Category"), options)
    if category == nil then
        return nil
    end
    if category == another then
        category = trim(b.ask(t("Nazwa kategorii", "The category's name")):gsub(":", " "):gsub("%s+", " "))
        if category == "" then
            return nil
        end
    end
    local note = trim(b.ask(t("Opis (Anuluj: bez opisu)", "A note (Cancel: none)")))
    local line = `- {money(form.amount, unit)} {category}`
    if note ~= "" then
        line ..= `: {note}`
    end
    b.appendToJournal(b.today(), `{line} #{WRITTEN_TAG}`)
    return t("Dopisano: ", "Added: ") .. `{money(form.amount, unit)}, {category}`
end, { label = t("Wydatki: dodaj", "Expenses: add") })
```
