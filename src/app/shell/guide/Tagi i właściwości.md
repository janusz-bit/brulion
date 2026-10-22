---
tags: [dokumentacja]
brulion: document
---
# Tagi i właściwości

## Tagi
Tag zaczyna się od `#`: `#projekt`, `#projekt/dom` (zagnieżdżony), `#[[do przeczytania]]` (ze spacjami). Tag może zawierać litery (także polskie), cyfry, `_`, `-` i `/`, ale nie same cyfry (`#1` nie jest tagiem). Nagłówek `# Tytuł` też nie jest tagiem. Wielkość liter nie ma znaczenia.

Tagi działają w dowolnym miejscu notatki, a także w nagłówku strony: `tags: [dokumentacja, pomoc]`. Widok **Tagi** w pasku bocznym wylicza wszystkie tagi z liczbą użyć; kliknięcie tagu pokazuje, co nim oznaczono. Filtr `tag:projekt` w [[Zapytania|zapytaniach]] łapie też `projekt/dom`.

Ta strona ma tag #dokumentacja.

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
