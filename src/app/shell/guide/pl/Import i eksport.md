---
tags: [dokumentacja]
brulion: document
---
# Import i eksport

Widok **Import i eksport** otwierasz z menu **Więcej** → **Notatnik** albo z palety poleceń. Import jest też na stronie startowej pustego notatnika („Importuj notatki”).

## Import
Obsługiwane źródła: Obsidian, Logseq, Notion, Evernote, Google Keep, OneNote, Apple Notes, Microsoft To Do, Todoist, Joplin, Simplenote, Roam, Trilium, Bear, Airtable, pliki Markdown, TXT i HTML, arkusze (CSV) oraz kalendarze iCalendar. Możesz wskazać pliki, archiwum ZIP albo cały folder.

1. Wybierz źródło i pliki.
2. Obejrzyj **podgląd**: co powstanie, co zostanie pominięte i dlaczego.
3. Zatwierdź. Nic nie jest zapisywane przed tym krokiem.

Pliki i foldery wybierasz w oknie wyboru plików systemu — na Androidzie także z Dysku Google albo karty SD. W przeglądarce też wybierzesz kilka plików naraz (folderu tam nie wybierzesz — spakuj go do ZIP-a); plik większy niż 512 MB zostanie pominięty z ostrzeżeniem w podglądzie, a gdy wybrane pliki razem przekroczą 1,5 GB, Brulion wczyta tylko te, które się mieszczą, i też o tym napisze — tak samo jak na komputerze.

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

