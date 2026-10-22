---
tags: [dokumentacja]
brulion: document
---
# Import i eksport

Widok **Import i eksport** otwierasz z menu **Więcej** (⋮) albo z palety poleceń. Import jest też na stronie startowej pustego notatnika („Importuj notatki”).

## Import
Obsługiwane źródła: Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Trilium, Bear, pliki Markdown, TXT i HTML oraz kalendarze iCalendar. Możesz wskazać pliki, archiwum ZIP albo cały folder.

1. Wybierz źródło i pliki.
2. Obejrzyj **podgląd**: co powstanie, co zostanie pominięte i dlaczego.
3. Zatwierdź. Nic nie jest zapisywane przed tym krokiem.

Importowane strony nie nadpisują istniejących — gdy nazwa się powtarza, nowa dostaje końcówkę ` 2`. Przed zmianami poprzednie wersje trafiają do historii.

Tekst z programów, które nie piszą w Markdownie (HTML, Evernote, Google Keep, bazy Notion, Trilium, kalendarze), zostaje tekstem: znaki, które w Brulionie coś znaczą, dostają ukośnik. Tak samo `$` — ale tylko tam, gdzie dwa dolary utworzyłyby [[Wzory matematyczne|wzór]] (`Cena $10, Netflix 15$` zapisze się jako `Cena \$10, Netflix 15$`); pojedyncze ceny, jak `$5 i $10`, zostają bez zmian.

Brulion jest zgodny z formatem Obsidiana i Logseqa, więc często wystarczy po prostu otworzyć ich folder jako notatnik. Notatki z Obsidiana (nagłówki i akapity) otworzą się jako dokumenty, a z Logseqa (same punkty) jako konspekty.

## Eksport
- archiwum **ZIP** z Markdownem, także w układzie Obsidiana lub Logseqa,
- statyczna **strona HTML** albo jeden plik HTML,
- **JSON** z całą zawartością,
- **iCalendar** — zadania i przypomnienia do kalendarza,
- **OPML** — konspekt,
- **CSV** — lista zadań.

Ponieważ notatki to zwykłe pliki, najprostszą kopią zapasową jest skopiowanie folderu notatnika.

Wróć do [[Spis treści|spisu treści]]. Jeśli chcesz zrozumieć, co leży w plikach, zobacz [[Format plików|Format plików]].
