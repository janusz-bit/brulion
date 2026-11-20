---
tags: [dokumentacja]
aliases: [API Luau, API skryptów]
brulion: document
---
# API rozszerzeń

Wszystko, co kod [[Rozszerzenia|rozszerzenia]] może zrobić z Twoim notatnikiem, to funkcje jednej tabeli: `b`. Ta strona wymienia je wszystkie; [[Przepisy na rozszerzenia]] pokazują je przy pracy. Żeby którąś wypróbować, otwórz **konsolę Luau** (paleta) i ją wpisz — `b.today()`, `#b.pages()` — albo ustaw kursor w przykładzie poniżej i wybierz **Uruchom blok Luau**: to, co przykład zwraca, pojawi się jako komunikat, a polecenie albo widok, który definiuje, będzie w palecie do ponownego wczytania rozszerzeń.

## Kilka pojęć
- **Stronę** nazywasz tak jak w linku: tytułem, aliasem albo ścieżką (`Projekty/Dom`). Dzień dziennika nazywa się swoją datą: `2026-11-13`.
- **Data** to tekst taki jak `2026-11-13`; z godziną `2026-11-13 09:00`.
- Czego nie ma, to `nil`: `b.read` strony, która nie istnieje. Pomyłka — zły argument, brak uprawnienia, wiersz zmieniony w międzyczasie — to **błąd**: polecenie się zatrzymuje i mówi dlaczego (zobacz Błędy na końcu).
- To, co daje Ci funkcja, jest kopią. Zmiana w niej niczego nie zmienia w notatkach.

## Czytanie
Czytanie nie wymaga uprawnień.

| Funkcja | Co dostajesz |
|---|---|
| `b.pages()` | nazwy wszystkich stron, także dni dziennika, alfabetycznie |
| `b.pages({ tag = "projekt" })` | tylko strony, które mają ten tag wśród własnych tagów (`tags: [projekt]` we właściwościach strony) |
| `b.pages({ prop = "nawyk" })` | tylko strony, które mają tę właściwość wśród własnych (`nawyk: codziennie` we właściwościach strony); `prop = { "habit", "nawyk" }` przyjmuje którąkolwiek, można też dodać `tag` |
| `b.page(nazwa)` | `{ name, path, tags, props, date }`: plik strony w notatniku (`Projekty/Dom.md`), jej własne tagi i właściwości oraz jej dzień, gdy to strona dziennika; `nil`, gdy takiej strony nie ma |
| `b.read(nazwa)` | cały tekst strony albo `nil` |
| `b.blocks({ page = nazwa })` | bloki strony od góry, każdy z blokami pod nim |
| `b.blocks({ tag = "pomysł" })` | każdy blok, który sam ma ten tag; z `journal = true`, `from = "2026-11-01"` albo `to = "2026-11-30"` tylko bloki dziennika |
| `b.blocks({ prop = "nawyk" })` | każdy blok, który ma pod sobą tę właściwość (`nawyk:: codziennie`); z `tag` bloki, które mają jedno i drugie, ze stroną `page` tylko bloki tej strony (także te głębiej) |
| `b.search(tekst, limit)` | to, co znajduje widok wyszukiwania: jedno trafienie na stronę, najlepsze najpierw (20 bez `limit`, najwyżej 200) |
| `b.query(tekst, limit, offset)` | zapytanie w języku opisanym na stronie [[Zapytania]]: `{ total, items }` (50 pozycji bez `limit`, najwyżej 1000; `offset` pomija tyle pierwszych, więc można przejść po dowolnej ich liczbie — `total` liczy wszystkie). Tygodnie zaczynają się od pierwszego dnia tygodnia z ustawień notatnika |
| `b.attachments()` | pliki notatnika, które nie są notatkami — obrazki, PDF-y — jako ścieżki: `zalaczniki/paragon.jpg` |
| `b.revision()` | liczbę, która zmienia się przy każdej zmianie notatki: to, co kod wyliczył z notatek, jest aktualne, dopóki ta liczba jest ta sama |
| `b.checkExtension(nazwa, tekst)` | próbuje `tekst` jako stronę rozszerzenia `nazwa`, nie zapisując go i o nic nie pytając: `{ ok, commands, views, hooks, … }` albo `ok = false` z `error` i wierszem strony `line`. Niczego nie zapisuje; tak Asystent AI sprawdza rozszerzenie, zanim je zapisze |

```luau
local wkrotce = b.query("type:tasks status:open due:<=+7d sort:due")
local wiersze = { wkrotce.total .. " zadań ma termin w ciągu tygodnia" }
for _, zadanie in wkrotce.items do
    table.insert(wiersze, zadanie.due .. "  " .. zadanie.text)
end
return table.concat(wiersze, "\n")
```

`b.blocks({ tag = … })` znajduje bloki, które same mają tag. `tag:` w zapytaniu znajduje też to, co leży pod blokiem z tagiem albo na stronie z tagiem, a `tag:projekt` łapie także `projekt/dom`.

