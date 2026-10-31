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
- **Widok tagu** — wszystko, co ma tag razem z podtagami — jest pod nagłówkiem strony („Widok tagu”, obok „Zadania z tym tagiem” i podtagów) i w widoku **Tagi** (przycisk na końcu wiersza; sam wiersz otwiera stronę). Na telefonie i w panelu bocznym „Widok tagu” to przycisk **#** obok tytułu strony, a „Zadania z tym tagiem” są w menu strony **⋯**.
- **Zmiana nazwy strony** poprawia też tagi, które ją nazywają (`#stara` → `#Nowa` albo `#[[Nowa nazwa]]`), także w `tags::` i w nagłówku strony; podtagi zostają bez zmian. Tak samo filtry `tag:stara` i `#stara` w [[Zapytania|zapytaniach]] (`tag:stara/x` zostaje).
- Po wpisaniu `#` podpowiedzi pokazują tagi i strony; nazwę ze spacją albo znakami spoza tagu Brulion zapisze jako `#[[Jan Kowalski]]`.
- **Menu tagu** — prawy przycisk myszy na tagu (na telefonie przytrzymanie): „Otwórz stronę tagu”, „Otwórz w panelu bocznym”, „Widok tagu”, „Zadania z tym tagiem”, „Kopiuj tag”.
- W widoku **Zadania** kliknięcie tagu filtruje listę; Ctrl+kliknięcie otwiera jego stronę, a prawy przycisk (na telefonie przytrzymanie) otwiera menu tagu z „Filtruj po tagu” na początku.

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

Inne programy widzą te same wpisy jako zwykłe właściwości. Zamiast nagłówka YAML możesz też zacząć stronę od linii `klucz:: wartość` (styl Logseq).

### Edycja w panelu Właściwości
Właściwości zmieniasz bez pisania YAML-a: kliknij nagłówek **Właściwości** nad treścią, żeby rozwinąć panel (zostaje rozwinięty, aż go zwiniesz). Każda właściwość ma pole według typu wartości:

- **tekst** — kliknij wartość, żeby ją zmienić; linki `[[strona]]` i `#tagi` w tekście działają. Wpisz `[[`, a Brulion podpowie strony,
- **liczba** — przyjmuje też „3,5” i „1 200” (zapisze `3.5` i `1200`),
- **pole wyboru** — `true` albo `false`, jedno kliknięcie,
- **data** i **data i godzina** — okno z kalendarzem; możesz wpisać „jutro”, „w piątek 9:00”,
- **lista**, **tagi** i **aliasy** — etykiety z ×; „+” dodaje następną (Enter albo przecinek), Backspace w pustym polu usuwa ostatnią.

Typu nie trzeba ustawiać: Brulion rozpoznaje go po wartości, a pusta wartość dostaje typ, jaki ten klucz ma najczęściej na innych stronach notatnika.

Nową właściwość dodasz przyciskiem **Dodaj właściwość** na dole panelu, w menu strony albo skrótem **Ctrl+;** (także na stronie, która nie ma jeszcze żadnych właściwości). Podpowiedzi pokazują klucze używane w całym notatniku i to, na ilu stronach. Klucza, który strona już ma, nie dodasz drugi raz (także pisanego inną wielkością liter) — Brulion powie, że taka właściwość już jest, a jej wartość zostanie bez zmian. Ikona typu przy właściwości (albo prawy przycisk, na telefonie przytrzymanie) otwiera menu: **Typ** (zmiana typu — gdy wartości nie da się zamienić, zostaje wyczyszczona i możesz to cofnąć), **Zmień nazwę**, **Kopiuj wartość** i **Usuń**. Każdą zmianę cofniesz (Ctrl+Z albo „Cofnij” w komunikacie).

Klawiatura w panelu: strzałki ↑ i ↓ przechodzą między właściwościami, → albo Enter zmienia wartość, ← albo F2 zmienia nazwę, Delete usuwa, Alt+↓ przechodzi do pierwszego bloku, Esc wraca do edytora.

Brulion zmienia tylko tę jedną właściwość — reszta nagłówka (komentarze, kolejność, cudzysłowy, zakończenia linii) zostaje bez zmian. Działa to tak samo w nagłówku YAML, jak w liniach `klucz:: wartość`; w tych drugich klucz może mieć tylko litery a–z, cyfry, `-` i `_`. Nagłówek, którego nie da się bezpiecznie zmienić w polach (na przykład wcięty), panel pokazuje tylko do odczytu — zmienisz go przyciskiem **Edytuj jako tekst**. To samo polecenie ma menu właściwości, której pola nie obsłużą (na przykład zagnieżdżonej mapy). Wartość otwarta w polu i zostawiona bez zmian zostaje zapisana tak jak była.

### Tagi i aliasy w nagłówku strony
Tagi i aliasy strony stoją też pod jej tytułem. Prawy przycisk na etykiecie (na telefonie przytrzymanie) otwiera menu: tag możesz otworzyć albo usunąć z tej strony, alias skopiować albo usunąć. Przycisk „+” na końcu dodaje tag albo alias bez otwierania panelu. Gdy panel **Właściwości** jest rozwinięty, tych etykiet pod tytułem nie ma — te same tagi i aliasy (z × i „+”) są w panelu tuż pod nimi; po zwinięciu panelu wracają.

## Właściwości bloków
W stylu Logseq/Dataview: linia `klucz:: wartość` bezpośrednio w bloku.

Szacowany koszt:: 1200 zł

Listę wartości w postaci linków lub tagów (`autor:: [[Jan]], [[Anna]]`) Brulion rozumie jako listę. Liczbowych właściwości strony można też użyć w [[Obliczenia|obliczeniach]] jako zmiennych.

## Wyszukiwanie po właściwościach
Wartości właściwości są częścią tekstu bloku, więc znajdziesz je zwykłym wyszukiwaniem, a w zapytaniach filtrem tekstowym.
