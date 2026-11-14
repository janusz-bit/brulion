---
tags: [extension]
permissions: [write]
description: Focus sessions — a 25-minute timer, each session logged in today's note, the week as a chart.
---
# Pomodoro

"Pomodoro: start" asks what to focus on, starts the app's timer and logs the session in today's
note as a line tagged `#pomodoro`; "Pomodoro: focus on this line" does the same for the task or
line under the cursor. The view "Pomodoro" counts today's sessions and charts the last seven days.
(`b.timer` is the gym's rest timer so far: one timer at a time, and its notification speaks of the
next set.)

```markdown
- 🍅 09:05 Quarterly report ([[Work]]) #pomodoro
```

## Sessions

```luau
local FOCUS_MINUTES = 25
local BREAK_MINUTES = 5
local TAG = "pomodoro"
local FIELDS = { "📅", "⏳", "🛫", "➕", "✅" }

local function addDays(date: string, n: number): string
    local y, m, d = date:match("^(%d+)-(%d+)-(%d+)$")
    local noon = os.time({ year = tonumber(y), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", noon)
end

-- A line's text as a session's name: without task dates and tags, one line, not too long.
local function subject(text: string): string
    for _, field in FIELDS do
        text = text:gsub("%s*" .. field .. " %d%d%d%d%-%d%d%-%d%d", "")
    end
    text = text:gsub("%s*#[^%s#]+", ""):gsub("%s+", " ")
    text = text:match("^%s*(.-)%s*$")
    local stop = utf8.offset(text, 81) -- 80 characters at most, never half a letter
    return if stop then text:sub(1, stop - 1) .. "…" else text
end

-- "2 h 5 min" for 5 sessions.
local function duration(sessions: number): string
    local minutes = sessions * FOCUS_MINUTES
    if minutes < 60 then
        return `{minutes} min`
    end
    return `{minutes // 60} h {minutes % 60} min`
end

local function start(what: string, page: string?): string
    local link = if page and page ~= "" then ` ([[{page}]])` else ""
    b.appendToJournal(b.today(), `- 🍅 {os.date("%H:%M")} {what}{link} #{TAG}`)
    b.timer(FOCUS_MINUTES * 60, what)
    return `Focus for {FOCUS_MINUTES} minutes: {what}`
end

-- What a logged line says after its tomato and time.
local function logged(text: string): string
    return subject((text:gsub("^🍅%s*%d%d?:%d%d%s*", "")))
end
```

## Commands

```luau
b.command("start", function(ctx)
    local what = b.ask("What will you focus on?")
    if what == "" then
        return nil
    end
    return start(subject(what), ctx.page)
end, { label = "Pomodoro: start" })

b.command("start-here", function(ctx)
    local block = ctx.block
    if not block or subject(block.text) == "" then
        return "Put the cursor in a task or a line with text."
    end
    return start(subject(block.text), block.page)
end, { label = "Pomodoro: focus on this line", block = true })

b.command("break", function(ctx)
    b.timer(BREAK_MINUTES * 60, "Break")
    return `A break of {BREAK_MINUTES} minutes`
end, { label = "Pomodoro: take a break" })
```

## The view

A piece that is not always there is written as `condition and node or nil`: `b.ui.column` and
`b.ui.row` leave a `nil` out.

```luau
b.view("today", function(params)
    local today = b.today()
    local perDay, sessions = {}, {}
    for _, block in b.blocks({ tag = TAG, from = addDays(today, -6), to = today }) do
        perDay[block.date] = (perDay[block.date] or 0) + 1
        if block.date == today then
            local time = block.text:match("^🍅%s*(%d%d?:%d%d)") or ""
            table.insert(sessions, b.ui.row({ b.ui.text(time, true), b.ui.text(logged(block.text)) }))
        end
    end
    local labels, counts = {}, {}
    for i = -6, 0 do
        local date = addDays(today, i)
        table.insert(labels, date:sub(6))
        table.insert(counts, perDay[date] or 0)
    end
    local done = #sessions
    return b.ui.column({
        b.ui.heading(`Pomodoro: {done} today`),
        if done == 0
            then b.ui.empty("No session yet", "Start one: the timer runs for 25 minutes.")
            else b.ui.text(`Focused for {duration(done)}.`),
        done > 0 and done % 4 == 0 and b.ui.text("Four in a row: time for a longer break.", true) or nil,
        b.ui.column(sessions),
        b.ui.row({ b.ui.run("Start", "start"), b.ui.run("Break", "break") }),
        b.ui.chart({
            kind = "bar",
            title = "Sessions, the last seven days",
            labels = labels,
            series = { { name = "Sessions", values = counts } },
        }),
    })
end, nil, { label = "Pomodoro", icon = "timer" })
```
