---
tags: [extension]
permissions: [write]
description: Focus sessions — a 25-minute timer, each session logged in today's note, the week as a chart.
---
# Pomodoro

"Pomodoro: start" asks what to focus on, starts the extension's own timer and logs the session in
today's note as a line tagged `#pomodoro`; "Pomodoro: focus on this line" does the same for the task
or line under the cursor. The timer counts down under the top bar in every view; when it ends, its
notification says what to do next and the `"timer"` hook notes the end of the session in the
journal (`✓ 09:30`). The view "Pomodoro" counts today's sessions and charts the last seven days. The
length of a session is a setting of this device (`b.settings`); the text of a line without its
dates comes from [[Wspólne funkcje]].

```markdown
- 🍅 09:05 Quarterly report ([[Work]]) #pomodoro
```

## Sessions

```luau
local BREAK_MINUTES = 5
local TAG = "pomodoro"
local withoutDates = b.require("Wspólne funkcje").withoutDates

b.settings({
    { key = "focus", label = "A session", value = 25, step = 5, unit = "min",
      description = "How long the timer of a focus session runs." },
})

local function focusMinutes(): number
    return math.clamp(math.floor(tonumber(b.setting("focus")) or 25), 1, 240)
end

-- A line's text as a session's name: without task dates and tags, one line, not too long.
local function subject(text: string): string
    text = withoutDates(text)
    text = text:gsub("%s*#[^%s#]+", ""):gsub("%s+", " ")
    text = text:match("^%s*(.-)%s*$")
    local stop = utf8.offset(text, 81) -- 80 characters at most, never half a letter
    return if stop then text:sub(1, stop - 1) .. "…" else text
end

-- "2 h 5 min" for 5 sessions.
local function duration(sessions: number): string
    local minutes = sessions * focusMinutes()
    if minutes < 60 then
        return `{minutes} min`
    end
    return `{minutes // 60} h {minutes % 60} min`
end

local function start(what: string, page: string?): string
    local link = if page and page ~= "" then ` ([[{page}]])` else ""
    b.appendToJournal(b.today(), `- 🍅 {os.date("%H:%M")} {what}{link} #{TAG}`)
    b.timer(focusMinutes() * 60, { label = what, message = "The session is over: take a break." })
    return `Focus for {focusMinutes()} minutes: {what}`
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
    b.timer(BREAK_MINUTES * 60, { label = "Break", message = "The break is over: back to work." })
    return `A break of {BREAK_MINUTES} minutes`
end, { label = "Pomodoro: take a break" })

-- The session's end in the journal, under its line: when the timer of a session ran out.
b.on("timer", function(e)
    if e.label ~= "Break" then
        b.appendToJournal(b.today(), `- ✓ {os.date("%H:%M")} {e.label} #{TAG}-end`)
    end
end)
```

## The view

A piece that is not always there is written as `condition and node or nil`: `b.ui.column` and
`b.ui.row` leave a `nil` out.

```luau
b.view("today", function(params)
    local today = b.today()
    local perDay, sessions = {}, {}
    for _, block in b.blocks({ tag = TAG, from = b.addDays(today, -6), to = today }) do
        perDay[block.date] = (perDay[block.date] or 0) + 1
        if block.date == today then
            local time = block.text:match("^🍅%s*(%d%d?:%d%d)") or ""
            table.insert(sessions, b.ui.row({ b.ui.text(time, true), b.ui.text(logged(block.text)) }))
        end
    end
    local labels, counts = {}, {}
    for i = -6, 0 do
        local date = b.addDays(today, i)
        table.insert(labels, date:sub(6))
        table.insert(counts, perDay[date] or 0)
    end
    local done = #sessions
    return b.ui.column({
        b.ui.heading(`Pomodoro: {done} today`),
        if done == 0
            then b.ui.empty("No session yet", `Start one: the timer runs for {focusMinutes()} minutes.`)
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
