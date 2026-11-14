---
tags: [extension]
description: Finds links to pages that do not exist, with the pages they are on and what the search finds for them.
---
# Link check

A link to a page that does not exist is not an error in Brulion — a tap makes the page — but a typo
in a link hides a note from its backlinks. The view "Broken links" lists every missing target, the
pages that link to it and the page the full-text search finds for its words — often the one that
was meant. It only reads: no permissions.

## Links in a text

Aliases (`[[Page|shown]]`), headings and blocks (`[[Page#Heading]]`, `[[Page#^id]]`) name the page
before the `|` or `#`. Code is skipped, and so are files (`![[photo.jpg]]`) and dates: a daily note
is made when its day comes.

```luau
-- The page names the [[links]] of `text` lead to, each once, in order.
local function targetsOf(text: string): { string }
    local found, seen = {}, {}
    local fenced = false
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        if line:find("^%s*```") or line:find("^%s*~~~") then
            fenced = not fenced
        elseif not fenced and #line <= 5000 and line:find("[[", 1, true) then
            line = line:gsub("`[^`]*`", "")
            for inner in line:gmatch("%[%[([^%[%]]+)%]%]") do
                local target = inner:match("^([^|#]*)"):match("^%s*(.-)%s*$")
                local key = b.fold(target)
                if target ~= "" and not seen[key] then
                    seen[key] = true
                    table.insert(found, target)
                end
            end
        end
    end
    return found
end

local function isFile(target: string): boolean
    local extension = target:match("%.(%a%w*)$")
    return extension ~= nil and #extension <= 5 and extension:lower() ~= "md"
end

local function isDate(target: string): boolean
    return target:match("^%d%d%d%d%-%d%d%-%d%d$") ~= nil
end
```

## The scan

Reading every page takes time in a big notebook, and a call has two seconds: the scan stops after
one and says how far it got.

```luau
local BUDGET_SECONDS = 1

-- { missing = { { target, sources } … }, scanned, pages, links }: targets in alphabetical order.
local function scan()
    local started = os.clock()
    local names = b.pages()
    local exists = {}
    local missing, order = {}, {}
    local scanned, links = 0, 0
    for _, name in names do
        if os.clock() - started > BUDGET_SECONDS then
            break
        end
        scanned += 1
        for _, target in targetsOf(b.read(name) or "") do
            links += 1
            if not isFile(target) and not isDate(target) then
                local key = b.fold(target)
                if exists[key] == nil then
                    exists[key] = b.page(target) ~= nil
                end
                if not exists[key] then
                    if not missing[key] then
                        missing[key] = { target = target, sources = {} }
                        table.insert(order, target)
                    end
                    table.insert(missing[key].sources, name)
                end
            end
        end
    end
    local list = {}
    for _, target in b.sorted(order) do
        table.insert(list, missing[b.fold(target)])
    end
    return { missing = list, scanned = scanned, pages = #names, links = links }
end

-- A page that exists and may be what `target` meant: the best full-text hit for its words.
local function similar(target: string): string?
    local hit = b.search(target, 1)[1]
    if hit and b.fold(hit.page) ~= b.fold(target) then
        return hit.page
    end
    return nil
end
```

## The view and the command

```luau
local MAX_SHOWN = 50

b.view("broken", function(params)
    local result = scan()
    local nodes = { b.ui.heading(`Broken links: {#result.missing}`) }
    if result.scanned < result.pages then
        table.insert(nodes, b.ui.text(`Checked {result.scanned} of {result.pages} pages: the notebook is too big for one pass.`, true))
    else
        table.insert(nodes, b.ui.text(`{result.links} links on {result.pages} pages.`, true))
    end
    if #result.missing == 0 then
        table.insert(nodes, b.ui.empty("Every link leads somewhere", "No link names a page that does not exist."))
        return b.ui.column(nodes)
    end
    for i, entry in result.missing do
        if i > MAX_SHOWN then
            table.insert(nodes, b.ui.text(`… and {#result.missing - MAX_SHOWN} more: the command prints all of them.`, true))
            break
        end
        local row = { b.ui.link(entry.target), b.ui.text("on", true) }
        for n, source in entry.sources do
            if n > 5 then
                table.insert(row, b.ui.text(`+{#entry.sources - 5}`, true))
                break
            end
            table.insert(row, b.ui.link(source))
        end
        local guess = similar(entry.target)
        if guess then
            table.insert(row, b.ui.text("search:", true))
            table.insert(row, b.ui.link(guess))
        end
        table.insert(nodes, b.ui.row(row))
    end
    return b.ui.column(nodes)
end, nil, { label = "Broken links", icon = "link" })

b.command("report", function(ctx)
    local result = scan()
    for _, entry in result.missing do
        print(`[[{entry.target}]] ← {table.concat(entry.sources, ", ")}`)
    end
    if result.scanned < result.pages then
        return `{#result.missing} broken links on the first {result.scanned} of {result.pages} pages`
    end
    return `{#result.missing} broken links on {result.pages} pages; the list is in the Luau console`
end, { label = "Link check: report" })
```
