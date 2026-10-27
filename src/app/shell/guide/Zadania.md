---
tags: [dokumentacja]
brulion: document
---
# Zadania

Zadanie to punkt listy z polem wyboru. Zapis jest zgodny z wtyczką Obsidian Tasks, więc zadania zostają czytelne w innych programach.

## Statusy
| Zapis | Status |
|---|---|
| `- [ ]` | do zrobienia |
| `- [/]` | w toku |
| `- [x]` | zrobione |
| `- [-]` | anulowane |
| `- [>]` | oczekujące (odłożone) |

**Ctrl+Enter** w bloku przełącza status po kolei (brak → do zrobienia → w toku → zrobione → brak). Kliknięcie pola wyboru odhacza zadanie albo przywraca je do zrobienia. Znaczniki Logseq (`TODO`, `DOING`, `DONE`, `LATER`, `NOW`, `WAITING`, `CANCELED`) też są zadaniami.

## Wybór statusu
Każdy status możesz wybrać od razu, na trzy sposoby:

- **Menu statusu** — kliknij pole wyboru prawym przyciskiem myszy albo przytrzymaj je palcem. Menu ma pozycje Do zrobienia, W toku, Zrobione, Oczekujące, Anulowane i Bez statusu; bieżący status jest zaznaczony. Działa wszędzie, gdzie widać pole zadania: w notatce i dzienniku, w wynikach [[Zapytania|zapytań]], we wstawionych blokach, w odnośnikach zwrotnych, na stronach tagów, w kalendarzu i w widoku Zadania.
- **Klawiatura** — **Ctrl+Shift+Enter** w edytowanym (albo zaznaczonym) bloku otwiera to samo menu; strzałki wybierają status, Enter zatwierdza, Escape zamyka.
- **Polecenie `/`** — wpisz w bloku `/` i początek nazwy: „Zadanie: do zrobienia”, „Zadanie: w toku”, „Zadanie: zrobione”, „Zadanie: oczekujące”, „Zadanie: anulowane”, „Zadanie: bez statusu”. Wystarczą też słowa takie jak `/czeka`, `/odłożone`, `/anul`, `/bez`, `/todo`, `/doing`, `/done`. Tekst polecenia znika, reszta bloku i kursor zostają.

Na telefonie przytrzymaj przycisk **Zadanie** na pasku nad klawiaturą — otworzy się menu statusu (krótkie dotknięcie przełącza status po kolei).

Blok, który nie był zadaniem, staje się zadaniem. **Bez statusu** usuwa pole wyboru (i datę ✅ lub ❌), tekst zostaje zwykłym punktem listy. Zadania zapisane znacznikami Logseq zachowują swój styl: `TODO` staje się `DOING`, `LATER` — `NOW`, oczekujące to `WAITING`. Zmianę statusu w otwartej notatce cofniesz przez Ctrl+Z, a w widoku Zadania i w kalendarzu — przyciskiem „Cofnij” w powiadomieniu.

## Terminy i daty
Znaczniki można dopisać gdziekolwiek w pierwszej linii zadania:

| Znacznik | Znaczenie |
|---|---|
| `📅 2026-10-20` | termin (deadline) |
| `⏳ 2026-10-18` | zaplanowane na dzień |
| `🛫 2026-10-16` | początek |
| `✅ 2026-10-14` | data wykonania (dopisywana sama) |
| `➕ 2026-10-10` | data utworzenia |
| `⏰ 2026-10-19 18:00` | przypomnienie — zobacz [[Przypomnienia i alarmy|Przypomnienia i alarmy]] |

Po terminie, zaplanowaniu i początku możesz dopisać godzinę: `📅 2026-10-20 15:00`.

**Zmiana daty bez szukania jej w tekście** — kliknij znacznik daty (📅, ⏳, 🛫, ✅, ➕, ❌) prawym przyciskiem myszy albo przytrzymaj go palcem. Menu ma pozycje „Zmień datę…” (kalendarz z godziną), „Dziś”, „Jutro”, „Dzień później”, „Tydzień później” i „Usuń datę”. Zmienia się tylko ta data — reszta wiersza zostaje, jak była, a zmianę cofniesz (`Ctrl+Z`). Zwykłe kliknięcie znacznika dalej zaczyna edycję tekstu.

Menu działa wszędzie, gdzie widać znacznik: w notatce i w dzienniku, w komórkach tabel, w wynikach [[Zapytania|zapytań]] i we wstawionych blokach (zmiana trafia wtedy do notatki, w której zadanie jest zapisane, a powiadomienie ma przycisk **Cofnij**), w widoku **Zadania** (na znacznikach pod zadaniem) i w kalendarzu (na wpisie dnia).

Znaczniki to zwykłe emoji. Gdy system nie ma czcionki z emoji (Brulion w przeglądarce, niektóre Linuksy), Brulion rysuje je własną czcionką — jednokolorowe, w kolorze tekstu.

