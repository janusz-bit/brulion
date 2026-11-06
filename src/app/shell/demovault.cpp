#include "demovault.h"

#include "markdown/journal.h"
#include "text/text.h"
#include "vault/vault.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

QString iso(QDate d)
{
    return d.toString(Qt::ISODate);
}

/// The next `dayOfWeek` strictly after `from`.
QDate nextWeekday(QDate from, int dayOfWeek)
{
    int delta = (dayOfWeek - from.dayOfWeek() + 7) % 7;
    return from.addDays(delta == 0 ? 7 : delta);
}

/// The next 10th of a month (today when it is the 10th).
QDate nextTenth(QDate from)
{
    if (from.day() <= 10)
        return QDate(from.year(), from.month(), 10);
    const QDate next = from.addMonths(1);
    return QDate(next.year(), next.month(), 10);
}

QString fill(QString text, const QHash<QString, QString> &vars)
{
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        text.replace(u'{' + it.key() + u'}', it.value());
    return text;
}

/// The documentation pages in `language` ("en" | "pl": src/app/shell/guide/<language>/*.md,
/// embedded as :/guide/<language>/...), written in Brulion's own Markdown: the notes of the
/// notebook "Brulion Documentation" (in Polish "Dokumentacja Brulionu"). Paths are relative to the
/// language's folder.
QList<DemoFile> guideFiles(const QString &language)
{
    QList<DemoFile> out;
    const QString root = u":/guide/"_s + language + u'/';
    QDirIterator it(root, { u"*.md"_s }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString path = it.filePath().mid(root.size());
        out.append({ path, QString::fromUtf8(file.readAll()) });
    }
    std::sort(out.begin(), out.end(), [](const DemoFile &a, const DemoFile &b) { return a.path < b.path; });
    return out;
}

// ---- Content: Polish ---------------------------------------------------------------------------

