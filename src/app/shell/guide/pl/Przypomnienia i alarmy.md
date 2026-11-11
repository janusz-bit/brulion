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
- Datę można zamienić na przypomnienie: okno daty (przycisk z kalendarzem przy zadaniu, „Wybierz datę…”, „Zmień datę…” na chipie daty) ma nad kalendarzem **Przypomnienie** i **Alarm**. Pytają o godzinę i zapisują `⏰` albo `⏰!` w miejscu daty 📅 ⏳ 🛫.
- Kliknięcie znaczka ⏰ w notatce otwiera okno, w którym możesz je zmienić, odłożyć albo usunąć. Zostaje on wyróżnionym chipem także wtedy, gdy edytujesz jego linię: kliknij go (na telefonie stuknij), żeby wybrać inną datę i godzinę; kursor zostaje tam, gdzie był. Żeby zmienić jego tekst z klawiatury, wejdź do niego kursorem strzałkami.
- Prawy przycisk myszy na znaczku ⏰ (na telefonie przytrzymanie palcem) otwiera menu: „Zmień datę i godzinę…”, „Godzinę później”, „Jutro o tej porze”, „Tydzień później”, przełącznik „Alarm”, podmenu **Powtarzanie** (gotowe reguły, „Zmień regułę…”, „Nie powtarzaj”) i — przy zadaniu — **Priorytet**, oraz „Usuń przypomnienie”. Każdą zmianę cofniesz (`Ctrl+Z`). To samo menu jest w wynikach zapytań, we wstawionych blokach, w tabelach, w widoku Zadania i w kalendarzu.
- W kalendarzu przypomnienie cykliczne pokazuje się w każdym dniu serii. Menu takiego dnia zmienia całą serię („Zmień całą serię…”). Gdy ten raz już minął, „Godzinę później” i „Jutro o tej porze” odkładają tylko jego — tak jak **Drzemka** w powiadomieniu. Przy terminie, który dopiero nadejdzie, menu nie ma tych pozycji, bo przypomnienie i tak by zadzwoniło.
- Zmiana reguły 🔁 zmienia tylko samą regułę: słowa zapisane po niej zostają. Gdy zmieniasz tylko godzinę, reguła zostaje zapisana tak jak była (np. „co tydzień”).
- W szybkiej notatce (Ctrl+Shift+N) wystarczy napisać datę w treści.

## Powtarzanie
Jeśli blok ma też regułę `🔁` (zobacz [[Zadania|Zadania]]), przypomnienie powtarza się razem z nią. Przypomnienie przy zadaniu zrobionym lub anulowanym nie dzwoni.

## Powiadomienia
- Dotknięcie powiadomienia otwiera notatkę na właściwym bloku.
- Tytuł powiadomienia to tekst bloku bez znaczników.
- Przypomnienie przy zadaniu ma w powiadomieniu przycisk **Zrobione** (obok **Drzemka** i **OK**): oznacza zadanie jako zrobione — `[x]` z datą `✅` — bez otwierania notatki, tak jak zmiana stanu w edytorze. Zadanie cykliczne dostaje od razu następny termin. Gdy w międzyczasie zmienisz to przypomnienie albo zadanie (także na innym urządzeniu), Brulion niczego nie zmienia i o tym powie. Przycisk jest w powiadomieniach na Linuksie, na Windows i na Androidzie — tam także na ekranie alarmu.
- Na Androidzie **Zrobione** działa też wtedy, gdy Brulion jest w tle albo zamknięty: zmiana trafia do notatki, gdy wrócisz do Brulionu, albo przy jego następnym uruchomieniu, gdy notatnik z tym zadaniem jest już wczytany (pokaże wtedy komunikat), a data `✅` to dzień, w którym stuknąłeś przycisk. Do tej chwili stuknięcie czeka w telefonie, więc nie przepadnie, nawet gdy zamkniesz Brulion wcześniej albo najpierw otworzysz inny notatnik. Do tego czasu inne urządzenia widzą zadanie jako otwarte.
- Na Androidzie przypomnienia i alarmy działają przy zamkniętej aplikacji, także po restarcie telefonu; wymagają zgody na powiadomienia i dokładne alarmy, o które aplikacja poprosi.
- Na komputerze Brulion działa w zasobniku, jeśli włączysz autostart, i stamtąd powiadamia.
- Na Windows przypomnienia i alarmy są powiadomieniami Windows z przyciskami **Drzemka** i **OK** (przy zadaniu także **Zrobione**); kliknięcie otwiera notatkę. Alarm zostaje na ekranie, a jego dźwięk powtarza się, aż odpowiesz (najdłużej przez czas dzwonienia alarmu). Dźwięk wybierzesz spośród dźwięków powiadomień Windows w *Ustawienia → Przypomnienia*. Powiadomienia działają, dopóki Brulion działa — także po zamknięciu okna, w zasobniku. Gdy w ustawieniach Windows wyłączysz powiadomienia Brulionu, przypomnienia pokaże ikona w zasobniku.
- Na Windows *Ustawienia → Aplikacja → Uruchamiaj po zalogowaniu* uruchamia Brulion ukryty w zasobniku zaraz po zalogowaniu, żeby przypomnienia dzwoniły od początku dnia. Instalator proponuje to samo.
- Przypomnienia, które minęły przy wyłączonym urządzeniu, czekają w dzienniku, gdzie pokazuje je baner z przypomnieniami do obsłużenia: **Otwórz**, **Drzemka**, przy zadaniu **Zrobione** i odrzucenie.

