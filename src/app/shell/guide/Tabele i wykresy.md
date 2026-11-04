---
tags: [dokumentacja]
aliases: [Tabele, Wykresy, CSV, Sortowanie tabel]
brulion: document
---
# Tabele i wykresy

Tabela w notatce to zwykła tabela Markdown, którą Brulion pokazuje jak arkusz: z formułami (zobacz [[Obliczenia#Tabele z formułami|Obliczenia]]), sortowaniem, filtrami, wymianą z arkuszami przez CSV i wykresami. W pliku zostaje sam tekst, czytelny w każdym programie.

## Sortowanie
Kliknij prawym przyciskiem komórkę albo nagłówek kolumny (na telefonie stuknij komórkę i wybierz **Więcej**), a potem **Kolumna → Sortuj rosnąco** lub **Sortuj malejąco**. Spróbuj na tej tabeli:

| Owoc | Cena za kg | Zebrano |
|---|--:|---|
| Śliwki | 7,50 zł | 15.09.2026 |
| Gruszki | 9 zł | 3 września 2026 |
| Żurawina | 24 zł | 2026-10-02 |
| Agrest | 12,80 zł | 2026-07-10 |
| Łubin | 5 zł | |
| **Razem** | =sum(above) | |

- Liczby układają się według wartości (`9 zł` przed `12,80 zł`), daty według dnia — także zapisane po polsku — a tekst alfabetycznie, po polsku: `Ł` po `L`, `Ż` na końcu, `Pokój 2` przed `Pokój 10`.
- Puste komórki zawsze idą na koniec. Wiersze o tej samej wartości zostają w dotychczasowej kolejności.
- Wiersz podsumowania (jak **Razem**, którego formuła sumuje kolumnę powyżej) zostaje na dole.
- Sortowanie przestawia wiersze w pliku — każdy wiersz zostaje dokładnie taki, jak był napisany. Cofniesz je przez **Ctrl+Z**.

## Filtrowanie
**Filtruj wiersze** w menu tabeli pokazuje pod nagłówkiem pole w każdej kolumnie. Wiersze, które nie pasują, znikają z widoku — w pliku nic się nie zmienia, a filtr działa, dopóki strona jest otwarta. Kolumna z filtrem ma w nagłówku lejek, a pod tabelą widać, ile wierszy jest pokazanych, i przycisk **Wyczyść filtry**.

| Wpisz | Pokazuje |
|---|---|
| `zolw` | komórki zawierające „żółw” (bez względu na wielkość liter i polskie znaki) |
| `!zolw` | komórki bez „żółw” |
| `> 10`, `<= 7,50`, `= 9` | liczby większe, mniejsze lub równe |
| `>= 2026-09-01` | daty od 1 września 2026 |
| `=` / `!=` | puste komórki / komórki z treścią |

Wiersz podsumowania zawsze zostaje widoczny i liczy wszystkie wiersze, także ukryte.

## CSV: arkusze kalkulacyjne
- **Kopiuj jako CSV** w menu tabeli kopiuje ją do schowka, a **Zapisz jako CSV…** zapisuje do pliku, który otworzysz w Excelu, LibreOffice albo Arkuszach Google. Formuły trafiają do pliku jako wyniki.
- `/Tabela z CSV…` wstawia tabelę z pliku CSV. Brulion sam rozpozna, czy wartości dzieli przecinek, średnik czy tabulator, i poradzi sobie z cudzysłowami, polskimi znakami (także z plików ze starszego Excela) i przejściami do nowej linii w komórkach. Wartości przychodzą jako tekst — nawet te, które w arkuszu były formułami.

## Wykresy
Blok `chart` rysuje wykres z liczb na stronie. Najprościej wpisać `/Wykres` pod tabelą:

| Miesiąc | Przychód | Koszt |
|---|--:|--:|
| Styczeń | 4 200 | 3 100 |
| Luty | 3 900 | =B1-500 |
| Marzec | 5 100 | 3 300 |
| Razem | =sum(above) | =sum(above) |

```chart
typ: słupkowy
tytuł: Pierwszy kwartał
```

Wykres bierze tabelę tuż nad sobą: etykiety z pierwszej kolumny, wartości z kolumn z liczbami (bez wiersza podsumowania) i formuły jako ich wyniki. Zmień liczbę w tabeli, a wykres zmieni się od razu. Kliknij wykres, żeby poprawić jego opis.

| Klucz | Co znaczy |
|---|---|
| `typ:` | `słupkowy`, `liniowy` albo `kołowy` (po angielsku `bar`, `line`, `pie`) |
| `tabela:` | `powyżej` (domyślnie) albo numer tabeli na stronie, np. `2` |
| `obliczenia:` | dane z bloku `calc`: `powyżej` albo jego numer |
| `zapytanie:` | dane z [[Zapytania|zapytania]]: liczba wyników w każdej grupie, np. `zapytanie: type:tasks tag:praca group:status` |
| `etykiety:` | kolumna z etykietami (nazwa nagłówka albo litera) |
| `wartości:` | kolumny z wartościami, po przecinku: `Przychód, Koszt` |
| `tytuł:` | tytuł nad wykresem |

Wykres kołowy wydatków z bloku obliczeń:

```calc
Czynsz: 1800
Jedzenie: 1200
Transport: 350
```

```chart
typ: kołowy
obliczenia: powyżej
```

Brulion rysuje wykresy sam, w kolorach motywu — jasnego i ciemnego — bez internetu i bez uruchamiania czegokolwiek z notatki. Czytnik ekranu odczyta typ wykresu, tytuł i wszystkie wartości. W eksporcie do HTML wykres jest obrazem, a pod nim można rozwinąć jego wartości i źródło. Gdy coś się nie zgadza (nie ma tabeli, kolumny o tej nazwie albo liczb), Brulion pokazuje opis wykresu i pod nim, czego brakuje.

Schematy, diagramy sekwencji i wykresy kołowe zapisane samodzielnie, bez tabeli, opisuje strona [[Diagramy|Diagramy]].