namespace pl {

const auto kWelcome = uR"md(---
tags: [przewodnik]
aliases: [Przewodnik, Start]
brulion: document
---
# Witaj w Brulionie

Brulion to notatnik w zwykłych plikach Markdown. Działa bez internetu — na komputerze, na telefonie i w przeglądarce. To jest **przykładowy brulion**: wszystko tutaj możesz edytować, klikać i psuć.

> [!tip] Od czego zacząć?
> Otwórz [[{today}|dzisiejszy dziennik]] (Ctrl+J) i zapisz pierwszą myśl. Każdy punkt listy (linia zaczynająca się od myślnika) to osobny blok.

## Dokument i konspekt
Każda notatka ma jeden z dwóch trybów. Przełączysz go ikoną obok tytułu strony, w menu strony albo poleceniem „Przełącz tryb notatki” (Ctrl+K).

- **Dokument**, jak w Obsidianie — tak wygląda ta strona. Akapity, nagłówki i listy czyta się jak zwykły tekst; kliknij akapit, żeby go zmienić.
- **Konspekt**, jak w Logsequ — tak wyglądają dziennik i [[Remont mieszkania]]. Każdy punkt to blok z kropką, który można wcinać, zwijać i przybliżać.

Tryb jest zapisany w samej notatce (właściwość `brulion: document` albo `brulion: outline`), więc na każdym urządzeniu wygląda tak samo. Nową stronę od razu tworzysz w wybranym trybie, a domyślne tryby ustawisz w Ustawieniach → Notatnik.

## Konspekt
- Każdy punkt listy to **blok**. Kliknij go, żeby edytować.
  - Tab wcina blok razem z jego dziećmi, Shift+Tab go wysuwa.
  - Alt+↑ i Alt+↓ przesuwają blok w górę i w dół.
    - W konspekcie kliknij kropkę przy bloku, żeby się do niego przybliżyć.
- Enter dzieli blok w miejscu kursora; w dokumencie Enter w pustym punkcie kończy listę.
- Ctrl+. zwija i rozwija dzieci bloku w konspekcie.

## Linki i odwołania
- Link do strony: [[Remont mieszkania]]. Strona jeszcze nie istnieje? Kliknięcie ją utworzy: [[Pomysły na prezenty]].
- Link z własnym podpisem: [[Książki 2026|moja lista lektur]].
- Link do nagłówka: [[Przepisy#Szarlotka babci]].
- Odwołanie do bloku z innej strony: ((budzet1)) — tekst pojawia się tutaj, a zmieniasz go u źródła.
- Każda strona pokazuje w panelu bocznym **odnośniki zwrotne**, czyli strony, które do niej prowadzą.

## Tagi
- Tagi zaczynają się od #: #przewodnik, #pomysł, #projekt/dom (tag zagnieżdżony).
- Tag ze spacjami: #[[do przeczytania]].
- Tag to link do strony: kliknij go, a otworzy się jego strona, pod którą są wszystkie bloki z tym tagiem.

## Zadania
- [ ] Zwykłe zadanie — kliknij pole wyboru albo naciśnij Ctrl+Enter; prawy przycisk myszy (na telefonie przytrzymanie) pokaże wszystkie stany
- [ ] Zadanie z terminem 📅 {in2}
- [ ] Zadanie zaplanowane na jutro ⏳ {tomorrow}
- [/] Zadanie w toku
- [x] Zadanie wykonane ✅ {yesterday}
- [ ] Ważne zadanie ⏫ 📅 {in3}
- [ ] Podlać kwiaty 🔁 every week on Saturday 📅 {nextSaturday}
- Po odhaczeniu zadania cyklicznego Brulion sam dopisze jego następne wystąpienie. Reguły powtarzania można wpisywać po polsku: „co tydzień”, „co 2 dni”, „w dni robocze”.

## Przypomnienia i alarmy
- Zadzwonić do mamy ⏰ {tomorrow} 18:00
- [ ] Oddać książkę do biblioteki 📅 {in3} ⏰ {in2} 09:00
- Pobudka na pociąg ⏰! {nextMonday} 05:45
  - Z wykrzyknikiem to **alarm**: głośny i dzwoni, dopóki go nie wyłączysz.
- Przypomnienie bez godziny ⏰ {in5}
  - Zadzwoni o domyślnej godzinie z ustawień notatnika.
- Daty wpisuj po polsku: „jutro o 9”, „w piątek 18:00”, „za 2 godziny” — w pliku zostaje zawsze `RRRR-MM-DD GG:MM`.
- Na telefonie przypomnienia działają nawet przy zamkniętej aplikacji.

## Obliczenia
Blok `calc` liczy linia po linii, z jednostkami, procentami i przecinkiem dziesiętnym:

```calc
cena = 12,50
ilość = 4
cena * ilość
rabat = 15%
ans - rabat
5 km to mi
3 h + 20 min
20% z 150
```

Rachunki miesięczne z etykietami i sumą:

```calc
Czynsz: 2400 zł
Prąd: 180 zł
Internet: 60 zł
suma
```

Obliczenie w tekście też działa: rok ma `= 52 * 7` dni w pełnych tygodniach.

## Tabele z formułami
Komórka zaczynająca się od `=` to formuła: `[Kolumna]` to komórka z tego samego wiersza, `above` — wszystko powyżej.

| Produkt | Cena | Ilość | Razem |
|---|---:|---:|---:|
| Farba | 89 | 3 | =[Cena]*[Ilość] |
| Wałek | 25 | 2 | =[Cena]*[Ilość] |
| Taśma | 12 | 4 | =[Cena]*[Ilość] |
| Suma | | | =sum(above) |

## Zapytania
Blok `query` zbiera zadania i bloki z całego notatnika. Otwarte zadania na najbliższy tydzień:

```query
type:tasks status:open due:<=+7d sort:due
```

## Osadzenia
- Osadzona sekcja innej strony: ![[Przepisy#Szarlotka babci]]
- Obraz z folderu załączników: ![[plan-mieszkania.svg]]
- Osadzony blok: !((budzet1))

## Ramki (callouty)
> [!note] Notatka
> Ramki mają rodzaje: note, tip, warning, danger, info, todo, quote.

> [!warning] Zapisywanie
> Zmiany zapisują się same — nie ma przycisku „Zapisz”. Poprzednie wersje strony znajdziesz w jej historii.

## Szablony
- Szablony leżą w folderze `templates/`. Nowy dziennik powstaje z szablonu [[templates/journal|journal]].
- Zmienne: `{{date}}`, `{{time}}`, `{{weekday}}`, `{{title}}`. Szablon wstawisz poleceniem `/szablon`.

## Kalendarz i dziennik
- Każdy dzień ma własną stronę w folderze `journals/` — zajrzyj do [[{yesterday}]] i [[{weekAgo}]].
- Kalendarz pokazuje terminy 📅, zaplanowane ⏳, przypomnienia ⏰ i dni z wpisami w dzienniku.

## Synchronizacja
- **Syncthing**: notatnik to zwykły folder. Udostępnij go w Syncthing (na Androidzie w Syncthing-Fork) i wskaż ten sam folder w Brulionie na każdym urządzeniu. Kopie konfliktowe (`*.sync-conflict-*`) Brulion pomaga scalić.
- **Nextcloud**: Ustawienia → Synchronizacja — adres serwera, użytkownik i hasło aplikacji. Zmiany z kilku urządzeń są scalane, a tekst nigdy nie ginie.
- Historia zmian każdej strony jest zapisywana na urządzeniu: menu strony → Historia.

## Import i eksport
- Import z Obsidiana, Logseqa, Notion, Evernote, Google Keep, Joplina, Simplenote, Roama, Beara oraz plików Markdown, TXT i HTML.
- Eksport do archiwum ZIP z Markdownem, strony HTML, JSON, iCalendar (zadania i przypomnienia), OPML i CSV.

## Dokumentacja
{docs}

## Skróty klawiszowe
| Skrót | Działanie |
|---|---|
| Ctrl+K | paleta poleceń |
| Ctrl+O | przejdź do strony |
| Ctrl+J | dzisiejszy dziennik |
| Ctrl+N | nowa strona |
| Ctrl+Shift+F | szukaj |
| Alt+← / Alt+→ | wstecz / dalej |
| Ctrl+E | tryb źródła Markdown |
| Ctrl+Enter | zmień status zadania |
| Ctrl+Shift+Enter | menu statusu zadania |
| Tab / Shift+Tab | wcięcie / wysunięcie bloku |

%%Ten komentarz widać tylko podczas edycji.%%
==Zakreślenie== i ~~przekreślenie~~ też działają.
)md"_s;

/// The welcome page's "Dokumentacja" section: where the documentation notebook is ...
const auto kDocsNotebook =
        uR"md(Pełna dokumentacja jest w osobnym notatniku „Dokumentacja Brulionu” (tryby, linki, zadania, przypomnienia, obliczenia, zapytania, synchronizacja, skróty). Otworzysz go przyciskiem „Przełącz notatnik” (dwie strzałki) obok nazwy notatnika w pasku bocznym albo skrótem Ctrl+Shift+B — i tak samo wrócisz.)md"_s;
/// ... and where it is read without one (the browser).
const auto kDocsOnline =
        uR"md(Pełna dokumentacja (tryby, linki, zadania, przypomnienia, obliczenia, zapytania, synchronizacja, skróty) jest częścią aplikacji na komputer i na Androida, gdzie tworzy osobny notatnik „Dokumentacja Brulionu”. W przeglądarce przeczytasz ją na [stronie projektu](https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/pl).)md"_s;

const auto kToday = uR"md(- Pierwszy dzień z [[Witaj w Brulionie|Brulionem]] #przewodnik
- [ ] Przejrzeć [[Remont mieszkania]] i zamówić farbę 📅 {in2}
- Spotkanie z ekipą remontową ⏰ {tomorrow} 17:30
- Przeczytane dziś: 40 stron „Lalki” — [[Książki 2026]]
- Pomysł na obiad: [[Przepisy#Pierogi ruskie|pierogi ruskie]] #pomysł
  - albo żurek, jeśli będzie zakwas
- Szybkie notatki z telefonu (przycisk „+”) trafiają właśnie tutaj.
)md"_s;

const auto kYesterday = uR"md(- [x] Kupić taśmę malarską ✅ {yesterday}
- Rozmowa z sąsiadem o hałasie podczas remontu — [[Remont mieszkania]]
  - Ustaliliśmy prace w godzinach 9–17.
- Spacer po parku, 8 km #zdrowie
- Wydatki dnia:

```calc
Kawa: 14 zł
Bilet: 4,40 zł
Zakupy: 86,30 zł
suma
```
)md"_s;

const auto kWeekAgo = uR"md(- Początek remontu! #projekt/dom
  - Plan: ![[plan-mieszkania.svg]]
- [x] Wybrać kolor ścian w salonie ✅ {weekAgo}
- Skończona „Ferdydurke” — [[Książki 2026]]
- Żółć, ćma, źdźbło, łąka — Brulion lubi polskie znaki.
)md"_s;

const auto kRemont = uR"md(---
tags: [projekt/dom]
status: w toku
brulion: outline
---
# Remont mieszkania

- Budżet całkowity to 12 000 zł — nie przekraczamy! ^budzet1
- Ekipa zaczyna o 9:00, kończy o 17:00 (umowa z sąsiadami z [[{yesterday}]]).

## Kosztorys
| Pozycja | Cena | Ilość | Razem |
|---|---:|---:|---:|
| Farba 10 l | 189 | 3 | =[Cena]*[Ilość] |
| Panele (m²) | 65 | 32 | =[Cena]*[Ilość] |
| Listwy (mb) | 18 | 40 | =[Cena]*[Ilość] |
| Robocizna | 4500 | 1 | =[Cena]*[Ilość] |
| **Razem** | | | =sum(above) |

Ile zostaje w budżecie:

```calc
budżet = 12000
farba = 189 * 3
panele = 65 * 32
listwy = 18 * 40
robocizna = 4500
budżet - farba - panele - listwy - robocizna
```

## Zadania
- [x] Wybrać kolor ścian ✅ {weekAgo}
- [ ] Zamówić farbę ⏫ 📅 {in2}
- [ ] Zerwać stare panele ⏳ {in3}
- [ ] Odebrać listwy z marketu 📅 {in5} ⏰ {in5} 16:00
- [ ] Zapłacić ratę za robociznę 🔁 every month on the 10th 📅 {next10th}

## Notatki
- Kolory: „Szałwiowa zieleń” w salonie, ciepła biel w kuchni. #pomysł
- Wszystkie otwarte zadania projektu zbiera zapytanie poniżej.

```query
type:tasks path:Projekty status:open sort:due
```
)md"_s;

const auto kBooks = uR"md(---
tags: [czytanie]
---
# Książki 2026

| Tytuł | Autor | Strony | Ocena |
|---|---|---:|---:|
| Lalka | Bolesław Prus | 680 | 5 |
| Ferdydurke | Witold Gombrowicz | 320 | 4 |
| Solaris | Stanisław Lem | 240 | 5 |
| Bieguni | Olga Tokarczuk | 420 | 4 |
| **Razem / średnia** | | =sum(above) | =avg(above) |

## Do przeczytania
- [/] Lalka — Bolesław Prus #[[do przeczytania]]
  - Czytam 40 stron dziennie.
- [ ] Księgi Jakubowe — Olga Tokarczuk #[[do przeczytania]]
- [ ] Cesarz — Ryszard Kapuściński #[[do przeczytania]]

Ile dni zostało do końca „Lalki”?

```calc
strony = 680
przeczytane = 200
(strony - przeczytane) / 40
```

> [!quote] Stanisław Lem
> Nie chcemy zdobywać kosmosu, chcemy tylko rozszerzyć Ziemię do jego granic.
)md"_s;

const auto kRecipes = uR"md(---
tags: [kuchnia]
---
# Przepisy

## Pierogi ruskie
- Składniki (ok. 40 sztuk):
  - 500 g mąki, 250 ml ciepłej wody, 2 łyżki oleju
  - 1 kg ziemniaków, 300 g twarogu, 2 cebule
- Zagnieść ciasto i odstawić na 30 minut.
- Gotować 2–3 minuty od wypłynięcia.

## Szarlotka babci
- Jabłka: szara reneta, dużo cynamonu.
- Kruche ciasto: 300 g mąki, 200 g masła, 100 g cukru, 3 żółtka.

> [!tip] Na większą blachę
> Pomnóż składniki przez 1,5:

```calc
mąka = 300
masło = 200
mąka * 1,5
masło * 1,5
```

## Żurek
- [ ] Kupić zakwas na żurek #zakupy ⏰ {nextSaturday} 10:00
- Biała kiełbasa, jajka, majeranek, chrzan.
)md"_s;

