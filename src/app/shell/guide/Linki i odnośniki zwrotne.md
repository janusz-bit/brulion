---
tags: [dokumentacja]
brulion: document
---
# Linki i odnośniki zwrotne

Linki łączą notatki w sieć. Każda strona pokazuje w panelu bocznym (zakładka **Linki**) i pod treścią **odnośniki zwrotne**: strony i bloki, które do niej prowadzą — linkiem albo [[Tagi i właściwości#Tag to link do strony|tagiem]] — także te, które tylko wspominają jej nazwę bez linku.

## Rodzaje linków
| Zapis | Znaczenie |
|---|---|
| `[[Strona]]` | link do strony |
| `[[Strona\|podpis]]` | link z własnym tekstem |
| `[[Strona#Nagłówek]]` | link do nagłówka |
| `[[#Nagłówek]]` | link do nagłówka na tej samej stronie |
| `[[Strona#^id]]` | link do bloku o identyfikatorze `id` |
| `((id))` | odwołanie do bloku z dowolnej strony — pokazuje jego tekst |
| `[tekst](https://adres)` | zwykły link w Markdownie |
| `#strona`, `#[[Dwa słowa]]` | tag — prowadzi do strony o tej nazwie |

Przykłady: [[Spis treści|Spis treści]], [[Zadania#Powtarzanie]], oraz strona, która jeszcze nie istnieje: [[Notatka do utworzenia]]. Kliknięcie takiego linku tworzy stronę.

## Jak Brulion znajduje stronę
Nazwa z linku jest dopasowywana do plików po kolei: pełna ścieżka w notatniku, ścieżka względem bieżącego folderu, sama nazwa pliku (bez względu na wielkość liter), a na końcu aliasy z nagłówka strony (`aliases: [Skrót]`). Przy kilku pasujących wygrywa strona o najkrótszej ścieżce. Gdy dwie strony mają tę samą nazwę w różnych folderach, użyj ścieżki: `[[Projekty/Dom]]`.

## Osadzanie
Wykrzyknik przed linkiem wstawia treść zamiast odnośnika:
- `![[Strona]]` — cała strona, `![[Strona#Nagłówek]]` — sekcja,
- `!((id))` — pojedynczy blok,
- `![[zdjęcie.png]]`, `![[dokument.pdf]]` — załączniki.

Osadzony fragment pokazuje bieżący tekst źródła; zmieniasz go u źródła.

## Odwołania do bloków
Żeby odwołać się do bloku, wpisz `((` i zacznij pisać jego treść — Brulion doda blokowi identyfikator, czyli końcówkę ` ^abc123`. Odwołanie `((abc123))` działa w całym notatniku, więc przeniesienie bloku na inną stronę niczego nie psuje.

## Zmiana nazwy i przenoszenie
Zmiana nazwy strony (F2) aktualizuje linki do niej i tagi, które ją nazywają, w pozostałych notatkach. Tak samo przeniesienie strony, załącznika albo całego folderu („Przenieś do…”, przeciągnięcie w drzewie, ukośnik w nowej nazwie w oknie zmiany nazwy): każdy link i tag dalej prowadzi do tej samej strony, a okno przenoszenia (i podpowiedź przy przeciąganiu) mówi wcześniej, ile ich się zmieni.
- Link po samej nazwie zostaje, dopóki wskazuje tę stronę: `[[Dom]]` po przeniesieniu jedynej strony „Dom” się nie zmienia.
- Gdy po przeniesieniu nazwa wskazywałaby inną stronę (np. w notatniku jest drugi „Dom” o krótszej ścieżce), link dostaje najkrótszą jednoznaczną ścieżkę: `[[Projekty/Dom]]`. Tak samo tagi: `#dom` → `#Projekty/Dom`.
- Linki ze ścieżką idą za stroną: `[[Projekty/Dom]]` → `[[Archiwum/Dom]]`; także obrazy i pliki z przenoszonego folderu: `![[Projekty/plan.png]]`.
- Nagłówek, blok i alias zostają: `[[Dom#Kuchnia|kuchnia]]` → `[[Projekty/Dom#Kuchnia|kuchnia]]`.

Podpowiedź `[[` też wstawia ścieżkę, gdy wybrana strona ma imienniczkę w innym folderze. Historia strony i kosz pozwalają cofnąć pomyłki, a komunikat po przeniesieniu ma „Cofnij”.

## Graf
Widok **Graf** rysuje strony i dni dziennika jako sieć połączeń: cały notatnik albo okolice jednej strony (głębokość 1–2). Tag, który nazywa stronę, łączy się z nią jak link. Przełączniki ukrywają tagi bez strony, brakujące strony, dzienniki i szablony.

Dalej: [[Tagi i właściwości|Tagi i właściwości]].
