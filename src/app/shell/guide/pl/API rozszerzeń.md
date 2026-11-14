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
| `b.page(nazwa)` | `{ name, tags, props, date }`: własne tagi i właściwości strony oraz jej dzień, gdy to strona dziennika; `nil`, gdy takiej strony nie ma |
| `b.read(nazwa)` | cały tekst strony albo `nil` |
| `b.blocks({ page = nazwa })` | bloki strony od góry, każdy z blokami pod nim |
| `b.blocks({ tag = "pomysł" })` | każdy blok, który sam ma ten tag; z `journal = true`, `from = "2026-11-01"` albo `to = "2026-11-30"` tylko bloki dziennika |
| `b.search(tekst, limit)` | to, co znajduje widok wyszukiwania: jedno trafienie na stronę, najlepsze najpierw (20 bez `limit`, najwyżej 200) |
| `b.query(tekst, limit)` | zapytanie w języku opisanym na stronie [[Zapytania]]: `{ total, items }` (50 pozycji bez `limit`, najwyżej 1000) |
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
| `task` | dla zadania znak w jego polu wyboru: `" "` otwarte, `"x"` zrobione, `"/"` w toku, `"-"` anulowane, `">"` odłożone; `nil` dla bloku, który nie jest zadaniem |
| `page`, `date` | strona, na której leży, i dzień, gdy to strona dziennika |
| `tags` | własne tagi bloku, małymi literami, bez krzyżyka |
| `props` | jego właściwości: `{ autor = "Bolesław Prus" }` dla wiersza `autor:: Bolesław Prus` pod nim |
| `children` | bloki pod nim (z `b.blocks` i `ctx.block`) |
| `ref` | miejsce bloku. Podaj je funkcji `b.replaceBlock`, żeby zmienić blok; przestaje działać, gdy wiersz się zmieni |
| `line`, `excerpt` | z `b.search`: wiersz pierwszego trafienia, liczony od 1, i tekst wokół niego |
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
| `b.write(nazwa, tekst)` | tworzy stronę albo zastępuje **cały** jej tekst; `"Spotkania/Budżet"` utworzy też folder |
| `b.replaceBlock(ref, tekst, task)` | daje pierwszemu wierszowi bloku nowy tekst; z `task` (`"x"`, `" "`, `"/"`, `"-"`) ustawia też pole wyboru tak, jak kliknięcie (zrobione zadanie dostaje datę). Zwraca nowe `ref` bloku |

`b.replaceBlock` zostawia punktor, wcięcie i daty na końcu wiersza. **Odmawia**, gdy wiersz zmienił się, odkąd kod go przeczytał — przyszła synchronizacja, coś tam dopisałeś — więc nigdy nie nadpisze zmiany, której kod nie widział. Błąd ma w sobie słowo `stale`; przeczytaj blok jeszcze raz.

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

## Pomocnicze

| Funkcja | Co dostajesz |
|---|---|
| `b.today()` | dzisiejszą datę: `2026-11-13` |
| `b.daysBetween(a, b)` | ile dni mija od daty `a` do daty `b` |
| `b.sorted(lista)` | teksty w kolejności alfabetycznej, z polskimi literami na swoich miejscach i `Rozdział 2` przed `Rozdział 10` |
| `b.fold(tekst)` | tekst małymi literami i bez polskich znaków (`Żółw` daje `zolw`): do porównywania i szukania |
| `b.formatNumber(n, miejsca)` | liczbę jako tekst, w polskim notatniku z przecinkiem: `82,5` |
| `b.lang()`, `b.uiLang()` | język notatek i język interfejsu: `"pl"` albo `"en"` |
| `b.log(tekst)`, `print(…)` | wiersz w konsoli Luau |
| `b.timer(sekundy, etykieta)` | uruchamia timer przerwy (ten ze strony [[Siłownia]]): powiadomienie, gdy czas minie |
| `b.data(nazwa)` | dane dołączone do wbudowanego rozszerzenia (lista ćwiczeń Siłowni, opis pisania rozszerzeń dla Asystenta AI); we własnym `nil` |

## Polecenia
`b.command(nazwa, function(ctx) … end, { label = "…", block = true })` dodaje do palety polecenie pod etykietą `label`.