const auto kJournalTemplate = uR"md(## Plan dnia
- [ ]

## Notatki
-
)md"_s;

const auto kMeetingTemplate = uR"md(---
tags: [spotkanie]
---
# {{title}}
Data: {{date}} {{time}}

## Uczestnicy
-

## Ustalenia
-

## Zadania
- [ ]
)md"_s;

const auto kPlanSvg = uR"svg(<svg xmlns="http://www.w3.org/2000/svg" width="480" height="300" viewBox="0 0 480 300">
  <rect x="0" y="0" width="480" height="300" fill="#fbf7ee"/>
  <g fill="none" stroke="#1f2430" stroke-width="4">
    <rect x="20" y="20" width="440" height="260"/>
    <line x1="260" y1="20" x2="260" y2="180"/>
    <line x1="20" y1="180" x2="200" y2="180"/>
    <line x1="320" y1="180" x2="460" y2="180"/>
  </g>
  <g fill="#9fc3e6" opacity="0.35">
    <rect x="22" y="22" width="236" height="156"/>
  </g>
  <g font-family="sans-serif" font-size="18" fill="#1f2430">
    <text x="90" y="105">Salon</text>
    <text x="320" y="105">Kuchnia</text>
    <text x="70" y="240">Sypialnia</text>
    <text x="330" y="240">Łazienka</text>
  </g>
  <text x="240" y="296" font-family="sans-serif" font-size="12" fill="#e2574c" text-anchor="middle">plan mieszkania — remont</text>
