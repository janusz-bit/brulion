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
| `b.pages({ paths = true })` | te same strony jako ich pliki w notatniku (`Projekty/Dom.md`): dwie strony „Dom” w różnych folderach to na liście nazw jedna nazwa, tu dwie ścieżki. Każda funkcja, która przyjmuje stronę, przyjmuje też taką ścieżkę |
| `b.page(nazwa)` | `{ name, path, tags, props, date }`: plik strony w notatniku (`Projekty/Dom.md`), jej własne tagi i właściwości oraz jej dzień, gdy to strona dziennika; `nil`, gdy takiej strony nie ma |
| `b.read(nazwa)` | cały tekst strony albo `nil` |
| `b.blocks({ page = nazwa })` | bloki strony od góry, każdy z blokami pod nim |
| `b.blocks({ tag = "pomysł" })` | każdy blok, który sam ma ten tag; z `journal = true`, `from = "2026-11-01"` albo `to = "2026-11-30"` tylko bloki dziennika |
| `b.blocks({ prop = "nawyk" })` | każdy blok, który ma pod sobą tę właściwość (`nawyk:: codziennie`); z `tag` bloki, które mają jedno i drugie, ze stroną `page` tylko bloki tej strony (także te głębiej) |
| `b.search(tekst, limit)` | to, co znajduje widok wyszukiwania: jedno trafienie na stronę, najlepsze najpierw (20 bez `limit`, najwyżej 200) |
| `b.query(tekst, limit, offset)` | zapytanie w języku opisanym na stronie [[Zapytania]]: `{ total, items }` (50 pozycji bez `limit`, najwyżej 1000; `offset` pomija tyle pierwszych, więc można przejść po dowolnej ich liczbie — `total` liczy wszystkie). Tygodnie zaczynają się od pierwszego dnia tygodnia z ustawień notatnika |
| `b.lines(nazwa)` | wiersze strony dokładnie tak, jak są zapisane, każdy jako `{ text, ref }` — także te, które nie są osobnym blokiem (drugi wiersz akapitu, komentarz): ich `ref` przyjmują `b.replaceLine`, `b.insertLine` i `b.removeLine`; `nil`, gdy takiej strony nie ma |
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
| `b.setProperties(ref, { { klucz, wartość }, … })` | kilka właściwości bloku jednym zapisem (nowe w tej kolejności). Zwraca `ref` bloku |
| `b.replaceLine(ref, tekst)` | cały wiersz zamienia na `tekst`, dokładnie taki, jaki podano — razem z wcięciem. Zwraca nowy `ref` wiersza |
| `b.insertLine(ref, tekst, gdzie)` | wstawia wiersze `tekst` tak, jak są zapisane, za tym wierszem (`"before"`: przed nim). Zwraca `ref` pierwszego nowego wiersza |
| `b.removeLine(ref)` | usuwa ten jeden wiersz |

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
| `b.toUtc(dataICzas)`, `b.fromUtc(tekst)` | miejscową datę i godzinę jako chwilę w UTC, jaką zapisują inne aplikacje (`2026-11-20 18:30` daje w listopadzie w Polsce `2026-11-20T17:30:00.000Z`), i z powrotem; `b.fromUtc` daje `nil` dla czegokolwiek, co nie jest taką chwilą |
| `b.lang()`, `b.uiLang()` | język notatek i język interfejsu: `"pl"` albo `"en"` |
| `b.log(tekst)`, `print(…)` | wiersz w konsoli Luau |
| `b.status(tekst)` | mówi, co robi teraz polecenie czekające na odpowiedź („Czytam „Dom”…”); czat z AI pokazuje to pod kropkami AI, `b.status("")` to czyści |
| `b.timer(sekundy, { label = "Herbata", message = "Herbata gotowa." })` | uruchamia własny timer rozszerzenia (niżej, Timery) |
| `b.stopTimer()` | zatrzymuje go |
| `b.data(nazwa)`, `b.data(nazwa, klucz)` | dane dołączone do wbudowanego rozszerzenia (katalog ćwiczeń Siłowni, opis pisania rozszerzeń dla Asystenta AI); z `klucz` jeden ich wpis (opis wykonania jednego ćwiczenia Siłowni: `b.data("instructions", "back-squat")`); we własnym `nil` |

## Polecenia
`b.command(nazwa, function(ctx) … end, { label = "…", block = true })` dodaje do palety polecenie pod etykietą `label`.