### Arkusze (CSV) i Airtable
Arkusz zapisany jako CSV — z Excela, LibreOffice, Arkuszy Google, Numbers albo innej aplikacji — staje się **stroną na wiersz**: komórki trafiają do [[Tagi i właściwości|właściwości]] strony z typami (liczby, daty, pola wyboru, listy, tagi, linki do stron innych wierszy), a baza Obsidiana obok stron pokazuje je wszystkie jako tabelę ([[Zapytania#Bazy z Obsidiana (Bases)|bazy]]). Możesz też wybrać **Jedną stronę z tabelą**.

Przed importem karta **Kolumny i właściwości** pokazuje, co Brulion znalazł, i pozwala to zmienić:
- **Tytuły stron z** kolumny (wykrywana: *Nazwa*, *Tytuł*, *Name*… albo pierwsza kolumna z niepowtarzającymi się wartościami) albo z **szablonu tytułu**, np. `{{Autor}} – {{Tytuł}}`; **Podfoldery**, np. `{{Kategoria}}`;
- **Treść strony z** kolumny (wykrywana: długa kolumna *Notatki* albo *Opis*) — pozostałe kolumny to właściwości;
- dla każdej kolumny nazwę właściwości i typ albo **Pomiń**;
- pod **Kodowanie, separator, wiersz nagłówka…**: polskie eksporty ze starego Excela (Windows-1250), separator `;` i pliki bez wiersza nagłówka Brulion rozpoznaje sam, a można je też wybrać ręcznie;
- pierwsze wiersze z tytułami, jakie dostaną ich strony, i pierwszą stronę dokładnie tak, jak zostanie zapisana.

**Airtable**: pobierz widok jako CSV (**••• → Download CSV**) i wybierz go — załączniki („zdjęcie.jpg (https://…)”) można pobrać do notatnika przyciskiem **Pobierz załączniki** (linki Airtable wygasają po kilku godzinach), a powiązane rekordy stają się linkami do stron wierszy, które wskazują. Albo połącz się **osobistym tokenem dostępu** (utwórz go na airtable.com/create/tokens z zakresami `data.records:read` i `schema.bases:read`): wybierz bazę i jej tabele, a Brulion pobierze je z typami pól i załącznikami. Token jest tylko w pamięci, do końca pobierania.

### Todoist
Każdy projekt staje się stroną z [[Zadania|zadaniami]]: sekcje jako nagłówki, podzadania z wcięciem, priorytety (p1 `🔺`, p2 `⏫`, p3 `🔼`), terminy `📅` (z terminem ostatecznym: data `⏳` i termin ostateczny `📅`), powtarzające się terminy jako reguły `🔁` tam, gdzie Brulion je rozumie („every monday”, „co tydzień”, `every!` liczy od wykonania), etykiety jako tagi, opisy i komentarze pod zadaniem.
- **Z tokenem API** (Todoist: **Ustawienia → Integracje → Programista**): wklej go, a Brulion pobierze wszystkie projekty z komentarzami i ich plikami. Token nie jest nigdzie zapisywany.
- **Bez niego**: wyeksportuj projekt jako szablon (**••• → Eksportuj jako szablon**, CSV) albo pobierz kopię zapasową (**Ustawienia → Kopie zapasowe**) i wybierz pliki — kilka projektów albo cały ZIP kopii naraz.

### OneNote
**Zaloguj się w Microsoft** tą samą bezpłatną rejestracją aplikacji co dla Microsoft To Do (wyżej) — dodaj do niej uprawnienie delegowane **`Notes.Read`**. Po zalogowaniu wybierz notesy; Brulion pobierze ich sekcje, grupy sekcji i strony z obrazkami (w pełnej rozdzielczości) i załączonymi plikami. Notesy, grupy sekcji i sekcje stają się folderami, podstrony strony — folderem obok niej; tagi „Do zrobienia” stają się zadaniami, pozostałe tagi (`#important`…) tagami, linki między stronami — linkami wiki. OneNote pozwala aplikacjom pobierać tylko kilka stron na minutę, więc duży notes chwilę trwa. W wersji przeglądarkowej OneNote nie da się zaimportować (Microsoft nie pozwala na to logowanie ze stron WWW).

### Apple Notes
Notatki Apple trzymają notatki w bazie danych na Macu, którą Brulion czyta bez Maca:
1. Na Macu w Finderze wybierz **Idź → Idź do folderu…** i wpisz `~/Library/Group Containers/group.com.apple.notes`.
2. Skopiuj cały ten folder (albo jego ZIP: prawy przycisk → **Kompresuj**) na komputer albo telefon z Brulionem.
3. **Importuj notatki → Apple Notes** i wybierz folder albo ZIP.

Foldery zostają folderami (kilka kont — po folderze na konto), pierwszy wiersz notatki jest jej tytułem, a formatowanie przechodzi: nagłówki, pogrubienie, kursywa, przekreślenie, wyróżnienia, listy, listy kontrolne, kod, cytaty, tabele, linki między notatkami, `#tagi`. Obrazki i pliki, rysunki (jako obrazki) i zeskanowane dokumenty (jako PDF albo obrazki) stają się załącznikami. Notatki zablokowane i te w *Ostatnio usuniętych* zostają pominięte (podgląd mówi, ile ich jest); załączniki, które są tylko w iCloud, trzeba najpierw otworzyć na Macu.

### Z wiersza poleceń
`brulion-cli import <plik, folder albo ZIP>` robi to samo co ten ekran — z `--dry-run` tylko pokazuje, co zostałoby zapisane, `--from` wybiera aplikację, a mapowanie CSV ma swoje opcje (`--title-template`, `--columns "Rok=text; Cena=skip"`…). Zobacz [[Wiersz poleceń i agenci AI]].

### Skarbiec Obsidiana bez importu
Folder Obsidiana — także synchronizowany Syncthingiem — otwórz po prostu jako notatnik (*Przełącz notatnik → Otwórz inny notatnik…*) na każdym urządzeniu i zostaw synchronizację Syncthingowi; nie włączaj dla niego dodatkowo Nextclouda. Folderu `.obsidian/` Brulion nie pokazuje i nigdy nie zmienia — czyta z niego tylko ustawienia dziennika i załączników.

- Folder, nazwy plików (wzór `DD.MM.YYYY` z Obsidiana to tu `dd.MM.yyyy`) i szablon dziennika, folder szablonów i folder załączników Brulion sam przejmie z ustawień Obsidiana, dopóki notatnik nie ma pliku `.brulion/config.json`. **Ustawienia → Notatnik** pokazują, co przejął; zmiana któregoś ustawienia albo przycisk **Zapisz w notatniku** zapisuje je w `.brulion/config.json` — zrób to, jeśli otwierasz notatnik także w przeglądarce przez Nextcloud, który nie przesyła `.obsidian/`. Wzoru z numerem tygodnia albo `Do` („27th”) ani załączników „obok notatki” Brulion nie przejmie — ustawienia pokażą ostrzeżenie, a wtedy wpisz to sam. Notatnik, w którym Brulion zapisywał już dni w `journals/` (albo załączniki w `assets/`), dalej ich tam używa.
- Do bloków linkuj przez **Kopiuj link do bloku** (`[[Strona#^id]]`) — odwołanie `((id))` Obsidian pokaże jako zwykły tekst. Przypomnienia `⏰`, bloki `calc` i `query` to składnia Brulionu; zadania wtyczki Tasks działają w obu aplikacjach, a bloki `dataview` wtyczki Dataview pokazują swoje wyniki także w Brulionie ([[Zapytania#Zapytania Dataview z Obsidiana|zapytania Dataview]]).
- Tablice Obsidiana (`.canvas`) otwierają się w widoku tablicy i po edycji w Brulionie Obsidian dalej je czyta ([[Tablice]]).
- Nie edytuj tej samej notatki w obu aplikacjach w tej samej chwili; zmiany z dysku Brulion przejmuje na bieżąco, a poprzednie wersje zostają w historii.
- *Centrum synchronizacji → Syncthing → Utwórz .stignore* dodaje reguły pomijające układ okien Obsidiana (`.obsidian/workspace*.json`) i kosz `.trash`, więc te pliki nie robią konfliktów. Skopiuj `.stignore` na każde urządzenie — Syncthing go nie przesyła.

### Strony z przeglądarki (Web Clipper)
Rozszerzenie **Brulion Web Clipper** dla Chrome, Chromium, Brave, Edge i Firefoksa zapisuje to, co czytasz: **artykuł** ze strony (sam tekst, bez menu i reklam), **zaznaczenie**, **całą stronę**, **zrzut ekranu** widocznej części albo sam **link** — jako Markdown, z obrazkami zapisanymi w załącznikach notatnika.

1. Zainstaluj rozszerzenie (z jego paczki: w przeglądarkach Chromium *Rozszerzenia → Tryb dewelopera → Załaduj rozpakowane*, w Firefoksie *about:debugging → Wczytaj tymczasowy dodatek*).
2. Raz połącz je z Brulionem: **Ustawienia → Aplikacja → Rozszerzenie przeglądarki (Web Clipper) → Połącz przeglądarki** (wiersz każdej przeglądarki pokazuje, czy jest połączona) albo w terminalu `brulion-cli clip install` (sprawdzi to `brulion-cli clip status`; na NixOS robi to moduł).
3. Na stronie kliknij przycisk rozszerzenia (**Alt+Shift+B**): wybierz, co wyciąć, tytuł, **dokąd** — nowa strona w folderze (domyślnie `Wycinki`; do wyboru także link w dzisiejszym dzienniku), dzisiejszy [[Dziennik, kalendarz i szablony|dziennik]] albo koniec sekcji nagłówka strony („## Do przeczytania”) —, szablon, tagi i **Wytnij**. **Otwórz w Brulionie** pokaże wynik.

Prawy klik na stronie, zaznaczeniu, linku albo obrazku wycina go z domyślnymi ustawieniami z opcji, a **Alt+Shift+K** od razu wycina zaznaczenie albo artykuł. Szablony: Brulionu (frontmatter ze źródłem, autorem, datami i tagami), w stylu Web Clippera Obsidiana i Joplina, krótki „link i zaznaczenie” do dziennika, strona z folderu szablonów albo własny tekst w opcjach rozszerzenia — ze zmiennymi [[Dziennik, kalendarz i szablony|szablonów]] oraz `{{url}}`, `{{site}}`, `{{author}}`, `{{published}}`, `{{content}}`, `{{selection}}`, `{{tags}}`.

Nie ma żadnego serwera: przeglądarka przekazuje wycinek do `brulion-cli` na tym samym komputerze, a jedyne zapytania idą po obrazki wycinka, do ich własnych stron — strona z internetu nigdy nie sprawi, że komputer sięgnie do Twojej sieci domowej. Bez tego połączenia — na telefonie, w wersji przeglądarkowej — rozszerzenie kopiuje stronę **z formatowaniem**: wklej ją do notatki, a stanie się Markdownem; na Androidzie **Udostępnij → Brulion** zapisze tytuł i adres strony. Dokumentacja dla programistów: docs/CLIPPER.md w repozytorium.

## Eksport
- archiwum **ZIP** z Markdownem, także w układzie Obsidiana lub Logseqa,
- statyczna **strona HTML** albo jeden plik HTML (diagramy Mermaid i wykresy jako obrazy, zobacz [[Diagramy#Eksport|Diagramy]] i [[Tabele i wykresy|Tabele i wykresy]]),
- **PDF** i **Word (DOCX)** — gotowe do druku i edycji, robione przez sam Brulion, bez Pandoca i LaTeX-a, także na telefonie (zobacz [[#Jedna strona|Jedna strona]]),
- **Pandoc Markdown** — jeden dokument do zamiany w plik Worda, LibreOffice, PDF albo EPUB,
- **JSON** z całą zawartością,
- **iCalendar** — zadania i przypomnienia do kalendarza,
- **OPML** — konspekt,
- **CSV** — lista zadań.

Pojedynczą tabelę z notatki skopiujesz albo zapiszesz jako CSV z jej menu, a plik CSV wstawisz jako tabelę poleceniem `/Tabela z CSV…` — zobacz [[Tabele i wykresy#CSV: arkusze kalkulacyjne|Tabele i wykresy]].

Gotowy plik zapiszesz przyciskiem **Zapisz…** w oknie zapisu systemu; w przeglądarce plik się pobiera (Chrome i Edge na komputerze najpierw pytają, gdzie go zapisać).

**Zdjęcia z internetu** (`![opis](https://…)`) w eksporcie HTML, PDF, DOCX i Pandoc: wybierz **Zostaw adresy** — plik jest mały, ale zdjęcia pokażą się tylko z dostępem do sieci — albo **Dołącz kopie**: zdjęcia trafią do eksportu (w jednym pliku HTML — wewnątrz pliku, na stronie HTML i w archiwum Pandoca — do folderu `assets/web`). Brulion bierze kopie z pamięci podręcznej urządzenia, brakujące najpierw pobiera; zdjęcie, którego nie uda się pobrać, zostaje adresem, a raport po eksporcie to wymienia. Zdjęć z tego urządzenia albo twojej sieci domowej (np. `192.168.…`, `nas.local`) eksport sam nie pobiera — dołączy je, jeśli wcześniej wyświetlisz je w notatce. Kopii jest najwyżej 256 MB łącznie; dalsze zdjęcia zostają adresami. Wybór zapamiętuje to urządzenie. Inne formaty zostawiają tekst notatek bez zmian.

W eksporcie HTML tag prowadzi do strony, którą nazywa, a eksport do Logseqa zapisuje `#[[Projekty/Dom]]`, gdy strona ma tam inną nazwę niż tag. Eksport do Obsidiana i Markdowna zostawia tagi bez zmian.

### Dokument dla programu Pandoc
Format **Pandoc Markdown (ZIP)** to archiwum z jednym plikiem `.md` i załącznikami, do których prowadzą linki. Eksportuj cały notatnik, folder albo jedną stronę (wtedy dokumentem jest ta strona). Rozpakuj archiwum i w jego folderze uruchom [pandoc](https://pandoc.org):

```
pandoc "Mój notatnik.md" -o "Mój notatnik.docx"
```

Zamiast `.docx` możesz podać `.odt`, `.epub`, `.html` albo `.pdf` (PDF wymaga LaTeX-a). Każda notatka jest rozdziałem z tytułem jako nagłówkiem. Linki `[[…]]`, odwołania do bloków i nagłówków prowadzą do miejsc w dokumencie, a osadzenia są wstawione. Wzory (`$…$`, `$$…$$`, blok ` ```math `) stają się wzorami pandoca, callouty — ramkami (`::: callout`), a zadania — polami wyboru. Daty i przypomnienia są zapisane słowami („termin: 2026-10-20”), bo emoji nie działają w każdym formacie. Tekst, który pandoc odczytałby inaczej niż Brulion (HTML, `@`, `^`, `~`), jest poprzedzony ukośnikiem.

### Jedna strona
Otwartą stronę wyeksportujesz bez zaglądania tutaj: w menu strony (⋮) wybierz **Eksportuj stronę…** albo **Drukuj…** (także z palety poleceń; na komputerze Ctrl+P). Formaty jednej strony:

- **Markdown** — plik notatki dokładnie taki, jaki jest,
- **Markdown z załącznikami** — archiwum ZIP z notatką i obrazami oraz plikami, do których prowadzi,
- **HTML** — jedna strona internetowa z obrazami w środku,
- **PDF** — strony gotowe do druku albo wysłania,
- **Word (DOCX)** — dokument do dalszej edycji w Wordzie, LibreOffice Writerze albo Dokumentach Google,
- **Pandoc Markdown** — dokument dla programu pandoc (zobacz niżej).

**Zapisz jako plik…** otwiera okno zapisu systemu (na Androidzie „Zapisz jako”, obok jest też **Zapisz w Pobranych**), w przeglądarce plik się pobiera. Markdown i HTML skopiujesz też do schowka.

PDF i DOCX robi sam Brulion: nagłówki, listy z polami wyboru zadań, tabele, kod, wzory, obrazy dopasowane do strony i [[Diagramy|diagramy]] jako obrazy. Terminy i przypomnienia są zapisane słowami („termin: 2026-11-20”), bo kolorowych emoji nie da się wydrukować. Linki do stron internetowych działają, a linki do innych notatek są zwykłym tekstem. PDF ma wbudowane czcionki, więc polskie litery wyglądają tak samo na każdym urządzeniu. Papier to A4 — albo Letter, jeśli tak ustawiony jest region urządzenia (np. USA, Kanada). Bardzo duża tabela (ponad 1500 wierszy) trafia na papier tylko z pierwszymi wierszami, a Word przyjmie najwyżej 63 kolumny — raport po eksporcie to wymienia. W eksporcie całego notatnika albo folderu do PDF-a czy DOCX każda notatka zaczyna się od nowej strony.

**Drukuj…** na komputerze otwiera okno drukowania systemu (drukarka, liczba kopii, rozmiar papieru), na Androidzie — drukowanie systemu z jego drukarkami i opcją „Zapisz jako PDF”, a w przeglądarce — jej okno drukowania.

### Kod QR
Najszybszy sposób, żeby przekazać notatkę komuś obok: **Kod QR…** w menu strony (⋮), w **Więcej** albo w palecie poleceń („Udostępnij jako kod QR…”) pokazuje notatkę jako kod QR. Skieruj na niego aparat innego telefonu, a pojawi się na nim tekst notatki — bez internetu, bez konta i bez żadnej aplikacji po drugiej stronie. **Kod QR…** w menu bloku (albo w **Więcej** zaznaczenia) robi to samo tylko z wybranymi blokami.

- **Markdown** daje notatkę dokładnie tak, jak jest zapisana — inna aplikacja do notatek (Brulion, Obsidian, Logseq) dostanie ją w całości, z linkami i zadaniami.
- **Zwykły tekst** daje to, co widać w notatce, jak „Kopiuj jako tekst”: punktory „•”, zadania ☐ i ☑, linki jako ich etykiety.
- **Dołącz tytuł** stawia na początku tytuł strony (notatka dzienna: jej dzień); notatka, która zaczyna się od własnego `# Tytułu`, już go ma.

Jeden kod mieści około trzech tysięcy liter (mniej, gdy są emoji i polskie znaki). Dłuższa notatka dzieli się na kilka kodów podobnej wielkości — **Kod 1 z 3**, ze strzałkami ‹ › albo klawiszami strzałek: zeskanuj je po kolei i połącz teksty. Najwyżej dwadzieścia kodów; z dłuższej notatki zaznacz fragment, który chcesz przekazać.

Ponieważ notatki to zwykłe pliki, najprostszą kopią zapasową jest skopiowanie folderu notatnika.

Wróć do [[Spis treści|spisu treści]]. Jeśli chcesz zrozumieć, co leży w plikach, zobacz [[Format plików|Format plików]].