</svg>
)svg"_s;

} // namespace pl

// ---- Content: English --------------------------------------------------------------------------

namespace en {

const auto kWelcome = uR"md(---
tags: [guide]
aliases: [Guide, Start]
brulion: document
---
# Welcome to Brulion

Brulion is a notebook made of plain Markdown files. It works offline — on your computer, on your phone and in the browser. This is the **sample notebook**: everything here is yours to edit, click and break.

> [!tip] Where to start?
> Open [[{today}|today's journal]] (Ctrl+J) and write down your first thought. Every list item (a line starting with a dash) is a block of its own.

## Documents and outlines
Every note is in one of two modes. Switch it with the icon next to the page title, in the page menu or with the “Toggle note mode” command (Ctrl+K).

- A **document**, as in Obsidian — like this page. Paragraphs, headings and lists read like ordinary text; click a paragraph to change it.
- An **outline**, as in Logseq — like the journal and [[Home renovation]]. Every item is a block with a bullet that you can indent, collapse and zoom into.

The mode is stored in the note itself (the property `brulion: document` or `brulion: outline`), so the note looks the same on every device. A new page can start in either mode, and Settings → Notebook sets the default ones.

## Outliner
- Every list item is a **block**. Click it to edit it.
  - Tab indents a block together with its children, Shift+Tab outdents it.
  - Alt+↑ and Alt+↓ move a block up and down.
    - In an outline, click the bullet of a block to zoom into it.
- Enter splits the block at the cursor; in a document, Enter on an empty item ends the list.
- Ctrl+. collapses and expands the children of a block in an outline.

## Links and references
- A link to a page: [[Home renovation]]. The page does not exist yet? A click creates it: [[Gift ideas]].
- A link with a label of its own: [[Books 2026|my reading list]].
- A link to a heading: [[Recipes#Grandma's apple pie]].
- A reference to a block on another page: ((budget1)) — its text shows up here, and you change it at the source.
- Every page lists its **backlinks** in the side panel: the pages that link to it.

## Tags
- Tags start with #: #guide, #idea, #project/home (a nested tag).
- A tag with spaces: #[[to read]].
- A tag is a link to a page: click it to open its page, with every block that has the tag below it.

## Tasks
- [ ] A plain task — click the checkbox or press Ctrl+Enter; a right-click (a long press on a phone) shows every status
- [ ] A task with a due date 📅 {in2}
- [ ] A task scheduled for tomorrow ⏳ {tomorrow}
- [/] A task in progress
- [x] A finished task ✅ {yesterday}
- [ ] An important task ⏫ 📅 {in3}
- [ ] Water the plants 🔁 every week on Saturday 📅 {nextSaturday}
- When you tick off a recurring task, Brulion adds its next occurrence by itself. Repeat rules are written in plain English: “every week”, “every 2 days”, “every weekday”.

## Reminders and alarms
- Call Mom ⏰ {tomorrow} 18:00
- [ ] Return the book to the library 📅 {in3} ⏰ {in2} 09:00
- Wake up for the train ⏰! {nextMonday} 05:45
  - With an exclamation mark it is an **alarm**: loud, and it keeps ringing until you turn it off.
- A reminder without a time ⏰ {in5}
  - It goes off at the default time from the notebook's settings.
- Type dates in plain English: “tomorrow at 9”, “friday at 6pm”, “in 2 hours” — the file always keeps `YYYY-MM-DD HH:MM`.
- On a phone, reminders work even when the app is closed.

## Calculations
A `calc` block calculates line by line, with units, percentages and variables:

```calc
price = 12.50
quantity = 4
price * quantity
discount = 15%
ans - discount
5 km to mi
3 h + 20 min
20% of 150
```

Monthly bills with labels and a total:

```calc
Rent: $1800
Electricity: $90
Internet: $45
sum
```

A calculation inside a sentence works too: a year has `= 52 * 7` days in full weeks.

## Tables with formulas
A cell that starts with `=` is a formula: `[Column]` is the cell of that column in the same row, `above` — everything above it.

| Item | Price | Qty | Total |
|---|---:|---:|---:|
| Paint | 32 | 3 | =[Price]*[Qty] |
| Roller | 9 | 2 | =[Price]*[Qty] |
| Tape | 4 | 4 | =[Price]*[Qty] |
| Sum | | | =sum(above) |

## Queries
A `query` block gathers tasks and blocks from the whole notebook. Open tasks for the coming week:

```query
type:tasks status:open due:<=+7d sort:due
```

## Embeds
- A section of another page, embedded: ![[Recipes#Grandma's apple pie]]
- A picture from the attachments folder: ![[floor-plan.svg]]
- An embedded block: !((budget1))

## Callouts
> [!note] Note
> Callouts come in several kinds: note, tip, warning, danger, info, todo, quote.

> [!warning] Saving
> Changes save themselves — there is no “Save” button. Earlier versions of a page are in its history.

## Templates
- Templates live in the `templates/` folder. A new journal page is made from the [[templates/journal|journal]] template.
- Variables: `{{date}}`, `{{time}}`, `{{weekday}}`, `{{title}}`. Insert a template with the `/template` command.

## Calendar and journal
- Every day has a page of its own in the `journals/` folder — take a look at [[{yesterday}]] and [[{weekAgo}]].
- The calendar shows due dates 📅, scheduled dates ⏳, reminders ⏰ and the days with journal entries.

## Sync
- **Syncthing**: a notebook is an ordinary folder. Share it in Syncthing (on Android in Syncthing-Fork) and choose the same folder in Brulion on every device. Brulion helps you merge conflict copies (`*.sync-conflict-*`).
- **Nextcloud**: Settings → Sync — the server address, your user name and an app password. Changes from several devices are merged, and no text is ever lost.
- The history of every page is kept on the device: page menu → History.

## Import and export
- Import from Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam and Bear, and from Markdown, TXT and HTML files.
- Export to a ZIP archive of Markdown, to HTML pages, JSON, iCalendar (tasks and reminders), OPML and CSV.

## Documentation
{docs}

## Keyboard shortcuts
| Shortcut | Action |
|---|---|
| Ctrl+K | command palette |
| Ctrl+O | go to page |
| Ctrl+J | today's journal |
| Ctrl+N | new page |
| Ctrl+Shift+F | search |
| Alt+← / Alt+→ | back / forward |
| Ctrl+E | Markdown source mode |
| Ctrl+Enter | change the task status |
| Ctrl+Shift+Enter | task status menu |
| Tab / Shift+Tab | indent / outdent a block |

%%This comment is only visible while you edit.%%
==Highlights== and ~~strikethrough~~ work too.
)md"_s;

/// The welcome page's "Documentation" section: where the documentation notebook is ...
const auto kDocsNotebook =
        uR"md(The full documentation is in a separate notebook, “Brulion Documentation” (modes, links, tasks, reminders, calculations, queries, sync, shortcuts). Open it with the “Switch notebook” button (the first letter of the notebook's name at the top of the icon bar on the left) or with Ctrl+Shift+B — and come back the same way.)md"_s;
/// ... and where it is read without one (the browser).
const auto kDocsOnline =
        uR"md(The full documentation (modes, links, tasks, reminders, calculations, queries, sync, shortcuts) comes with the desktop and Android apps, where it is a separate notebook, “Brulion Documentation”. In the browser, read it on the [project page](https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/en).)md"_s;

const auto kToday = uR"md(- My first day with [[Welcome to Brulion|Brulion]] #guide
- [ ] Go through [[Home renovation]] and order the paint 📅 {in2}
- Meeting with the renovation crew ⏰ {tomorrow} 17:30
- Read today: 40 pages of “Middlemarch” — [[Books 2026]]
- Dinner idea: [[Recipes#Pancakes|pancakes]] #idea
  - or tomato soup, if I get fresh basil
- Quick notes from the phone (the “+” button) land right here.
)md"_s;

const auto kYesterday = uR"md(- [x] Buy masking tape ✅ {yesterday}
- Talked to the neighbor about the noise during the renovation — [[Home renovation]]
  - We agreed on working hours: 9:00–17:00 only.
- A walk in the park, 8 km #health
- Today's expenses:

```calc
Coffee: $4.50
Bus ticket: $2.75
Groceries: $38.20
sum
```
)md"_s;

const auto kWeekAgo = uR"md(- The renovation starts! #project/home
  - Plan: ![[floor-plan.svg]]
- [x] Choose the color of the living room walls ✅ {weekAgo}
- Finished “Frankenstein” — [[Books 2026]]
- Café, naïve, Zürich, Łódź, żółć — Brulion is at home with any alphabet.
)md"_s;

const auto kRenovation = uR"md(---
tags: [project/home]
status: in progress
brulion: outline
---
# Home renovation

- The total budget: 12,000 dollars — not a cent more! ^budget1
- The crew starts at 9:00 and finishes at 17:00 (as agreed with the neighbors on [[{yesterday}]]).

## Cost estimate
| Item | Price | Qty | Total |
|---|---:|---:|---:|
| Paint (gallon) | 45 | 6 | =[Price]*[Qty] |
| Flooring (sq ft) | 4 | 350 | =[Price]*[Qty] |
| Baseboards (ft) | 2 | 400 | =[Price]*[Qty] |
| Labor | 4500 | 1 | =[Price]*[Qty] |
| **Total** | | | =sum(above) |

What is left of the budget:

```calc
budget = 12000
paint = 45 * 6
flooring = 4 * 350
baseboards = 2 * 400
labor = 4500
budget - paint - flooring - baseboards - labor
```

## Tasks
- [x] Choose the wall colors ✅ {weekAgo}
- [ ] Order the paint ⏫ 📅 {in2}
- [ ] Pull up the old floor ⏳ {in3}
- [ ] Pick up the baseboards from the store 📅 {in5} ⏰ {in5} 16:00
- [ ] Pay the crew's monthly installment 🔁 every month on the 10th 📅 {next10th}

## Notes
- Colors: “Sage green” in the living room, warm white in the kitchen. #idea
- The query below gathers every open task of the project.

```query
type:tasks path:Projects status:open sort:due
```
)md"_s;

const auto kBooks = uR"md(---
tags: [reading]
---
# Books 2026

| Title | Author | Pages | Rating |
|---|---|---:|---:|
| Frankenstein | Mary Shelley | 280 | 4 |
| Solaris | Stanisław Lem | 204 | 5 |
| The Left Hand of Darkness | Ursula K. Le Guin | 304 | 5 |
| Pride and Prejudice | Jane Austen | 432 | 4 |
| **Total / average** | | =sum(above) | =avg(above) |

## To read
- [/] Middlemarch — George Eliot #[[to read]]
  - Reading 40 pages a day.
- [ ] Dune — Frank Herbert #[[to read]]
- [ ] The Remains of the Day — Kazuo Ishiguro #[[to read]]

How many days until I finish “Middlemarch”?

```calc
pages = 880
finished = 200
(pages - finished) / 40
```

> [!quote] Stanisław Lem, Solaris
> We don't want to conquer the cosmos, we simply want to extend the boundaries of Earth to the frontiers of the cosmos.
)md"_s;

const auto kRecipes = uR"md(---
tags: [cooking]
---
# Recipes

## Pancakes
- Ingredients (about 12 pancakes):
  - 200 g flour, 300 ml milk, 1 egg, 3 tablespoons of melted butter
  - 1 tablespoon of sugar, 2 teaspoons of baking powder, a pinch of salt
- Whisk the wet and the dry ingredients separately, then stir them together — a few lumps are fine.
- Cook on a hot pan until bubbles appear, then flip.

## Grandma's apple pie
- Apples: tart ones, like Granny Smith, and plenty of cinnamon.
- Shortcrust pastry: 300 g flour, 200 g butter, 100 g sugar, 3 egg yolks.

> [!tip] For a bigger baking tin
> Multiply the ingredients by 1.5:

```calc
flour = 300
butter = 200
flour * 1.5
butter * 1.5
```

## Tomato soup
- [ ] Buy fresh basil #shopping ⏰ {nextSaturday} 10:00
- Ripe tomatoes, an onion, two cloves of garlic, a splash of cream.
)md"_s;

const auto kJournalTemplate = uR"md(## Plan for the day
- [ ]

## Notes
-
)md"_s;

const auto kMeetingTemplate = uR"md(---
tags: [meeting]
---
# {{title}}
Date: {{date}} {{time}}

## Attendees
-

## Decisions
-

## Action items
- [ ]
)md"_s;

