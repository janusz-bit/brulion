---
tags: [dokumentacja]
brulion: document
---
# Przypomnienia i alarmy

Przypomnienie to znacznik `⏰` z datą i godziną w dowolnym bloku — nie tylko w zadaniu.

| Zapis | Działanie |
|---|---|
| `⏰ 2026-10-20 09:00` | powiadomienie o tej chwili |
| `⏰ 2026-10-20` | powiadomienie o domyślnej godzinie z ustawień |
| `⏰! 2026-10-20 06:30` | **alarm** — głośny, o wysokim priorytecie, trwa, dopóki go nie wyłączysz |

Czas jest czasem lokalnym, zapisanym bez strefy; „9:00” znaczy 9:00 u Ciebie.

## Dodawanie
- Wpisz `⏰` i napisz po ludzku: „jutro o 7:30”, „za 2 godziny”, „w piątek 18:00”.
- Albo użyj „Przypomnienie…” lub „Alarm…” z menu `/`: okno z szybkimi terminami, kalendarzem, godziną, przełącznikiem alarmu i powtarzaniem.
- Kliknięcie znaczka ⏰ w notatce otwiera okno, w którym możesz je zmienić, odłożyć albo usunąć.
- W szybkiej notatce (Ctrl+Shift+N) wystarczy napisać datę w treści.

## Powtarzanie
Jeśli blok ma też regułę `🔁` (zobacz [[Zadania|Zadania]]), przypomnienie powtarza się razem z nią. Przypomnienie przy zadaniu zrobionym lub anulowanym nie dzwoni.

## Powiadomienia
- Dotknięcie powiadomienia otwiera notatkę na właściwym bloku.
- Tytuł powiadomienia to tekst bloku bez znaczników.
- Na Androidzie przypomnienia i alarmy działają przy zamkniętej aplikacji, także po restarcie telefonu; wymagają zgody na powiadomienia i dokładne alarmy, o które aplikacja poprosi.
- Na komputerze Brulion działa w zasobniku, jeśli włączysz autostart, i stamtąd powiadamia.
- Przypomnienia, które minęły przy wyłączonym urządzeniu, czekają w dzienniku, gdzie pokazuje je baner z przypomnieniami do obsłużenia.

## Który notatnik przypomina
Przypomnienia i alarmy pochodzą z otwartego notatnika. Gdy przełączysz się na inny swój notatnik, Brulion odlicza już tylko jego przypomnienia, a poprzedniego czekają, aż do niego wrócisz:
- na Androidzie alarmy poprzedniego notatnika zostają wyłączone, a te, których czas minie, zanim wrócisz, przepadają — nie zadzwonią ani nie pojawią się później;
- na komputerze po powrocie od razu pojawią się przypomnienia spóźnione najwyżej 6 godzin i alarmy spóźnione nie dłużej, niż alarm dzwoni; starsze trafią na listę przegapionych.

Wyjątkiem jest ta dokumentacja: nie ma własnych przypomnień, więc gdy ją czytasz, na Androidzie alarmy Twojego notatnika dzwonią dalej, a dotknięcie powiadomienia otwiera notatkę w Twoim notatniku. Na komputerze przypomnienia, które minęły w tym czasie, pokażą się po powrocie, jak wyżej.

## Gdzie je znajdziesz
Przypomnienia i alarmy widać w **Kalendarzu** (miesiąc, tydzień, agenda) i w zapytaniach: `type:reminders remind:this-week` — zobacz [[Zapytania|Zapytania]]. Domyślną godzinę ustawisz w *Ustawienia → Notatnik*.
