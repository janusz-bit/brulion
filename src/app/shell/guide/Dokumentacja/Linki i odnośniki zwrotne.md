---
tags: [dokumentacja]
brulion: document
---
# Linki i odnośniki zwrotne

Linki łączą notatki w sieć. Każda strona pokazuje w panelu bocznym (zakładka **Linki**) i pod treścią **odnośniki zwrotne**: strony i bloki, które do niej prowadzą, także te, które tylko wspominają jej nazwę bez linku.

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

Przykłady: [[Dokumentacja/Spis treści|Spis treści]], [[Przepisy#Szarlotka babci]], oraz strona, która jeszcze nie istnieje: [[Notatka do utworzenia]]. Kliknięcie takiego linku tworzy stronę.

## Jak Brulion znajduje stronę
Nazwa z linku jest dopasowywana do plików po kolei: pełna ścieżka w brulionie, ścieżka względem bieżącego folderu, sama nazwa pliku (bez względu na wielkość liter), a na końcu aliasy z nagłówka strony (`aliases: [Skrót]`). Przy kilku pasujących wygrywa strona o najkrótszej ścieżce. Gdy dwie strony mają tę samą nazwę w różnych folderach, użyj ścieżki: `[[Projekty/Dom]]`.

## Osadzanie
Wykrzyknik przed linkiem wstawia treść zamiast odnośnika:
- `![[Strona]]` — cała strona, `![[Strona#Nagłówek]]` — sekcja,
- `!((id))` — pojedynczy blok,
- `![[zdjęcie.png]]`, `![[dokument.pdf]]` — załączniki.

Osadzony fragment pokazuje bieżący tekst źródła; zmieniasz go u źródła.

## Odwołania do bloków
Żeby odwołać się do bloku, wpisz `((` i zacznij pisać jego treść — Brulion doda blokowi identyfikator, czyli końcówkę ` ^abc123`. Odwołanie `((abc123))` działa w całym brulionie, więc przeniesienie bloku na inną stronę niczego nie psuje.

## Zmiana nazwy
Zmiana nazwy strony (F2) aktualizuje linki do niej w pozostałych notatkach. Historia strony i kosz pozwalają cofnąć pomyłki.

## Graf
Widok **Graf** rysuje strony i dni dziennika jako sieć połączeń: cały brulion albo okolice jednej strony (głębokość 1–2). Przełączniki ukrywają tagi, brakujące strony, dzienniki i szablony.

Dalej: [[Dokumentacja/Tagi i właściwości|Tagi i właściwości]].