const auto kPlanSvg = uR"svg(<svg xmlns="http://www.w3.org/2000/svg" width="480" height="300" viewBox="0 0 480 300">
  <rect x="0" y="0" width="480" height="300" fill="#fbf7ee"/>
  <g fill="none" stroke="#1f2430" stroke-width="4">
    <rect x="20" y="20" width="440" height="260"/>
    <line x1="260" y1="20" x2="260" y2="180"/>
    <line x1="20" y1="180" x2="200" y2="180"/>
    <line x1="320" y1="180" x2="460" y2="180"/>
  </g>
  <g fill="#9fc3e6" opacity="0.35">
    <rect x="22" y="22" width="236" height="156"/>
  </g>
  <g font-family="sans-serif" font-size="18" fill="#1f2430">
    <text x="80" y="105">Living room</text>
    <text x="320" y="105">Kitchen</text>
    <text x="70" y="240">Bedroom</text>
    <text x="330" y="240">Bathroom</text>
  </g>
  <text x="240" y="296" font-family="sans-serif" font-size="12" fill="#e2574c" text-anchor="middle">floor plan — renovation</text>
</svg>
)svg"_s;
} // namespace en

} // namespace

namespace {

/// The sample notebook's texts and paths in one language.
struct SampleTexts {
    QStringView welcome, docsNotebook, docsOnline, today, yesterday, weekAgo;
    QStringView projectPath, project, booksPath, books, recipesPath, recipes;
    QStringView journalTemplate, meetingTemplatePath, meetingTemplate, picturePath, picture;
};

bool isPolish(QStringView language)
{
    return language == u"pl";
}

SampleTexts sampleTexts(QStringView language)
{
    if (isPolish(language))
        return {pl::kWelcome, pl::kDocsNotebook, pl::kDocsOnline, pl::kToday, pl::kYesterday, pl::kWeekAgo,
                u"Projekty/Remont mieszkania.md", pl::kRemont, u"Książki 2026.md", pl::kBooks, u"Przepisy.md",
                pl::kRecipes, pl::kJournalTemplate, u"templates/Spotkanie.md", pl::kMeetingTemplate,
                u"assets/plan-mieszkania.svg", pl::kPlanSvg};
    return {en::kWelcome, en::kDocsNotebook, en::kDocsOnline, en::kToday, en::kYesterday, en::kWeekAgo,
            u"Projects/Home renovation.md", en::kRenovation, u"Books 2026.md", en::kBooks, u"Recipes.md",
            en::kRecipes, en::kJournalTemplate, u"templates/Meeting.md", en::kMeetingTemplate,
            u"assets/floor-plan.svg", en::kPlanSvg};
}

QString fillView(QStringView text, const QHash<QString, QString> &vars)
{
    return fill(text.toString(), vars);
}

} // namespace

