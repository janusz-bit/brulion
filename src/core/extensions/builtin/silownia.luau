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
--   - 82,5 kg 18 % rano #waga
--
-- Commands: start (a plan into today's note, with progression), add-set (on the line under the
-- caret), from-catalog (an exercise page from the bundled catalog), weight (a weigh-in into today's
-- note); for the command line (palette = false): volume-report, weights, catalog-search. Views:
-- today (the workout on a phone: tick, steppers, + set, the rest timer), progress (every exercise;
-- one exercise's chart), volume (sets × reps × weight per muscle and week), weight (the body weight
-- log: a chart with its trend, quick entry), catalog (the exercise picker: search, muscle,
-- equipment) and exercise (one exercise of the catalog: muscles, instructions, its page).
--
-- The catalog (exercises.json, exercise-instructions.json) is Brulion's 51 exercises and
-- free-exercise-db's (github.com/yuhonas/free-exercise-db, the Unlicense), made by
-- scripts/gym-catalog.py.

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
        weightTags = { "waga", "weight" },
        weightTag = "waga",
        bodyweight = "MC",
        percent = " %",
        workout = "Trening",
        -- (property keys are ASCII: docs/SYNTAX.md §4)
        catalog = "katalog",
        muscles = "miesnie",
        secondaryMuscles = "miesnie-pomocnicze",
        equipment = "sprzet",
        category = "kategoria",
        level = "poziom",
        instructions = "Wykonanie",
        instructionsNote = "Opis po angielsku, z free-exercise-db (Unlicense).",
    },
    en = {
        workoutTags = { "workout", "trening" },
        planTags = { "workout-plan", "plan-treningowy" },
        tag = "workout",
        weightTags = { "weight", "waga" },
        weightTag = "weight",
        bodyweight = "BW",
        percent = "%",
        workout = "Workout",
        catalog = "catalog",
        muscles = "muscles",
        secondaryMuscles = "secondary-muscles",
        equipment = "equipment",
        category = "category",
        level = "level",
        instructions = "Instructions",
        instructionsNote = "From free-exercise-db (Unlicense).",
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
        nextSet = "Czas na kolejną serię.",
        restSetting = "Przerwa między seriami",
        restSettingNote = "Gdy ani ćwiczenie, ani trening, ani plan nie podają jej we właściwości przerwa::.",
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
        macroRecord = "Rekord ćwiczenia: najlepsza seria i szacowane 1RM",
        macroExercise = "ćwiczenie",
        macroExample = "Przysiad",
        macroNoExercise = "podaj ćwiczenie",
        macroSessions = "Liczba sesji ćwiczenia (ostatnie dni: drugi argument)",
        macroDays = "dni",
        variablePlan = "Trening z planu (z ciężarami z ostatniej sesji)",
        variablePlanArg = "plan",
        unitSetting = "Jednostka masy",
        unitSettingNote = "Masa ciała i objętość w tej jednostce; w niej też ciężar wiersza, który jednostki nie podaje.",
        -- the catalog
        catalog = "Katalog ćwiczeń",
        catalogView = "Siłownia: katalog ćwiczeń",
        catalogNote = "Ćwiczenia Brulionu i free-exercise-db (Unlicense); opisy wykonania po angielsku.",
        searchLabel = "Szukaj ćwiczenia",
        searchPlaceholder = "Nazwa, np. wiosłowanie",
        search = "Szukaj",
        muscle = "Mięsień",
        anyMuscle = "Każdy mięsień",
        equipmentLabel = "Sprzęt",
        anyEquipment = "Każdy sprzęt",
        kind = "Rodzaj",
        anyKind = "Każdy rodzaj",
        exercisesCount = { "%d ćwiczenie", "%d ćwiczenia", "%d ćwiczeń" },
        showMore = "Pokaż więcej",
        clearFilters = "Wyczyść filtry",
        nothingMatches = "Nic nie pasuje",
        nothingMatchesText = "Spróbuj innej nazwy albo wyczyść filtry.",
        addToToday = "Do dzisiejszego treningu",
        addedToToday = "„%s” w dzisiejszym treningu",
        createPage = "Utwórz stronę ćwiczenia",
        primaryMuscles = "Mięśnie główne",
        secondaryMuscles = "Mięśnie pomocnicze",
        level = "Poziom",
        instructions = "Wykonanie",
        instructionsEnglish = "Opis po angielsku, z free-exercise-db.",
        noInstructions = "Katalog nie ma opisu tego ćwiczenia.",
        unknownExercise = "Katalog nie ma takiego ćwiczenia",
        -- weekly volume
        volume = "Objętość",
        volumeTitle = "Objętość tygodniowa",
        volumeView = "Siłownia: objętość tygodniowa",
        volumeNote = "Serie × powtórzenia × ciężar na każdy mięsień; mięśnie pomocnicze liczą się w połowie, serie z masą ciała — z masą z dziennika.",
        thisWeek = "Ten tydzień",
        averageOf = "Średnia (%s)",
        setsThisWeek = "Serie w tym tygodniu",
        againstAverage = "Wobec średniej",
        weeksCount = { "%d tydzień", "%d tygodnie", "%d tygodni" },
        period = "Okres",
        muscleColumn = "Mięsień",
        averageColumn = "Średnio",
        setsColumn = "Serie",
        weekOf = "tydzień od %s",
        cellText = "%s, %s: %s, %s",
        setsCount = { "%s seria", "%s serie", "%s serii" },
        unassigned = "Bez przypisanych mięśni: %s. Dopisz na stronie ćwiczenia „miesnie:: …” albo „katalog:: …”.",
        noVolume = "Brak treningów w tym okresie",
        noVolumeText = "Objętość liczy się z wykonanych serii treningów (#trening) w dzienniku.",
        -- body weight
        bodyWeight = "Masa ciała",
        weightView = "Siłownia: masa ciała",
        weighIn = "Pomiar",
        weighInPlaceholder = "np. 82,5 albo 82,5 kg 18 % rano",
        save = "Zapisz",
        weightSaved = "Zapisano: %s",
        weightNotUnderstood = "Nie rozumiem — wpisz masę, np. „82,5” albo „82,5 kg 18 %”.",
        noWeights = "Brak pomiarów",
        noWeightsText = "Pomiar to punkt dziennika z tagiem #waga, np. „- 82,5 kg #waga”. Wpisz pierwszy powyżej.",
        latest = "Ostatni pomiar",
        trend = "Trend",
        change7 = "Zmiana w 7 dni",
        change30 = "Zmiana w 30 dni",
        bodyFat = "Tkanka tłuszczowa",
        range = "Zakres",
        ranges = { { label = "30 dni", value = "30" }, { label = "90 dni", value = "90" }, { label = "Rok", value = "365" }, { label = "Wszystko", value = "all" } },
        history = "Pomiary",
        openDay = "Otwórz dzień",
        remove = "Usuń",
        removed = "Usunięto pomiar z %s",
        logWeight = "Siłownia: zapisz masę ciała",
        weightField = "Masa",
        fatField = "Tkanka tłuszczowa (0 — bez pomiaru)",
        macroWeight = "Masa ciała: ostatni pomiar i trend",
        trendNote = "trend %s",
        undo = "Cofnij",
        details = "Szczegóły",
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
        nextSet = "Time for the next set.",
        restSetting = "Rest between sets",
        restSettingNote = "When neither the exercise, the workout nor the plan gives one in a rest:: property.",
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
        macroRecord = "An exercise's record: the best set and the estimated 1RM",
        macroExercise = "exercise",
        macroExample = "Squat",
        macroNoExercise = "name an exercise",
        macroSessions = "How many sessions of an exercise (the last days: a second argument)",
        macroDays = "days",
        variablePlan = "A plan's workout (with the weights of the last session)",
        variablePlanArg = "plan",
        unitSetting = "Weight unit",
        unitSettingNote = "Body weight and volume in this unit; the weight of a line that names no unit too.",
        -- the catalog
        catalog = "Exercise catalog",
        catalogView = "Gym: exercise catalog",
        catalogNote = "Brulion's exercises and free-exercise-db's (Unlicense).",
        searchLabel = "Find an exercise",
        searchPlaceholder = "A name, e.g. row",
        search = "Search",
        muscle = "Muscle",
        anyMuscle = "Any muscle",
        equipmentLabel = "Equipment",
        anyEquipment = "Any equipment",
        kind = "Kind",
        anyKind = "Any kind",
        exercisesCount = { "%d exercise", "%d exercises", "%d exercises" },
        showMore = "Show more",
        clearFilters = "Clear the filters",
        nothingMatches = "Nothing matches",
        nothingMatchesText = "Try another name or clear the filters.",
        addToToday = "To today's workout",
        addedToToday = "“%s” is in today's workout",
        createPage = "Create the exercise's page",
        primaryMuscles = "Primary muscles",
        secondaryMuscles = "Secondary muscles",
        level = "Level",
        instructions = "Instructions",
        instructionsEnglish = "",
        noInstructions = "The catalog has no instructions for this exercise.",
        unknownExercise = "No such exercise in the catalog",
        -- weekly volume
        volume = "Volume",
        volumeTitle = "Weekly volume",
        volumeView = "Gym: weekly volume",
        volumeNote = "Sets × reps × weight for every muscle; secondary muscles count half, bodyweight sets with the body weight of the log.",
        thisWeek = "This week",
        averageOf = "Average (%s)",
        setsThisWeek = "Sets this week",
        againstAverage = "Against the average",
        weeksCount = { "%d week", "%d weeks", "%d weeks" },
        period = "Period",
        muscleColumn = "Muscle",
        averageColumn = "Average",
        setsColumn = "Sets",
        weekOf = "week of %s",
        cellText = "%s, %s: %s, %s",
        setsCount = { "%s set", "%s sets", "%s sets" },
        unassigned = "No muscles known: %s. Add “muscles:: …” or “catalog:: …” to the exercise's page.",
        noVolume = "No workouts in this period",
        noVolumeText = "Volume counts the done sets of the workouts (#workout) in the journal.",
        -- body weight
        bodyWeight = "Body weight",
        weightView = "Gym: body weight",
        weighIn = "Weigh-in",
        weighInPlaceholder = "e.g. 82.5 or 82.5 kg 18% morning",
        save = "Save",
        weightSaved = "Noted: %s",
        weightNotUnderstood = "Not understood — type a weight, e.g. “82.5” or “82.5 kg 18%”.",
        noWeights = "No weigh-ins yet",
        noWeightsText = "A weigh-in is a journal item tagged #weight, e.g. “- 82.5 kg #weight”. Type the first one above.",
        latest = "Latest",
        trend = "Trend",
        change7 = "Change in 7 days",
        change30 = "Change in 30 days",
        bodyFat = "Body fat",
        range = "Range",
        ranges = { { label = "30 days", value = "30" }, { label = "90 days", value = "90" }, { label = "A year", value = "365" }, { label = "All", value = "all" } },
        history = "Weigh-ins",
        openDay = "Open the day",
        remove = "Remove",
        removed = "Removed the weigh-in of %s",
        logWeight = "Gym: log body weight",
        weightField = "Weight",
        fatField = "Body fat (0 — not measured)",
        macroWeight = "Body weight: the latest weigh-in and the trend",
        trendNote = "trend %s",
        undo = "Undo",
        details = "Details",
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

-- The gym's own settings (Settings → Extensions → Gym → Settings), kept on the device.
b.settings({
    { key = "rest", label = T.restSetting, description = T.restSettingNote, value = 120, step = 15, unit = "s" },
    { key = "unit", label = T.unitSetting, description = T.unitSettingNote, options = { "kg", "lb" } },
})

-- "kg" or "lb": what body weight and volume are shown in, the weight of a line without a unit.
local function unit(): string
    return if b.setting("unit") == "lb" then "lb" else "kg"
end

-- A count that may be a half ("4,5 serii": the secondary muscles' half sets) in words.
local function countedNumber(forms: { string }, n: number): string
    if n ~= math.floor(n) then
        return string.format(forms[3], b.formatNumber(n, 1))
    end
    local form = forms[3]
    if n == 1 then
        form = forms[1]
    elseif n % 10 >= 2 and n % 10 <= 4 and (n % 100 < 12 or n % 100 > 14) then
        form = forms[2]
    end
    return string.format(form, b.formatNumber(n))
end

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
            return `{W.bodyweight}{if w > 0 then "+" else "-"}{b.formatNumber(math.abs(w))} {set.unit or unit()}`
        end
        return W.bodyweight
    end
    if set.weight then
        return `{b.formatNumber(set.weight :: number)} {set.unit or unit()}`
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
    return if (set.unit or unit()) == "lb" then w * KG_PER_LB else w
end

-- kg in the unit of the setting, and back.
local function shown(kg: number): number
    return if unit() == "lb" then kg / KG_PER_LB else kg
end

-- Epley's estimate of the one-rep max; nil above 12 reps where it means little.
local function e1rm(set: Set): number?
    local w, r = kilograms(set), set.reps
    if w == nil or r == nil or r < 1 or r > 12 then
        return nil
    end
    return if r == 1 then w else w * (1 + r / 30)
end

-- One set's weight × reps in kg; a bodyweight set ("MC × 10", "MC+10 kg × 5") with `bodyKg`, the
-- body weight of the log on that day (none: no volume), plus or minus what it adds.
local function setVolume(set: Set, bodyKg: number?): number
    local reps = set.reps
    if reps == nil or reps <= 0 then
        return 0
    end
    if set.bodyweight then
        if bodyKg == nil then
            return 0
        end
        local added = set.weight or 0
        if set.unit == "lb" then
            added *= KG_PER_LB
        end
        return math.max(0, bodyKg + added) * reps
    end
    return (kilograms(set) or 0) * reps
end

local function volume(sets: { Set }, bodyKg: number?): number
    local total = 0
    for _, set in sets do
        total += setVolume(set, bodyKg)
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

-- The history read once per change of the notebook (b.revision): the views, a page of many macros
-- and the volume of many weeks read it once.
local sessionsCache = { revision = -1, sessions = {} :: { Session } }
local function cachedSessions(): { Session }
    local revision = b.revision()
    if sessionsCache.revision ~= revision then
        sessionsCache = { revision = revision, sessions = allSessions() }
    end
    return sessionsCache.sessions
end

local function sessionsOf(exercise: string): { Session }
    local key = b.fold(exercise)
    local out: { Session } = {}
    for _, s in cachedSessions() do
        if b.fold(s.entry.exercise) == key then
            table.insert(out, s)
        end
    end
    return out
end

type Day = { date: string, sets: { Set } }

-- One row per day (two lines of the same exercise in a day are one session) of one exercise's
-- sessions, oldest first.
local function daysFrom(sessions: { Session }): { Day }
    local days: { Day } = {}
    for _, s in sessions do
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

local function daysOf(exercise: string): { Day }
    return daysFrom(sessionsOf(exercise))
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

-- Rest between sets: the line's or the workout's `przerwa::` / `rest::`, else the setting (2 minutes).
local function restSeconds(props: { [string]: string }?): number
    local text = props and (props["przerwa"] or props["rest"])
    if text then
        local sets = parseSets(text)
        if sets[1] and sets[1].seconds then
            return sets[1].seconds :: number
        end
    end
    return math.clamp(tonumber(b.setting("rest")) or 120, 1, 86400)
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

-- Body weight (docs/SYNTAX.md §5 "Gym": "- 82,5 kg 18 % rano #waga" in a daily note) -------------

type Weighing = {
    date: string,
    ref: string,
    line: number,
    text: string, -- the block's text as the index has it
    task: string?,
    weight: number, -- as written, in `unit`
    unit: string, -- "kg" | "lb"
    kg: number,
    fat: number?, -- per cent of the body
    notes: string, -- the other words and tags, as written
    tag: string, -- the weight tag as written ("#waga", "#Weight")
}

-- Both languages' tags are always read.
local WEIGHT_TAGS: { [string]: boolean } = { waga = true, weight = true }

-- A weigh-in's text: "82,5 kg", "82.5kg 18%", "181 lb rano", "82,5" (in the setting's unit), "18 %"
-- its body fat; the other words and tags are notes. Returns the weight (nil: no weigh-in), its
-- unit, the body fat, the notes and the weight tag as written. Linear in the text (anchored steps,
-- as parseSets).
local function parseWeighing(text: string): (number?, string, number?, string, string)
    if #text > MAX_LINE then
        return nil, "", nil, "", ""
    end
    local t = " " .. text
    t = t:gsub("%s%^[%w%-]+%s*$", "") -- block id
    for _, field in TASK_FIELDS do
        t = t:gsub("%s" .. field .. "%s*%d%d%d%d%-%d%d%-%d%d", "")
    end
    local tag = ""
    local tags: { string } = {}
    for word in t:gmatch("%s(#[^%s#]+)") do
        if tag == "" and WEIGHT_TAGS[b.lower(word:sub(2))] then
            tag = word
        else
            table.insert(tags, word)
        end
    end
    t = trim((t:gsub("%s#[^%s#]+", "")))
    local low = t:lower() -- (ASCII only: the same byte positions as `t`)
    local weight: number? = nil
    local written: string? = nil
    local fat: number? = nil
    local notes: { string } = {}
    local pos, n = 1, #low
    while pos <= n do
        if low:find("^%s", pos) then
            pos += 1
            continue
        end
        local numEnd, numText = readNumber(low, pos)
        if numEnd then
            local _, percentEnd = low:find("^%s*%%", numEnd + 1)
            if percentEnd and fat == nil then
                fat = toNumber(numText)
                pos = percentEnd + 1
                continue
            end
            if weight == nil and percentEnd == nil then
                local _, unitEnd, word = low:find("^%s*(%a+)%f[%A]", numEnd + 1)
                weight = toNumber(numText)
                if unitEnd and word and WEIGHT_UNITS[word] then
                    written = WEIGHT_UNITS[word]
                    pos = unitEnd + 1
                else
                    pos = numEnd + 1
                end
                continue
            end
        end
        local _, wordEnd = t:find("^%S+", pos)
        local stop = wordEnd or pos
        table.insert(notes, t:sub(pos, stop))
        pos = stop + 1
    end
    for _, extra in tags do
        table.insert(notes, extra)
    end
    if weight == nil or weight <= 0 then
        return nil, "", nil, "", tag
    end
    return weight, written or unit(), fat, table.concat(notes, " "), tag
end

-- The line of a weigh-in in Brulion's spelling: "82,5 kg 18 % rano #waga".
local function formatWeighing(weight: number, weightUnit: string, fat: number?, notes: string, tag: string?): string
    local parts = { `{b.formatNumber(weight)} {weightUnit}` }
    if fat then
        table.insert(parts, b.formatNumber(fat :: number, 1) .. W.percent)
    end
    if notes ~= "" then
        table.insert(parts, notes)
    end
    table.insert(parts, if tag and tag ~= "" then tag else "#" .. W.weightTag)
    return table.concat(parts, " ")
end

type WeightDay = { date: string, kg: number, fat: number?, trend: number }

-- Every weigh-in of the journal, oldest first (a day in the order of its note), and the days with
-- their last weigh-in and the trend: an average smoothed by 10 % a day (the Hacker's Diet's), over
-- days without a weigh-in too. Read again only when a note changed.
local weightCache = { revision = -1, entries = {} :: { Weighing }, days = {} :: { WeightDay } }
local function weightLog(): ({ Weighing }, { WeightDay })
    local revision = b.revision()
    if weightCache.revision == revision then
        return weightCache.entries, weightCache.days
    end
    local entries: { Weighing } = {}
    local seen: { [string]: boolean } = {}
    for tag in WEIGHT_TAGS do
        for _, block in b.blocks({ tag = tag, journal = true }) do
            local date = block.date
            if date == nil or seen[block.ref] then
                continue
            end
            seen[block.ref] = true
            local weight, weightUnit, fat, notes, written = parseWeighing(block.text)
            if weight then
                table.insert(entries, {
                    date = date,
                    ref = block.ref,
                    line = tonumber(block.line) or 0,
                    text = block.text,
                    task = block.task,
                    weight = weight,
                    unit = weightUnit,
                    kg = if weightUnit == "lb" then weight * KG_PER_LB else weight,
                    fat = fat,
                    notes = notes,
                    tag = written,
                })
            end
        end
    end
    table.sort(entries, function(x: Weighing, y: Weighing): boolean
        if x.date ~= y.date then
            return x.date < y.date
        end
        return x.line < y.line
    end)
    local days: { WeightDay } = {}
    for _, e in entries do
        local last = days[#days]
        if last and last.date == e.date then
            last.kg = e.kg
            last.fat = e.fat or last.fat
        else
            table.insert(days, { date = e.date, kg = e.kg, fat = e.fat, trend = e.kg })
        end
    end
    local previous: WeightDay? = nil
    for _, day in days do
        if previous then
            local gap = math.max(1, b.daysBetween(previous.date, day.date))
            day.trend = previous.trend + (1 - 0.9 ^ gap) * (day.kg - previous.trend)
        end
        previous = day
    end
    weightCache = { revision = revision, entries = entries, days = days }
    return entries, days
end

-- The last day of `days` on or before `date` (nil: none; `days` oldest first).
local function dayOnOrBefore(days: { WeightDay }, date: string): WeightDay?
    local lo, hi, found = 1, #days, nil :: WeightDay?
    while lo <= hi do
        local mid = (lo + hi) // 2
        if days[mid].date <= date then
            found = days[mid]
            lo = mid + 1
        else
            hi = mid - 1
        end
    end
    return found
end

-- The body weight on `date` for its bodyweight sets: the last weigh-in on or before it, else the
-- first one after it; nil without weigh-ins.
local function bodyWeightOn(days: { WeightDay }, date: string): number?
    local day = dayOnOrBefore(days, date) or days[1]
    return if day then day.kg else nil
end

-- The catalog (exercises.json, exercise-instructions.json: scripts/gym-catalog.py) ---------------

type CatalogItem = {
    id: string,
    pl: string,
    en: string,
    aliases: { string },
    primary: { string },
    secondary: { string },
    equipment: string?,
    category: string,
    level: string?,
    source: string?,
}
type Names = { pl: string, en: string, names: { string }? }
type Catalog = {
    muscles: { [string]: Names },
    equipment: { [string]: Names },
    categories: { [string]: Names },
    levels: { [string]: Names },
    exercises: { CatalogItem },
}

-- Built once per load (the bundled data never changes while the code runs): the exercises by id
-- and by every name and alias folded — names before aliases, Brulion's 51 before the rest:
-- "Przysiad" is the back squat —, their names folded for the search, the muscles by any name.
local catalogData: Catalog? = nil
local catalogById: { [string]: CatalogItem } = {}
local catalogByName: { [string]: CatalogItem } = {}
local catalogText: { [string]: string } = {}
local catalogNames: { [string]: { pl: string, en: string } } = {}
local catalogOrder: { [string]: number } = {}
local muscleByName: { [string]: string } = {}

local function catalog(): Catalog
    if catalogData then
        return catalogData :: Catalog
    end
    local data = b.data("exercises") :: Catalog?
    if data == nil or type(data.exercises) ~= "table" then
        data = { muscles = {}, equipment = {}, categories = {}, levels = {}, exercises = {} }
    end
    local loaded = data :: Catalog
    for i, item in loaded.exercises do
        catalogById[item.id] = item
        catalogOrder[item.id] = i
        local pl, en = b.fold(item.pl), b.fold(item.en)
        catalogNames[item.id] = { pl = pl, en = en }
        for _, key in { pl, en } do
            if catalogByName[key] == nil then
                catalogByName[key] = item
            end
        end
    end
    for _, item in loaded.exercises do
        local words = { catalogNames[item.id].pl, catalogNames[item.id].en }
        for _, alias in item.aliases do
            local key = b.fold(alias)
            table.insert(words, key)
            if catalogByName[key] == nil then
                catalogByName[key] = item
            end
        end
        catalogText[item.id] = " " .. table.concat(words, " ")
    end
    for id, names in loaded.muscles do
        muscleByName[b.fold(id)] = id
        muscleByName[b.fold(names.pl)] = id
        muscleByName[b.fold(names.en)] = id
        for _, name in names.names or {} do
            muscleByName[b.fold(name)] = id
        end
    end
    catalogData = loaded
    return loaded
end

-- A name of the catalog's (a muscle's, an equipment's, a kind's, a level's) in `lang`; an id it
-- does not know is shown as it is (a muscle a page names in its own words).
local function nameIn(table_: { [string]: Names }, id: string, lang: string): string
    local names = table_[id]
    if names == nil then
        return id
    end
    return if lang == "pl" then names.pl else names.en
end

local function exerciseName(item: CatalogItem, lang: string): string
    return if lang == "pl" then item.pl else item.en
end

local function muscleNames(ids: { string }, lang: string): string
    local out: { string } = {}
    for _, id in ids do
        table.insert(out, nameIn(catalog().muscles, id, lang))
    end
    return table.concat(out, ", ")
end

-- The catalog's exercise a note's name means ("Przysiad", "squat", "Wyciskanie leżąc"), or nil.
local function catalogItemOf(name: string): CatalogItem?
    catalog()
    return catalogByName[b.fold(name)]
end

type Filters = { muscle: string?, equipment: string?, category: string? }

-- The exercises whose names or aliases have every word of `query` (any case, any diacritics) and
-- that pass the filters (a primary muscle, an equipment, a kind), the best first: the name itself,
-- a name that starts so, a word that does, Brulion's own 51, names translated to the interface's
-- language, then alphabetically.
local function searchCatalog(query: string, filters: Filters): { CatalogItem }
    local data = catalog()
    local q = b.fold(query)
    local words: { string } = {}
    for word in q:gmatch("%S+") do
        table.insert(words, word)
    end
    local lang = b.uiLang()
    local exact = catalogByName[q]
    local found: { CatalogItem } = {}
    local score: { [string]: number } = {}
    for _, item in data.exercises do
        if filters.equipment and item.equipment ~= filters.equipment then
            continue
        end
        if filters.category and item.category ~= filters.category then
            continue
        end
        if filters.muscle and not table.find(item.primary, filters.muscle) then
            continue
        end
        local text = catalogText[item.id]
        local matches = true
        for _, word in words do
            if not text:find(word, 1, true) then
                matches = false
                break
            end
        end
        if not matches then
            continue
        end
        local name = if lang == "pl" then catalogNames[item.id].pl else catalogNames[item.id].en
        local rank = 3
        if q == "" then
            rank = 3
        elseif item == exact then
            rank = 0
        elseif name:sub(1, #q) == q then
            rank = 1
        elseif (" " .. name):find(" " .. q, 1, true) then
            rank = 2
        end
        local own = if catalogOrder[item.id] <= 51 then 0 else 1
        local translated = if lang ~= "pl" or item.pl ~= item.en then 0 else 1
        score[item.id] = rank * 4 + own * 2 + translated
        table.insert(found, item)
    end
    local sortedNames: { string } = {}
    local byName: { [string]: number } = {}
    for _, item in found do
        table.insert(sortedNames, exerciseName(item, lang))
    end
    for i, name in b.sorted(sortedNames) do
        if byName[name] == nil then
            byName[name] = i
        end
    end
    table.sort(found, function(x: CatalogItem, y: CatalogItem): boolean
        if score[x.id] ~= score[y.id] then
            return score[x.id] < score[y.id]
        end
        local nx, ny = byName[exerciseName(x, lang)], byName[exerciseName(y, lang)]
        if nx ~= ny then
            return nx < ny
        end
        return catalogOrder[x.id] < catalogOrder[y.id]
    end)
    return found
end

-- An exercise page's text from the catalog (from-catalog, the exercise view's "Create"): its
-- catalog id, muscles, equipment, kind and level as properties, the instructions under them.
local function exercisePageText(item: CatalogItem): string
    local data = catalog()
    local lang = b.lang()
    local lines = { `{W.catalog}:: {item.id}`, `{W.muscles}:: {muscleNames(item.primary, lang)}` }
    if #item.secondary > 0 then
        table.insert(lines, `{W.secondaryMuscles}:: {muscleNames(item.secondary, lang)}`)
    end
    if item.equipment then
        table.insert(lines, `{W.equipment}:: {nameIn(data.equipment, item.equipment :: string, lang)}`)
    end
    table.insert(lines, `{W.category}:: {nameIn(data.categories, item.category, lang)}`)
    if item.level then
        table.insert(lines, `{W.level}:: {nameIn(data.levels, item.level :: string, lang)}`)
    end
    local steps = b.data("instructions", item.id)
    if type(steps) == "table" and #steps > 0 then
        table.insert(lines, "")
        table.insert(lines, `## {W.instructions}`)
        table.insert(lines, "")
        if lang == "pl" then
            table.insert(lines, W.instructionsNote)
            table.insert(lines, "")
        end
        for i, step in steps do
            table.insert(lines, `{i}. {step}`)
        end
    end
    return table.concat(lines, "\n") .. "\n"
end

-- Muscles and weekly volume ---------------------------------------------------------------------

-- "a, b; c" → { "a", "b", "c" }
local function splitList(text: string): { string }
    local out: { string } = {}
    for part in text:gmatch("[^,;]+") do
        local name = trim(part)
        if name ~= "" then
            table.insert(out, name)
        end
    end
    return out
end

type Muscles = { primary: { string }, secondary: { string } }

-- The lines "mięśnie:: …" and "mięśnie-pomocnicze:: …" at the top of a page, which are no
-- properties (a key is ASCII): the exercise pages the catalog wrote before 0.24 have them.
local function oldMuscleLines(exercise: string): { [string]: string }
    local out: { [string]: string } = {}
    local lines = b.lines(exercise)
    if lines == nil then
        return out
    end
    for i, line in lines do
        if i > 12 then
            break
        end
        local key, value = line.text:match("^%s*(mi%S+)::%s*(.-)%s*$")
        if key == "mięśnie" then
            out["miesnie"] = value
        elseif key == "mięśnie-pomocnicze" then
            out["miesnie-pomocnicze"] = value
        end
    end
    return out
end

-- The muscles an exercise of the notes works: its page's `miesnie::` (`muscles::`, the primary
-- ones) and `miesnie-pomocnicze::` (`secondary-muscles::`), else the catalog's exercise its page's
-- `katalog::` (`catalog::`) or its name means; none for an exercise the catalog does not know.
-- Names a page gives the catalog does not know are muscle groups of their own.
local function musclesOf(exercise: string): Muscles
    catalog()
    local page = b.page(exercise)
    local props: { [string]: string } = if page then page.props else {}
    if page and props["miesnie"] == nil and props["muscles"] == nil then
        local old = oldMuscleLines(exercise)
        if old["miesnie"] then
            props = table.clone(props)
            props["miesnie"], props["miesnie-pomocnicze"] = old["miesnie"], old["miesnie-pomocnicze"]
        end
    end
    local own = props["miesnie"] or props["muscles"]
    if own then
        local primary: { string } = {}
        local secondary: { string } = {}
        for _, name in splitList(own) do
            local id = muscleByName[b.fold(name)] or b.lower(name)
            if not table.find(primary, id) then
                table.insert(primary, id)
            end
        end
        for _, name in splitList(props["miesnie-pomocnicze"] or props["secondary-muscles"] or "") do
            local id = muscleByName[b.fold(name)] or b.lower(name)
            if not table.find(primary, id) and not table.find(secondary, id) then
                table.insert(secondary, id)
            end
        end
        return { primary = primary, secondary = secondary }
    end
    local id = props["katalog"] or props["catalog"]
    local item = (if id then catalogById[trim(id)] else nil) or catalogByName[b.fold(exercise)]
    if item then
        return { primary = item.primary, secondary = item.secondary }
    end
    return { primary = {}, secondary = {} }
end

-- The first day of the week `date` is in (the notebook's first day of the week).
local function weekOf(date: string): string
    return b.addDays(date, -((b.weekday(date) - b.weekStart() + 7) % 7))
end

type MuscleWeeks = { id: string, volume: { number }, sets: { number } }
type VolumeReport = {
    weeks: { string }, -- the weeks' first days, oldest first; the last is this week
    muscles: { MuscleWeeks }, -- the busiest first (this week and the average together)
    totals: { number }, -- the volume of every week (each set once, whatever its muscles)
    sets: { number },
    first: number, -- the first week with a workout (the average is of the weeks from it)
    unassigned: { string }, -- exercises done whose muscles nobody knows
}

-- Sets × reps × weight (kg) of the done sets, per muscle and week, of the last `weeks` weeks: every
-- primary muscle of an exercise gets its whole volume and every set, every secondary muscle half
-- of both; a bodyweight set counts with the body weight of the log (bodyWeightOn), a timed set
-- or a distance as a set without volume.
local function volumeReport(weeks: number): VolumeReport
    local current = weekOf(b.today())
    local first = b.addDays(current, -7 * (weeks - 1))
    local weekList: { string } = {}
    local index: { [string]: number } = {}
    local totals: { number } = {}
    local setCounts: { number } = {}
    for i = 1, weeks do
        weekList[i] = b.addDays(first, 7 * (i - 1))
        index[weekList[i]] = i
        totals[i] = 0
        setCounts[i] = 0
    end
    local _, days = weightLog()
    local byMuscle: { [string]: MuscleWeeks } = {}
    local muscles: { MuscleWeeks } = {}
    local known: { [string]: Muscles } = {}
    local unassigned: { string } = {}
    local missing: { [string]: boolean } = {}
    local firstWeek = weeks + 1
    local function add(id: string, week: number, amount: number, count: number)
        local row = byMuscle[id]
        if row == nil then
            local volumes: { number } = {}
            local sets: { number } = {}
            for i = 1, weeks do
                volumes[i] = 0
                sets[i] = 0
            end
            row = { id = id, volume = volumes, sets = sets }
            byMuscle[id] = row
            table.insert(muscles, row)
        end
        row.volume[week] += amount
        row.sets[week] += count
    end
    local weekCache: { [string]: number } = {}
    for _, s in cachedSessions() do
        if s.date < first then
            continue
        end
        local week = weekCache[s.date]
        if week == nil then
            week = index[weekOf(s.date)] or 0
            weekCache[s.date] = week
        end
        if week == 0 then
            continue -- a day after today
        end
        firstWeek = math.min(firstWeek, week)
        local key = b.fold(s.entry.exercise)
        local m = known[key]
        if m == nil then
            m = musclesOf(s.entry.exercise)
            known[key] = m
        end
        local bodyKg = bodyWeightOn(days, s.date)
        local amount = volume(s.entry.sets, bodyKg)
        local count = #s.entry.sets
        totals[week] += amount
        setCounts[week] += count
        if #m.primary == 0 and not missing[key] then
            missing[key] = true
            table.insert(unassigned, s.entry.exercise)
        end
        for _, id in m.primary do
            add(id, week, amount, count)
        end
        for _, id in m.secondary do
            add(id, week, amount / 2, count / 2)
        end
    end
    -- the busiest first: this week's volume and the weeks' average together
    local weight: { [string]: number } = {}
    for _, row in muscles do
        local sum = 0
        for i = 1, weeks do
            sum += row.volume[i]
        end
        weight[row.id] = row.volume[weeks] + sum / weeks
    end
    table.sort(muscles, function(x: MuscleWeeks, y: MuscleWeeks): boolean
        if weight[x.id] ~= weight[y.id] then
            return weight[x.id] > weight[y.id]
        end
        return x.id < y.id
    end)
    return {
        weeks = weekList,
        muscles = muscles,
        totals = totals,
        sets = setCounts,
        first = firstWeek,
        unassigned = b.sorted(unassigned),
    }
end

-- The average of `values` over the weeks before the last, from the first week with a workout
-- (nil: no such week).
local function averageBefore(values: { number }, first: number): number?
    local last = #values - 1
    if first > last then
        return nil
    end
    local sum = 0
    for i = first, last do
        sum += values[i]
    end
    return sum / (last - first + 1)
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
        { key = "weight", label = T.weight, kind = "number", value = last.weight or 0, step = if (last.unit or unit()) == "lb" then 5 else 2.5, unit = last.unit or unit() },
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
        set.unit = set.unit or unit()
    end
    table.insert(entry.sets, set)
    b.replaceBlock(block.ref, formatEntry(entry), block.task)
    b.timer(restSeconds(block.props), { label = T.rest, message = T.nextSet })
    return nil
end, { label = T.addSet, block = true })

-- An exercise page from the catalog, named as the notes' language names the exercise; an
-- existing page is kept.
local function createExercisePage(item: CatalogItem): string
    local name = exerciseName(item, b.lang())
    if b.read(name) ~= nil then
        return string.format(T.exists, name)
    end
    b.write(name, exercisePageText(item))
    return string.format(T.created, name)
end

-- The exercise as an open line of today's workout: under today's last workout block (one with
-- exercise lines, or a title of its own), else in a new one. Its sets as a plan writes them.
local function addToToday(item: CatalogItem): string
    local name = exerciseName(item, b.lang())
    local sets = if item.category == "cardio" then "20 min" elseif item.category == "stretching" then "2×30 s" else "3×10"
    local line = `- [ ] [[{name}]] {sets}`
    local today = b.today()
    for _, tag in W.workoutTags do
        local workouts = b.blocks({ tag = tag, journal = true, from = today, to = today })
        local workout = workouts[#workouts]
        -- (a tagged exercise line without children is a workout of its own: left as it is)
        if workout and (#workout.children > 0 or parseEntry(workout.text) == nil) then
            b.insertBlock(workout.ref, line, "child")
            return name
        end
    end
    b.appendToJournal(today, `- {W.workout} #{W.tag}\n  {line}`)
    return name
end

b.command("from-catalog", function(ctx)
    local query = b.ask(T.whichExercise)
    if trim(query) == "" then
        return T.cancelled
    end
    local found = searchCatalog(query, {})
    if #found == 0 then
        return T.nothingFound
    end
    -- the best 50, best first
    local lang = b.lang()
    local names: { string } = {}
    local byName: { [string]: CatalogItem } = {}
    for _, item in found do
        local name = exerciseName(item, lang)
        if byName[name] == nil then
            byName[name] = item
            table.insert(names, name)
        end
        if #names >= 50 then
            break
        end
    end
    local chosen = if #names == 1 then names[1] else b.choose(T.pickExercise, names)
    if chosen == nil then
        return T.cancelled
    end
    return createExercisePage(byName[chosen])
end, { label = T.newExercise })

-- Weighing in (the palette, the weight view's field, `brulion-cli gym weight add`) ---------------

-- A weigh-in in Brulion's spelling at the end of the day's note; returns the line.
local function writeWeighing(date: string, weight: number, weightUnit: string, fat: number?, notes: string): string
    local line = formatWeighing(weight, weightUnit, fat, notes, nil)
    b.appendToJournal(date, "- " .. line)
    return line
end

local function round(x: number, decimals: number): number
    local f = 10 ^ decimals
    return math.floor(x * f + 0.5) / f
end

type WeightSummary = { last: WeightDay?, trend: number?, change7: number?, change30: number?, fat: number?, fatDate: string? }

-- The latest weigh-in, the trend now, the trend's change over the 7 and the 30 days before the
-- latest weigh-in (kg), the latest body fat.
local function weightSummary(days: { WeightDay }): WeightSummary
    local last = days[#days]
    if last == nil then
        return {}
    end
    local function change(n: number): number?
        local before = dayOnOrBefore(days, b.addDays(last.date, -n))
        return if before and before ~= last then last.trend - before.trend else nil
    end
    local fat, fatDate = nil :: number?, nil :: string?
    for i = #days, 1, -1 do
        if days[i].fat then
            fat, fatDate = days[i].fat, days[i].date
            break
        end
    end
    return { last = last, trend = last.trend, change7 = change(7), change30 = change(30), fat = fat, fatDate = fatDate }
end

-- "82,5 kg" in the unit of the setting.
local function formatMass(kg: number, decimals: number?): string
    return `{b.formatNumber(shown(kg), decimals or 1)} {unit()}`
end

local function signed(kg: number): string
    local value = shown(kg)
    local text = b.formatNumber(math.abs(value), 1)
    if text == b.formatNumber(0) then
        return `±0 {unit()}`
    end
    return `{if value > 0 then "+" else "−"}{text} {unit()}`
end

-- ctx.args { weight, unit?, fat?, notes?, date? } or { text = "82,5 kg 18 %" } (the command line);
-- without them a form (the palette), with the last weigh-in in it.
b.command("weight", function(ctx)
    local args = ctx.args or {}
    local date = if type(args.date) == "string" and args.date ~= "" then args.date else b.today()
    if not pcall(b.weekday, date) then
        return { message = `{date}?`, tone = "error" }
    end
    local weight = tonumber(args.weight)
    local weightUnit = if args.unit == "lb" or args.unit == "kg" then args.unit else unit()
    local fat = tonumber(args.fat)
    local notes = if type(args.notes) == "string" then trim(args.notes) else ""
    if weight == nil and type(args.text) == "string" then
        local w, u, f, n = parseWeighing(args.text)
        weight, weightUnit, fat = w, u, f
        notes = trim(n .. " " .. notes)
    end
    if weight == nil and args.weight == nil and args.text == nil then
        local _, days = weightLog()
        local last = days[#days]
        local answer = b.form(T.bodyWeight, {
            { key = "weight", label = T.weightField, value = if last then round(shown(last.kg), 1) else (if unit() == "lb" then 180 else 80), step = 0.1, unit = unit() },
            { key = "fat", label = T.fatField, value = if last and last.fat then last.fat else 0, step = 0.5, unit = "%" },
        })
        if answer == nil then
            return T.cancelled
        end
        weight, fat, weightUnit = tonumber(answer.weight), tonumber(answer.fat), unit()
    end
    if weight == nil or weight <= 0 or weight > 2000 then
        return { message = T.weightNotUnderstood, tone = "warning" }
    end
    if fat and (fat <= 0 or fat >= 100) then
        fat = nil
    end
    local line = writeWeighing(date, weight, weightUnit, fat, notes)
    return { message = string.format(T.weightSaved, line), tone = "success", date = date, line = line }
end, { label = T.logWeight })

-- The command line's data (brulion-cli gym …: docs/CLI.md) ---------------------------------------

-- An id of `table_` named by its id or a name in either language ("chest", "klatka piersiowa");
-- nil for "" or nothing, false for a name it does not know.
local function idNamed(table_: { [string]: Names }, text: any, synonyms: { [string]: string }?): (string | boolean)?
    if type(text) ~= "string" or trim(text) == "" then
        return nil
    end
    local key = b.fold(text)
    for id, names in table_ do
        if b.fold(id) == key or b.fold(names.pl) == key or b.fold(names.en) == key then
            return id
        end
    end
    if synonyms and synonyms[key] then
        return synonyms[key]
    end
    return false
end

local function catalogRow(item: CatalogItem)
    local data = catalog()
    local lang = b.lang()
    return {
        id = item.id,
        name = exerciseName(item, lang),
        pl = item.pl,
        en = item.en,
        aliases = item.aliases,
        primary = item.primary,
        secondary = item.secondary,
        muscles = muscleNames(item.primary, lang),
        secondaryMuscles = muscleNames(item.secondary, lang),
        equipment = item.equipment,
        equipmentName = if item.equipment then nameIn(data.equipment, item.equipment :: string, lang) else nil,
        category = item.category,
        categoryName = nameIn(data.categories, item.category, lang),
        level = item.level,
        source = item.source,
    }
end

-- { query?, muscle?, equipment?, category?, limit?, id? } → { total, exercises = { … } } (with
-- `id`: that exercise and its instructions), or { error, unknown = "muscle" | … }.
b.command("catalog-search", function(ctx)
    local args = ctx.args or {}
    local data = catalog()
    if type(args.id) == "string" then
        local item = catalogById[args.id]
        if item == nil then
            return { error = T.unknownExercise, unknown = "id" }
        end
        local row = catalogRow(item)
        row.instructions = b.data("instructions", item.id) or {}
        return { total = 1, exercises = { row } }
    end
    local filters: Filters = {}
    for _, f in { { key = "muscle", table_ = data.muscles, synonyms = muscleByName }, { key = "equipment", table_ = data.equipment }, { key = "category", table_ = data.categories } } do
        local id = idNamed(f.table_, args[f.key], f.synonyms)
        if id == false then
            return { error = `{args[f.key]}?`, unknown = f.key }
        elseif type(id) == "string" then
            (filters :: any)[f.key] = id
        end
    end
    local found = searchCatalog(if type(args.query) == "string" then args.query else "", filters)
    local limit = math.clamp(math.floor(tonumber(args.limit) or 20), 1, 2000)
    local rows = {}
    for i = 1, math.min(limit, #found) do
        table.insert(rows, catalogRow(found[i]))
    end
    return { total = #found, exercises = rows }
end, { palette = false })

-- { weeks? } → the weekly volume in the setting's unit, every muscle's weeks (docs/CLI.md).
b.command("volume-report", function(ctx)
    local args = ctx.args or {}
    local weeks = math.clamp(math.floor(tonumber(args.weeks) or 12), 1, 104)
    local r = volumeReport(weeks)
    local lang = b.uiLang()
    local muscles = {}
    for _, row in r.muscles do
        local volumes = {}
        for i = 1, weeks do
            volumes[i] = round(shown(row.volume[i]), 1)
        end
        local average = averageBefore(row.volume, r.first)
        table.insert(muscles, {
            id = row.id,
            name = nameIn(catalog().muscles, row.id, lang),
            volume = volumes,
            sets = row.sets,
            current = volumes[weeks],
            currentSets = row.sets[weeks],
            average = if average then round(shown(average), 1) else nil,
        })
    end
    local totals = {}
    for i = 1, weeks do
        totals[i] = round(shown(r.totals[i]), 1)
    end
    local average = averageBefore(r.totals, r.first)
    return {
        unit = unit(),
        weeks = r.weeks,
        totals = totals,
        sets = r.sets,
        current = totals[weeks],
        average = if average then round(shown(average), 1) else nil,
        muscles = muscles,
        unassigned = r.unassigned,
    }
end, { palette = false })

-- { from?, to? } → the weigh-ins (their own units and kg), the trend, the changes (docs/CLI.md).
b.command("weights", function(ctx)
    local args = ctx.args or {}
    local from = if type(args.from) == "string" then args.from else ""
    local to = if type(args.to) == "string" and args.to ~= "" then args.to else "9999-12-31"
    local entries, days = weightLog()
    local trendOf: { [string]: number } = {}
    for _, day in days do
        trendOf[day.date] = day.trend
    end
    local list = {}
    for _, e in entries do
        if e.date >= from and e.date <= to then
            table.insert(list, {
                date = e.date,
                weight = e.weight,
                unit = e.unit,
                kg = round(e.kg, 2),
                fat = e.fat,
                notes = e.notes,
                trend = round(shown(trendOf[e.date]), 2),
                line = e.line,
                text = e.text,
            })
        end
    end
    local s = weightSummary(days)
    return {
        unit = unit(),
        entries = list,
        latest = if s.last then round(shown(s.last.kg), 2) else nil,
        latestDate = if s.last then s.last.date else nil,
        trend = if s.trend then round(shown(s.trend), 2) else nil,
        change7 = if s.change7 then round(shown(s.change7), 2) else nil,
        change30 = if s.change30 then round(shown(s.change30), 2) else nil,
        fat = s.fat,
    }
end, { palette = false })

-- Macros (docs/EXTENSIONS.md "Macros"): {{ext:silownia/rekord Przysiad}} in any note -------------

local function cachedSessionsOf(exercise: string): { Session }
    return sessionsOf(exercise)
end

b.macro("rekord", function(ctx)
    local exercise = trim(ctx.args[1] or "")
    if exercise == "" then
        return b.ui.text(T.macroNoExercise, { muted = true })
    end
    local best = bestOf(daysFrom(cachedSessionsOf(exercise)))
    if best == nil then
        return b.ui.text(T.noHistory, { muted = true })
    end
    return {
        b.ui.heading(`{formatLoad(best.set)} × {best.set.reps}`),
        b.ui.text(` (1RM ≈ {formatMass(best.estimate)}, {best.date})`, { muted = true }),
    }
end, { description = T.macroRecord, args = { T.macroExercise }, example = T.macroExample })

-- {{ext:silownia/sesje Przysiad, 30}}: the sessions of an exercise, of the last 30 days or all.
b.macro("sesje", function(ctx)
    local exercise = trim(ctx.args[1] or "")
    if exercise == "" then
        return b.ui.text(T.macroNoExercise, { muted = true })
    end
    local days = tonumber(ctx.args[2] or "")
    local from = if days and days > 0 then b.addDays(b.today(), -math.floor(days) + 1) else ""
    local count = 0
    for _, day in daysFrom(cachedSessionsOf(exercise)) do
        if day.date >= from then
            count += 1
        end
    end
    return counted(T.sessions, count)
end, { description = T.macroSessions, args = { T.macroExercise, T.macroDays }, example = T.macroExample .. ", 30" })

-- {{ext:silownia/waga}}: the latest weigh-in and the trend.
b.macro("waga", function(ctx)
    local _, days = weightLog()
    local summary = weightSummary(days)
    local last = summary.last
    if last == nil then
        return b.ui.text(T.noHistory, { muted = true })
    end
    return {
        b.ui.heading(formatMass(last.kg)),
        b.ui.text(` ({string.format(T.trendNote, formatMass(last.trend))}, {last.date})`, { muted = true }),
    }
end, { description = T.macroWeight, args = {}, example = "" })

-- Template variables (docs/TEMPLATES.md "Variables of extensions"): {{silownia/plan Nogi}} writes a
-- plan's workout where the template puts it, as "Start a workout" adds it (the first plan by name
-- without an argument; nothing without plans).
b.variable("plan", function(ctx)
    local wanted = b.fold(ctx.args[1] or "")
    for _, plan in planPages() do
        if wanted == "" or b.fold(plan) == wanted then
            return workoutText(plan)
        end
    end
    return ""
end, { description = T.variablePlan, args = { T.variablePlanArg } })

-- Views -----------------------------------------------------------------------------------------

local function recordOf(days: { Day }): string?
    local best = bestOf(days)
    if best == nil then
        return nil
    end
    local set = best.set
    return string.format(T.record, `{formatLoad(set)} × {set.reps}`, formatMass(best.estimate), best.date)
end

local function progressView(exercise: string)
    local days = daysOf(exercise)
    local _, weighed = weightLog()
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
        local top = heaviest(day.sets)
        table.insert(labels, day.date)
        table.insert(estimates, if best then round(shown(best), 1) else nil)
        table.insert(tops, if top then round(shown(top), 1) else nil)
        local entry: Entry = { exercise = "", linked = false, sets = day.sets, notes = "" }
        table.insert(rows, 1, {
            day.date,
            trim(formatEntry(entry)),
            formatMass(volume(day.sets, bodyWeightOn(weighed, day.date))),
            if best then formatMass(best) else "—",
        })
    end
    local items: { Node } = {
        b.ui.open(T.allExercises, "progress", {}),
        b.ui.heading(exercise),
    }
    local record = recordOf(days)
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

-- Every exercise from one pass over the history (not one pass per exercise: the list of a
-- notebook with years of workouts stays fast on a phone).
local function exercisesView()
    local names: { string } = {}
    local byKey: { [string]: { Session } } = {}
    local keyOf: { [string]: string } = {}
    for _, s in cachedSessions() do
        local key = keyOf[s.entry.exercise]
        if key == nil then
            key = b.fold(s.entry.exercise)
            keyOf[s.entry.exercise] = key
        end
        if byKey[key] == nil then
            byKey[key] = {}
            table.insert(names, s.entry.exercise)
        end
        table.insert(byKey[key], s)
    end
    if #names == 0 then
        return b.ui.column({ b.ui.empty(T.noExercises, T.noExercisesText), b.ui.run(T.startButton, "start") })
    end
    local items: { Node } = {}
    for _, name in b.sorted(names) do
        local days = daysFrom(byKey[keyOf[name]])
        local record = recordOf(days)
        local count = counted(T.sessions, #days)
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
                -- A card of the same lines in every card, however long its texts: the exercise,
                -- the last set's steppers, the set buttons.
                local cardLines: { Node } = {
                    b.ui.row({
                        b.ui.check(line.task == "x" or line.task == "X" or line.task == nil, "toggle", args),
                        b.ui.link(entry.exercise, entry.label),
                        b.ui.text(trim(formatEntry({ exercise = "", linked = false, sets = entry.sets, notes = "" })), true),
                    }),
                }
                if last.reps then
                    local steppers: { Node } = {}
                    if not last.bodyweight then
                        table.insert(steppers, b.ui.stepper({
                            value = last.weight or 0, step = if (last.unit or unit()) == "lb" then 5 else 2.5,
                            unit = last.unit or unit(), action = "weight", args = args,
                        }))
                    end
                    table.insert(steppers, b.ui.stepper({ value = last.reps :: number, step = 1, action = "reps", args = args }))
                    table.insert(cardLines, b.ui.row(steppers))
                end
                local buttons: { Node } = { b.ui.button(T.addSetButton, "addSet", args) }
                if #entry.sets > 1 then
                    table.insert(buttons, b.ui.button(T.removeSetButton, "removeSet", args))
                end
                table.insert(cardLines, b.ui.row(buttons))
                table.insert(rows, b.ui.card(cardLines))
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
        return b.ui.column({
            b.ui.empty(T.noWorkout, T.noWorkoutText),
            b.ui.run(T.startButton, "start"),
            b.ui.open(T.catalog, "catalog", {}, { icon = "search" }),
        })
    end
    table.insert(items, b.ui.open(T.catalog, "catalog", {}, { icon = "search" }))
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
                    set.weight, set.bodyweight, set.unit = value, false, set.unit or unit()
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
        b.timer(tonumber(args.rest) or restSeconds(nil), { label = T.rest, message = T.nextSet })
    end,
    removeSet = function(args)
        editLine(args, function(entry)
            if #entry.sets > 1 then
                table.remove(entry.sets)
            end
        end)
    end,
})

-- "17.11" (Polish), "Nov 17": a week's or a day's label on a chart's axis.
local function shortDate(date: string): string
    local day = tonumber(date:sub(9, 10)) or 0
    if b.uiLang() == "pl" then
        return `{day}.{date:sub(6, 7)}`
    end
    return `{b.monthName(tonumber(date:sub(6, 7)) or 1, "short")} {day}`
end

-- Weekly volume: this week against the average, the weeks' totals, every muscle's weeks as a
-- heat map and a table; the exercises whose muscles nobody knows.
local WEEK_CHOICES = { 4, 8, 12, 26, 52 }
local volumeState = { weeks = 12 }

local function volumeView(params: { [string]: any })
    local weeks = math.clamp(math.floor(tonumber(params.weeks) or volumeState.weeks), 2, 104)
    local r = volumeReport(weeks)
    local muscles = catalog().muscles
    local lang = b.uiLang()
    local options = {}
    for _, n in WEEK_CHOICES do
        table.insert(options, { label = counted(T.weeksCount, n), value = tostring(n) })
    end
    local items: { Node } = {
        b.ui.heading(T.volumeTitle),
        b.ui.text(T.volumeNote, true),
        b.ui.select({ options = options, value = tostring(weeks), label = T.period, action = "weeks" }),
    }
    if r.first > weeks then
        table.insert(items, b.ui.empty(T.noVolume, T.noVolumeText, "chart-column"))
        return b.ui.column(items)
    end
    local current = r.totals[weeks]
    local average = averageBefore(r.totals, r.first)
    local stats = {
        { label = T.thisWeek, value = formatMass(current, 0), tone = "accent" },
        {
            label = string.format(T.averageOf, counted(T.weeksCount, math.max(1, weeks - r.first))),
            value = if average then formatMass(average, 0) else "—",
        },
        { label = T.setsThisWeek, value = b.formatNumber(r.sets[weeks]) },
    }
    if average and average > 0 then
        local change = (current / average - 1) * 100
        table.insert(stats, {
            label = T.againstAverage,
            value = `{if change >= 0 then "+" else "−"}{b.formatNumber(math.abs(change), 0)}{W.percent}`,
            tone = if change >= 0 then "success" else "warning",
        })
    end
    table.insert(items, b.ui.stats(stats))
    local labels: { string } = {}
    local totals: { number } = {}
    for i, week in r.weeks do
        labels[i] = shortDate(week)
        totals[i] = round(shown(r.totals[i]), 0)
    end
    table.insert(items, b.ui.chart({ kind = "bar", labels = labels, series = { { name = T.volume, values = totals } } }))
    if #r.muscles > 0 then
        local rows = {}
        local tableRows: { { string } } = {}
        for _, row in r.muscles do
            local name = nameIn(muscles, row.id, lang)
            -- a muscle trained only with timed sets (the plank's abs): its sets, not its volume
            local any = 0
            for i = 1, weeks do
                any += row.volume[i]
            end
            local values = {}
            local texts = {}
            for i = 1, weeks do
                local amount = if any > 0 then row.volume[i] else row.sets[i]
                values[i] = if amount > 0 then amount else nil
                texts[i] = string.format(T.cellText, name, string.format(T.weekOf, labels[i]),
                    formatMass(row.volume[i], 0), countedNumber(T.setsCount, row.sets[i]))
            end
            local note = if any > 0 then formatMass(row.volume[weeks], 0) else countedNumber(T.setsCount, row.sets[weeks])
            table.insert(rows, { label = name, values = values, texts = texts, note = note })
            local avg = averageBefore(row.volume, r.first)
            table.insert(tableRows, {
                b.formatNumber(row.sets[weeks], 1),
                name,
                formatMass(row.volume[weeks], 0),
                if avg then formatMass(avg, 0) else "—",
            })
        end
        table.insert(items, b.ui.heatmap({ columns = labels, rows = rows, scale = "row", label = T.volumeTitle }))
        table.insert(items, b.ui.table({ T.setsColumn, T.muscleColumn, T.thisWeek, T.averageColumn }, tableRows))
    end
    if #r.unassigned > 0 then
        table.insert(items, b.ui.text(string.format(T.unassigned, table.concat(r.unassigned, ", ")), { muted = true, icon = "info" }))
        local links: { Node } = {}
        for i, name in r.unassigned do
            if i > 12 then
                break
            end
            table.insert(links, b.ui.link(name))
        end
        table.insert(items, b.ui.row(links))
    end
    return b.ui.column(items)
end

b.view("volume", volumeView, {
    weeks = function(args)
        volumeState.weeks = math.clamp(math.floor(tonumber(args.value) or 12), 2, 104)
    end,
}, { label = T.volumeView, icon = "chart-column" })

-- The body weight log: quick entry, the latest weigh-in and the trend, a chart of the range with
-- the trend line, body fat, the last weigh-ins with their menus.
local weightState = { range = "90" }

local function weightView(params: { [string]: any })
    local range = if type(params.range) == "string" and params.range ~= "" then params.range else weightState.range
    local entries, days = weightLog()
    local items: { Node } = {
        b.ui.heading(T.bodyWeight),
        b.ui.input({ label = T.weighIn, placeholder = T.weighInPlaceholder, action = "add", submit = T.save }),
    }
    if #days == 0 then
        table.insert(items, b.ui.empty(T.noWeights, T.noWeightsText, "weight"))
        return b.ui.column(items)
    end
    local s = weightSummary(days)
    local last = s.last :: WeightDay
    local stats = {
        { label = `{T.latest} · {shortDate(last.date)}`, value = formatMass(last.kg), tone = "accent" },
        { label = T.trend, value = formatMass(last.trend) },
        { label = T.change7, value = if s.change7 then signed(s.change7 :: number) else "—" },
        { label = T.change30, value = if s.change30 then signed(s.change30 :: number) else "—" },
    }
    if s.fat then
        table.insert(stats, { label = T.bodyFat, value = b.formatNumber(s.fat :: number, 1) .. W.percent })
    end
    table.insert(items, b.ui.stats(stats))
    local from = if range == "all" then "" else b.addDays(b.today(), -((tonumber(range) or 90) - 1))
    local labels: { string } = {}
    local weights: { number? } = {}
    local trends: { number? } = {}
    local fats: { number? } = {}
    local fatCount = 0
    for _, day in days do
        if day.date >= from then
            table.insert(labels, shortDate(day.date))
            table.insert(weights, round(shown(day.kg), 2))
            table.insert(trends, round(shown(day.trend), 2))
            table.insert(fats, day.fat)
            if day.fat then
                fatCount += 1
            end
        end
    end
    local options = {}
    for _, o in T.ranges do
        table.insert(options, { label = o.label, value = o.value })
    end
    table.insert(items, b.ui.select({ options = options, value = range, label = T.range, action = "range" }))
    if #labels >= 1 then
        table.insert(items, b.ui.chart({
            kind = "line",
            labels = labels,
            series = { { name = T.bodyWeight, values = weights }, { name = T.trend, values = trends } },
        }))
    end
    if fatCount >= 2 then
        table.insert(items, b.ui.chart({ kind = "line", title = T.bodyFat, labels = labels, series = { { name = T.bodyFat, values = fats } } }))
    end
    table.insert(items, b.ui.heading(T.history))
    local shown_ = 0
    for i = #entries, 1, -1 do
        local e = entries[i]
        local text = `{b.formatNumber(e.weight)} {e.unit}`
        if e.fat then
            text ..= " · " .. b.formatNumber(e.fat :: number, 1) .. W.percent
        end
        table.insert(items, b.ui.row({
            b.ui.text(e.date, true),
            b.ui.text(text),
            if e.notes ~= "" then b.ui.text(e.notes, true) else nil,
            b.ui.menu({
                { label = T.openDay, journal = e.date },
                { label = T.remove, action = "remove", args = { ref = e.ref, date = e.date }, icon = "trash-2" },
            }),
        }))
        shown_ += 1
        if shown_ >= 14 then
            break
        end
    end
    return b.ui.column(items)
end

b.view("weight", weightView, {
    add = function(args)
        local weight, weightUnit, fat, notes = parseWeighing(tostring(args.value or ""))
        if weight == nil or weight > 2000 then
            return { message = T.weightNotUnderstood, tone = "warning" }
        end
        if fat and (fat <= 0 or fat >= 100) then
            fat = nil
        end
        local line = writeWeighing(b.today(), weight, weightUnit, fat, notes)
        -- its ref, for Undo: the last weigh-in of today with this text
        local entries = weightLog()
        local undo = nil
        for i = #entries, 1, -1 do
            if entries[i].date == b.today() and entries[i].text == line then
                undo = { label = T.undo, action = "remove", args = { ref = entries[i].ref, date = entries[i].date } }
                break
            end
        end
        return { message = string.format(T.weightSaved, line), tone = "success", button = undo }
    end,
    remove = function(args)
        b.removeBlock(args.ref :: string)
        return { message = string.format(T.removed, tostring(args.date)), tone = "success" }
    end,
    range = function(args)
        weightState.range = tostring(args.value or "90")
    end,
}, { label = T.weightView, icon = "weight" })

-- The exercise picker: a name, a primary muscle, an equipment and a kind; each row opens the
-- exercise, its menu adds it to today's workout or makes its page.
local catalogState = { query = "", muscle = "", equipment = "", category = "", limit = 30 }

local function choices(table_: { [string]: Names }, any: string, lang: string): { any }
    local names: { string } = {}
    local byName: { [string]: string } = {}
    for id, n in table_ do
        local name = if lang == "pl" then n.pl else n.en
        table.insert(names, name)
        byName[name] = id
    end
    local out: { any } = { { label = any, value = "" } }
    for _, name in b.sorted(names) do
        table.insert(out, { label = name, value = byName[name] })
    end
    return out
end

local function catalogView(params: { [string]: any })
    local data = catalog()
    local lang = b.uiLang()
    local notes = b.lang()
    -- a ```view block's params set the filters it starts with
    local query = catalogState.query
    local filters: Filters = {}
    for _, key in { "muscle", "equipment", "category" } do
        local value = (catalogState :: any)[key]
        if value == "" and type(params[key]) == "string" then
            value = params[key]
        end
        if value ~= "" then
            (filters :: any)[key] = value
        end
    end
    local found = searchCatalog(query, filters)
    local items: { Node } = {
        b.ui.heading(T.catalog),
        b.ui.text(T.catalogNote, true),
        b.ui.input({
            label = T.searchLabel, placeholder = T.searchPlaceholder, value = query, keep = true,
            action = "search", submit = T.search,
        }),
        b.ui.row({
            b.ui.select({ options = choices(data.muscles, T.anyMuscle, lang), value = filters.muscle or "", label = T.muscle, action = "filter", args = { key = "muscle" } }),
            b.ui.select({ options = choices(data.equipment, T.anyEquipment, lang), value = filters.equipment or "", label = T.equipmentLabel, action = "filter", args = { key = "equipment" } }),
            b.ui.select({ options = choices(data.categories, T.anyKind, lang), value = filters.category or "", label = T.kind, action = "filter", args = { key = "category" } }),
        }),
    }
    local filtered = query ~= "" or filters.muscle ~= nil or filters.equipment ~= nil or filters.category ~= nil
    if #found == 0 then
        table.insert(items, b.ui.empty(T.nothingMatches, T.nothingMatchesText, "search"))
        table.insert(items, b.ui.button(T.clearFilters, "clear", {}))
        return b.ui.column(items)
    end
    table.insert(items, b.ui.text(counted(T.exercisesCount, #found), true))
    if filtered then
        table.insert(items, b.ui.button(T.clearFilters, "clear", {}, { icon = "x" }))
    end
    -- a card per exercise: its name (opens it) and its menu, its muscles and equipment under them
    for i = 1, math.min(#found, catalogState.limit) do
        local item = found[i]
        local what = muscleNames(item.primary, lang)
        if item.equipment then
            what ..= " · " .. nameIn(data.equipment, item.equipment :: string, lang)
        end
        table.insert(items, b.ui.card({
            b.ui.row({
                b.ui.open(exerciseName(item, notes), "exercise", { id = item.id }),
                b.ui.menu({
                    { label = T.addToToday, action = "addToToday", args = { id = item.id }, icon = "plus" },
                    { label = T.createPage, action = "createPage", args = { id = item.id }, icon = "file-text" },
                    { label = T.details, view = "exercise", params = { id = item.id } },
                }),
            }),
            b.ui.text(what, true),
        }))
    end
    if #found > catalogState.limit then
        table.insert(items, b.ui.button(T.showMore, "more", {}))
    end
    return b.ui.column(items)
end

local catalogActions = {
    search = function(args)
        catalogState.query = trim(tostring(args.value or ""))
        catalogState.limit = 30
    end,
    filter = function(args)
        local key = tostring(args.key)
        if key == "muscle" or key == "equipment" or key == "category" then
            (catalogState :: any)[key] = tostring(args.value or "")
            catalogState.limit = 30
        end
    end,
    clear = function(args)
        catalogState.query, catalogState.muscle, catalogState.equipment, catalogState.category = "", "", "", ""
        catalogState.limit = 30
    end,
    more = function(args)
        catalogState.limit += 30
    end,
    addToToday = function(args)
        catalog()
        local item = catalogById[tostring(args.id)]
        if item == nil then
            return { message = T.unknownExercise, tone = "warning" }
        end
        return { message = string.format(T.addedToToday, addToToday(item)), tone = "success" }
    end,
    createPage = function(args)
        catalog()
        local item = catalogById[tostring(args.id)]
        if item == nil then
            return { message = T.unknownExercise, tone = "warning" }
        end
        local name = exerciseName(item, b.lang())
        return { message = createExercisePage(item), button = { label = T.openPage, page = name } }
    end,
}

b.view("catalog", catalogView, catalogActions, { label = T.catalogView, icon = "search" })

-- One exercise of the catalog: its muscles, equipment, kind and level, its instructions, its
-- page (made from the catalog when missing) and its sessions in the journal.
local function exerciseView(params: { [string]: any })
    local data = catalog()
    local item = catalogById[tostring(params.id or "")]
    if item == nil then
        return b.ui.column({ b.ui.empty(T.unknownExercise, "", "search"), b.ui.open(T.catalog, "catalog", {}) })
    end
    local lang = b.uiLang()
    local name = exerciseName(item, b.lang())
    local other = if b.lang() == "pl" then item.en else item.pl
    local items: { Node } = { b.ui.open(T.catalog, "catalog", {}, { icon = "arrow-left" }), b.ui.heading(name) }
    if other ~= name then
        table.insert(items, b.ui.text(other, true))
    end
    table.insert(items, b.ui.text(`{T.primaryMuscles}: {muscleNames(item.primary, lang)}`, { icon = "dumbbell" }))
    if #item.secondary > 0 then
        table.insert(items, b.ui.text(`{T.secondaryMuscles}: {muscleNames(item.secondary, lang)}`, true))
    end
    local facts: { string } = {}
    if item.equipment then
        table.insert(facts, `{T.equipmentLabel}: {nameIn(data.equipment, item.equipment :: string, lang)}`)
    end
    table.insert(facts, `{T.kind}: {nameIn(data.categories, item.category, lang)}`)
    if item.level then
        table.insert(facts, `{T.level}: {nameIn(data.levels, item.level :: string, lang)}`)
    end
    table.insert(items, b.ui.text(table.concat(facts, " · "), true))
    table.insert(items, b.ui.row({
        b.ui.button(T.addToToday, "addToToday", { id = item.id }, { primary = true, icon = "plus" }),
        if b.read(name) ~= nil then b.ui.link(name, T.openPage) else b.ui.button(T.createPage, "createPage", { id = item.id }, { icon = "file-text" }),
    }))
    local days = daysOf(name)
    if #days > 0 then
        local record = recordOf(days)
        table.insert(items, b.ui.open(record or counted(T.sessions, #days), "progress", { exercise = name }, { icon = "chart-column" }))
    end
    table.insert(items, b.ui.divider())
    table.insert(items, b.ui.heading(T.instructions))
    local steps = b.data("instructions", item.id)
    if type(steps) == "table" and #steps > 0 then
        if T.instructionsEnglish ~= "" then
            table.insert(items, b.ui.text(T.instructionsEnglish, true))
        end
        local lines: { string } = {}
        for i, step in steps do
            table.insert(lines, `{i}. {step}`)
        end
        table.insert(items, b.ui.markdown(table.concat(lines, "\n")))
    else
        table.insert(items, b.ui.text(T.noInstructions, true))
    end
    return b.ui.column(items)
end

b.view("exercise", exerciseView, catalogActions)
