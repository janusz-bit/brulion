---
tags: [dokumentacja]
brulion: document
---
# Obliczenia

Brulion ma własny kalkulator, który liczy wprost w notatkach. W pliku zostają tylko Twoje wpisy; wyniki są liczone i pokazywane obok na bieżąco, w formacie języka aplikacji (po polsku `42,5`).

## Bloki `calc`
Blok z językiem `calc` liczy linia po linii, ze wspólnymi zmiennymi:

```calc
cena = 12,50
ilość = 4
cena * ilość
rabat = 15%
ans - rabat
5 km to mi
20% z 150
```

- `nazwa = wyrażenie` tworzy zmienną (nazwy mogą mieć polskie znaki), `f(x) = x^2` — funkcję.
- `ans` to poprzedni wynik.
- Procenty: `x + 10%`, `x - 10%`, `20% z x` (także `of`, `od`), `x jako % z y`.
- Jednostki: `5 km`, `3 h + 20 min`, `100 km / 2 h`, `20°C to °F`; konwersja słowem `to`, `in`, `na` albo `w`.
- Waluty to etykiety, które przechodzą przez rachunki: `12 zł + 5 zł`, `€10`, `$5`. Różnych walut Brulion nigdy nie przelicza.
- `#` i `//` zaczynają komentarz. Tekst przed dwukropkiem to etykieta: `Czynsz: 2400 zł`.
- Przecinek dziesiętny działa tak samo jak kropka; w wywołaniach funkcji oddzielaj argumenty średnikiem: `round(2,345; 2)`.
- Funkcje: `sum`/`suma`, `avg`/`średnia`, `min`, `max`, `count`, `median`, `round`/`zaokr`, `floor`, `ceil`, `abs`, `sqrt`, `pow`, `exp`, `ln`, `log`, trygonometria, `if`/`jeśli`.

### Sumy
Samo słowo `suma` (`razem`, `total`) w osobnej linii sumuje wyniki powyżej aż do pustej linii, a `średnia` — uśrednia je:

```calc
Czynsz: 2400 zł
Prąd: 180 zł
Internet: 60 zł
suma
```

## Rachunki w tekście
Kod w linii zaczynający się od `=` pokazuje wynik. Rok ma `= 52 * 7` dni w pełnych tygodniach. Zmienne z wcześniejszych bloków `calc` na tej samej stronie są dostępne, podobnie jak liczbowe właściwości strony.

## Tabele z formułami
Komórka tabeli zaczynająca się od `=` jest formułą:

| Produkt | Cena | Ilość | Razem |
|---|---:|---:|---:|
| Zeszyt | 8,50 | 3 | =[Cena]*[Ilość] |
| Długopis | 3 | 10 | =[Cena]*[Ilość] |
| Teczka | 12 | 2 | =[Cena]*[Ilość] |
| **Razem** | | | =sum(above) |

Odwołania:

| Zapis | Znaczenie |
|---|---|
| `[Cena]` | komórka z tego samego wiersza w kolumnie o nagłówku „Cena” |
| `A1`, `C3` | komórka: litera kolumny i numer wiersza danych (1 = pierwszy pod nagłówkiem) |
| `A1:A5`, `B:B` | zakres, cała kolumna |
| `above`, `below`, `left`, `right` | wszystkie liczby powyżej, poniżej, na lewo, na prawo (`powyżej`, `poniżej`) |

Zwykłe komórki czytane są tak, jak zapisują ludzie: `12,50 zł`, `23%`. Błąd w formule pokazuje się jako `#ERR` (po najechaniu: powód), a zapętlenie jako `#CYCLE`. Tabelę edytujesz jak arkusz, a Brulion przepisuje ją do Markdownu.

Wstaw tabelę albo blok obliczeń poleceniami `/Tabela` i `/Obliczenia`. Zobacz też [[Tagi i właściwości|właściwości]], z których wartości można liczyć.
