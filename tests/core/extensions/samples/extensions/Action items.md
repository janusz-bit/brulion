---
tags: [extension]
permissions: [write, ai]
description: Asks the model for the action items of the note on screen and adds the ones you accept to today's note as tasks.
---
# Action items

"AI: tasks from this note" sends the note on screen to the model chosen in Settings → Extensions →
AI. The model proposes tasks through a tool; each one is shown and waits for a yes ("Add all"
accepts the rest), and the accepted ones are added to today's note under a line that links back:

```markdown
- Action items of [[Meeting with Anna]]
  - [ ] Send the offer 📅 2026-11-20
```

## The model's tool

The model answers by calling `add_task`; nothing it says is written without the user's yes, and
what it sends is checked — a task is one line, a date is a real day.

```luau
local MAX_CHARS = 20000 -- of the note
local MAX_TURNS = 4
local MAX_TASKS = 20

local SYSTEM = table.concat({
    "You read one note and find its action items: things somebody still has to do that the note",
    "does not list as a checkbox task yet. Call add_task once for each, as a short imperative",
    "sentence in the note's language. Give a due date only when the note names a day.",
    "When nothing is left to do, say so in one sentence and call no tool.",
}, " ")

local TOOLS = {
    {
        name = "add_task",
        description = "Proposes one action item of the note as a task. The user accepts or skips it.",
        input = {
            type = "object",
            properties = {
                text = { type = "string", description = "The task: short, imperative, in the note's language" },
                due = { type = "string", description = "Its due date as YYYY-MM-DD, only when the note names a day" },
            },
            required = { "text" },
        },
    },
}

-- The model's text as one line of a task (without a list marker or checkbox of its own), or nil.
local function taskText(value): string?
    if type(value) ~= "string" then
        return nil
    end
    local text = value:gsub("%s+", " "):match("^%s*(.-)%s*$")
    text = text:gsub("^[-*] ", ""):gsub("^%[.%] ", "")
    local stop = utf8.offset(text, 201) -- 200 characters at most
    if stop then
        text = text:sub(1, stop - 1)
    end
    return if text == "" then nil else text
end

local function dueDate(value): string?
    if type(value) == "string" and value:match("^%d%d%d%d%-%d%d%-%d%d$") and pcall(b.daysBetween, b.today(), value) then
        return value
    end
    return nil
end
```

## The command

`pcall` catches what goes wrong with the model (no key, no connection); the questions wait inside
the loop like anywhere else in a command.

```luau
b.command("extract", function(ctx)
    if ctx.page == "" then
        return "Open a note first."
    end
    local note = b.read(ctx.page)
    if not note or note:match("^%s*$") then
        return "This note is empty."
    end
    local stop = utf8.offset(note, MAX_CHARS + 1)
    if stop then
        note = note:sub(1, stop - 1)
    end

    local messages = {
        { role = "user", content = `Today is {b.today()}. The note "{ctx.page}":\n\n{note}` },
    }
    local accepted = {}
    local proposed, acceptAll, said = 0, false, ""
    for turn = 1, MAX_TURNS do
        local ok, text, reply = pcall(b.ai, { system = SYSTEM, messages = messages, tools = TOOLS, maxTokens = 2000 })
        if not ok then
            -- "b.ai: no connection: …": the words after the function's name are for people.
            return "AI is not available: " .. tostring(text):gsub("^.-b%.ai: ", "", 1)
        end
        said = text or ""
        if not reply.toolCalls then
            break
        end
        table.insert(messages, { role = "assistant", raw = reply.raw }) -- its turn, verbatim
        local results = {}
        for _, call in reply.toolCalls do
            local input = if type(call.input) == "table" then call.input else {}
            local task = taskText(input.text)
            local content, failed = "Added.", false
            if call.name ~= "add_task" then
                content, failed = "No such tool.", true
            elseif not task then
                content, failed = "A task needs a text of one line.", true
            elseif #accepted >= MAX_TASKS then
                content, failed = "Enough tasks for one note.", true
            else
                local due = dueDate(input.due)
                local line = if due then `{task} 📅 {due}` else task
                proposed += 1
                local choice = if acceptAll then "Add" else b.choose(`Add this task?\n{line}`, { "Add", "Add all", "Skip" })
                if choice == "Add all" then
                    acceptAll = true
                end
                if choice == "Add" or choice == "Add all" then
                    table.insert(accepted, line)
                else
                    content = "The user skipped it."
                end
            end
            table.insert(results, { id = call.id, content = content, isError = failed })
        end
        table.insert(messages, { role = "user", results = results }) -- every result at once
    end

    if #accepted == 0 then
        if proposed > 0 then
            return "No task added."
        end
        return if said ~= "" then said else "No action items found."
    end
    local lines = { `- Action items of [[{ctx.page}]]` }
    for _, line in accepted do
        table.insert(lines, `  - [ ] {line}`)
    end
    b.appendToJournal(b.today(), table.concat(lines, "\n"))
    local count = if #accepted == 1 then "1 task" else `{#accepted} tasks`
    return `{count} added to today's note`
end, { label = "AI: tasks from this note" })
```
