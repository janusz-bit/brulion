---
tags: [dokumentacja]
brulion: document
---
# Dziennik, kalendarz i szablony

## Dziennik
Każdy dzień ma własną stronę — plik w folderze `journals/` o nazwie z datą (domyślnie `2026-10-14.md`). Widok **Dziennik** (Ctrl+J) pokazuje dni jeden pod drugim: dzisiejszy na górze, wcześniejsze niżej, a „Przejdź do dnia…” otwiera dowolną datę.

- Strona dnia powstaje przy pierwszej zmianie. Samo oglądanie niczego nie tworzy.
- Link do dnia, np. `[[2026-10-14]]`, wyświetla się jako czytelna data („śr 14 paź”). Prawy przycisk myszy na nim (na telefonie przytrzymanie) otwiera menu: „Otwórz dzień”, „Otwórz w panelu bocznym”, „Kopiuj link”, „Zmień datę…”, „Dziś”, „Jutro”, „Dzień później”, „Tydzień później”. Zmienia się tylko data w linku (podpis po `|` zostaje), a zmianę cofniesz `Ctrl+Z`.
- Szybka notatka (Ctrl+Shift+N) dopisuje się na końcu dzisiejszego dziennika.
- Tekst, odnośnik albo plik `.md`/`.txt` z innej aplikacji zapiszesz w oknie **Zapisz w Brulionie**: na Androidzie przez **Udostępnij → Brulion**, na komputerze przez **Otwórz za pomocą → Brulion** w Eksploratorze Windows albo **Otwórz za pomocą → Inny program → Brulion** w menedżerze plików na Linuksie, a w wersji w przeglądarce zainstalowanej jako aplikacja (Chrome lub Edge na Androidzie) przez **Udostępnij → Brulion** — tekst i odnośniki. Okno pozwala poprawić tekst i wybrać **Dzisiejszy dziennik** (blok na końcu dnia; kilka plików — po bloku na plik) albo **Nową stronę** (tytuł z nazwy pliku, treść bez zmian). Z pliku spoza notatnika powstaje kopia, a sam plik zostaje bez zmian; plik, który już jest w otwartym notatniku, po prostu się otwiera. Daty w udostępnionym tekście nie tworzą przypomnień.
- Bloki z kilku dni zaznaczysz i skopiujesz naraz: Shift+kliknięciem w blok innego dnia, przeciągnięciem myszą albo Shift+strzałkami za brzeg dnia (na telefonie stuknięciem w blok innego dnia, gdy zaznaczanie jest włączone). W schowku każdy dzień zaczyna się od linku do niego — zobacz [[Dokument i konspekt#Zaznaczanie i kopiowanie|Zaznaczanie i kopiowanie]].
- **Ctrl+F** w dzienniku szuka we wszystkich dniach naraz: licznik („3 z 12 · 4 dni”) obejmuje każdy dzień, Enter i strzałki przechodzą do następnego dnia z wynikiem (dziennik sam się tam przewija), a trafienia są podświetlone w każdym widocznym dniu. **Ctrl+H** dodaje zamianę: „Zamień wszystko” zmienia wszystkie dni, a jedno **Cofnij** przywraca je wszystkie. Szukanie obejmuje ostatnie 1000 dni; starsze znajdziesz przez „W całym notatniku…”.
- Nazwę pliku i folder dziennika zmienisz w *Ustawienia → Notatnik*.
- Dzień zapisany pod złą datą przeniesiesz albo przemianujesz jak każdy plik: w drzewie folderów, na liście **Wszystkie strony** albo po „Otwórz jako stronę” (F2, menu **Więcej**) — zobacz [[Pierwsze kroki#Foldery|Foldery]].

## Notatki tygodniowe i miesięczne
Obok stron dni możesz prowadzić stronę **tygodnia** (tydzień ISO, od poniedziałku) i **miesiąca** — na plan, podsumowanie albo przegląd. Domyślnie leżą w folderze dziennika i nazywają się jak w Obsidianie: `2026-W45.md` (tydzień 45 roku 2026) i `2026-11.md`.

- Otworzysz je skrótem **Ctrl+Alt+W** (ten tydzień) i **Ctrl+Alt+M** (ten miesiąc), z palety poleceń (Ctrl+K: „Notatka tego tygodnia”, „…poprzedniego”, „…następnego”) albo z kalendarza: kliknij **numer tygodnia** z lewej strony miesiąca (w widoku tygodnia — w lewym górnym rogu) albo **nazwę miesiąca** czy zakres tygodnia w nagłówku. Klawisze **W** i **M** w kalendarzu otwierają tydzień i miesiąc zaznaczonego dnia.
- Numer tygodnia, który ma już notatkę, jest pogrubiony i w kolorze akcentu, a przy nagłówku z notatką stoi mała książka.
- Jak strona dnia, notatka powstaje przy pierwszej zmianie — z własnego szablonu: `templates/weekly.md` albo `templates/monthly.md`. W nim `{{date}}` to pierwszy dzień okresu, `{{end}}` ostatni, `{{week}}` numer tygodnia, a `{{month}}` nazwa miesiąca.
- Strzałki pod tytułem przechodzą do poprzedniego i następnego tygodnia czy miesiąca.
- Uwaga na przełom roku: 1 stycznia 2027 należy do tygodnia `2026-W53`, a 30 grudnia 2024 do `2025-W01` — tak liczy norma ISO.
- Folder, nazwy plików i szablony zmienisz w *Ustawienia → Notatnik*. W nazwie tygodnia `yyyy` to rok tygodnia, `ww` jego numer, a tekst bierzesz w apostrofy (`yyyy-'W'ww`); nazwa miesiąca to zwykły format daty bez dnia (`yyyy-MM`, `MMMM yyyy`).

## Kalendarz
Widok **Kalendarz** ma układ miesiąca, tygodnia i agendy. Pokazuje:
- terminy 📅 i zaplanowane ⏳ zadań,
- przypomnienia ⏰ i alarmy,
- dni, w których jest wpis w dzienniku.

Na telefonie miesiąc pokazuje małą ikonę dla każdego rodzaju otwartego wpisu: czerwony wykrzyknik — zaległe, bursztynowy dzwonek — przypomnienie lub alarm, niebieski kalendarz — termin, fioletowa klepsydra — zaplanowane. Zrobione wpisy nie mają ikony. Krótka szara kreska pod numerem dnia oznacza, że w tym dniu jest wpis w dzienniku.

Kliknięcie dnia — w agendzie jego nagłówka — otwiera jego plan, z którego możesz dodać zadanie lub przypomnienie bez wychodzenia z kalendarza. Prawy przycisk myszy na wpisie (na telefonie przytrzymanie) otwiera menu jego daty albo przypomnienia — zmiana trafia do notatki, a powiadomienie ma przycisk **Cofnij**. W panelu bocznym strony dziennika jest też mały kalendarz miesiąca.

### Kalendarz z ikony w zasobniku
Na komputerze (Windows i Linux) mały kalendarz otworzysz bez wyciągania okna Brulionu: z ikony Brulionu w zasobniku systemowym, obok zegara. Wystarczy kliknąć ikonę (albo wybrać **Kalendarz** z jej menu, prawy przycisk myszy). Okienko pokazuje dzisiejszą datę, miesiąc z kropkami w dniach z wpisami i listę zadań i przypomnień wybranego dnia. Kliknięcie zadania albo **Otwórz dziennik** pokazuje ten dzień w oknie Brulionu, a na dole są **Otwórz Brulion** i **Szybka notatka…**. Kalendarz znika po kliknięciu obok, po Escape albo po ponownym kliknięciu ikony.

Co robi kliknięcie ikony, wybierzesz w *Ustawienia → Aplikacja → Kliknięcie ikony w zasobniku*: **Kalendarz** (domyślnie) albo **Okno**. Podwójne kliknięcie zawsze pokazuje okno. Kalendarza zegara Windows Brulion nie zastępuje — Windows na to nie pozwala — ale możesz ukryć zegar systemowy i przypiąć ikonę Brulionu na pasku zadań. Jak to zrobić, opisuje `docs/WINDOWS.md` w repozytorium (rozdział „Kalendarz z zasobnika”).

Zobacz [[Zadania|Zadania]] i [[Przypomnienia i alarmy|Przypomnienia i alarmy]].

## Szablony
Szablony to zwykłe notatki w folderze `templates/`. Wstawiasz je poleceniem `/Szablon…`. Zmienne w nich zastępowane są przy wstawianiu:

| Zmienna | Wartość |
|---|---|
| `{{date}}` | dzisiejsza data `RRRR-MM-DD`; `{{date:dd.MM.yyyy}}` — własny format |
| `{{time}}` | godzina `GG:MM` |
| `{{weekday}}` | nazwa dnia tygodnia |
| `{{week}}` | numer tygodnia (ISO) |
| `{{month}}` | nazwa miesiąca („listopad”) |
| `{{end}}` | ostatni dzień tygodnia lub miesiąca w ich notatkach (gdzie indziej — data); `{{end:d MMMM}}` — własny format |
| `{{title}}` | tytuł strony |
| `{{cursor}}` | gdzie ustawić kursor po wstawieniu |

Plik `templates/journal.md` jest szablonem **nowego dnia dziennika**: pusty dzień pokazuje go jako tekst startowy, a notatka powstaje razem z pierwszą zmianą. Tak samo działają `templates/weekly.md` i `templates/monthly.md` dla notatek tygodniowych i miesięcznych. Przykładowy brulion ma gotowe szablony `journal` i `Spotkanie`.

Wróć do [[Spis treści|spisu treści]].