## Blok
Blok to punkt listy, akapit albo nagłówek. `b.blocks`, `b.search`, `b.query` i `ctx.block` opisują go tak samo:

| Pole | Czym jest |
|---|---|
| `text` | pierwszy wiersz bloku tak, jak go napisałeś, bez punktora i pola wyboru; tagi, daty i linki dalej w nim są |
| `plain` | same jego słowa: zadanie bez dat, bez tagów, linki jako ich nazwy — `Zapłacić czynsz` |
| `task` | dla zadania znak w jego polu wyboru: `" "` otwarte, `"x"` zrobione, `"/"` w toku, `"-"` anulowane, `">"` odłożone; `nil` dla bloku, który nie jest zadaniem |
| `page`, `date` | strona, na której leży, i dzień, gdy to strona dziennika |
| `tags` | własne tagi bloku, małymi literami, bez krzyżyka |
| `props` | jego właściwości: `{ autor = "Bolesław Prus" }` dla wiersza `autor:: Bolesław Prus` pod nim |
| `children` | bloki pod nim (z `b.blocks` i `ctx.block`) |
| `ref` | miejsce bloku. Podaj je funkcji `b.replaceBlock` albo innej funkcji zapisu, żeby zmienić blok; przestaje działać, gdy wiersz się zmieni |
| `path`, `line` | z `b.blocks` i `ctx.block`: plik strony i wiersz bloku w nim, liczony od 1 — `b.ui.link(blok.page, nil, blok.line)` otwiera go w tym miejscu |
| `line`, `excerpt` | z `b.search`: wiersz, który pasuje najlepiej — ma najwięcej szukanych słów — liczony od 1, i tekst wokół niego |
| `due`, `scheduled`, `start`, `reminder` | z `b.query`: daty zadania i czas przypomnienia |

```luau
-- Strona „Zadania” jako konspekt: każdy blok, bloki pod nim wcięte.
local wiersze = {}
local function pokaz(bloki, wciecie)
    for _, blok in bloki do
        table.insert(wiersze, wciecie .. blok.text)
        pokaz(blok.children, wciecie .. "  ")
    end
end
pokaz(b.blocks({ page = "Zadania" }), "")
return table.concat(wiersze, "\n")
```

## Zapis
Żeby zmieniać notatki, strona rozszerzenia potrzebuje `uprawnienia: [zapis]` we właściwościach. Konsola może pisać zawsze.

| Funkcja | Co robi |
|---|---|
| `b.appendToJournal(data, tekst)` | dopisuje tekst na końcu notatki tego dnia; brakującą notatkę tworzy z Twojego szablonu dziennika. Tekst trafia tak, jak go napisano: punkt zacznij od `- ` |
| `b.appendToPage(nazwa, tekst)` | to samo na końcu dowolnej strony; strona, której nie ma, zostaje utworzona |
| `b.write(nazwa, tekst)` | tworzy stronę albo zastępuje **cały** jej tekst; `"Spotkania/Budżet"` utworzy też folder |
| `b.replaceBlock(ref, tekst, task)` | daje pierwszemu wierszowi bloku nowy tekst; z `task` (`"x"`, `" "`, `"/"`, `"-"`) ustawia też pole wyboru tak, jak kliknięcie (zrobione zadanie dostaje datę). Zwraca nowe `ref` bloku |
| `b.insertBlock(ref, tekst, gdzie)` | wstawia nowe wiersze przy bloku: za nim i blokami pod nim (`"after"`, gdy bez `gdzie`), przed nim (`"before"`) albo jako ostatni blok pod nim (`"child"`) — z pasującym wcięciem. Zwraca `ref` pierwszego nowego wiersza |
| `b.removeBlock(ref)` | usuwa blok razem z blokami pod nim |
| `b.setTask(ref, stan)` | samo pole wyboru, tak jak ustawia je kliknięcie: `"x"` zrobione (z dzisiejszą datą), `" "` otwarte, `"/"` w toku, `"-"` anulowane. Zwraca `ref` bloku |
| `b.setProperty(ref, klucz, wartość)` | właściwość bloku `klucz:: wartość`: zmienia ją, gdy jest, a gdy jej nie ma, dopisuje pod blokiem; `nil` albo `""` ją usuwa. Zwraca `ref` bloku |

`b.replaceBlock` zostawia punktor, wcięcie i daty zadania (gdziekolwiek stoją w wierszu), chyba że nowy tekst ma własne daty. Ona i pozostałe funkcje, które biorą `ref`, **odmawiają**, gdy wiersz zmienił się, odkąd kod go przeczytał — przyszła synchronizacja, coś tam dopisałeś — więc nigdy nie nadpiszą zmiany, której kod nie widział. Błąd ma w sobie słowo `stale`; przeczytaj blok jeszcze raz. Wiersze dopisane albo usunięte nad blokiem nie przeszkadzają.