- `ctx.page` to nazwa strony na ekranie; pusty tekst, gdy żadnej nie ma.
- Z `block = true` polecenie działa na bloku pod kursorem, `ctx.block`, a paleta pokazuje je tylko w notatce. Sprawdź, czy `ctx.block` jest.
- Z `palette = false` paleta go nie pokazuje: uruchamia je przycisk widoku (`b.ui.run`) albo sama aplikacja — tak czat z AI uruchamia polecenie `chat` asystenta AI.
- `ctx.folder`, `ctx.path` i `ctx.shared` przychodzą z elementów menu drzewa folderów i okien udostępniania (niżej, Aplikacja).
- `ctx.selection` to to, co zaznaczyłeś w notatce przed wybraniem polecenia: tekst zaznaczony w edytowanym bloku (albo w źródle Markdown) albo zaznaczone bloki jako Markdown — to samo, na czym pracuje AI w edytorze; pusty tekst, gdy nic nie jest zaznaczone.
- Z `key = "Ctrl+Alt+W"` polecenie proponuje skrót klawiszowy; dostaje go tylko wtedy, gdy nie ma go żadne polecenie samego Brulionu. Każde polecenie rozszerzenia jest w **Ustawieniach → Skróty klawiszowe**, gdzie możesz mu nadać dowolny skrót.
- Z `chat = true` jest **rozmówcą** w czacie z AI: czat pozwala wybrać je jako tego, kto odpowiada w rozmowie. Dostaje `ctx.messages` (rozmowę, na końcu wiadomość użytkownika; obrazki jako `images`) i `ctx.omitted` (ile pierwszych wiadomości nie zmieściło się w modelu), a to, co zwraca, jest jego wiadomością w rozmowie. Więcej na stronie [[AI]].
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
| `b.ui.button(etykieta, akcja, args, opcje)` | przycisk, który wywołuje jedną z akcji widoku; `opcje` mogą dać mu skrót klawiszowy (`key = "space"`, `"1"`, `"u"`), uczynić go głównym (`primary = true`), zabarwić (`tone = "success"`, `"danger"`, `"warning"`, `"info"`), dodać drugi wiersz pod etykietą (`hint = "4 dni"`) albo ikonę `icon` |
| `b.ui.markdown(tekst, opcje)` | Markdown tak, jak pokazuje go notatka — pogrubienia, linki otwierające strony, zakreślenia, listy; `{ size = "large" }` go powiększa, `{ muted = true }` przygasza |
| `b.ui.stats({ { label = "Do powtórki", value = 12, tone = "accent" }, … })` | liczby obok siebie, każda z podpisem pod nią |
| `b.ui.divider()` | cienka kreska w poprzek |
| `b.ui.menu(pozycje, etykieta)` | przycisk „⋯” z menu (pozycje niżej) |
| `b.ui.row({ … })` | karta z kontrolkami obok siebie: pola wyboru, liczniki, teksty, linki, przyciski, menu |
| `b.ui.check(zaznaczone, akcja, args, { menu = …, label = … })` | pole wyboru zadania: `zaznaczone` to `true`, `false` albo `"skipped"` (przekreślone); `menu` otwiera się prawym przyciskiem albo przytrzymaniem. Tylko wewnątrz wiersza `row` |
| `b.ui.stepper({ value = 3, step = 1, unit = "kg", action = "ustaw", args = { … } })` | liczba z − i +, którą można też wpisać; akcja dostaje nową liczbę jako `args.value`. Tylko wewnątrz wiersza `row` |
| `b.ui.table(kolumny, wiersze)` | tabela: lista nagłówków i lista wierszy, każdy jako lista tekstów. Najlepiej do czterech kolumn |
| `b.ui.chart({ kind = "line", labels = { … }, series = { { name = "…", values = { … } } } })` | wykres, `"line"` (liniowy) albo `"bar"` (słupkowy), liczba na każdą etykietę |
| `b.ui.month({ month = "2026-11", days = { ["2026-11-03"] = { state = "done", action = "…", args = { … }, menu = { … } } } })` | miesiąc jako kalendarz, kwadrat na każdy dzień: `"done"` zielony, `"missed"` czerwony, `"skipped"` przekreślony, `"due"` z obwódką, `"future"` blady, `"rest"` zwykły; stuknięcie dnia z akcją `action` ją wywołuje, prawy przycisk albo przytrzymanie otwiera jego `menu`. Tygodnie zaczynają się od pierwszego dnia tygodnia z ustawień notatnika, dzisiejszy dzień ma obwódkę |
| `b.ui.heatmap({ columns = { "3.11", "10.11" }, rows = { { label = "Klatka", values = { 4200, 3900 }, texts = { … }, note = "3900 kg" } } })` | siatka kwadratów, wiersz na rzecz i kolumna na okres (objętość Siłowni na mięsień i tydzień), każdy kwadrat tym mocniejszy, im większa jego wartość wobec największej w wierszu (`scale = "all"`: w całej siatce); `nil` albo 0 to pusty kwadrat. `texts` to to, co kwadrat mówi po wskazaniu albo stuknięciu, `note` krótki tekst na końcu wiersza |
| `b.ui.pictures({ "assets/kot.jpg", { src = "https://example.com/pies.jpg", alt = "Pies na łące", caption = "Burek" } }, { label = "Zwierzęta" })` | obrazy obok siebie (do ośmiu): obraz z notatnika po ścieżce, tak jak podaje ją `b.attachments()`, albo z internetu po adresie. Te z internetu pokazują się jak w notatkach — z pamięci podręcznej urządzenia, pobrane po stuknięciu **Pokaż obrazy** albo przy włączonym „Pokazuj obrazy z internetu bez pytania”. `alt` opisuje obraz dla czytnika ekranu, `caption` idzie pod nim |
| `b.ui.empty(tytuł, tekst, ikona)` | co pokazać, gdy nie ma czego pokazać, z wybraną ikoną |
| `b.ui.open(etykieta, widok, params, opcje)` | otwiera inny widok rozszerzenia, z `params` dla jego funkcji; z `opcje` jak u przycisku (`{ primary = true, icon = "play" }`) wygląda jak przycisk |
| `b.ui.run(etykieta, polecenie)` | przycisk, który uruchamia jedno z poleceń rozszerzenia |
| `b.ui.input({ placeholder = "…", label = "…", action = "dodaj", submit = "Dodaj" })` | pole tekstowe z przyciskiem: Enter albo przycisk daje akcji wpisany tekst jako `args.value`, potem pole się czyści (`keep = true` go zostawia). Z `multiline = true` ma kilka wierszy: Ctrl+Enter albo przycisk |
| `b.ui.select({ options = { "A", { label = "B", value = "b" } }, value = "b", action = "wybierz" })` | przycisk z wybraną opcją; jego menu pokazuje wszystkie, a wybór daje akcji `value` opcji |
| `b.ui.slider({ value = 3, min = 1, max = 5, step = 1, unit = "", action = "ustaw" })` | liczba wybierana na pasku, pokazana obok; akcja dostaje ją jako `args.value`, gdy puścisz suwak (albo naciśniesz strzałkę) |
| `b.ui.date({ value = "2026-11-21", action = "przesun" })` | dzień na przycisku, który otwiera kalendarz; akcja dostaje wybrany dzień — albo `""` dla „Bez daty” |
| `b.ui.toggle(włączony, akcja, args, { label = "…" })` | przełącznik; stuknięcie daje akcji `args.value`, `true` albo `false` |
| `b.ui.progress(wartość, maks, etykieta)` albo `b.ui.progress({ value = 3, max = 4, label = "3 z 4", tone = "success" })` | pasek postępu wypełniony do `value` z `max` (1, jeśli go pominiesz), z podpisem nad nim; `tone` (`"success"`, `"danger"`, `"warning"`) barwi podpis i pasek |

