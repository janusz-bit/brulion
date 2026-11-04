---
tags: [dokumentacja]
brulion: document
---
# Format plików

Brulion nie ma własnego formatu. To CommonMark z dodatkami GFM (tabele, listy zadań, przekreślenia) plus kilka rozszerzeń zgodnych z Obsidianem (wtyczka Tasks) i czytelnych w Logsequ. Wszystko, co zapisze Brulion, da się odczytać w dowolnym edytorze tekstu.

## Układ folderu
```
brulion/
  journals/2026-10-14.md    dni dziennika
  Projekty/Dom.md           strony (foldery = przestrzenie nazw)
  assets/                   załączniki
  templates/                szablony (journal.md = nowy dzień)
  .brulion/config.json      ustawienia wspólne dla urządzeń
  .trash/                   kosz
```

- Tytuł strony to nazwa pliku bez `.md`.
- Nazwy różniące się tylko wielkością liter są jedną stroną — Brulion nigdy nie tworzy takich bliźniaków.
- Dane robocze urządzenia (historia wersji, ślady synchronizacji, indeks) leżą poza folderem, więc Syncthing i Nextcloud ich nie ruszają.
- Wśród nich jest gotowy indeks notatnika (`index-cache.bin`), dzięki któremu kolejne otwarcie dużego notatnika jest szybsze. Można go bezpiecznie usunąć — Brulion zbuduje go od nowa.
- Pomijane są `.brulion/`, `.trash/`, `.git/`, `.obsidian/`, `logseq/` i pliki tymczasowe synchronizacji.

## Ściągawka składni
| Zapis | Znaczenie |
|---|---|
| `[[Strona]]`, `![[Strona]]` | link, osadzenie |
| `((id))`, ` ^id` | odwołanie do bloku, identyfikator bloku |
| `#tag`, `#[[dwa słowa]]` | tag |
| `- [ ]`, `- [x]` | zadanie |
| `📅 ⏳ 🛫 ✅ ➕` | daty zadania |
| `🔁 every week` | powtarzanie |
| `⏰ RRRR-MM-DD GG:MM`, `⏰!` | przypomnienie, alarm |
| `klucz:: wartość` | właściwość bloku |
| `> [!tip] Tytuł` | ramka |
| podwójne `=` wokół tekstu, `%%komentarz%%` | zakreślenie, ukryty komentarz |
| `<mark style="background: #FF5582A6;">tekst</mark>`, `<mark class="hltr-red">` | kolorowe zakreślenie (jak wtyczka Highlightr do Obsidiana; zobacz [[Dokument i konspekt#Kolorowe zakreślenia|Kolorowe zakreślenia]]) |
| `icon: 🚀`, `banner: "![[obraz.jpg]]"`, `banner_y: 0.3` | ikona i okładka strony (jak Iconize i Banners; zobacz [[Tagi i właściwości#Ikona i okładka strony|Ikona i okładka strony]]) |
| blok z językiem `calc` lub `query` | obliczenia, zapytanie |
| `$…$`, `$$…$$`, blok z językiem `math` | wzór matematyczny (LaTeX) |
| blok z językiem `mermaid` | diagram Mermaid (zobacz [[Diagramy]]) |
| blok z językiem `chart` | wykres z tabeli, bloku `calc` albo zapytania (zobacz [[Tabele i wykresy]]) |
| komórka zaczynająca się od `=` | formuła w tabeli |
| `![[plik.pdf]]`, `![[plik.pdf#page=3]]`, `[[plik.pdf#page=3]]` | miniatura pierwszej (trzeciej) strony PDF-a, odnośnik otwierający przeglądarkę PDF na tej stronie — zapis jak w Obsidianie (zobacz [[Dokument i konspekt#PDF-y|PDF-y]]) |
| `![[Rysunek ….svg]]` | rysunek (zobacz [[Rysunki]]): obraz SVG ze sceną Excalidraw w środku; w scenie także papier (`brulionBackground`), kolor tła (`viewBackgroundColor`), jasne albo ciemne tło (`brulionTheme`: `light`, `dark`, bez klucza — jak motyw) i podkład — obraz albo strona PDF pod pismem: zablokowany element `image` na samym spodzie z `customData.brulionBackdrop` (`path` — ścieżka pliku w notatniku, `page` — strona PDF, 0 dla obrazu), a jego obraz w scenie (`files`) i w samym SVG |
| `![[Szkic.excalidraw]]`, `*.excalidraw.md` | rysunek wtyczki Excalidraw dla Obsidiana (frontmatter `excalidraw-plugin`, scena w bloku `## Drawing`: `compressed-json` albo `json`) albo plik `.excalidraw` — Brulion edytuje go i zapisuje w tym samym formacie, zmieniając tylko scenę i wpisy zmienionych tekstów |

Pliki nigdy nie zawierają dat słowami („jutro”) — tylko `RRRR-MM-DD` i `GG:MM`. HTML w notatce jest pokazywany jako tekst i nigdy nie jest wykonywany — poza znacznikami `<mark>` kolorowych zakreśleń, z których Brulion bierze tylko kolor (zawsze jeden z kolorów swojej palety).

## Co zostaje nietknięte
Brulion przepisuje tylko to, co zmieniłeś: reszta pliku, w tym zakończenia linii i formatowanie, zostaje bajt w bajt taka sama. Dlatego różnice w systemie kontroli wersji pozostają małe.

Pełna specyfikacja dla programistów: `docs/SYNTAX.md` w repozytorium. Przegląd funkcji: [[Spis treści|Spis treści]].