```luau
b.command("zrobione", function(ctx)
    local blok = ctx.block
    if not blok or not blok.task then
        return "Ustaw kursor w zadaniu."
    end
    b.replaceBlock(blok.ref, blok.text, "x")
    return "Zrobione: " .. blok.text
end, { label = "Oznacz zadanie jako zrobione", block = true })
```

```luau
-- Pierwszy krok pod zadaniem przy kursorze, od razu w toku, i notka, kiedy go dodano.
b.command("podziel", function(ctx)
    local blok = ctx.block
    if not blok or not blok.task then
        return "Ustaw kursor w zadaniu."
    end
    local krok = b.insertBlock(blok.ref, "- [ ] Pierwszy krok", "child")
    b.setTask(krok, "/")
    b.setProperty(blok.ref, "podzielone", b.today())
    return `Pierwszy krok pod „{blok.plain}”`
end, { label = "Podziel zadanie", block = true })

-- Zrobione zadania strony na ekranie przechodzą na stronę „Archiwum”.
b.command("archiwizuj", function(ctx)
    if ctx.page == "" then
        return "Najpierw otwórz stronę."
    end
    local przeniesione = 0
    for _, blok in b.blocks({ page = ctx.page }) do
        if blok.task == "x" and #blok.children == 0 then
            b.appendToPage("Archiwum", `- [x] {blok.text} (z [[{ctx.page}]])`)
            b.removeBlock(blok.ref)
            przeniesione += 1
        end
    end
    return `Zrobione zadania w [[Archiwum]]: {przeniesione}`
end, { label = "Przenieś zrobione zadania do archiwum" })
```

## Pomocnicze

| Funkcja | Co dostajesz |
|---|---|
| `b.today()` | dzisiejszą datę: `2026-11-13` |
| `b.now()` | datę i godzinę teraz: `2026-11-13 09:05` |
| `b.daysBetween(a, b)` | ile dni mija od daty `a` do daty `b` |
| `b.addDays(data, n)`, `b.addMonths(data, n)` | datę `n` dni albo miesięcy później (wcześniej, gdy `n` jest ujemne): `b.addDays("2026-12-31", 1)` to `2027-01-01`, `b.addMonths("2026-01-31", 1)` to `2026-02-28` |
| `b.weekday(data)` | dzień tygodnia: 1 to poniedziałek, 7 niedziela |
| `b.weekStart()` | pierwszy dzień tygodnia z ustawień notatnika, liczony tak samo |
| `b.weekdayName(n, forma)`, `b.monthName(n, forma)` | nazwę dnia tygodnia (1–7) albo miesiąca (1–12) w języku interfejsu; `forma` to `"full"` (bez niej: `środa`, `listopad`), `"short"` (`pn`) albo `"of"` (dopełniacz, jak w dacie: `listopada`) |
| `b.sorted(lista)` | teksty w kolejności alfabetycznej, z polskimi literami na swoich miejscach i `Rozdział 2` przed `Rozdział 10` |
| `b.fold(tekst)` | tekst małymi literami i bez polskich znaków (`Żółw` daje `zolw`): do porównywania i szukania |
| `b.lower(tekst)` | tekst małymi literami, z polskimi znakami (`ŻÓŁW` daje `żółw`, czego `string.lower` nie umie) |
| `b.plain(tekst)` | same słowa wiersza: bez tagów, linki jako ich nazwy (`Czytać [[Książki/Diuna]] #wieczór` daje `Czytać Diuna`) |
| `b.parseRecurrence(tekst)` | jak Brulion rozumie powtarzanie („co 2 tygodnie w poniedziałek”, „every month on the 15th”): `{ every = "week", interval = 2, weekdays = { 1 } }` albo `nil` |
| `b.parseWeekdays(tekst)` | listę dni tygodnia: `"pn, śr i pt"` daje `{ 1, 3, 5 }`; `nil`, gdy to nie są dni |
| `b.formatRecurrence(reguła, język)` | takie powtarzanie słowami, po polsku (`"pl"`) albo po angielsku (`"en"`): `co tydzień w poniedziałek, piątek` |
| `b.formatNumber(n, miejsca)` | liczbę jako tekst, w polskim notatniku z przecinkiem: `82,5` |
| `b.lang()`, `b.uiLang()` | język notatek i język interfejsu: `"pl"` albo `"en"` |
| `b.log(tekst)`, `print(…)` | wiersz w konsoli Luau |
| `b.status(tekst)` | mówi, co robi teraz polecenie czekające na odpowiedź („Czytam „Dom”…”); czat z AI pokazuje to pod kropkami AI, `b.status("")` to czyści |
| `b.timer(sekundy, etykieta)` | uruchamia timer przerwy (ten ze strony [[Siłownia]]): powiadomienie, gdy czas minie |
| `b.data(nazwa)` | dane dołączone do wbudowanego rozszerzenia (lista ćwiczeń Siłowni, opis pisania rozszerzeń dla Asystenta AI); we własnym `nil` |

