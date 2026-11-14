---
tags: [rozszerzenie]
opis: Liczy słowa, znaki i czas czytania strony albo bloku; ranking najdłuższych stron.
---
# Licznik słów

Rozszerzenie tylko czyta notatki, więc nie potrzebuje żadnych uprawnień. W palecie: „Policz słowa
na stronie”, „Policz słowa w bloku” (z kursorem w bloku), „Najdłuższe strony” i widok „Statystyki
notatnika”.

## Liczenie

Słowo to ciąg znaków bez odstępów, w którym jest litera albo cyfra: same znaczniki Markdownu (`-`,
`#`, `**`), myślniki i emoji się nie liczą. Tekst to bajty UTF-8, więc znaki liczy `utf8.len`.

```luau
local WORDS_PER_MINUTE = 200

-- The properties at the very start ("---" … "---") are not the page's text.
local function withoutFrontmatter(text: string): string
    if not text:find("^%-%-%-\r?\n") then
        return text
    end
    local _, stop = text:find("\n%-%-%-[ \t]*\r?\n", 4)
    if stop then
        return text:sub(stop + 1)
    end
    return text:find("\n%-%-%-[ \t]*$", 4) and "" or text
end

-- A letter or a digit: ASCII, or the first byte of a two-byte UTF-8 letter (ą, ż, é, я …).
local LETTER = "[%w\195-\223]"

local function countWords(text: string): number
    local n = 0
    for word in text:gmatch("%S+") do
        if word:find(LETTER) then
            n += 1
        end
    end
    return n
end

local function stats(text: string)
    local words = countWords(text)
    return {
        words = words,
        chars = utf8.len(text) or #text, -- text that is not UTF-8: its bytes
        minutes = math.max(1, math.ceil(words / WORDS_PER_MINUTE)),
    }
end

-- 1 słowo, 2–4 słowa (but 12–14 słów), the rest słów.
local function plural(n: number, one: string, few: string, many: string): string
    local last, lastTwo = n % 10, n % 100
    local word = many
    if n == 1 then
        word = one
    elseif last >= 2 and last <= 4 and (lastTwo < 12 or lastTwo > 14) then
        word = few
    end
    return `{n} {word}`
end

local function wordsOf(n: number): string
    return plural(n, "słowo", "słowa", "słów")
end

local function describe(s): string
    local chars = plural(s.chars, "znak", "znaki", "znaków")
    return `{wordsOf(s.words)}, {chars}, ok. {s.minutes} min czytania`
end
```

## Polecenia

```luau
b.command("policz", function(ctx)
    if ctx.page == "" then
        return "Otwórz najpierw stronę."
    end
    local text = b.read(ctx.page)
    if not text then
        return `Nie ma strony „{ctx.page}”.`
    end
    return `„{ctx.page}”: {describe(stats(withoutFrontmatter(text)))}`
end, { label = "Policz słowa na stronie" })

-- The block's own text and the text of everything under it.
local function collect(block, parts)
    table.insert(parts, block.text)
    for _, child in block.children do
        collect(child, parts)
    end
    return parts
end

b.command("policz-blok", function(ctx)
    local block = ctx.block
    if not block then
        return "Ustaw kursor w bloku."
    end
    local own = stats(block.text)
    if #block.children == 0 then
        return `Blok: {describe(own)}`
    end
    local all = stats(table.concat(collect(block, {}), "\n"))
    return `Blok: {describe(own)}; z podpunktami: {wordsOf(all.words)}`
end, { label = "Policz słowa w bloku", block = true })
```

## Ranking i widok

Każda strona jest czytana przy każdym rysowaniu widoku: dla kilku tysięcy stron to wciąż ułamek
sekundy, a liczby zawsze są aktualne.

```luau
-- Every page with its numbers, the longest first; equal ones stay in alphabetical order.
local function ranking()
    local rows = {}
    for order, name in b.pages() do
        local text = b.read(name)
        if text then
            local s = stats(withoutFrontmatter(text))
            table.insert(rows, { name = name, words = s.words, chars = s.chars, order = order })
        end
    end
    table.sort(rows, function(x, y)
        if x.words ~= y.words then
            return x.words > y.words
        end
        return x.order < y.order
    end)
    return rows
end

b.command("ranking", function(ctx)
    local rows = ranking()
    local total = 0
    for place, row in rows do
        total += row.words
        if place <= 10 then
            print(`{place}. {row.name}: {wordsOf(row.words)}`)
        end
    end
    local pages = plural(#rows, "strona", "strony", "stron")
    return `{pages}, razem {wordsOf(total)}. Ranking jest w konsoli Luau.`
end, { label = "Najdłuższe strony" })

b.view("statystyki", function(params)
    local rows = ranking()
    if #rows == 0 then
        return b.ui.empty("Pusty notatnik", "Nie ma jeszcze żadnej strony.")
    end
    local words, chars = 0, 0
    for _, row in rows do
        words += row.words
        chars += row.chars
    end
    local top = {}
    for place = 1, math.min(#rows, 15) do
        local row = rows[place]
        table.insert(top, { `{place}.`, row.name, b.formatNumber(row.words, 0), b.formatNumber(row.chars, 0) })
    end
    local pages = plural(#rows, "strona", "strony", "stron")
    local average = b.formatNumber(words / #rows, 1)
    return b.ui.column({
        b.ui.heading("Statystyki notatnika"),
        b.ui.text(`{pages}, {wordsOf(words)}, średnio {average} na stronę.`),
        b.ui.table({ "Nr", "Strona", "Słowa", "Znaki" }, top),
        b.ui.row({ b.ui.text("Najdłuższa:", true), b.ui.link(rows[1].name) }),
        b.ui.run("Wypisz ranking w konsoli", "ranking"),
    })
end, nil, { label = "Statystyki notatnika", icon = "chart-column" })
```
