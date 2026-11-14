---
tags: [extension]
permissions: [write]
description: Makes one-tap commands that add a line to today's note — each one a small extension page of its own.
---
# Quick entries

"Quick entries: new" asks for a name and a line, and writes an extension page — "Quick entry
Coffee" — whose one command adds that line to today's note (`{time}` in the line becomes the time).
The page is checked with `b.checkExtension` before it is written. Brulion then asks for approval on
the new page: code an extension writes is code "changed elsewhere", like code from a sync.

## The generated page

The line goes into the generated code as a Luau string (`%q` escapes it), never as code.

```luau
local FOLDER = b.lang() == "pl" and "Rozszerzenia" or "Extensions"
local PREFIX = "Quick entry "

local function trim(s: string): string
    return (s:match("^%s*(.-)%s*$"))
end

-- A name as a page's title: without what file names, links and code fences cannot hold.
local function titleFrom(name: string): string
    return trim((name:gsub('[/\\:*?"<>|%[%]#^`%c]', " "):gsub("%s+", " ")))
end

local function pageText(name: string, line: string): string
    local label = "Quick entry: " .. name
    return table.concat({
        "---",
        "tags: [extension]",
        "permissions: [write]",
        "description: A quick entry — one command that adds a line to today's note.",
        "---",
        "Made by the extension [[Quick entries]]; change the line below as you like.",
        "",
        "```luau",
        "local LINE = " .. string.format("%q", line),
        "",
        'b.command("add", function(ctx)',
        '    local line = LINE:gsub("{time}", os.date("%H:%M"))',
        '    b.appendToJournal(b.today(), "- " .. line)',
        '    return "Added: " .. line',
        "end, { label = " .. string.format("%q", label) .. " })",
        "```",
        "",
    }, "\n")
end
```

## The command and the view

```luau
b.command("new", function(ctx)
    local name = titleFrom(b.ask("A name for the quick entry (Coffee, Idea, Run …)"))
    if name == "" then
        return nil
    end
    local line = trim(b.ask("The line it adds to today's note ({time}: the time)"))
    if line == "" then
        return nil
    end
    if line:find("%c") then
        return "The line must be one line of text."
    end

    local title = PREFIX .. name
    local text = pageText(name, line)
    local report = b.checkExtension(title, text)
    if not report.ok then
        return `The page would not load (line {report.line or "?"}): {report.error}`
    end
    local target = `{FOLDER}/{title}`
    if b.read(title) ~= nil then
        if b.choose(`"{title}" is there already. Replace it?`, { "Replace", "Keep" }) ~= "Replace" then
            return nil
        end
        target = title
    end
    b.write(target, text)
    return `Written: "{title}" with the command "{report.commands[1].label}". Approve it on its page.`
end, { label = "Quick entries: new" })

b.view("list", function(params)
    local nodes = { b.ui.heading("Quick entries") }
    local found = 0
    for _, tag in { "extension", "rozszerzenie" } do
        for _, name in b.pages({ tag = tag }) do
            if name:sub(1, #PREFIX) == PREFIX then
                found += 1
                table.insert(nodes, b.ui.link(name, name:sub(#PREFIX + 1)))
            end
        end
    end
    if found == 0 then
        table.insert(nodes, b.ui.empty("No quick entry yet", "Make one: a command that adds a line to today's note."))
    end
    table.insert(nodes, b.ui.run("New quick entry", "new"))
    return b.ui.column(nodes)
end, nil, { label = "Quick entries", icon = "pencil" })
```