## Polecenia
`b.command(nazwa, function(ctx) … end, { label = "…", block = true })` dodaje do palety polecenie pod etykietą `label`.

- `ctx.page` to nazwa strony na ekranie; pusty tekst, gdy żadnej nie ma.
- Z `block = true` polecenie działa na bloku pod kursorem, `ctx.block`, a paleta pokazuje je tylko w notatce. Sprawdź, czy `ctx.block` jest.
- Z `palette = false` paleta go nie pokazuje: uruchamia je przycisk widoku (`b.ui.run`) albo sama aplikacja — tak czat z AI uruchamia polecenie `chat` asystenta AI.
- `ctx.args` to to, co polecenie dostało od wywołującego: od przycisku komunikatu (niżej) albo z `brulion-cli extension run … --args '{"page": "Dom"}'`; `{}`, gdy nic nie dostało (pisz `ctx.args or {}`: tura czatu z AI nie ma go wcale).
- Tekst, który funkcja zwróci, pojawia się jako komunikat. Można nie zwracać niczego. Tabela `{ message = "…", tone = "success", button = { label = "Otwórz", page = "Dom" } }` też jest komunikatem: `tone` to `"success"`, `"warning"` albo `"error"`, a przycisk otwiera stronę, uruchamia jedno z poleceń rozszerzenia (`command = "…"`, jego `args` jako `ctx.args`) albo — w tym, co zwraca akcja widoku — jeszcze raz akcję tego widoku (`action = "…"`, `args`): tak działa **Cofnij**.

## Widoki
`b.view(nazwa, function(params) … end, akcje, { label = "…", icon = "…" })` dodaje widok. Jego funkcja zwraca, co pokazać, zbudowane z poniższych elementów; z etykietą `label` widok jest w palecie. `icon` to nazwa jednej z ikon Brulionu, na przykład `folder`, `star`, `calendar-check`, `chart-column`, `list-checks`.

| Element | Co pokazuje |
|---|---|
| `b.ui.column({ … })` | elementy jeden pod drugim: zwykle element zewnętrzny |
| `b.ui.card({ … })` | elementy w jednej karcie |
| `b.ui.heading(tekst)` | nagłówek |
| `b.ui.text(tekst, przygaszony)` | akapit; z `true` jako drugim argumentem w spokojniejszym kolorze, z `{ muted = true, icon = "flame", tone = "success" }` także z ikoną przed nim i w kolorze (`"success"`, `"danger"`, `"accent"`) |
| `b.ui.link(strona, etykieta, wiersz)` | link do strony; z numerem wiersza (liczonym od 1) otwiera stronę w tym miejscu |
| `b.ui.button(etykieta, akcja, args)` | przycisk, który wywołuje jedną z akcji widoku |
| `b.ui.menu(pozycje, etykieta)` | przycisk „⋯” z menu (pozycje niżej) |
| `b.ui.row({ … })` | karta z kontrolkami obok siebie: pola wyboru, liczniki, teksty, linki, przyciski, menu |
| `b.ui.check(zaznaczone, akcja, args, { menu = …, label = … })` | pole wyboru zadania: `zaznaczone` to `true`, `false` albo `"skipped"` (przekreślone); `menu` otwiera się prawym przyciskiem albo przytrzymaniem. Tylko wewnątrz wiersza `row` |
| `b.ui.stepper({ value = 3, step = 1, unit = "kg", action = "ustaw", args = { … } })` | liczba z − i +, którą można też wpisać; akcja dostaje nową liczbę jako `args.value`. Tylko wewnątrz wiersza `row` |
| `b.ui.table(kolumny, wiersze)` | tabela: lista nagłówków i lista wierszy, każdy jako lista tekstów. Najlepiej do czterech kolumn |
| `b.ui.chart({ kind = "line", labels = { … }, series = { { name = "…", values = { … } } } })` | wykres, `"line"` (liniowy) albo `"bar"` (słupkowy), liczba na każdą etykietę |
| `b.ui.month({ month = "2026-11", days = { ["2026-11-03"] = { state = "done", action = "…", args = { … }, menu = { … } } } })` | miesiąc jako kalendarz, kwadrat na każdy dzień: `"done"` zielony, `"missed"` czerwony, `"skipped"` przekreślony, `"due"` z obwódką, `"future"` blady, `"rest"` zwykły; stuknięcie dnia z akcją `action` ją wywołuje, prawy przycisk albo przytrzymanie otwiera jego `menu`. Tygodnie zaczynają się od pierwszego dnia tygodnia z ustawień notatnika, dzisiejszy dzień ma obwódkę |
| `b.ui.empty(tytuł, tekst, ikona)` | co pokazać, gdy nie ma czego pokazać, z wybraną ikoną |
| `b.ui.open(etykieta, widok, params)` | otwiera inny widok rozszerzenia, z `params` dla jego funkcji |
| `b.ui.run(etykieta, polecenie)` | przycisk, który uruchamia jedno z poleceń rozszerzenia |
| `b.ui.input({ placeholder = "…", label = "…", action = "dodaj", submit = "Dodaj" })` | pole tekstowe z przyciskiem: Enter albo przycisk daje akcji wpisany tekst jako `args.value`, potem pole się czyści (`keep = true` go zostawia). Z `multiline = true` ma kilka wierszy: Ctrl+Enter albo przycisk |
| `b.ui.select({ options = { "A", { label = "B", value = "b" } }, value = "b", action = "wybierz" })` | przycisk z wybraną opcją; jego menu pokazuje wszystkie, a wybór daje akcji `value` opcji |
| `b.ui.slider({ value = 3, min = 1, max = 5, step = 1, unit = "", action = "ustaw" })` | liczba wybierana na pasku, pokazana obok; akcja dostaje ją jako `args.value`, gdy puścisz suwak (albo naciśniesz strzałkę) |
| `b.ui.date({ value = "2026-11-21", action = "przesun" })` | dzień na przycisku, który otwiera kalendarz; akcja dostaje wybrany dzień — albo `""` dla „Bez daty” |
| `b.ui.toggle(włączony, akcja, args, { label = "…" })` | przełącznik; stuknięcie daje akcji `args.value`, `true` albo `false` |
| `b.ui.progress({ value = 3, max = 4, label = "3 z 4", tone = "success" })` | pasek wypełniony do `value` z `max` |

