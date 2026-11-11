--!strict
-- Siłownia (gym): workouts written in daily notes as plain Markdown, plans as pages, history,
-- records and progress from the notebook's index — Brulion's first built-in extension, written
-- with the extensions' public API only (docs/EXTENSIONS.md; the format: docs/SYNTAX.md §5 "Gym").
--
--   - Trening A #trening
--     plan:: [[Trening A]]
--     - [x] [[Przysiad]] 100 kg 5×5
--     - [x] [[Wyciskanie leżąc]] 60 kg × 10; 80 kg × 5, 5, 4 @9
--     - [ ] [[Podciąganie]] MC × 10, 8, 6
--
-- Commands: start (a plan into today's note, with progression), add-set (on the line under the
-- caret), from-catalog (an exercise page from the bundled catalog). Views: today (the workout on a
-- phone: tick, steppers, + set, the rest timer), progress (every exercise; one exercise's chart).

type Set = {
    reps: number?,
    weight: number?, -- in `unit`; with `bodyweight` the weight added (negative: assisted)
    unit: string?, -- "kg" | "lb"
    bodyweight: boolean,
    seconds: number?,
    meters: number?,
    rpe: number?,
}

type Entry = {
    exercise: string, -- the link's target or the plain name
    label: string?, -- the alias of [[Target|Alias]]
    linked: boolean,
    sets: { Set },
    notes: string,
}

type Session = { date: string, ref: string, entry: Entry }
-- The API's shapes (docs/EXTENSIONS.md): a view's nodes and the index's blocks.
type Node = { type: string, [string]: any }
type Block = {
    ref: string,
    page: string,
    date: string?,
    text: string,
    task: string?,
    tags: { string },
    props: { [string]: string },
    children: { Block },
}

local KG_PER_LB = 0.45359237
local WEIGHT_UNITS: { [string]: string } = { kg = "kg", lb = "lb", lbs = "lb" }
local DURATION_UNITS: { [string]: number } = { s = 1, sek = 1, sec = 1, min = 60, h = 3600 }
local DISTANCE_UNITS: { [string]: number } = { km = 1000, m = 1 }
local BODYWEIGHT_WORDS: { [string]: boolean } = { mc = true, bw = true }
-- (patterns work on bytes: each emoji on its own, never in a [set])
local TASK_FIELDS = { "✅", "❌", "➕", "📅", "⏳", "🛫" }

-- Words written into notes: the notebook's language (b.lang). Both languages are always read.
local WRITTEN = {
    pl = {
        workoutTags = { "trening", "workout" },
        planTags = { "plan-treningowy", "workout-plan" },
        tag = "trening",
        bodyweight = "MC",
        muscles = "mięśnie",
        equipment = "sprzęt",
        category = "kategoria",
    },
    en = {
        workoutTags = { "workout", "trening" },
        planTags = { "workout-plan", "plan-treningowy" },
        tag = "workout",
        bodyweight = "BW",
        muscles = "muscles",
        equipment = "equipment",
        category = "category",
    },
}

-- The interface's words: the app's language (b.uiLang).
local INTERFACE = {
    pl = {
        start = "Siłownia: zacznij trening",
        addSet = "Siłownia: dodaj serię",
        tidy = "Siłownia: popraw zapis serii",
        newExercise = "Siłownia: ćwiczenie z katalogu",
        whichPlan = "Który plan?",
        noPlans = "Brak planów. Plan to strona z tagiem #plan-treningowy i listą ćwiczeń, np. „[[Przysiad]] 3×5”.",
        cancelled = "Anulowano",
        started = "Trening „%s” dopisany do dzisiejszej notatki",
        notExercise = "Ustaw kursor w wierszu ćwiczenia, np. „[[Przysiad]] 100 kg 3×5”",
        set = "Nowa seria",
        weight = "Ciężar",
        reps = "Powtórzenia",
        rest = "Przerwa",
        whichExercise = "Jakie ćwiczenie? (część nazwy)",
        pickExercise = "Wybierz ćwiczenie",
        nothingFound = "Nie ma takiego ćwiczenia w katalogu",
        created = "Utworzono stronę „%s”",
        exists = "Strona „%s” już istnieje",
        noHistory = "brak wpisów",
        record = "Rekord: %s (szac. 1RM %s), %s",
        estimated = "Szac. 1RM",
        heaviest = "Najcięższa seria",
        date = "Data",
        sets = "Serie",
        volume = "Objętość",
        exercises = "Ćwiczenia",
        noExercises = "Brak historii",
        noExercisesText = "Treningi to bloki z tagiem #trening w notatkach dziennika. Zacznij od planu albo dopisz ćwiczenie, np. „- [[Przysiad]] 100 kg 5×5 #trening”.",
        noWorkout = "Dziś bez treningu",
        noWorkoutText = "„Zacznij trening” dopisze do dzisiejszej notatki ćwiczenia z planu, z ciężarem z ostatniej sesji.",
        startButton = "Zacznij trening",
        addSetButton = "+ seria",
        removeSetButton = "− seria",
        sessions = { "%d sesja", "%d sesje", "%d sesji" }, -- 1, 2–4 (not 12–14), the rest
        openPage = "Strona ćwiczenia",
        allExercises = "Wszystkie ćwiczenia",
    },
    en = {
        start = "Gym: start a workout",
        addSet = "Gym: add a set",
        tidy = "Gym: tidy the sets",
        newExercise = "Gym: exercise from the catalog",
        whichPlan = "Which plan?",
        noPlans = "No plans yet. A plan is a page tagged #workout-plan with a list of exercises, e.g. “[[Squat]] 3×5”.",
        cancelled = "Cancelled",
        started = "Workout “%s” added to today's note",
        notExercise = "Put the cursor on an exercise line, e.g. “[[Squat]] 100 kg 3×5”",
        set = "New set",
        weight = "Weight",
        reps = "Reps",
        rest = "Rest",
        whichExercise = "Which exercise? (part of the name)",
        pickExercise = "Choose an exercise",
        nothingFound = "No such exercise in the catalog",
        created = "Created the page “%s”",
        exists = "The page “%s” already exists",
        noHistory = "no entries",
        record = "Record: %s (est. 1RM %s), %s",
        estimated = "Est. 1RM",
        heaviest = "Heaviest set",
        date = "Date",
        sets = "Sets",
        volume = "Volume",
        exercises = "Exercises",
        noExercises = "No history yet",
        noExercisesText = "Workouts are blocks tagged #workout in daily notes. Start from a plan or write an exercise, e.g. “- [[Squat]] 100 kg 5×5 #workout”.",
        noWorkout = "No workout today",
        noWorkoutText = "“Start a workout” adds a plan's exercises to today's note, with the weights of the last session.",
        startButton = "Start a workout",
        addSetButton = "+ set",
        removeSetButton = "− set",
        sessions = { "%d session", "%d sessions", "%d sessions" },
        openPage = "Exercise page",
        allExercises = "All exercises",
    },
}

-- "%d sesje": the form of a count (Polish: 1, 2–4 but not 12–14, the rest; English: 1, the rest).
local function counted(forms: { string }, n: number): string
    local form = forms[3]
    if n == 1 then
        form = forms[1]
    elseif n % 10 >= 2 and n % 10 <= 4 and (n % 100 < 12 or n % 100 > 14) then
        form = forms[2]
    end
    return string.format(form, n)
end

local W = if b.lang() == "pl" then WRITTEN.pl else WRITTEN.en
local T = if b.uiLang() == "pl" then INTERFACE.pl else INTERFACE.en

local function trim(s: string): string
    return (s:match("^%s*(.-)%s*$") :: string)
end

local function toNumber(text: string): number
    return tonumber((text:gsub(",", "."))) or 0
end

local function emptySet(load: Set?): Set
    return {
        reps = nil,
        weight = if load then load.weight else nil,
        unit = if load then load.unit else nil,
        bodyweight = if load then load.bodyweight else false,
        seconds = nil,
        meters = nil,
        rpe = nil,
    }
end

-- Parsing ---------------------------------------------------------------------------------------

-- The number at `pos` ("100", "82,5", "7.5"): its last position and its text. Read in two
-- anchored steps on purpose: Luau's patterns backtrack, and one pattern like "%d+[,.]?%d*%s*%a+"
-- is quadratic on a long run of digits (20 000 digits: 140 s, measured).
local function readNumber(low: string, pos: number): (number?, string)
    local _, e = low:find("^%d+", pos)
    if e == nil then
        return nil, ""
    end
    local _, f = low:find("^[,.]%d+", e + 1)
    local last = f or e
    return last, low:sub(pos, last)
end

local function isInteger(text: string): boolean
    return text:find("[,.]") == nil
end

-- The sets after the exercise's name: "60 kg × 10; 80 kg × 5, 5, 4 @9", "100 kg 5×5",
-- "4x8 60 kg", "MC+10 kg 3×8", "3×60 s", "5 km 27:30". Returns the sets and the words it did not
-- understand (kept as notes). Linear in the text; never fails.
local function parseSets(rest: string): ({ Set }, string)
    local src = (rest:gsub("×", "x"))
    local low = src:lower()
    local sets: { Set } = {}
    local notes: { string } = {}
    local load: Set? = nil
    local group: { Set } = {} -- the sets since the last load or rpe
    local unloadedGroup = false -- "4x8" before any weight: a weight right after applies to it
    local pos = 1
    local n = #low

    local function add(set: Set)
        table.insert(sets, set)
        table.insert(group, set)
    end

    local function setLoad(newLoad: Set)
        if unloadedGroup and #group > 0 then
            for _, set in group do
                set.weight, set.unit, set.bodyweight = newLoad.weight, newLoad.unit, newLoad.bodyweight
            end
        else
            group = {}
        end
        unloadedGroup = false
        load = newLoad
    end

    local function addSeconds(seconds: number)
        local last = sets[#sets]
        if last and last.meters and not last.seconds then
            last.seconds = seconds -- "5 km 27:30": the time of that distance
        else
            local set = emptySet(load)
            set.seconds = seconds
            add(set)
        end
    end

    local function addReps(count: number, reps: number)
        for _ = 1, math.min(count, 100) do
            local set = emptySet(load)
            set.reps = reps
            add(set)
        end
    end

    while pos <= n do
        if low:find("^[%s,;:]", pos) then
            pos += 1
            continue
        end

        -- MC / BW (bodyweight), optionally "+10 kg" / "-15 kg"
        local _, wordEnd, word = low:find("^(%a+)", pos)
        if word and wordEnd and BODYWEIGHT_WORDS[word] then
            local bw: Set = emptySet(nil)
            bw.bodyweight = true
            pos = wordEnd + 1
            local _, signEnd, sign = low:find("^%s*([%+%-])%s*", pos)
            if signEnd and sign then
                local numEnd, numText = readNumber(low, signEnd + 1)
                local _, unitEnd, unit = low:find("^%s*(%a+)", (numEnd or signEnd) + 1)
                if numEnd and unitEnd and unit and WEIGHT_UNITS[unit] then
                    bw.weight = toNumber(numText) * (if sign == "-" then -1 else 1)
                    bw.unit = WEIGHT_UNITS[unit]
                    pos = unitEnd + 1
                end
            end
            setLoad(bw)
            continue
        end

        -- "× 5, 5, 4": the reps that follow belong to the current load
        local _, ex = low:find("^x%f[%A]", pos)
        if ex then
            pos = ex + 1
            continue
        end

        -- RPE of the sets since the last load
        if low:find("^@", pos) then
            local _, at = low:find("^@%s*", pos)
            local rpeEnd, rpeText = readNumber(low, (at :: number) + 1)
            if rpeEnd then
                for _, set in group do
                    set.rpe = toNumber(rpeText)
                end
                group = {}
                pos = rpeEnd + 1
                continue
            end
        end

        local numEnd, numText = readNumber(low, pos)
        if numEnd then
            local after = numEnd + 1
            local value = toNumber(numText)
            if isInteger(numText) then
                -- a clock: 27:30, 1:05:00
                local _, m1, mm = low:find("^:(%d%d)%f[%D]", after)
                if m1 and mm then
                    local _, s1, ss = low:find("^:(%d%d)%f[%D]", m1 + 1)
                    if s1 and ss then
                        addSeconds(value * 3600 + toNumber(mm) * 60 + toNumber(ss))
                        pos = s1 + 1
                    else
                        addSeconds(value * 60 + toNumber(mm))
                        pos = m1 + 1
                    end
                    continue
                end
                -- N × duration ("3×60 s") or N × reps ("5×5")
                local _, xEnd = low:find("^%s*x%s*", after)
                if xEnd then
                    local qEnd, qText = readNumber(low, xEnd + 1)
                    if qEnd then
                        local _, ue, unit = low:find("^%s*(%a+)%f[%A]", qEnd + 1)
                        if ue and unit and DURATION_UNITS[unit] then
                            for _ = 1, math.min(value, 100) do
                                local set = emptySet(load)
                                set.seconds = toNumber(qText) * DURATION_UNITS[unit]
                                add(set)
                            end
                            pos = ue + 1
                            continue
                        elseif isInteger(qText) then
                            if load == nil then
                                group = {}
                                unloadedGroup = true
                            end
                            addReps(value, toNumber(qText))
                            pos = qEnd + 1
                            continue
                        end
                    end
                end
            end
            -- a quantity: weight (a new load), duration or distance
            local _, ue, unit = low:find("^%s*(%a+)%f[%A]", after)
            if ue and unit then
                local weightUnit = WEIGHT_UNITS[unit]
                if weightUnit then
                    local newLoad = emptySet(nil)
                    newLoad.weight, newLoad.unit = value, weightUnit
                    setLoad(newLoad)
                    pos = ue + 1
                    continue
                elseif DURATION_UNITS[unit] then
                    addSeconds(value * DURATION_UNITS[unit])
                    pos = ue + 1
                    continue
                elseif DISTANCE_UNITS[unit] then
                    local set = emptySet(load)
                    set.meters = value * DISTANCE_UNITS[unit]
                    add(set)
                    pos = ue + 1
                    continue
                end
            end
            -- a bare number: one set of that many reps ("5,5,4" without a unit: 5, 5, 4)
            local _, intEnd, int = low:find("^(%d+)", pos)
            addReps(1, toNumber(int :: string))
            pos = (intEnd :: number) + 1
            continue
        end

        -- anything else is a note, kept as written
        local _, ew = src:find("^%S+", pos)
        local stop = ew or pos
        table.insert(notes, src:sub(pos, stop))
        pos = stop + 1
    end

    -- (No weight and no MC / BW: the load is not written — a plan's "3×5", a bodyweight exercise
    -- written without MC. Written back as it was.)
    return sets, table.concat(notes, " ")
end

local function startsSets(word: string): boolean
    local w = word:lower()
    return w:find("^%d") ~= nil
        or BODYWEIGHT_WORDS[w] == true
        or w:find("^[mb][cw][%+%-]") ~= nil
        or w:find("^@") ~= nil
end

-- One exercise line: "[[Przysiad]] 100 kg 5×5", "Przysiad 4x8 60kg". Nil for anything else
-- (a workout's title, a note without sets).
local MAX_LINE = 2000 -- bytes; a longer line is not an exercise (and stays cheap to skip)

local function parseEntry(text: string): Entry?
    if #text > MAX_LINE then
        return nil
    end
    local t = " " .. text
    t = t:gsub("%s%^[%w%-]+%s*$", "") -- block id
    for _, field in TASK_FIELDS do -- Obsidian Tasks' dates (the host keeps them when writing)
        t = t:gsub("%s" .. field .. "%s*%d%d%d%d%-%d%d%-%d%d", "")
    end
    local tags: { string } = {}
    for tag in t:gmatch("%s(#[^%s#]+)") do
        table.insert(tags, tag) -- kept, written back after the sets
    end
    t = t:gsub("%s#[^%s#]+", "")
    t = trim(t)

    local exercise, label, rest, linked = "", nil :: string?, "", false
    local inner, after = t:match("^%[%[([^%]]-)%]%](.*)$")
    if inner and after then
        local target, alias = inner:match("^([^|]*)|(.*)$")
        exercise = trim(target or inner)
        label = if alias and trim(alias) ~= "" then trim(alias) else nil
        rest, linked = after, true
    else
        local name: { string } = {}
        local words = t:split(" ")
        local i = 1
        while i <= #words and not startsSets(words[i]) do
            if words[i] ~= "" then
                table.insert(name, words[i])
            end
            i += 1
        end
        exercise = (table.concat(name, " "):gsub(":$", ""))
        rest = table.concat(words, " ", i)
    end
    if exercise == "" then
        return nil
    end
    -- a comment after the sets, kept as written: "(kolano boli 2 dni)", "— lżej", "// rozgrzewka"
    local comment = ""
    local cut = math.min(rest:find("(", 1, true) or #rest + 1, rest:find("—", 1, true) or #rest + 1,
        rest:find("//", 1, true) or #rest + 1)
    if cut <= #rest then
        comment = trim(rest:sub(cut))
        rest = rest:sub(1, cut - 1)
    end
    local sets, words = parseSets(rest)
    if #sets == 0 then
        return nil
    end
    local notes = trim(words .. " " .. comment .. " " .. table.concat(tags, " "))
    return { exercise = exercise, label = label, linked = linked, sets = sets, notes = notes }
end

-- Writing ---------------------------------------------------------------------------------------

local function formatDuration(seconds: number): string
    if seconds >= 120 and seconds % 60 == 0 then
        return `{b.formatNumber(seconds / 60)} min`
    end
    return `{b.formatNumber(seconds)} s`
end

local function formatClock(seconds: number): string
    local s = math.floor(seconds + 0.5)
    local h, m, sec = s // 3600, (s // 60) % 60, s % 60
    if h > 0 then
        return string.format("%d:%02d:%02d", h, m, sec)
    end
    return string.format("%d:%02d", m, sec)
end

local function formatDistance(meters: number): string
    if meters >= 1000 then
        return `{b.formatNumber(meters / 1000, 2)} km`
    end
    return `{b.formatNumber(meters)} m`
end

local function formatLoad(set: Set): string
    if set.bodyweight then
        local w = set.weight
        if w and w ~= 0 then
            return `{W.bodyweight}{if w > 0 then "+" else "-"}{b.formatNumber(math.abs(w))} {set.unit or "kg"}`
        end
        return W.bodyweight
    end
    if set.weight then
        return `{b.formatNumber(set.weight :: number)} {set.unit or "kg"}`
    end
    return ""
end

local function sameGroup(x: Set, y: Set): boolean
    return x.weight == y.weight
        and x.unit == y.unit
        and x.bodyweight == y.bodyweight
        and x.rpe == y.rpe
        and (x.reps == nil) == (y.reps == nil)
        and x.seconds == y.seconds
        and x.meters == nil
        and y.meters == nil
end

local function formatGroup(group: { Set }): string
    local first = group[1]
    local parts: { string } = {}
    local loadText = formatLoad(first)
    if first.meters then
        local s = formatDistance(first.meters :: number)
        if first.seconds then
            s ..= " " .. formatClock(first.seconds :: number)
        end
        table.insert(parts, s)
    elseif first.reps then
        local allSame = true
        local reps: { string } = {}
        for _, set in group do
            table.insert(reps, tostring(set.reps))
            allSame = allSame and set.reps == first.reps
        end
        if loadText ~= "" then
            table.insert(parts, loadText)
        end
        if allSame and #group > 1 then
            table.insert(parts, `{#group}×{first.reps}`)
        else
            table.insert(parts, "× " .. table.concat(reps, ", "))
        end
    elseif first.seconds then
        if loadText ~= "" and not first.bodyweight then
            table.insert(parts, loadText)
        end
        local d = formatDuration(first.seconds :: number)
        table.insert(parts, if #group > 1 then `{#group}×{d}` else d)
    end
    if first.rpe then
        table.insert(parts, "@" .. b.formatNumber(first.rpe :: number))
    end
    return table.concat(parts, " ")
end

-- The line in Brulion's own spelling (what "+ set" and the plans write).
local function formatEntry(entry: Entry): string
    local name = if not entry.linked then entry.exercise
        elseif entry.label then `[[{entry.exercise}|{entry.label}]]`
        else `[[{entry.exercise}]]`
    local groups: { string } = {}
    local current: { Set } = {}
    for _, set in entry.sets do
        if #current > 0 and not sameGroup(current[1], set) then
            table.insert(groups, formatGroup(current))
            current = {}
        end
        table.insert(current, set)
    end
    if #current > 0 then
        table.insert(groups, formatGroup(current))
    end
    local line = name .. " " .. table.concat(groups, "; ")
    if entry.notes ~= "" then
        line ..= " " .. entry.notes
    end
    return line
end

-- Numbers ---------------------------------------------------------------------------------------

local function kilograms(set: Set): number?
    local w = set.weight
    if w == nil or set.bodyweight then
        return nil
    end
    return if set.unit == "lb" then w * KG_PER_LB else w
end

-- Epley's estimate of the one-rep max; nil above 12 reps where it means little.
local function e1rm(set: Set): number?
    local w, r = kilograms(set), set.reps
    if w == nil or r == nil or r < 1 or r > 12 then
        return nil
    end
    return if r == 1 then w else w * (1 + r / 30)
end

local function volume(sets: { Set }): number
    local total = 0
    for _, set in sets do
        total += (kilograms(set) or 0) * (set.reps or 0)
    end
    return total
end

-- History ---------------------------------------------------------------------------------------

local function isDone(task: string?): boolean
    return task == nil or task == "x" or task == "X"
end

-- Every exercise done in a daily note's workout block, oldest first.
local function allSessions(): { Session }
    local out: { Session } = {}
    local seen: { [string]: boolean } = {}
    for _, tag in W.workoutTags do
        for _, workout in b.blocks({ tag = tag, journal = true }) do
            local date = workout.date
            if date == nil or seen[workout.ref] then
                continue
            end
            seen[workout.ref] = true
            local lines = if #workout.children > 0 then workout.children else { workout }
            for _, line in lines do
                if isDone(line.task) then
                    local entry = parseEntry(line.text)
                    if entry then
                        table.insert(out, { date = date, ref = line.ref, entry = entry })
                    end
                end
            end
        end
    end
    -- stable by date: the order inside a day stays the notes' order
    local order: { [Session]: number } = {}
    for i, s in out do
        order[s] = i
    end
    table.sort(out, function(x: Session, y: Session): boolean
        if x.date ~= y.date then
            return x.date < y.date
        end
        return order[x] < order[y]
    end)
    return out
end

local function sessionsOf(exercise: string): { Session }
    local key = b.fold(exercise)
    local out: { Session } = {}
    for _, s in allSessions() do
        if b.fold(s.entry.exercise) == key then
            table.insert(out, s)
        end
    end
    return out
end

type Day = { date: string, sets: { Set } }

-- One row per day (two lines of the same exercise in a day are one session).
local function daysOf(exercise: string): { Day }
    local days: { Day } = {}
    for _, s in sessionsOf(exercise) do
        local last = days[#days]
        if last == nil or last.date ~= s.date then
            last = { date = s.date, sets = {} }
            table.insert(days, last)
        end
        for _, set in s.entry.sets do
            table.insert(last.sets, set)
        end
    end
    return days
end

type Best = { set: Set, estimate: number, date: string }

local function bestOf(days: { Day }): Best?
    local best: Best? = nil
    for _, day in days do
        for _, set in day.sets do
            local estimate = e1rm(set)
            if estimate and (best == nil or estimate > best.estimate) then
                best = { set = set, estimate = estimate, date = day.date }
            end
        end
    end
    return best
end

local function heaviest(sets: { Set }): number?
    local top: number? = nil
    for _, set in sets do
        local w = kilograms(set)
        if w and (top == nil or w > top) then
            top = w
        end
    end
    return top
end

-- Plans -----------------------------------------------------------------------------------------

local function planPages(): { string }
    local names: { string } = {}
    local seen: { [string]: boolean } = {}
    for _, tag in W.planTags do
        for _, name in b.pages({ tag = tag }) do
            if not seen[name] then
                seen[name] = true
                table.insert(names, name)
            end
        end
    end
    return b.sorted(names)
end

-- "+2,5 kg", "+5 lb", "2.5"; nil when the text is not a step.
local function parseStep(text: string?): number?
    if text == nil then
        return nil
    end
    local value = text:match("^%s*%+?%s*(%d+[,.]?%d*)")
    return if value then toNumber(value) else nil
end

-- The sets planned for today: the plan's sets and reps at the last session's top weight, one step
-- heavier when every planned set was done with at least the planned reps there.
local function nextEntry(item: Entry, step: number?): Entry
    local days = daysOf(item.exercise)
    local last = days[#days]
    local planned = item.sets
    local weight: number? = planned[1] and planned[1].weight
    local unit = planned[1] and planned[1].unit
    if last then
        local top = nil :: number?
        local topUnit = nil :: string?
        for _, set in last.sets do
            if set.weight and not set.bodyweight and (top == nil or set.weight > top) then
                top, topUnit = set.weight, set.unit
            end
        end
        if top then
            local done = 0
            local target = planned[1] and planned[1].reps or 0
            for _, set in last.sets do
                if set.weight == top and (set.reps or 0) >= target then
                    done += 1
                end
            end
            local success = done >= #planned
            local increment = step or (if topUnit == "lb" then 5 else 2.5)
            weight, unit = if success then top + increment else top, topUnit
        end
    end
    local sets: { Set } = {}
    for _, p in planned do
        local set = table.clone(p)
        if weight then
            set.weight, set.unit, set.bodyweight = weight, unit, false
        end
        table.insert(sets, set)
    end
    return { exercise = item.exercise, label = item.label, linked = item.linked, sets = sets, notes = "" }
end

local function workoutText(plan: string): string
    local lines = { `- {plan} #{W.tag}`, `  plan:: [[{plan}]]` }
    for _, block in b.blocks({ page = plan }) do
        local item = parseEntry(block.text)
        if item then
            local step = parseStep(block.props["progresja"] or block.props["progression"])
            table.insert(lines, `  - [ ] {formatEntry(nextEntry(item, step))}`)
        end
    end
    return table.concat(lines, "\n")
end

-- Rest between sets: the line's or the workout's `przerwa::` / `rest::`, else 2 minutes.
local function restSeconds(props: { [string]: string }?): number
    local text = props and (props["przerwa"] or props["rest"])
    if text then
        local sets = parseSets(text)
        if sets[1] and sets[1].seconds then
            return sets[1].seconds :: number
        end
    end
    return 120
end

-- A workout's rest: its own property, else its plan page's (`plan:: [[Trening A]]`).
local function workoutRest(workout: Block): number
    local own = workout.props["przerwa"] or workout.props["rest"]
    if own then
        return restSeconds(workout.props)
    end
    local plan = workout.props["plan"] -- "[[Trening A]]", or "Trening A" as the index reads a link
    local target = if plan then (plan:match("%[%[([^%]|]+)") or trim(plan)) else nil
    local page = if target then b.page(target) else nil
    return restSeconds(if page then page.props else nil)
end

-- Commands --------------------------------------------------------------------------------------

b.command("start", function(ctx)
    local plans = planPages()
    if #plans == 0 then
        return T.noPlans
    end
    local plan = b.choose(T.whichPlan, plans)
    if plan == nil then
        return T.cancelled
    end
    b.appendToJournal(b.today(), workoutText(plan))
    return string.format(T.started, plan)
end, { label = T.start })

b.command("tidy", function(ctx)
    local block = ctx.block
    local entry = if block and type(block.text) == "string" then parseEntry(block.text) else nil
    if block == nil or entry == nil then
        return T.notExercise
    end
    b.replaceBlock(block.ref, formatEntry(entry), block.task)
    return nil
end, { label = T.tidy, block = true })

b.command("add-set", function(ctx)
    local block = ctx.block
    local entry = if block and type(block.text) == "string" then parseEntry(block.text) else nil
    if block == nil or entry == nil then
        return T.notExercise
    end
    local last = entry.sets[#entry.sets]
    local answer = b.form(T.set, {
        { key = "weight", label = T.weight, kind = "number", value = last.weight or 0, step = if last.unit == "lb" then 5 else 2.5, unit = last.unit or "kg" },
        { key = "reps", label = T.reps, kind = "number", value = last.reps or 0, step = 1 },
    })
    if answer == nil then
        return T.cancelled
    end
    local set = table.clone(last)
    set.weight = tonumber(answer.weight) or last.weight
    set.reps = tonumber(answer.reps) or last.reps
    set.rpe = nil
    if set.weight and set.weight ~= 0 and set.bodyweight and not last.weight then
        set.bodyweight = false
        set.unit = set.unit or "kg"
    end
    table.insert(entry.sets, set)
    b.replaceBlock(block.ref, formatEntry(entry), block.task)
    b.timer(restSeconds(block.props), T.rest)
    return nil
end, { label = T.addSet, block = true })

type CatalogText = { name: string, category: string, muscles: { string }, equipment: string? }
type CatalogEntry = { id: string, pl: CatalogText, en: CatalogText }

b.command("from-catalog", function(ctx)
    local query = b.fold(b.ask(T.whichExercise))
    if query == "" then
        return T.cancelled
    end
    local catalog = b.data("exercises") :: { CatalogEntry }
    local names: { string } = {}
    local byName: { [string]: CatalogEntry } = {}
    for _, item in catalog do
        local name = if b.lang() == "pl" then item.pl.name else item.en.name
        if b.fold(item.pl.name):find(query, 1, true) or b.fold(item.en.name):find(query, 1, true) then
            table.insert(names, name)
            byName[name] = item
        end
    end
    if #names == 0 then
        return T.nothingFound
    end
    local chosen = if #names == 1 then names[1] else b.choose(T.pickExercise, b.sorted(names))
    if chosen == nil then
        return T.cancelled
    end
    if b.read(chosen) ~= nil then
        return string.format(T.exists, chosen)
    end
    local item = if b.lang() == "pl" then byName[chosen].pl else byName[chosen].en
    local lines = {
        `{W.category}:: {item.category}`,
        `{W.muscles}:: {table.concat(item.muscles, ", ")}`,
    }
    if item.equipment then
        table.insert(lines, `{W.equipment}:: {item.equipment}`)
    end
    b.write(chosen, table.concat(lines, "\n") .. "\n")
    return string.format(T.created, chosen)
end, { label = T.newExercise })

-- Views -----------------------------------------------------------------------------------------

local function recordText(exercise: string): string?
    local best = bestOf(daysOf(exercise))
    if best == nil then
        return nil
    end
    local set = best.set
    return string.format(T.record, `{formatLoad(set)} × {set.reps}`, `{b.formatNumber(best.estimate, 1)} kg`, best.date)
end

local function progressView(exercise: string)
    local days = daysOf(exercise)
    local labels: { string } = {}
    local estimates: { number? } = {}
    local tops: { number? } = {}
    local rows: { { string } } = {}
    for _, day in days do
        local best: number? = nil
        for _, set in day.sets do
            local e = e1rm(set)
            if e and (best == nil or e > best) then
                best = e
            end
        end
        table.insert(labels, day.date)
        table.insert(estimates, best)
        table.insert(tops, heaviest(day.sets))
        local entry: Entry = { exercise = "", linked = false, sets = day.sets, notes = "" }
        table.insert(rows, 1, {
            day.date,
            trim(formatEntry(entry)),
            `{b.formatNumber(volume(day.sets))} kg`,
            if best then `{b.formatNumber(best, 1)} kg` else "—",
        })
    end
    local items: { Node } = {
        b.ui.open(T.allExercises, "progress", {}),
        b.ui.heading(exercise),
    }
    local record = recordText(exercise)
    if record then
        table.insert(items, b.ui.text(record, true))
    end
    if #labels >= 2 then
        table.insert(items, b.ui.chart({
            kind = "line",
            labels = labels,
            series = { { name = T.estimated, values = estimates }, { name = T.heaviest, values = tops } },
        }))
    end
    table.insert(items, b.ui.table({ T.date, T.sets, T.volume, T.estimated }, rows))
    table.insert(items, b.ui.link(exercise, T.openPage))
    return b.ui.column(items)
end

local function exercisesView()
    local names: { string } = {}
    local counts: { [string]: number } = {}
    for _, s in allSessions() do
        local key = b.fold(s.entry.exercise)
        if counts[key] == nil then
            counts[key] = 0
            table.insert(names, s.entry.exercise)
        end
        counts[key] += 1
    end
    if #names == 0 then
        return b.ui.column({ b.ui.empty(T.noExercises, T.noExercisesText), b.ui.run(T.startButton, "start") })
    end
    local items: { Node } = {}
    for _, name in b.sorted(names) do
        local record = recordText(name)
        local count = counted(T.sessions, counts[b.fold(name)])
        table.insert(items, b.ui.row({
            b.ui.open(name, "progress", { exercise = name }),
            b.ui.text(if record then `{record} · {count}` else count, true),
        }))
    end
    return b.ui.column(items)
end

b.view("progress", function(params)
    local exercise = params.exercise
    if type(exercise) == "string" and exercise ~= "" then
        return progressView(exercise)
    end
    return exercisesView()
end)

-- Today's workout on a phone: tick an exercise, change the last set's weight and reps, add or
-- remove a set (adding one starts the rest timer). Every action rewrites one line (replaceBlock).
local function todayView()
    local items: { Node } = {}
    local seen: { [string]: boolean } = {}
    for _, tag in W.workoutTags do
        for _, workout in b.blocks({ tag = tag, journal = true, from = b.today(), to = b.today() }) do
            if seen[workout.ref] then
                continue
            end
            seen[workout.ref] = true
            local rest = workoutRest(workout)
            local lines = if #workout.children > 0 then workout.children else { workout }
            local rows: { Node } = {}
            for _, line in lines do
                local entry = parseEntry(line.text)
                if entry == nil then
                    continue
                end
                local last = entry.sets[#entry.sets]
                local args = { ref = line.ref, text = line.text, task = line.task or "", rest = rest }
                local controls: { Node } = {
                    b.ui.check(line.task == "x" or line.task == "X" or line.task == nil, "toggle", args),
                    b.ui.link(entry.exercise, entry.label),
                    b.ui.text(trim(formatEntry({ exercise = "", linked = false, sets = entry.sets, notes = "" })), true),
                }
                if last.reps then
                    if not last.bodyweight then
                        table.insert(controls, b.ui.stepper({
                            value = last.weight or 0, step = if last.unit == "lb" then 5 else 2.5,
                            unit = last.unit or "kg", action = "weight", args = args,
                        }))
                    end
                    table.insert(controls, b.ui.stepper({ value = last.reps :: number, step = 1, action = "reps", args = args }))
                end
                table.insert(controls, b.ui.button(T.addSetButton, "addSet", args))
                if #entry.sets > 1 then
                    table.insert(controls, b.ui.button(T.removeSetButton, "removeSet", args))
                end
                table.insert(rows, b.ui.row(controls))
            end
            if #rows > 0 then
                table.insert(items, b.ui.heading(trim((" " .. workout.text):gsub("%s#[^%s#]+", ""))))
                for _, row in rows do
                    table.insert(items, row)
                end
            end
        end
    end
    if #items == 0 then
        return b.ui.column({ b.ui.empty(T.noWorkout, T.noWorkoutText), b.ui.run(T.startButton, "start") })
    end
    return b.ui.column(items)
end

local function editLine(args: { [string]: any }, change: (Entry) -> ())
    local entry = parseEntry(args.text :: string)
    if entry == nil then
        return
    end
    change(entry)
    local task = args.task :: string
    b.replaceBlock(args.ref :: string, formatEntry(entry), if task == "" then nil else task)
end

b.view("today", function(params) return todayView() end, {
    toggle = function(args)
        local task = args.task :: string
        local done = task == "x" or task == "X" or task == "" -- a plain line counts as done
        b.replaceBlock(args.ref :: string, args.text :: string, if done then " " else "x")
    end,
    weight = function(args)
        editLine(args, function(entry)
            local value = math.max(0, tonumber(args.value) or 0)
            -- no weight written yet (a plan's "3×5"): it is every set's; else the last set's
            local unloaded = true
            for _, set in entry.sets do
                if set.weight ~= nil or set.bodyweight then
                    unloaded = false
                end
            end
            for i, set in entry.sets do
                if unloaded or i == #entry.sets then
                    set.weight, set.bodyweight, set.unit = value, false, set.unit or "kg"
                end
            end
        end)
    end,
    reps = function(args)
        editLine(args, function(entry)
            entry.sets[#entry.sets].reps = math.max(0, math.floor(tonumber(args.value) or 0))
        end)
    end,
    addSet = function(args)
        editLine(args, function(entry)
            local set = table.clone(entry.sets[#entry.sets])
            set.rpe = nil
            table.insert(entry.sets, set)
        end)
        b.timer(tonumber(args.rest) or 120, T.rest)
    end,
    removeSet = function(args)
        editLine(args, function(entry)
            if #entry.sets > 1 then
                table.remove(entry.sets)
            end
        end)
    end,
})
