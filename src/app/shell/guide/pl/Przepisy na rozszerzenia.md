---
tags: [dokumentacja]
aliases: [Przykłady rozszerzeń, Przykłady skryptów]
brulion: document
---
# Przepisy na rozszerzenia

Gotowe kawałki do własnych [[Rozszerzenia|rozszerzeń]]. Żeby któryś zatrzymać, utwórz stronę poleceniem **Nowe rozszerzenie** (paleta) i wstaw kod do jej bloku `luau`; gdzie przepis mówi **Wymaga: zapis**, dopisz `uprawnienia: [zapis]` do właściwości strony. Żeby tylko spróbować, ustaw kursor w kodzie tutaj i wybierz **Uruchom blok Luau** — polecenie albo widok będzie w palecie do ponownego wczytania rozszerzeń. Każdą użytą tu funkcję objaśnia [[API rozszerzeń]].

## Notatnik w liczbach
Polecenie, które tylko czyta i odpowiada komunikatem.

```luau
b.command("liczby", function(ctx)
    local projekty = b.pages({ tag = "projekt" })
    local otwarte = b.query("type:tasks status:open", 1)
    local zalegle = b.query("type:tasks due:overdue", 1)
    local zadania = `otwartych zadań: {otwarte.total} (zaległych: {zalegle.total})`
    return `Stron: {#b.pages()}, projektów: {#projekty}, {zadania}`
end, { label = "Notatnik w liczbach" })
```

## Zadanie na jutro, zadanie o dzień później
**Wymaga: zapis.** Daty to teksty, a `dodajDni` przesuwa datę o kilka dni. Pierwsze polecenie pyta o zadanie i zapisuje je w dzisiejszym dzienniku z terminem na jutro; drugie przesuwa o dzień termin zadania pod kursorem.

```luau
-- "2026-11-13" i 1 dają "2026-11-14"; koniec miesiąca i roku też się zgadza.
local function dodajDni(data, n)
    local r, m, d = data:match("^(%d+)-(%d+)-(%d+)$")
    local poludnie = os.time({ year = tonumber(r), month = tonumber(m), day = tonumber(d) + n })
    return os.date("!%Y-%m-%d", poludnie)
end

b.command("jutro", function(ctx)
    local tekst = b.ask("Zadanie na jutro")
    if tekst == "" then
        return nil
    end
    local termin = dodajDni(b.today(), 1)
    b.appendToJournal(b.today(), `- [ ] {tekst} 📅 {termin}`)
    return `Dodano, termin {termin}`
end, { label = "Zadanie na jutro" })

b.command("przeloz", function(ctx)
    local blok = ctx.block
    local termin = blok and blok.text:match("📅 (%d%d%d%d%-%d%d%-%d%d)")
    if not termin then
        return "Ustaw kursor w zadaniu z terminem."
    end
    local pozniej = dodajDni(termin, 1)
    local tekst = blok.text:gsub("📅 %d%d%d%d%-%d%d%-%d%d", "📅 " .. pozniej, 1)
    b.replaceBlock(blok.ref, tekst, blok.task)
    return `Termin: {pozniej}`
end, { label = "Przełóż zadanie o dzień", block = true })
```

## Notatka z kilku pytań
**Wymaga: zapis.** Lista do wyboru, pytanie, nowa strona w folderze i link do niej w dzisiejszym dzienniku.

```luau
local RODZAJE = {
    ["Spotkanie"] = { folder = "Spotkania", tag = "spotkanie" },
    ["Książka"] = { folder = "Książki", tag = "książka" },
    ["Pomysł"] = { folder = "Pomysły", tag = "pomysł" },
}