Nie musisz znać tych znaczników na pamięć. Po wpisaniu `📅` (albo wybraniu „Termin…” z menu `/`) wystarczy napisać po polsku lub angielsku: „jutro”, „w piątek”, „za 3 dni”, „15 października”, „15.10 18:30”. Brulion wstawi datę w postaci `RRRR-MM-DD` — w pliku zawsze zostaje data, nigdy słowa.

## Priorytety
`🔺` najwyższy, `⏫` wysoki, `🔼` średni, `🔽` niski, `⏬` najniższy. Brak znacznika to zwykły priorytet. Logseqowe `[#A]`, `[#B]`, `[#C]` też są rozumiane.

Priorytet zmienisz z menu: prawy przycisk myszy na znaczniku priorytetu (na telefonie przytrzymanie) daje „Najwyższy” … „Najniższy” i „Bez priorytetu”; bieżący jest zaznaczony. Logseqowy `[#B]` zostaje w zapisie Logseqa (`[#A]`, `[#B]`, `[#C]`).

## Powtarzanie
Znacznik `🔁` z regułą: `🔁 every week`, `🔁 every 2 days`, `🔁 every month on the 15th`, `🔁 every weekday`, `🔁 every year`. Dodatek `when done` liczy następny termin od dnia wykonania, a nie od pierwotnego terminu.

Po polsku reguły można wpisać tak: „codziennie”, „co 3 dni”, „w dni robocze”, „co tydzień w poniedziałek, piątek”, „co miesiąc 15.”, „co miesiąc ostatniego”, „co rok”, „po wykonaniu”. Brulion zapisze je w postaci kanonicznej.

Po odhaczeniu zadania cyklicznego Brulion dopisuje jego następne wystąpienie nad ukończonym.

Regułę zmienisz z menu: prawy przycisk myszy na znaczniku 🔁 (na telefonie przytrzymanie) daje „Zmień regułę…”, gotowe reguły („Codziennie”, „W dni robocze”, „Co tydzień”, „Co 2 tygodnie”, „Co miesiąc”, „Co roku”), przełącznik „Od daty wykonania” (`when done`) i „Nie powtarzaj”. Zmienia się tylko reguła — w `🔁 co tydzień kupić mleko` tekst „kupić mleko” zostaje.

## Widok Zadania
Widok **Zadania** zbiera zadania z całego notatnika: pole szybkiego dodawania, filtry i sekcje Zaległe, Dziś, Jutro, W tym tygodniu, Później, Bez daty, Zrobione. „Pokaż jako zapytanie” zamienia bieżące filtry w blok [[Zapytania|zapytania]], który możesz wkleić do notatki. Kliknięcie tagu w zadaniu filtruje listę po tym tagu; Ctrl+kliknięcie otwiera stronę tagu, a prawy przycisk (na telefonie przytrzymanie) — menu tagu: „Filtruj po tagu”, „Otwórz stronę tagu”, „Widok tagu”, „Otwórz w panelu bocznym”, „Kopiuj tag”. Prawy przycisk albo przytrzymanie znacznika pod zadaniem (termin, zaplanowanie, początek, przypomnienie, powtarzanie, priorytet) otwiera jego menu; zmianę cofniesz przyciskiem **Cofnij** w powiadomieniu.

Zadania z terminami pokazuje też kalendarz ([[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]]).

## Widżety na Androidzie
Na ekranie głównym telefonu postawisz widżety Brulionu (przytrzymaj puste miejsce → **Widżety** → **Brulion**):

- **Zadania — Mój dzień** — zaległe i dzisiejsze zadania jak w widoku Zadania; większy widżet pokazuje też jutrzejsze. Stuknięcie w zadanie otwiera notatkę na tym wierszu.
- **Kalendarz** — miesiąc z kropkami i agenda wybranego dnia: najpierw wpisy z godziną. Niski widżet pokazuje samą agendę.
- **Notatki** — **Dziennik dziś**, **Szybka notatka**, ulubione i ostatnie strony.

Przycisk **+** dodaje zadanie bez otwierania aplikacji: wpisz tekst, wybierz **Dziś**, **Jutro**, **Bez terminu** albo **Data…**, ewentualnie **Godzina…** (przypomnienie) i **Alarm**, i stuknij **Dodaj**. Zadanie trafia na koniec dzisiejszego dziennika — tak jak z aplikacji, na przykład `- [ ] Kupić żółty ser 📅 2026-10-28` — od razu, także gdy Brulion jest zamknięty. Słowa w tekście nie są tu datami: „jutro o 9” zostaje tekstem, dzień i godzinę wybierasz przyciskami. Gdy wpisu nie da się zapisać od razu (na przykład folder notatnika jest akurat niedostępny), czeka, a Brulion dopisze go po otwarciu — nic z tego, co wpiszesz, nie ginie.

Widżety pokazują stan z chwili, gdy Brulion był ostatnio otwarty: zmiany z innych urządzeń zobaczysz po otwarciu aplikacji. O północy same przenoszą zadania na nowy dzień. Zadanie odhaczysz w aplikacji albo przyciskiem **Zrobione** w powiadomieniu przypomnienia.
