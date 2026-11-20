---
tags: [dokumentacja]
brulion: document
---
# Zapytania

Blok z językiem `query` zbiera zadania, bloki, strony albo przypomnienia z całego notatnika i pokazuje je na bieżąco. Zadania można odhaczać prosto w wynikach. Oto strony z tagiem #dokumentacja, czyli wszystkie strony tego notatnika:

```query
type:pages tag:dokumentacja sort:page
```

Wstaw zapytanie poleceniem `/Zapytanie` albo przyciskiem „Pokaż jako zapytanie” w widoku [[Zadania|Zadania]]. Domyślnie wynik ma najwyżej 500 pozycji; dodaj `limit:20`, żeby go skrócić.

## Składnia
- Warunki rozdzielasz spacjami lub nowymi liniami. **Wszystkie** warunki muszą być spełnione; wartości rozdzielone przecinkiem to „lub” (`status:todo,doing`).
- Minus przed warunkiem go odwraca: `-tag:prywatne`, `-kupić`.
- `//` zaczyna komentarz do końca linii, a linia zaczynająca się od `# ` jest komentarzem (na przykład tytułem zapytania).
- Skróty: samo słowo szuka tekstu, `#praca` to `tag:praca`, `[[Strona]]` to `ref:Strona`.
- Klucze i wartości mają polskie odpowiedniki i nie zależą od wielkości liter ani polskich znaków: `stan:otwarte termin:jutro`.
- Gdy zmieniasz nazwę strony albo przenosisz stronę lub folder, filtry `tag:`, `ref:`, `[[…]]`, `page:` i `path:`, które je nazywają, zmieniają się razem z nimi (nazwa ze spacją trafia w `[[…]]`: `tag:[[Mój dom]]`).

## Najważniejsze klucze
| Klucz | Co robi |
|---|---|
| `type:` (`typ:`) | `tasks` / `zadania`, `blocks` / `bloki`, `pages` / `strony`, `reminders` / `przypomnienia` |
| `status:` (`stan:`) | `todo`, `doing`, `done`, `cancelled`, `deferred`, `open` (todo + doing), `closed`, `all` |
| `priority:` (`priorytet:`) | `highest`, `high`, `medium`, `low`, `lowest`, `none` |
| `tag:` | tag; `projekt` łapie też `projekt/dom` |
| `page:` (`strona:`) | strona po tytule, ścieżce albo aliasie; `this` to strona z zapytaniem |
| `path:` (`ścieżka:`) | początek ścieżki, np. `Projekty/` |
| `ref:` (`link:`) | elementy linkujące do strony albo oznaczone jej tagiem (`ref:Dom` łapie też `#dom`) |
| `due:`, `scheduled:`, `start:`, `done:`, `created:` | warunek na datę zadania |
| `remind:` (`przypomnienie:`) | warunek na datę przypomnienia |
| `date:` (`data:`) | warunek na datę dziennika |
| `is:` (`jest:`) | `blocked`/`zablokowane` — zadanie czeka na niezrobione zadanie (`⛔`), `blocking`/`blokujące` — inne zadanie czeka na nie, `running`/`mierzone` — licznik czasu działa, `tracked`/`z-czasem` — ma zapisany czas; `-is:blocked` to zadania niezablokowane |
| `sort:` (`sortuj:`) | np. `sort:due,-priority`; minus odwraca; także właściwość: `sort:-koszt` |
| `group:` (`grupuj:`) | `page`, `due`, `tag`, `status`, `priority`… albo właściwość: `group:etap` |
| `limit:` | maksymalna liczba wyników |
| `klucz::wartość` | właściwość — niżej |
| `view:` (`widok:`) | `table` / `tabela`, `board` / `tablica`, `gallery` / `galeria`, `calendar` / `kalendarz`, `list` / `lista` — niżej |
| `columns:` (`kolumny:`), `summary:` (`podsumowanie:`) | kolumny widoku i ich sumy, średnie… — niżej |

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
- `ref:this` — bloki, które linkują do tej strony albo mają jej tag,
- `type:tasks ref:this status:open group:page` — otwarte zadania powiązane z tą stroną, pogrupowane,
- `status:open -is:blocked sort:due` — to, co można zrobić już teraz (bez zadań, które na coś czekają).

Zobacz też [[Przypomnienia i alarmy|Przypomnienia i alarmy]]. Jeśli w zapytaniu jest błąd, w miejscu wyników pojawia się opis, co poprawić.

## Właściwości
Dwa dwukropki filtrują po właściwości — strony (jej nagłówka YAML albo linii `klucz:: wartość` na początku) albo bloku (jego własnych linii `klucz:: wartość`, a gdy ich nie ma — rodzica, a potem strony):

- `status::zrobione`, `status::"w toku"` — tekst (wielkość liter i polskie znaki bez znaczenia); lista pasuje, gdy pasuje któryś jej element,
- `koszt::>100`, `koszt::<=1200`, `koszt::100..200` — liczby jak liczby,
- `termin::<dziś`, `termin::ten-tydzień`, `termin::dziś..+7d` — daty, jak w warunkach na daty wyżej,
- `właściciel::[[Jan Kowalski]]` — link do tej strony (działa też alias),
- `pilne::tak` — pole wyboru,
- `nazwa::*dom*` — wzorzec,
- `termin::brak` — nie ma takiej właściwości (albo jest pusta), `ocena::dowolna` — jest,
- `-status::zrobione` — wszystko poza tym; wartości po przecinku to „lub”: `status::planowany,"w toku"`.

