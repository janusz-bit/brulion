---
tags: [dokumentacja]
brulion: document
---
# Import i eksport

Widok **Import i eksport** otwierasz z menu **Więcej** (⋮) albo z palety poleceń. Import jest też na stronie startowej pustego notatnika („Importuj notatki”).

## Import
Obsługiwane źródła: Obsidian, Logseq, Notion, Evernote, Google Keep, Microsoft To Do, Joplin, Simplenote, Roam, Trilium, Bear, pliki Markdown, TXT i HTML oraz kalendarze iCalendar. Możesz wskazać pliki, archiwum ZIP albo cały folder.

1. Wybierz źródło i pliki.
2. Obejrzyj **podgląd**: co powstanie, co zostanie pominięte i dlaczego.
3. Zatwierdź. Nic nie jest zapisywane przed tym krokiem.

Pliki i foldery wybierasz w oknie wyboru plików systemu — na Androidzie także z Dysku Google albo karty SD. W przeglądarce też wybierzesz kilka plików naraz (folderu tam nie wybierzesz — spakuj go do ZIP-a); plik większy niż 512 MB zostanie pominięty z ostrzeżeniem w podglądzie.

Importowane strony nie nadpisują istniejących — gdy nazwa się powtarza, nowa dostaje końcówkę ` 2`. Przed zmianami poprzednie wersje trafiają do historii.

Tekst z programów, które nie piszą w Markdownie (HTML, Evernote, Google Keep, bazy Notion, Trilium, kalendarze), zostaje tekstem: znaki, które w Brulionie coś znaczą, dostają ukośnik. Tak samo `$` — ale tylko tam, gdzie dwa dolary utworzyłyby [[Wzory matematyczne|wzór]] (`Cena $10, Netflix 15$` zapisze się jako `Cena \$10, Netflix 15$`); pojedyncze ceny, jak `$5 i $10`, zostają bez zmian.

Rysunki (*canvas*) z Trilium stają się [[Rysunki|rysunkami]] Brulionu: każdy to jeden plik SVG z obrazem z Trilium i sceną Excalidraw w środku (także z wklejonymi obrazami), który otworzysz w edytorze rysunków i dalej zmienisz.

