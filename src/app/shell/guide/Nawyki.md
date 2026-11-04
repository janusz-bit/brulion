---
tags: [dokumentacja]
brulion: document
---
# Nawyki

Nawyk to punkt, który chcesz odhaczać regularnie: medytacja, bieganie, czytanie. Brulion liczy serię dni bez przerwy, pokazuje, jak często się udaje, i rysuje kalendarz miesiąca. Wszystko zostaje w zwykłych notatkach: nawyk to punkt z właściwością, a każde odhaczenie to zadanie w dzienniku danego dnia — więc synchronizuje się jak każda notatka i jest czytelne w Obsidianie i Logsequ.

## Widok Nawyki
Otwórz **Nawyki** z paska bocznego (pod Zadaniami) albo z palety poleceń (Ctrl+K, „Nawyki”). Każdy nawyk ma kartę:

- **kółko** po lewej odhacza nawyk na dziś; drugie kliknięcie cofa odhaczenie. Prawy przycisk myszy albo przytrzymanie palcem daje też **Pomiń** — dzień usprawiedliwiony, który nie przerywa serii (choroba, wyjazd);
- pod nazwą jest **harmonogram** w słowach, a przy nawykach „N razy w tygodniu” — postęp tygodnia, np. „2 z 3 w tym tygodniu”;
- płomień pokazuje **serię** (ile razy z rzędu się udało), najdłuższą serię i **skuteczność** w procentach;
- **kalendarz miesiąca**: zielone dni są zrobione, czerwone — opuszczone, przekreślone — pominięte, dzisiejszy dzień ma obwódkę. Kliknięcie minionego dnia odhacza go albo cofa odhaczenie (gdy zapomnisz wpisać wczoraj). Strzałki nad kartami zmieniają miesiąc.

Menu „⋯” karty otwiera stronę, na której nawyk jest zapisany, albo dzisiejszy dziennik. Każde odhaczenie z widoku można cofnąć przyciskiem **Cofnij** w powiadomieniu.

## Nowy nawyk
Przycisk **Dodaj nawyk** (albo klawisz N w widoku) pyta o nazwę i o to, jak często. Brulion od razu pokazuje, jak rozumie harmonogram. Nawyk trafia na stronę z pierwszym nawykiem, a gdy żadnego jeszcze nie ma — na nową stronę „Nawyki”.

Możesz też napisać nawyk sam, w dowolnej notatce: to punkt listy z właściwością `habit::` (albo `nawyk::`), której wartością jest harmonogram. Na przykład te trzy punkty to prawdziwe nawyki tego notatnika — zajrzyj do widoku Nawyki:

- Medytacja
  habit:: codziennie
- Bieganie #zdrowie
  habit:: pn, śr, pt
- Czytanie
  habit:: 3 razy w tygodniu

Nazwą jest tekst punktu (tagi i nawiasy linków się nie liczą, wielkość liter też nie). Całą stronę zrobisz nawykiem, dopisując `habit: codziennie` w jej nagłówku YAML — nazwą jest wtedy tytuł strony. Właściwość `start::` z datą (`start:: 2026-11-01`) mówi, od kiedy liczyć; bez niej liczenie zaczyna się od pierwszego odhaczenia.

## Harmonogramy
| Zapis | Kiedy |
|---|---|
| `codziennie`, `co 2 dni` | każdego dnia, co drugi dzień |
| `pn, śr, pt`, `poniedziałek i czwartek`, `w dni robocze` | w te dni tygodnia |
| `co 2 tygodnie w poniedziałek` | co drugi poniedziałek |
| `co miesiąc 15.`, `co miesiąc ostatniego` | w ten dzień miesiąca |
| `3 razy w tygodniu`, `raz w tygodniu`, `dwa razy w miesiącu`, `co tydzień` | dowolne dni, byle tyle razy w tygodniu (miesiącu) |

Rozumiane są też zapisy angielskie: `every day`, `mon, fri`, `3 times a week`. Harmonogramu, którego Brulion nie rozumie (np. „co drugi wtorek”), karta nie liczy — pokazuje ostrzeżenie, żeby go poprawić.

## Odhaczanie w dzienniku
Odhaczenie to zadanie w dzienniku z tagiem `#nawyk` (albo `#habit`) i nazwą nawyku:

```markdown
- [x] Medytacja #nawyk ✅ 2026-11-03
- [-] Bieganie #nawyk ❌ 2026-11-03
- [ ] Czytanie #nawyk
```

`[x]` to zrobione, `[-]` (anulowane) — pominięte, a `[ ]` — jeszcze nie. Widok Nawyki zapisuje właśnie takie linie (obok pozostałych odhaczeń dnia), ale możesz je pisać i odhaczać sam w dzienniku — liczą się tak samo. Wygodnie jest dopisać nawyki do szablonu dziennika (zobacz [[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]]): każdy nowy dzień zaczyna się wtedy od listy `- [ ] Medytacja #nawyk` do odhaczenia.

## Jak liczona jest seria
- Przy nawykach na konkretne dni liczą się tylko te dni. Dzień, w którym nawyk był zaplanowany, ale nie został odhaczony — także dzień **bez notatki w dzienniku** — przerywa serię. Dzisiejszy dzień jej nie przerywa, dopóki trwa.
- Przy nawykach „N razy w tygodniu” liczą się całe tygodnie (miesiące): tydzień z co najmniej N odhaczeniami przedłuża serię. Każdy pominięty dzień zmniejsza wymaganą liczbę o jeden. Pierwszy, niepełny tydzień liczy się tylko wtedy, gdy się udał.
- Pominięte dni (`[-]`) nie przerywają serii i nie obniżają skuteczności.
- Dodatkowe odhaczenie w dzień, w którym nawyk nie był zaplanowany, widać w kalendarzu, ale nie zmienia serii.

> [!tip] Z wiersza poleceń
> `brulion-cli habit list` pokazuje nawyki z serią, a `brulion-cli habit tick Medytacja` odhacza nawyk na dziś — zobacz [[Wiersz poleceń i agenci AI|Wiersz poleceń i agenci AI]].
