# Import notatek z innych aplikacji

Ekran importu (**Więcej → Importuj notatki**, na telefonie też z szuflady ☰) przyjmuje pliki,
archiwa ZIP i całe foldery. Wybierz aplikację, z której pochodzą notatki — pod jej nazwą są
kroki eksportu — albo od razu wskaż pliki: Brulion sam rozpozna format. Przed zapisem pokazuje
podgląd (strony, notatki dzienne, załączniki, zmienione nazwy, ostrzeżenia), a po imporcie raport.
Strony trafiają do folderu importu (domyślnie `Import/<aplikacja>`), notatki dzienne do dziennika,
załączniki do `assets/<folder importu>/`. Nic nie jest nadpisywane: gdy nazwa jest zajęta, nowa
strona dostaje dopisek „ 2”, a linki do niej są poprawiane. Szczegóły dla telefonu:
[ANDROID.md](ANDROID.md#import-notatek-obsidian-logseq-notion).

Obsługiwane aplikacje i formaty: Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin,
Simplenote, Roam Research, **Trilium Notes**, Bear, pliki Markdown/TXT, HTML i kalendarze
iCalendar.

Tekst z aplikacji, które nie piszą w Markdownie (HTML, Evernote, Google Keep, bazy Notion,
Trilium, kalendarze), zostaje tekstem: znaki, które w Brulionie coś znaczą, dostają `\`.
Dotyczy to też znaku `$` — ale tylko tam, gdzie dwa `$` utworzyłyby wzór („Cena $10,
Netflix 15$”, „$x$”); pojedyncze ceny („$5 i $10”, „15$”) zostają bez zmian. Kod i adresy
linków nie są zmieniane.

## Folder Obsidiana bez importu

Skarbca Obsidiana nie trzeba importować — także takiego, który już synchronizujesz
Syncthingiem. Brulion czyta te same pliki `.md`, więc folder skarbca możesz po prostu otworzyć
jako notatnik (na ekranie startowym albo *Przełącz notatnik → Otwórz inny notatnik…*) na każdym
urządzeniu, na którym Syncthing go trzyma; na Androidzie Brulion poprosi o „Dostęp do wszystkich
plików” (szczegóły: [ANDROID.md](ANDROID.md)). Obie aplikacje mogą pracować na tym samym
folderze na zmianę.

**Polecenie:** zostań przy Syncthingu i otwórz folder. *Nie* włączaj dla tego notatnika
dodatkowo synchronizacji z Nextcloudem — dwa mechanizmy synchronizacji na jednym folderze
przesyłają te same zmiany dwiema drogami i mnożą konflikty. Nextcloud ma sens tam, gdzie
Syncthinga nie ma (przeglądarka, notatki w pamięci aplikacji na telefonie). Import wybierz
tylko wtedy, gdy chcesz osobnej kopii (np. żeby wypróbować Brulion bez dotykania skarbca):
zaimportowane notatki trafiają do innego notatnika i nie są już tymi samymi plikami.

**Co działa od razu:** linki `[[Strona]]`, `[[Strona|alias]]`, `[[Strona#Nagłówek]]`,
`[[Strona#^id]]`, osadzenia `![[…]]` (także obrazów leżących w dowolnym folderze), linki
Markdownowe, nagłówek YAML (`tags`, `aliases`), tagi, ramki `> [!note]`, `==zakreślenia==`,
`%%komentarze%%`, wzory `$…$` i zadania w składni wtyczki **Tasks** (`📅 ⏳ 🛫 ✅ 🔁`) — Brulion
także je tak zapisuje. Notatki z nagłówkami i akapitami otwierają się jako dokumenty, jak
w Obsidianie. Zmiana nazwy albo przeniesienie strony poprawia linki do niej we wszystkich
notatkach (jak w Obsidianie). Folder `.obsidian/` (ustawienia, wtyczki, motywy) jest pomijany
i nigdy nie zmieniany, a kopie konfliktów Syncthinga (`*.sync-conflict-*`) notatek nie są
pokazywane jako strony, tylko w centrum synchronizacji jako konflikty do scalenia.

**Co ustawić raz** (*Ustawienia → Notatnik*; zapisuje się w `.brulion/config.json`, więc inne
urządzenia dostaną to przez Syncthing):

* **Folder dziennika** — Brulion trzyma dni w `journals/`, a Obsidian (wtyczka *Daily notes*)
  domyślnie w głównym folderze skarbca. Wpisz folder z ustawień notatek dziennych Obsidiana
  (`.obsidian/daily-notes.json`, pole `folder`) albo zostaw pole puste, gdy dni leżą na samej
  górze — inaczej Brulion założy nowe dni w `journals/`, obok starych.
* **Nazwy plików dziennika** — ten sam wzór co w Obsidianie (pole `format`), ale w zapisie Qt:
  `YYYY-MM-DD` to `yyyy-MM-dd` (domyślny w obu), `DD.MM.YYYY` to `dd.MM.yyyy`.
* **Szablon dziennika** — np. `Szablony/Dzień.md`, jeśli Obsidian ma szablon notatki dziennej.
  Zmienne `{{date}}`, `{{time}}`, `{{title}}` działają w obu aplikacjach.
* **Folder załączników** — gdzie trafiają wklejone obrazy (w Brulionie `assets/`; w Obsidianie
  *Settings → Files and links → Default location for new attachments*). Obrazy już wstawione
  działają bez tego.
* **Tryb dziennika** — dni są domyślnie konspektem (punkty, jak w Logseq); jeśli piszesz
  w notatkach dziennych akapitami, wybierz *Dokument*.

**Co się różni:**

* **Tagi** to w Brulionie linki do stron (jak w Logseq): `#projekt` prowadzi do strony
  „projekt” — zob. „Tagi a strony” niżej. W plikach nic się przez to nie zmienia.
* **Odwołanie do bloku** `((id))` (*Kopiuj odwołanie do bloku*, `Ctrl+Shift+C`) to składnia
  Logseq: Obsidian pokaże je jako zwykły tekst. W skarbcu, który otwierasz w obu aplikacjach,
  używaj *Kopiuj link do bloku* — `[[Strona#^id]]` działa w obu.
* Przypomnienia `⏰ …`, bloki kodu `calc` i `query` oraz formuły w tabelach to składnia
  Brulionu — Obsidian pokaże je jako tekst albo blok kodu. W drugą stronę: zapytania Dataview
  i pliki innych wtyczek (np. rysunki Excalidraw `*.excalidraw.md`) Brulion pokaże jako zwykły
  tekst lub stronę; pliki `.canvas` nie są stronami.
* **Ulubione** (zakładki Obsidiana z `.obsidian/bookmarks.json` nie są czytane) i **zwinięcia
  bloków** Brulion pamięta na urządzeniu, nie w plikach.

**Co Brulion dopisuje do folderu:** `.brulion/config.json` (dopiero po zmianie ustawień
notatnika), `.trash/` na usunięte pliki (ten sam folder, którego używa Obsidian przy opcji
*Move to Obsidian trash*; obie aplikacje go pomijają), nowe dni dziennika (dopiero po
pierwszym wpisie — samo oglądanie dnia niczego nie tworzy), wklejone obrazy w folderze
załączników, klucz `brulion: document`/`outline` w nagłówku YAML notatki, której tryb zmienisz
ręcznie (Obsidian pokaże go jako zwykłą właściwość), i identyfikator ` ^id` na końcu bloku,
do którego skopiujesz link — w formacie Obsidiana. Historia wersji, indeks i stan
synchronizacji leżą poza folderem, więc Syncthing ich nie przesyła.

**Ryzyko konfliktów:** nie edytuj tej samej notatki w tej samej chwili w obu aplikacjach ani na
dwóch urządzeniach naraz. Brulion przejmuje zmiany z dysku na bieżąco (także w otwartej
notatce, scalając je z tym, co właśnie piszesz), a poprzednie wersje zostają w *Historii
wersji*. Gdy Syncthing mimo to zostawi kopię `*.sync-conflict-*`, scal ją w centrum
synchronizacji. Konflikty w `.obsidian/` (np. `workspace.json`) dotyczą tylko Obsidiana.

## Tagi a strony

W Brulionie tag nazywa stronę, jak w Logseq: `#praca` prowadzi do strony „praca” (szczegóły:
[SYNTAX.md](SYNTAX.md) §3). Strony tagów z Logseq (np. `pages/praca.md`) łączą się więc po
imporcie ze swoimi tagami, a tagi zagnieżdżone `#projekt/dom` — ze stronami przestrzeni nazw,
które import zapisuje w folderach (`projekt/dom.md`). W Obsidianie tagi i strony są osobne: tag
`#projekt` obok strony „Projekt” po imporcie zacznie do niej prowadzić, a jej odnośniki pokażą
bloki z tym tagiem. Przy eksporcie: Obsidian i Markdown dostają tagi bez zmian (Obsidian dalej
trzyma je osobno), Logseq — `#[[Projekty/Dom]]`, gdy strona ma tam inną nazwę niż tag, a w
eksporcie HTML tag prowadzi do strony, którą nazywa.

## Trilium Notes

Działa z eksportem ZIP z Trilium 0.10x (projekt TriliumNext, znów nazywany po prostu Trilium),
TriliumNext 0.9x i z ostatnich wersji zadam/trilium (0.6x).

### Jak wyeksportować notatki

1. W drzewie notatek kliknij prawym przyciskiem notatkę, którą chcesz przenieść razem ze
   wszystkim, co jest pod nią. Wszystkie notatki naraz: notatka główna (*root*) na samej górze
   drzewa.
2. Wybierz **Eksportuj** (*Export*), potem **Ta notatka i wszystkie jej elementy podrzędne**
   (*This note & all descendants*) i format **HTML** (zalecany — zachowuje całe formatowanie)
   albo **Markdown**.
3. W Brulionie wybierz pobrany plik ZIP — nie trzeba go rozpakowywać.

Wskazówki:

* **Notatki chronione** (zaszyfrowane) trafiają do eksportu tylko wtedy, gdy przed eksportem
  otworzysz sesję chronioną (wpiszesz hasło). Inaczej ZIP zawiera tylko ich miejsce w drzewie —
  Brulion pominie je i powie, ile ich było.
* Kilka eksportów importuj **po kolei**, każdy osobno (wybrane razem rozpakowałyby się w to samo
  miejsce). Brulion ostrzeże, gdy w wyborze są pliki spoza eksportu.
* Format **„HTML do publikacji w sieci”** to gotowa strona WWW, a nie notatki — Brulion go nie
  importuje. **OPML** też nie jest obsługiwany. Eksport pojedynczej notatki (bez elementów
  podrzędnych) daje zwykły plik `.html` albo `.md` — zaimportuj go jako HTML lub Markdown.
* Eksport HTML jest dokładniejszy niż Markdown: eksport Markdown z Trilium gubi podkreślenia,
  a w starszych wersjach także stany zadań, zwijane bloki i osadzone notatki.
* Notatki większe niż 4 MB albo z ponad 5000 elementami list Brulion zapisuje jako pliki w
  załącznikach (z linkiem na stronie i ostrzeżeniem) — ich zamiana trwałaby zbyt długo.
* Ponowny import tego samego eksportu niczego nie nadpisuje: strony dostają dopisek „ 2” (a treść
  notatek dziennych jest dopisywana drugi raz), linki prowadzą do stron z nowego importu.

### Co się z czym zamienia

| W Trilium | W Brulionie |
|---|---|
| notatka tekstowa | strona `<folder importu>/<ścieżka w drzewie>/<tytuł>.md`; znaki, których nie może mieć nazwa pliku (`/ : ? "` …), wypadają z nazwy, a prawdziwy tytuł trafia do `aliases`. Nazwy zastrzeżone w Windows (`CON`, `NUL` …) dostają `_` (notatnik może się synchronizować z Windows), nazwy folderów pomijanych przez notatnik (`logseq`, `node_modules`) — dopisek „ 2” |
| notatka z elementami podrzędnymi | strona i folder o tej samej nazwie (`Projekty.md` i `Projekty/`); bez własnej treści — sam folder. Pod treścią strony, po linii `---`, jest lista elementów podrzędnych — jak lista notatek, którą Trilium pokazuje pod notatką (obrazy osadzone, pliki jako linki; z etykietą `#hideChildrenOverview` tylko klony, obrazy i pliki) |
| prefiks gałęzi (np. „Część I”) | część nazwy: `Część I - Rozdział 1.md`, sam tytuł w `aliases` |
| notatki o tym samym tytule w jednym miejscu | „Zadania” i „Zadania 2” (wielkość liter się nie liczy) |
| kolejność w drzewie | Brulion układa strony w folderze alfabetycznie; kolejność z Trilium zostaje na listach elementów podrzędnych |
| kolekcja (*book*) | strona z listą linków do elementów podrzędnych, w kolejności z Trilium (kolekcja obrazów — obrazy); pusta kolekcja bez elementów — nic |
| notatka bez treści, ale z etykietami, linkami do niej lub klonami | strona z listą elementów podrzędnych |
| notatka dnia (`#dateNote`) | notatka dzienna w dzienniku (`journals/2026-10-16.md`, folder i format z ustawień notatnika; dopisywana, gdy taki dzień już jest). Własny dopisek w tytule („24 - Czwartek - Wigilia!”) — nagłówek `# Wigilia!` na początku dnia. Elementy podrzędne dnia są stronami w folderze kalendarza i są linkowane z dnia. Strona z importu nigdy nie zajmuje ścieżki notatki dziennej |
| kalendarz, rok, kwartał, miesiąc, tydzień | tylko foldery (strona — gdy mają własną treść) |
| klon (ta sama notatka w kilku miejscach) | importowany raz, w pierwszym miejscu drzewa; w pozostałych miejscach — link na liście elementów podrzędnych rodzica |
| notatka kodu | strona z blokiem kodu w jej języku (```` ```python ````); notatka typu Markdown — jej treść |
| diagram Mermaid | blok ```` ```mermaid ```` ze źródłem i link do obrazu SVG wyrenderowanego przez Trilium (taki SVG ma etykiety w HTML i style CSS, których Brulion nie potrafi narysować); obraz, który da się pokazać, jest osadzony nad źródłem |
| canvas (Excalidraw) | strona z obrazem SVG i plikiem `.excalidraw` w załącznikach |
| mapa myśli | lista zagnieżdżona, link do obrazu (jeśli jest) i plik `.json` |
| mapa relacji | lista linków do notatek z mapy |
| strona WWW (*web view*) | link do jej adresu |
| obraz, plik | załącznik w `assets/<folder importu>/<ścieżka>/`; notatki, które je pokazują, osadzają go |
| załączniki notatki | `assets/<folder importu>/<ścieżka notatki>/…`, osadzone tam, gdzie były w treści; pozostałe — na końcu strony |
| zapisane wyszukiwanie, mapa notatek, notatka renderowana, skrót | pomijane — ostrzeżenie wymienia ich tytuły |
| bardzo głębokie drzewo, bardzo długie tytuły | najgłębsze notatki trafiają do folderu przodka (ścieżka pliku ma limit długości) — ostrzeżenie |

Atrybuty:

| W Trilium | W Brulionie |
|---|---|
| etykieta bez wartości `#projekt` | tag (`tags: [projekt]`) |
| etykieta z wartością `#status=w toku` | właściwość `status: w toku` (kilka etykiet o tej samej nazwie — lista) |
| relacja `~autor` → notatka | właściwość z linkiem: `autor: "[[Import/Trilium/Osoby/Jan Kowalski\|Jan Kowalski]]"` |
| `#pageUrl` (strony zapisane z przeglądarki) | właściwość `source` |
| szablon (`#template`) z definicjami atrybutów (`#label:telefon=…`) | strona z pustymi właściwościami (`telefon: ""`) — przenieś ją do folderu szablonów, jeśli chcesz z niej korzystać |
| etykiety i relacje samego Trilium (`#iconClass`, `#cssClass`, `#archived`, `#sorted`, `#viewType`, `#bookZoomLevel`, `#calendarRoot`, `~template`, skrypty, udostępnianie…) | pomijane |
| atrybut dziedziczony | tylko na notatce, która go ma (Brulion nie dziedziczy atrybutów) |

Treść notatek:

| W Trilium | W Brulionie |
|---|---|
| lista zadań i ich stany | `- [ ]`, `- [x]`, `- [/]` (w trakcie), `- [-]` (anulowane); „może” i własne stany — `- [ ]`; puste zadania (pusty checkbox z szablonu dnia) są pomijane |
| element listy z kilkoma blokami (kod, obraz, cytat, kilka akapitów) | bloki zostają w elemencie, z wcięciem pod jego znacznikiem |
| dwie listy albo dwa cytaty tuż obok siebie | osobne (druga lista ze znacznikiem `*` albo `1)`, żeby Markdown ich nie połączył) |
| uwaga (*admonition*: notatka, wskazówka, ważne, ostrożnie, ostrzeżenie) | callout `> [!tip]`, `> [!warning]` … |
| blok zwijany | zwinięty callout `> [!note]- Tytuł` (rozwinięty: `+`) |
| przypisy (także w notatkach zaimportowanych do Trilium z Markdowna) | `[^1]` w tekście, treść przypisów na końcu strony |
| wzór w tekście / blokowy | `$E=mc^2$` / blok ```` ```math ```` — Brulion składa wzory, źródło LaTeX zostaje w pliku; `$` z tekstu wokół, który połączyłby się ze wzorem (np. cena w tym samym akapicie), dostaje `\$`; gdy litera czy cyfra tuż za wzorem zmieniłaby, co Brulion uzna za wzór, wzory tej linii zostają kodem `` `$E=mc^2$` `` |
| `$` w tekście (ceny, `$x$` jako zwykły tekst) | bez zmian, a tam, gdzie dwa `$` utworzyłyby wzór („Cena $10, Netflix 15$”), pierwszy dostaje `\$` — widać go jako `$` |
| link referencyjny do notatki | `[[Import/Trilium/Projekty/Strona\|Strona]]` (pokazuje tytuł z Trilium) |
| link z własnym tekstem | `[[Import/Trilium/Projekty/Strona\|tekst]]`; link do notatki dnia — `[[2026-10-16]]` |
| link do notatki spoza eksportu, wykluczonej albo chronionej | sam tekst linku (ostrzeżenie) |
| osadzona notatka (*include note*) | `![[Import/Trilium/Projekty/Strona]]` |
| `#słowo` w tekście (w Trilium to zwykły tekst, nie etykieta) | `\#słowo` — nie staje się tagiem (we wzorze `#` zostaje bez zmian: należy do wzoru) |
| podgląd linku (*link preview*) | zwykły link `[tytuł](adres)` |
| obraz w treści | `![[assets/…/obraz.png]]`; obrazy w `data:` — zapisane jako załączniki |
| nagłówki, pogrubienie, kursywa, przekreślenie, zaznaczenie, kod, cytaty, tabele, linie | Markdown Brulionu (`==zaznaczenie==`, tabele GFM, ```` ```język ````) |
| podkreślenie, kolor tekstu, indeks górny i dolny, ikony w tekście, podział strony | sam tekst (ikony i podziały wypadają) |

Linki wskazują strony po ich ścieżce w notatniku, z tytułem jako tekstem linku: w Trilium link
prowadzi do jednej, konkretnej notatki, a sam tytuł mógłby wskazać inną stronę o tym samym
tytule (np. z wcześniejszego importu albo własną notatkę „Zadania”). Gdy zmienisz w Brulionie
nazwę strony albo folderu, linki do nich są poprawiane.

Bezpieczeństwo: notatki Trilium mogą zawierać skrypty (notatki kodu JS, `#run`, `~renderNote`)
— Brulion niczego nie uruchamia, kod trafia na strony jako tekst. HTML z notatek przechodzi
przez konwerter, który usuwa skrypty, ramki i niebezpieczne linki.