Sześć ostatnich może stać w wierszu `row` albo w osobnej linii, którą wtedy wypełniają.

**Menu** to lista pozycji: `{ label = "…", action = "…", args = { … } }` wywołuje jedną z akcji widoku, `{ label = "…", page = "…", line = 5 }` otwiera stronę, `{ label = "…", journal = "2026-11-03" }` otwiera ten dzień dziennika.

Funkcja widoku musi zwrócić element (lista elementów pokaże się jak kolumna): widok, który nic nie zwraca, mówi o tym, zamiast zostać pusty. Element, który nie zawsze jest potrzebny, można zapisać jako `warunek and b.ui.text("…") or nil` — `nil` w kolumnie albo w wierszu jest pomijany, a komórka tabeli z `nil` jest pusta.

**Akcje** to funkcje z trzeciego argumentu, po nazwie. Przycisk, pole wyboru albo licznik wskazuje jedną z nich i niesie dla niej `args` — włóż tam to, czego akcja potrzebuje, na przykład `ref` i `text` bloku. Widok rysuje się od nowa po każdej akcji i po każdej zmianie w notatkach, więc jego funkcja powinna za każdym razem czytać notatnik i nigdy nie pytać ani nie zapisywać. Widok poniżej ma akcję, która zapisuje, więc jego strona potrzebuje `uprawnienia: [zapis]`.

```luau
b.view("pomysly", function(params)
    local wiersze = { b.ui.heading("Pomysły") }
    for _, blok in b.blocks({ tag = "pomysł" }) do
        table.insert(wiersze, b.ui.row({
            b.ui.text(blok.text),
            b.ui.link(blok.page),
            b.ui.button("Na dziś", "kopiuj", { text = blok.text, page = blok.page }),
        }))
    end
    if #wiersze == 1 then
        return b.ui.empty("Nie ma jeszcze pomysłów", "Oznacz wiersz tagiem pomysł.")
    end
    return b.ui.column(wiersze)
end, {
    kopiuj = function(args)
        b.appendToJournal(b.today(), `- {args.text} (z [[{args.page}]])`)
    end,
}, { label = "Pomysły", icon = "star" })
```

```luau
-- Miesiąc dziennika: dni z notatką są zielone, minione bez niej czerwone. Każde otwarte zadanie
-- z terminem w tym miesiącu ma menu, a odhaczenie mówi o tym z przyciskiem Cofnij.
b.view("miesiac", function(params)
    local dzis = b.today()
    local miesiac = params.month or dzis:sub(1, 7)
    local dni, zapisane, dzien = {}, 0, miesiac .. "-01"
    while dzien:sub(1, 7) == miesiac do
        local stan = if dzien > dzis then "future" elseif b.page(dzien) then "done" else "missed"
        zapisane += if stan == "done" then 1 else 0
        dni[dzien] = { state = stan, menu = { { label = "Otwórz dziennik", journal = dzien } } }
        dzien = b.addDays(dzien, 1)
    end
    local wiersze = {
        b.ui.heading(`{b.monthName(tonumber(miesiac:sub(6, 7)))} {miesiac:sub(1, 4)}`),
        b.ui.card({
            b.ui.text(`Dni z notatką: {zapisane}`, { icon = "book-open", tone = "accent" }),
            b.ui.month({ month = miesiac, days = dni }),
        }),
    }
    local ostatni = b.addDays(b.addMonths(miesiac .. "-01", 1), -1)
    for _, zadanie in b.query(`type:tasks status:open due:{miesiac}-01..{ostatni} sort:due`, 20).items do
        table.insert(wiersze, b.ui.row({
            b.ui.check(false, "ustaw", { ref = zadanie.ref, stan = "x" }, {
                label = zadanie.plain,
                menu = { { label = "Anuluj je", action = "ustaw", args = { ref = zadanie.ref, stan = "-" } } },
            }),
            b.ui.text(zadanie.plain),
            b.ui.text(zadanie.due, { muted = true, icon = "calendar" }),
            b.ui.menu({ { label = "Otwórz jego stronę", page = zadanie.page } }),
        }))
    end
    return b.ui.column(wiersze)
end, {
    ustaw = function(args)
        local ref = b.setTask(args.ref, args.stan)
        return {
            message = if args.stan == "x" then "Zrobione" else "Anulowane",
            tone = "success",
            button = { label = "Cofnij", action = "ustaw", args = { ref = ref, stan = " " } },
        }
    end,
}, { label = "Miesiąc dziennika", icon = "calendar" })
```