Sześć ostatnich może stać w wierszu `row` albo w osobnej linii, którą wtedy wypełniają.

**Menu** to lista pozycji: `{ label = "…", action = "…", args = { … } }` wywołuje jedną z akcji widoku, `{ label = "…", page = "…", line = 5 }` otwiera stronę, `{ label = "…", view = "…", params = { … } }` otwiera inny widok rozszerzenia, `{ label = "…", journal = "2026-11-03" }` otwiera ten dzień dziennika.

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

`panel = true` w ostatnim argumencie widoku robi z niego sekcję **panelu bocznego** przy każdej stronie i każdym dniu; `panel = "page"` tylko przy stronach, `panel = "journal"` tylko przy dniach dziennika. Na telefonie panel boczny to szuflada wysuwana z prawej krawędzi. `panel = "sidebar"` umieszcza go zamiast tego w **panelu stron** po lewej, pod Ulubionymi i Ostatnimi.

## Makra
`b.macro(nazwa, function(ctx) … end, { description = "…", args = { "…" }, example = "…" })` daje notatkom **makro**: wpisanie `{{ext:<rozszerzenie>/<nazwa> argumenty}}` w dowolnym miejscu notatki pokazuje w tekście to, co zwraca funkcja, zawsze aktualne. Rozszerzenie wskazuje jego identyfikator (tytuł strony małymi literami, bez polskich znaków, z `-` zamiast spacji: „Licznik słów” to `licznik-slow`). Wpisanie `{{` w notatce podsuwa makra z ich opisami.