Brulion jest zgodny z formatem Obsidiana i Logseqa, więc często wystarczy po prostu otworzyć ich folder jako notatnik. Notatki z Obsidiana (nagłówki i akapity) otworzą się jako dokumenty, a z Logseqa (same punkty) jako konspekty. Tag to w Brulionie [[Tagi i właściwości#Tag to link do strony|link do strony]], jak w Logsequ: strony tagów z Logseqa łączą się ze swoimi tagami. W Obsidianie tagi i strony są osobne — tag `#projekt` obok strony „Projekt” zacznie do niej prowadzić.

### Microsoft To Do
Każda lista zadań z Microsoft To Do staje się stroną z [[Zadania|zadaniami]]: stan, ważność (`⏫`, `🔽`), termin `📅`, przypomnienie `⏰`, powtarzanie `🔁`, kategorie jako tagi, notatka pod zadaniem, kroki jako podpunkty, linki i pliki. Daty są przeliczane na strefę czasową urządzenia. To jednorazowe przeniesienie, nie synchronizacja.

**Zaloguj się w Microsoft** pobiera wszystko samo, ale wymaga własnej, bezpłatnej rejestracji aplikacji w Microsoft Entra (raz):

1. Na entra.microsoft.com: **Rejestracje aplikacji → Nowa rejestracja**, nazwa np. „Brulion”, typ kont: **Tylko osobiste konta Microsoft** (dla outlook.com i hotmail.com) albo konta organizacyjne dla konta służbowego. Adres przekierowania zostaw pusty.
2. **Uwierzytelnianie → Zezwalaj na przepływy klienta publicznego: Tak** — bez tego logowanie kończy się błędem AADSTS7000218.
3. **Uprawnienia interfejsu API → Dodaj uprawnienie → Microsoft Graph → Uprawnienia delegowane → `Tasks.Read`**.
4. Skopiuj **Identyfikator aplikacji (klienta)** i wklej go w Brulionie (**Importuj notatki → Microsoft To Do**). Pod **Więcej** jest urząd: `consumers` (domyślny) dla kont osobistych, `organizations` dla służbowych, `common` dla obu.

Potem **Zaloguj się w Microsoft**: otwórz microsoft.com/devicelogin (także na telefonie), wpisz pokazany kod i zaloguj się. Brulion prosi tylko o odczyt zadań, a token logowania trzyma w pamięci tylko na czas pobierania — nic nie zapisuje.

Bez rejestracji: w Graph Explorerze (developer.microsoft.com/graph/graph-explorer) zaloguj się, zgódź się na `Tasks.Read`, uruchom `GET https://graph.microsoft.com/v1.0/me/todo/lists?$expand=tasks`, zapisz odpowiedź jako plik `.json` i wybierz go przyciskiem **Wybierz pliki…** — bez kroków i plików. W przeglądarce działa tylko ta droga.

### Skarbiec Obsidiana bez importu
Folder Obsidiana — także synchronizowany Syncthingiem — otwórz po prostu jako notatnik (*Przełącz notatnik → Otwórz inny notatnik…*) na każdym urządzeniu i zostaw synchronizację Syncthingowi; nie włączaj dla niego dodatkowo Nextclouda. Folderu `.obsidian/` Brulion nie pokazuje i nigdy nie zmienia — czyta z niego tylko ustawienia dziennika i załączników.

- Folder, nazwy plików (wzór `DD.MM.YYYY` z Obsidiana to tu `dd.MM.yyyy`) i szablon dziennika, folder szablonów i folder załączników Brulion sam przejmie z ustawień Obsidiana, dopóki notatnik nie ma pliku `.brulion/config.json`. **Ustawienia → Notatnik** pokazują, co przejął; zmiana któregoś ustawienia albo przycisk **Zapisz w notatniku** zapisuje je w `.brulion/config.json` — zrób to, jeśli otwierasz notatnik także w przeglądarce przez Nextcloud, który nie przesyła `.obsidian/`. Wzoru z numerem tygodnia albo `Do` („27th”) ani załączników „obok notatki” Brulion nie przejmie — ustawienia pokażą ostrzeżenie, a wtedy wpisz to sam. Notatnik, w którym Brulion zapisywał już dni w `journals/` (albo załączniki w `assets/`), dalej ich tam używa.
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

Gotowy plik zapiszesz przyciskiem **Zapisz…** w oknie zapisu systemu; w przeglądarce plik się pobiera (Chrome i Edge na komputerze najpierw pytają, gdzie go zapisać).

**Zdjęcia z internetu** (`![opis](https://…)`) w eksporcie HTML i Pandoc: wybierz **Zostaw adresy** — plik jest mały, ale zdjęcia pokażą się tylko z dostępem do sieci — albo **Dołącz kopie**: zdjęcia trafią do eksportu (w jednym pliku HTML — wewnątrz pliku, na stronie HTML i w archiwum Pandoca — do folderu `assets/web`). Brulion bierze kopie z pamięci podręcznej urządzenia, brakujące najpierw pobiera; zdjęcie, którego nie uda się pobrać, zostaje adresem, a raport po eksporcie to wymienia. Zdjęć z tego urządzenia albo twojej sieci domowej (np. `192.168.…`, `nas.local`) eksport sam nie pobiera — dołączy je, jeśli wcześniej wyświetlisz je w notatce. Kopii jest najwyżej 256 MB łącznie; dalsze zdjęcia zostają adresami. Wybór zapamiętuje to urządzenie. Inne formaty zostawiają tekst notatek bez zmian.

W eksporcie HTML tag prowadzi do strony, którą nazywa, a eksport do Logseqa zapisuje `#[[Projekty/Dom]]`, gdy strona ma tam inną nazwę niż tag. Eksport do Obsidiana i Markdowna zostawia tagi bez zmian.

### Dokument dla programu Pandoc
Format **Pandoc Markdown (ZIP)** to archiwum z jednym plikiem `.md` i załącznikami, do których prowadzą linki. Eksportuj cały notatnik, folder albo jedną stronę (wtedy dokumentem jest ta strona). Rozpakuj archiwum i w jego folderze uruchom [pandoc](https://pandoc.org):

```
pandoc "Mój notatnik.md" -o "Mój notatnik.docx"
```

Zamiast `.docx` możesz podać `.odt`, `.epub`, `.html` albo `.pdf` (PDF wymaga LaTeX-a). Każda notatka jest rozdziałem z tytułem jako nagłówkiem. Linki `[[…]]`, odwołania do bloków i nagłówków prowadzą do miejsc w dokumencie, a osadzenia są wstawione. Wzory (`$…$`, `$$…$$`, blok ` ```math `) stają się wzorami pandoca, callouty — ramkami (`::: callout`), a zadania — polami wyboru. Daty i przypomnienia są zapisane słowami („termin: 2026-10-20”), bo emoji nie działają w każdym formacie. Tekst, który pandoc odczytałby inaczej niż Brulion (HTML, `@`, `^`, `~`), jest poprzedzony ukośnikiem.

Ponieważ notatki to zwykłe pliki, najprostszą kopią zapasową jest skopiowanie folderu notatnika.

Wróć do [[Spis treści|spisu treści]]. Jeśli chcesz zrozumieć, co leży w plikach, zobacz [[Format plików|Format plików]].
