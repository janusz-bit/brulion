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
- Na telefonie z Androidem tekst, odnośnik albo plik `.md`/`.txt` z innej aplikacji zapiszesz przez **Udostępnij → Brulion**: okno **Zapisz w Brulionie** pozwala poprawić tekst i wybrać **Dzisiejszy dziennik** (blok na końcu dnia; kilka plików — po bloku na plik) albo **Nową stronę** (tytuł z nazwy pliku, treść bez zmian). Daty w udostępnionym tekście nie tworzą przypomnień.
- Bloki z kilku dni zaznaczysz i skopiujesz naraz: Shift+kliknięciem w blok innego dnia, przeciągnięciem myszą albo Shift+strzałkami za brzeg dnia (na telefonie stuknięciem w blok innego dnia, gdy zaznaczanie jest włączone). W schowku każdy dzień zaczyna się od linku do niego — zobacz [[Dokument i konspekt#Zaznaczanie i kopiowanie|Zaznaczanie i kopiowanie]].
- Nazwę pliku i folder dziennika zmienisz w *Ustawienia → Notatnik*.
- Dzień zapisany pod złą datą przeniesiesz albo przemianujesz jak każdy plik: w drzewie folderów, na liście **Wszystkie strony** albo po „Otwórz jako stronę” (F2, menu **Więcej**) — zobacz [[Pierwsze kroki#Foldery|Foldery]].

## Kalendarz
Widok **Kalendarz** ma układ miesiąca, tygodnia i agendy. Pokazuje:
- terminy 📅 i zaplanowane ⏳ zadań,
- przypomnienia ⏰ i alarmy,
- dni, w których jest wpis w dzienniku.

Kliknięcie dnia otwiera jego plan, z którego możesz dodać zadanie lub przypomnienie bez wychodzenia z kalendarza. Prawy przycisk myszy na wpisie (na telefonie przytrzymanie) otwiera menu jego daty albo przypomnienia — zmiana trafia do notatki, a powiadomienie ma przycisk **Cofnij**. W panelu bocznym strony dziennika jest też mały kalendarz miesiąca.

### Kalendarz z ikony w zasobniku
Na komputerze (Windows i Linux) mały kalendarz otworzysz bez wyciągania okna Brulionu: z ikony Brulionu w zasobniku systemowym, obok zegara. Na Windows wystarczy kliknąć ikonę, na Linuksie wybierz **Kalendarz** z jej menu (prawy przycisk myszy). Okienko pokazuje dzisiejszą datę, miesiąc z kropkami w dniach z wpisami i listę zadań i przypomnień wybranego dnia. Kliknięcie zadania albo **Otwórz dziennik** pokazuje ten dzień w oknie Brulionu, a na dole są **Otwórz Brulion** i **Szybka notatka…**. Kalendarz znika po kliknięciu obok, po Escape albo po ponownym kliknięciu ikony.

Co robi kliknięcie ikony, wybierzesz w *Ustawienia → Aplikacja → Kliknięcie ikony w zasobniku*: **Kalendarz** (domyślnie na Windows) albo **Okno** (domyślnie na Linuksie). Podwójne kliknięcie zawsze pokazuje okno. Kalendarza zegara Windows Brulion nie zastępuje — Windows na to nie pozwala — ale możesz ukryć zegar systemowy i przypiąć ikonę Brulionu na pasku zadań. Jak to zrobić, opisuje `docs/WINDOWS.md` w repozytorium (rozdział „Kalendarz z zasobnika”).

Zobacz [[Zadania|Zadania]] i [[Przypomnienia i alarmy|Przypomnienia i alarmy]].

## Szablony
Szablony to zwykłe notatki w folderze `templates/`. Wstawiasz je poleceniem `/Szablon…`. Zmienne w nich zastępowane są przy wstawianiu:

| Zmienna | Wartość |
|---|---|
| `{{date}}` | dzisiejsza data `RRRR-MM-DD`; `{{date:dd.MM.yyyy}}` — własny format |
| `{{time}}` | godzina `GG:MM` |
| `{{weekday}}` | nazwa dnia tygodnia |
| `{{title}}` | tytuł strony |
| `{{cursor}}` | gdzie ustawić kursor po wstawieniu |

Plik `templates/journal.md` jest szablonem **nowego dnia dziennika**: pusty dzień pokazuje go jako tekst startowy, a notatka powstaje razem z pierwszą zmianą. Przykładowy brulion ma gotowe szablony `journal` i `Spotkanie`.

Wróć do [[Spis treści|spisu treści]].