- `ctx.args` to lista argumentów rozdzielonych przecinkami: `{{ext:a/b Przysiad, 30}}` daje `{ "Przysiad", "30" }`; `ctx.text` to one tak, jak je zapisano.
- `ctx.page` i `ctx.path` wskazują notatkę z makrem, `ctx.date` jej dzień, gdy to dzień dziennika.
- Zwróć tekst, liczbę, `b.ui.text` (przygaszony albo w kolorze), `b.ui.link`, `b.ui.heading` (pogrubiony) — albo ich listę. Zawsze pokazuje się jako tekst, nigdy jako HTML.
- Makro liczy się od nowa samo, gdy zmieni się notatka, którą czytało, i gdy zmieni się któreś z ustawień jego rozszerzenia. Nie może pytać ani niczego zmieniać — nie zapisuje, nie zmienia ustawień, nie uruchamia timerów — i ma ćwierć sekundy.
- Makro rozszerzenia, które jeszcze nie działało na tym urządzeniu, pokazuje kłódkę: kliknięcie pokazuje kod rozszerzenia do przeczytania przed uruchomieniem.
- Wartość widać wszędzie tam, gdzie tekst notatki: w **Zadaniach**, w odnośnikach zwrotnych, w wynikach zapytania i w komórkach widoku bazy danych, a także w blokach skopiowanych i wklejonych do poczty albo edytora biurowego. Eksport do HTML, PDF i Worda też ją pokazuje; eksport do Markdownu i skopiowany Markdown zostawiają makro tak, jak je zapisano.

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
`b.on(zdarzenie, function(e) … end)` uruchamia funkcję, gdy coś się wydarzy:

| Zdarzenie | Kiedy | `e` |
|---|---|---|
| `"save"` | zaraz po tym, jak to urządzenie zapisało stronę | `page`, `path` |
| `"change"` | po zmianach w notatkach, skądkolwiek przyszły — z Twojego pisania, synchronizacji, innego programu, **innego rozszerzenia** — gdy zmiany ucichną | `pages` (nazwy; także strona usunięta), `paths`, `by` (inne rozszerzenia, które pisały), `sources` (`"app"`, `"extension"`, `"sync"`, `"other"`) |
| `"task"` | zadanie zostało odhaczone, otwarte z powrotem, anulowane albo zaczęte | zadanie jako blok, z `task` (jego stan teraz), `previous`, `source` i `by` |
| `"journal"` | powstała notatka dnia | `date`, `page`, `path`, `source`, `by` |
| `"open"` | notatnik został otwarty (raz, po wczytaniu rozszerzeń) | `date` |
| `"day"` | zaczął się nowy dzień, gdy Brulion działa | `date`, `previous` |
| `"timer"` | skończył się własny timer rozszerzenia | `label`, `seconds` |
| `"route"` | ekran pokazuje coś innego: stronę, dzień, widok | `view`, `page`, `path`, `date`, `previous` |

Reakcja nigdy nie słyszy tego, co zapisało jej własne rozszerzenie — więc nie odpowiada sama sobie — ale słyszy każde inne: zadanie odhaczone w widoku jednego rozszerzenia dociera do strony, którą prowadzi inne. Rozszerzenia odpowiadające sobie nawzajem zatrzymują się po czterech rundach. Synchronizacja przynosi to, co zapisały rozszerzenia na innym urządzeniu, z `source = "sync"`: reakcja, która coś dopisuje przy każdym zdarzeniu, powinna je pomijać, inaczej wpis pojawi się raz z każdego urządzenia. Reakcja nie może zadać pytania, a taka, która zawiedzie trzy razy z rzędu, zostaje wyłączona do ponownego wczytania rozszerzenia.

```luau
-- Wpis w dzisiejszym dzienniku przy każdym zadaniu zrobionym na tym urządzeniu.
b.on("task", function(e)
    if e.task == "x" and e.source ~= "sync" then
        b.appendToJournal(b.today(), `- Zrobione: {e.plain} ([[{e.page}]])`)
    end
end)

-- Nowa notatka dnia dostaje wiersz na początek.
b.on("journal", function(e)
    if e.source ~= "sync" then
        b.appendToPage(e.path, "- Trzy rzeczy na dziś:")
    end
end)
```

## Pytania

| Funkcja | Co dostajesz |
|---|---|
| `b.ask(pytanie)` | wiersz tekstu, który wpisałeś; pusty tekst, gdy zamknąłeś okno |
| `b.choose(pytanie, { "A", "B" })` | opcję, którą wybrałeś; `nil`, gdy zamknąłeś okno |
| `b.form(tytuł, pola)` | liczby, każda z − i +, i wiersze tekstu: `{ klucz = liczba albo tekst }`; `nil`, gdy zamknąłeś okno |

Pole liczbowe formularza to `{ key = "powt", label = "Powtórzenia", value = 8, step = 1, unit = "" }`, pole tekstowe `{ key = "nazwa", label = "Nazwa", kind = "text", value = "", placeholder = "np. Medytacja" }` — `placeholder` to szary przykład w pustym polu. Pytania działają w poleceniach i akcjach; nie działają w widokach, w reakcjach ani podczas wczytywania strony. Okno pokazuje najwyżej pierwsze 10 000 znaków tekstu pytania; `b.choose` przyjmuje do 500 opcji, a `b.form` do 50 pól.

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

## Ustawienia
`b.settings({ … })` daje rozszerzeniu własne ustawienia: w **Ustawieniach → Rozszerzenia** (i w rogu jego strony) pojawia się dla niego przycisk **Ustawienia**, gdzie je zmieniasz. `b.setting(klucz)` odczytuje jedno; `b.setSetting(klucz, wartość)` zmienia je z kodu. Są zapisane na tym urządzeniu, nie w notatniku: telefon i komputer mogą mieć każdy swoje.

