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
| blok z językiem `calc` lub `query` | obliczenia, zapytanie |
| `$…$`, `$$…$$`, blok z językiem `math` | wzór matematyczny (LaTeX) |
| komórka zaczynająca się od `=` | formuła w tabeli |
| `![[Rysunek ….svg]]` | rysunek (zobacz [[Rysunki]]): obraz SVG ze sceną Excalidraw w środku; w scenie także papier (`brulionBackground`) i jasne albo ciemne tło (`brulionTheme`: `light`, `dark`, bez klucza — jak motyw) |

Pliki nigdy nie zawierają dat słowami („jutro”) — tylko `RRRR-MM-DD` i `GG:MM`. HTML w notatce jest pokazywany jako tekst i nigdy nie jest wykonywany.

## Co zostaje nietknięte
Brulion przepisuje tylko to, co zmieniłeś: reszta pliku, w tym zakończenia linii i formatowanie, zostaje bajt w bajt taka sama. Dlatego różnice w systemie kontroli wersji pozostają małe.

Pełna specyfikacja dla programistów: `docs/SYNTAX.md` w repozytorium. Przegląd funkcji: [[Spis treści|Spis treści]].
