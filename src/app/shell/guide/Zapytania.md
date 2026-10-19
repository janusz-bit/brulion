---
tags: [dokumentacja]
brulion: document
---
# Zapytania

Blok z językiem `query` zbiera zadania, bloki, strony albo przypomnienia z całego brulionu i pokazuje je na bieżąco. Zadania można odhaczać prosto w wynikach. Oto strony z tagiem #dokumentacja, czyli wszystkie strony tego brulionu:

```query
type:pages tag:dokumentacja sort:page
```

Wstaw zapytanie poleceniem `/Zapytanie` albo przyciskiem „Pokaż jako zapytanie” w widoku [[Zadania|Zadania]]. Domyślnie wynik ma najwyżej 500 pozycji; dodaj `limit:20`, żeby go skrócić.

## Składnia
- Warunki rozdzielasz spacjami lub nowymi liniami. **Wszystkie** warunki muszą być spełnione; wartości rozdzielone przecinkiem to „lub” (`status:todo,doing`).
- Minus przed warunkiem go odwraca: `-tag:prywatne`, `-kupić`.
- `//` zaczyna komentarz do końca linii, a linia zaczynająca się od `# ` jest tytułem komentarza.
- Skróty: samo słowo szuka tekstu, `#praca` to `tag:praca`, `[[Strona]]` to `ref:Strona`.
- Klucze i wartości mają polskie odpowiedniki i nie zależą od wielkości liter ani polskich znaków: `stan:otwarte termin:jutro`.

## Najważniejsze klucze
| Klucz | Co robi |
|---|---|
| `type:` (`typ:`) | `tasks` / `zadania`, `blocks` / `bloki`, `pages` / `strony`, `reminders` / `przypomnienia` |
| `status:` (`stan:`) | `todo`, `doing`, `done`, `cancelled`, `deferred`, `open` (todo + doing), `closed`, `all` |
| `priority:` (`priorytet:`) | `highest`, `high`, `medium`, `low`, `lowest`, `none` |
| `tag:` | tag; `projekt` łapie też `projekt/dom` |
| `page:` (`strona:`) | strona po tytule, ścieżce albo aliasie; `this` to strona z zapytaniem |
| `path:` (`ścieżka:`) | początek ścieżki, np. `Projekty/` |
| `ref:` (`link:`) | elementy linkujące do strony |
| `due:`, `scheduled:`, `start:`, `done:`, `created:` | warunek na datę zadania |
| `remind:` (`przypomnienie:`) | warunek na datę przypomnienia |
| `date:` (`data:`) | warunek na datę dziennika |
| `sort:` (`sortuj:`) | np. `sort:due,-priority`; minus odwraca |
| `group:` (`grupuj:`) | `page`, `due`, `tag`, `status`, `priority`… |
| `limit:` | maksymalna liczba wyników |

## Daty w warunkach
Operatory `<`, `<=`, `>`, `>=` poprzedzają wyrażenie; bez operatora data musi mieścić się w danym dniu lub okresie.

- dni: `today`/`dziś`, `tomorrow`/`jutro`, `yesterday`/`wczoraj`, `2026-10-15`,
- przesunięcia: `+7d`, `-2w`, `+1m`, `+1y` (po polsku także `+2tyg`),
- okresy: `this-week`, `next-week`, `this-month`, `last-month`… (po polsku `ten-tydzień`, `przyszły-miesiąc`),
- zakres: `today..+7d`,
- słowa: `overdue` / `zaległe`, `none` / `brak`, `any` / `dowolna`.

Przykład: `due:<=+7d` oznacza „termin w ciągu tygodnia, łącznie z zaległymi”.

## Przykłady
- `type:tasks status:open tag:praca due:<=+7d sort:due` — otwarte zadania z tagiem #praca na najbliższy tydzień,
- `remind:this-week` — przypomnienia na ten tydzień,
- `type:pages tag:projekt sort:page` — strony projektów,
- `ref:this` — bloki, które linkują do tej strony,
- `type:tasks ref:this status:open group:page` — otwarte zadania powiązane z tą stroną, pogrupowane.

Zobacz też [[Przypomnienia i alarmy|Przypomnienia i alarmy]]. Jeśli w zapytaniu jest błąd, w miejscu wyników pojawia się opis, co poprawić.