QString sampleNotebookFolder(QStringView language)
{
    return isPolish(language) ? u"Przykładowy brulion"_s : u"Sample notebook"_s;
}

QString demoWelcomePage(QStringView language)
{
    return isPolish(language) ? u"Witaj w Brulionie.md"_s : u"Welcome to Brulion.md"_s;
}

QList<DemoFile> demoVaultFiles(const QDateTime &now, QStringView language, bool docsNotebook)
{
    const SampleTexts texts = sampleTexts(language);
    const QDate today = now.date();
    const QHash<QString, QString> vars{
            {u"today"_s, iso(today)},
            {u"yesterday"_s, iso(today.addDays(-1))},
            {u"weekAgo"_s, iso(today.addDays(-7))},
            {u"tomorrow"_s, iso(today.addDays(1))},
            {u"in2"_s, iso(today.addDays(2))},
            {u"in3"_s, iso(today.addDays(3))},
            {u"in5"_s, iso(today.addDays(5))},
            {u"nextMonday"_s, iso(nextWeekday(today, Qt::Monday))},
            {u"nextSaturday"_s, iso(nextWeekday(today, Qt::Saturday))},
            {u"next10th"_s, iso(nextTenth(today))},
            {u"docs"_s, (docsNotebook ? texts.docsNotebook : texts.docsOnline).toString()},
    };
    const VaultConfig config;
    const auto journal = [&](QDate d) { return markdown::journalPathForDate(d, config); };
    QList<DemoFile> files = {
        { demoWelcomePage(language), fillView(texts.welcome, vars) },
        { journal(today), fillView(texts.today, vars) },
        { journal(today.addDays(-1)), fillView(texts.yesterday, vars) },
        { journal(today.addDays(-7)), fillView(texts.weekAgo, vars) },
        { texts.projectPath.toString(), fillView(texts.project, vars) },
        { texts.booksPath.toString(), fillView(texts.books, vars) },
        { texts.recipesPath.toString(), fillView(texts.recipes, vars) },
        { config.journalTemplate, texts.journalTemplate.toString() },
        { texts.meetingTemplatePath.toString(), texts.meetingTemplate.toString() },
        { texts.picturePath.toString(), texts.picture.toString() },
    };
    return files;
}