Gdy funkcja ma dużo do wyliczenia, może trzymać wynik w zmiennej razem z `b.revision()` i liczyć od nowa tylko wtedy, gdy ta liczba się zmieni — tak robią [[Nawyki]].

Widok poniżej przyjmuje to, co wpiszesz i wybierzesz: suwak, wybór, notatkę, dzień i przełącznik. Z `panel = "journal"` jest też sekcją panelu bocznego przy każdym dniu dziennika.

```luau
local nastroj = { poziom = 3, pogoda = "słońce", spacer = false }

b.view("nastroj", function(params)
    return b.ui.column({
        b.ui.heading(`Jaki jest {params.date or b.today()}?`),
        b.ui.slider({ value = nastroj.poziom, min = 1, max = 5, step = 1, label = "Nastrój", action = "poziom" }),
        b.ui.select({
            options = { { label = "Słońce", value = "słońce" }, { label = "Deszcz", value = "deszcz" }, "Śnieg" },
            value = nastroj.pogoda, label = "Pogoda", action = "pogoda",
        }),
        b.ui.input({ placeholder = "Co na to wpłynęło?", action = "notatka", submit = "Zapisz" }),
        b.ui.row({
            b.ui.date({ value = params.date or b.today(), label = "Dzień", action = "dzien" }),
            b.ui.toggle(nastroj.spacer, "spacer", nil, { label = "Spacer" }),
        }),
        b.ui.progress({ value = nastroj.poziom, max = 5, label = `{nastroj.poziom} z 5`, tone = "accent" }),
    })
end, {
    poziom = function(args) nastroj.poziom = args.value end,
    pogoda = function(args) nastroj.pogoda = args.value end,
    spacer = function(args) nastroj.spacer = args.value end,
    dzien = function(args) nastroj.dzien = args.value end,
    notatka = function(args)
        local spacer = if nastroj.spacer then ", spacer" else ""
        b.appendToJournal(nastroj.dzien or b.today(), `- Nastrój {nastroj.poziom}/5, {nastroj.pogoda}{spacer}: {args.value}`)
        return "Zapisane"
    end,
}, { label = "Nastrój", icon = "heart", panel = "journal" })
```

## Widoki na stronie i w panelu bocznym
Widok można narysować **na stronie**: napisz blok kodu `view`, którego pierwszy wiersz go wskazuje — identyfikator rozszerzenia, ukośnik, nazwa widoku — a kolejne wiersze to jego parametry, `nazwa: wartość`. Menu ukośnika podsuwa każdy widok z etykietą jako **Widok: …** i samo pisze taki blok. Widok działa tam tak samo jak osobno: jego przyciski, pola wyboru i pola tekstowe działają, a on sam nadąża za notatkami. Dostaje też `page`, `path` i `date` strony wśród parametrów. Kliknięcie obok kontrolek edytuje tekst bloku.

````markdown
```view
docs/nastroj
```
````

`panel = true` w ostatnim argumencie widoku robi z niego sekcję **panelu bocznego** przy każdej stronie i każdym dniu; `panel = "page"` tylko przy stronach, `panel = "journal"` tylko przy dniach dziennika. Na telefonie panel boczny to szuflada wysuwana z prawej krawędzi.

## Makra
`b.macro(nazwa, function(ctx) … end, { description = "…", args = { "…" }, example = "…" })` daje notatkom **makro**: wpisanie `{{ext:<rozszerzenie>/<nazwa> argumenty}}` w dowolnym miejscu notatki pokazuje w tekście to, co zwraca funkcja, zawsze aktualne. Rozszerzenie wskazuje jego identyfikator (tytuł strony małymi literami, bez polskich znaków, z `-` zamiast spacji: „Licznik słów” to `licznik-slow`). Wpisanie `{{` w notatce podsuwa makra z ich opisami.

