--!strict
-- Fiszki (flashcards): cards written in notes as plain Markdown, scheduled with FSRS-6, their state
-- kept next to each card in the note itself (docs/SYNTAX.md §15 "Flashcards") — written with the
-- extensions' public API only (docs/EXTENSIONS.md), like the gym and the habits.
--
--   Stolica Peru::Lima <!--SR:!fsrs,2026-11-23T08:00:00.000Z,3,3.17,5.28,2,2,0,0,2026-11-20T08:00:00.000Z-->
--   Jaka jest stolica Polski?
--   ?
--   Warszawa
--   Stolicą Peru jest ==Lima==.                  (in a note tagged #flashcards or #fiszki)
--   - Co znaczy „brulion”? #card                (Logseq: the block, its children the answer)
--
-- Cards: the Obsidian Spaced Repetition plugin's (single-line `Q::A`, `Q:::A` both ways, multi-line
-- `?` / `??`, clozes `==answer==`, `==1;;answer;;hint==`, Anki's `{{c1::answer::hint}}`) in notes with
-- a flashcard tag anywhere, and Logseq's (`#card` blocks, `{{cloze answer \\ cue}}`). State: the
-- plugin's scheduling comment with its FSRS segments (`<!--SR:!fsrs,…-->`; its SM-2 segments are
-- read and converted), Logseq's `card-*` properties with `card-fsrs::`. Every review is also a line
-- of a review log page per month ("Fiszki/Powtórki 2026-11"), read back for the statistics and the
-- new-card limit.
--
-- Notes: the plugin's whole-note reviews — a note with a review tag (#review) is read whole and graded
-- Hard, Good or Easy; its state is `sr-due`, `sr-interval` and `sr-ease` in its frontmatter (the
-- plugin's SM-2 variant with load balancing, ported).
--
-- Views: decks (params group "tag" | "page" | "folder"), review (params deck, ahead, more), stats
-- (params deck), notes (the review of notes). Actions: reveal, grade, undo, reset; notes: grade, skip,
-- unskip, undo. Commands (palette = false: the app, `brulion-cli card …` and tests run them with
-- ctx.args): count, list, show, grade, stats, schedule (FSRS alone), notes, review-note,
-- schedule-note (the notes' scheduling alone).
-- Settings come with params / ctx.args: tags ("flashcards, fiszki"), newPerDay (20), retention
-- (0.9), maxInterval (36500) and the plugin's card settings the notebook keeps in
-- `.brulion/config.json` (the host passes them): singleLineSeparator ("::"),
-- singleLineReversedSeparator (":::"), multilineSeparator ("?"), multilineReversedSeparator ("??"),
-- multilineEndMarker (""), clozeHighlights (true), clozeBold (false), clozeCurly (false),
-- foldersAsDecks (false), reviewTags ("review"; the notes reviewed as a whole).
--
-- Time is local wall-clock time counted in minutes since 1970-01-01 00:00 (a day is 1440 minutes,
-- no time zones, no DST); only the formats other apps read are UTC (b.toUtc / b.fromUtc).

type Node = { type: string, [string]: any }
type Memory = {
    state: number, -- 0 new, 1 learning, 2 review, 3 relearning (ts-fsrs's numbers, the plugin's field)
    step: number, -- the (re)learning step
    s: number, -- stability (days), 0 while new
    d: number, -- difficulty 1 … 10, 0 while new
    due: number, -- minutes; 0 while new
    last: number?, -- the last review, minutes
    reps: number,
    lapses: number,
    ivl: number, -- the days scheduled (0 while learning)
}
type Settings = {
    tags: { string },
    newPerDay: number,
    retention: number,
    maxInterval: number,
    learning: { number }, -- minutes
    relearning: { number },
    fuzz: boolean,
    typeAnswer: boolean, -- a field for the answer before it is shown (a device setting)
    -- The plugin's card settings (docs/SYNTAX.md §15 "Settings of the cards"):
    inline: { { sep: string, kind: string } }, -- the single-line separators, the longest first
    multi: string, -- "" = off
    multiRev: string,
    endMarker: string, -- "" = a blank line ends a multi-line card
    clozeHighlights: boolean,
    clozeBold: boolean,
    clozeCurly: boolean,
    foldersAsDecks: boolean,
    parseKey: string, -- what a note's cards depend on: cached cards are valid for the same key
    -- Whole-note reviews (the plugin's note settings, docs/SYNTAX.md §15 "Notes to review"):
    reviewTags: { string },
    baseEase: number, -- 250
    easyBonus: number, -- 1.3
    lapsesIntervalChange: number, -- 0.5: a Hard's factor
    maxNoteInterval: number, -- 36525 days
    loadBalance: boolean,
}
type Card = {
    id: string,
    baseId: string, -- the id before two copies of one card were told apart
    key: string, -- what the id is the hash of
    kind: string, -- "basic" | "reversed" | "multiline" | "multireversed" | "cloze" | "logseq"
    sibling: number, -- 1-based: the segment of the scheduling comment
    siblings: number,
    question: string, -- Markdown
    answer: string,
    context: string, -- "Geografia › Europa"
    page: string,
    path: string,
    line: number, -- the card's first line (1-based)
    lastLine: number,
    decks: { string }, -- tag paths ("flashcards/geografia"), or the folder's (foldersAsDecks)
    folderDeck: boolean,
    folder: string,
    memory: Memory,
    -- where the state is written: a comment line, or a Logseq block
    stateRef: string,
    stateText: string,
    stateLine: number,
    stateOwn: boolean, -- the comment is on a line of its own
    segments: { string }, -- the comment's segments as written ("" none)
    logseq: boolean,
    blockRef: string?,
    props: { [string]: string }?,
}

-- ---- words ------------------------------------------------------------------------------------

-- Written into notes: the notebook's language (b.lang). Both languages are always read.
local WRITTEN = {
    pl = {
        logFolder = "Fiszki",
        logPage = "Powtórki %s",
        logTitle = "Powtórki fiszek: %s. Każdy wiersz to jedna ocena — Brulion czyta je do statystyk.",
        grades = { "znowu", "trudne", "dobrze", "łatwe" },
        states = { [0] = "nowa", [1] = "nauka", [2] = "powtórka", [3] = "ponowna" },
    },
    en = {
        logFolder = "Flashcards",
        logPage = "Reviews %s",
        logTitle = "Flashcard reviews of %s. Each line is one grade — Brulion reads them for its statistics.",
        grades = { "again", "hard", "good", "easy" },
        states = { [0] = "new", [1] = "learning", [2] = "review", [3] = "relearning" },
    },
}
local GRADE_WORDS: { [string]: number } = {
    again = 1, hard = 2, good = 3, easy = 4, znowu = 1, trudne = 2, dobrze = 3, ["łatwe"] = 4, latwe = 4,
}
local STATE_WORDS: { [string]: number } = {
    new = 0, learning = 1, review = 2, relearning = 3, nowa = 0, nauka = 1, ["powtórka"] = 2, powtorka = 2,
    ponowna = 3,
}

-- The interface's words: the app's language (b.uiLang).
local INTERFACE = {
    pl = {
        title = "Fiszki",
        allCards = "Wszystkie fiszki",
        due = "Do powtórki",
        new = "Nowe",
        learning = "W nauce",
        reviewedToday = "Dziś ocenione",
        total = "Wszystkie",
        young = "Młode",
        mature = "Dojrzałe",
        retention = "Zapamiętane (30 dni)",
        studyAll = "Ucz się wszystkich",
        study = "Ucz się",
        studyAhead = "Ucz się z wyprzedzeniem",
        moreNew = "Jeszcze %d nowych",
        statistics = "Statystyki",
        openPage = "Otwórz stronę",
        byTag = "Talie według tagów",
        byPage = "Talie według stron",
        byFolder = "Talie według folderów",
        root = "(główny folder)",
        deckCounts = "%d do powtórki · %d nowych · %d wszystkich",
        noCards = "Nie ma jeszcze fiszek",
        noCardsText = "Oznacz notatkę tagiem #fiszki (albo #flashcards) i pisz w niej „Pytanie::Odpowiedź”, pytanie, wiersz „?” i odpowiedź, albo luki ==tak==. Bloki z #card (Logseq) też są fiszkami.",
        noneInDeck = "W tej talii nie ma fiszek",
        doneTitle = "Na dziś wszystko",
        doneText = "Ocenione: %d. Następna powtórka: %s.",
        doneNothing = "Następna powtórka: %s.",
        noNext = "brak zaplanowanych",
        showAnswer = "Pokaż odpowiedź",
        typeHint = "Wpisz odpowiedź",
        check = "Sprawdź",
        typed = "Twoja odpowiedź: %s",
        grades = { "Znowu", "Trudne", "Dobrze", "Łatwe" },
        undo = "Cofnij",
        undone = "Cofnięto ocenę",
        nothingToUndo = "Nie ma czego cofnąć",
        edit = "Edytuj",
        more = "Więcej",
        reset = "Zacznij od nowa (zapomnij)",
        resetDone = "Fiszka zaczyna od nowa",
        backToDecks = "Talie",
        progress = "%d z %d",
        session = "%d ocenione · zostało: %d do powtórki, %d nowych, %d w nauce",
        stateNames = { [0] = "nowa", [1] = "w nauce", [2] = "powtórka", [3] = "ponowna nauka" },
        changed = "Wiersz zmienił się w międzyczasie — fiszka pokazana od nowa",
        notSaved = "Nie udało się zapisać oceny: %s",
        reviewsPerDay = "Oceny dziennie (30 dni)",
        correct = "Pamiętane",
        forgotten = "Zapomniane",
        forecast = "Do powtórki w kolejnych dniach",
        cards = "Fiszki",
        stateCounts = "Stan fiszek",
        today = "dziś",
        tomorrow = "jutro",
        inDays = "za %s",
        units = {
            minutes = { "%d min", "%d min", "%d min" },
            hours = { "%d godz.", "%d godz.", "%d godz." },
            days = { "%d dzień", "%d dni", "%d dni" },
            months = { "%s mies.", "%s mies.", "%s mies." },
            years = { "%s roku", "%s lata", "%s lat" },
        },
        noReviews = "Nie ma jeszcze ocen",
        noReviewsText = "Statystyki pojawią się po pierwszych powtórkach.",
        deckOf = "Talia: %s",
        badGrade = "Ocena to 1–4 albo again, hard, good, easy (znowu, trudne, dobrze, łatwe)",
        noSuchCard = "Nie ma fiszki %s",
        notDue = "dziś nie do powtórki",
        notesTitle = "Notatki do powtórki",
        noNotes = "Nie ma notatek do powtórki",
        noNotesText = "Oznacz notatkę tagiem #%s: pojawi się tu cała, a po przeczytaniu ocenisz ją — trudne, dobrze albo łatwe. Stan powtórek trafia do nagłówka notatki (sr-due, sr-interval, sr-ease), jak w wtyczce Spaced Repetition.",
        notesDoneTitle = "Notatki na dziś przejrzane",
        noteSession = "%d ocenione · zostało: %d do powtórki, %d nowych",
        skip = "Pomiń",
        openNote = "Otwórz notatkę",
        noteNew = "nowa notatka",
        noteChanged = "Nagłówek notatki zmienił się w międzyczasie — notatka pokazana od nowa",
        noteCut = "Dalsza część notatki jest w edytorze.",
        badNoteGrade = "Ocena notatki to 2–4 albo hard, good, easy (trudne, dobrze, łatwe)",
        noSuchNote = "Nie ma notatki %s",
        notReviewNote = "Notatka %s nie ma tagu do powtórki (#%s)",
        showSkipped = "Pokaż pominięte",
    },
    en = {
        title = "Flashcards",
        allCards = "All cards",
        due = "Due",
        new = "New",
        learning = "Learning",
        reviewedToday = "Reviewed today",
        total = "All",
        young = "Young",
        mature = "Mature",
        retention = "Retention (30 days)",
        studyAll = "Study all",
        study = "Study",
        studyAhead = "Study ahead",
        moreNew = "%d more new cards",
        statistics = "Statistics",
        openPage = "Open the page",
        byTag = "Decks by tag",
        byPage = "Decks by page",
        byFolder = "Decks by folder",
        root = "(top folder)",
        deckCounts = "%d due · %d new · %d in all",
        noCards = "No flashcards yet",
        noCardsText = "Tag a note #flashcards and write “Question::Answer” in it, a question, a line “?” and the answer, or clozes ==like this==. Blocks tagged #card (Logseq) are cards too.",
        noneInDeck = "This deck has no cards",
        doneTitle = "All done for today",
        doneText = "Reviewed: %d. Next review: %s.",
        doneNothing = "Next review: %s.",
        noNext = "none scheduled",
        showAnswer = "Show answer",
        typeHint = "Type the answer",
        check = "Check",
        typed = "Your answer: %s",
        grades = { "Again", "Hard", "Good", "Easy" },
        undo = "Undo",
        undone = "Grade undone",
        nothingToUndo = "Nothing to undo",
        edit = "Edit",
        more = "More",
        reset = "Start over (forget)",
        resetDone = "The card starts over",
        backToDecks = "Decks",
        progress = "%d of %d",
        session = "%d reviewed · left: %d due, %d new, %d learning",
        stateNames = { [0] = "new", [1] = "learning", [2] = "review", [3] = "relearning" },
        changed = "The line changed in the meantime — the card is shown again",
        notSaved = "The grade could not be saved: %s",
        reviewsPerDay = "Reviews per day (30 days)",
        correct = "Recalled",
        forgotten = "Forgotten",
        forecast = "Due in the coming days",
        cards = "Cards",
        stateCounts = "Cards by state",
        today = "today",
        tomorrow = "tomorrow",
        inDays = "in %s",
        units = {
            minutes = { "%d min", "%d min" },
            hours = { "%d h", "%d h" },
            days = { "%d day", "%d days" },
            months = { "%s mo", "%s mo" },
            years = { "%s year", "%s years" },
        },
        noReviews = "No reviews yet",
        noReviewsText = "The statistics show up after the first reviews.",
        deckOf = "Deck: %s",
        badGrade = "A grade is 1–4 or again, hard, good, easy (znowu, trudne, dobrze, łatwe)",
        noSuchCard = "No card %s",
        notDue = "not due today",
        notesTitle = "Notes to review",
        noNotes = "No notes to review",
        noNotesText = "Tag a note #%s: it shows up here whole, and after reading it you grade it — hard, good or easy. The state of the reviews goes into the note's header (sr-due, sr-interval, sr-ease), as in the Spaced Repetition plugin.",
        notesDoneTitle = "All notes reviewed for today",
        noteSession = "%d reviewed · left: %d due, %d new",
        skip = "Skip",
        openNote = "Open the note",
        noteNew = "new note",
        noteChanged = "The note's header changed in the meantime — the note is shown again",
        noteCut = "The rest of the note is in the editor.",
        badNoteGrade = "A note's grade is 2–4 or hard, good, easy (trudne, dobrze, łatwe)",
        noSuchNote = "No note %s",
        notReviewNote = "The note %s does not have a review tag (#%s)",
        showSkipped = "Show the skipped notes",
    },
}

local function written()
    return if b.lang() == "en" then WRITTEN.en else WRITTEN.pl
end

local function ui()
    return if b.uiLang() == "pl" then INTERFACE.pl else INTERFACE.en
end

-- Polish plural forms: 1, 2–4 (not 12–14), the rest; English: 1, the rest.
local function pluralForm(n: number, forms: { string }): string
    if #forms == 2 then
        return if n == 1 then forms[1] else forms[2]
    elseif n == 1 then
        return forms[1]
    end
    local mod10, mod100 = n % 10, n % 100
    return if mod10 >= 2 and mod10 <= 4 and (mod100 < 12 or mod100 > 14) then forms[2] else forms[3]
end

local function trim(s: string): string
    return (s:match("^%s*(.-)%s*$")) :: string
end

-- ---- time: local wall-clock minutes -----------------------------------------------------------

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

-- "2026-11-20" (a day number) or "2026-11-20 18:30" / "2026-11-20T18:30" (minutes); nil otherwise.
local function dayOf(date: any): number?
    if type(date) ~= "string" then
        return nil
    end
    local ys, ms, ds = string.match(date, "^(%d%d%d%d)%-(%d%d)%-(%d%d)")
    if ys == nil then
        return nil
    end
    local y, m, d = tonumber(ys) :: number, tonumber(ms) :: number, tonumber(ds) :: number
    if y < 1 or m < 1 or m > 12 or d < 1 or d > daysInMonth(y, m) then
        return nil
    end
    return fromCivil(y, m, d)
end

local function minutesOf(text: any): number?
    local day = dayOf(text)
    if day == nil then
        return nil
    end
    local hs, mins = string.match(text, "^%d%d%d%d%-%d%d%-%d%d[ T](%d%d?):(%d%d)")
    local h, m = tonumber(hs) or 0, tonumber(mins) or 0
    if h > 23 or m > 59 then
        return nil
    end
    return day * 1440 + h * 60 + m
end

local function dateOf(day: number): string
    local y, m, d = civil(day)
    return string.format("%04d-%02d-%02d", y, m, d)
end

local function stampOf(minutes: number): string
    local day = minutes // 1440
    local rest = minutes - day * 1440
    return string.format("%s %02d:%02d", dateOf(day), rest // 60, rest % 60)
end

-- ---- FSRS-6 -----------------------------------------------------------------------------------
-- A port of py-fsrs 6.3.3 (open-spaced-repetition, MIT): its states, learning and relearning
-- steps, formulas and rounding (half to even), with the published default parameters. One change:
-- the fuzz is drawn from a generator seeded by the card and its review count (FNV-1a and
-- Mulberry32, as ts-fsrs 6 draws it) instead of Python's global random() — the same interval on
-- every device and in every test.

local W = {
    0.212, 1.2931, 2.3065, 8.2956, 6.4133, 0.8334, 3.0194, 0.001, 1.8722, 0.1666, 0.796,
    1.4835, 0.0614, 0.2629, 1.6483, 0.6014, 1.8729, 0.5425, 0.0912, 0.0658, 0.1542,
} -- (the papers' w[i] is W[i + 1])
local DECAY = -W[21]
local FACTOR = 0.9 ^ (1 / DECAY) - 1
local S_MIN = 0.001
local FUZZ_RANGES = { { 2.5, 7.0, 0.15 }, { 7.0, 20.0, 0.1 }, { 20.0, math.huge, 0.05 } }

local function roundEven(x: number): number
    local f = math.floor(x)
    local r = x - f
    if r > 0.5 then
        return f + 1
    elseif r < 0.5 then
        return f
    end
    return if f % 2 == 0 then f else f + 1
end

local function clampD(d: number): number
    return math.clamp(d, 1, 10)
end

local function clampS(s: number): number
    return math.max(s, S_MIN)
end

local function initialStability(g: number): number
    return clampS(W[g])
end

local function initialDifficulty(g: number, clamp: boolean): number
    local d = W[5] - math.exp(W[6] * (g - 1)) + 1
    return if clamp then clampD(d) else d
end

-- The probability of recall `elapsed` days after a review with stability `s`.
local function retrievability(elapsed: number, s: number): number
    return (1 + FACTOR * elapsed / s) ^ DECAY
end

local function nextInterval(st: Settings, s: number): number
    local i = roundEven(s / FACTOR * (st.retention ^ (1 / DECAY) - 1))
    return math.min(math.max(i, 1), st.maxInterval)
end

local function shortTermStability(s: number, g: number): number
    local inc = math.exp(W[18] * (g - 3 + W[19])) * s ^ -W[20]
    if g >= 2 then
        inc = math.max(inc, 1)
    end
    return clampS(s * inc)
end

local function nextDifficulty(d: number, g: number): number
    local delta = -(W[7] * (g - 3))
    local damped = d + (10 - d) * delta / 9
    return clampD(W[8] * initialDifficulty(4, false) + (1 - W[8]) * damped)
end

local function forgetStability(d: number, s: number, r: number): number
    local long = W[12] * d ^ -W[13] * ((s + 1) ^ W[14] - 1) * math.exp((1 - r) * W[15])
    local short = s / math.exp(W[18] * W[19])
    return math.min(long, short)
end

local function recallStability(d: number, s: number, r: number, g: number): number
    local hard = if g == 2 then W[16] else 1
    local easy = if g == 4 then W[17] else 1
    return s * (1 + math.exp(W[9]) * (11 - d) * s ^ -W[10] * (math.exp((1 - r) * W[11]) - 1) * hard * easy)
end

local function nextStability(d: number, s: number, r: number, g: number): number
    return clampS(if g == 1 then forgetStability(d, s, r) else recallStability(d, s, r, g))
end

-- 32-bit arithmetic on doubles (Luau numbers): exact below 2^53.
local U32 = 4294967296
local function imul(a: number, c: number): number
    local al, ah = a % 65536, (a // 65536) % 65536
    local cl, ch = c % 65536, (c // 65536) % 65536
    return (al * cl + ((ah * cl + al * ch) % 65536) * 65536) % U32
end

-- FNV-1a over the UTF-8 bytes of `s`, as ts-fsrs's fnv1a32. (Its prime is 2^24 + 403: a shift and
-- a product below 2^53, exact in a double, instead of a general 32-bit multiply.)
local function fnv1a32(s: string): number
    local h = 2166136261
    for i = 1, #s do
        h = bit32.bxor(h, string.byte(s, i))
        h = (bit32.lshift(h, 24) + h * 403) % U32
    end
    return h
end

-- The first number of Mulberry32 seeded with `seed` (a 32-bit integer), in [0, 1).
local function mulberry32(seed: number): number
    local t = (seed + 0x6D2B79F5) % U32
    t = imul(bit32.bxor(t, bit32.rshift(t, 15)), bit32.bor(t, 1))
    t = bit32.bxor(t, (t + imul(bit32.bxor(t, bit32.rshift(t, 7)), bit32.bor(t, 61))) % U32)
    return bit32.bxor(t, bit32.rshift(t, 14)) / U32
end

local function fuzzRange(st: Settings, days: number): (number, number)
    local delta = 1.0
    for _, r in FUZZ_RANGES do
        delta += r[3] * math.max(math.min(days, r[2]) - r[1], 0)
    end
    local lo = math.max(2, roundEven(days - delta))
    local hi = math.min(roundEven(days + delta), st.maxInterval)
    return math.min(lo, hi), hi
end

local function fuzzed(st: Settings, days: number, draw: number): number
    if days < 2.5 then
        return days
    end
    local lo, hi = fuzzRange(st, days)
    return math.min(math.floor(draw * (hi - lo + 1) + lo), st.maxInterval)
end

local function newMemory(): Memory
    return { state = 0, step = 0, s = 0, d = 0, due = 0, reps = 0, lapses = 0, ivl = 0 }
end

local function copyMemory(m: Memory): Memory
    return {
        state = m.state, step = m.step, s = m.s, d = m.d, due = m.due, last = m.last, reps = m.reps, lapses = m.lapses,
        ivl = m.ivl,
    }
end

-- The card after a review with grade `g` (1 Again, 2 Hard, 3 Good, 4 Easy) at minute `now`;
-- `seed` names the card for the fuzz. py-fsrs's review_card, with "new" as its learning step 0.
local function review(st: Settings, m0: Memory, g: number, now: number, seed: string): Memory
    local m = copyMemory(m0)
    local state = if m.state == 0 then 1 else m.state
    local days = if m.last then (now - (m.last :: number)) // 1440 else nil
    local function r(): number
        return retrievability(math.max(0, days or 0), m.s)
    end
    local nextMinutes: number -- the interval as minutes (a learning step)
    local nextDays: number? -- or as days
    local fresh = m.state == 0 or m.s <= 0 or m.d <= 0
    if state == 1 or state == 3 then
        local steps = if state == 1 then st.learning else st.relearning
        if fresh then
            m.s = initialStability(g)
            m.d = initialDifficulty(g, true)
        elseif days ~= nil and days < 1 then
            m.s = shortTermStability(m.s, g)
            m.d = nextDifficulty(m.d, g)
        else
            m.s = nextStability(m.d, m.s, r(), g)
            m.d = nextDifficulty(m.d, g)
        end
        if #steps == 0 or (m.step >= #steps and g >= 2) then
            state = 2
            m.step = 0
            nextDays = nextInterval(st, m.s)
        elseif g == 1 then
            m.step = 0
            nextMinutes = steps[1]
        elseif g == 2 then
            if m.step == 0 and #steps == 1 then
                nextMinutes = steps[1] * 1.5
            elseif m.step == 0 then
                nextMinutes = (steps[1] + steps[2]) / 2
            else
                nextMinutes = steps[m.step + 1]
            end
        elseif g == 3 then
            if m.step + 1 == #steps then
                state = 2
                m.step = 0
                nextDays = nextInterval(st, m.s)
            else
                m.step += 1
                nextMinutes = steps[m.step + 1]
            end
        else
            state = 2
            m.step = 0
            nextDays = nextInterval(st, m.s)
        end
    else -- review
        if days ~= nil and days < 1 then
            m.s = shortTermStability(m.s, g)
        else
            m.s = nextStability(m.d, m.s, r(), g)
        end
        m.d = nextDifficulty(m.d, g)
        if g == 1 then
            m.lapses += 1
            if #st.relearning == 0 then
                nextDays = nextInterval(st, m.s)
            else
                state = 3
                m.step = 0
                nextMinutes = st.relearning[1]
            end
        else
            nextDays = nextInterval(st, m.s)
        end
    end
    m.reps += 1
    m.state = state
    if nextDays ~= nil then
        if st.fuzz and state == 2 then
            nextDays = fuzzed(st, nextDays :: number, mulberry32(fnv1a32(seed .. tostring(m.reps))))
        end
        m.ivl = nextDays :: number
        m.due = now + (nextDays :: number) * 1440
    else
        m.ivl = 0
        m.due = now + math.floor((nextMinutes :: number) + 0.5)
    end
    m.last = now
    return m
end

-- fsrs-rs's memory_state_from_sm2: the stability and difficulty an SM-2 card (the plugin's
-- `!2026-11-23,3,250`, Logseq's card-ease-factor / card-last-interval) stands for — the stability
-- that gives a retention of 90 % at its interval, the difficulty that makes a Good at that
-- interval multiply the stability by its ease.
local function fromSm2(ease: number, interval: number): (number, number)
    local r = 0.9
    local s = math.max(interval, S_MIN) * FACTOR / (r ^ (1 / DECAY) - 1)
    local d = 11 - (ease - 1) / (math.exp(W[9]) * s ^ -W[10] * (math.exp((1 - r) * W[11]) - 1))
    return s, clampD(d)
end

-- ---- the state as written ---------------------------------------------------------------------

-- A sibling never reviewed, as the plugin writes it (it keeps the sibling's place in the comment).
local NEW_SEGMENT = "2000-01-01,1,250"
local LOGSEQ_KEYS = {
    "card-last-interval", "card-repeats", "card-ease-factor", "card-next-schedule", "card-last-reviewed",
    "card-last-score", "card-fsrs",
}

local function finite(x: number?): boolean
    return x ~= nil and x == x and math.abs(x :: number) ~= math.huge
end

-- "2026-11-23T08:00:00.000Z" → local minutes; nil for "-", "nil", "" or anything else.
local function fromIso(text: string?): number?
    if text == nil or text == "" or text == "-" or text == "nil" then
        return nil
    end
    local ok, value = pcall(b.fromUtc, text)
    if not ok or type(value) ~= "string" then
        return nil
    end
    return minutesOf(value)
end

local function toIso(minutes: number): string
    return b.toUtc(stampOf(minutes))
end

-- A number as the state writes it: at most four decimals, no trailing zeros ("3.1704", "2").
local function numberText(x: number): string
    local s = string.format("%.4f", x)
    s = s:gsub("0+$", "")
    s = s:gsub("%.$", "")
    return if s == "-0" then "0" else s
end

local function split(s: string, sep: string): { string }
    local out = {}
    local start = 1
    while true do
        local i = string.find(s, sep, start, true)
        if not i then
            table.insert(out, s:sub(start))
            return out
        end
        table.insert(out, s:sub(start, i - 1))
        start = i + #sep
    end
end

-- The plugin's FSRS fields (after "fsrs,"): due, interval, stability, difficulty, state, reps,
-- lapses, learning steps, last review — or nil when the due is none.
local function parseFsrs(f: { string }): Memory?
    local due = fromIso(trim(f[1] or ""))
    if due == nil then
        return nil
    end
    local function num(i: number): number
        local x = tonumber(trim(f[i] or ""))
        return if finite(x) then x :: number else 0
    end
    local m = newMemory()
    m.state = math.clamp(math.floor(num(5)), 0, 3)
    if m.state == 0 then
        return newMemory() -- a card that starts over (the plugin's reset)
    end
    m.due = due
    m.ivl = math.max(0, math.floor(num(2)))
    m.s = num(3)
    m.d = num(4)
    m.reps = math.max(0, math.floor(num(6)))
    m.lapses = math.max(0, math.floor(num(7)))
    m.step = math.max(0, math.floor(num(8)))
    m.last = fromIso(trim(f[9] or ""))
    if m.s <= 0 then
        m.s = math.max(m.ivl, 1) -- a stability that is none: as the interval stands for it
    end
    if m.d <= 0 then
        m.d = 5
    end
    m.s = clampS(m.s)
    m.d = clampD(m.d)
    return m
end

-- The plugin's SM-2 segment "2026-11-23,3,250" (due, interval in days, ease × 100): a review
-- card whose memory fromSm2 gives, reviewed `interval` days before its due; nil for a new one.
local function parseSm2(segment: string): Memory?
    local f = split(segment, ",")
    local due = dayOf(trim(f[1] or ""))
    if due == nil or trim(f[1] or "") == "2000-01-01" then
        return nil
    end
    local ivl = math.max(0, math.floor(tonumber(trim(f[2] or "")) or 0))
    local ease = (tonumber(trim(f[3] or "")) or 250) / 100
    if not finite(ease) or ease <= 0 then
        ease = 2.5
    end
    local m = newMemory()
    m.state = 2
    m.s, m.d = fromSm2(ease, ivl)
    m.ivl = ivl
    m.due = (due :: number) * 1440
    m.last = m.due - ivl * 1440
    m.reps = math.max(1, roundEven(math.log(ivl + 1, 2)))
    return m
end

local function parseSegment(segment: string): Memory?
    if segment:sub(1, 5) == "fsrs," then
        return parseFsrs(split(segment:sub(6), ","))
    end
    return parseSm2(segment)
end

local function formatFsrs(m: Memory): string
    return table.concat({
        toIso(m.due), tostring(m.ivl), numberText(m.s), numberText(m.d), tostring(m.state), tostring(m.reps),
        tostring(m.lapses), tostring(m.step), if m.last then toIso(m.last :: number) else "-",
    }, ",")
end

-- The scheduling comment of a line: its text ("<!--SR:!…-->"), where it starts and its segments.
local function commentOf(line: string): (string?, number?, { string })
    local start = string.find(line, "<!--SR:", 1, true)
    if not start then
        return nil, nil, {}
    end
    local stop = string.find(line, "-->", start + 7, true)
    if not stop then
        return nil, nil, {}
    end
    local inner = line:sub(start + 7, stop - 1)
    local segments = {}
    for _, part in split(inner, "!") do
        local t = trim(part)
        if t ~= "" then
            table.insert(segments, t)
        end
    end
    return line:sub(start, stop + 2), start, segments
end

-- The line without its scheduling comment (and the one space before it).
local function withoutComment(line: string): string
    local comment, start = commentOf(line)
    if not comment then
        return line
    end
    local before = line:sub(1, (start :: number) - 1)
    local after = line:sub((start :: number) + #(comment :: string))
    if before:sub(-1) == " " then
        before = before:sub(1, -2)
    end
    return before .. after
end

-- Logseq's card: its properties written by Logseq or by Brulion (card-fsrs:: the plugin's FSRS
-- fields). The later of the two reviews counts: a card reviewed in Logseq afterwards goes on from
-- Logseq's interval and ease.
local function logseqMemory(props: { [string]: string }): Memory
    local fsrs = props["card-fsrs"]
    local m: Memory? = if fsrs then parseFsrs(split(fsrs, ",")) else nil
    local logseqLast = fromIso(props["card-last-reviewed"])
    if m ~= nil and (logseqLast == nil or (m.last ~= nil and (m.last :: number) >= (logseqLast :: number))) then
        return m :: Memory
    end
    local repeats = tonumber(props["card-repeats"] or "") or 0
    if repeats < 1 or logseqLast == nil then
        return if m ~= nil then m :: Memory else newMemory()
    end
    local ivl = math.max(0, tonumber(props["card-last-interval"] or "") or 0)
    local ease = tonumber(props["card-ease-factor"] or "") or 2.5
    if not finite(ease) or ease <= 0 then
        ease = 2.5
    end
    local mem = newMemory()
    mem.state = 2
    mem.s, mem.d = fromSm2(ease, ivl)
    mem.ivl = math.floor(ivl + 0.5)
    mem.last = logseqLast
    mem.due = fromIso(props["card-next-schedule"]) or ((logseqLast :: number) + mem.ivl * 1440)
    mem.reps = math.floor(repeats)
    return mem
end

-- Logseq's six properties as Logseq writes them after a review of grade `g`, and card-fsrs.
local LOGSEQ_SCORES = { 1, 3, 5, 5 }
local function logseqProperties(m: Memory, g: number, now: number): { { string } }
    -- (Logseq's ease from the difficulty, as the plugin maps one to the other: 1 → 3.0, 10 → 1.3.)
    local ease = if m.d > 0 then 1.3 + (10 - m.d) / 9 * 1.7 else 2.5
    return {
        { "card-last-interval", tostring(m.ivl) },
        { "card-repeats", tostring(m.reps) },
        { "card-ease-factor", numberText(math.floor(ease * 100 + 0.5) / 100) },
        { "card-next-schedule", toIso(m.due) },
        { "card-last-reviewed", toIso(now) },
        { "card-last-score", tostring(LOGSEQ_SCORES[g]) },
        { "card-fsrs", formatFsrs(m) },
    }
end

-- ---- cards in notes ---------------------------------------------------------------------------

-- A line of a note: its text as written and its ref (b.lines).
type Line = { text: string, ref: string }
-- A card as the parser finds it: its kind, its lines (1-based, inclusive).
type Found = { kind: string, first: number, last: number, sep: string? }

-- A `key:: value` line that is no list item: a property as Brulion and Logseq read it (a page's
-- or a block's), never a card — `Stolica:: Warszawa` is one, `Stolica :: Warszawa` and
-- `- Stolica:: Warszawa` are cards.
local function isProperty(line: string): boolean
    if not string.find(line, "::", 1, true) then
        return false
    end
    local key = string.match(line, "^%s*([^%s:]+)::%s") or string.match(line, "^%s*([^%s:]+)::$")
    if key == nil then
        return false
    end
    return not (key :: string):find("^[%-%*%+]$") and not (key :: string):find("^%d+[%.%)]$")
end

-- Is position `at` of `line` inside inline code (an odd number of backticks before it and after it,
-- as the plugin counts)?
local function inCode(line: string, at: number): boolean
    if not string.find(line, "`", 1, true) then
        return false
    end
    local before = select(2, line:sub(1, at - 1):gsub("`", ""))
    local after = select(2, line:sub(at):gsub("`", ""))
    return before % 2 == 1 and after % 2 == 1
end

-- The separator of a single-line card, as the plugin looks: the separators longest first (":::"
-- before "::"), the first whose first occurrence is not in inline code (the plugin looks at the
-- first only); nil for no card. `kind` is "basic" or "reversed".
local function inlineSeparator(st: Settings, line: string): { sep: string, kind: string }?
    if isProperty(line) or line:sub(1, 7) == "<!--SR:" then
        return nil
    end
    -- (The `::` of Anki's clozes, `{{c1::answer::hint}}`, is no separator.)
    if string.find(line, "{{", 1, true) then
        line = line:gsub("{{[cC]?%d+::.-}}", function(m: string)
            return string.rep(" ", #m)
        end)
    end
    for _, s in st.inline do
        local at = string.find(line, s.sep, 1, true)
        if at and not inCode(line, at) then
            return s
        end
    end
    return nil
end

-- A cloze deletion of a line: where it is (bytes), what it hides, its number (0: unnumbered) and
-- its hint.
type Deletion = { from: number, to: number, answer: string, number: number, hint: string?, pattern: number }

-- What a deletion's inside holds: its answer, its number (0: none) and its hint. Pattern 2 is
-- Anki's `c1::answer::hint`, the others the plugin's `1;;answer;;hint`.
local function deletionInside(inner: string, pattern: number): (string, number, string?)
    local sep = if pattern == 2 then "::" else ";;"
    local number, rest
    if pattern == 2 then
        number, rest = string.match(inner, "^[cC]?(%d+)::(.+)$")
    else
        number, rest = string.match(inner, "^(%d+);;(.+)$")
    end
    local answer, hint = rest or inner, nil
    local h = string.find(answer, sep, 1, true)
    if h then
        answer, hint = answer:sub(1, h - 1), answer:sub(h + #sep)
    end
    return answer, tonumber(number) or 0, hint
end

-- The deletions of a line, by the plugin's cloze patterns: `==answer==`, `==1;;answer;;hint==`
-- (pattern 1; "Convert ==highlights== to clozes", on), `**answer**` (pattern 3; "Convert **bold
-- text** to clozes", off), `{{answer}}` (pattern 4; "Convert {{curly brackets}} to clozes", off)
-- and Anki's `{{c1::answer::hint}}` / `{{1::answer::hint}}` (pattern 2; the plugin reads them with
-- a pattern of its own, always on here). Not in inline code (the plugin would take them there
-- too). Linear in the line.
local function deletionsOf(st: Settings, line: string): { Deletion }
    local out: { Deletion } = {}
    if not (st.clozeHighlights or st.clozeBold or st.clozeCurly or string.find(line, "{{", 1, true)) then
        return out
    end
    -- Inline code blanked (the same length): its `==` and `{{` are code.
    local scan = line
    if string.find(line, "`", 1, true) then
        scan = line:gsub("(`+)(.-)%1", function(fence: string, inner: string)
            return string.rep(" ", #fence * 2 + #inner)
        end)
    end
    -- A deletion that opens and closes with the same two characters (`==`, `**`).
    local function pairs2(mark: string, pattern: number)
        local at = 1
        while true do
            local a = string.find(scan, mark, at, true)
            if not a then
                break
            end
            local z = string.find(scan, mark, a + 3, true)
            if not z then
                break
            end
            local answer, number, hint = deletionInside(line:sub(a + 2, z - 1), pattern)
            if trim(answer) ~= "" then
                table.insert(out, { from = a, to = z + 1, answer = answer, number = number, hint = hint, pattern = pattern })
            end
            at = z + 2
        end
    end
    if st.clozeHighlights then
        pairs2("==", 1)
    end
    if st.clozeBold then
        pairs2("**", 3)
    end
    local at = 1
    while string.find(scan, "{{", at, true) do
        local a = string.find(scan, "{{", at, true) :: number
        local z = string.find(scan, "}}", a + 2, true)
        if not z then
            break
        end
        local inner = line:sub(a + 2, z - 1)
        if string.match(inner, "^[cC]?%d+::.") then
            local answer, number, hint = deletionInside(inner, 2)
            if trim(answer) ~= "" then
                table.insert(out, { from = a, to = z + 1, answer = answer, number = number, hint = hint, pattern = 2 })
            end
        elseif st.clozeCurly and not string.find(inner, "^cloze[%s:]") then
            local answer, number, hint = deletionInside(inner, 4)
            if trim(answer) ~= "" then
                table.insert(out, { from = a, to = z + 1, answer = answer, number = number, hint = hint, pattern = 4 })
            end
        end
        at = z + 2
    end
    table.sort(out, function(x, y)
        return x.from < y.from
    end)
    -- Deletions inside another one are text.
    local kept, stop = {}, 0
    for _, d in out do
        if d.from > stop then
            table.insert(kept, d)
            stop = d.to
        end
    end
    return kept
end

local function isFence(line: string): string?
    return string.match(line, "^(```+)") or string.match(line, "^(~~~+)")
end

-- The cards of a note's lines, as the Obsidian Spaced Repetition plugin's parser finds them: a
-- single-line card is its line (and a scheduling comment on the next line), a multi-line card the
-- paragraph around its `?` / `??` line (the separators are the settings'), a cloze card the
-- paragraph of a line with a deletion; a blank line ends a paragraph — unless the settings have an
-- end-of-card marker: then a card goes on through blank lines up to a line that is the marker —,
-- code fences are skipped whole, an HTML comment starting a line (not a scheduling one) is skipped
-- to its end. Lines in `skip` (Logseq's cards) are no part of any. Unlike the plugin, a property
-- line ends a paragraph as a blank line does, and the line after a one-line HTML comment counts.
local function findCards(st: Settings, lines: { Line }, from: number, skip: { [number]: boolean }): { Found }
    local found: { Found } = {}
    local n = #lines
    local kind: string? = nil
    local first = from
    local count = 0 -- lines in the paragraph so far
    local marker = st.endMarker
    local function close(last: number)
        if kind ~= nil and count > 0 then
            -- (Blank lines before the marker are no part of the card: its state goes to its last line.)
            while last > first and trim(lines[last].text) == "" do
                last -= 1
            end
            table.insert(found, { kind = kind :: string, first = first, last = last })
        end
        kind = nil
        count = 0
    end
    local i = from
    while i <= n do
        local line = lines[i].text
        local t = trim(line)
        if skip[i] or isProperty(line) or (marker ~= "" and t == marker) then
            close(i - 1)
            first = i + 1
            i += 1
            continue
        end
        if t == "" then
            if kind ~= nil and marker ~= "" then
                count += 1 -- inside a card that only its marker ends
            else
                close(i - 1)
                first = i + 1
            end
            i += 1
            continue
        end
        if line:sub(1, 4) == "<!--" and line:sub(1, 7) ~= "<!--SR:" then
            while i <= n and not string.find(lines[i].text, "-->", 1, true) do
                i += 1
            end
            close(i - 1)
            first = i + 1
            i += 1
            continue
        end
        if count == 0 then
            first = i
        end
        count += 1
        local inline = inlineSeparator(st, line)
        if inline then
            -- A single-line card ends the paragraph: the lines above it are no part of it.
            local last = i
            if i + 1 <= n and lines[i + 1].text:sub(1, 7) == "<!--SR:" then
                last = i + 1
            end
            table.insert(found, { kind = inline.kind, first = i, last = last, sep = inline.sep })
            kind = nil
            count = 0
            i = last + 1
            first = i
            continue
        end
        local fence = isFence(line)
        if st.multi ~= "" and t == st.multi then
            if count > 1 then
                kind = "multiline"
            end
        elseif st.multiRev ~= "" and t == st.multiRev then
            if count > 1 then
                kind = "multireversed"
            end
        elseif fence then
            while i + 1 <= n and lines[i + 1].text:sub(1, #(fence :: string)) ~= fence do
                i += 1
                count += 1
            end
            if i + 1 <= n then
                i += 1
                count += 1
            end
        elseif kind == nil and #deletionsOf(st, line) > 0 then
            kind = "cloze"
        end
        i += 1
    end
    close(n)
    return found
end

-- The tags of a line (outside inline code), lower case, without "#".
local function tagsOf(line: string): { string }
    local out = {}
    for start, tag in string.gmatch(" " .. line, "[%s%(%[,;]()#([^%s#%[%]%(%),;!?\"']+)") do
        if not inCode(line, (start :: number) - 1) then
            local t = b.lower(((tag :: string):gsub("[%.:/]+$", "")))
            if t ~= "" and not t:find("^%d+$") then
                table.insert(out, t)
            end
        end
    end
    return out
end

-- Is `tag` one of the flashcard tags or a tag under one ("flashcards/geografia")?
local function isCardTag(st: Settings, tag: string): boolean
    for _, t in st.tags do
        if tag == t or tag:sub(1, #t + 1) == t .. "/" then
            return true
        end
    end
    return false
end

-- The lines' common indentation removed, and the first line's list marker or checkbox.
local function dedent(texts: { string }): { string }
    if #texts == 1 then
        local line = texts[1]
        local first = string.byte(line, 1)
        -- (Most cards: one line that starts with a letter.)
        if first == nil or not (first == 32 or first == 9 or first == 45 or first == 42 or first == 43 or (first >= 48 and first <= 57)) then
            return texts
        end
    end
    local indent: number? = nil
    for _, t in texts do
        if trim(t) ~= "" then
            local w = #(string.match(t, "^[ \t]*") :: string)
            indent = if indent == nil then w else math.min(indent :: number, w)
        end
    end
    -- (A list of several items keeps its markers: only a lone item's goes.)
    local items = 0
    for _, t in texts do
        local line = t:sub((indent or 0) + 1)
        if string.match(line, "^[%-%*%+]%s") or string.match(line, "^%d+[%.%)]%s") then
            items += 1
        end
    end
    local out = {}
    for i, t in texts do
        local line = t:sub((indent or 0) + 1)
        if i == 1 and items <= 1 then
            line = line:gsub("^[%-%*%+]%s+%[.%]%s+", ""):gsub("^[%-%*%+]%s+", ""):gsub("^%d+[%.%)]%s+", "")
        end
        table.insert(out, line)
    end
    return out
end

-- A side of a card as shown: scheduling comments, a block id at the end, the flashcard tags and
-- #card left out, Logseq's card properties too.
local function cleanSide(st: Settings, texts: { string }): string
    local out = {}
    for _, t in dedent(texts) do
        if isProperty(t) then
            continue -- a block's properties (Logseq's card-*:: too)
        end
        local line = t
        if string.find(line, "<!--SR:", 1, true) then
            line = withoutComment(line)
        end
        if string.find(line, "^", 1, true) then
            line = line:gsub("%s+%^[%w%-]+%s*$", "")
        end
        if string.find(line, "#", 1, true) then
            line = line:gsub("(%s?)#([^%s#%[%]%(%),;!?\"']+)", function(space: string, tag: string)
                local t2 = b.lower(tag)
                if isCardTag(st, t2) or t2 == "card" then
                    return ""
                end
                return nil :: any
            end)
        end
        if string.find(line, "%s$") then
            line = line:gsub("%s+$", "")
        end
        table.insert(out, line)
    end
    while #out > 0 and trim(out[#out]) == "" do
        table.remove(out)
    end
    while #out > 0 and trim(out[1]) == "" do
        table.remove(out, 1)
    end
    return table.concat(out, "\n")
end

-- A cloze side: the deletions of card `k` hidden ([...] or [hint]) on the question's side,
-- highlighted on the answer's; the other deletions show their answer as text.
local function clozeSide(st: Settings, texts: { string }, wanted: (Deletion, number) -> boolean, answerSide: boolean): { string }
    local out = {}
    local ordinal = 0 -- the deletion's place in the paragraph
    for _, t in texts do
        local ds = deletionsOf(st, t)
        if #ds == 0 then
            table.insert(out, t)
            continue
        end
        local parts, at = {}, 1
        for _, d in ds do
            ordinal += 1
            table.insert(parts, t:sub(at, d.from - 1))
            if wanted(d, ordinal) then
                if answerSide then
                    table.insert(parts, "==" .. trim(d.answer) .. "==")
                else
                    table.insert(parts, if d.hint and trim(d.hint :: string) ~= "" then `**[{trim(d.hint :: string)}]**` else "**[…]**")
                end
            else
                table.insert(parts, d.answer)
            end
            at = d.to + 1
        end
        table.insert(parts, t:sub(at))
        table.insert(out, table.concat(parts))
    end
    return out
end

-- The heading path above line `line` (headings outside code): "Geografia › Europa".
local function contextOf(page: string, headings: { { level: number, text: string, line: number } }, line: number): string
    local stack: { { level: number, text: string } } = {}
    for _, h in headings do
        if h.line >= line then
            break
        end
        while #stack > 0 and stack[#stack].level >= h.level do
            table.remove(stack)
        end
        table.insert(stack, { level = h.level, text = h.text })
    end
    local parts = { page }
    for k, h in stack do
        if not (k == 1 and b.fold(h.text) == b.fold(page)) then
            table.insert(parts, h.text)
        end
    end
    return table.concat(parts, " › ")
end

-- ---- the notebook's cards ---------------------------------------------------------------------

local MAX_CARDS = 50000
local MAX_PAGES = 5000

local function folderOf(path: string): string
    return string.match(path, "^(.*)/[^/]*$") or ""
end

local function hex(n: number): string
    return string.format("%08x", n)
end

-- The Markdown of lines `a` … `b` as written.
local function textsOf(lines: { Line }, a: number, z: number): { string }
    local out = {}
    for i = a, math.min(z, #lines) do
        table.insert(out, lines[i].text)
    end
    return out
end

-- One card per sibling of what findCards found on a page.
local function cardsOfFound(st: Settings, page: string, path: string, lines: { Line }, f: Found, decks: { string },
                            folderDeck: boolean, context: string, out: { Card })
    local texts = textsOf(lines, f.first, f.last)
    -- Where the state goes: a comment line of its own after the card (the plugin's default), else
    -- the card's last line (Brulion writes the comment there, as the plugin's "on the same line").
    local stateLine = f.last
    local own = false
    if lines[f.last].text:sub(1, 7) == "<!--SR:" and f.last > f.first then
        own = true
    else
        for i = f.last, f.first, -1 do
            if commentOf(lines[i].text) then
                stateLine = i
                own = trim(withoutComment(lines[i].text)) == ""
                break
            end
        end
    end
    local _, _, segments = commentOf(lines[stateLine].text)
    -- The card's own text, without the comment line.
    local body = {}
    for i, t in texts do
        if not (own and f.first + i - 1 == stateLine) then
            table.insert(body, t)
        end
    end
    local sides: { { q: string, a: string } } = {}
    if f.kind == "basic" or f.kind == "reversed" then
        local line = body[1]
        local sep = f.sep :: string
        local at = string.find(line, sep, 1, true) :: number
        -- A tag at the start of the card is the deck of this card alone.
        local q = line:sub(1, at - 1)
        local lead = string.match(trim(q), "^#([^%s#]+)")
        if lead and isCardTag(st, b.lower(lead :: string)) then
            if not folderDeck then
                decks = { b.lower(lead :: string) }
            end
            q = trim(q):gsub("^#[^%s#]+%s*", "")
        end
        local question = cleanSide(st, { q })
        local answer = cleanSide(st, { line:sub(at + #sep) })
        table.insert(sides, { q = question, a = answer })
        if f.kind == "reversed" then
            table.insert(sides, { q = answer, a = question })
        end
    elseif f.kind == "multiline" or f.kind == "multireversed" then
        local mark = if f.kind == "multireversed" then st.multiRev else st.multi
        local split = #body
        for i, t in body do
            if trim(t) == mark then
                split = i
                break
            end
        end
        local qs, as = {}, {}
        for i, t in body do
            if i < split then
                table.insert(qs, t)
            elseif i > split then
                table.insert(as, t)
            end
        end
        local question, answer = cleanSide(st, qs), cleanSide(st, as)
        table.insert(sides, { q = question, a = answer })
        if f.kind == "multireversed" then
            table.insert(sides, { q = answer, a = question })
        end
    else -- cloze: numbered deletions are asked by number, else one card each (the plugin's rule)
        local all: { Deletion } = {}
        local numbered = false
        for _, t in body do
            for _, d in deletionsOf(st, t) do
                table.insert(all, d)
                numbered = numbered or d.number > 0
            end
        end
        if numbered then
            local top = 0
            for _, d in all do
                top = math.max(top, d.number)
            end
            for k = 1, math.min(top, 100) do
                local function wanted(d: Deletion, _ordinal: number): boolean
                    return d.number == k
                end
                local q = cleanSide(st, clozeSide(st, body, wanted, false))
                local a = cleanSide(st, clozeSide(st, body, wanted, true))
                table.insert(sides, { q = q, a = a })
            end
        else
            -- (The k-th deletion of the paragraph by its place.)
            for k = 1, math.min(#all, 100) do
                local function wanted(_d: Deletion, ordinal: number): boolean
                    return ordinal == k
                end
                local q = cleanSide(st, clozeSide(st, body, wanted, false))
                local a = cleanSide(st, clozeSide(st, body, wanted, true))
                table.insert(sides, { q = q, a = a })
            end
        end
    end
    local key = path .. "\n" .. withoutComment(table.concat(body, "\n"))
    for k, side in sides do
        if #out >= MAX_CARDS then
            return
        end
        if trim(side.q) == "" or (f.kind == "cloze" and side.q == side.a) then
            continue -- a numbered cloze with a gap: no card in that place
        end
        local segment = segments[k]
        local memory = if segment ~= nil then parseSegment(segment :: string) else nil
        local id = hex(fnv1a32(key .. "\n" .. tostring(k)))
        table.insert(out, {
            id = id,
            baseId = id,
            key = key,
            kind = f.kind,
            sibling = k,
            siblings = #sides,
            question = side.q,
            answer = side.a,
            context = context,
            page = page,
            path = path,
            line = f.first,
            lastLine = f.last,
            decks = decks,
            folderDeck = folderDeck,
            folder = folderOf(path),
            memory = memory or newMemory(),
            stateRef = lines[stateLine].ref,
            stateText = lines[stateLine].text,
            stateLine = stateLine,
            stateOwn = own,
            segments = segments,
            logseq = false,
        })
    end
end

-- The cards of one page with a flashcard tag: its decks by the plugin's rules (a tag at the start
-- of a card, else the last line of flashcard tags above it, else the frontmatter's, else the
-- note's first).
local function pageCards(st: Settings, name: string, skip: { [number]: boolean }, out: { Card })
    local info = b.page(name)
    local lines = b.lines(name)
    if info == nil or lines == nil then
        return
    end
    local path: string = info.path
    local from = 1
    -- The frontmatter is no part of any card.
    if #lines > 0 and trim(lines[1].text) == "---" then
        for i = 2, #lines do
            if trim(lines[i].text) == "---" then
                from = i + 1
                break
            end
        end
    end
    local frontDecks = {}
    for _, t in info.tags or {} do
        if isCardTag(st, b.lower(t)) then
            table.insert(frontDecks, b.lower(t))
        end
    end
    local found = findCards(st, lines, from, skip)
    local inCard: { [number]: boolean } = {}
    for _, f in found do
        for i = f.first, f.last do
            inCard[i] = true
        end
    end
    -- Lines of tags (outside cards) and headings, in order.
    local tagLines: { { line: number, tags: { string } } } = {}
    local headings: { { level: number, text: string, line: number } } = {}
    local firstTag: string? = if #frontDecks > 0 then frontDecks[1] else nil
    local fence: string? = nil
    for i = from, #lines do
        local text = lines[i].text
        local f = isFence(text)
        if fence then
            if f and text:sub(1, #(fence :: string)) == fence then
                fence = nil
            end
            continue
        elseif f then
            fence = f
            continue
        end
        local hashes, title = string.match(text, "^(#+)%s+(.*)$")
        if hashes and #(hashes :: string) <= 6 then
            table.insert(headings, { level = #(hashes :: string), text = trim(withoutComment(title :: string)), line = i })
        end
        if not string.find(text, "#", 1, true) then
            continue
        end
        local cardTags = {}
        for _, t in tagsOf(text) do
            if isCardTag(st, t) then
                table.insert(cardTags, t)
            end
        end
        if #cardTags > 0 then
            firstTag = firstTag or cardTags[1]
            if not inCard[i] then
                table.insert(tagLines, { line = i, tags = cardTags })
            end
        end
    end
    for _, f in found do
        local decks = frontDecks
        for k = #tagLines, 1, -1 do
            if tagLines[k].line < f.first then
                decks = tagLines[k].tags
                break
            end
        end
        if #decks == 0 then
            decks = if firstTag then { firstTag :: string } else { st.tags[1] }
        end
        -- "Convert folders to decks": the plugin gives every card of a note in a folder that folder's
        -- deck, whatever its tags say (notes outside any folder keep the tags').
        local folderDeck = st.foldersAsDecks and folderOf(path) ~= ""
        if folderDeck then
            decks = { b.lower(folderOf(path)) }
        end
        cardsOfFound(st, info.name, path, lines, f, decks, folderDeck, contextOf(info.name, headings, f.first), out)
    end
end

-- Logseq's cards: blocks tagged #card — the block's text the question, its children the answer,
-- `{{cloze answer \\ cue}}` hidden all at once.
local function logseqClozes(text: string, answerSide: boolean): string
    return (text:gsub("{{cloze%s+(.-)}}", function(inner: string)
        local answer, cue = inner, nil
        local at = string.find(inner, "\\\\", 1, true)
        while at do
            local nextAt = string.find(inner, "\\\\", at + 2, true)
            if not nextAt then
                break
            end
            at = nextAt
        end
        if at then
            answer, cue = inner:sub(1, at - 1), inner:sub(at + 2)
        end
        if answerSide then
            return "==" .. trim(answer) .. "=="
        end
        return if cue and trim(cue :: string) ~= "" then `**({trim(cue :: string)})**` else "**[…]**"
    end))
end

local function indentOf(text: string): number
    local w = string.match(text, "^[ \t]*") :: string
    return #(w:gsub("\t", "    "))
end

local function logseqCards(st: Settings, out: { Card }, skipByPage: { [string]: { [number]: boolean } })
    local linesOf: { [string]: { Line }? } = {}
    for _, block in b.blocks({ tag = "card" }) do
        if #out >= MAX_CARDS then
            return
        end
        local path: string = block.path
        local lines = linesOf[path]
        if lines == nil then
            lines = b.lines(block.page) or {}
            linesOf[path] = lines
        end
        local ls = lines :: { Line }
        local first: number = block.line
        if first < 1 or first > #ls then
            continue
        end
        -- The block's own lines: a list item's first line and the deeper lines under it that are no
        -- list item, then its children — up to a line no deeper than the block. A paragraph's: its
        -- lines up to a blank one (no children).
        local base = indentOf(ls[first].text)
        local item = string.match(ls[first].text, "^%s*[%-%*%+]%s") ~= nil
        local ownEnd, last = first, first
        local inChildren = false
        for i = first + 1, #ls do
            local t = ls[i].text
            if trim(t) == "" then
                if not item then
                    break
                end
                continue
            end
            local deeper = indentOf(t) > base
            if item and not deeper then
                break
            end
            if item and string.match(t, "^%s*[%-%*%+]%s") then
                inChildren = true
            end
            if not inChildren then
                ownEnd = i
            end
            last = i
        end
        local skip = skipByPage[path] or {}
        for i = first, last do
            skip[i] = true
        end
        skipByPage[path] = skip
        local own, children = {}, {}
        for i = first, ownEnd do
            table.insert(own, ls[i].text)
        end
        for i = ownEnd + 1, last do
            table.insert(children, ls[i].text)
        end
        -- (The tags ending the block's line are its decks, not words of the question.)
        local head = own[1]
        own[1] = (head:gsub("%s+#[^%s#]+%s*$", ""))
        while own[1] ~= head do
            head = own[1]
            own[1] = (head:gsub("%s+#[^%s#]+%s*$", ""))
        end
        local question = cleanSide(st, own)
        local answer = cleanSide(st, children)
        local hasCloze = string.find(question, "{{cloze ", 1, true) ~= nil
        local q = logseqClozes(question, false)
        local a = if hasCloze then logseqClozes(question, true) .. (if answer ~= "" then "\n\n" .. answer else "") else answer
        local decks = { "card" }
        for _, t in block.tags or {} do
            if t ~= "card" then
                table.insert(decks, t)
            end
        end
        local key = path .. "\n" .. question
        local props: { [string]: string } = block.props or {}
        local id = hex(fnv1a32(key .. "\n1"))
        table.insert(out, {
            id = id,
            baseId = id,
            key = key,
            kind = "logseq",
            sibling = 1,
            siblings = 1,
            question = q,
            answer = a,
            context = block.page,
            page = block.page,
            path = path,
            line = first,
            lastLine = last,
            decks = decks,
            folderDeck = false,
            folder = folderOf(path),
            memory = logseqMemory(props),
            stateRef = block.ref,
            stateText = ls[first].text,
            stateLine = first,
            stateOwn = false,
            segments = {},
            logseq = true,
            blockRef = block.ref,
            props = props,
        })
    end
end

-- Every card of the notebook, in the order of their pages (by path) and lines; kept while
-- b.revision() and the tags are the same.
local cache = { revision = -1, tags = "", cards = {} :: { Card }, byId = {} :: { [string]: Card } }
-- Each deck page's cards as its text was when they were read: a page whose text is the same is not
-- parsed again (a note changed elsewhere changes the revision, not this page's cards).
local pageCache: { [string]: { text: string, tags: string, cards: { Card } } } = {}

-- Is `path` a page of the review log ("Fiszki/Powtórki 2026-11")?
local function isLogPage(path: string): boolean
    for _, w in { WRITTEN.pl, WRITTEN.en } do
        local prefix = w.logFolder .. "/" .. (string.match(w.logPage, "^(.-)%%s") or "")
        if path:sub(1, #prefix) == prefix and folderOf(path) == w.logFolder then
            return true
        end
    end
    return false
end

local function collect(st: Settings): { Card }
    local tagsKey = st.parseKey
    if cache.revision == b.revision() and cache.tags == tagsKey then
        return cache.cards
    end
    local out: { Card } = {}
    local skip: { [string]: { [number]: boolean } } = {}
    logseqCards(st, out, skip)
    -- The pages with a flashcard tag anywhere (their own, a block's, a sub-tag's).
    local names: { [string]: boolean } = {}
    local list: { string } = {}
    for _, tag in st.tags do
        local offset = 0
        repeat
            local result = b.query(`type:pages tag:"{tag}"`, 1000, offset)
            for _, item in result.items do
                if not names[item.page] and #list < MAX_PAGES then
                    names[item.page] = true
                    table.insert(list, item.page)
                end
            end
            offset += #result.items
        until #result.items == 0 or offset >= result.total or #list >= MAX_PAGES
    end
    -- "Convert folders to decks": every note in a folder is read, tagged or not (the review log
    -- pages are not: they are lines of grades, not cards).
    if st.foldersAsDecks then
        local offset = 0
        repeat
            local result = b.query("type:pages", 1000, offset)
            for _, item in result.items do
                if not names[item.page] and #list < MAX_PAGES then
                    local info = b.page(item.page)
                    local path: string = if info then info.path else ""
                    if folderOf(path) ~= "" and not isLogPage(path) then
                        names[item.page] = true
                        table.insert(list, item.page)
                    end
                end
            end
            offset += #result.items
        until #result.items == 0 or offset >= result.total or #list >= MAX_PAGES
    end
    local byPath: { { path: string, name: string } } = {}
    for _, name in list do
        local info = b.page(name)
        if info then
            table.insert(byPath, { path = info.path, name = name })
        end
    end
    table.sort(byPath, function(x, y)
        return x.path < y.path
    end)
    local pageCardsOut: { Card } = {}
    local kept: { [string]: { text: string, tags: string, cards: { Card } } } = {}
    for _, p in byPath do
        if #pageCardsOut + #out >= MAX_CARDS then
            break
        end
        local skipped = skip[p.path]
        local text = b.read(p.name)
        local hit = pageCache[p.path]
        local cardsOfPage: { Card }
        if skipped == nil and text ~= nil and hit ~= nil and hit.text == text and hit.tags == tagsKey then
            cardsOfPage = hit.cards
        else
            cardsOfPage = {}
            pageCards(st, p.name, skipped or {}, cardsOfPage)
        end
        if skipped == nil and text ~= nil then
            kept[p.path] = { text = text :: string, tags = tagsKey, cards = cardsOfPage }
        end
        for _, c in cardsOfPage do
            table.insert(pageCardsOut, c)
        end
    end
    pageCache = kept
    -- Notes' cards first, then Logseq's, each by path and line.
    for _, c in out do
        table.insert(pageCardsOut, c)
    end
    table.sort(pageCardsOut, function(x, y)
        if x.path ~= y.path then
            return x.path < y.path
        end
        if x.line ~= y.line then
            return x.line < y.line
        end
        return x.sibling < y.sibling
    end)
    local byId: { [string]: Card } = {}
    for _, c in pageCardsOut do
        -- The same card twice (two copies of a line): the second gets an id of its own.
        local id = c.baseId
        local n = 1
        while byId[id] do
            n += 1
            id = hex(fnv1a32(c.key .. "\n" .. tostring(c.sibling) .. "\n" .. tostring(n)))
        end
        c.id = id
        byId[id] = c
    end
    cache = { revision = b.revision(), tags = tagsKey, cards = pageCardsOut, byId = byId }
    return pageCardsOut
end

-- ---- the review log --------------------------------------------------------------------------

-- One line per grade on a page per month ("Fiszki/Powtórki 2026-11"):
--   - 2026-11-20 18:30 dobrze · powtórka · Stolica Peru
-- appended at the end (two devices' lines merge as additions); read back for the statistics and
-- for the new cards already started today.
type Entry = { at: number, grade: number, state: number }

local function logPageOf(month: string): string
    local w = written()
    return w.logFolder .. "/" .. string.format(w.logPage, month)
end

local logCache = { revision = -1, months = "", entries = {} :: { Entry } }

-- The entries of the months `months` (both languages' pages), oldest first.
local function logEntries(months: { string }): { Entry }
    local key = table.concat(months, ",")
    if logCache.revision == b.revision() and logCache.months == key then
        return logCache.entries
    end
    local out: { Entry } = {}
    local seen: { [string]: boolean } = {}
    for _, month in months do
        for _, w in { WRITTEN.pl, WRITTEN.en } do
            local name = w.logFolder .. "/" .. string.format(w.logPage, month)
            if seen[name] then
                continue
            end
            seen[name] = true
            for _, line in b.lines(name) or {} do
                local date, time, grade, state = string.match(line.text, "^%s*[%-%*]%s+(%d%d%d%d%-%d%d%-%d%d) (%d%d?:%d%d) ([^%s·]+)%s*·%s*([^·]-)%s*·")
                if date then
                    local at = minutesOf(date .. " " .. time)
                    local g = GRADE_WORDS[b.lower(grade :: string)]
                    local stt = STATE_WORDS[b.lower(trim(state :: string))]
                    if at and g and stt then
                        table.insert(out, { at = at :: number, grade = g :: number, state = stt :: number })
                    end
                end
            end
        end
    end
    table.sort(out, function(x, y)
        return x.at < y.at
    end)
    logCache = { revision = b.revision(), months = key, entries = out }
    return out
end

local function monthOf(day: number): string
    return dateOf(day):sub(1, 7)
end

-- This month's and the last two's (the statistics' 30 days, whatever the day).
local function recentMonths(today: number): { string }
    local y, m = civil(today)
    local out = {}
    for k = 2, 0, -1 do
        local index = y * 12 + (m - 1) - k
        table.insert(out, string.format("%04d-%02d", index // 12, index % 12 + 1))
    end
    return out
end

-- ---- settings, counts, the queue --------------------------------------------------------------

local DEFAULT_TAGS = { "flashcards", "fiszki" }

-- Settings from a view's params or a command's args (the app passes its own; text or numbers).
local function settingsOf(args: { [string]: any }?): Settings
    local a = args or {}
    local tags = {}
    local listed = a.tags
    if type(listed) == "table" then
        for _, t in listed do
            if type(t) == "string" then
                table.insert(tags, t)
            end
        end
    elseif type(listed) == "string" then
        for t in string.gmatch(listed, "[^,%s]+") do
            table.insert(tags, t)
        end
    end
    local clean = {}
    for _, t in tags do
        local tag = b.lower(trim((t:gsub("^#", ""))))
        if tag ~= "" and not table.find(clean, tag) then
            table.insert(clean, tag)
        end
    end
    if #clean == 0 then
        clean = table.clone(DEFAULT_TAGS)
    end
    local function num(key: string, default: number, lo: number, hi: number): number
        local x = tonumber(a[key])
        if not finite(x) then
            return default
        end
        return math.clamp(x :: number, lo, hi)
    end
    -- (A retention may come as a percentage: 90.)
    local given = tonumber(a.retention)
    local retention = if finite(given) and (given :: number) > 1 then (given :: number) / 100 else given
    retention = if finite(retention) then math.clamp(retention :: number, 0.7, 0.99) else 0.9
    -- The plugin's card settings (a missing key is its default; "" switches a separator off, as
    -- clearing the plugin's field does; a line break or a text of more than 40 characters is none).
    local function text(key: string, default: string): string
        local v = a[key]
        if type(v) ~= "string" or #v > 40 or string.find(v, "[\r\n]") then
            return default
        end
        return v :: string
    end
    local function flag(key: string, default: boolean): boolean
        local v = a[key]
        if v == true or v == "true" then
            return true
        elseif v == false or v == "false" then
            return false
        end
        return default
    end
    local single, singleRev = text("singleLineSeparator", "::"), text("singleLineReversedSeparator", ":::")
    local inline = {}
    -- (The plugin sorts them longest first; the basic one first when they are as long.)
    if single ~= "" then
        table.insert(inline, { sep = single, kind = "basic" })
    end
    if singleRev ~= "" then
        table.insert(inline, { sep = singleRev, kind = "reversed" })
    end
    table.sort(inline, function(x, y)
        if #x.sep ~= #y.sep then
            return #x.sep > #y.sep
        end
        return x.kind == "basic" and y.kind ~= "basic"
    end)
    local multi = trim(text("multilineSeparator", "?"))
    local multiRev = trim(text("multilineReversedSeparator", "??"))
    local endMarker = trim(text("multilineEndMarker", ""))
    local highlights, bold, curly = flag("clozeHighlights", true), flag("clozeBold", false), flag("clozeCurly", false)
    local folders = flag("foldersAsDecks", false)
    -- The tags of the notes reviewed as a whole (the plugin's "Note review tags", default #review).
    local reviewTags = {}
    local reviewListed = a.reviewTags
    local reviewWords: { string } = {}
    if type(reviewListed) == "table" then
        for _, t in reviewListed do
            if type(t) == "string" then
                table.insert(reviewWords, t)
            end
        end
    elseif type(reviewListed) == "string" then
        for t in string.gmatch(reviewListed, "[^,%s]+") do
            table.insert(reviewWords, t)
        end
    end
    for _, t in reviewWords do
        local tag = b.lower(trim((t:gsub("^#", ""))))
        if tag ~= "" and not table.find(reviewTags, tag) then
            table.insert(reviewTags, tag)
        end
    end
    if #reviewTags == 0 then
        reviewTags = { "review" }
    end
    local key = table.concat({
        table.concat(clean, ","), single, singleRev, multi, multiRev, endMarker,
        if highlights then "h" else "-", if bold then "b" else "-", if curly then "c" else "-",
        if folders then "f" else "-",
    }, "\n")
    return {
        tags = clean,
        inline = inline,
        multi = multi,
        multiRev = multiRev,
        endMarker = endMarker,
        clozeHighlights = highlights,
        clozeBold = bold,
        clozeCurly = curly,
        foldersAsDecks = folders,
        parseKey = key,
        reviewTags = reviewTags,
        baseEase = math.floor(num("baseEase", 250, 130, 1000)),
        easyBonus = num("easyBonus", 1.3, 1, 5),
        lapsesIntervalChange = num("lapsesIntervalChange", 0.5, 0.1, 1),
        maxNoteInterval = math.floor(num("maxNoteInterval", 36525, 1, 36525)),
        loadBalance = flag("loadBalance", true),
        newPerDay = math.floor(num("newPerDay", 20, 0, 9999)),
        retention = retention,
        maxInterval = math.floor(num("maxInterval", 36500, 1, 36500)),
        learning = { 1, 10 },
        relearning = { 10 },
        fuzz = a.fuzz ~= false and a.fuzz ~= "false",
        typeAnswer = a.typeAnswer == true or a.typeAnswer == "true",
    }
end

local function nowMinutes(): number
    return minutesOf(b.now()) :: number
end

-- A deck: "" or "all" every card, "tag:flashcards/geografia" (sub-decks too), "page:Geografia",
-- "folder:Nauka" (sub-folders too).
local function inDeck(c: Card, deck: string): boolean
    if deck == "" or deck == "all" then
        return true
    end
    local kind, value = string.match(deck, "^(%a+):(.*)$")
    if kind == "tag" then
        local v = b.lower(value :: string)
        for _, d in c.decks do
            if d == v or d:sub(1, #v + 1) == v .. "/" then
                return true
            end
        end
        return false
    elseif kind == "page" then
        return c.page == value or b.fold(c.page) == b.fold(value :: string)
    elseif kind == "folder" then
        local v = value :: string
        return c.folder == v or c.folder:sub(1, #v + 1) == v .. "/"
    end
    return false
end

local LEARN_AHEAD = 20 -- minutes: a learning card due this soon is shown when nothing else is left

local function isLearning(m: Memory): boolean
    return m.state == 1 or m.state == 3
end

-- New cards started today (the log's), for the daily limit.
local function newToday(today: number): number
    local n = 0
    for _, e in logEntries({ monthOf(today) }) do
        if e.state == 0 and e.at // 1440 == today then
            n += 1
        end
    end
    return n
end

type Queue = { learning: { Card }, review: { Card }, new: { Card }, newLeft: number, soon: { Card } }

-- What is to study now: learning cards due, review cards due today (or within `ahead` days), new
-- cards up to the day's limit (and `more` beyond it).
local function queueOf(st: Settings, cards: { Card }, deck: string, now: number, ahead: number, more: number): Queue
    local today = now // 1440
    local q: Queue = { learning = {}, review = {}, new = {}, newLeft = 0, soon = {} }
    local newCards = {}
    for _, c in cards do
        if not inDeck(c, deck) then
            continue
        end
        local m = c.memory
        if m.state == 0 then
            table.insert(newCards, c)
        elseif isLearning(m) then
            if m.due <= now then
                table.insert(q.learning, c)
            elseif m.due <= now + LEARN_AHEAD then
                table.insert(q.soon, c)
            end
        elseif m.due // 1440 <= today + ahead then
            table.insert(q.review, c)
        end
    end
    local byDue = function(x: Card, y: Card): boolean
        if x.memory.due ~= y.memory.due then
            return x.memory.due < y.memory.due
        end
        if x.path ~= y.path then
            return x.path < y.path
        end
        return x.line < y.line
    end
    table.sort(q.learning, byDue)
    table.sort(q.soon, byDue)
    -- Reviews: the most overdue day first, then as written.
    table.sort(q.review, function(x: Card, y: Card): boolean
        local dx, dy = x.memory.due // 1440, y.memory.due // 1440
        if dx ~= dy then
            return dx < dy
        end
        if x.path ~= y.path then
            return x.path < y.path
        end
        if x.line ~= y.line then
            return x.line < y.line
        end
        return x.sibling < y.sibling
    end)
    q.newLeft = math.max(0, st.newPerDay + more - newToday(today))
    for i = 1, math.min(q.newLeft, #newCards) do
        table.insert(q.new, newCards[i])
    end
    return q
end

-- Counts of a deck: due (reviews and learning cards due now), new available today, learning, all.
local function countsOf(st: Settings, cards: { Card }, deck: string, now: number)
    local q = queueOf(st, cards, deck, now, 0, 0)
    local total, newTotal, learning = 0, 0, 0
    for _, c in cards do
        if inDeck(c, deck) then
            total += 1
            if c.memory.state == 0 then
                newTotal += 1
            elseif isLearning(c.memory) then
                learning += 1
            end
        end
    end
    return {
        due = #q.review + #q.learning,
        review = #q.review,
        learningDue = #q.learning,
        new = #q.new,
        newTotal = newTotal,
        learning = learning,
        total = total,
    }
end

-- ---- writing a grade ---------------------------------------------------------------------------

-- What undoes one change: a line written back, a line removed, properties set back.
type Step = { op: string, ref: string, text: string?, props: { { string } }? }

-- The state line with `comment` in place of its scheduling comment, or with it added: before a
-- block id at the end, else at the end after one space (the plugin's "on the same line").
local function withComment(line: string, comment: string): string
    local old, start = commentOf(line)
    if old then
        return line:sub(1, (start :: number) - 1) .. comment .. line:sub((start :: number) + #(old :: string))
    end
    local body, id = string.match(line, "^(.-)(%s+%^[%w%-]+%s*)$")
    if body then
        return (body :: string) .. " " .. comment .. (id :: string)
    end
    local trimmed = line:gsub("%s+$", "")
    return trimmed .. " " .. comment
end

-- Writes the card's state `m` (nil: the card starts over); the steps that undo it.
local function writeState(c: Card, m: Memory?, g: number, now: number): { Step }
    if c.logseq then
        local pairs_: { { string } } = if m then logseqProperties(m :: Memory, g, now) else {}
        if m == nil then
            for _, key in LOGSEQ_KEYS do
                table.insert(pairs_, { key, "" })
            end
        end
        local before = {}
        local props = c.props or {}
        for _, p in pairs_ do
            table.insert(before, { p[1], props[p[1]] or "" })
        end
        local ref = b.setProperties(c.blockRef :: string, pairs_)
        return { { op = "props", ref = ref, props = before } }
    end
    local segments = table.clone(c.segments)
    while #segments < c.siblings do
        table.insert(segments, NEW_SEGMENT)
    end
    while #segments > c.siblings do
        table.remove(segments)
    end
    segments[c.sibling] = if m then "fsrs," .. formatFsrs(m :: Memory) else NEW_SEGMENT
    local allNew = true
    for _, seg in segments do
        allNew = allNew and seg == NEW_SEGMENT
    end
    local line = c.stateText
    local comment = "<!--SR:!" .. table.concat(segments, "!") .. "-->"
    if allNew and not c.stateOwn then
        -- Nothing reviewed any more: the comment goes (one on a line of its own stays, all new).
        local ref = b.replaceLine(c.stateRef, withoutComment(line))
        return { { op = "line", ref = ref, text = line } }
    end
    -- A card ending with a code fence keeps its comment on the next line (as the plugin does).
    if not commentOf(line) and isFence(trim(line)) and c.lastLine > c.line then
        local ref = b.insertLine(c.stateRef, comment, "after")
        return { { op = "remove", ref = ref } }
    end
    local ref = b.replaceLine(c.stateRef, withComment(line, comment))
    return { { op = "line", ref = ref, text = line } }
end

-- The log's line of a grade, appended to its month's page (made when missing).
local function writeLog(c: Card, g: number, state: number, now: number): Step?
    local w = written()
    local day = now // 1440
    local page = logPageOf(monthOf(day))
    local words = trim((c.question:gsub("%s+", " "):gsub("%*%*", ""):gsub("==", "")))
    if utf8.len(words) and (utf8.len(words) :: number) > 80 then
        words = words:sub(1, (utf8.offset(words, 80) :: number) - 1) .. "…"
    end
    local text = string.format("- %s %s · %s · %s", stampOf(now), w.grades[g], w.states[state], words)
    if b.page(page) == nil then
        b.write(page, string.format(w.logTitle, monthOf(day)) .. "\n\n" .. text .. "\n")
    else
        b.appendToPage(page, text)
    end
    local lines = b.lines(page) or {}
    for i = #lines, 1, -1 do
        if lines[i].text == text then
            return { op = "remove", ref = lines[i].ref }
        end
    end
    return nil
end

local function undoSteps(steps: { Step })
    for i = #steps, 1, -1 do
        local s = steps[i]
        if s.op == "line" then
            b.replaceLine(s.ref, s.text :: string)
        elseif s.op == "remove" then
            b.removeLine(s.ref)
        elseif s.op == "props" then
            b.setProperties(s.ref, s.props :: { { string } })
        elseif s.op == "write" then
            b.write(s.ref, s.text :: string)
        end
    end
end

-- A grade written: the card's state, the log; the steps that undo both.
local function grade(st: Settings, c: Card, g: number, now: number): (Memory, { Step })
    local before = c.memory
    local after = review(st, before, g, now, c.id)
    local steps = writeState(c, after, g, now)
    local logged = writeLog(c, g, before.state, now)
    if logged then
        table.insert(steps, logged :: Step)
    end
    return after, steps
end

-- ---- views ------------------------------------------------------------------------------------

-- An interval as a grade's button shows it: "1 min", "10 min", "4 dni", "2,5 mies.", "1,2 roku".
local function intervalText(minutes: number): string
    local u = ui().units
    if minutes < 60 then
        local n = math.max(1, math.floor(minutes + 0.5))
        return string.format(pluralForm(n, u.minutes), n)
    elseif minutes < 1440 then
        local n = math.floor(minutes / 60 + 0.5)
        return string.format(pluralForm(n, u.hours), n)
    end
    local days = math.floor(minutes / 1440 + 0.5)
    if days < 31 then
        return string.format(pluralForm(days, u.days), days)
    elseif days < 365 then
        local months = math.floor(days / 30 * 10 + 0.5) / 10
        local shown = b.formatNumber(months, 1)
        return string.format(pluralForm(if months == math.floor(months) then months else 5, u.months), shown)
    end
    local years = math.floor(days / 365 * 10 + 0.5) / 10
    local shown = b.formatNumber(years, 1)
    return string.format(pluralForm(if years == math.floor(years) then years else 2, u.years), shown)
end

-- When a card is due, in words: "dziś", "jutro", "za 4 dni".
local function dueText(m: Memory, now: number): string
    local today = now // 1440
    local day = m.due // 1440
    if m.state == 0 or day <= today then
        return ui().today
    elseif day == today + 1 then
        return ui().tomorrow
    end
    return string.format(ui().inDays, intervalText((day - today) * 1440))
end

-- The review session: what was graded and how to undo it, the card shown with its answer.
type Session = {
    key: string, day: number, done: number, revealed: string?, typed: string?, last: string?, undo: { { steps: { Step }, id: string } },
    total: number,
}
local session: Session = { key = "", day = -1, done = 0, revealed = nil, typed = nil, last = nil, undo = {}, total = 0 }

local function sessionFor(key: string, now: number): Session
    if session.key ~= key or session.day ~= now // 1440 then
        session = { key = key, day = now // 1440, done = 0, revealed = nil, typed = nil, last = nil, undo = {}, total = 0 }
    end
    return session
end

local function numberParam(params: { [string]: any }, key: string): number
    local x = tonumber(params[key])
    return if finite(x) then math.max(0, math.floor(x :: number)) else 0
end

local function deckTitle(deck: string): string
    if deck == "" or deck == "all" then
        return ui().allCards
    end
    local kind, value = string.match(deck, "^(%a+):(.*)$")
    if kind == "tag" then
        return "#" .. (value :: string)
    elseif kind == "folder" and value == "" then
        return ui().root
    end
    return value or deck
end

-- The next card of the queue: learning cards due, reviews, new cards — not the card just graded
-- while another is there — and, when nothing else is left, a learning card due within 20 minutes.
local function nextCard(q: Queue, last: string?): Card?
    local order = {}
    for _, list in { q.learning, q.review, q.new } do
        for _, c in list do
            table.insert(order, c)
        end
    end
    for _, c in order do
        if c.id ~= last then
            return c
        end
    end
    if #order > 0 then
        return order[1]
    end
    return q.soon[1]
end

local GRADE_TONES = { "danger", "warning", "success", "info" }

-- An answer's words as a typed one is compared with them: its plain text, letter case and
-- diacritics aside, spaces single.
local function answerWords(text: string): string
    return b.fold(trim((b.plain(text):gsub("%s+", " "))))
end

-- What was typed, with the answer: right (its words) or wrong.
local function typedNode(i: any, typed: string, answer: string): Node
    local right = answerWords(typed) ~= "" and answerWords(typed) == answerWords(answer)
    return b.ui.text(string.format(i.typed, trim(typed)),
        { tone = if right then "success" else "danger", icon = if right then "check" else "x" })
end

local function reviewView(params: { [string]: any }): Node
    local st = settingsOf(params)
    local now = nowMinutes()
    local deck = if type(params.deck) == "string" then params.deck else ""
    local ahead, more = numberParam(params, "ahead"), numberParam(params, "more")
    local cards = collect(st)
    local s = sessionFor(deck .. "|" .. ahead .. "|" .. more .. "|" .. st.parseKey, now)
    local q = queueOf(st, cards, deck, now, ahead, more)
    local left = #q.review + #q.learning + #q.new
    s.total = math.max(s.total, s.done + left)
    local i = ui()
    local nodes: { Node } = {}
    local card = nextCard(q, s.last)
    if card == nil then
        -- Done: when the next card is due, and what else may be studied.
        local nextDue: number? = nil
        for _, c in cards do
            if inDeck(c, deck) and c.memory.state ~= 0 then
                nextDue = if nextDue == nil then c.memory.due else math.min(nextDue :: number, c.memory.due)
            end
        end
        local when = if nextDue then dueText({ state = 2, due = nextDue :: number } :: any, now) else i.noNext
        local any = false
        for _, c in cards do
            if inDeck(c, deck) then
                any = true
                break
            end
        end
        if not any then
            table.insert(nodes, b.ui.empty(i.noneInDeck, i.noCardsText, "layers"))
        else
            table.insert(nodes, b.ui.empty(i.doneTitle,
                if s.done > 0 then string.format(i.doneText, s.done, when) else string.format(i.doneNothing, when),
                "circle-check"))
        end
        local row: { Node } = {}
        if #s.undo > 0 then
            table.insert(row, b.ui.button(i.undo, "undo", { deck = deck }, { key = "u", icon = "undo-2" }))
        end
        table.insert(row, b.ui.open(i.studyAhead, "review", { deck = deck, ahead = ahead + 1, more = more, group = params.group }, { icon = "fast-forward" }))
        local newWaiting = 0
        for _, c in cards do
            if inDeck(c, deck) and c.memory.state == 0 then
                newWaiting += 1
            end
        end
        if newWaiting > #q.new then
            local extra = math.min(10, newWaiting - #q.new)
            table.insert(row, b.ui.open(string.format(i.moreNew, extra), "review", { deck = deck, ahead = ahead, more = more + extra, group = params.group }, { icon = "plus" }))
        end
        table.insert(nodes, b.ui.row(row))
        table.insert(nodes, b.ui.open(i.backToDecks, "decks", { group = params.group }))
        return b.ui.column(nodes)
    end
    local c = card :: Card
    table.insert(nodes, b.ui.progress(s.done, math.max(1, s.total),
        string.format(i.session, s.done, #q.review, #q.new, #q.learning + #q.soon)))
    local revealed = s.revealed == c.id
    local face: { Node } = {
        b.ui.text(c.context, { muted = true, icon = if c.logseq then "list" else "file-text" }),
        b.ui.markdown(c.question, { size = "large" }),
    }
    if revealed then
        table.insert(face, b.ui.divider())
        table.insert(face, b.ui.markdown(c.answer))
        if s.typed ~= nil then
            table.insert(face, typedNode(i, s.typed :: string, c.answer))
        end
    end
    table.insert(nodes, b.ui.card(face))
    local args = { id = c.id, ref = c.stateRef, text = c.stateText, deck = deck }
    if not revealed then
        if st.typeAnswer then
            table.insert(nodes, b.ui.input({ placeholder = i.typeHint, action = "typed", args = args, submit = i.check }))
        end
        table.insert(nodes, b.ui.row({
            b.ui.button(i.showAnswer, "reveal", args, { key = "space", primary = not st.typeAnswer, icon = "eye" }),
        }))
    else
        local row: { Node } = {}
        for g = 1, 4 do
            local after = review(st, c.memory, g, now, c.id)
            local minutes = after.due - now
            local a = table.clone(args)
            a.grade = g
            table.insert(row, b.ui.button(i.grades[g], "grade", a, {
                key = tostring(g), tone = GRADE_TONES[g], hint = intervalText(minutes),
            }))
        end
        table.insert(nodes, b.ui.row(row))
    end
    local tools: { Node } = {}
    if #s.undo > 0 then
        table.insert(tools, b.ui.button(i.undo, "undo", { deck = deck }, { key = "u", icon = "undo-2" }))
    end
    table.insert(tools, b.ui.link(c.page, i.edit, c.line))
    table.insert(tools, b.ui.text(i.stateNames[c.memory.state] .. (if c.memory.state ~= 0 then " · " .. dueText(c.memory, now) else ""), true))
    table.insert(tools, b.ui.menu({
        { label = i.reset, action = "reset", args = args, icon = "rotate-ccw" },
        { label = i.openPage, page = c.page, line = c.line },
        { label = i.backToDecks, view = "decks", params = { group = params.group } },
    }, i.more))
    table.insert(nodes, b.ui.row(tools))
    return b.ui.column(nodes)
end

-- The card an action names, as it is now: nil (and a message) when its line changed meanwhile.
local function cardFor(st: Settings, args: { [string]: any }): Card?
    collect(st)
    local c = cache.byId[args.id]
    if c == nil or c.stateRef ~= args.ref or c.stateText ~= args.text then
        return nil
    end
    return c
end

local reviewActions = {
    reveal = function(args)
        session.revealed = args.id
        session.typed = nil
        return nil
    end,
    -- The answer typed (typeAnswer): the card shown with it.
    typed = function(args)
        session.revealed = args.id
        session.typed = if type(args.value) == "string" then string.sub(args.value, 1, 2000) else nil
        return nil
    end,
    grade = function(args)
        local st = settingsOf(args)
        local g = math.floor(tonumber(args.grade) or 0)
        if g < 1 or g > 4 then
            return nil
        end
        local c = cardFor(st, args)
        if c == nil then
            session.revealed = nil
            session.typed = nil
            return { message = ui().changed, tone = "warning" }
        end
        local ok, result, steps = pcall(grade, st, c :: Card, g, nowMinutes())
        if not ok then
            b.log(tostring(result))
            return { message = string.format(ui().notSaved, tostring(result)), tone = "error" }
        end
        table.insert(session.undo, { steps = steps, id = (c :: Card).id })
        while #session.undo > 50 do
            table.remove(session.undo, 1)
        end
        session.done += 1
        session.last = (c :: Card).id
        session.revealed = nil
        session.typed = nil
        return nil
    end,
    undo = function(args)
        local last = table.remove(session.undo)
        if last == nil then
            return { message = ui().nothingToUndo }
        end
        local ok, err = pcall(undoSteps, last.steps)
        if not ok then
            b.log(tostring(err))
            return { message = string.format(ui().notSaved, tostring(err)), tone = "error" }
        end
        session.done = math.max(0, session.done - 1)
        session.last = nil
        session.revealed = last.id -- the card again, its answer shown
        return { message = ui().undone }
    end,
    reset = function(args)
        local st = settingsOf(args)
        local c = cardFor(st, args)
        if c == nil then
            return { message = ui().changed, tone = "warning" }
        end
        local ok, steps = pcall(writeState, c :: Card, nil, 3, nowMinutes())
        if not ok then
            return { message = string.format(ui().notSaved, tostring(steps)), tone = "error" }
        end
        table.insert(session.undo, { steps = steps :: { Step }, id = (c :: Card).id })
        session.revealed = nil
        session.typed = nil
        return { message = ui().resetDone, tone = "success" }
    end,
}

-- ---- whole-note reviews -------------------------------------------------------------------------

-- The Spaced Repetition plugin's note reviews (docs/SYNTAX.md §15 "Notes to review"): a note with
-- a review tag (#review) is reviewed as a whole; its state is three keys of its frontmatter,
--   sr-due: 2026-11-23      the day it is due
--   sr-interval: 3          the days of the last interval
--   sr-ease: 250            the ease × 100
-- and a review is Hard, Good or Easy (the plugin's SM-2 variant, `osrSchedule`, ported with its
-- load balancing). Only these lines of the note are written — one line each, so that a sync merges
-- reviews on two devices line by line — and the rest stays byte for byte.
local NOTE_KEYS = { "sr-due", "sr-interval", "sr-ease" }
local NOTE_MAX_SHOWN = 100000 -- characters of a note drawn in a review
local NOTE_CHUNK = 3000

type NoteState = { due: number, interval: number, ease: number } -- due: a day number
type NoteRow = { name: string, path: string, state: NoteState?, sig: string }
-- A frontmatter line `key: value # comment` in its parts, to write another value in its place.
type Part = { head: string, quote: string, value: string, tail: string }

local function jsRound(x: number): number
    return math.floor(x + 0.5)
end

-- The first and the last line (1-based) of the frontmatter among `texts` (---, up to --- or ...).
local function frontOf(texts: { string }): (number?, number?)
    if texts[1] == nil or not texts[1]:match("^%-%-%-%s*$") then
        return nil, nil
    end
    for i = 2, #texts do
        if texts[i]:match("^%-%-%-%s*$") or texts[i]:match("^%.%.%.%s*$") then
            return 1, i
        end
    end
    return nil, nil
end

local function partsOf(text: string, key: string): Part?
    if text:sub(1, #key) ~= key then
        return nil
    end
    local gap, after = string.match(text:sub(#key + 1), "^([ \t]*):(.*)$")
    if gap == nil then
        return nil
    end
    local lead, body = string.match(after :: string, "^([ \t]*)(.*)$")
    local head = key .. (gap :: string) .. ":" .. (lead :: string)
    local text2 = body :: string
    local q = text2:sub(1, 1)
    if q == '"' or q == "'" then
        local close = text2:find(q, 2, true)
        if close then
            return { head = head, quote = q, value = text2:sub(2, close - 1), tail = text2:sub(close + 1) }
        end
    end
    local value, tail = string.match(text2, "^(.-)(%s+#.*)$")
    if value == nil then
        value, tail = string.match(text2, "^(.-)(%s*)$")
    end
    return { head = head, quote = "", value = value :: string, tail = tail :: string }
end

local MONTH_NAMES: { [string]: number } = {
    jan = 1, feb = 2, mar = 3, apr = 4, may = 5, jun = 6, jul = 7, aug = 8, sep = 9, oct = 10, nov = 11, dec = 12,
}

local function validDay(y: number?, m: number?, d: number?): number?
    if y == nil or m == nil or d == nil or y < 1 or m < 1 or m > 12 or d < 1 or d > daysInMonth(y, m) then
        return nil
    end
    return fromCivil(y, m, d)
end

-- The plugin's date formats (moment's, in this order): YYYY-MM-DD, DD-MM-YYYY, "ddd MMM DD YYYY".
local function noteDay(text: string): number?
    local iso = dayOf(text)
    if iso then
        return iso
    end
    local d, m, y = string.match(text, "^(%d%d?)%-(%d%d?)%-(%d%d%d%d)")
    if d then
        return validDay(tonumber(y), tonumber(m), tonumber(d))
    end
    local mon, d2, y2 = string.match(text, "^%a+ (%a%a%a) (%d%d?) (%d%d%d%d)")
    if mon then
        return validDay(tonumber(y2), MONTH_NAMES[b.lower(mon :: string)], tonumber(d2))
    end
    return nil
end

-- JavaScript's parseFloat: the number a text starts with.
local function floatOf(text: string): number?
    local n = string.match(text, "^%s*([%+%-]?%d+%.?%d*)") or string.match(text, "^%s*([%+%-]?%.%d+)")
    return if n then tonumber(n) else nil
end

-- A note's state from its lines (as b.lines gives them), and the text of the lines it stands on
-- (what a write checks again). A state needs all three keys: the due day, an interval and an ease
-- that are numbers and not 0 (the plugin's truthiness test); else the note is new.
local function stateOf(texts: { string }): (NoteState?, string)
    local first, last = frontOf(texts)
    if first == nil or last == nil then
        return nil, ""
    end
    local found: { [string]: Part } = {}
    local signature: { string } = {}
    for i = first + 1, last - 1 do
        for _, key in NOTE_KEYS do
            if found[key] == nil then
                local p = partsOf(texts[i], key)
                if p then
                    found[key] = p
                    table.insert(signature, texts[i])
                end
            end
        end
    end
    local sig = table.concat(signature, "\n")
    local due, interval, ease = found["sr-due"], found["sr-interval"], found["sr-ease"]
    if due == nil or interval == nil or ease == nil then
        return nil, sig
    end
    local day = noteDay(due.value)
    local ivl, ez = floatOf(interval.value), floatOf(ease.value)
    if day == nil or not finite(ivl) or not finite(ez) or ivl == 0 or ez == 0 then
        return nil, sig
    end
    return { due = day, interval = ivl :: number, ease = ez :: number }, sig
end

-- The plugin's `osrSchedule` (st3v3nmw/obsidian-spaced-repetition 1.15.4, note-scheduling.ts):
-- the interval and ease after `response` (2 Hard, 3 Good, 4 Easy), the last interval and ease,
-- the days the review is late and the notes' due days (a histogram of the days from today: load
-- balancing, which moves a long interval to the least used day within a few days of it).
local function osrSchedule(st: Settings, response: number, interval0: number, ease0: number, late: number,
    histogram: { [number]: number }?): (number, number)
    local lateDays = math.max(0, math.floor(late))
    local interval = math.max(1, interval0)
    local ease = ease0
    if response == 4 then
        ease += 20
        interval = ((interval + lateDays) * ease) / 100
        interval *= st.easyBonus
    elseif response == 3 then
        interval = ((interval + lateDays / 2) * ease) / 100
    elseif response == 2 then
        ease = math.max(130, ease - 20)
        interval = math.max(1, (interval + lateDays / 4) * st.lapsesIntervalChange)
    end
    if st.loadBalance and histogram ~= nil then
        interval = jsRound(interval)
        if interval > 7 then
            local fuzz: number
            if interval <= 21 then
                fuzz = 1
            elseif interval <= 180 then
                fuzz = math.min(3, math.floor(interval * 0.05))
            else
                fuzz = math.min(7, math.floor(interval * 0.025))
            end
            local h = histogram :: { [number]: number }
            if h[interval] ~= nil then
                local best = interval
                local original = interval
                for i = 1, fuzz do
                    local stop = false
                    for _, candidate in { original - i, original + i } do
                        if h[candidate] == nil then
                            best = candidate
                            stop = true
                            break
                        end
                        if h[candidate] < h[best] then
                            best = candidate
                        end
                    end
                    if stop then
                        break
                    end
                end
                interval = best
            end
        end
    end
    interval = math.min(interval, st.maxNoteInterval)
    interval = jsRound(interval * 10) / 10
    return interval, ease
end

local notesCache = { revision = -1, key = "", rows = {} :: { NoteRow } }

-- The notes with a review tag (their own, a block's, a sub-tag's), by path, each with its state.
local function collectNotes(st: Settings): { NoteRow }
    local key = table.concat(st.reviewTags, ",")
    if notesCache.revision == b.revision() and notesCache.key == key then
        return notesCache.rows
    end
    local names: { [string]: boolean } = {}
    local list: { string } = {}
    for _, tag in st.reviewTags do
        local offset = 0
        repeat
            local result = b.query(`type:pages tag:"{tag}"`, 1000, offset)
            for _, item in result.items do
                if not names[item.page] and #list < MAX_PAGES then
                    names[item.page] = true
                    table.insert(list, item.page)
                end
            end
            offset += #result.items
        until #result.items == 0 or offset >= result.total or #list >= MAX_PAGES
    end
    local rows: { NoteRow } = {}
    for _, name in list do
        local info = b.page(name)
        if info and not isLogPage(info.path) then
            local texts: { string } = {}
            for _, line in b.lines(info.path) or {} do
                table.insert(texts, line.text)
                if #texts >= 1000 then
                    break -- a frontmatter is never this long
                end
            end
            local state, sig = stateOf(texts)
            table.insert(rows, { name = name, path = info.path, state = state, sig = sig })
        end
    end
    table.sort(rows, function(x, y)
        return x.path < y.path
    end)
    notesCache = { revision = b.revision(), key = key, rows = rows }
    return rows
end

type NoteQueue = {
    due: { NoteRow }, new: { NoteRow }, scheduled: number, histogram: { [number]: number },
}

-- What is to review today: due notes (the earliest due day first), then new ones, in their paths'
-- order — the plugin's, which orders notes of one day by their PageRank. `skipped` are left out.
local function noteQueue(rows: { NoteRow }, today: number, skipped: { [string]: boolean }?): NoteQueue
    local q: NoteQueue = { due = {}, new = {}, scheduled = 0, histogram = {} }
    for _, r in rows do
        if r.state then
            q.scheduled += 1
            local days = (r.state :: NoteState).due - today
            q.histogram[days] = (q.histogram[days] or 0) + 1
        end
        if skipped and skipped[r.path] then
            continue
        end
        if r.state == nil then
            table.insert(q.new, r)
        elseif (r.state :: NoteState).due <= today then
            table.insert(q.due, r)
        end
    end
    table.sort(q.due, function(x, y)
        local dx, dy = (x.state :: NoteState).due, (y.state :: NoteState).due
        if dx ~= dy then
            return dx < dy
        end
        return x.path < y.path
    end)
    return q
end

local function noteCounts(rows: { NoteRow }, today: number)
    local q = noteQueue(rows, today, nil)
    return { due = #q.due, new = #q.new, scheduled = q.scheduled, total = #rows }
end

-- The state after `response` on `today`: a new note starts from interval 1 and the base ease (the
-- plugin's links to other notes' eases are not used), an old one from its own and the days late.
local function noteReview(st: Settings, row: NoteRow, response: number, today: number,
    histogram: { [number]: number }): NoteState
    local s = row.state
    local interval, ease, late = 1, st.baseEase, 0
    if s then
        interval, ease, late = s.interval, s.ease, today - s.due
    end
    local ivl, ez = osrSchedule(st, response, interval, ease, late, histogram)
    return { due = today + jsRound(ivl), interval = ivl, ease = ez }
end

local function numberText(x: number): string
    return tostring(jsRound(x * 10) / 10)
end

-- Writes `state` into the note's frontmatter (made when there is none): the three lines changed in
-- place, missing ones added after the others or before the closing line; the steps that undo it.
local function writeNoteState(path: string, state: NoteState): { Step }
    local values: { [string]: string } = {
        ["sr-due"] = dateOf(state.due), ["sr-interval"] = numberText(state.interval), ["sr-ease"] = numberText(state.ease),
    }
    local steps: { Step } = {}
    local function read(): ({ any }, { string })
        local lines = b.lines(path) or {}
        local texts: { string } = {}
        for _, l in lines do
            table.insert(texts, l.text)
        end
        return lines, texts
    end
    local lines, texts = read()
    local first, last = frontOf(texts)
    if first == nil then
        local header = { "---", "sr-due: " .. values["sr-due"], "sr-interval: " .. values["sr-interval"],
            "sr-ease: " .. values["sr-ease"], "---" }
        if #lines == 0 then
            b.write(path, table.concat(header, "\n") .. "\n")
            return { { op = "write", ref = path, text = "" } }
        end
        local ref = b.insertLine(lines[1].ref, header[1], "before")
        table.insert(steps, { op = "remove", ref = ref })
        for k = 2, #header do
            ref = b.insertLine(ref, header[k], "after")
            table.insert(steps, { op = "remove", ref = ref })
        end
        return steps
    end
    for _, key in NOTE_KEYS do
        lines, texts = read()
        first, last = frontOf(texts)
        local at: number? = nil
        local lastKey = 0
        for i = (first :: number) + 1, (last :: number) - 1 do
            for _, k in NOTE_KEYS do
                if partsOf(texts[i], k) then
                    lastKey = i
                    if k == key and at == nil then
                        at = i
                    end
                end
            end
        end
        if at then
            local old = texts[at :: number]
            local p = partsOf(old, key) :: Part
            local new = p.head .. p.quote .. values[key] .. p.quote .. p.tail
            if new ~= old then
                local ref = b.replaceLine(lines[at :: number].ref, new)
                table.insert(steps, { op = "line", ref = ref, text = old })
            end
        else
            local before = if lastKey > 0 then lastKey + 1 else last :: number
            local ref = b.insertLine(lines[before].ref, key .. ": " .. values[key], "before")
            table.insert(steps, { op = "remove", ref = ref })
        end
    end
    return steps
end

-- The note's lines after its frontmatter as Markdown chunks of about NOTE_CHUNK characters, cut at
-- blank lines outside code fences; the last flag says that the rest of a long note was left out.
local function noteBody(path: string): ({ string }, boolean)
    local texts: { string } = {}
    for _, l in b.lines(path) or {} do
        table.insert(texts, l.text)
    end
    local _, last = frontOf(texts)
    local chunks: { string } = {}
    local current: { string } = {}
    local size, total = 0, 0
    local fence: string? = nil
    local function flush()
        local text = table.concat(current, "\n")
        if trim(text) ~= "" then
            table.insert(chunks, text)
        end
        current = {}
        size = 0
    end
    for i = (last or 0) + 1, #texts do
        local line = texts[i]
        local mark = isFence(line)
        if mark then
            if fence == nil then
                fence = mark
            elseif mark == fence then
                fence = nil
            end
        end
        table.insert(current, line)
        size += #line + 1
        total += #line + 1
        if total > NOTE_MAX_SHOWN and fence == nil then
            flush()
            return chunks, i < #texts
        end
        if fence == nil and trim(line) == "" and size >= NOTE_CHUNK then
            flush()
        end
    end
    flush()
    return chunks, false
end

type NoteSession = {
    day: number, done: number, total: number, skipped: { [string]: boolean }, skips: number,
    undo: { { steps: { Step }, path: string } },
}
local noteSession: NoteSession = { day = -1, done = 0, total = 0, skipped = {}, skips = 0, undo = {} }

local function noteSessionFor(today: number): NoteSession
    if noteSession.day ~= today then
        noteSession = { day = today, done = 0, total = 0, skipped = {}, skips = 0, undo = {} }
    end
    return noteSession
end

local NOTE_GRADE_TONES = { [2] = "warning", [3] = "success", [4] = "info" }

-- What a button of the note's review shows: the days till its due.
local function daysText(today: number, due: number): string
    return intervalText(math.max(1, due - today) * 1440)
end

local function notesView(params: { [string]: any }): Node
    local st = settingsOf(params)
    local now = nowMinutes()
    local today = now // 1440
    local rows = collectNotes(st)
    local s = noteSessionFor(today)
    local q = noteQueue(rows, today, s.skipped)
    local i = ui()
    local nodes: { Node } = {}
    local group = params.group
    local row: NoteRow? = q.due[1] or q.new[1]
    s.total = math.max(s.total, s.done + #q.due + #q.new)
    if row == nil then
        if #rows == 0 then
            table.insert(nodes, b.ui.empty(i.noNotes, string.format(i.noNotesText, st.reviewTags[1]), "file-text"))
        else
            table.insert(nodes, b.ui.empty(i.notesDoneTitle,
                if s.done > 0 then string.format(i.doneText, s.done, i.noNext) else i.noNext, "circle-check"))
        end
        local tools: { Node } = {}
        if #s.undo > 0 then
            table.insert(tools, b.ui.button(i.undo, "undo", {}, { key = "u", icon = "undo-2" }))
        end
        if s.skips > 0 then
            table.insert(tools, b.ui.button(i.showSkipped, "unskip", {}, { icon = "rotate-ccw" }))
        end
        if #tools > 0 then
            table.insert(nodes, b.ui.row(tools))
        end
        table.insert(nodes, b.ui.open(i.backToDecks, "decks", { group = group }))
        return b.ui.column(nodes)
    end
    local r = row :: NoteRow
    table.insert(nodes, b.ui.progress(s.done, math.max(1, s.total), string.format(i.noteSession, s.done, #q.due, #q.new)))
    local face: { Node } = { b.ui.text(r.name, { muted = true, icon = "file-text" }) }
    local chunks, cut = noteBody(r.path)
    for _, chunk in chunks do
        table.insert(face, b.ui.markdown(chunk))
    end
    if cut then
        table.insert(face, b.ui.text(i.noteCut, true))
    end
    table.insert(nodes, b.ui.card(face))
    local args = { path = r.path, sig = r.sig }
    local buttons: { Node } = {}
    for g = 2, 4 do
        local after = noteReview(st, r, g, today, q.histogram)
        local a = table.clone(args)
        a.grade = g
        table.insert(buttons, b.ui.button(i.grades[g], "grade", a, {
            key = tostring(g - 1), tone = NOTE_GRADE_TONES[g], hint = daysText(today, after.due),
        }))
    end
    table.insert(nodes, b.ui.row(buttons))
    local tools: { Node } = {}
    table.insert(tools, b.ui.button(i.skip, "skip", args, { key = "s", icon = "skip-forward" }))
    if #s.undo > 0 then
        table.insert(tools, b.ui.button(i.undo, "undo", {}, { key = "u", icon = "undo-2" }))
    end
    table.insert(tools, b.ui.link(r.name, i.edit, 1))
    local label = if r.state then i.stateNames[2] .. " · " .. dueText({ state = 2, due = (r.state :: NoteState).due * 1440 } :: any, now)
        else i.noteNew
    table.insert(tools, b.ui.text(label, true))
    table.insert(tools, b.ui.menu({
        { label = i.openNote, page = r.name },
        { label = i.backToDecks, view = "decks", params = { group = group } },
    }, i.more))
    table.insert(nodes, b.ui.row(tools))
    return b.ui.column(nodes)
end

local noteActions = {
    grade = function(args)
        local st = settingsOf(args)
        local g = math.floor(tonumber(args.grade) or 0)
        if g < 2 or g > 4 then
            return nil
        end
        local now = nowMinutes()
        local today = now // 1440
        local found: NoteRow? = nil
        for _, r in collectNotes(st) do
            if r.path == args.path then
                found = r
            end
        end
        if found == nil or (found :: NoteRow).sig ~= args.sig then
            return { message = ui().noteChanged, tone = "warning" }
        end
        local r = found :: NoteRow
        local q = noteQueue(collectNotes(st), today, nil)
        local ok, steps = pcall(writeNoteState, r.path, noteReview(st, r, g, today, q.histogram))
        if not ok then
            b.log(tostring(steps))
            return { message = string.format(ui().notSaved, tostring(steps)), tone = "error" }
        end
        local s = noteSessionFor(today)
        table.insert(s.undo, { steps = steps :: { Step }, path = r.path })
        while #s.undo > 50 do
            table.remove(s.undo, 1)
        end
        s.done += 1
        return nil
    end,
    skip = function(args)
        local s = noteSessionFor(nowMinutes() // 1440)
        s.skipped[args.path] = true
        s.skips += 1
        return nil
    end,
    unskip = function(args)
        local s = noteSessionFor(nowMinutes() // 1440)
        s.skipped = {}
        s.skips = 0
        return nil
    end,
    undo = function(args)
        local s = noteSessionFor(nowMinutes() // 1440)
        local last = table.remove(s.undo)
        if last == nil then
            return { message = ui().nothingToUndo }
        end
        local ok, err = pcall(undoSteps, last.steps)
        if not ok then
            b.log(tostring(err))
            return { message = string.format(ui().notSaved, tostring(err)), tone = "error" }
        end
        s.done = math.max(0, s.done - 1)
        return { message = ui().undone }
    end,
}

-- ---- the decks ----------------------------------------------------------------------------------

local function decksOf(cards: { Card }, group: string): { { deck: string, title: string, depth: number } }
    local seen: { [string]: boolean } = {}
    local out = {}
    for _, c in cards do
        if group == "page" then
            local key = "page:" .. c.page
            if not seen[key] then
                seen[key] = true
                table.insert(out, { deck = key, title = c.page, depth = 0 })
            end
        elseif group == "folder" then
            local parts = split(c.folder, "/")
            local path = ""
            for k, part in parts do
                if part == "" and k == 1 then
                    local key = "folder:"
                    if not seen[key] then
                        seen[key] = true
                        table.insert(out, { deck = key, title = ui().root, depth = 0 })
                    end
                    break
                end
                path = if path == "" then part else path .. "/" .. part
                local key = "folder:" .. path
                if not seen[key] then
                    seen[key] = true
                    table.insert(out, { deck = key, title = part, depth = k - 1, sort = path })
                end
            end
        else
            for _, d in c.decks do
                local parts = split(d, "/")
                local path = ""
                for k, part in parts do
                    path = if path == "" then part else path .. "/" .. part
                    local key = "tag:" .. path
                    if not seen[key] then
                        seen[key] = true
                        table.insert(out, { deck = key, title = if k == 1 and not c.folderDeck then "#" .. part else part, depth = k - 1 })
                    end
                end
            end
        end
    end
    -- In alphabetical order of their paths (b.sorted: Polish letters in their places).
    local keys = {}
    local byKey = {}
    for _, d in out do
        table.insert(keys, d.deck)
        byKey[d.deck] = d
    end
    local sorted = {}
    for _, key in b.sorted(keys) do
        table.insert(sorted, byKey[key])
    end
    return sorted
end

-- The keys of the decks a card is in, as decksOf() lists them (each parent of a tag or a folder).
local function deckKeysOf(c: Card, group: string): { string }
    local keys: { string } = {}
    local seen: { [string]: boolean } = {}
    local function add(key: string)
        if not seen[key] then
            seen[key] = true
            table.insert(keys, key)
        end
    end
    if group == "page" then
        add("page:" .. c.page)
    elseif group == "folder" then
        if c.folder == "" then
            add("folder:")
        else
            local path = ""
            for _, part in split(c.folder, "/") do
                path = if path == "" then part else path .. "/" .. part
                add("folder:" .. path)
            end
        end
    else
        for _, d in c.decks do
            local path = ""
            for _, part in split(d, "/") do
                path = if path == "" then part else path .. "/" .. part
                add("tag:" .. path)
            end
        end
    end
    return keys
end

-- Every deck's counts in one pass over the cards (countsOf() for each deck would be one per deck):
-- due now, new left today (the day's limit shared, as a session of the deck would offer), all.
local function deckCounts(st: Settings, cards: { Card }, group: string, now: number)
    local today = now // 1440
    local newLeft = math.max(0, st.newPerDay - newToday(today))
    local out: { [string]: { due: number, new: number, total: number } } = {}
    for _, c in cards do
        local m = c.memory
        local due = (isLearning(m) and m.due <= now) or (m.state == 2 and m.due // 1440 <= today)
        for _, key in deckKeysOf(c, group) do
            local n = out[key]
            if n == nil then
                n = { due = 0, new = 0, total = 0 }
                out[key] = n
            end
            n.total += 1
            if m.state == 0 then
                n.new += 1
            elseif due then
                n.due += 1
            end
        end
    end
    for _, n in out do
        n.new = math.min(n.new, newLeft)
    end
    return out
end

local function decksView(params: { [string]: any }): Node
    local group = if params.group == "page" or params.group == "folder" then params.group else "tag"
    local st = settingsOf(params)
    local now = nowMinutes()
    local cards = collect(st)
    local i = ui()
    local noteRows = collectNotes(st)
    if #cards == 0 and #noteRows == 0 then
        return b.ui.empty(i.noCards, i.noCardsText, "layers")
    end
    if #cards == 0 then
        -- Only notes to review: their entry alone.
        local n = noteCounts(noteRows, now // 1440)
        return b.ui.column({
            b.ui.card({
                b.ui.row({ b.ui.open(i.notesTitle, "notes", { group = group }, { primary = true, icon = "file-text", key = "enter" }) }),
                b.ui.text(string.format(i.deckCounts, n.due, n.new, n.total), {
                    muted = n.due == 0, tone = if n.due > 0 then "accent" else nil, icon = "file-text",
                }),
            }),
            b.ui.text(i.noCardsText, true),
        })
    end
    local all = countsOf(st, cards, "", now)
    local today = now // 1440
    local reviewed = 0
    for _, e in logEntries({ monthOf(today) }) do
        if e.at // 1440 == today then
            reviewed += 1
        end
    end
    local nodes: { Node } = {
        b.ui.stats({
            { label = i.due, value = all.due, tone = if all.due > 0 then "accent" else nil },
            { label = i.new, value = all.new, tone = if all.new > 0 then "success" else nil },
            { label = i.learning, value = all.learning },
            { label = i.reviewedToday, value = reviewed },
        }),
        b.ui.row({
            b.ui.open(i.studyAll, "review", { deck = "", group = group }, { primary = true, icon = "play", key = "enter" }),
            b.ui.open(i.statistics, "stats", { deck = "", group = group }, { icon = "chart-column" }),
        }),
    }
    if #noteRows > 0 then
        -- Notes to review as a whole (the plugin's #review): one entry with its counts.
        local n = noteCounts(noteRows, now // 1440)
        table.insert(nodes, b.ui.card({
            b.ui.row({ b.ui.open(i.notesTitle, "notes", { group = group }, { icon = "file-text" }) }),
            b.ui.text(string.format(i.deckCounts, n.due, n.new, n.total), {
                muted = n.due == 0, tone = if n.due > 0 then "accent" else nil, icon = "file-text",
            }),
        }))
    end
    table.insert(nodes, b.ui.heading(if group == "page" then i.byPage elseif group == "folder" then i.byFolder else i.byTag))
    local counts = deckCounts(st, cards, group, now)
    for _, d in decksOf(cards, group) do
        local n = counts[d.deck] or { due = 0, new = 0, total = 0 }
        local title = string.rep("    ", d.depth) .. d.title
        local items = {
            { label = i.study, view = "review", params = { deck = d.deck, group = group }, icon = "play" },
            { label = i.studyAhead, view = "review", params = { deck = d.deck, ahead = 1, group = group }, icon = "fast-forward" },
            { label = i.statistics, view = "stats", params = { deck = d.deck, group = group }, icon = "chart-column" },
        }
        if group == "page" then
            table.insert(items, { label = i.openPage, page = d.title })
        end
        -- A card per deck: its name (a tap studies it) with its menu, its counts under them.
        table.insert(nodes, b.ui.card({
            b.ui.row({ b.ui.open(title, "review", { deck = d.deck, group = group }), b.ui.menu(items, i.more) }),
            b.ui.text(string.format(i.deckCounts, n.due, n.new, n.total), {
                muted = n.due == 0, tone = if n.due > 0 then "accent" else nil, icon = "layers",
            }),
        }))
    end
    return b.ui.column(nodes)
end

-- ---- statistics -------------------------------------------------------------------------------

type Stats = {
    total: number, new: number, learning: number, young: number, mature: number, due: number,
    reviewedToday: number, retention: number?, reviews: number, perDay: { { date: string, correct: number, again: number } },
    forecast: { { date: string, count: number } },
}

local function statsOf(st: Settings, cards: { Card }, deck: string, now: number): Stats
    local today = now // 1440
    local counts = countsOf(st, cards, deck, now)
    local young, mature = 0, 0
    local forecast = {}
    for k = 0, 29 do
        forecast[k + 1] = { date = dateOf(today + k), count = 0 }
    end
    for _, c in cards do
        if not inDeck(c, deck) then
            continue
        end
        local m = c.memory
        if m.state == 2 then
            if m.ivl >= 21 then
                mature += 1
            else
                young += 1
            end
        end
        if m.state ~= 0 then
            local k = math.max(0, m.due // 1440 - today)
            if k < 30 then
                forecast[k + 1].count += 1
            end
        end
    end
    -- The log's last 30 days: grades per day, and the share of reviews (of cards in review)
    -- recalled — "true retention". The log names no deck: its numbers are the notebook's.
    local perDay = {}
    for k = 29, 0, -1 do
        table.insert(perDay, { date = dateOf(today - k), correct = 0, again = 0 })
    end
    local recalled, reviews, reviewedToday, all = 0, 0, 0, 0
    for _, e in logEntries(recentMonths(today)) do
        local day = e.at // 1440
        local k = day - (today - 29)
        if k < 0 or k > 29 then
            continue
        end
        all += 1
        local d = perDay[k + 1]
        if e.grade == 1 then
            d.again += 1
        else
            d.correct += 1
        end
        if day == today then
            reviewedToday += 1
        end
        if e.state == 2 then
            reviews += 1
            if e.grade > 1 then
                recalled += 1
            end
        end
    end
    return {
        total = counts.total, new = counts.newTotal, learning = counts.learning, young = young, mature = mature,
        due = counts.due, reviewedToday = reviewedToday, retention = if reviews > 0 then recalled / reviews else nil,
        reviews = all, perDay = perDay, forecast = forecast,
    }
end

local function percent(x: number): string
    return b.formatNumber(x * 100, 1) .. " %"
end

local function statsView(params: { [string]: any }): Node
    local st = settingsOf(params)
    local now = nowMinutes()
    local deck = if type(params.deck) == "string" then params.deck else ""
    local cards = collect(st)
    local i = ui()
    local x = statsOf(st, cards, deck, now)
    local nodes: { Node } = {
        b.ui.heading(string.format(i.deckOf, deckTitle(deck))),
        b.ui.stats({
            { label = i.due, value = x.due, tone = if x.due > 0 then "accent" else nil },
            { label = i.reviewedToday, value = x.reviewedToday },
            { label = i.retention, value = if x.retention then percent(x.retention :: number) else "—",
              tone = if x.retention and (x.retention :: number) >= st.retention - 0.05 then "success" else nil },
            { label = i.total, value = x.total },
        }),
        b.ui.stats({
            { label = i.new, value = x.new },
            { label = i.learning, value = x.learning },
            { label = i.young, value = x.young },
            { label = i.mature, value = x.mature },
        }),
    }
    local labels, correct, again = {}, {}, {}
    for _, d in x.perDay do
        table.insert(labels, d.date:sub(9, 10) .. "." .. d.date:sub(6, 7))
        table.insert(correct, d.correct)
        table.insert(again, d.again)
    end
    if x.reviews > 0 then
        table.insert(nodes, b.ui.chart({
            kind = "bar", title = i.reviewsPerDay, labels = labels,
            series = { { name = i.correct, values = correct }, { name = i.forgotten, values = again } },
        }))
    else
        table.insert(nodes, b.ui.text(i.noReviews .. " — " .. i.noReviewsText, true))
    end
    local flabels, fvalues = {}, {}
    for _, d in x.forecast do
        table.insert(flabels, d.date:sub(9, 10) .. "." .. d.date:sub(6, 7))
        table.insert(fvalues, d.count)
    end
    table.insert(nodes, b.ui.chart({
        kind = "bar", title = i.forecast, labels = flabels, series = { { name = i.cards, values = fvalues } },
    }))
    table.insert(nodes, b.ui.row({
        b.ui.open(i.study, "review", { deck = deck, group = params.group }, { primary = true, icon = "play" }),
        b.ui.open(i.backToDecks, "decks", { group = params.group }, { icon = "layers" }),
    }))
    return b.ui.column(nodes)
end

b.view("decks", decksView, nil, { icon = "layers" })
b.view("review", reviewView, reviewActions, { icon = "layers" })
b.view("stats", statsView, nil, { icon = "chart-column" })
b.view("notes", notesView, noteActions, { icon = "file-text" })

-- ---- commands (the app's badge, brulion-cli card …, tests) ------------------------------------

local function argsOf(ctx): { [string]: any }
    return if type(ctx.args) == "table" then ctx.args else {}
end

local STATE_KEYS = { [0] = "new", [1] = "learning", [2] = "review", [3] = "relearning" }

local function memoryJson(m: Memory, now: number)
    local o: { [string]: any } = { state = STATE_KEYS[m.state], reps = m.reps, lapses = m.lapses }
    if m.state ~= 0 then
        o.due = stampOf(m.due)
        o.interval = m.ivl
        o.stability = math.floor(m.s * 10000 + 0.5) / 10000
        o.difficulty = math.floor(m.d * 10000 + 0.5) / 10000
        if m.last then
            o.lastReview = stampOf(m.last :: number)
            local r = retrievability(math.max(0, (now - (m.last :: number)) // 1440), m.s)
            o.retrievability = math.floor(r * 10000 + 0.5) / 10000
        end
        o.step = m.step
    end
    return o
end

local function cardJson(c: Card, now: number)
    return {
        id = c.id, kind = c.kind, sibling = c.sibling, siblings = c.siblings, page = c.page, path = c.path,
        line = c.line, decks = c.decks, folder = c.folder, question = c.question, answer = c.answer,
        context = c.context, ref = c.stateRef, memory = memoryJson(c.memory, now),
    }
end

local function gradeOf(word: any): number?
    local n = tonumber(word)
    if n then
        return if n >= 1 and n <= 4 and n == math.floor(n) then n else nil
    end
    if type(word) ~= "string" then
        return nil
    end
    return GRADE_WORDS[b.lower(trim(word))]
end

-- When the caller sets the time (brulion-cli card grade --at, tests): its minutes, else now.
local function atOf(args: { [string]: any }): number
    return minutesOf(args.at) or nowMinutes()
end

-- The due count the app shows by "Flashcards": cards due now and new ones left today.
b.command("count", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local now = atOf(args)
    local cards = collect(st)
    local n = countsOf(st, cards, if type(args.deck) == "string" then args.deck else "", now)
    local notes = noteCounts(collectNotes(st), now // 1440)
    return {
        due = n.due, new = n.new, learning = n.learning, total = n.total, review = n.review,
        notesDue = notes.due, notesNew = notes.new, notesTotal = notes.total,
    }
end, { palette = false })

-- `brulion-cli card list`: { deck?, due? (only what is to study now), limit? } → cards and counts.
b.command("list", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local now = atOf(args)
    local deck = if type(args.deck) == "string" then args.deck else ""
    local cards = collect(st)
    local limit = math.clamp(math.floor(tonumber(args.limit) or 1000), 1, 10000)
    local out = {}
    if args.due == true or args.due == "true" then
        local q = queueOf(st, cards, deck, now, numberParam(args, "ahead"), numberParam(args, "more"))
        for _, list in { q.learning, q.review, q.new } do
            for _, c in list do
                if #out < limit then
                    table.insert(out, cardJson(c, now))
                end
            end
        end
    else
        for _, c in cards do
            if inDeck(c, deck) and #out < limit then
                table.insert(out, cardJson(c, now))
            end
        end
    end
    return { cards = out, counts = countsOf(st, cards, deck, now), now = stampOf(now) }
end, { palette = false })

b.command("show", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    collect(st)
    local c = cache.byId[tostring(args.id or "")]
    if c == nil then
        return { error = "card_not_found", message = string.format(ui().noSuchCard, tostring(args.id)) }
    end
    local now = atOf(args)
    local o = cardJson(c :: Card, now)
    -- What each grade would schedule.
    local next = {}
    for g = 1, 4 do
        local after = review(st, (c :: Card).memory, g, now, (c :: Card).id)
        next[WRITTEN.en.grades[g]] = { due = stampOf(after.due), interval = intervalText(after.due - now) }
    end
    o.next = next
    return o
end, { palette = false })

-- `brulion-cli card grade <id> <grade>`: { id, grade (1–4 or again, hard, good, easy), at? }.
b.command("grade", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local g = gradeOf(args.grade)
    if g == nil then
        return { error = "bad_argument", message = ui().badGrade }
    end
    collect(st)
    local c = cache.byId[tostring(args.id or "")]
    if c == nil then
        return { error = "card_not_found", message = string.format(ui().noSuchCard, tostring(args.id)) }
    end
    local now = atOf(args)
    local before = (c :: Card).memory
    local after = grade(st, c :: Card, g :: number, now)
    return {
        id = (c :: Card).id, grade = WRITTEN.en.grades[g :: number], before = memoryJson(before, now),
        after = memoryJson(after, now), interval = intervalText(after.due - now), question = (c :: Card).question,
        page = (c :: Card).page, path = (c :: Card).path, line = (c :: Card).line,
    }
end, { palette = false })

local function noteJson(st: Settings, r: NoteRow, today: number, histogram: { [number]: number })
    local o: { [string]: any } = { page = r.name, path = r.path }
    local s = r.state
    if s == nil then
        o.status = "new"
    else
        o.status = if s.due <= today then "due" else "scheduled"
        o.due = dateOf(s.due)
        o.interval = s.interval
        o.ease = s.ease
    end
    local nextOf = {}
    for g = 2, 4 do
        local after = noteReview(st, r, g, today, histogram)
        nextOf[WRITTEN.en.grades[g]] = {
            due = dateOf(after.due), interval = after.interval, ease = after.ease, text = daysText(today, after.due),
        }
    end
    o.next = nextOf
    return o
end

-- `brulion-cli card notes`: { due? (only what is to review now), limit? } → the notes with a review
-- tag, what each grade would schedule, and the counts.
b.command("notes", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local today = atOf(args) // 1440
    local rows = collectNotes(st)
    local q = noteQueue(rows, today, nil)
    local limit = math.clamp(math.floor(tonumber(args.limit) or 1000), 1, 10000)
    local list: { NoteRow } = {}
    if args.due == true or args.due == "true" then
        for _, r in q.due do
            table.insert(list, r)
        end
        for _, r in q.new do
            table.insert(list, r)
        end
    else
        list = rows
    end
    local out = {}
    for _, r in list do
        if #out < limit then
            table.insert(out, noteJson(st, r, today, q.histogram))
        end
    end
    return {
        notes = out, counts = { due = #q.due, new = #q.new, scheduled = q.scheduled, total = #rows },
        today = dateOf(today), tags = st.reviewTags,
    }
end, { palette = false })

-- `brulion-cli card review-note <page> <grade>`: { page (a name or a path), grade (hard, good, easy,
-- 2–4), at? } → the state before and after; the note's frontmatter written.
b.command("review-note", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local g = gradeOf(args.grade)
    if g == nil or (g :: number) < 2 then
        return { error = "bad_argument", message = ui().badNoteGrade }
    end
    local word = tostring(args.page or "")
    local today = atOf(args) // 1440
    local rows = collectNotes(st)
    local found: NoteRow? = nil
    for _, r in rows do
        if r.path == word or r.path == word .. ".md" or b.fold(r.name) == b.fold(word) then
            found = r
            break
        end
    end
    if found == nil then
        local info = b.page(word)
        if info then
            return { error = "not_review_note", message = string.format(ui().notReviewNote, word, st.reviewTags[1]) }
        end
        return { error = "note_not_found", message = string.format(ui().noSuchNote, word) }
    end
    local r = found :: NoteRow
    local q = noteQueue(rows, today, nil)
    local before = noteJson(st, r, today, q.histogram)
    local after = noteReview(st, r, g :: number, today, q.histogram)
    writeNoteState(r.path, after)
    return {
        page = r.name, path = r.path, grade = WRITTEN.en.grades[g :: number], before = before,
        after = { due = dateOf(after.due), interval = after.interval, ease = after.ease },
        interval = daysText(today, after.due),
    }
end, { palette = false })

-- The plugin's scheduling alone (tests, the documentation): { grade, interval?, ease?, late?
-- (days), histogram? ({ ["5"] = 2 }: notes due in that many days from today), loadBalance? } →
-- { interval, ease }.
b.command("schedule-note", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local g = gradeOf(args.grade)
    if g == nil or (g :: number) < 2 then
        return { error = "bad_argument", message = ui().badNoteGrade }
    end
    local histogram: { [number]: number }? = nil
    if type(args.histogram) == "table" then
        histogram = {}
        for k, v in args.histogram do
            (histogram :: { [number]: number })[tonumber(k) :: number] = tonumber(v) :: number
        end
    end
    local interval, ease = osrSchedule(st, g :: number, tonumber(args.interval) or 1, tonumber(args.ease) or st.baseEase,
        tonumber(args.late) or 0, histogram)
    return { interval = interval, ease = ease, text = daysText(0, jsRound(interval)) }
end, { palette = false })

b.command("stats", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    local now = atOf(args)
    local deck = if type(args.deck) == "string" then args.deck else ""
    local x = statsOf(st, collect(st), deck, now)
    return {
        deck = deck, total = x.total, new = x.new, learning = x.learning, young = x.young, mature = x.mature,
        due = x.due, reviewedToday = x.reviewedToday, retention = x.retention, reviews = x.reviews,
        perDay = x.perDay, forecast = x.forecast,
    }
end, { palette = false })

-- FSRS alone (tests, the documentation): { grades = { 3, 3, 1 … }, at = { "2026-11-20 08:00", … }
-- (one per grade; none: each at the due of the one before), start?, seed?, fuzz?, retention? } →
-- the memory after each grade, with `r`, the retrievability at its next due. Also { sm2 = { ease,
-- interval } } → the stability and difficulty an SM-2 card stands for, { draw = "text" } → the
-- fuzz's first number for that seed, { fuzzRange = days } → the fuzz's { low, high }.
b.command("schedule", function(ctx)
    local args = argsOf(ctx)
    local st = settingsOf(args)
    if type(args.sm2) == "table" then
        local s, d = fromSm2(tonumber(args.sm2[1]) or 2.5, tonumber(args.sm2[2]) or 1)
        return { stability = s, difficulty = d }
    elseif type(args.draw) == "string" then
        return { draw = mulberry32(fnv1a32(args.draw)), hash = fnv1a32(args.draw) }
    elseif tonumber(args.fuzzRange) then
        local lo, hi = fuzzRange(st, tonumber(args.fuzzRange) :: number)
        return { low = lo, high = hi }
    end
    local m = newMemory()
    local seed = tostring(args.seed or "card")
    local now = minutesOf(args.start) or nowMinutes()
    local out = {}
    for k, word in args.grades or {} do
        local g = gradeOf(word)
        if g == nil then
            return { error = "bad_argument", message = ui().badGrade }
        end
        local at = if type(args.at) == "table" and args.at[k] then minutesOf(args.at[k]) else nil
        if at == nil and k > 1 then
            at = m.due
        end
        now = at or now
        m = review(st, m, g :: number, now, seed)
        table.insert(out, {
            state = STATE_KEYS[m.state], stability = m.s, difficulty = m.d, interval = m.ivl, step = m.step,
            due = stampOf(m.due), minutes = m.due - now, reps = m.reps, lapses = m.lapses,
            r = retrievability(m.ivl, m.s),
        })
    end
    return { steps = out }
end, { palette = false })