b.command("nowa-notatka", function(ctx)
    local rodzaj = b.choose("Jaka to notatka?", { "Spotkanie", "Książka", "Pomysł" })
    if rodzaj == nil then
        return nil
    end
    local tytul = b.ask(`{rodzaj}: jaki tytuł?`)
    if tytul == "" then
        return nil
    end
    local nazwa = `{RODZAJE[rodzaj].folder}/{tytul}`
    if b.read(nazwa) ~= nil then
        return `Strona {nazwa} już jest`
    end
    b.write(nazwa, table.concat({
        "---",
        `tags: [{RODZAJE[rodzaj].tag}]`,
        `utworzono: {b.today()}`,
        "---",
        `# {tytul}`,
        "",
        "- ",
    }, "\n") .. "\n")
    b.appendToJournal(b.today(), `- {rodzaj}: [[{nazwa}]]`)
    return `Utworzono: {nazwa}`
end, { label = "Nowa notatka wybranego rodzaju" })
```

## Terminy tego tygodnia z polami wyboru
**Wymaga: zapis.** Widok nad zapytaniem. Odhaczenie zadania wywołuje akcję z `ref` i `text` zadania; jeśli wiersz zmienił się w międzyczasie, nic nie zostaje zapisane, a widok rysuje się od nowa.

```luau
b.view("tydzien", function(params)
    local wynik = b.query("type:tasks status:open due:<=+7d sort:due", 100)
    if #wynik.items == 0 then
        return b.ui.empty("Nic pilnego", "Żadne otwarte zadanie nie ma terminu w ciągu tygodnia.")
    end
    local wiersze = { b.ui.heading(`Terminy w ciągu tygodnia: {wynik.total}`) }
    for _, zadanie in wynik.items do
        table.insert(wiersze, b.ui.row({
            b.ui.check(false, "zrobione", { ref = zadanie.ref, text = zadanie.text }),
            b.ui.text(zadanie.text),
            b.ui.link(zadanie.page),
        }))
    end
    return b.ui.column(wiersze)
end, {
    zrobione = function(args)
        b.replaceBlock(args.ref, args.text, "x")
    end,
}, { label = "Terminy tego tygodnia", icon = "calendar-check" })
```

## Projekty w tabeli
Widok z tabelą: każda strona z tagiem `projekt`, jej otwarte zadania i najbliższy termin.

```luau
b.view("projekty", function(params)
    local wiersze = {}
    for _, nazwa in b.pages({ tag = "projekt" }) do
        local otwarte = b.query(`type:tasks status:open page:"{nazwa}" sort:due`, 1)
        local pierwsze = otwarte.items[1]
        table.insert(wiersze, { nazwa, tostring(otwarte.total), pierwsze and pierwsze.due or "—" })
    end
    if #wiersze == 0 then
        return b.ui.empty("Brak projektów", "Daj stronie tag projekt w jej właściwościach.")
    end
    local elementy = {
        b.ui.heading("Projekty"),
        b.ui.table({ "Projekt", "Otwarte", "Najbliższy termin" }, wiersze),
    }
    for _, wiersz in wiersze do
        table.insert(elementy, b.ui.link(wiersz[1]))
    end
    return b.ui.column(elementy)
end, nil, { label = "Projekty", icon = "folder" })
```

## Słowa na dzień
Wykres dziennika: notatki dni z dwóch tygodni wybrane zapytaniem, ich słowa policzone.

```luau
local function slowa(tekst)
    local n = 0
    for _ in tekst:gmatch("%S+") do
        n += 1
    end
    return n
end

b.view("pisanie", function(params)
    local dni = b.query("type:pages date:-13d..today sort:date", 14)
    local etykiety, liczby = {}, {}
    for _, dzien in dni.items do
        table.insert(etykiety, dzien.date:sub(6))
        table.insert(liczby, slowa(b.read(dzien.page) or ""))
    end
    if #etykiety == 0 then
        return b.ui.empty("Brak notatek dni", "Przez ostatnie dwa tygodnie nic nie napisano.")
    end
    return b.ui.column({
        b.ui.heading("Słowa na dzień"),
        b.ui.chart({
            kind = "bar",
            labels = etykiety,
            series = { { name = "Słowa", values = liczby } },
        }),
    })
end, nil, { label = "Słowa na dzień", icon = "chart-column" })
```

## Liczniki z − i +
**Wymaga: zapis.** Wiersze takie jak `- Szklanki wody 3 #licznik`, gdziekolwiek w notatniku, stają się widokiem z licznikiem przy każdym; dotknięcie przepisuje liczbę w jego wierszu.