- `ctx.args` to lista argumentów rozdzielonych przecinkami: `{{ext:a/b Przysiad, 30}}` daje `{ "Przysiad", "30" }`; `ctx.text` to one tak, jak je zapisano.
- `ctx.page` i `ctx.path` wskazują notatkę z makrem, `ctx.date` jej dzień, gdy to dzień dziennika.
- Zwróć tekst, liczbę, `b.ui.text` (przygaszony albo w kolorze), `b.ui.link`, `b.ui.heading` (pogrubiony) — albo ich listę. Zawsze pokazuje się jako tekst, nigdy jako HTML.
- Makro liczy się od nowa samo, gdy zmieni się notatka, którą czytało. Nie może pytać ani zapisywać i ma ćwierć sekundy.
- Makro rozszerzenia, które jeszcze nie działało na tym urządzeniu, pokazuje kłódkę: kliknięcie pyta, czy je uruchomić.

```luau
b.macro("slowa", function(ctx)
    local nazwa = ctx.args[1] or ctx.page
    local n = 0
    for _ in (b.read(nazwa) or ""):gmatch("%S+") do
        n += 1
    end
    return { b.ui.heading(tostring(n)), " słów w ", b.ui.link(nazwa) }
end, { description = "Ile słów ma strona", args = { "strona" }, example = "Dom" })
```

## Zmienne szablonów
`b.variable(nazwa, function(ctx) … end, { description = "…" })` daje [[Dziennik, kalendarz i szablony|szablonom]] zmienną: `{{<rozszerzenie>/<nazwa>}}` w szablonie zastępuje to, co zwraca funkcja, gdy szablon zostaje użyty — Markdown, dowolnie wiele wierszy. `ctx.date` to dzień, dla którego szablon jest wypełniany. W odróżnieniu od makra wartość zostaje wpisana do notatki raz.

```luau
b.variable("agenda", function(ctx)
    local dzien = ctx.date or b.today()
    local wiersze = {}
    for _, zadanie in b.query("type:tasks status:open sort:due", 50).items do
        if zadanie.due and zadanie.due <= dzien then
            table.insert(wiersze, `- [ ] {zadanie.plain} ([[{zadanie.page}]])`)
        end
    end
    return if #wiersze > 0 then table.concat(wiersze, "\n") else "- Nic na dziś"
end, { description = "Otwarte zadania z terminem do dnia notatki" })
```

## Reakcje
`b.on("save", function(e) … end)` działa zaraz po tym, jak to urządzenie zapisało stronę: `e.page` to jej nazwa. `b.on("change", function(e) … end)` działa po zmianach w notatkach, skądkolwiek przyszły — z Twojego pisania, synchronizacji, innego programu — gdy zmiany ucichną: `e.pages` to lista nazw zmienionych stron (jest w niej także strona usunięta). To, co rozszerzenia zapisują same, nie wraca jako zmiana. Reakcja nie może zadać pytania, a taka, która zawiedzie trzy razy z rzędu, zostaje wyłączona do ponownego wczytania rozszerzenia.

## Pytania

| Funkcja | Co dostajesz |
|---|---|
| `b.ask(pytanie)` | wiersz tekstu, który wpisałeś; pusty tekst, gdy zamknąłeś okno |
| `b.choose(pytanie, { "A", "B" })` | opcję, którą wybrałeś; `nil`, gdy zamknąłeś okno |
| `b.form(tytuł, pola)` | liczby, każda z − i +, i wiersze tekstu: `{ klucz = liczba albo tekst }`; `nil`, gdy zamknąłeś okno |

Pole liczbowe formularza to `{ key = "powt", label = "Powtórzenia", value = 8, step = 1, unit = "" }`, pole tekstowe `{ key = "nazwa", label = "Nazwa", kind = "text", value = "", placeholder = "np. Medytacja" }` — `placeholder` to szary przykład w pustym polu. Pytania działają w poleceniach i akcjach; nie działają w widokach, w reakcjach ani podczas wczytywania strony.

```luau
b.command("spacer", function(ctx)
    local odpowiedz = b.form("Spacer", {
        { key = "km", label = "Dystans", value = 3, step = 0.5, unit = "km" },
        { key = "min", label = "Czas", value = 40, step = 5, unit = "min" },
        { key = "gdzie", label = "Gdzie", kind = "text", value = "", placeholder = "np. w parku" },
    })
    if odpowiedz == nil then
        return nil
    end
    local km, min = b.formatNumber(odpowiedz.km), b.formatNumber(odpowiedz.min)
    local gdzie = if odpowiedz.gdzie ~= "" then ` ({odpowiedz.gdzie})` else ""
    b.appendToJournal(b.today(), `- Spacer: {km} km w {min} min{gdzie}`)
    return "Spacer jest w dzisiejszym dzienniku"
end, { label = "Zapisz spacer" })
```

