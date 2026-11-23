---
tags: [dokumentacja]
brulion: document
---
# Fiszki

Fiszka to pytanie z odpowiedzią, które chcesz zapamiętać: słówko, stolica, data, wzór. Brulion pyta o każdą fiszkę tuż przed tym, zanim byś ją zapomniał — o łatwą rzadko, o trudną częściej — więc kilka minut dziennie wystarcza, żeby pamiętać setki fiszek. Termin powtórki wyznacza algorytm **FSRS**, ten sam, którego używa Anki.

Fiszki to zwykłe wiersze Twoich notatek, zapisane tak, jak zapisują je wtyczka **Spaced Repetition** Obsidiana i **Logseq**, więc notatnik wspólny z tymi aplikacjami działa w obu: każda powtórka zapisuje się obok swojej fiszki, w ich własnym formacie. Fiszki to [[Rozszerzenia|rozszerzenie]] wbudowane w Brulion i włączone od początku; **Ustawienia → Rozszerzenia → Fiszki** je wyłączają i włączają, ustawiają, ile nowych fiszek dziennie się uczysz i jakie tagi robią z notatki talię.

## Jak pisać fiszki
Notatka staje się **talią**, gdy ma tag `#fiszki` (albo `#flashcards`, albo tag pod nim: `#fiszki/hiszpański`) — gdziekolwiek: we właściwościach albo w osobnym wierszu. W takiej notatce:

| Piszesz | Dostajesz |
|---|---|
| `Stolica Peru::Lima` | fiszkę: pytanie przed `::`, odpowiedź po nim |
| `żółw:::turtle` | dwie fiszki, w obie strony: żółw → turtle i turtle → żółw |
| pytanie, wiersz `?`, odpowiedź | fiszkę z kilku wierszy; odpowiedź kończy się na pustym wierszu |
| pytanie, wiersz `??`, odpowiedź | to samo, w obie strony |
| `Najwyższe są ==Rysy== w ==Tatrach==.` | lukę: fiszkę na każdą zakreśloną część, którą pytanie ukrywa jako `[…]` |
| `W ==2;;Toruniu== urodził się ==1;;Kopernik;;astronom==` | luki z numerami: fiszkę na każdy numer; tekst po `;;` to podpowiedź pokazana zamiast `[…]` |
| `{{c1::Żółw::zwierzę}} jest powolny` | luki Anki, numerowane tak samo |

Wiersz w rodzaju `stolica:: Warszawa` — jedno słowo, `::` i spacja, nie punkt listy — to właściwość, nie fiszka; napisz `stolica :: Warszawa` albo `- stolica:: Warszawa`. Fiszki w kodzie — w blokach i w `kodzie w wierszu` — się nie liczą.

W **Logsequ** fiszka to blok z tagiem `#card`: blok jest pytaniem, a bloki pod nim odpowiedzią. `{{cloze Paryż}}` w nim jest ukryte, gdy padnie pytanie, a `{{cloze Paryż \\ miasto}}` pokazuje podpowiedź `(miasto)`. Fiszki z Logseqa działają w Brulionie bez zmian.

## Ustawienia fiszek
Wtyczka Spaced Repetition pozwala zmienić sposób zapisu fiszek i Brulion ma te same ustawienia. Są zapisane w notatniku (w jego `.brulion/config.json`), więc każde urządzenie czyta Twoje notatki tak samo — **Ustawienia → Rozszerzenia → Fiszki**:

- **Separatory** — tekst między pytaniem a odpowiedzią (`::`, a w obie strony `:::`) i wiersz dzielący fiszkę z kilku wierszy (`?`, a w obie strony `??`) mogą być dowolne, na przykład `->` i `<->`. Puste pole wyłącza separator.
- **Koniec fiszki z kilku wierszy** — ustaw na przykład `---`, a fiszka trwa do wiersza, w którym jest tylko to, więc może mieć puste wiersze.
- **Luki** — oprócz zakreśleń, jak w `a ==b==`, lukami mogą być tekst `**pogrubiony**` albo w `{{klamrach}}`; luki Anki, `{{c1::…}}`, są zawsze.
- **Foldery jako talie** — każda notatka w folderze jest czytana, z tagiem czy bez, a jej fiszki są w talii tego folderu: `Języki/Hiszpański` staje się `#języki/hiszpański`.

