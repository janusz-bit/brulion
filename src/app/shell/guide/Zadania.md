---
tags: [dokumentacja]
brulion: document
---
# Zadania

Zadanie to punkt listy z polem wyboru. Zapis jest zgodny z wtyczką Obsidian Tasks, więc zadania zostają czytelne w innych programach.

## Stany
| Zapis | Stan |
|---|---|
| `- [ ]` | do zrobienia |
| `- [/]` | w toku |
| `- [x]` | zrobione |
| `- [-]` | anulowane |
| `- [>]` | oczekujące (odłożone) |

**Ctrl+Enter** w bloku przełącza stan po kolei (brak → do zrobienia → w toku → zrobione). Kliknięcie pola wyboru odhacza zadanie albo przywraca je do zrobienia. Znaczniki Logseq (`TODO`, `DOING`, `DONE`, `LATER`, `NOW`, `WAITING`, `CANCELED`) też są zadaniami.

## Wybór stanu
Każdy stan możesz wybrać od razu, na trzy sposoby:

- **Menu stanu** — kliknij pole wyboru prawym przyciskiem myszy albo przytrzymaj je palcem. Menu ma pozycje Do zrobienia, W toku, Zrobione, Oczekujące, Anulowane i Bez statusu; bieżący stan jest zaznaczony. Działa wszędzie, gdzie widać pole zadania: w notatce i dzienniku, w wynikach [[Zapytania|zapytań]], we wstawionych blokach, w odnośnikach zwrotnych, na stronach tagów, w kalendarzu i w widoku Zadania.
- **Klawiatura** — **Ctrl+Shift+Enter** w edytowanym (albo zaznaczonym) bloku otwiera to samo menu; strzałki wybierają stan, Enter zatwierdza, Escape zamyka.
- **Polecenie `/`** — wpisz w bloku `/` i początek nazwy: „Zadanie: do zrobienia”, „Zadanie: w toku”, „Zadanie: zrobione”, „Zadanie: oczekujące”, „Zadanie: anulowane”, „Zadanie: bez statusu”. Wystarczą też słowa takie jak `/czeka`, `/odłożone`, `/anul`, `/bez`, `/todo`, `/doing`, `/done`. Tekst polecenia znika, reszta bloku i kursor zostają.

Na telefonie przytrzymaj przycisk **Zadanie** na pasku nad klawiaturą — otworzy się menu stanu (krótkie dotknięcie przełącza stan po kolei).

Blok, który nie był zadaniem, staje się zadaniem. **Bez statusu** usuwa pole wyboru (i datę ✅ lub ❌), tekst zostaje zwykłym punktem listy. Zadania zapisane znacznikami Logseq zachowują swój styl: `TODO` staje się `DOING`, `LATER` — `NOW`, oczekujące to `WAITING`. Zmianę stanu cofniesz przez Ctrl+Z (w widoku Zadania i w kalendarzu — przyciskiem „Cofnij” w powiadomieniu).

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

Nie musisz znać tych znaczników na pamięć. Po wpisaniu `📅` (albo wybraniu „Termin…” z menu `/`) wystarczy napisać po polsku lub angielsku: „jutro”, „w piątek”, „za 3 dni”, „15 października”, „15.10 18:30”. Brulion wstawi datę w postaci `RRRR-MM-DD` — w pliku zawsze zostaje data, nigdy słowa.

## Priorytety
`🔺` najwyższy, `⏫` wysoki, `🔼` średni, `🔽` niski, `⏬` najniższy. Brak znacznika to zwykły priorytet. Logseqowe `[#A]`, `[#B]`, `[#C]` też są rozumiane.

## Powtarzanie
Znacznik `🔁` z regułą: `🔁 every week`, `🔁 every 2 days`, `🔁 every month on the 15th`, `🔁 every weekday`, `🔁 every year`. Dodatek `when done` liczy następny termin od dnia wykonania, a nie od pierwotnego terminu.

Po polsku reguły można wpisać tak: „codziennie”, „co 3 dni”, „w dni robocze”, „co tydzień w poniedziałek, piątek”, „co miesiąc 15.”, „co miesiąc ostatniego”, „co rok”, „po wykonaniu”. Brulion zapisze je w postaci kanonicznej.

Po odhaczeniu zadania cyklicznego Brulion dopisuje jego następne wystąpienie nad ukończonym.

## Widok Zadania
Widok **Zadania** zbiera zadania z całego brulionu: pole szybkiego dodawania, filtry i sekcje Zaległe, Dziś, Jutro, W tym tygodniu, Później, Bez daty, Zrobione. „Pokaż jako zapytanie” zamienia bieżące filtry w blok [[Zapytania|zapytania]], który możesz wkleić do notatki.

Zadania z terminami pokazuje też kalendarz ([[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]]).
