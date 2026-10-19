#include "demovault.h"

#include "markdown/journal.h"
#include "vault/vault.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>

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

/// The documentation pages (src/app/shell/guide/*.md, embedded as :/guide/...), written in
/// Brulion's own Markdown: the notes of the notebook "Dokumentacja Brulionu".
QList<DemoFile> guideFiles()
{
    QList<DemoFile> out;
    QDirIterator it(u":/guide"_s, { u"*.md"_s }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString path = it.filePath().mid(QStringLiteral(":/guide/").size());
        out.append({ path, QString::fromUtf8(file.readAll()) });
    }
    std::sort(out.begin(), out.end(), [](const DemoFile &a, const DemoFile &b) { return a.path < b.path; });
    return out;
}

// ---- Content ------------------------------------------------------------------------------------

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
- Kliknij tag, żeby zobaczyć wszystko, co nim oznaczono.

## Zadania
- [ ] Zwykłe zadanie — kliknij pole wyboru albo naciśnij Ctrl+Enter
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
Pełna dokumentacja jest w osobnym brulionie „Dokumentacja Brulionu” (tryby, linki, zadania, przypomnienia, obliczenia, zapytania, synchronizacja, skróty). Przełączysz się do niego przyciskiem ⇄ obok nazwy brulionu w pasku bocznym — i tak samo wrócisz.

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
| Ctrl+Enter | zmień stan zadania |
| Tab / Shift+Tab | wcięcie / wysunięcie bloku |

%%Ten komentarz widać tylko podczas edycji.%%
==Zakreślenie== i ~~przekreślenie~~ też działają.
)md"_s;

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

} // namespace

QString demoWelcomePage()
{
    return u"Witaj w Brulionie.md"_s;
}

QList<DemoFile> demoVaultFiles(const QDateTime &now)
{
    const QDate today = now.date();
    const QHash<QString, QString> vars{
        { u"today"_s, iso(today) },
        { u"yesterday"_s, iso(today.addDays(-1)) },
        { u"weekAgo"_s, iso(today.addDays(-7)) },
        { u"tomorrow"_s, iso(today.addDays(1)) },
        { u"in2"_s, iso(today.addDays(2)) },
        { u"in3"_s, iso(today.addDays(3)) },
        { u"in5"_s, iso(today.addDays(5)) },
        { u"nextMonday"_s, iso(nextWeekday(today, Qt::Monday)) },
        { u"nextSaturday"_s, iso(nextWeekday(today, Qt::Saturday)) },
        { u"next10th"_s, iso(nextTenth(today)) },
    };
    const VaultConfig config;
    const auto journal = [&](QDate d) { return markdown::journalPathForDate(d, config); };
    QList<DemoFile> files = {
        { demoWelcomePage(), fill(kWelcome, vars) },
        { journal(today), fill(kToday, vars) },
        { journal(today.addDays(-1)), fill(kYesterday, vars) },
        { journal(today.addDays(-7)), fill(kWeekAgo, vars) },
        { u"Projekty/Remont mieszkania.md"_s, fill(kRemont, vars) },
        { u"Książki 2026.md"_s, fill(kBooks, vars) },
        { u"Przepisy.md"_s, fill(kRecipes, vars) },
        { u"templates/journal.md"_s, kJournalTemplate },
        { u"templates/Spotkanie.md"_s, kMeetingTemplate },
        { u"assets/plan-mieszkania.svg"_s, kPlanSvg },
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

QString docsWelcomePage()
{
    return u"Spis treści.md"_s;
}

QList<DemoFile> docsVaultFiles()
{
    return guideFiles();
}

int seedDocsVault(Vault &vault)
{
    return seedFiles(vault, docsVaultFiles());
}

int seedDemoVault(Vault &vault, const QDateTime &now)
{
    return seedFiles(vault, demoVaultFiles(now));
}

} // namespace brulion::shell
