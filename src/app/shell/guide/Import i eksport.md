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

Brulion jest zgodny z formatem Obsidiana i Logseqa, więc często wystarczy po prostu otworzyć ich folder jako notatnik. Notatki z Obsidiana (nagłówki i akapity) otworzą się jako dokumenty, a z Logseqa (same punkty) jako konspekty. Tag to w Brulionie [[Tagi i właściwości#Tag to link do strony|link do strony]], jak w Logsequ: strony tagów z Logseqa łączą się ze swoimi tagami. W Obsidianie tagi i strony są osobne — tag `#projekt` obok strony „Projekt” zacznie do niej prowadzić.

### Skarbiec Obsidiana bez importu
Folder Obsidiana — także synchronizowany Syncthingiem — otwórz po prostu jako notatnik (*Przełącz notatnik → Otwórz inny notatnik…*) na każdym urządzeniu i zostaw synchronizację Syncthingowi; nie włączaj dla niego dodatkowo Nextclouda. Folder `.obsidian/` Brulion pomija.

- W **Ustawienia → Notatnik** wpisz raz folder i format dziennika z notatek dziennych Obsidiana (domyślnie trzyma je na samej górze skarbca, więc usuń `journals` z pola „Folder dziennika” — puste pole to główny folder; format `YYYY-MM-DD` z Obsidiana to tu `yyyy-MM-dd`), szablon dziennika (ścieżka z końcówką `.md`, której Obsidian nie zapisuje) i folder załączników. Inaczej nowe dni powstaną w `journals/`.
- Do bloków linkuj przez **Kopiuj link do bloku** (`[[Strona#^id]]`) — odwołanie `((id))` Obsidian pokaże jako zwykły tekst. Przypomnienia `⏰`, bloki `calc` i `query` to składnia Brulionu; zadania wtyczki Tasks działają w obu aplikacjach.
- Nie edytuj tej samej notatki w obu aplikacjach w tej samej chwili; zmiany z dysku Brulion przejmuje na bieżąco, a poprzednie wersje zostają w historii.
- *Centrum synchronizacji → Syncthing → Utwórz .stignore* dodaje reguły pomijające układ okien Obsidiana (`.obsidian/workspace*.json`) i kosz `.trash`, więc te pliki nie robią konfliktów. Skopiuj `.stignore` na każde urządzenie — Syncthing go nie przesyła.

## Eksport
- archiwum **ZIP** z Markdownem, także w układzie Obsidiana lub Logseqa,
- statyczna **strona HTML** albo jeden plik HTML,
- **Pandoc Markdown** — jeden dokument do zamiany w plik Worda, LibreOffice, PDF albo EPUB,
- **JSON** z całą zawartością,
- **iCalendar** — zadania i przypomnienia do kalendarza,
- **OPML** — konspekt,
- **CSV** — lista zadań.

W eksporcie HTML tag prowadzi do strony, którą nazywa, a eksport do Logseqa zapisuje `#[[Projekty/Dom]]`, gdy strona ma tam inną nazwę niż tag. Eksport do Obsidiana i Markdowna zostawia tagi bez zmian.

### Dokument dla programu Pandoc
Format **Pandoc Markdown (ZIP)** to archiwum z jednym plikiem `.md` i załącznikami, do których prowadzą linki. Eksportuj cały notatnik, folder albo jedną stronę (wtedy dokumentem jest ta strona). Rozpakuj archiwum i w jego folderze uruchom [pandoc](https://pandoc.org):

```
pandoc "Mój notatnik.md" -o "Mój notatnik.docx"
```

Zamiast `.docx` możesz podać `.odt`, `.epub`, `.html` albo `.pdf` (PDF wymaga LaTeX-a). Każda notatka jest rozdziałem z tytułem jako nagłówkiem. Linki `[[…]]`, odwołania do bloków i nagłówków prowadzą do miejsc w dokumencie, a osadzenia są wstawione. Wzory (`$…$`, `$$…$$`, blok ` ```math `) stają się wzorami pandoca, callouty — ramkami (`::: callout`), a zadania — polami wyboru. Daty i przypomnienia są zapisane słowami („termin: 2026-10-20”), bo emoji nie działają w każdym formacie. Tekst, który pandoc odczytałby inaczej niż Brulion (HTML, `@`, `^`, `~`), jest poprzedzony ukośnikiem.

Ponieważ notatki to zwykłe pliki, najprostszą kopią zapasową jest skopiowanie folderu notatnika.

Wróć do [[Spis treści|spisu treści]]. Jeśli chcesz zrozumieć, co leży w plikach, zobacz [[Format plików|Format plików]].