## Talie
**Fiszki** na pasku bocznym (i na dolnym pasku telefonu, gdy je tam wybierzesz) pokazują, ile fiszek czeka dziś — ta sama liczba jest na ich ikonie. Karta **Talie** pokazuje talie **według tagów** — `#fiszki/hiszpański` z podtaliami pod nim —, **według stron** albo **według folderów**, każdą z liczbą fiszek do powtórki i nowych, których możesz się dziś nauczyć. Stuknięcie talii zaczyna jej sesję; jej menu **⋯** daje też naukę z wyprzedzeniem i statystyki talii. **Ucz się wszystkich** (albo Enter) to nauka wszystkich talii naraz.

Żeby uczyć się tylko fiszek ze strony, którą czytasz, uruchom **Powtórz fiszki z tej strony** z palety poleceń (Ctrl+K).

## Sesja
Fiszka pokazuje pytanie, duże, a nad nim stronę i nagłówki, z których pochodzi. Przypomnij sobie odpowiedź, potem:

- **Pokaż odpowiedź** (spacja) — odpowiedź pojawia się pod kreską;
- oceń, jak dobrze ją znałeś: **Znowu** (1) — zapomniałeś, **Trudne** (2), **Dobrze** (3), **Łatwe** (4). Każdy przycisk mówi, kiedy fiszka wróci: nowa za minutę albo dziesięć, dopóki jej nie umiesz, znana za dni, tygodnie, miesiące;
- **Cofnij** (U) cofa ostatnią ocenę — fiszka wraca z pokazaną odpowiedzią;
- **Edytuj** otwiera notatkę w wierszu fiszki; menu **⋯** potrafi zacząć fiszkę od nowa.

Nowe fiszki przychodzą po powtórkach, najwyżej 20 dziennie (Ustawienia zmieniają tę liczbę). Gdy wszystko zrobione, Brulion mówi, kiedy następna powtórka; **Ucz się z wyprzedzeniem** daje jutrzejsze powtórki, a możesz też nauczyć się dziesięciu kolejnych nowych fiszek. Karta **Statystyki** pokazuje, ile fiszek oceniłeś każdego dnia, ile powtórek zapamiętałeś (celem jest około 90 %), ile fiszek jest nowych, w nauce, młodych i dojrzałych i ile wróci w kolejnych dniach.

## Gdzie zapisują się powtórki
Po powtórce wiersz fiszki dostaje na końcu komentarz — niewidoczny w Brulionie, w trybie czytania Obsidiana i w Logsequ:

```markdown
Stolica Peru::Lima <!--SR:!fsrs,2026-11-23T08:00:00.000Z,3,3.17,5.28,2,2,0,0,2026-11-20T08:00:00.000Z-->
```

To własny format wtyczki Spaced Repetition, więc wtyczka ustawiona na FSRS kontynuuje od niego (a fiszki, które zaplanowała algorytmem SM-2 — `<!--SR:!2026-11-25,10,250-->` — Brulion też kontynuuje). Fiszka z Logseqa dostaje właściwości Logseqa `card-…::`, więc Logseq wie, kiedy o nią zapytać. Sam tekst fiszki nigdy się nie zmienia.

Każda ocena to też wiersz dziennika powtórek, jednej strony na miesiąc: `Fiszki/Powtórki 2026-11` (w angielskim notatniku `Flashcards/Reviews 2026-11`). Z niego biorą się statystyki; możesz go usunąć w każdej chwili, wtedy tylko statystyki zaczną się od nowa.

Powtórka zmienia jeden wiersz, więc dwa urządzenia, które powtarzają różne fiszki jednej notatki, scalają się czysto przez Nextcloud, a gdy powtórzysz tę samą fiszkę na obu, zostaje późniejsza powtórka. Z Syncthingiem dzieje się to samo, gdy scalisz kopię konfliktu w centrum synchronizacji.

> [!tip] Z wiersza poleceń
> `brulion-cli card due` wypisuje fiszki do nauki, a `brulion-cli card review <id> dobrze` ocenia jedną — tak może Cię przepytać agent AI; zobacz [[Wiersz poleceń i agenci AI]].

## Spróbuj
Ta strona też jest talią: fiszki poniżej czekają w **Fiszkach**.

#fiszki/brulion

Gdzie Brulion zapisuje powtórkę fiszki?::obok fiszki, w komentarzu wtyczki Spaced Repetition
Jaki algorytm wyznacza terminy powtórek?::FSRS
Co cofa Cofnij w sesji?
?
Ostatnią ocenę — fiszka wraca z pokazaną odpowiedzią.

Brulion pyta o fiszkę tuż przed tym, zanim byś ją ==zapomniał==.