## AI
`b.ai("pytanie")` pyta model wybrany w **Ustawieniach → Rozszerzenia → AI** i zwraca jego odpowiedź jako tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort`, `images` (obrazki z notatnika dla modelu, który widzi: `b.ai({ prompt = "Co tu jest napisane?", images = { "paragon.jpg" } })`), `messages` dla rozmowy i `tools`, które model może wywołać. Strona potrzebuje uprawnienia `ai`. Więcej na stronie [[AI]].

## Luau w skrócie
- Teksty łączy `..`, można je też pisać w odwrotnych apostrofach z klamrami: `` `{n} stron` ``. Lista to `{ "a", "b" }` i liczy się od 1; `#lista` to jej długość; `for i, element in lista do … end` przechodzi po niej.
- `local` tworzy zmienną, `function … end` funkcję, `--` zaczyna komentarz, `~=` znaczy „różne”, `nil` znaczy „nic”.
- Nazwy zmiennych i funkcji pisz bez polskich znaków (`slowa`, nie `słowa`); w tekstach w cudzysłowie polskie znaki są w porządku.
- Tekst liczy bajty, nie litery: `#"żółw"` to 7. Do liter użyj `utf8.len(tekst)`, do porównywania `b.fold`, do sortowania `b.sorted`.
- Liczbę pisze się z kropką: `82.5`. Pokazuj ją przez `b.formatNumber`.
- `b.now()` to bieżąca data z godziną, `os.date("%H:%M")` sama godzina.
- Nie ma plików, sieci ani innych programów: jest tylko `b`.

## Limity
Jedno wywołanie może trwać 2 sekundy (czas, w którym odpowiadasz na pytanie, się nie liczy) — makro ćwierć sekundy, zmienna szablonu sekundę —, a wszystkie rozszerzenia razem mają 64 MB pamięci; po przekroczeniu wywołanie zostaje zatrzymane, a reszta działa dalej. Widok pokazuje do 2000 wierszy, wiersz `row` do 24 kontrolek; `b.blocks` daje do 20 000 bloków; jedno pytanie do modelu ma do 200 000 znaków. Makro pokazuje najwyżej 400 znaków; makra jednej strony mają razem 2 sekundy.

## Błędy
Komunikaty silnika są po angielsku:

| Komunikat | Co znaczy |
|---|---|
| `permission denied: write` | strona nie ma `uprawnienia: [zapis]` we właściwościach |
| `permission denied: ai` | to samo dla `ai` |
| `stale: the block changed since it was read` | wiersz nie jest już tym, co kod przeczytał: przeczytaj go jeszcze raz |
| `not a block reference` | `b.replaceBlock` (albo `b.insertBlock`, `b.removeBlock`, `b.setTask`, `b.setProperty`) dostało coś, co nie jest `ref` bloku: podaj `ref` takie, jakie dało `b.blocks`, `b.query`, `b.search` albo `ctx.block` |
| `the task status must be one character` | `b.setTask` albo `b.replaceBlock` dostało `"zrobione"`: napisz `"x"` |
| `cannot set the property` | nazwa właściwości może mieć tylko litery, cyfry, `-` i `_` |
| `must be one line` | wiersz bloku albo wartość właściwości ma w sobie podział wiersza |
| `attempt to index nil` | czegoś nie było: strony, która nie istnieje, zadania w bloku, który nim nie jest, `ctx.block` poza blokiem |
| `argument 1 must be a string` | funkcja dostała coś innego, niż przyjmuje; liczba mówi, który to argument |
| `invalid date` | data nie jest zapisana jak `2026-11-13` albo nie ma takiego dnia |
| `the query needs a page, a tag or a prop` | `b.blocks` nie dostało ani strony, ani tagu, ani właściwości |
| `argument 1 must be a table` | `b.pages("projekt")`: napisz `b.pages({ tag = "projekt" })` |
| `the view "…" returned nothing` | funkcja widoku skończyła się, nie zwracając tego, co pokazać |
| `Widok nie może czekać na pytanie ani na AI` | funkcja widoku o coś zapytała: pytaj w poleceniu albo w akcji |
| `a hook cannot ask a question` | zapytała reakcja |
| `cannot wait for an answer here` | pytanie albo `b.ai` wewnątrz funkcji, którą wywołuje `table.sort` albo `string.gsub` |
| `an extension cannot ask a question while it loads` | pytanie poza funkcją: pytaj wewnątrz polecenia |
| `timeout: the extension ran too long` | jedno wywołanie trwało dłużej niż 2 sekundy |
| `a macro or a template variable cannot change notes` | makro albo zmienna szablonu zapisywały: zapisuj w poleceniu albo akcji |
| `a macro returns a text, a number, b.ui.text, b.ui.link or a list of them` | makro zwróciło coś, czego nie da się pokazać |

`pcall` łapie błąd, więc kod może działać dalej: `local ok, komunikat = pcall(b.write, nazwa, tekst)`. `error("…", 0)` zatrzymuje polecenie z Twoim własnym komunikatem.