## Przypomnienia z innych urządzeń
Przypomnienie dopisane na komputerze (albo przez agenta AI z `brulion-cli`) dociera do telefonu razem z notatką, przez Syncthing albo Nextcloud. Na Androidzie zadzwoni także wtedy, gdy Brulion jest zamknięty: co jakiś czas telefon sam sprawdza notatki w tle — przy Nextcloud najpierw je synchronizuje — i nastawia to, co znalazł. Nowe przypomnienia dochodzą, a usunięte albo przesunięte na komputerze znikają albo się przesuwają.
- Jak często, wybierzesz w *Ustawienia → Przypomnienia → Sprawdzaj notatki w tle*: co 15 minut, co 30 minut (domyślnie), co godzinę albo wcale. Pod tym ustawieniem widać, kiedy było ostatnie sprawdzenie.
- Android może odłożyć sprawdzanie, gdy telefon długo leży nieużywany (nawet o godzinę lub więcej), więc przypomnienie na „za 10 minut” może nie zdążyć, a to na jutro rano zdąży. Pomaga ustawienie baterii Brulionu na „Bez ograniczeń”.
- Gdy notatnika nie da się przeczytać (np. karta pamięci jest odłączona), nastawione przypomnienia zostają bez zmian.

## Który notatnik przypomina
Przypomnienia i alarmy pochodzą z otwartego notatnika. Gdy przełączysz się na inny swój notatnik, Brulion odlicza już tylko jego przypomnienia, a poprzedniego czekają, aż do niego wrócisz:
- na Androidzie alarmy poprzedniego notatnika zostają wyłączone, a te, których czas minie, zanim wrócisz, przepadają — nie zadzwonią ani nie pojawią się później;
- na komputerze po powrocie od razu pojawią się przypomnienia spóźnione najwyżej 6 godzin i alarmy spóźnione nie dłużej, niż alarm dzwoni; starsze trafią na listę przegapionych.

Wyjątkiem jest ta dokumentacja: nie ma własnych przypomnień, więc gdy ją czytasz, na Androidzie alarmy Twojego notatnika dzwonią dalej, a dotknięcie powiadomienia otwiera notatkę w Twoim notatniku. Na komputerze przypomnienia, które minęły w tym czasie, pokażą się po powrocie, jak wyżej.

## Gdzie je znajdziesz
Przypomnienia i alarmy widać w **Kalendarzu** (miesiąc, tydzień, agenda) i w zapytaniach: `type:reminders remind:this-week` — zobacz [[Zapytania|Zapytania]]. Domyślną godzinę ustawisz w *Ustawienia → Notatnik*.
