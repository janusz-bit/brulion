--!strict
-- Nawyki (habits): recurring points of the day kept as plain Markdown (docs/SYNTAX.md §5 "Habits")
-- — a habit is defined once (a block or a page with `habit::` / `nawyk::`), each day's check is a
-- task tagged #nawyk / #habit in that day's daily note. Written with the extensions' public API only
-- (docs/EXTENSIONS.md), like the gym.
--
--   - Medytacja
--     habit:: codziennie
--   - [x] Medytacja #nawyk ✅ 2026-11-03      (in journals/2026-11-03.md)
--
-- View: month (params.month "YYYY-MM": a card per habit — today's check, the schedule, streaks,
-- the month's grid). Action: set { name, page, date, state }. Commands: add, tick (the palette);
-- list, set, schedule (palette = false: brulion-cli habit … and tests run them with ctx.args).
--
-- Dates are local calendar days counted as day numbers (days since 1970-01-01) — no clock, no
-- time zones, so DST and year boundaries change nothing.

type Schedule = {
    kind: string, -- "days" | "weekdays" | "monthDay" | "times"
    interval: number,
    weekdays: { number },
    monthDay: number, -- 0 = the month's last day
    times: number,
    period: string, -- "week" | "month" | "year"
}

type Habit = {
    name: string,
    key: string,
    scheduleText: string,
    schedule: Schedule?,
    start: number?, -- a day number
    path: string,
    line: number, -- 0-based (0 for a page)
    ref: string,
    isPage: boolean,
    duplicate: boolean,
}

type Marks = { days: { [number]: string }, list: { number } } -- "done" | "skipped" | "open"
type Check = { ref: string, key: string, mark: string, path: string, line: number }
type Node = { [string]: any }

local MAX_INTERVAL = 999
local MAX_COUNTED_DAYS = 3660
local MAX_GRID_DAYS = 400
local MAX_BLOCKS = 20000 -- b.blocks gives at most this many
local CHECK_TAGS = { "nawyk", "habit" }
local DEFINING = { "habit", "nawyk" }
local RANK: { [string]: number } = { open = 0, skipped = 1, done = 2 }
local STATUS_CHAR: { [string]: string } = { done = "x", skipped = "-", open = " " }

-- ---- words ------------------------------------------------------------------------------------

-- Written into notes: the notebook's language (b.lang).
local WRITTEN = {
    pl = { tag = "nawyk", page = "Nawyki", listPage = "Lista nawyków", everyDay = "codziennie" },
    en = { tag = "habit", page = "Habits", listPage = "Habit list", everyDay = "every day" },
}

-- The interface's words: the app's language (b.uiLang).
local INTERFACE = {
    pl = {
        title = "Nawyki",
        addLabel = "Nawyki: dodaj nawyk",
        tickLabel = "Nawyki: odhacz nawyk",
        doneOf = "%d z %d zrobione dziś",
        withProblem = { "%d z problemem", "%d z problemem", "%d z problemem" },
        noHabits = "Nie ma jeszcze nawyków",
        noHabitsText = "Dodaj go tutaj albo dopisz do dowolnego punktu właściwość „habit:: codziennie” (albo „pn, śr, pt”, „3 razy w tygodniu”). Odhaczaj tutaj albo w dzienniku: „- [x] Nazwa #nawyk”.",
        addHabit = "Dodaj nawyk",
        streak = "Seria: %s",
        longest = "najdłuższa: %s",
        completed = "wykonano: %s",
        units = {
            days = { "%d dzień", "%d dni", "%d dni" },
            times = { "%d raz", "%d razy", "%d razy" },
            weeks = { "%d tydzień", "%d tygodnie", "%d tygodni" },
            months = { "%d miesiąc", "%d miesiące", "%d miesięcy" },
            years = { "%d rok", "%d lata", "%d lat" },
        },
        periods = {
            week = "%d z %d w tym tygodniu",
            month = "%d z %d w tym miesiącu",
            year = "%d z %d w tym roku",
            any = "%d z %d w tym okresie",
        },
        duplicate = "Zapisany dwa razy — ta definicja nie jest używana",
        notUnderstood = "Niezrozumiały harmonogram: „%s”",
        more = "Więcej",
        doneToday = "Zrobione dziś",
        skipToday = "Pomiń dziś",
        noCheckToday = "Bez odhaczenia dziś",
        openDefinition = "Otwórz definicję",
        openTodaysJournal = "Otwórz dzisiejszy dziennik",
        done = "Zrobione",
        skipped = "Pominięte (seria trwa)",
        noCheck = "Bez odhaczenia",
        openDaysJournal = "Otwórz dziennik tego dnia",
        markedToday = "Zrobione na dziś: %s",
        marked = "Odhaczono: %s",
        daySkipped = "Pominięto dzień: %s",
        removed = "Cofnięto odhaczenie: %s",
        undo = "Cofnij",
        notSaved = "Nie udało się zapisać dziennika.",
        newHabit = "Nowy nawyk",
        name = "Nazwa",
        namePlaceholder = "np. Medytacja",
        howOften = "Jak często",
        schedulePlaceholder = "codziennie, pn śr pt, 3 razy w tygodniu…",
        noName = "Podaj nazwę nawyku",
        taken = "Nawyk o tej nazwie już istnieje",
        scheduleNotUnderstood = "Nie rozumiem — spróbuj „codziennie”, „pn, śr, pt” albo „3 razy w tygodniu”",
        added = "Dodano nawyk na stronie „%s”",
        open = "Otwórz",
        whichHabit = "Który nawyk dziś zrobiony?",
        allDone = "Wszystkie nawyki na dziś zrobione",
        stateWords = "Stan to zrobione, pominięte albo brak (też: done, skipped, open)",
        noSuchHabit = "Nie ma nawyku „%s”",
        badName = "Tej nazwy nie da się zapisać jako odhaczenia",
        badDate = "Niepoprawna data: %s",
        macroStreak = "Seria nawyku, najdłuższa i wykonanie",
        macroHabit = "nawyk",
        macroExample = "Czytanie",
        macroUnknown = "nie ma nawyku „%s”",
        panelToday = "Nawyki dnia",
    },
    en = {
        title = "Habits",
        addLabel = "Habits: add a habit",
        tickLabel = "Habits: tick a habit",
        doneOf = "%d of %d done today",
        withProblem = { "%d with a problem", "%d with a problem" },
        noHabits = "No habits yet",
        noHabitsText = "Add one here, or give any point the property “habit:: every day” (or “mon, wed, fri”, “3 times a week”). Tick it here or in the journal: “- [x] Name #habit”.",
        addHabit = "Add habit",
        streak = "Streak: %s",
        longest = "longest: %s",
        completed = "completed: %s",
        units = {
            days = { "%d day", "%d days" },
            times = { "%d time", "%d times" },
            weeks = { "%d week", "%d weeks" },
            months = { "%d month", "%d months" },
            years = { "%d year", "%d years" },
        },
        periods = {
            week = "%d of %d this week",
            month = "%d of %d this month",
            year = "%d of %d this year",
            any = "%d of %d this period",
        },
        duplicate = "Defined twice — this definition is not used",
        notUnderstood = "Schedule not understood: “%s”",
        more = "More",
        doneToday = "Done today",
        skipToday = "Skip today",
        noCheckToday = "No check today",
        openDefinition = "Open its definition",
        openTodaysJournal = "Open today's journal",
        done = "Done",
        skipped = "Skipped (no streak lost)",
        noCheck = "No check",
        openDaysJournal = "Open the journal of this day",
        markedToday = "Done for today: %s",
        marked = "Marked done: %s",
        daySkipped = "Day skipped: %s",
        removed = "Check removed: %s",
        undo = "Undo",
        notSaved = "The journal could not be saved.",
        newHabit = "New habit",
        name = "Name",
        namePlaceholder = "e.g. Meditation",
        howOften = "How often",
        schedulePlaceholder = "every day, Mon Wed Fri, 3 times a week…",
        noName = "Give the habit a name",
        taken = "A habit of this name exists already",
        scheduleNotUnderstood = "Not understood — try “every day”, “mon, wed, fri” or “3 times a week”",
        added = "Habit added to “%s”",
        open = "Open",
        whichHabit = "Which habit is done today?",
        allDone = "Every habit is done for today",
        stateWords = "The state is done, skipped or open (also: zrobione, pominięte, brak)",
        noSuchHabit = "No habit named %s",
        badName = "The habit's name cannot be written as a check",
        badDate = "Not a date: %s",
        macroStreak = "A habit's streak, its longest and how often it was done",
        macroHabit = "habit",
        macroExample = "Reading",
        macroUnknown = "no habit “%s”",
        panelToday = "The day's habits",
    },
}

local function written()
    return if b.lang() == "en" then WRITTEN.en else WRITTEN.pl
end

local function ui()
    return if b.uiLang() == "pl" then INTERFACE.pl else INTERFACE.en
end

-- Polish plural forms: 1, 2–4 (not 12–14), the rest; English: 1, the rest.
local function plural(n: number, forms: { string }): string
    local form
    if #forms == 2 then
        form = if n == 1 then forms[1] else forms[2]
    elseif n == 1 then
        form = forms[1]
    else
        local mod10, mod100 = n % 10, n % 100
        form = if mod10 >= 2 and mod10 <= 4 and (mod100 < 12 or mod100 > 14) then forms[2] else forms[3]
    end
    return string.format(form, n)
end

-- White space trimmed and single (ASCII white space: names come single-spaced from b.plain).
local function simplify(s: string): string
    return (s:gsub("%s+", " "):gsub("^ ", ""):gsub(" $", ""))
end

-- What habit names are compared by: NFC, case folded, white space single.
local function keyOf(name: string): string
    return simplify(b.lower(name))
end

-- ---- day numbers ------------------------------------------------------------------------------

local function isLeap(y: number): boolean
    return (y % 4 == 0 and y % 100 ~= 0) or y % 400 == 0
end

local MONTH_DAYS = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }

local function daysInMonth(y: number, m: number): number
    return if m == 2 and isLeap(y) then 29 else MONTH_DAYS[m]
end

-- Days since 1970-01-01 of a valid y-m-d (the proleptic Gregorian calendar).
local function fromCivil(y: number, m: number, d: number): number
    y = if m <= 2 then y - 1 else y
    local era = y // 400
    local yoe = y - era * 400
    local mp = (m + 9) % 12
    local doy = (153 * mp + 2) // 5 + d - 1
    local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
    return era * 146097 + doe - 719468
end

local function civil(n: number): (number, number, number)
    n += 719468
    local era = n // 146097
    local doe = n - era * 146097
    local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
    local y = yoe + era * 400
    local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
    local mp = (5 * doy + 2) // 153
    local d = doy - (153 * mp + 2) // 5 + 1
    local m = if mp < 10 then mp + 3 else mp - 9
    return (if m <= 2 then y + 1 else y), m, d
end

-- "2026-11-03" → its day number, nil for anything else.
local function dayOf(date: any): number?
    if type(date) ~= "string" then
        return nil
    end
    local ys, ms, ds = string.match(date, "^(%d%d%d%d)%-(%d%d)%-(%d%d)$")
    if ys == nil then
        return nil
    end
    local y, m, d = tonumber(ys) :: number, tonumber(ms) :: number, tonumber(ds) :: number
    if y < 1 or m < 1 or m > 12 or d < 1 or d > daysInMonth(y, m) then
        return nil
    end
    return fromCivil(y, m, d)
end

local function dateOf(n: number): string
    local y, m, d = civil(n)
    return string.format("%04d-%02d-%02d", y, m, d)
end

-- 1 = Monday … 7 = Sunday (1970-01-01 was a Thursday).
local function weekdayOf(n: number): number
    return (n + 3) % 7 + 1
end

local function addMonths(n: number, k: number): number
    local y, m, d = civil(n)
    local index = y * 12 + (m - 1) + k
    local ny, nm = index // 12, index % 12 + 1
    return fromCivil(ny, nm, math.min(d, daysInMonth(ny, nm)))
end

local function addYears(n: number, k: number): number
    local y, m, d = civil(n)
    return fromCivil(y + k, m, math.min(d, daysInMonth(y + k, m)))
end

-- ---- schedules --------------------------------------------------------------------------------

local function newSchedule(fields: { [string]: any }): Schedule
    local s: Schedule = { kind = "days", interval = 1, weekdays = {}, monthDay = 0, times = 1, period = "week" }
    for k, v in fields do
        (s :: any)[k] = v
    end
    return s
end

local COUNT_WORDS: { [string]: number } = {
    raz = 1, jeden = 1, jednego = 1, dwa = 2, trzy = 3, cztery = 4, piec = 5, szesc = 6, siedem = 7,
    once = 1, one = 1, twice = 2, two = 2, thrice = 3, three = 3, four = 4, five = 5, six = 6, seven = 7,
}

local PERIOD_WORDS: { [string]: string } = {
    tydzien = "week", tygodniu = "week", tygodnie = "week", tygodni = "week", tydz = "week", tyg = "week",
    week = "week", weeks = "week", wk = "week",
    miesiac = "month", miesiacu = "month", miesiace = "month", miesiecy = "month", mies = "month",
    month = "month", months = "month", mo = "month",
    rok = "year", roku = "year", lata = "year", lat = "year", year = "year", years = "year", yr = "year",
}

local PERIOD_DAYS: { [string]: number } = { week = 7, month = 31, year = 366 }

