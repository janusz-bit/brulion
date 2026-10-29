# Import notatek z innych aplikacji

Ekran importu (**Więcej → Importuj notatki**, na telefonie też z szuflady ☰) przyjmuje pliki,
archiwa ZIP i całe foldery. Wybierz aplikację, z której pochodzą notatki — pod jej nazwą są
kroki eksportu — albo od razu wskaż pliki: Brulion sam rozpozna format. Przed zapisem pokazuje
podgląd (strony, notatki dzienne, załączniki, zmienione nazwy, ostrzeżenia), a po imporcie raport.
Strony trafiają do folderu importu (domyślnie `Import/<aplikacja>`), notatki dzienne do dziennika,
załączniki do `assets/<folder importu>/`. Nic nie jest nadpisywane: gdy nazwa jest zajęta, nowa
strona dostaje dopisek „ 2”, a linki do niej są poprawiane. Szczegóły dla telefonu:
[ANDROID.md](ANDROID.md#import-notatek-obsidian-logseq-notion).

Obsługiwane aplikacje i formaty: Obsidian, Logseq, Notion, Evernote, Google Keep,
**[Microsoft To Do](#microsoft-to-do)** (zadania), Joplin, Simplenote, Roam Research,
**Trilium Notes**, Bear, pliki Markdown/TXT, HTML i kalendarze iCalendar.

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
notatkach (jak w Obsidianie). Folder `.obsidian/` (ustawienia, wtyczki, motywy) nie jest
pokazywany i nigdy nie jest zmieniany (Brulion czyta z niego tylko kilka ustawień, zob. niżej), a kopie konfliktów Syncthinga (`*.sync-conflict-*`) notatek nie są
pokazywane jako strony, tylko w centrum synchronizacji jako konflikty do scalenia.

**Co Brulion przejmie sam:** dopóki notatnik nie ma `.brulion/config.json`, ustawienia
dziennika i załączników bierze z ustawień Obsidiana w `.obsidian/` — niczego tam nie zmienia
i sam niczego nie zapisuje:

* **Folder dziennika** — z `.obsidian/daily-notes.json` (pole `folder`; puste albo `/` to główny
  folder skarbca). Bez tego pliku (Obsidian zapisuje go dopiero po zmianie ustawień *Daily
  notes*) Brulion przyjmie domyślny dla Obsidiana główny folder tylko wtedy, gdy leży w nim
  już jakiś dzień `RRRR-MM-DD.md`, a wtyczka *Daily notes* nie jest wyłączona
  (`.obsidian/core-plugins.json`).
* **Nazwy plików dziennika** — wzór z pola `format`, przepisany z zapisu Moment (Obsidian) na
  zapis Qt (Brulion): `YYYY-MM-DD` to `yyyy-MM-dd` (domyślny w obu), `DD.MM.YYYY` to
  `dd.MM.yyyy`, `[Dzień] YYYY-MM-DD` to `'Dzień' yyyy-MM-dd`, a `/` we wzorze to podfoldery jak
  w Obsidianie. Wzorów z elementami bez odpowiednika (`Do` — „27th”, numery tygodni `ww`/`WW`,
  dzień roku `DDD`, godziny) Brulion nie użyje: zostanie przy `yyyy-MM-dd`, a ustawienia pokażą
  ostrzeżenie — wtedy wpisz nazwy plików sam. Nazwy miesięcy i dni (`MMMM`, `dddd`) Brulion
  pisze po angielsku, także gdy Obsidian pisze je po polsku (ostrzeżenie w ustawieniach).
* **Szablon dziennika** — z pola `template`, z dopisaną końcówką `.md` (Obsidian zapisuje
  ścieżkę bez niej), np. `Szablony/Dzień.md`; bez szablonu w Obsidianie nowe dni zaczynają się
  pusto. Zmienne `{{date}}`, `{{time}}`, `{{title}}` działają w obu aplikacjach.
* **Folder szablonów** — z `.obsidian/templates.json` (wtyczka *Templates*, pole `folder`).
* **Folder załączników** — z `.obsidian/app.json` (*Settings → Files and links → Default
  location for new attachments*): wybrany folder albo główny folder skarbca. Położenia „obok
  notatki” (`./`) Brulion nie ma: zostaje przy `assets/` i pokazuje ostrzeżenie. Obrazy już
  wstawione działają w każdym przypadku.

*Ustawienia → Notatnik* mówią, co zostało przejęte. Zmiana któregokolwiek ustawienia notatnika
zapisuje wszystkie (także przejęte) w `.brulion/config.json`; od tej chwili obowiązuje ten plik,
a zmiany w ustawieniach Obsidiana już się nie liczą. Zanim to nastąpi, Brulion czyta `.obsidian/`
przy każdym otwarciu notatnika i przy okresowym sprawdzaniu folderu (co parę minut i po powrocie
do aplikacji). Przycisk **Zapisz w notatniku** zapisuje przejęte ustawienia od razu — przydaje się,
gdy ten sam notatnik otwierasz w przeglądarce przez Nextcloud: synchronizacja nie przesyła
`.obsidian/`, więc bez `config.json` przeglądarka miałaby ustawienia domyślne Brulionu.

Folder, którego Brulion już używał z ustawieniami domyślnymi (bez `config.json`), nie zmienia się
po aktualizacji: dziennika nie przejmie, gdy w `journals/` leży już jakaś notatka, folderu
załączników — gdy w `assets/` leży jakiś plik, folderu szablonów — gdy w `templates/` leży jakiś
plik. Nie liczą się przy tym pliki w folderze, który dla tego ustawienia wybrał sam Obsidian (np.
dni Obsidiana w `journals/` albo `Journals/`): wtedy Brulion przejmie wzór nazw i szablon, a
swoje dawne dni `RRRR-MM-DD.md` dalej rozpozna. Brulion sprawdza to ponownie, gdy pliki w tych
folderach się pojawiają albo znikają (np. z synchronizacji na nowym urządzeniu, która przysłała
`.obsidian/` przed `journals/`). Wartość złego typu w plikach Obsidiana (np. liczba zamiast
ścieżki) Brulion pomija z ostrzeżeniem i zostaje przy swojej.

**Co ustawić raz** (*Ustawienia → Notatnik*):

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
* **Rysunki** Brulionu (`/Rysunek`) to pliki SVG z wbudowaną sceną Excalidraw: Obsidian pokazuje
  je jako obrazy, a excalidraw.com otwiera do edycji. Rysunek z Excalidraw zapisany jako SVG
  z opcją *Embed scene* („Osadź scenę”) Brulion otwiera w edytorze rysunków — pismo odręczne,
  kształty, strzałki, tekst i obrazy zmienia, a rysunek z ramkami albo osadzonymi stronami
  internetowymi tylko pokazuje. Pliki
  `*.excalidraw.md` wtyczki Excalidraw dla Obsidiana zostają tekstem (docs/SYNTAX.md §2
  „Drawings”).
* **Ulubione** (zakładki Obsidiana z `.obsidian/bookmarks.json` nie są czytane) i **zwinięcia
  bloków** Brulion pamięta na urządzeniu, nie w plikach.

**Co Brulion dopisuje do folderu:** `.brulion/config.json` (dopiero po zmianie ustawień
notatnika albo po *Zapisz w notatniku*), `.trash/` na usunięte pliki (ten sam folder, którego używa Obsidian przy opcji
*Move to Obsidian trash*; obie aplikacje go pomijają), nowe dni dziennika (dopiero po
pierwszym wpisie — samo oglądanie dnia niczego nie tworzy), wklejone obrazy w folderze
załączników, klucz `brulion: document`/`outline` w nagłówku YAML notatki, której tryb zmienisz
ręcznie (Obsidian pokaże go jako zwykłą właściwość), i identyfikator ` ^id` na końcu bloku,
do którego skopiujesz link — w formacie Obsidiana. Na prośbę (*Utwórz .stignore*, zob. niżej)
także plik `.stignore` dla Syncthinga. Historia wersji, indeks i stan
synchronizacji leżą poza folderem, więc Syncthing ich nie przesyła.

**Ryzyko konfliktów:** nie edytuj tej samej notatki w tej samej chwili w obu aplikacjach ani na
dwóch urządzeniach naraz. Brulion przejmuje zmiany z dysku na bieżąco (także w otwartej
notatce, scalając je z tym, co właśnie piszesz), a poprzednie wersje zostają w *Historii
wersji*. Gdy Syncthing mimo to zostawi kopię `*.sync-conflict-*`, scal ją w centrum
synchronizacji. Konflikty w `.obsidian/` (np. `workspace.json`) dotyczą tylko Obsidiana;
zapobiega im zalecany plik `.stignore`: *Centrum synchronizacji → Syncthing → Utwórz .stignore*
dodaje reguły pomijające układ okien Obsidiana (`.obsidian/workspace*.json`, każde urządzenie ma
własny) i folder `.trash`. Syncthing nie przesyła samego `.stignore` — skopiuj go na każde
urządzenie.

## Tagi a strony

W Brulionie tag nazywa stronę, jak w Logseq: `#praca` prowadzi do strony „praca” (szczegóły:
[SYNTAX.md](SYNTAX.md) §3). Strony tagów z Logseq (np. `pages/praca.md`) łączą się więc po
imporcie ze swoimi tagami, a tagi zagnieżdżone `#projekt/dom` — ze stronami przestrzeni nazw,
które import zapisuje w folderach (`projekt/dom.md`). W Obsidianie tagi i strony są osobne: tag
`#projekt` obok strony „Projekt” po imporcie zacznie do niej prowadzić, a jej odnośniki pokażą
bloki z tym tagiem. Przy eksporcie: Obsidian i Markdown dostają tagi bez zmian (Obsidian dalej
trzyma je osobno), Logseq — `#[[Projekty/Dom]]`, gdy strona ma tam inną nazwę niż tag, a w
eksporcie HTML tag prowadzi do strony, którą nazywa.

## Microsoft To Do

Brulion przenosi listy zadań z Microsoft To Do: każda lista staje się stroną z zadaniami
w folderze importu (`Import/Microsoft To Do/Zadania.md`, `…/Zakupy.md`). Są dwie drogi:

* **Zaloguj się w Microsoft** (zalecane) — Brulion sam pobiera wszystkie listy razem z krokami,
  linkami i plikami. Wymaga jednorazowej, bezpłatnej rejestracji własnej aplikacji w Microsoft
  Entra (opis niżej).
* **Plik z Graph Explorera** — bez rejestracji, ale bez kroków, linków i plików.

To jednorazowe przeniesienie, a nie synchronizacja: zmiany zrobione później w To Do nie trafią do
Brulionu, a w To Do nic się nie zmienia. Ponowny import tworzy strony z dopiskiem „ 2”.

### Co się z czym zamienia

| W To Do | W Brulionie |
|---|---|
| lista | strona `<folder importu>/<nazwa listy>.md` z `source: Microsoft To Do` we frontmatterze; lista bez zadań — nic (ostrzeżenie podaje, ile ich było) |
| zadanie | `- [ ] Tytuł`; znaki, które w Brulionie coś znaczą (`#`, `[[`, `*`, `$`), dostają `\` |
| w toku / czeka na innych, odroczone | `- [/]` / `- [>]` |
| ukończone | `- [x] … ✅ 2026-10-25` (dzień ukończenia, jak go zapisał To Do) — pod nieukończonymi, od ostatnio ukończonych |
| ważne / niska ważność | `⏫` / `🔽` |
| termin, data rozpoczęcia, utworzenia | `📅 2026-10-27`, `🛫 2026-10-20`, `➕ 2026-10-14` |
| przypomnienie | `⏰ 2026-10-27 08:00` (tylko w nieukończonych zadaniach) |
| powtarzanie | `🔁 every day`, `every 2 weeks on Monday, Friday`, `every weekday` (dni robocze), `every month on the 15th`, `every year`. „Drugi wtorek miesiąca” i podobne — bez 🔁, zostaje tylko termin (ostrzeżenie); koniec serii (data albo liczba powtórzeń) jest pomijany (ostrzeżenie) |
| kategorie | tagi: „Praca domowa” → `#Praca-domowa` |
| notatka | wiersze pod zadaniem (notatka w HTML — zamieniona na Markdown) |
| kroki | podpunkty `- [ ]` / `- [x]`; kroki nieodhaczone w ukończonym zadaniu — `- [-]` (anulowane), żeby nie wisiały jako otwarte zadania |
| linki (np. do wiadomości w Outlooku) | podpunkty `[nazwa](adres)` |
| pliki | załączniki w `assets/Microsoft To Do/`, osadzone pod zadaniem; większe niż 25 MB są pomijane (ostrzeżenie) |

Daty i godziny Microsoft podaje w UTC — Brulion przelicza je na strefę czasową urządzenia, więc
termin ustawiony na 27 października zostaje 27 października. Nie przenoszą się: „Mój dzień”,
ręcznie ułożona kolejność zadań (zadania idą w kolejności, w jakiej poda je Microsoft), grupy
list, osoby, którym udostępniono listę, i przypisania zadań.

### Logowanie do Microsoft

Microsoft wpuszcza do zadań tylko aplikację zarejestrowaną w Microsoft Entra. Brulion nie ma
własnego serwera ani wspólnej rejestracji — rejestrujesz „swoją” aplikację raz, za darmo, i
podajesz jej identyfikator. Rejestracja wymaga katalogu Microsoft Entra: konto służbowe lub
szkolne Microsoft 365 już go ma (rejestracje może jednak blokować administrator), a dla konta
osobistego (outlook.com, hotmail.com, live.com) katalog „Katalog domyślny” powstaje po założeniu
bezpłatnego konta Azure. Od 2024 r. Microsoft nie pozwala rejestrować aplikacji samym kontem
osobistym bez katalogu.

**Rejestracja aplikacji (raz):**

1. Otwórz [entra.microsoft.com](https://entra.microsoft.com) i zaloguj się.
2. Wybierz **Aplikacje → Rejestracje aplikacji → Nowa rejestracja** (*Applications → App
   registrations → New registration*).
3. Nazwa: np. „Brulion”. **Obsługiwane typy kont** (*Supported account types*):
   * zadania na koncie osobistym (outlook.com, hotmail.com): **Tylko osobiste konta Microsoft**
     (*Personal Microsoft accounts only*);
   * zadania na koncie służbowym: **Konta tylko w tym katalogu organizacyjnym** albo **w dowolnym
     katalogu organizacyjnym**;
   * oba rodzaje: **Konta w dowolnym katalogu organizacyjnym i osobiste konta Microsoft**.
4. **Identyfikator URI przekierowania** (*Redirect URI*) zostaw pusty i kliknij **Zarejestruj**.
5. **Uwierzytelnianie** (*Authentication*) → **Ustawienia zaawansowane** → **Zezwalaj na przepływy
   klienta publicznego** (*Allow public client flows*): **Tak**, potem **Zapisz**. Bez tego
   logowanie kończy się błędem AADSTS7000218.
6. **Uprawnienia interfejsu API** (*API permissions*) → **Dodaj uprawnienie** → **Microsoft
   Graph** → **Uprawnienia delegowane** → zaznacz `Tasks.Read` → **Dodaj uprawnienia**.
   `User.Read`, dodane domyślnie, może zostać.
7. Na stronie **Przegląd** (*Overview*) skopiuj **Identyfikator aplikacji (klienta)**
   (*Application (client) ID*), np. `3f2504e0-4f89-41d3-9a0c-0305e82c3301`.

Klucza tajnego (*client secret*) ani certyfikatu nie trzeba — Brulion loguje się jako aplikacja
publiczna, kodem urządzenia.

**Import:**

1. **Importuj notatki → Microsoft To Do**, wklej identyfikator w pole **Identyfikator aplikacji
   (klienta)**. Brulion zapamięta go na tym urządzeniu (jedna rejestracja wystarcza dla wszystkich
   notatników).
2. Pod **Więcej** jest **urząd** (*authority*): `consumers` (domyślny) dla aplikacji tylko dla
   kont osobistych, `organizations` albo identyfikator lub domena katalogu
   (`firma.onmicrosoft.com`) dla kont służbowych, `common`, gdy aplikacja przyjmuje oba rodzaje
   kont.
3. Kliknij **Zaloguj się w Microsoft**. Brulion pokaże kod (np. `F7GH2K9QX`). Otwórz
   [microsoft.com/devicelogin](https://microsoft.com/devicelogin) — na tym samym albo innym
   urządzeniu, np. w telefonie —, wpisz kod, zaloguj się kontem, na którym są zadania, i zgódź
   się na odczyt zadań. Kod jest ważny około 15 minut.
4. Brulion pobiera listy (pasek pokazuje postęp), a potem zwykły podgląd importu. Nic nie jest
   zapisywane przed zatwierdzeniem. Wybranie w tym czasie pliku albo wpisanie ścieżki przerywa
   logowanie — importuje się wtedy to, co wskazano.

Bezpieczeństwo: Brulion prosi tylko o odczyt zadań (`Tasks.Read`) i łączy się tylko
z `login.microsoftonline.com` i `graph.microsoft.com`. Token dostępu jest w pamięci tylko na
czas pobierania — nie trafia do notatnika, ustawień ani rejestru zdarzeń, a Brulion nie prosi
o token odświeżania, więc każdy import to nowe logowanie. Zapamiętane są tylko identyfikator
aplikacji i urząd. Zgodę cofniesz na koncie Microsoft (konto osobiste:
[account.live.com/consent/Manage](https://account.live.com/consent/Manage)) albo usuwając
rejestrację aplikacji.

| Błąd | Co zrobić |
|---|---|
| AADSTS7000218 | w rejestracji aplikacji włącz **Zezwalaj na przepływy klienta publicznego** (krok 5) |
| AADSTS700016 | sprawdź identyfikator; aplikacja tylko dla kont osobistych wymaga urzędu `consumers` |
| AADSTS50059, AADSTS9002331, AADSTS50020 | rodzaj konta nie pasuje do rejestracji albo urzędu: konto osobiste — `consumers` i rejestracja dla kont osobistych; konto służbowe — `organizations` albo katalog firmy |
| AADSTS65001 | brak zgody na `Tasks.Read`: dodaj uprawnienie (krok 6); w firmie zgodę może musieć dać administrator |
| „Brulion nie może czytać Twoich zadań” | brak uprawnienia `Tasks.Read` w rejestracji — dodaj je i zaloguj się ponownie |
| „Microsoft To Do jest niedostępny dla tego konta” | konto nie ma skrzynki Exchange Online, w której To Do trzyma zadania |

### Bez rejestracji: plik z Graph Explorera

[Graph Explorer](https://developer.microsoft.com/graph/graph-explorer) to strona Microsoftu do
wysyłania zapytań do Microsoft Graph — nie wymaga własnej rejestracji aplikacji.

1. Otwórz Graph Explorera i zaloguj się (**Sign in**) kontem, na którym są zadania.
2. **Modify permissions** → przy `Tasks.Read` kliknij **Consent** i zgódź się.
3. Wpisz zapytanie `GET` `https://graph.microsoft.com/v1.0/me/todo/lists?$expand=tasks`
   i kliknij **Run query**.
4. Skopiuj całą odpowiedź (**Response preview**) do pliku tekstowego z rozszerzeniem `.json`
   (np. `To Do.json`, kodowanie UTF-8).
5. W Brulionie: **Importuj notatki → Microsoft To Do → Wybierz pliki…** i wskaż ten plik.

Taki plik nie ma kroków, linków ani plików (Brulion powie, że załączniki zostały w To Do).
Przy dużych listach Microsoft może oddać tylko część zadań — Brulion ostrzeże, że eksport jest
niepełny. Wtedy pobierz zadania każdej listy osobno: w odpowiedzi znajdź `id` listy, uruchom
`GET https://graph.microsoft.com/v1.0/me/todo/lists/<id listy>/tasks`, zapisz odpowiedź jako
`<nazwa listy>.json` (strona dostanie nazwę pliku) i zaimportuj wszystkie pliki naraz — można też
spakować je do ZIP-a. Także taka odpowiedź bywa podzielona na części; jeśli Brulion ostrzeże, że
plik ma tylko pierwsze zadania listy, całość pobierze **Zaloguj się w Microsoft**.

W wersji w przeglądarce logowanie do Microsoft nie działa (Microsoft nie pozwala stronom WWW
logować się kodem urządzenia) — tam użyj pliku z Graph Explorera.

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
