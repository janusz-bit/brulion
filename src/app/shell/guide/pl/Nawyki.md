---
tags: [dokumentacja]
brulion: document
---
# Nawyki

Nawyk to punkt, który chcesz odhaczać regularnie: medytacja, bieganie, czytanie. Brulion liczy serię dni bez przerwy, pokazuje, jak często się udaje, i rysuje kalendarz miesiąca. Wszystko zostaje w zwykłych notatkach: nawyk to punkt z właściwością, a każde odhaczenie to zadanie w dzienniku danego dnia — więc synchronizuje się jak każda notatka i jest czytelne w Obsidianie i Logsequ.

Nawyki to [[Rozszerzenia|rozszerzenie]] wbudowane w Brulion i włączone od początku. **Ustawienia → Rozszerzenia → Nawyki** je wyłączają (razem z nimi znikają Nawyki z paska bocznego i polecenie z palety, a notatki zostają, jakie były) i włączają z powrotem; **Kod nawyków → Edytuj kod** umieszcza ich kod na stronie Twojego notatnika, gdzie możesz go przeczytać i zmienić ([[Rozszerzenia#Kod Siłowni, Nawyków, Fiszek i Asystenta]]).

## Widok Nawyki
Otwórz **Nawyki** z paska bocznego (pod Zadaniami) albo z palety poleceń (Ctrl+K, „Nawyki”). Na górze widać, ile dzisiejszych nawyków jest już zrobionych („2 z 3 zrobione dziś”), a każdy nawyk ma kartę:

- **kółko** po lewej odhacza nawyk na dziś; drugie kliknięcie cofa odhaczenie. Prawy przycisk myszy albo przytrzymanie palcem otwiera menu: **Zrobione dziś**, **Pomiń dziś** — dzień usprawiedliwiony, który nie przerywa serii (choroba, wyjazd) —, **Bez odhaczenia dziś**, **Otwórz definicję** (stronę, na której nawyk jest zapisany, w jego wierszu) i **Otwórz dzisiejszy dziennik**. Przycisk **„⋯”** karty ma to samo menu;
- obok nazwy jest **harmonogram** w słowach;
- płomień pokazuje **serię** (ile razy z rzędu się udało), najdłuższą serię, **skuteczność** w procentach, a przy nawykach „N razy w tygodniu” także postęp tygodnia, np. „2 z 3 w tym tygodniu”;
- **kalendarz miesiąca**: zielone dni są zrobione, czerwone — opuszczone, przekreślone — pominięte, dzień, w którym nawyk wypada, ma cienką obwódkę, a dzisiejszy — wyraźną. Kliknięcie minionego dnia odhacza go albo cofa odhaczenie (gdy zapomnisz wpisać wczoraj); prawy przycisk albo przytrzymanie dnia daje do wyboru: zrobione, pominięte (seria trwa), bez odhaczenia, dziennik tego dnia i definicję nawyku. Strzałki nad kartami (albo ← i →, PgUp i PgDn) zmieniają miesiąc, a **Ten miesiąc** wraca do bieżącego.

Każde odhaczenie z widoku można cofnąć przyciskiem **Cofnij** w powiadomieniu. Nawyk, którego harmonogramu Brulion nie rozumie, albo zapisany dwa razy, ma kartę, która o tym mówi, z linkiem do miejsca, gdzie jest zapisany.

## Nowy nawyk
Przycisk **Dodaj nawyk** (albo klawisz N w widoku) pyta o nazwę i o to, jak często — „codziennie”, „pn, śr, pt”, „3 razy w tygodniu”; bez harmonogramu nawyk jest codzienny. Gdy Brulion nie rozumie harmonogramu albo nawyk o tej nazwie już jest, mówi o tym i pyta jeszcze raz, z tym, co wpisałeś. Nawyk trafia na stronę z pierwszym nawykiem, a gdy żadnego jeszcze nie ma — na nową stronę „Nawyki”; przycisk **Otwórz** w powiadomieniu prowadzi na nią.

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

`[x]` to zrobione, `[-]` (anulowane) — pominięte, a `[ ]` — jeszcze nie. Widok Nawyki zapisuje właśnie takie linie — odhacza wiersz nawyku z tego dnia, gdy już jest, a gdy go nie ma, dopisuje `- [x] Medytacja #nawyk ✅` z dzisiejszą datą obok pozostałych odhaczeń dnia — ale możesz je pisać i odhaczać sam w dzienniku: liczą się tak samo. Gdy jeden dzień ma dwie notatki w dzienniku (na przykład kopię zostawioną przez synchronizację), liczą się odhaczenia z obu, a widok zagląda do obu, zanim coś zapisze. Wygodnie jest dopisać nawyki do szablonu dziennika (zobacz [[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]]): każdy nowy dzień zaczyna się wtedy od listy `- [ ] Medytacja #nawyk` do odhaczenia.

## Jak liczona jest seria
- Przy nawykach na konkretne dni liczą się tylko te dni. Dzień, w którym nawyk był zaplanowany, ale nie został odhaczony — także dzień **bez notatki w dzienniku** — przerywa serię. Dzisiejszy dzień jej nie przerywa, dopóki trwa.
- Przy nawykach „N razy w tygodniu” liczą się całe tygodnie (miesiące): tydzień z co najmniej N odhaczeniami przedłuża serię. Każdy pominięty dzień zmniejsza wymaganą liczbę o jeden. Pierwszy, niepełny tydzień liczy się tylko wtedy, gdy się udał.
- Pominięte dni (`[-]`) nie przerywają serii i nie obniżają skuteczności.
- Dodatkowe odhaczenie w dzień, w którym nawyk nie był zaplanowany, widać w kalendarzu, ale nie zmienia serii.

> [!tip] Z wiersza poleceń
> `brulion-cli habit list` pokazuje nawyki z serią, a `brulion-cli habit tick Medytacja` odhacza nawyk na dziś — zobacz [[Wiersz poleceń i agenci AI|Wiersz poleceń i agenci AI]].