-- A count written as a number or a word ("3", "trzy", "raz", "twice"); 0 when not one.
local function countOf(word: string): number
    if string.match(word, "^%d+$") then
        return tonumber(word) :: number
    end
    return COUNT_WORDS[word] or 0
end

-- The units and separators of "N razy w tygodniu", in the order they are tried.
local UNITS = { "x", "×", "razy", "raz", "times", "time", "dni", "dzien", "days", "day" }
local SEPARATORS = { "w", "na", "a", "an", "per", "every", "each", "in", "in a", "in an" }

-- The rest of a "times" schedule from `at` (after the count): unit? sep (count)? period — the
-- first way it reads, tried in the order a regular expression would ("3 razy w tygodniu",
-- "3x/tydz", "5 razy na 2 tygodnie", "twice a month"). The interval and the period's word or nil.
local function timesRest(s: string, at: number): (string?, string?)
    local function afterSeparator(p: number): (string?, string?)
        p += #(string.match(s, "^%s*", p) :: string)
        -- (\d+ \s*)? then [a-z]+$
        local digits = string.match(s, "^%d+", p)
        if digits then
            local q = p + #digits
            q += #(string.match(s, "^%s*", q) :: string)
            local word = string.match(s, "^[a-z]+$", q)
            if word then
                return digits, word
            end
        end
        local word = string.match(s, "^[a-z]+$", p)
        if word then
            return nil, word
        end
        return nil, nil
    end
    local function separated(p: number): (boolean, string?, string?)
        p += #(string.match(s, "^%s*", p) :: string)
        if string.sub(s, p, p) == "/" then
            local interval, word = afterSeparator(p + 1)
            if word then
                return true, interval, word
            end
        end
        for _, sep in SEPARATORS do
            if string.sub(s, p, p + #sep - 1) == sep then
                local next = string.sub(s, p + #sep, p + #sep)
                if next ~= "" and string.match(next, "[%s%d]") then
                    local interval, word = afterSeparator(p + #sep)
                    if word then
                        return true, interval, word
                    end
                end
            end
        end
        return false, nil, nil
    end
    local p = at + #(string.match(s, "^%s*", at) :: string)
    for _, unit in UNITS do
        if string.sub(s, p, p + #unit - 1) == unit then
            local ok, interval, word = separated(p + #unit)
            if ok then
                return interval, word
            end
        end
    end
    local ok, interval, word = separated(p)
    if ok then
        return interval, word
    end
    return nil, nil
end

-- "3 razy w tygodniu", "3x/tydz", "raz na 2 tygodnie", "twice a month", "3 days a week".
local function parseTimes(folded: string): Schedule?
    -- the count: \d+ or [a-z]+ (longest first; a shorter word leaves letters for the unit)
    local candidates: { string } = {}
    local digits = string.match(folded, "^%d+")
    if digits then
        table.insert(candidates, digits)
    else
        local word = string.match(folded, "^[a-z]+")
        if word then
            for n = #word, 1, -1 do
                table.insert(candidates, string.sub(word, 1, n))
            end
        end
    end
    for _, count in candidates do
        local interval, periodWord = timesRest(folded, #count + 1)
        if periodWord then
            local times = countOf(count)
            local period = PERIOD_WORDS[periodWord]
            if times < 1 or period == nil then
                return nil
            end
            local n = 1
            if interval then
                n = tonumber(interval) :: number
                if n < 1 or n > MAX_INTERVAL then
                    return nil
                end
            end
            if times > PERIOD_DAYS[period] * n then
                return nil
            end
            return newSchedule({ kind = "times", times = times, period = period, interval = n })
        end
    end
    return nil
end

-- A recurrence rule (b.parseRecurrence) as a habit's schedule.
local function fromRule(rule: { [string]: any }): Schedule?
    local interval = rule.interval or 1
    if interval < 1 or interval > MAX_INTERVAL then
        return nil
    end
    local every = rule.every
    if every == "day" then
        if rule.workdays then
            if interval ~= 1 then
                return nil -- every 2nd business day: no habit schedule
            end
            return newSchedule({ kind = "weekdays", weekdays = { 1, 2, 3, 4, 5 } })
        end
        return newSchedule({ kind = "days", interval = interval })
    elseif every == "week" then
        if rule.weekdays or rule.workdays then
            return newSchedule({ kind = "weekdays", interval = interval, weekdays = rule.weekdays or { 1, 2, 3, 4, 5 } })
        end
        return newSchedule({ kind = "times", interval = interval, period = "week" })
    elseif every == "month" then
        if rule.lastDay or rule.monthDay then
            return newSchedule({ kind = "monthDay", interval = interval, monthDay = if rule.lastDay then 0 else rule.monthDay })
        end
        return newSchedule({ kind = "times", interval = interval, period = "month" })
    elseif every == "year" then
        return newSchedule({ kind = "times", interval = interval, period = "year" })
    end
    return nil
end

-- A habit's schedule, the value of its `habit::` property (docs/SYNTAX.md §5 "Habits"); nil when
-- not understood.
local function parseSchedule(text: string): Schedule?
    local folded = b.fold(text)
    -- A sentence's full stop is no part of a count or a weekday list ("co miesiąc 15." keeps its
    -- own: the recurrence parser reads the text as written).
    folded = simplify((folded:gsub("%.+$", "")))
    if folded == "" or folded == "tak" or folded == "yes" or folded == "true" then
        return newSchedule({})
    end
    local times = parseTimes(folded)
    if times then
        return times
    end
    local weekdays = b.parseWeekdays(folded)
    if weekdays and #weekdays > 0 then
        return newSchedule({ kind = "weekdays", weekdays = weekdays })
    end
    local rule = b.parseRecurrence(text)
    if rule then
        return fromRule(rule)
    end
    return nil
end

local function sameList(a: { number }, b2: { number }): boolean
    if #a ~= #b2 then
        return false
    end
    for i, v in a do
        if b2[i] ~= v then
            return false
        end
    end
    return true
end

-- "3 razy w tygodniu", "twice a month", "5 razy na 2 tygodnie".
local function formatTimes(s: Schedule, lang: string): string
    if lang == "pl" then
        local count = if s.times == 1 then "raz" else `{s.times} razy`
        if s.interval == 1 then
            local where = { week = " w tygodniu", month = " w miesiącu", year = " w roku" }
            return count .. where[s.period]
        end
        local units = {
            week = { "tydzień", "tygodnie", "tygodni" },
            month = { "miesiąc", "miesiące", "miesięcy" },
            year = { "rok", "lata", "lat" },
        }
        return `{count} na {plural(s.interval, { "%d " .. units[s.period][1], "%d " .. units[s.period][2], "%d " .. units[s.period][3] })}`
    end
    local count = if s.times == 1 then "once" elseif s.times == 2 then "twice" else `{s.times} times`
    if s.interval == 1 then
        return `{count} a {s.period}`
    end
    return `{count} every {s.interval} {s.period}s`
end

-- The schedule as words in the interface's language; parseSchedule reads them back.
local function formatSchedule(s: Schedule): string
    local lang = if b.uiLang() == "pl" then "pl" else "en"
    if s.kind == "days" then
        return b.formatRecurrence({ every = "day", interval = s.interval }, lang)
    elseif s.kind == "weekdays" then
        if s.interval == 1 and sameList(s.weekdays, { 1, 2, 3, 4, 5 }) then
            return if lang == "pl" then "w dni robocze" else "every weekday"
        end
        if s.interval == 1 then
            local names = {}
            for _, d in s.weekdays do
                table.insert(names, b.weekdayName(d, "short"))
            end
            return table.concat(names, ", ")
        end
        return b.formatRecurrence({ every = "week", interval = s.interval, weekdays = s.weekdays }, lang)
    elseif s.kind == "monthDay" then
        if s.monthDay == 0 then
            return b.formatRecurrence({ every = "month", interval = s.interval, lastDay = true }, lang)
        end
        return b.formatRecurrence({ every = "month", interval = s.interval, monthDay = s.monthDay }, lang)
    end
    if s.times == 1 and s.interval > 1 then
        return b.formatRecurrence({ every = s.period, interval = s.interval }, lang)
    end
    return formatTimes(s, lang)
end

-- The first day of the period holding day `n`.
local function periodStart(period: string, n: number, weekStart: number): number
    if period == "week" then
        return n - (weekdayOf(n) - weekStart + 7) % 7
    end
    local y, m = civil(n)
    if period == "month" then
        return fromCivil(y, m, 1)
    end
    return fromCivil(y, 1, 1)
end

local function monthsBetween(a: number, b2: number): number
    local ya, ma = civil(a)
    local yb, mb = civil(b2)
    return (yb - ya) * 12 + (mb - ma)
end

-- Is day `n` a due day of a days / weekdays / month-day schedule starting on `start`?
local function isScheduled(s: Schedule, start: number, n: number, weekStart: number): boolean
    if n < start then
        return false
    end
    local interval = math.max(1, s.interval)
    if s.kind == "days" then
        return (n - start) % interval == 0
    elseif s.kind == "weekdays" then
        if not table.find(s.weekdays, weekdayOf(n)) then
            return false
        end
        local weeks = (periodStart("week", n, weekStart) - periodStart("week", start, weekStart)) // 7
        return weeks % interval == 0
    elseif s.kind == "monthDay" then
        if monthsBetween(start, n) % interval ~= 0 then
            return false
        end
        local y, m, d = civil(n)
        local last = daysInMonth(y, m)
        local target = if s.monthDay <= 0 then last else math.min(s.monthDay, last)
        return d == target
    end
    return false
end

-- ---- statistics -------------------------------------------------------------------------------

local function addPeriods(period: string, n: number, k: number): number
    if period == "week" then
        return n + 7 * k
    elseif period == "month" then
        return addMonths(n, k)
    end
    return addYears(n, k)
end

local function periodsBetween(period: string, a: number, b2: number): number
    if period == "week" then
        return (b2 - a) // 7
    elseif period == "month" then
        return monthsBetween(a, b2)
    end
    local ya = civil(a)
    local yb = civil(b2)
    return yb - ya
end

-- The block of `interval` periods holding `day`, for a times habit anchored at `start`.
local function blockOf(s: Schedule, start: number, day: number, weekStart: number): (number, number)
    local interval = math.max(1, s.interval)
    local anchor = periodStart(s.period, start, weekStart)
    local n = periodsBetween(s.period, anchor, periodStart(s.period, day, weekStart))
    local index = n // interval -- floor: a day before the anchor belongs to an earlier block
    local first = addPeriods(s.period, anchor, index * interval)
    return first, addPeriods(s.period, first, interval) - 1
end

-- Done and skipped days of [from, to].
local function countIn(m: Marks, from: number, to: number): (number, number)
    local done, skipped = 0, 0
    for d = from, to do
        local mark = m.days[d]
        if mark == "done" then
            done += 1
        elseif mark == "skipped" then
            skipped += 1
        end
    end
    return done, skipped
end

-- What a times block amounts to: done, target, met, excused.
local function evaluate(s: Schedule, m: Marks, first: number, last: number, from: number, today: number)
    local done, skipped = countIn(m, math.max(first, from), math.min(last, today))
    local target = math.max(0, s.times - skipped)
    local met = done > 0 and done >= target
    return done, target, met, not met and target == 0
end

local function unitOf(s: Schedule): string
    if s.kind == "days" then
        return if s.interval == 1 then "days" else "times"
    elseif s.kind ~= "times" then
        return "times"
    end
    return if s.period == "week" then "weeks" elseif s.period == "month" then "months" else "years"
end

-- The habit's start: `start::`, else its first recorded day, else today.
local function effectiveStart(h: Habit, m: Marks, today: number): number
    if h.start then
        return h.start
    end
    local first = m.list[1]
    if first and first <= today then
        return first
    end
    return today
end

local NO_MARKS: Marks = { days = {}, list = {} }

local function statsOf(h: Habit, m: Marks, today: number, weekStart: number)
    local st = {
        start = today, current = 0, longest = 0, unit = "days", done = 0, met = 0, expected = 0,
        dueToday = false, todayMark = "open", todayRecorded = false, periodDone = 0, periodTarget = 0,
        lastDone = nil :: number?,
    }
    local s = h.schedule
    if s == nil then
        return st
    end
    st.unit = unitOf(s)
    st.start = effectiveStart(h, m, today)
    st.todayRecorded = m.days[today] ~= nil
    st.todayMark = m.days[today] or "open"
    for _, d in m.list do
        if d > today then
            break
        end
        if m.days[d] == "done" then
            st.lastDone = d
        end
    end
    if st.start > today then
        return st
    end
    local from = math.max(st.start, today - MAX_COUNTED_DAYS)
    for _, d in m.list do
        if d >= from and d <= today and m.days[d] == "done" then
            st.done += 1
        end
    end
    local run = 0
    if s.kind ~= "times" then -- due days
        for d = from, today do
            if not isScheduled(s, st.start, d, weekStart) then
                continue
            end
            local mark = m.days[d] or "open"
            if mark == "done" then
                run += 1
                st.met += 1
                st.expected += 1
                st.longest = math.max(st.longest, run)
            elseif mark == "skipped" then
                continue
            elseif d < today then
                run = 0
                st.expected += 1
            else
                st.dueToday = true -- today, not done yet: pending
            end
        end
        st.current = run
        return st
    end
    local first, last = blockOf(s, st.start, from, weekStart)
    while first <= today do
        local done, target, met, excused = evaluate(s, m, first, last, from, today)
        local current = last >= today
        local partial = first < from
        if current then
            st.periodDone = done
            st.periodTarget = target
            st.dueToday = not met and not excused
        end
        if met then
            run += 1
            st.met += 1
            st.expected += 1
            st.longest = math.max(st.longest, run)
        elseif excused or current or partial then
            -- not counted
        else
            run = 0
            st.expected += 1
        end
        first, last = blockOf(s, st.start, last + 1, weekStart)
    end
    st.current = run
    return st
end

-- Each day's state of [from, to] (at most 400 days) for the month grid: outside | future | rest |
-- due | done | skipped | missed.
local function daysOf(h: Habit, m: Marks, from: number, to: number, today: number, weekStart: number): { string }
    local out: { string } = {}
    if from > to then
        return out
    end
    if to - from >= MAX_GRID_DAYS then
        to = from + MAX_GRID_DAYS - 1
    end
    local s = h.schedule
    local start = effectiveStart(h, m, today)
    -- A times habit's today is due while its period still needs a day.
    local periodOpen = false
    if s and s.kind == "times" and today >= from and today <= to and start <= today then
        local first, last = blockOf(s, start, today, weekStart)
        local _, _, met, excused = evaluate(s, m, first, last, math.max(start, today - MAX_COUNTED_DAYS), today)
        periodOpen = not met and not excused
    end
    for d = from, to do
        local mark = m.days[d] or "open"
        local state
        if d > today then
            state = "future"
        elseif mark == "done" then
            state = "done"
        elseif mark == "skipped" then
            state = "skipped"
        elseif d < start or s == nil then
            state = "outside"
        elseif s.kind == "times" then
            state = if d == today and periodOpen then "due" else "rest"
        elseif isScheduled(s, start, d, weekStart) then
            state = if d == today then "due" else "missed"
        else
            state = "rest"
        end
        table.insert(out, state)
    end
    return out
end

-- ---- the notebook's habits --------------------------------------------------------------------

local function markOf(task: string?): string
    if task == "x" or task == "X" then
        return "done"
    elseif task == "-" then
        return "skipped"
    end
    return "open"
end

local function stronger(a: string?, b2: string): string
    if a == nil or RANK[b2] > RANK[a] then
        return b2
    end
    return a
end

local function definesHabit(props: { [string]: string }): boolean
    return props.habit ~= nil or props.nawyk ~= nil
end

-- `start::` as written: 2026-10-01, [[2026-10-01]], a YAML date.
local function startOf(props: { [string]: string }): number?
    local value = props.start or props.od
    if value == nil then
        return nil
    end
    local s = simplify(value)
    local inner = string.match(s, "^%[%[(.*)%]%]$")
    if inner then
        s = simplify(inner)
    end
    return dayOf(s)
end

local function makeHabit(name: string, props: { [string]: string }, path: string, line: number, ref: string, isPage: boolean): Habit
    -- the property's value as written, trimmed
    local scheduleText = ((props.habit or props.nawyk or ""):gsub("^%s+", ""):gsub("%s+$", ""))
    return {
        name = name,
        key = keyOf(name),
        scheduleText = scheduleText,
        schedule = parseSchedule(scheduleText),
        start = startOf(props),
        path = path,
        line = line,
        ref = ref,
        isPage = isPage,
        duplicate = false,
    }
end

-- Every block of daily notes carrying `tag` itself, also past b.blocks' 20 000 (read again from
-- the last day read, oldest first).
local function taggedBlocks(tag: string, visit: ({ [string]: any }) -> ())
    local from: string? = nil
    local seen: { [string]: boolean } = {}
    while true do
        local blocks = b.blocks({ tag = tag, journal = true, from = from })
        for _, block in blocks do
            if not seen[block.ref] then
                seen[block.ref] = true
                visit(block)
            end
        end
        if #blocks < MAX_BLOCKS then
            return
        end
        local last = blocks[#blocks].date :: string
        from = if last == from then b.addDays(last, 1) else last
    end
end

-- A check of a daily note's block, or nil: a task tagged #nawyk / #habit (its own tags) that
-- defines no habit, with a name.
local function checkOf(block: { [string]: any }): Check?
    if block.task == nil or definesHabit(block.props) then
        return nil
    end
    local key = keyOf(block.plain)
    if key == "" then
        return nil
    end
    return { ref = block.ref, key = key, mark = markOf(block.task), path = block.path, line = block.line }
end

local function byPlace(a: { path: string, line: number }, c: { path: string, line: number }): boolean
    if a.path ~= c.path then
        return a.path < c.path
    end
    return a.line < c.line
end

-- What the notebook holds, read again only when its notes changed (b.revision).
local cache: {
    revision: number,
    habits: { Habit },
    byKey: { [string]: Habit },
    marks: { [string]: Marks },
}? = nil

local function notebook()
    local revision = b.revision()
    if cache and cache.revision == revision then
        return cache
    end
    local habits: { Habit } = {}
    for _, title in b.pages({ prop = DEFINING }) do
        local page = b.page(title)
        local name = if page then simplify(page.name) else ""
        if page and name ~= "" and definesHabit(page.props) then
            local h = makeHabit(name, page.props, page.path, 0, page.path, true)
            if h.key ~= "" then
                table.insert(habits, h)
            end
        end
    end
    for _, block in b.blocks({ prop = DEFINING }) do
        local name = block.plain :: string
        if name ~= "" then -- leading `habit::` lines of a page: the page is the habit
            table.insert(habits, makeHabit(name, block.props, block.path, block.line - 1, block.ref, false))
        end
    end
    -- Notes by path, then document order (a page's own definition first).
    table.sort(habits, function(a, c)
        if a.path ~= c.path then
            return a.path < c.path
        end
        if a.isPage ~= c.isPage then
            return a.isPage
        end
        return a.line < c.line
    end)
    local byKey: { [string]: Habit } = {}
    for _, h in habits do
        if byKey[h.key] then
            h.duplicate = true
        else
            byKey[h.key] = h
        end
    end
    local marks: { [string]: Marks } = {}
    for _, tag in CHECK_TAGS do
        taggedBlocks(tag, function(block)
            local c = checkOf(block)
            local day = c and dayOf(block.date)
            if c == nil or day == nil then
                return
            end
            local m = marks[c.key]
            if m == nil then
                m = { days = {}, list = {} }
                marks[c.key] = m
            end
            if m.days[day] == nil then
                table.insert(m.list, day)
            end
            m.days[day] = stronger(m.days[day], c.mark)
        end)
    end
    for _, m in marks do
        table.sort(m.list)
    end
    cache = { revision = revision, habits = habits, byKey = byKey, marks = marks }
    return cache :: any
end

local function valid(h: Habit): boolean
    return h.schedule ~= nil and h.key ~= "" and not h.duplicate
end

local function marksOf(nb, h: Habit): Marks
    return nb.marks[h.key] or NO_MARKS
end

-- ---- writing a check ----------------------------------------------------------------------------

-- The checks of day `date` (every daily note of it), by path and line.
local function checksOn(date: string): { Check }
    local out: { Check } = {}
    local seen: { [string]: boolean } = {}
    for _, tag in CHECK_TAGS do
        for _, block in b.blocks({ tag = tag, from = date, to = date }) do
            if not seen[block.ref] then
                seen[block.ref] = true
                local c = checkOf(block)
                if c then
                    table.insert(out, c)
                end
            end
        end
    end
    table.sort(out, byPlace)
    return out
end

-- Sets habit `name`'s mark of day `date` (docs/SYNTAX.md §5 "What Brulion writes"): the day's
-- first check of the habit gets it; without one a new line goes right after the last check of any
-- habit in that note (with its indentation), else as the daily note's last block. "open" sets the
-- day's done and skipped checks back to [ ] and writes nothing without one. `isPage`: the new line
-- links to the habit's page. { changed, before (nil: no check), line? (0-based) } or nil + "refused".
local function setCheck(name: string, isPage: boolean, date: string, state: string)
    local key = keyOf(b.plain(name))
    if key == "" or dayOf(date) == nil or STATUS_CHAR[state] == nil then
        return nil, "refused"
    end
    local all = checksOn(date)
    local mine: { Check } = {}
    local before: string? = nil
    for _, c in all do
        if c.key == key then
            table.insert(mine, c)
            before = stronger(before, c.mark)
        end
    end
    local result = { changed = false, before = before, line = if mine[1] then mine[1].line - 1 else nil }
    if state == "open" then
        for _, c in mine do
            if c.mark ~= "open" then
                b.setTask(c.ref, " ")
                result.changed = true
            end
        end
        return result
    end
    for _, c in mine do
        if c.mark == state then
            result.line = c.line - 1
            return result -- already so
        end
    end
    if #mine > 0 then
        b.setTask(mine[1].ref, STATUS_CHAR[state])
        result.changed = true
        return result
    end
    -- A new line: the name as written (white space single), a page habit's as a link.
    local clean = simplify(name)
    local shown = if isPage then `[[{clean}]]` else clean
    local tag = written().tag
    -- It must read back as this habit's check (a name with a link's alias reads as its label).
    if keyOf(b.plain(`{shown} #{tag}`)) ~= key then
        return nil, "refused"
    end
    local mark = if state == "done" then "✅" else "❌"
    local line = `- [{STATUS_CHAR[state]}] {shown} #{tag} {mark} {b.today()}`
    local others: { Check } = {}
    for _, c in all do
        if c.path == all[1].path then
            table.insert(others, c)
        end
    end
    if #others > 0 then
        local ref = b.insertBlock(others[#others].ref, line, "after")
        local n = string.match(ref, "#L(%d+)@")
        result.line = if n then tonumber(n) :: number - 1 else nil
    else
        b.appendToJournal(date, line)
        result.line = nil
        -- (the index, when it follows writes, has the new line)
        for _, c in checksOn(date) do
            if c.key == key then
                result.line = c.line - 1
                break
            end
        end
    end
    result.changed = true
    result.created = true
    return result
end

-- The words a state is given in (habits' "done", "zrobione" …).
local STATE_WORDS: { [string]: string } = {
    done = "done", x = "done", zrobione = "done", zrobiony = "done", zrobiono = "done", tak = "done", yes = "done",
    skipped = "skipped", skip = "skipped", cancelled = "skipped", ["pominięte"] = "skipped",
    pominiete = "skipped", ["pominięty"] = "skipped", pominiety = "skipped", ["pomiń"] = "skipped",
    pomin = "skipped", open = "open", none = "open", todo = "open", undo = "open", brak = "open", nie = "open",
    no = "open", otwarte = "open",
}

local function stateOf(word: any): string?
    if type(word) ~= "string" then
        return nil
    end
    return STATE_WORDS[(b.lower(word):gsub("^%s+", ""):gsub("%s+$", ""))]
end

-- A view action's or a command's message about a check written, with its undo.
local function checkMessage(name: string, isPage: boolean, date: string, state: string, before: string?)
    local T = ui()
    local message
    if state == "done" then
        message = string.format(if date == b.today() then T.markedToday else T.marked, name)
    elseif state == "skipped" then
        message = string.format(T.daySkipped, name)
    else
        message = string.format(T.removed, name)
    end
    return {
        message = message,
        tone = if state == "done" then "success" else "info",
        button = { label = T.undo, action = "set", args = { name = name, page = isPage, date = date, state = before or "open" } },
    }
end

-- ---- the view ------------------------------------------------------------------------------------

local function nextState(state: string): string
    return if state == "done" or state == "skipped" then "open" else "done"
end

local function setArgs(h: Habit, date: string, state: string)
    return { name = h.name, page = h.isPage, date = date, state = state }
end

local function dayMenu(h: Habit, date: string)
    local T = ui()
    return {
        { label = T.done, action = "set", args = setArgs(h, date, "done") },
        { label = T.skipped, action = "set", args = setArgs(h, date, "skipped") },
        { label = T.noCheck, action = "set", args = setArgs(h, date, "open") },
        { label = T.openDaysJournal, journal = date },
        { label = T.openDefinition, page = h.path, line = h.line + 1 },
    }
end

local function habitMenu(h: Habit, today: string)
    local T = ui()
    return {
        { label = T.doneToday, action = "set", args = setArgs(h, today, "done") },
        { label = T.skipToday, action = "set", args = setArgs(h, today, "skipped") },
        { label = T.noCheckToday, action = "set", args = setArgs(h, today, "open") },
        { label = T.openDefinition, page = h.path, line = h.line + 1 },
        { label = T.openTodaysJournal, journal = today },
    }
end

local function rateText(st): string?
    if st.expected <= 0 then
        return nil
    end
    return `{math.floor(st.met / st.expected * 100 + 0.5)}%`
end

local function streakLine(h: Habit, st): string
    local T = ui()
    local forms = T.units[st.unit]
    local parts = {
        string.format(T.streak, plural(st.current, forms)),
        string.format(T.longest, plural(st.longest, forms)),
    }
    local rate = rateText(st)
    if rate then
        table.insert(parts, string.format(T.completed, rate))
    end
    local s = h.schedule :: Schedule
    if s.kind == "times" then
        local form = if s.interval ~= 1 then T.periods.any else T.periods[s.period]
        table.insert(parts, string.format(form, st.periodDone, st.periodTarget))
    end
    return table.concat(parts, " · ")
end

local function problemCard(h: Habit): Node
    local T = ui()
    local problem = if h.duplicate then T.duplicate else string.format(T.notUnderstood, h.scheduleText)
    return b.ui.card({
        b.ui.text(h.name),
        b.ui.text(problem, { tone = "danger" }),
        b.ui.link(h.path, T.openDefinition, h.line + 1),
    })
end

local function habitCard(nb, h: Habit, st, today: number, first: number, last: number, weekStart: number): Node
    local m = marksOf(nb, h)
    local todayDate = dateOf(today)
    local mark = if st.todayRecorded then st.todayMark else "open"
    local checked: any = if mark == "done" then true elseif mark == "skipped" then "skipped" else false
    local days = {}
    local states = daysOf(h, m, first, last, today, weekStart)
    for i, state in states do
        local date = dateOf(first + i - 1)
        if state == "future" then
            days[date] = { state = state }
        else
            days[date] = { state = state, action = "set", args = setArgs(h, date, nextState(state)), menu = dayMenu(h, date) }
        end
    end
    local y, mo = civil(first)
    return b.ui.card({
        b.ui.row({
            b.ui.check(checked, "set", setArgs(h, todayDate, nextState(mark)), { menu = habitMenu(h, todayDate), label = h.name }),
            b.ui.text(h.name),
            b.ui.text(formatSchedule(h.schedule :: Schedule), true),
            b.ui.menu(habitMenu(h, todayDate), ui().more),
        }),
        b.ui.text(streakLine(h, st), { icon = "flame", muted = true }),
        b.ui.month({ month = string.format("%04d-%02d", y, mo), days = days }),
    })
end

local function monthView(params: { [string]: any }): Node
    local T = ui()
    local nb = notebook()
    if #nb.habits == 0 then
        return b.ui.column({ b.ui.empty(T.noHabits, T.noHabitsText, "flame"), b.ui.run(T.addHabit, "add") })
    end
    local today = dayOf(b.today()) :: number
    local weekStart = b.weekStart()
    local ty, tm = civil(today)
    local y, m = ty, tm
    if type(params.month) == "string" then
        local ys, ms = string.match(params.month, "^(%d%d%d%d)%-(%d%d)$")
        if ys and tonumber(ms) :: number >= 1 and tonumber(ms) :: number <= 12 and tonumber(ys) :: number >= 1 then
            y, m = tonumber(ys) :: number, tonumber(ms) :: number
        end
    end
    local first = fromCivil(y, m, 1)
    local last = first + daysInMonth(y, m) - 1
    local done, due, problems = 0, 0, 0
    local cards: { Node } = {}
    for _, h in nb.habits do
        if not valid(h) then
            problems += 1
            table.insert(cards, problemCard(h))
            continue
        end
        local st = statsOf(h, marksOf(nb, h), today, weekStart)
        if st.todayRecorded and st.todayMark == "done" then
            done += 1
        elseif st.dueToday then
            due += 1
        end
        table.insert(cards, habitCard(nb, h, st, today, first, last, weekStart))
    end
    local summary = string.format(T.doneOf, done, done + due)
    if problems > 0 then
        summary ..= " · " .. plural(problems, T.withProblem)
    end
    local nodes: { Node } = { b.ui.text(summary, true) }
    for _, card in cards do
        table.insert(nodes, card)
    end
    -- (The month and its arrows are the Habits view's header: HabitsView.qml.)
    return b.ui.column(nodes)
end

-- Macros (docs/EXTENSIONS.md "Macros"): {{ext:nawyki/seria Czytanie}} — a habit's streak in any note.
b.macro("seria", function(ctx)
    local T = ui()
    local name = simplify(ctx.args[1] or "")
    local nb = notebook()
    local h = nb.byKey[keyOf(name)]
    if h == nil or not valid(h) then
        return b.ui.text(string.format(T.macroUnknown, name), { muted = true })
    end
    local st = statsOf(h, marksOf(nb, h), dayOf(b.today()) :: number, b.weekStart())
    return b.ui.text(streakLine(h, st), { tone = if st.current > 0 then "success" else nil })
end, { description = ui().macroStreak, args = { ui().macroHabit }, example = ui().macroExample })

-- A tap on a check or a day: that day's check written (and its toast with Undo).
local function setAction(args)
    local name = if type(args.name) == "string" then args.name else ""
    local date = if type(args.date) == "string" then args.date else b.today()
    local state = stateOf(args.state) or "done"
    local isPage = args.page == true
    local ok, result, problem = pcall(setCheck, name, isPage, date, state)
    if not ok or result == nil then
        if not ok then
            b.log(tostring(result))
        end
        return { message = ui().notSaved, tone = "error" }
    end
    if not result.changed then
        return nil
    end
    return checkMessage(name, isPage, date, state, result.before)
end

b.view("month", monthView, { set = setAction }, { icon = "flame" })

-- The side panel beside a day of the journal (docs/EXTENSIONS.md "Panels"): that day's checks.
local function dayPanel(params: { [string]: any }): Node
    local T = ui()
    local nb = notebook()
    local date = if type(params.date) == "string" and dayOf(params.date) then params.date else b.today()
    local day = dayOf(date) :: number
    local today = dayOf(b.today()) :: number
    local weekStart = b.weekStart()
    local rows: { Node } = {}
    for _, h in nb.habits do
        if valid(h) then
            local m = marksOf(nb, h)
            local mark = m.days[day] or "open"
            local checked: any = if mark == "done" then true elseif mark == "skipped" then "skipped" else false
            local st = statsOf(h, m, today, weekStart)
            table.insert(rows, b.ui.row({
                b.ui.check(checked, "set", setArgs(h, date, nextState(mark)), { menu = dayMenu(h, date), label = h.name }),
                b.ui.text(h.name),
                b.ui.text(plural(st.current, T.units[st.unit]), { icon = "flame", muted = true }),
            }))
        end
    end
    if #rows == 0 then
        return b.ui.text(T.noHabits, true)
    end
    return b.ui.column(rows)
end

b.view("dzien", dayPanel, { set = setAction }, { label = ui().panelToday, icon = "flame", panel = "journal" })

-- ---- commands ---------------------------------------------------------------------------------

local function argsOf(ctx): { [string]: any }
    return if type(ctx.args) == "table" then ctx.args else {}
end

local function scheduleJson(s: Schedule)
    return {
        kind = s.kind, interval = s.interval, weekdays = s.weekdays, monthDay = s.monthDay, times = s.times,
        period = s.period, words = formatSchedule(s),
    }
end

-- One habit as `brulion-cli habit list` gives it (docs/CLI.md).
local function habitJson(nb, h: Habit, today: number, days: number, weekStart: number)
    local o: { [string]: any } = {
        name = h.name, key = h.key, schedule = h.scheduleText, valid = valid(h), path = h.path, line = h.line,
        ref = h.ref,
    }
    if h.duplicate then
        o.problem = "duplicate"
        return o
    end
    if h.schedule == nil then
        o.problem = "schedule_not_understood"
        return o
    end
    local s = h.schedule :: Schedule
    local m = marksOf(nb, h)
    local st = statsOf(h, m, today, weekStart)
    o.scheduleText = formatSchedule(s)
    o.start = dateOf(st.start)
    o.today = { date = dateOf(today), mark = if st.todayRecorded then st.todayMark else "", due = st.dueToday }
    o.streak = { current = st.current, longest = st.longest, unit = st.unit }
    o.done = st.done
    o.met = st.met
    o.expected = st.expected
    if st.expected > 0 then
        o.rate = math.floor(st.met / st.expected * 1000 + 0.5) / 1000
    end
    if s.kind == "times" then
        o.period = { done = st.periodDone, target = st.periodTarget }
    end
    if st.lastDone then
        o.lastDone = dateOf(st.lastDone)
    end
    if days > 0 then
        local from = today - (days - 1)
        local list = {}
        for i, state in daysOf(h, m, from, today, today, weekStart) do
            table.insert(list, { date = dateOf(from + i - 1), state = state })
        end
        o.days = list
    end
    return o
end

b.command("list", function(ctx)
    local args = argsOf(ctx)
    local date = if args.date == nil or args.date == "" then b.today() else args.date
    local today = dayOf(date)
    if today == nil then
        return { error = "bad_argument", message = string.format(ui().badDate, tostring(date)) }
    end
    local days = math.clamp(math.floor(tonumber(args.days) or 7), 0, MAX_GRID_DAYS)
    local nb = notebook()
    local weekStart = b.weekStart()
    local list = {}
    for _, h in nb.habits do
        table.insert(list, habitJson(nb, h, today :: number, days, weekStart))
    end
    local undefined = {}
    for key in nb.marks do
        if nb.byKey[key] == nil then
            table.insert(undefined, key)
        end
    end
    return { date = dateOf(today :: number), habits = list, undefined = b.sorted(undefined) }
end, { palette = false })

-- `brulion-cli habit set`: { name, state, date? } → what was written, or { error, message, … }.
b.command("set", function(ctx)
    local T = ui()
    local args = argsOf(ctx)
    local state = stateOf(args.state)
    if state == nil then
        return { error = "bad_argument", message = T.stateWords, state = args.state }
    end
    local nb = notebook()
    local raw = if type(args.name) == "string" then args.name else ""
    local name = simplify(raw)
    local h = nb.byKey[keyOf(b.plain(raw))] -- (the name's first line, as a block's)
    if h == nil then
        local names = {}
        for _, other in nb.habits do
            if not other.duplicate then
                table.insert(names, other.name)
            end
        end
        return { error = "habit_not_found", message = string.format(T.noSuchHabit, name), name = name, habits = names }
    end
    local date = if args.date == nil or args.date == "" then b.today() else args.date
    if dayOf(date) == nil then
        return { error = "bad_argument", message = string.format(T.badDate, tostring(date)) }
    end
    local result, problem = setCheck(h.name, h.isPage, date, state)
    if result == nil then
        return { error = "bad_name", message = T.badName, problem = problem }
    end
    return {
        habit = h.name, key = h.key, date = date, mark = state, before = result.before or "",
        changed = result.changed, created = result.created == true, line = result.line, isPage = h.isPage,
    }
end, { palette = false })

-- What a habit's schedule reads as: { kind, interval, weekdays, monthDay, times, period, words }.
b.command("schedule", function(ctx)
    local text = argsOf(ctx).text
    local s = parseSchedule(if type(text) == "string" then text else "")
    if s == nil then
        return { error = "not_understood" }
    end
    return scheduleJson(s)
end, { palette = false })

b.command("tick", function(ctx)
    local T = ui()
    local nb = notebook()
    local today = dayOf(b.today()) :: number
    local weekStart = b.weekStart()
    local names: { string } = {}
    local byName: { [string]: Habit } = {}
    for _, h in nb.habits do
        if valid(h) then
            local st = statsOf(h, marksOf(nb, h), today, weekStart)
            if not (st.todayRecorded and st.todayMark == "done") then
                table.insert(names, h.name)
                byName[h.name] = h
            end
        end
    end
    if #nb.habits == 0 then
        return T.noHabits
    end
    if #names == 0 then
        return T.allDone
    end
    local chosen = b.choose(T.whichHabit, names)
    if chosen == nil or byName[chosen] == nil then
        return nil
    end
    local h = byName[chosen]
    local date = b.today()
    local result = setCheck(h.name, h.isPage, date, "done")
    if result == nil then
        return { message = T.notSaved, tone = "error" }
    end
    if not result.changed then
        return nil
    end
    return checkMessage(h.name, h.isPage, date, "done", result.before)
end, { label = INTERFACE[if b.uiLang() == "pl" then "pl" else "en"].tickLabel })

b.command("add", function(ctx)
    local T = ui()
    local W = written()
    local name, schedule, why = "", "", nil
    while true do
        local title = if why then `{T.newHabit}\n{why}` else T.newHabit
        local answer = b.form(title, {
            { key = "name", label = T.name, kind = "text", value = name, placeholder = T.namePlaceholder },
            { key = "schedule", label = T.howOften, kind = "text", value = schedule, placeholder = T.schedulePlaceholder },
        })
        if answer == nil then
            return nil
        end
        name = if type(answer.name) == "string" then answer.name else tostring(answer.name or "")
        schedule = if type(answer.schedule) == "string" then answer.schedule else tostring(answer.schedule or "")
        local clean = b.plain(name)
        local words = simplify(schedule)
        local nb = notebook()
        if clean == "" then
            why = T.noName
        elseif nb.byKey[keyOf(clean)] then
            why = T.taken
        elseif words ~= "" and parseSchedule(words) == nil then
            why = T.scheduleNotUnderstood
        else
            -- The page of the first habit defined by a block, else "Nawyki" / "Habits".
            local page, title2 = W.page, W.page
            -- "Nawyki" may name this extension's own code ("Edit the code": Rozszerzenia/Nawyki.md):
            -- habits never go into an extension page.
            local named = b.page(page)
            if named then
                for _, tag in named.tags do
                    if tag == "extension" or tag == "rozszerzenie" then
                        page, title2 = W.listPage, W.listPage
                        break
                    end
                end
            end
            for _, h in nb.habits do
                if not h.isPage then
                    page = (h.path:gsub("%.md$", ""))
                    title2 = b.page(page) and b.page(page).name or page
                    break
                end
            end
            if words == "" then
                words = W.everyDay
            end
            b.appendToPage(page, `- {clean}\n  habit:: {words}`)
            return { message = string.format(T.added, title2), tone = "success", button = { label = T.open, page = page } }
        end
    end
end, { label = INTERFACE[if b.uiLang() == "pl" then "pl" else "en"].addLabel })
