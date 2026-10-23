---
tags: [dokumentacja]
brulion: document
---
# Tagi i właściwości

## Tagi
Tag zaczyna się od `#`: `#projekt`, `#projekt/dom` (zagnieżdżony), `#[[do przeczytania]]` (ze spacjami). Tag może zawierać litery (także polskie), cyfry, `_`, `-` i `/`, ale nie same cyfry (`#1` nie jest tagiem). Nagłówek `# Tytuł` też nie jest tagiem. Wielkość liter nie ma znaczenia.

Tagi działają w dowolnym miejscu notatki, a także w nagłówku strony: `tags: [dokumentacja, pomoc]`. Widok **Tagi** w pasku bocznym wylicza wszystkie tagi z liczbą użyć. Filtr `tag:projekt` w [[Zapytania|zapytaniach]] łapie też `projekt/dom`.

## Tag to link do strony
Tag nazywa stronę, jak w Logseq: kliknięcie `#praca` otwiera stronę „praca”. Jeśli jej jeszcze nie ma, otwiera się pusta i powstaje, gdy coś napiszesz — tag zagnieżdżony `#praca/raporty` trafi do folderu `praca`, a tag z datą (`#2026-10-13`) otworzy ten dzień dziennika. Tag znajduje stronę tak jak [[Linki i odnośniki zwrotne|link]] wpisany w głównym folderze notatnika: po pełnej ścieżce (`#Projekty/Dom`), po nazwie (przy kilku stronach o tej samej nazwie — ta o najkrótszej ścieżce) albo po aliasie. Wielkość liter się nie liczy, polskie znaki tak: `#zolw` to nie `Żółw`.

- **Odnośniki** pod stroną pokazują bloki z jej tagiem obok bloków, które do niej linkują, a strony oznaczone tagiem w całości (w nagłówku) jako „cała strona”. Podtagi (`#praca/raporty`) to osobne strony.
- **Widok tagu** — wszystko, co ma tag razem z podtagami — jest pod nagłówkiem strony („Widok tagu”, obok „Zadania z tym tagiem” i podtagów) i w widoku **Tagi** (przycisk na końcu wiersza; sam wiersz otwiera stronę).
- **Zmiana nazwy strony** poprawia też tagi, które ją nazywają (`#stara` → `#Nowa` albo `#[[Nowa nazwa]]`), także w `tags::` i w nagłówku strony; podtagi zostają bez zmian. Tak samo filtry `tag:stara` i `#stara` w [[Zapytania|zapytaniach]] (`tag:stara/x` zostaje).
- Po wpisaniu `#` podpowiedzi pokazują tagi i strony; nazwę ze spacją albo znakami spoza tagu Brulion zapisze jako `#[[Jan Kowalski]]`.
- W widoku **Zadania** kliknięcie tagu filtruje listę; Ctrl+kliknięcie (na telefonie przytrzymanie) otwiera jego stronę.

Ta strona ma tag #dokumentacja — kliknij go, żeby zobaczyć jego stronę, a pod nią wszystko, co nim oznaczono.

## Właściwości strony
Na samym początku pliku może stać nagłówek YAML (frontmatter):

```
---
tags: [projekt]
aliases: [Remont]
status: w toku
brulion: outline
---
```

- `tags` — tagi całej strony,
- `aliases` — dodatkowe nazwy, pod którymi strona odpowiada na linki,
- `brulion` — tryb notatki ([[Dokument i konspekt|dokument albo konspekt]]),
- dowolne własne klucze, np. `status` — Brulion pokazuje je w panelu **Właściwości** nad treścią.

Inne programy widzą te same wpisy jako zwykłe właściwości.

## Właściwości bloków
W stylu Logseq/Dataview: linia `klucz:: wartość` bezpośrednio w bloku.

Szacowany koszt:: 1200 zł

Listę wartości w postaci linków lub tagów (`autor:: [[Jan]], [[Anna]]`) Brulion rozumie jako listę. Liczbowych właściwości strony można też użyć w [[Obliczenia|obliczeniach]] jako zmiennych.

## Wyszukiwanie po właściwościach
Wartości właściwości są częścią tekstu bloku, więc znajdziesz je zwykłym wyszukiwaniem, a w zapytaniach filtrem tekstowym.