static int seedFiles(Vault &vault, const QList<DemoFile> &files)
{
    int written = 0;
    for (const DemoFile &file : files) {
        if (vault.exists(file.path))
            continue;
        if (!vault.writeText(file.path, file.content, ChangeSource::Import))
            return -1;
        ++written;
    }
    return written;
}

QString docsWelcomePage(QStringView language)
{
    return isPolish(language) ? u"Spis treści.md"_s : u"Contents.md"_s;
}

QList<DemoFile> docsVaultFiles(QStringView language)
{
    return guideFiles(isPolish(language) ? u"pl"_s : u"en"_s);
}

namespace {

/// LocalStore key of the record of written pages: { path: hash of the content written, or "" for
/// a page that was written once but is the user's now (changed or deleted) }.
const QString kDocsRecordKey = u"docs/written"_s;

/// A page as 0.3.0–0.3.5 wrote it (they kept no record): the hash (text::contentHash) of one of
/// its versions and the releases that shipped that version (bit 0 = 0.3.0 … bit 5 = 0.3.5).
struct LegacyPage {
    QStringView path;
    QStringView hash;
    quint8 releases;
};
constexpr quint8 kAllLegacyReleases = 0x3f;
constexpr LegacyPage kLegacyPages[] = {
        {u"Dokument i konspekt.md", u"1a3847db096a68cc", 0x0f},
        {u"Dokument i konspekt.md", u"07a2aa74245cccc9", 0x30},
        {u"Dziennik, kalendarz i szablony.md", u"25c46ab029055233", 0x3f},
        {u"Format plików.md", u"19e3690de1791371", 0x3f},
        {u"Import i eksport.md", u"c778d68e43a33134", 0x3f},
        {u"Linki i odnośniki zwrotne.md", u"85f966d27af2b5da", 0x3f},
        {u"Obliczenia.md", u"157be2d7a7f72eb8", 0x07},
        {u"Obliczenia.md", u"a2a05af0f93b43ca", 0x38},
        {u"Pierwsze kroki.md", u"c95401dd7e00e3f6", 0x3f},
        {u"Przypomnienia i alarmy.md", u"070fac78559b6e07", 0x3f},
        {u"Skróty klawiszowe.md", u"4bc3730c7547924a", 0x3f},
        {u"Spis treści.md", u"47118489593860e9", 0x07},
        {u"Spis treści.md", u"02940f35c901e2a7", 0x38},
        {u"Synchronizacja.md", u"eb52f80bdfbc9f04", 0x01},
        {u"Synchronizacja.md", u"beeea9683e9bd3ba", 0x3e},
        {u"Tagi i właściwości.md", u"eda9f43c799b509f", 0x3f},
        {u"Wzory matematyczne.md", u"e2b44d31e1d076f9", 0x38},
        {u"Zadania.md", u"5a963851985af0cf", 0x3f},
        {u"Zapytania.md", u"8aaa9a048057db33", 0x3f},
};

/// Releases that shipped `path` with content `hash` (0 = none: changed by the user).
quint8 legacyReleases(const QString &path, const QString &hash)
{
    quint8 releases = 0;
    for (const LegacyPage &p : kLegacyPages) {
        if (p.path == path && p.hash == hash)
            releases |= p.releases;
    }
    return releases;
}

/// The record a notebook written by 0.3.0–0.3.5 would have: a page still as one of those
/// versions wrote it is recorded with its hash (so it is updated), any other content of a
/// page they wrote as the user's. A missing page counts as deleted by the user when every
/// release that may have written this notebook (judged by the versions of the unchanged pages)
/// shipped it; a page only some of them had (added later) is left out, so it gets written.
QJsonObject legacyRecord(Vault &vault)
{
    QHash<QString, quint8> shipped; // path -> releases with any version of it
    for (const LegacyPage &p : kLegacyPages)
        shipped[p.path.toString()] |= p.releases;
    QHash<QString, QString> onDisk; // path -> content hash
    quint8 possible = kAllLegacyReleases;
    for (auto it = shipped.cbegin(); it != shipped.cend(); ++it) {
        if (!vault.exists(it.key()))
            continue;
        const QString hash = text::contentHash(vault.readText(it.key()));
        onDisk.insert(it.key(), hash);
        if (const quint8 releases = legacyReleases(it.key(), hash))
            possible &= releases;
    }
    if (!possible) // versions that never shipped together: no conclusion
        possible = kAllLegacyReleases;
    QJsonObject record;
    for (auto it = shipped.cbegin(); it != shipped.cend(); ++it) {
        if (const auto disk = onDisk.constFind(it.key()); disk != onDisk.cend())
            record.insert(it.key(), legacyReleases(it.key(), *disk) ? *disk : QString());
        else if ((it.value() & possible) == possible)
            record.insert(it.key(), QString());
    }
    return record;
}

/// updateDocsVault(); `legacy`: the pages may have been written by 0.3.0–0.3.5 (the Polish ones).
DocsUpdate updateDocs(Vault &vault, const QList<DemoFile> &pages, bool legacy)
{
    DocsUpdate result;
    if (pages.isEmpty())
        return result; // a build without this language's pages: nothing to write, nothing dropped
    LocalStore *store = vault.store();
    QJsonObject record; // none: a new notebook, or one emptied to start over
    if (!vault.markdownFiles().isEmpty() && store->contains(kDocsRecordKey))
        record = store->get(kDocsRecordKey).toObject();
    else if (!vault.markdownFiles().isEmpty() && legacy)
        record = legacyRecord(vault); // written by 0.3.0–0.3.5
    const auto write = [&](const DemoFile &page) {
        if (vault.writeText(page.path, page.content, ChangeSource::Import))
            return true;
        result.ok = false;
        return false;
    };

    QSet<QString> current;
    for (const DemoFile &page : pages) {
        current.insert(page.path);
        const QString hash = text::contentHash(page.content);
        if (!vault.exists(page.path)) {
            if (record.contains(page.path))
                continue; // written before and deleted by the user: stays deleted
            if (!write(page))
                break;
            record.insert(page.path, hash);
            ++result.added;
            continue;
        }
        const QString onDisk = text::contentHash(vault.readText(page.path));
        if (onDisk == hash) {
            record.insert(page.path, hash);
        } else if (record.value(page.path).toString() == onDisk) {
            if (!write(page))
                break;
            record.insert(page.path, hash);
            ++result.updated;
        } else {
            ++result.kept; // the user's text: never overwritten
        }
    }
    if (result.ok) {
        for (const QString &path : record.keys()) {
            if (current.contains(path))
                continue;
            // No longer part of the documentation: gone unless the user changed it (then it is
            // simply the user's page from now on).
            if (vault.exists(path) && text::contentHash(vault.readText(path)) == record.value(path).toString()) {
                if (!vault.remove(path, ChangeSource::Import)) {
                    result.ok = false;
                    break;
                }
                ++result.removed;
            }
            record.remove(path);
        }
    }
    store->set(kDocsRecordKey, record);
    // At once, not after the store's usual delay: pages updated to this version but missing from
    // a lost record would count as the user's (no known hash) and never be updated again.
    store->flush();
    return result;
}

} // namespace

DocsUpdate updateDocsVault(Vault &vault, QStringView language)
{
    return updateDocs(vault, docsVaultFiles(language), isPolish(language));
}

DocsUpdate updateDocsVault(Vault &vault, const QList<DemoFile> &pages)
{
    return updateDocs(vault, pages, true);
}

int seedDemoVault(Vault &vault, const QDateTime &now, QStringView language, bool docsNotebook)
{
    return seedFiles(vault, demoVaultFiles(now, language, docsNotebook));
}

} // namespace brulion::shell