- `ctx.page` to nazwa strony na ekranie; pusty tekst, gdy żadnej nie ma.
- Z `block = true` polecenie działa na bloku pod kursorem, `ctx.block`, a paleta pokazuje je tylko w notatce. Sprawdź, czy `ctx.block` jest.
- Tekst, który funkcja zwróci, pojawia się jako komunikat. Można nie zwracać niczego.

## Widoki
`b.view(nazwa, function(params) … end, akcje, { label = "…", icon = "…" })` dodaje widok. Jego funkcja zwraca, co pokazać, zbudowane z poniższych elementów; z etykietą `label` widok jest w palecie. `icon` to nazwa jednej z ikon Brulionu, na przykład `folder`, `star`, `calendar-check`, `chart-column`, `list-checks`.

| Element | Co pokazuje |
|---|---|
| `b.ui.column({ … })` | elementy jeden pod drugim: zwykle element zewnętrzny |
| `b.ui.heading(tekst)` | nagłówek |
| `b.ui.text(tekst, przygaszony)` | akapit; z `true` jako drugim argumentem w spokojniejszym kolorze |
| `b.ui.link(strona, etykieta)` | link do strony |
| `b.ui.button(etykieta, akcja, args)` | przycisk, który wywołuje jedną z akcji widoku |
| `b.ui.row({ … })` | karta z kontrolkami obok siebie: pola wyboru, liczniki, teksty, linki, przyciski |
| `b.ui.check(zaznaczone, akcja, args)` | pole wyboru zadania. Tylko wewnątrz wiersza `row` |
| `b.ui.stepper({ value = 3, step = 1, unit = "kg", action = "ustaw", args = { … } })` | liczba z − i +, którą można też wpisać; akcja dostaje nową liczbę jako `args.value`. Tylko wewnątrz wiersza `row` |
| `b.ui.table(kolumny, wiersze)` | tabela: lista nagłówków i lista wierszy, każdy jako lista tekstów. Najlepiej do czterech kolumn |
| `b.ui.chart({ kind = "line", labels = { … }, series = { { name = "…", values = { … } } } })` | wykres, `"line"` (liniowy) albo `"bar"` (słupkowy), liczba na każdą etykietę |
| `b.ui.empty(tytuł, tekst)` | co pokazać, gdy nie ma czego pokazać |
| `b.ui.open(etykieta, widok, params)` | otwiera inny widok rozszerzenia, z `params` dla jego funkcji |
| `b.ui.run(etykieta, polecenie)` | przycisk, który uruchamia jedno z poleceń rozszerzenia |

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

## Reakcje
`b.on("save", function(e) … end)` działa zaraz po tym, jak to urządzenie zapisało stronę: `e.page` to jej nazwa. `b.on("change", function(e) … end)` działa po zmianach w notatkach, skądkolwiek przyszły — z Twojego pisania, synchronizacji, innego programu — gdy zmiany ucichną: `e.pages` to lista nazw zmienionych stron (jest w niej także strona usunięta). To, co rozszerzenia zapisują same, nie wraca jako zmiana. Reakcja nie może zadać pytania, a taka, która zawiedzie trzy razy z rzędu, zostaje wyłączona do ponownego wczytania rozszerzenia.

## Pytania

| Funkcja | Co dostajesz |
|---|---|
| `b.ask(pytanie)` | wiersz tekstu, który wpisałeś; pusty tekst, gdy zamknąłeś okno |
| `b.choose(pytanie, { "A", "B" })` | opcję, którą wybrałeś; `nil`, gdy zamknąłeś okno |
| `b.form(tytuł, pola)` | liczby, każda z − i +: `{ klucz = liczba }`; `nil`, gdy zamknąłeś okno |

Pole formularza to `{ key = "powt", label = "Powtórzenia", value = 8, step = 1, unit = "" }`. Pytania działają w poleceniach i akcjach; nie działają w widokach, w reakcjach ani podczas wczytywania strony.

```luau
b.command("spacer", function(ctx)
    local odpowiedz = b.form("Spacer", {
        { key = "km", label = "Dystans", value = 3, step = 0.5, unit = "km" },
        { key = "min", label = "Czas", value = 40, step = 5, unit = "min" },
    })
    if odpowiedz == nil then
        return nil
    end
    local km, min = b.formatNumber(odpowiedz.km), b.formatNumber(odpowiedz.min)
    b.appendToJournal(b.today(), `- Spacer: {km} km w {min} min`)
    return "Spacer jest w dzisiejszym dzienniku"
end, { label = "Zapisz spacer" })
```

