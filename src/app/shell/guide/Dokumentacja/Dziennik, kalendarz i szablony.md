---
tags: [dokumentacja]
brulion: document
---
# Dziennik, kalendarz i szablony

## Dziennik
Każdy dzień ma własną stronę — plik w folderze `journals/` o nazwie z datą (domyślnie `2026-10-14.md`). Widok **Dziennik** (Ctrl+J) pokazuje dni jeden pod drugim: dzisiejszy na górze, wcześniejsze niżej, a „Przejdź do dnia…” otwiera dowolną datę.

- Strona dnia powstaje przy pierwszej zmianie. Samo oglądanie niczego nie tworzy.
- Link do dnia, np. `[[2026-10-14]]`, wyświetla się jako czytelna data („śr 14 paź”).
- Szybka notatka (Ctrl+Shift+N) dopisuje się na końcu dzisiejszego dziennika.
- Nazwę pliku i folder dziennika zmienisz w *Ustawienia → Notatnik*.

## Kalendarz
Widok **Kalendarz** ma układ miesiąca, tygodnia i agendy. Pokazuje:
- terminy 📅 i zaplanowane ⏳ zadań,
- przypomnienia ⏰ i alarmy,
- dni, w których jest wpis w dzienniku.

Kliknięcie dnia otwiera jego plan, z którego możesz dodać zadanie lub przypomnienie bez wychodzenia z kalendarza. W panelu bocznym strony dziennika jest też mały kalendarz miesiąca.

Zobacz [[Dokumentacja/Zadania|Zadania]] i [[Dokumentacja/Przypomnienia i alarmy|Przypomnienia i alarmy]].

## Szablony
Szablony to zwykłe notatki w folderze `templates/`. Wstawiasz je poleceniem `/Szablon…`. Zmienne w nich zastępowane są przy wstawianiu:

| Zmienna | Wartość |
|---|---|
| `{{date}}` | dzisiejsza data `RRRR-MM-DD`; `{{date:dd.MM.yyyy}}` — własny format |
| `{{time}}` | godzina `GG:MM` |
| `{{weekday}}` | nazwa dnia tygodnia |
| `{{title}}` | tytuł strony |
| `{{cursor}}` | gdzie ustawić kursor po wstawieniu |

Plik `templates/journal.md` jest szablonem **nowego dnia dziennika**: pusty dzień pokazuje go jako tekst startowy, a notatka powstaje razem z pierwszą zmianą. W tym brulionie zajrzyj do [[templates/journal|szablonu dziennika]] i [[templates/Spotkanie|szablonu spotkania]].

Wróć do [[Dokumentacja/Spis treści|spisu treści]].