| Ustawienie | Co to jest |
|---|---|
| `{ key = "budzet", label = "Budżet miesięczny", value = 2000, step = 100, unit = "zł" }` | liczba z − i +, którą można też wpisać |
| `{ key = "waluta", label = "Waluta", value = "zł" }` | wiersz tekstu |
| `{ key = "grosze", label = "Pokazuj grosze", value = false }` | przełącznik |
| `{ key = "okres", label = "Okres", options = { "miesiąc", "tydzień" } }` | jedna z opcji |

`value` to wartość, dopóki jej nie zmienisz, `description = "…"` to zdanie pod nim. [[Siłownia]] też ma jedno: przerwę między seriami.

```luau
b.settings({
    { key = "szklanka", label = "Szklanka", value = 250, step = 50, unit = "ml" },
    { key = "cel", label = "Cel na dzień", value = 2000, step = 250, unit = "ml" },
})

b.command("woda", function(ctx)
    local szklanka, cel = b.setting("szklanka"), b.setting("cel")
    b.appendToJournal(b.today(), `- Woda: {szklanka} ml`)
    local wypite = 0
    for _, block in b.blocks({ page = b.today() }) do
        wypite += tonumber(block.text:match("^Woda: (%d+) ml")) or 0
    end
    return `Dziś {wypite} z {cel} ml`
end, { label = "Szklanka wody" })
```

## Timery
`b.timer(sekundy, { label = "Herbata", message = "Herbata gotowa." })` uruchamia własny timer rozszerzenia: jego odliczanie widać pod górnym paskiem w każdym widoku (z × do zatrzymania), a gdy czas minie, powiadomienie mówi „Herbata: koniec” z Twoją wiadomością. Każde rozszerzenie ma jeden timer — nowy zastępuje poprzedni —, `b.stopTimer()` go zatrzymuje, a `b.on("timer", function(e) … end)` działa, gdy się skończy (`e.label`, `e.seconds`).

Na Androidzie powiadomienie przychodzi także przy zamkniętym Brulionie: koniec timera jest ustawiany jako alarm, gdy timer startuje (i odwoływany, gdy go zatrzymasz). Na komputerze przychodzi, dopóki Brulion działa — także w zasobniku z zamkniętym oknem —, w przeglądarce, dopóki jego karta jest otwarta. Brulion uruchomiony ponownie w trakcie timera pokazuje, jak odlicza dalej; timer, który skończył się w międzyczasie, uruchamia swoje reakcje `"timer"` przy otwarciu notatnika, z `e.late` równym `true`.