## AI
`b.ai("pytanie")` pyta model wybrany w **Ustawieniach → Rozszerzenia → AI** i zwraca jego odpowiedź jako tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort`, `messages` dla rozmowy i `tools`, które model może wywołać. Strona potrzebuje uprawnienia `ai`. Więcej na stronie [[AI]].

## Luau w skrócie
- Teksty łączy `..`, można je też pisać w odwrotnych apostrofach z klamrami: `` `{n} stron` ``. Lista to `{ "a", "b" }` i liczy się od 1; `#lista` to jej długość; `for i, element in lista do … end` przechodzi po niej.
- `local` tworzy zmienną, `function … end` funkcję, `--` zaczyna komentarz, `~=` znaczy „różne”, `nil` znaczy „nic”.
- Nazwy zmiennych i funkcji pisz bez polskich znaków (`slowa`, nie `słowa`); w tekstach w cudzysłowie polskie znaki są w porządku.
- Tekst liczy bajty, nie litery: `#"żółw"` to 7. Do liter użyj `utf8.len(tekst)`, do porównywania `b.fold`, do sortowania `b.sorted`.
- Liczbę pisze się z kropką: `82.5`. Pokazuj ją przez `b.formatNumber`.
- `os.date("%H:%M")` to bieżąca godzina.
- Nie ma plików, sieci ani innych programów: jest tylko `b`.

## Limity
Jedno wywołanie może trwać 2 sekundy (czas, w którym odpowiadasz na pytanie, się nie liczy), a wszystkie rozszerzenia razem mają 64 MB pamięci; po przekroczeniu wywołanie zostaje zatrzymane, a reszta działa dalej. Widok pokazuje do 2000 wierszy, wiersz `row` do 24 kontrolek; `b.blocks` daje do 20 000 bloków; jedno pytanie do modelu ma do 200 000 znaków.

## Błędy
Komunikaty silnika są po angielsku:

| Komunikat | Co znaczy |
|---|---|
| `permission denied: write` | strona nie ma `uprawnienia: [zapis]` we właściwościach |
| `permission denied: ai` | to samo dla `ai` |
| `stale: the block changed since it was read` | wiersz nie jest już tym, co kod przeczytał: przeczytaj go jeszcze raz |
| `not a block reference` | `b.replaceBlock` dostało coś, co nie jest `ref` bloku: podaj `ref` takie, jakie dało `b.blocks`, `b.query`, `b.search` albo `ctx.block` |
| `attempt to index nil` | czegoś nie było: strony, która nie istnieje, zadania w bloku, który nim nie jest, `ctx.block` poza blokiem |
| `argument 1 must be a string` | funkcja dostała coś innego, niż przyjmuje; liczba mówi, który to argument |
| `invalid date` | data nie jest zapisana jak `2026-11-13` albo nie ma takiego dnia |
| `the query needs a page or a tag` | `b.blocks` nie dostało ani strony, ani tagu |
| `argument 1 must be a table` | `b.pages("projekt")`: napisz `b.pages({ tag = "projekt" })` |
| `the view "…" returned nothing` | funkcja widoku skończyła się, nie zwracając tego, co pokazać |
| `Widok nie może czekać na pytanie ani na AI` | funkcja widoku o coś zapytała: pytaj w poleceniu albo w akcji |
| `a hook cannot ask a question` | zapytała reakcja |
| `cannot wait for an answer here` | pytanie albo `b.ai` wewnątrz funkcji, którą wywołuje `table.sort` albo `string.gsub` |
| `an extension cannot ask a question while it loads` | pytanie poza funkcją: pytaj wewnątrz polecenia |
| `timeout: the extension ran too long` | jedno wywołanie trwało dłużej niż 2 sekundy |

`pcall` łapie błąd, więc kod może działać dalej: `local ok, komunikat = pcall(b.write, nazwa, tekst)`. `error("…", 0)` zatrzymuje polecenie z Twoim własnym komunikatem.