Klucz nie zależy od wielkości liter ani polskich znaków (`budzet::` znajdzie `Budżet`); klucz ze spacjami bierzesz w cudzysłów: `"termin oddania"::2026-11-01`. Dla stron `sort:` i `group:` przyjmują też nazwy właściwości (`type:pages sort:-koszt group:status`); `status::` z dwoma dwukropkami zawsze oznacza właściwość, także przy zadaniach.

```text
type:pages tag:projekt status::"w toku",planowany koszt::>100 sort:-koszt
type:tasks etap::zakupy status:open
```

## Widoki: tabele, tablice, galerie, kalendarze
`widok:` pokazuje wyniki inaczej — i pozwala je zmieniać na miejscu:

- `widok:tabela` — tabela: `kolumny:` wybiera kolumny (`tytuł`, `status`, `termin`, `tagi`, `ścieżka`, `zmieniono`… albo dowolna właściwość), a kolumna `"Brutto = koszt * 1.23"` to formuła (funkcje Dataview: `round`, `dateformat`, `length`…). `podsumowanie:koszt=suma,ocena=średnia` dodaje wyniki pod kolumnami (także `min`, `maks`, `mediana`, `liczba`, `unikalne`, `najwcześniej`, `najpóźniej`…). Kliknij komórkę, żeby ją zmienić: Brulion zapisuje tę jedną właściwość w stronie (albo linię `klucz::` zadania) i nie rusza reszty; na tej stronie cofa to Ctrl+Z, a zmiana innej strony ma **Cofnij** w komunikacie. Kliknięcie nagłówka kolumny sortuje po niej; jej menu (prawy przycisk, długie przytrzymanie) przesuwa ją albo ukrywa; **Kolumny** dodaje nową. To zmienia `sort:` i `kolumny:` samego zapytania.
- `widok:tablica` — tablica (kanban): kolumny według `grupuj:` (zadania domyślnie według stanu), karty przeciągasz myszą do innej kolumny — na telefonie po przytrzymaniu palca na karcie. Upuszczenie karty zapisuje wartość kolumny: stan zadania albo właściwość (`grupuj:etap`). **+** w kolumnie dodaje stronę (albo zadanie) z tą wartością.
- `widok:galeria` — karty z okładką strony albo jej pierwszym obrazkiem.
- `widok:kalendarz` — miesiąc z wierszami w ich datach (zadania według terminu, strony według właściwości z datą albo `grupuj:właściwość`); przeciągnij wpis na inny dzień, żeby go przenieść; strzałki zmieniają miesiąc.
- `widok:lista` — jeden wiersz w linii.

**+** u góry tworzy nowy wiersz: stronę w folderze z `ścieżka:` zapytania, z jego `tag:` i wartościami filtrów `klucz::wartość` — albo, dla zadań, zadanie na tej stronie.

Spróbuj: tablica niżej pokazuje zadania tej strony według stanu — przeciągnij kartę do „Zrobione”:

```query
type:tasks page:this widok:tablica
```

- [ ] Przeczytać o widokach
- [/] Wypróbować tablicę
- [ ] Zrobić tabelę moich projektów

```text
type:pages tag:projekt widok:tabela sortuj:-koszt
kolumny:tytuł,status,koszt,termin,"Brutto = koszt * 1.23"
podsumowanie:koszt=suma
```

## Bazy z Obsidiana (Bases)
Plik `.base` z Obsidiana otwiera się w Brulionie jako strona z widokami (tabela, karty, lista, tablica kanban; mapa pokazuje się jako tabela). Filtry (`file.hasTag("książka")`, `file.inFolder("Książki")`, `status != "przeczytana"`), formuły (`cena.toFixed(2)`, `if(…)`), nazwy wyświetlane, sortowanie, grupy, limity i podsumowania działają jak w Obsidianie. Notatka pokazuje widok bazy tam, gdzie ją osadza — `![[Książki.base]]` albo `![[Książki.base#Czytam]]` dla widoku o tej nazwie — a blok kodu z językiem `base` pokazuje YAML bazy wprost w notatce. Zmiana komórki albo przeciągnięcie karty zapisuje właściwość notatki, a zmiana sortowania lub kolumn zapisuje je z powrotem w bazie — zmieniają się tylko te linie. Wyrażenia regularne i funkcje wtyczek mówią, że nie są obsługiwane, zamiast się psuć.

## Zapytania Dataview z Obsidiana
Skarbiec współdzielony z Obsidianem często ma zapytania wtyczki Dataview. Brulion pokazuje także ich wyniki: blok kodu z językiem `dataview` wypisuje strony, tabelę albo zadania, tylko do odczytu — odhaczyć można jedynie zadania.

```text
TABLE status, budżet AS "Budżet" FROM #projekt WHERE status != "zrobione" SORT budżet DESC
```

- `LIST`, `TABLE` (kolumny z `AS "nazwa"`, `WITHOUT ID` ukrywa kolumnę ze stroną) i `TASK`,
- `FROM #tag`, `FROM "Folder"`, `FROM [[Strona]]` (strony, które do niej linkują), łączone przez `AND`, `OR` i `-`,
- `WHERE`, `SORT … DESC`, `GROUP BY`, `FLATTEN`, `LIMIT`,
- pola z nagłówka YAML i z linii `klucz:: wartość` oraz `file.name`, `file.mtime`, `file.tags`, `file.tasks`…,
- daty, np. `date(today) + dur(7 days)`, i funkcje Dataview (`contains`, `length`, `round`, `dateformat`…).

Brulion czyta te zapytania sam i nigdy nie uruchamia skryptów: bloki `dataviewjs` zostają kodem, a funkcje z wyrażeniami regularnymi i widoki `CALENDAR` mówią, że nie są obsługiwane.
