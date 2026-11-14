---
tags: [rozszerzenie]
uprawnienia: [zapis]
opis: Utrzymuje stronę „Projekty” — strony z tagiem #projekt według statusu, z liczbą otwartych zadań.
---
# Indeks projektów

Rozszerzenie utrzymuje stronę [[Projekty]]: listę stron z tagiem `#projekt`, pogrupowaną według
właściwości `status`, z liczbą otwartych zadań i najbliższym terminem. Strona powstaje od nowa po
każdej zmianie notatek (`b.on("change")`) — ale jest zapisywana tylko wtedy, gdy jej treść naprawdę
się zmieniła, więc synchronizacja nie ma czego wysyłać bez potrzeby. Polecenie „Odśwież indeks
projektów” robi to samo na żądanie.

## Treść strony

```luau
local INDEX = "Projekty"
local TAG = "projekt"
local STATUSES = { "aktywny", "wstrzymany", "zakończony" } -- in this order on the page; others follow
local NO_STATUS = "bez statusu"

-- A page name inside a query: in quotes the query language knows (a name may have spaces).
local function quoted(name: string): string
    return if name:find('"', 1, true) then `„{name}”` else `"{name}"`
end

local function capitalized(s: string): string
    local stop = utf8.offset(s, 2) or #s + 1
    local first = s:sub(1, stop - 1)
    local upper = { ["ą"] = "Ą", ["ć"] = "Ć", ["ę"] = "Ę", ["ł"] = "Ł", ["ń"] = "Ń", ["ó"] = "Ó", ["ś"] = "Ś", ["ź"] = "Ź", ["ż"] = "Ż" }
    return (upper[first] or first:upper()) .. s:sub(stop)
end

-- One project's line: its link, its open tasks and the nearest due date.
local function line(name: string): string
    local page = quoted(name)
    local open = b.query(`type:tasks status:open page:{page}`, 1).total
    if open == 0 then
        return `- [[{name}]] — bez otwartych zadań`
    end
    local nearest = b.query(`type:tasks status:open page:{page} due:any sort:due`, 1).items[1]
    if nearest and nearest.due then
        return `- [[{name}]] — otwarte zadania: {open}, najbliższy termin: {nearest.due}`
    end
    return `- [[{name}]] — otwarte zadania: {open}`
end

-- The whole page, and how many projects it lists.
local function content(): (string, number)
    -- A group per status. b.fold makes "Zakończony" and "zakonczony" one status — it is the key,
    -- never what the page shows: that is the spelling of STATUSES, or the first one met.
    local groups, order, titles = {}, {}, {}
    for _, status in STATUSES do
        local key = b.fold(status)
        groups[key] = { title = status, lines = {} }
        table.insert(order, key)
    end
    local unsorted = { title = NO_STATUS, lines = {} }
    local count = 0
    for _, name in b.pages({ tag = TAG }) do
        if name ~= INDEX then
            local page = b.page(name)
            local status = (page and page.props.status or ""):match("^%s*(.-)%s*$")
            local key = b.fold(status)
            local group = if key == "" then unsorted else groups[key]
            if not group then
                group = { title = status, lines = {} }
                groups[key] = group
                table.insert(titles, status)
            end
            table.insert(group.lines, line(name))
            count += 1
        end
    end
    -- The known statuses first, then the others in alphabetical order, the pages without one last.
    for _, title in b.sorted(titles) do
        table.insert(order, b.fold(title))
    end

    local lines = {
        "---",
        "tags: [spis]",
        "---",
        "Tę stronę pisze rozszerzenie [[Indeks projektów]]: zmiany wpisane tutaj zostaną zastąpione.",
    }
    local function add(group)
        if #group.lines == 0 then
            return
        end
        table.insert(lines, "")
        table.insert(lines, `## {capitalized(group.title)}`)
        table.insert(lines, "")
        for _, projectLine in group.lines do
            table.insert(lines, projectLine)
        end
    end
    for _, key in order do
        add(groups[key])
    end
    add(unsorted)
    if count == 0 then
        table.insert(lines, "")
        table.insert(lines, "Żadna strona nie ma jeszcze tagu #projekt.")
    end
    return table.concat(lines, "\n") .. "\n", count
end

-- Writes the page when it differs; true when it was written.
local function rebuild(): (boolean, number)
    local text, count = content()
    if b.read(INDEX) == text then
        return false, count
    end
    b.write(INDEX, text)
    return true, count
end
```

## Reakcje i polecenie

To, co rozszerzenia zapisują same, nie wraca do nich jako „change”, więc przepisanie strony nie
wywołuje kolejnego. Reakcja na „save” tylko odnotowuje w konsoli zapis strony projektu.

```luau
b.on("change", function(e)
    local written, count = rebuild()
    if written then
        return `Indeks projektów przepisany ({count})`
    end
    return nil
end)

b.on("save", function(e)
    local page = b.page(e.page)
    if page and table.find(page.tags, TAG) then
        print(`Zapisano projekt: {e.page} ({e.path})`)
    end
end)

b.command("odswiez", function(ctx)
    local written, count = rebuild()
    if written then
        return `Strona „{INDEX}” przepisana, projektów: {count}`
    end
    return `Strona „{INDEX}” jest aktualna, projektów: {count}`
end, { label = "Odśwież indeks projektów" })
```