```luau
b.view("liczniki", function(params)
    local wiersze = {}
    for _, blok in b.blocks({ tag = "licznik" }) do
        local nazwa, liczba = blok.text:match("^(.-)%s+(%d+)%s+#licznik")
        if nazwa then
            table.insert(wiersze, b.ui.row({
                b.ui.text(nazwa),
                b.ui.stepper({
                    value = tonumber(liczba),
                    step = 1,
                    action = "ustaw",
                    args = { ref = blok.ref, nazwa = nazwa },
                }),
            }))
        end
    end
    if #wiersze == 0 then
        return b.ui.empty("Brak liczników", "Napisz wiersz, który kończy się liczbą i tagiem licznik.")
    end
    return b.ui.column(wiersze)
end, {
    ustaw = function(args)
        b.replaceBlock(args.ref, `{args.nazwa} {b.formatNumber(args.value, 0)} #licznik`)
    end,
}, { label = "Liczniki", icon = "list-checks" })
```

## Dziennik pracy, który pisze się sam
**Wymaga: zapis.** Reakcja na zapis: każda strona, którą edytujesz, trafia raz do dzisiejszego dziennika jako link.

```luau
b.on("save", function(e)
    local strona = b.page(e.page)
    if not strona or strona.date then
        return                          -- zniknęła w międzyczasie albo to sam dzień dziennika
    end
    local link = `[[{e.page}]]`
    local dzis = b.read(b.today()) or ""
    if not dzis:find(link, 1, true) then
        b.appendToJournal(b.today(), `- Praca nad {link}`)
    end
end)
```

## Strona, która pisze się sama
**Wymaga: zapis.** Reakcja na zmiany utrzymuje stronę „Spis projektów” w zgodzie ze stronami z tagiem `projekt`. Zapisuje tylko wtedy, gdy coś się zmieniło, więc synchronizacja nie przenosi niczego na darmo, a to, co zapisuje rozszerzenie, nigdy nie wraca do niego jako zmiana.

```luau
local function przebuduj()
    local wiersze = { "Pisane przez rozszerzenie; zmiany tutaj zostaną zastąpione.", "" }
    for _, nazwa in b.pages({ tag = "projekt" }) do
        local otwarte = b.query(`type:tasks status:open page:"{nazwa}"`, 1).total
        table.insert(wiersze, `- [[{nazwa}]] — otwarte zadania: {otwarte}`)
    end
    local tekst = table.concat(wiersze, "\n") .. "\n"
    if b.read("Spis projektów") ~= tekst then
        b.write("Spis projektów", tekst)
    end
end

b.on("change", function(e)
    przebuduj()
end)

b.command("przebuduj", function(ctx)
    przebuduj()
    return "Spis projektów jest aktualny"
end, { label = "Przebuduj spis projektów" })
```

## Podsumowanie tygodnia od AI
**Wymaga: zapis, ai** (i modelu wybranego w **Ustawieniach → Rozszerzenia → AI**, zobacz [[AI]]). Zadania zrobione w tym tygodniu idą do modelu; jego odpowiedź trafia do dzisiejszego dziennika.

```luau
b.command("podsumowanie", function(ctx)
    local zrobione = b.query("type:tasks status:done done:this-week", 200)
    if #zrobione.items == 0 then
        return "W tym tygodniu nie skończono żadnego zadania."
    end
    local wiersze = {}
    for _, zadanie in zrobione.items do
        table.insert(wiersze, `- {zadanie.text} ({zadanie.page})`)
    end
    local odpowiedz = b.ai({
        system = "Piszesz krótkie, ciepłe podsumowanie tygodnia z listy zrobionych zadań: "
            .. "trzy zdania, bez listy.",
        prompt = table.concat(wiersze, "\n"),
        maxTokens = 600,
    })
    local wiersz = odpowiedz:gsub("%s+", " ")
    b.appendToJournal(b.today(), `- Podsumowanie tygodnia: {wiersz}`)
    return "Podsumowanie jest w dzisiejszym dzienniku"
end, { label = "Podsumuj mój tydzień (AI)" })
```

## Dobre nawyki
- Sprawdzaj, czego może nie być: `b.read` i `b.page` dają `nil` dla strony, której nie ma, `ctx.block` nie ma poza blokiem, a `ctx.page` jest pustym tekstem poza stroną.
- W widoku daj kontrolce to, czego potrzebuje jej akcja (`ref`, `text`), zamiast szukać tego od nowa: wiersz, który się zmienił, zostanie wtedy odrzucony, a nie nadpisany.
- Proś notatnik o mniej: zapytanie z filtrami i limitem jest szybsze niż czytanie każdej strony.
- Porównuj teksty przez `b.fold`, a sortuj przez `b.sorted`, żeby polskie litery zachowywały się, jak trzeba.
- To, co ma zostać, trzymaj w notatkach. Zmienne znikają, gdy strona zostanie zapisana od nowa.
