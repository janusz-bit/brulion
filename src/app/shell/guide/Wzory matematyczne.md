---
tags: [dokumentacja]
brulion: document
---
# Wzory matematyczne

Wzory piszesz w notatce jak w LaTeXu, a Brulion od razu pokazuje je jako czytelny wzór — bez internetu i bez uruchamiania kodu. W pliku zostaje samo źródło LaTeX, więc notatka dalej jest zwykłym Markdownem.

## W tekście
Formułę otaczasz znakami `$`: `$E = mc^2$` wygląda tak: $E = mc^2$.

Wzór może stać w środku zdania: pole koła to $\pi r^2$, a pierwiastek z dwóch to $\sqrt{2}$.

Żeby kwoty i zmienne nie zamieniały się we wzory, zaraz po otwierającym `$` musi stać znak (nie spacja), a tuż przed nim nie może być litery ani cyfry. Tuż przed zamykającym `$` nie może być spacji, a zaraz po nim litery ani cyfry. Dlatego `koszt $5 i $10`, `Netflix 15$, Spotify 10$`, `10$/h` czy `$PATH:$HOME` zostają zwykłym tekstem. Wzór może przejść do następnej linii, ale nie przez pustą linię. Jeśli chcesz napisać sam znak dolara, poprzedź go ukośnikiem: `\$100`.

Ukośnik przed nawiasem oznacza zwykły nawias, jak w Obsidianie: `\(…\)` i `\[…\]` to nie wzory (tak zapisują nawiasy importy z innych programów). Wzory pisz między `$…$` albo `$$…$$`.

## Osobna linia
Dłuższy wzór możesz zapisać między podwójnymi dolarami:

$$
\frac{a + b}{2} = \frac{a}{2} + \frac{b}{2}
$$

albo w bloku `math`:

```math
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
```

Blok `math` (także `latex` i `tex`) jest edytowany w miejscu: kliknij go, żeby zobaczyć i poprawić źródło LaTeX; Enter dodaje kolejną linię wzoru (przydaje się w macierzach i układach). Wstawisz go poleceniem `/Wzór matematyczny`.

## Co Brulion rozumie
- greckie litery i symbole: `\alpha`, `\pi`, `\infty`, `\leq`, `\to`, `\in`;
- indeksy i potęgi: `x_i`, `x^{n+1}`, `a_1^2`;
- ułamki, pierwiastki i funkcje: `\frac{a}{b}`, `\sqrt{x}`, `\sqrt[3]{x}`, `\sin`, `\log`;
- duże operatory: `\sum_{i=1}^{n}`, `\int_0^1`, `\bigcup_i` (także z `\limits`);
- nawiasy dopasowane do wzoru: `\left( … \right)`, `\left\{ … \right\}`, `\big[ … \big]`;
- tekst we wzorze: `\text{gdy } x > 0`;
- macierze, układy równań i wyrównanie: `pmatrix`, `cases`, `aligned`.

Nieznane polecenie nie ginie — pokazuje się tak, jak je napisałeś. Wzory są tylko do czytania: plik zawsze zachowuje LaTeX, a eksport HTML pokazuje gotowy wzór.

Zobacz też [[Obliczenia|Obliczenia]] — tam Brulion liczy liczby, a nie składa wzory.