```luau
b.command("herbata", function(ctx)
    b.timer(4 * 60, { label = "Herbata", message = "Herbata gotowa: wyjmij torebkę." })
    return "Cztery minuty"
end, { label = "Timer herbaty" })

b.on("timer", function(e)
    b.appendToJournal(b.today(), `- {os.date("%H:%M")} {e.label} ({e.seconds // 60} min)`)
end)
```

## Wspólny kod
`b.require("Wspólne funkcje")` daje to, co zwraca kod innej strony rozszerzenia — zwykle tabelę funkcji, których używa kilka rozszerzeń, żeby każde nie nosiło własnej kopii. Taka strona to rozszerzenie jak każde inne (z tagiem `rozszerzenie`, kodem w blokach `luau`, zakończonym przez `return`) i działa dopiero, gdy zatwierdzisz ją na tym urządzeniu. Każde rozszerzenie, które jej używa, dostaje własną kopię, z własnymi uprawnieniami: wspólny kod nigdy nie zrobi więcej, niż wolno rozszerzeniu, które go używa. Gdy wspólna strona się zmieni, rozszerzenia, które jej używają, wczytują się od nowa.

```luau
-- Strona „Wspólne funkcje” kończy się przez `return M`: tabelę funkcji.
local wspolne = b.require("Wspólne funkcje")

b.command("otwarte-zadania", function(ctx)
    return wspolne.count(b.query("type:tasks status:open", 1).total, "otwarte zadanie", "otwartych zadań")
end, { label = "Ile otwartych zadań" })
```

## AI
`b.ai("pytanie")` pyta model wybrany w **Ustawieniach → Rozszerzenia → AI** i zwraca jego odpowiedź jako tekst; tabela dodaje `system`, `model`, `maxTokens`, `effort`, `thoughts = true` (pole `thoughts` odpowiedzi: to, co model myślał — streszczenie myślenia Claude'a, rozumowanie serwera, myśli modelu na urządzeniu), `images` (obrazki z notatnika dla modelu, który widzi: `b.ai({ prompt = "Co tu jest napisane?", images = { "paragon.jpg" } })`), `messages` dla rozmowy i `tools`, które model może wywołać. Strona potrzebuje uprawnienia `ai`. Więcej na stronie [[AI]].

## Aplikacja
`b.app` to sam Brulion: to, co pokazuje, jego polecenia, ustawienia tego urządzenia i notatnika, kolory, górny pasek, pasek stanu i menu. Czytanie nie wymaga niczego; zmienianie wymaga uprawnienia `aplikacja` (`uprawnienia: [aplikacja]`). Zmienia się aplikacja na tym urządzeniu — tak jak przy ustawieniach zmienianych ręcznie — a to, co rozszerzenie dodało do aplikacji, znika, gdy je wyłączysz.

| Funkcja | Co robi |
|---|---|
| `b.app.state()` | co jest na ekranie: `view` (`"journals"`, `"page"`, `"calendar"` …), `page`, `date`, `dark`, `touch`, `narrow`, `width`, `height`, `platform`, `language` |
| `b.app.commands()` | polecenia palety, każde `{ id, title, shortcut, enabled }` |
| `b.app.run(id)` | uruchamia jedno z nich: `b.app.run("go.calendar")`, `b.app.run("theme.toggle")` |
| `b.app.settings()` | ustawienia, które rozszerzenie może zmienić, z wartościami i tym, co przyjmują: tego urządzenia (`scope = "device"`), potem własne ustawienia notatnika (`scope = "notebook"`, zapisane w `.brulion/config.json` i synchronizowane: `journalsFolder`, `journalFileFormat`, `journalTemplate`, `templatesFolder`, `newPagesFolder`, `weekStartsOn`, `defaultReminderTime`, fiszkowe `flashcards.tags`, `flashcards.clozeBold` …, `themePage`) |
| `b.app.get(klucz)`, `b.app.set(klucz, wartość)` | jedno ustawienie: `b.app.set("density", "compact")`, `b.app.set("focusMode", true)`, `b.app.set("editorFontSize", 18)`; ustawienia notatnika dla każdego jego urządzenia: `b.app.set("journalsFolder", "Dziennik")`, `b.app.set("weekStartsOn", "sunday")`, `b.app.set("themePage", "Motywy/Sepia.md")` |
| `b.app.navigate(widok)` | pokazuje widok: `"calendar"`, `"tasks"`, `{ view = "tasks", query = "status:open" }` |
| `b.app.open(strona, { line = 12, side = true })` | otwiera stronę, w danym wierszu, w panelu bocznym |
| `b.app.back()` | wraca |
| `b.app.toast(tekst, { tone = "success", button = { label = "Cofnij", command = "cofnij" } })` | komunikat, z przyciskiem uruchamiającym jedno z poleceń rozszerzenia |
| `b.app.theme({ … })` | kolory, zaokrąglenia i czcionki Brulionu, dopóki rozszerzenie działa (niżej) |
| `b.app.contribute(miejsce, element)` | dodaje przycisk albo pozycję w aplikacji (niżej) |
| `b.app.remove(miejsce, id)`, `b.app.contributions()` | zabiera jeden; wypisuje elementy wszystkich rozszerzeń |
| `b.app.inputRule({ match = "->", replace = "→" })`, `b.app.inputRules()` | tekst zamieniany w trakcie pisania (niżej); reguły wszystkich rozszerzeń |
| `b.app.tagStyle({ tag = "pilne", tint = "danger" })`, `b.app.tagStyles()` | bloki z tagiem w kolorze (niżej); style wszystkich rozszerzeń |

Przechodzenie (`navigate`, `open`, `back`, `run`) i komunikaty dzieją się po wywołaniu — w poleceniach, akcjach i reakcjach, nie podczas wczytywania strony. Reakcja `"route"` uruchamia się, gdy ekran pokazuje coś innego: `e.view`, `e.page`, `e.date`.

**Kolory.** `b.app.theme` przyjmuje nazwy tokenów wyglądu Brulionu: `bg`, `bgElevated`, `bgSunken` (powierzchnie), `fg`, `fgMuted` (tekst), `accent`, `link`, `tagFg`, `border`, `selection`, `mark` (wyróżnienia), `danger`, `warning`, `success` i inne, zaokrąglenia `radius`, `radiusSm`, `radiusLg`, odstępy między elementami `space` (od 3 do 6, zwykle 4) oraz czcionki `fontUi`, `fontSans`, `fontSerif`, `fontMono`. Kolor to `"#rrggbb"`; kolory jednego trybu idą do `dark = { … }` albo `light = { … }`. Literówka w nazwie to błąd, który podaje właściwą nazwę. Te same nazwy, jako zwykłe wiersze bloku `theme`, tworzą stronę motywu, która nie potrzebuje żadnego kodu ([[Pierwsze kroki#Motywy|Motywy]]); kolory rozszerzenia kładą się na nią.

```luau
-- Sepia: strony w kolorze papieru w jasnym trybie, morski akcent, łagodniejsze rogi.
b.app.theme({
    accent = "#1f8a80",
    radius = 9,
    light = { bg = "#f4ecd8", bgElevated = "#fbf5e6", bgSunken = "#ebe1c8", fg = "#3b2f20" },
    dark = { bg = "#14110c" },
})
```

**Przyciski i pozycje.** `b.app.contribute(miejsce, element)` dodaje element w jednym z jedenastu miejsc: `"toolbar"` (przycisk górnego paska), `"statusbar"` (tekst na dole okna), `"sidebar"` (pozycja paska bocznego), `"pageMenu"` (menu strony), `"blockMenu"` (menu bloku), `"slash"` (menu `/`), `"tabMenu"` (menu karty), `"folderMenu"` (menu wiersza w drzewie folderów), `"bottomNav"` (przycisk dolnej nawigacji telefonu), `"keyboardToolbar"` (przycisk paska edycji nad klawiaturą telefonu) i `"share"` (akcja okien udostępniania). Element ma `id`, `label`, `icon` i mówi, co robi: `command` (jedno z poleceń rozszerzenia), `view`, `page`, `run` (polecenie palety) albo, w menu `/`, `insert` (tekst do wpisania). To samo `id` jeszcze raz zastępuje element — tekst paska stanu, który coś liczy, po prostu dodaje się od nowa. Etykieta to twój tekst: Brulion jej nie tłumaczy.

To, co dostaje polecenie, zależy od miejsca, w którym je wybrano: stronę karty jako `ctx.page`; w drzewie folderów ścieżkę wiersza jako `ctx.path`, a folder jako `ctx.folder` albo stronę jako `ctx.page`; na pasku nad klawiaturą edytowany blok, `ctx.block`, i to, co jest w nim zaznaczone, `ctx.selection`; w oknie udostępniania to, co udostępniono, `ctx.shared` — `text`, `title`, `incoming` (`true`, gdy inna aplikacja udostępniła to Brulionowi, `false` dla strony udostępnianej z „Eksportuj stronę” Bruliona) i, dla plików, `files`. Akcja udostępniania tylko uruchamia polecenie i przejmuje to, co udostępniono: Brulion sam niczego wtedy nie zapisuje. Dolna nawigacja i pasek nad klawiaturą pokazują same ikony, więc ich przyciski potrzebują `icon`. W dolnej nawigacji jest miejsce na 2 przyciski wszystkich rozszerzeń — stają za jej widokami i nigdy nie zajmują miejsca żadnego z nich —, na pasku nad klawiaturą na 4 przyciski jednego rozszerzenia (8 w sumie), a akcji udostępniania może być 4 (8 w sumie); każde inne miejsce mieści 16 elementów rozszerzenia.

```luau
-- Dzisiejsze otwarte zadania na pasku stanu, zawsze aktualne, i pozycja `/` na spotkanie.
local function policz()
    local termin = b.query("type:tasks status:open due:<=today", 1).total
    b.app.contribute("statusbar", {
        id = "termin",
        label = if termin == 0 then "Nic na dziś" else `Na dziś: {termin}`,
        icon = "calendar-check",
        run = "go.tasks",
    })
end

policz()
b.on("change", function(e)
    policz()
end)

b.app.contribute("slash", {
    id = "spotkanie",
    label = "Notatka ze spotkania",
    insert = "## Spotkanie\n- Kto:\n- Ustalenia:\n- [ ] Następny krok",
})
```

```luau
-- To, co udostępniono, zapisane jako zakładka, i przycisk nad klawiaturą telefonu, który
-- oznacza edytowany blok jako ważny.
b.command("zakladka", function(ctx)
    if not ctx.shared then
        return
    end
    b.appendToPage("Zakładki", `- {ctx.shared.title}: {ctx.shared.text:sub(1, 200)}`)
    return "Zapisano zakładkę"
end, { palette = false })

b.command("wazne", function(ctx)
    if ctx.block then
        b.setProperty(ctx.block.ref, "priorytet", "wysoki")
    end
end, { palette = false })

b.app.contribute("share", { id = "zakladka", label = "Zakładka", icon = "bookmark", command = "zakladka" })
b.app.contribute("keyboardToolbar", { id = "wazne", label = "Ważne", icon = "flag", command = "wazne" })
```

**W trakcie pisania.** `b.app.inputRule({ match, replace })` zamienia tekst w chwili, gdy go wpisujesz: `->` staje się `→`, gdy wpiszesz `>`. Jedno Ctrl+Z przywraca to, co wpisałeś. Z `word = true` zamieniane jest tylko całe słowo, gdy wpiszesz po nim spację albo znak interpunkcyjny. Nic nie jest zamieniane w kodzie, wzorach, celach linków ani adresach stron, ani przy wklejaniu. Brulion nie ma własnych reguł — rozszerzenie deklaruje te, których chcesz — a `b.app.remove("inputRule", "->")` zabiera jedną z nich.

```luau
-- Strzałki i półpauzy w trakcie pisania oraz poprawione słowo.
b.app.inputRule({ match = "->", replace = "→" })
b.app.inputRule({ match = "<-", replace = "←" })
b.app.inputRule({ match = "--", replace = "–" })
b.app.inputRule({ match = "...", replace = "…" })
b.app.inputRule({ match = "zolw", replace = "żółw", word = true })
```

**Bloki według tagu.** `b.app.tagStyle({ tag, tint })` rysuje każdy blok z tym tagiem na kolorowym tle z paskiem przy lewej krawędzi. `tint` to jeden z kolorów motywu — `"accent"`, `"success"`, `"danger"`, `"warning"`, `"info"`, `"neutral"`, które dopasowują się do jasnego i ciemnego trybu — albo kolor `"#rrggbb"`. `fill = false` zostawia sam pasek, `bar = false` samo tło. Styl tagu `projekt` koloruje też `#projekt/dom`.

```luau
-- Pilne bloki na czerwono, czekające z bursztynowym paskiem.
b.app.tagStyle({ tag = "pilne", tint = "danger" })
b.app.tagStyle({ tag = "czeka", tint = "warning", fill = false })
b.app.tagStyle({ tag = "pomysł", tint = "#8e6cd8" })
```

Agent AI, z którym pracujesz, może zrobić to wszystko od razu w działającej aplikacji, bez strony — a Ty możesz to cofnąć w menu **Na żywo** na górnym pasku. Zobacz [[Rozszerzenia]] i opis dla programistów.

## Luau w skrócie
- Teksty łączy `..`, można je też pisać w odwrotnych apostrofach z klamrami: `` `{n} stron` ``. Lista to `{ "a", "b" }` i liczy się od 1; `#lista` to jej długość; `for i, element in lista do … end` przechodzi po niej.
- `local` tworzy zmienną, `function … end` funkcję, `--` zaczyna komentarz, `~=` znaczy „różne”, `nil` znaczy „nic”.
- Nazwy zmiennych i funkcji pisz bez polskich znaków (`slowa`, nie `słowa`); w tekstach w cudzysłowie polskie znaki są w porządku.
- Tekst liczy bajty, nie litery: `#"żółw"` to 7. Do liter użyj `utf8.len(tekst)`, do porównywania `b.fold`, do sortowania `b.sorted`.
- Liczbę pisze się z kropką: `82.5`. Pokazuj ją przez `b.formatNumber`.
- `b.now()` to bieżąca data z godziną, `os.date("%H:%M")` sama godzina.
- Nie ma plików, sieci ani innych programów: jest tylko `b`.

## Limity
Jedno wywołanie może trwać 2 sekundy (czas, w którym odpowiadasz na pytanie, się nie liczy) — makro ćwierć sekundy, zmienna szablonu sekundę —, a wszystkie rozszerzenia razem mają 64 MB pamięci; po przekroczeniu wywołanie zostaje zatrzymane, a reszta działa dalej. Widok pokazuje do 2000 wierszy, wiersz `row` do 24 kontrolek; `b.blocks` daje do 20 000 bloków; jedno pytanie do modelu ma do 200 000 znaków. Makro pokazuje najwyżej 400 znaków; makra jednej strony mają razem 2 sekundy. Tekst pytania pokazuje do 10 000 znaków, `b.choose` do 500 opcji, `b.form` do 50 pól; etykieta timera 100 znaków; ustawienia rozszerzenia 64 KB; `b.require` sięga do 16 stron w głąb.

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
| `b.require: … waits for approval on this device` | wspólny kod zmienił się gdzie indziej albo jest nowy: zatwierdź jego stronę |
| `b.require: no page named …`, `… is no extension page` | strony, której kod ma być użyty, nie ma albo nie ma tagu `rozszerzenie` |
| `b.require: a cycle` | dwie strony wymagają siebie nawzajem |
| `b.setSetting: "…" is a number` | wartość innego rodzaju niż jej ustawienie |
| `b.choose: at most 500 options` | za dużo opcji na jedno pytanie: pytaj po kawałku |
| `timeout: the extension ran too long` | jedno wywołanie trwało dłużej niż 2 sekundy |
| `a macro or a template variable cannot change notes` (albo `… settings or timers`) | makro albo zmienna szablonu zapisywały, zmieniały ustawienie albo uruchamiały timer: rób to w poleceniu albo akcji |
| `a macro returns a text, a number, b.ui.text, b.ui.link or a list of them` | makro zwróciło coś, czego nie da się pokazać |

`pcall` łapie błąd, więc kod może działać dalej: `local ok, komunikat = pcall(b.write, nazwa, tekst)`. `error("…", 0)` zatrzymuje polecenie z Twoim własnym komunikatem.
