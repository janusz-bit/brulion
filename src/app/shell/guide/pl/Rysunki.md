---
tags: [dokumentacja]
aliases: [Rysunek, Rysowanie, Notatki odręczne, Schematy, Kształty, Notatki na slajdach, Podkład]
brulion: document
---
# Rysunki

W notatce możesz rysować i pisać odręcznie — rysikiem na tablecie, palcem na telefonie albo myszką na komputerze — a także rysować schematy: prostokąty, elipsy, romby, linie, strzałki, trójkąty, gwiazdy, dymki, chmurki i inne kształty oraz tekst. Rysunek to zwykły obraz w notatniku, więc widać go także w Obsidianie, w przeglądarce i w menedżerze plików, a Syncthing synchronizuje go jak każdy inny plik.

## Nowy rysunek
Wpisz w bloku `/Rysunek` (albo `/szkic`, `/rysuj`) i naciśnij Enter. Na telefonie jest też przycisk **Rysunek** na pasku nad klawiaturą, a w menu bloku (prawy przycisk myszy albo przytrzymanie punktora) — **Wstaw rysunek**. Brulion zapisuje w folderze załączników pusty rysunek `Rysunek 2026-10-27 14.32.svg`, wstawia go do notatki jako `![[Rysunek 2026-10-27 14.32.svg]]` i od razu otwiera do rysowania.

Rysunek w notatce ma w rogu ikonę pióra. Kliknij go albo stuknij, żeby rysować dalej. W widoku samego obrazu (link do pliku) jest przycisk **Edytuj rysunek**.

## Narzędzia
- **Pióro** (klawisz P) — linia grubieje, gdy mocniej naciskasz rysik. Myszką i palcem linia jest cieńsza, gdy prowadzisz szybciej, jak prawdziwy atrament.
- **Zakreślacz** (H) — szeroka, półprzezroczysta linia, np. do zaznaczania fragmentów notatek z wykładu.
- **Gumka** (E) — usuwa każdą linię, kształt albo tekst, którego dotknie (kształt razem z napisem w środku). Drugi koniec rysika (gumka na rysiku) albo wciśnięty przycisk na rysiku ściera zawsze, bez przełączania narzędzia.
- **Zaznacz** (V, ikona lassa albo przerywanego kwadratu) — lassem albo prostokątem, zobacz niżej [[#Zaznaczanie lassem|Zaznaczanie lassem]].
- **Kształty** — prostokąt (R), elipsa (O), romb (D), linia (L), strzałka (A) i jedenaście kolejnych na liście, zobacz niżej [[#Kształty i strzałki|Kształty i strzałki]].
- **Tekst** (T) — zobacz niżej [[#Tekst|Tekst]].
- **Kolory i grubości** — sześć kolorów pióra (atrament, niebieski, czerwony, zielony, pomarańczowy, fioletowy), pięć kolorów zakreślacza i trzy grubości każdego; dla tekstu cztery rozmiary: mały, średni, duży i bardzo duży. Na telefonie są pod kółkiem z bieżącym kolorem. Brulion pamięta ostatni wybór na tym urządzeniu.
- **Papier** — gładki, w linie albo w kratkę (na telefonie w menu **Więcej**).
- **Tło rysunku** — jasne albo ciemne, zobacz niżej [[#Jasne i ciemne tło|Jasne i ciemne tło]] (na telefonie w menu **Więcej**).
- **Cofnij** i **Ponów** (`Ctrl+Z`, `Ctrl+Shift+Z`) — cofają linie, kształty, tekst, starcie gumką, każdą zmianę zaznaczenia (przesunięcie, rozmiar, obrót, odbicie, kolor, styl, kopię, grupowanie, usunięcie) oraz zmianę papieru i tła.
- **Pokaż cały rysunek** (`Ctrl+0`) i **Gotowe** — powrót do notatki.

Na telefonie pasek narzędzi jest na dole ekranu. Gdy telefon trzymasz pionowo, pasek ma dwa wiersze: u góry narzędzia i kółko koloru, niżej **Cofnij**, **Ponów**, **Więcej** i **Gotowe** — wszystkie przyciski są na ekranie.

## Kształty i strzałki
Przycisk kształtu na pasku pokazuje ostatnio używany kształt (mała strzałka w jego rogu oznacza listę pod nim); kliknij go jeszcze raz, gdy kształt jest wybrany, a rozwinie się lista: **Prostokąt** (R), **Elipsa** (O), **Romb** (D), **Linia** (L), **Strzałka** (A), a pod nimi **Trójkąt**, **Trójkąt prostokątny**, **Równoległobok**, **Trapez**, **Pięciokąt**, **Sześciokąt**, **Gwiazda**, **Strzałka blokowa**, **Dymek**, **Serce** i **Chmurka**.
- Przeciągnij po kartce — kształt rośnie razem z ruchem rysika, palca albo myszy. Z wciśniętym `Shift` prostokąt staje się kwadratem, elipsa kołem, a linia i strzałka obracają się co 15°; pozostałe kształty rysują się wtedy w kwadracie.
- Kształt ma kolor pióra i dwa razy większą grubość (cienka, średnia, gruba). Prostokąty mają zaokrąglone rogi, jak w Excalidraw.
- Narzędzie zostaje wybrane, więc możesz narysować kilka kształtów po kolei. Pióro wybierzesz klawiszem P.
- Kształty z dolnej części listy to w pliku zwykłe zamknięte linie Excalidraw, więc excalidraw.com i Obsidian też je pokażą i pozwolą edytować. Mogą mieć napis w środku, tak jak prostokąt (zobacz [[#Tekst|Tekst]]): excalidraw.com zna napisy tylko w prostokącie, elipsie, rombie i na strzałkach, więc Brulion zapisuje napis w trójkącie czy gwieździe jako tekst zgrupowany z kształtem — w excalidraw.com widać go w tym samym miejscu i przesuwa się razem z kształtem.

## Tekst
- Wybierz **Tekst** (T) i stuknij w kartkę — pojawia się pole do pisania. `Enter` zaczyna nowy wiersz.
- Kliknięcie obok pola, `Esc`, `Ctrl+Enter` albo przycisk ✓ (na ekranie dotykowym) kończy pisanie. Pusty tekst znika.
- Stuknij w istniejący tekst, żeby go poprawić. Stuknij w środek prostokąta, elipsy albo rombu, a napiszesz w nim etykietę: wyśrodkowaną i zawiniętą tak jak w Excalidraw — w elipsie i rombie węższą, żeby zmieściła się w środku. Tak samo w trójkącie, gwieździe, dymku i pozostałych kształtach z listy: napis staje w najszerszym miejscu kształtu (w trójkącie niżej, w dymku w jego „chmurce”). Gdy tekst się nie mieści, kształt rośnie. Etykieta przesuwa się, obraca i usuwa razem z kształtem.
- Pole zamknięte bez zmian niczego w rysunku nie zmienia — tekst z Excalidraw zostaje dokładnie taki, jaki był.
- Narzędziem **Zaznacz** to samo robi podwójne kliknięcie (albo dwa stuknięcia).
- Na telefonie kartka przesuwa się tak, żeby pisany tekst był nad klawiaturą.

## Zaznaczanie lassem
Wybierz **Zaznacz** (V) i obrysuj rysikiem, palcem albo myszką to, co chcesz zaznaczyć — zaznaczy się wszystko, co jest przynajmniej w połowie w środku. Kliknij **Zaznacz** jeszcze raz, gdy jest już wybrane (on też ma małą strzałkę w rogu), a wybierzesz sposób: **Lassem** albo **Prostokątem** (przeciągasz prostokąt zamiast obrysowywać). Brulion pamięta wybór na tym urządzeniu. Stuknięcie zaznacza jeden element, stuknięcie w puste miejsce zdejmuje zaznaczenie, a `Shift` dodaje do zaznaczenia (myszką).
- **Przeciągnij** wewnątrz ramki zaznaczenia, żeby je przesunąć. Strzałki przyczepione do przesuwanych kształtów idą za nimi.
- Nad zaznaczeniem pojawia się pasek: **kolory** (zmiana koloru), **Styl** (zobacz niżej [[#Styl zaznaczenia|Styl zaznaczenia]]), **Duplikuj** (`Ctrl+D`), **Na wierzch** (`Ctrl+Shift+]`), **Na spód** (`Ctrl+Shift+[`), **Więcej** i **Usuń** (`Delete`). Na telefonie **Na wierzch** i **Na spód** są w **Więcej**.
- **Więcej** ma też **Kopiuj** (`Ctrl+C`), **Wytnij** (`Ctrl+X`), **Obróć o 90°**, **Odbij w poziomie** (`Shift+H`), **Odbij w pionie** (`Shift+V`), **Grupuj** (`Ctrl+G`) i **Rozgrupuj** (`Ctrl+Shift+G`). Tekst przy odbiciu zostaje czytelny — zmienia tylko miejsce. Grupa zaznacza się i przesuwa w całości.
- Klawisze: `Ctrl+A` zaznacza wszystko, strzałki przesuwają zaznaczenie (z `Shift` dziesięć razy dalej), `Ctrl+]` i `Ctrl+[` przesuwają je o jedno piętro w górę i w dół, `Ctrl+Shift+←`, `→`, `↑`, `↓` wyrównują do lewej, prawej, góry i dołu, `Alt+H` i `Alt+V` rozkładają równo w poziomie i w pionie, `Esc` zdejmuje zaznaczenie.
- Grupy z Excalidraw zaznaczają się w całości. Elementów zablokowanych w Excalidraw — ani napisów w zablokowanych kształtach — nie da się zaznaczyć, zetrzeć ani zmienić.
- Gdy **Rysuj palcem** jest wyłączone, palec tylko przesuwa kartkę — dłoń oparta na zaznaczeniu go nie przesunie.

## Zmiana rozmiaru i obracanie
Zaznaczenie ma na ramce uchwyty — przeciągnij je rysikiem, myszką albo palcem (przy włączonym **Rysuj palcem**):
- **Kwadraty w rogach i na środkach boków** zmieniają rozmiar, a przeciwległy róg albo bok zostaje na miejscu. `Shift` zachowuje proporcje. Grubość linii się nie zmienia.
- **Kółko nad ramką** (pod ramką, gdy nad nią nie ma miejsca) obraca zaznaczenie; z `Shift` co 15°. Ramka jednego elementu obraca się razem z nim.
- Obraz i tekst zachowują proporcje (obraz bez nich z `Shift`), a większy tekst ma po prostu większe litery. Kilka zaznaczonych elementów skaluje się razem z rogów ramki, zawsze z zachowaniem proporcji.
- Napis w kształcie zawija się na nowo, a kształt nie zrobi się mniejszy, niż potrzeba, żeby go pomieścić. Strzałki przyczepione do kształtu idą za nim.
- `Esc` w trakcie przeciągania rezygnuje ze zmiany.

## Styl zaznaczenia
Przycisk **Styl** na pasku nad zaznaczeniem pokazuje to, co zaznaczone elementy mają:
- **Wypełnienie** i **Wzór wypełnienia** (pełne, kreskowane, w kratkę) — kształty;
- **Grubość linii** (cienka, średnia, gruba) — wszystko, co jest rysowane linią;
- **Rodzaj linii** (ciągła, kreskowana, kropkowana) — kształty, linie i strzałki;
- **Rogi** (ostre, zaokrąglone) — prostokąty, romby, a na liniach i strzałkach łagodny łuk zamiast załamań;
- **Początek linii** i **Koniec linii** (brak, grot, trójkąt, kółko, kreska, romb) — linie i strzałki;
- **Rozmiar tekstu**;
- **Wyrównaj** — gdy zaznaczone są co najmniej dwie rzeczy: do lewej, do środka w poziomie, do prawej, do góry, do środka w pionie, do dołu; przy trzech i więcej także **Rozłóż w poziomie** i **Rozłóż w pionie** (skrajne zostają na miejscu, pozostałe dostają równe odstępy). Grupa, kształt z napisem i każdy inny element przesuwają się w całości, według ramki, którą zajmują — obrócony element według swojego obróconego obrysu. Strzałki przyczepione do kształtów idą za nimi;
- **Krycie** (100, 75, 50, 25 %).

Każdy wybór to jeden krok cofania. Panel zostaje otwarty, więc możesz zmienić kilka rzeczy po kolei.

## Jasne i ciemne tło
Przycisk **Tło rysunku** (słońce z księżycem, słońce albo księżyc na pasku narzędzi) ma trzy możliwości:
- **Jak motyw** (domyślnie) — rysunek jest jasny w jasnym motywie Brulionu i ciemny w ciemnym, a zmienia się od razu, gdy przełączysz motyw (także wieczorem, gdy motyw idzie za systemem);
- **Jasne** — zawsze biała kartka (albo w wybranym kolorze tła);
- **Ciemne** — zawsze ciemna kartka, także w jasnym motywie.

Wybór zapisuje się w pliku rysunku, więc każde urządzenie pokaże go tak samo. Działa i w edytorze rysunku, i w podglądzie w notatce, i w widoku samego obrazu.

Na ciemnej kartce kolory są dopasowane tak samo jak w ciemnym trybie Excalidraw: atrament jest jasnoszary, niebieski, czerwony i fioletowy jaśniejsze, linie papieru ciemniejsze. Zakreślacz zostaje w swoim kolorze, tylko trochę mocniejszy, żeby był widoczny na ciemnym tle, a pismo pod nim dało się przeczytać. Same kolory w pliku się nie zmieniają: przełączenie z powrotem na jasne tło pokaże dokładnie to, co narysowałeś. Kółka kolorów na pasku pokazują kolory tak, jak wyglądają na bieżącej kartce.

**Własny kolor tła.** W tym samym menu **Kolor tła…** (na telefonie w **Więcej**) wybierzesz kolor kartki z siatki albo wpiszesz dowolny, np. `#fff9db`; biały przywraca zwykłą kartkę. Podgląd pokazuje atrament na tym kolorze, a gdy atrament byłby słabo widoczny, okno o tym uprzedza — narysowane linie nie zmieniają koloru, ale możesz wybrać inny kolor pisaka. Linie i kratka papieru dopasowują się do koloru kartki. Na ciemnej kartce (**Ciemne** albo **Jak motyw** w ciemnym motywie) kolor tła zmienia się tak samo jak pozostałe kolory, np. kremowy staje się ciemnobrązowy; żeby widzieć dokładnie wybrany kolor, ustaw **Jasne**. Kolor tła zapisuje się w rysunku i można go cofnąć; widzą go też Excalidraw i Obsidian.

W innych programach:
- rysunek z tłem **Ciemne** jest ciemny wszędzie;
- rysunek z tłem **Jak motyw** jest ciemny w Obsidianie z ciemnym motywem (na komputerze także wtedy, gdy system jest jasny), a w przeglądarkach i zwykle też na GitHubie, gdy używają ciemnego motywu — plik ma w środku opis ciemnych kolorów dla przeglądarek (`prefers-color-scheme`). Program, który go nie rozumie, pokaże jasny rysunek;
- **excalidraw.com** pokazuje rysunek w swoim motywie, niezależnie od tego wyboru.

## Notatki na slajdach i PDF-ach
Rysunek może leżeć na obrazie, slajdzie wyeksportowanym jako obraz albo stronie PDF — piszesz wtedy prosto po slajdach z wykładu. Taki obraz pod rysunkiem to **podkład**.

**Wstawianie z notatki.** Wpisz w bloku `/Rysunek na obrazie lub PDF` (albo krócej `/slajd`, `/pdf`, `/podkład`, `/wykład`) albo wybierz w menu bloku **Wstaw rysunek na obrazie lub PDF…** i wskaż plik:
- **obraz** (zdjęcie tablicy, slajd zapisany jako PNG albo JPEG) — powstaje jeden rysunek „slajd (rysunek).svg” i od razu otwiera się do rysowania; zaczyna z **jasnym** tłem, bo zdjęcie ma swoje kolory;
- **PDF** — otwiera się okno ze stronami pliku jako miniaturami. Stuknij strony, które chcesz (stuknięcie drugi raz je odznacza), wybierz **Wszystkie** albo **Żadna**, albo wpisz je w polu **Strony**, np. `1-3, 7` (miniatury i pole zmieniają się razem). Z klawiatury: `Tab` przechodzi do miniatur, strzałki między nimi, `Spacja` zaznacza stronę. **Wstaw rysunki (N)** tworzy po jednym rysunku na stronę — „wykład 3 s. 4.svg” — pierwszy w miejscu kursora, kolejne w osobnych blokach pod nim, po kolei. Naraz wstawisz najwyżej 60 stron. Przygotowanie stron trwa chwilę (pasek postępu „Strona 3 z 12…”); **Anuluj** przerywa. Jeden rysunek otwiera się od razu, kilka zostaje w notatce. Całe wstawienie cofniesz jednym `Ctrl+Z` (pliki rysunków zostają w załącznikach, jak przy `/Rysunek`).

**Rysuj na tej stronie** w przeglądarce PDF (zobacz [[Dokument i konspekt#PDF-y|PDF-y]]) robi rysunek na stronie, którą właśnie czytasz — „wykład 3 s. 4.svg” — i od razu go otwiera. Rysunek trafia do notatki, z której otwarto PDF, jako osobny blok zaraz pod blokiem z tym PDF-em (także gdy w międzyczasie przesunął się w notatce); PDF otwarty z drzewa albo szybkiego przełączania, bez notatki, dodaje rysunek do dzisiejszego dziennika (Brulion to powie). **Wstecz** z edytora rysunku wraca do przeglądarki na tę samą stronę, kolejne **Wstecz** — do notatki, w której rysunek już jest pod PDF-em. Gdy ta notatka ma już rysunek na tej stronie, przycisk najpierw proponuje go otworzyć (**Otwórz rysunek „…”**) albo zrobić **Nowy rysunek na tej stronie** — nie powstaje przypadkiem drugi plik.

Plik spoza notatnika Brulion kopiuje do folderu załączników, jak obrazy. Plik, który już jest w notatniku (np. PDF zsynchronizowany do folderu wykładu), zostaje na swoim miejscu — na Androidzie też, gdy wskażesz go w pamięci telefonu, na karcie pamięci albo w Pobranych — a ten sam plik wybrany drugi raz nie jest kopiowany ponownie. Jeśli z wybranego pliku nie powstanie żaden rysunek (anulujesz wybór stron, PDF jest chroniony hasłem albo uszkodzony, obrazu nie da się pokazać), skopiowany plik znika z załączników.

**Podkład w edytorze rysunku.** W menu **Papier** (na telefonie w **Więcej**):
- **Podkład z obrazu lub PDF…** — kładzie obraz albo stronę PDF pod istniejący rysunek (albo zmienia podkład, który już jest);
- **Inna strona PDF…** — inna strona tego samego PDF-a (gdy podkładem jest strona PDF); jeśli pliku nie ma już w notatniku (przeniesiony, usunięty), Brulion to powie — rysunek ma jednak w sobie obraz strony i wygląda dalej tak samo;
- **Poprzednia strona PDF** i **Następna strona PDF** (`PgUp`, `PgDown`) — przejście do rysunku na sąsiedniej stronie tego samego PDF-a, bez wracania do notatki. Brulion szuka go w notatce, w której jest bieżący rysunek: jeśli notatka ma już rysunek na tej stronie, otwiera go; jeśli nie — robi nowy („wykład 3 s. 5.svg”) i wstawia go do notatki w osobnym bloku zaraz za bieżącym rysunkiem (następna strona) albo tuż przed nim (poprzednia), więc rysunki stoją w notatce po kolei. Bieżący rysunek zapisuje się najpierw, a **Wstecz** wraca do notatki. Rysunek, którego nie ma w żadnej notatce, dodaje nowy do dzisiejszego dziennika;
- **Usuń podkład** — zostawia samo pismo.

Każdą zmianę podkładu cofniesz. Podkład jest zablokowany: gumka go nie ściera, lasso i `Ctrl+A` go nie zaznaczają, a **Na spód** kładzie elementy tuż nad nim.

**Ciemne tło.** Na ciemnej kartce podkład też ciemnieje — jak tryb nocny czytnika PDF: biała strona staje się ciemnoszara, czarny tekst jasny — więc jasne pismo dalej dobrze widać. Rysunek na stronie PDF zaczyna z tłem **Jak motyw** (ciemnieje razem z Brulionem), rysunek na obrazie z tłem **Jasne**; obie możesz zmienić przyciskiem **Tło rysunku**. Rysunek z tłem **Ciemne** ma ciemny podkład także w Obsidianie i w przeglądarce.

**Gdzie działa PDF.** Strony PDF wstawisz — z okna wyboru stron i przyciskiem **Rysuj na tej stronie** w przeglądarce PDF — w aplikacji na Androida, Windows i Linuksa (pakiet Nix i AppImage). W wersji w przeglądarce PDF-ów nie ma (przeglądarkowa wersja Qt nie umie ich rysować; PDF otwiera się tam w nowej karcie) — tam podkładem może być obraz, a rysunki na stronach PDF zrobione na innym urządzeniu wyglądają i edytują się normalnie, bo obraz strony jest w pliku rysunku.

**W pliku.** Obraz podkładu jest w samym rysunku (najwyżej 2400 pikseli na dłuższym boku i 4 MB), więc rysunek wygląda tak samo na każdym urządzeniu, w Obsidianie, w przeglądarce i w excalidraw.com — tam podkład jest zwykłym zablokowanym obrazem na spodzie. Obraz jest w pliku dwa razy (w scenie i w samym obrazie), więc rysunek ze slajdem waży zwykle od 0,5 do 1,7 MB.

## Kopiowanie i wklejanie (także z excalidraw.com)
Zaznaczenie skopiujesz `Ctrl+C` (albo **Kopiuj** w **Więcej** nad zaznaczeniem), wytniesz `Ctrl+X`, a wkleisz `Ctrl+V` albo **Wklej** w menu **Papier** (na telefonie w **Więcej**). Wklejone elementy pojawiają się pod kursorem myszy (albo na środku widoku), od razu zaznaczone — możesz je przesunąć; całe wklejenie cofniesz jednym `Ctrl+Z`.

Brulion kopiuje w tym samym formacie co Excalidraw, więc działa to w obie strony: elementy skopiowane w Brulionie wkleisz w **excalidraw.com** (i w innym rysunku Brulionu), a skopiowane w excalidraw.com — w Brulionie. Wkleisz też całą zawartość pliku `.excalidraw` skopiowaną jako tekst. Ramek i osadzonych stron internetowych z Excalidraw Brulion nie rysuje — przy wklejaniu je pomija i mówi, ile ich było. Zwykły tekst ze schowka wkleja się jako nowy tekst na rysunku.

## Rysik, palec i mysz
- **Rysik** rysuje. **Palce** wtedy nie rysują, tylko przesuwają kartkę (jeden palec) i powiększają ją (dwa palce) — możesz więc oprzeć dłoń na ekranie. Dopóki rysik dotyka ekranu, dłoń niczego nie zmienia: nie przesuwa kartki i nie przerywa linii.
- Na telefonie bez rysika **palec rysuje**, a kartkę przesuwasz i powiększasz dwoma palcami. Gdy Brulion pierwszy raz zobaczy rysik, przestawia się na rysowanie rysikiem. Sam wybierzesz to przełącznikiem **Rysuj palcem**.
- W wersji w przeglądarce rysik działa tak samo — z naciskiem i drugim końcem jako gumką — o ile przeglądarka przekazuje stronie nacisk rysika (robią to m.in. Chrome i Edge).
- **Myszką** rysujesz z wciśniętym lewym przyciskiem. Kartkę przesuwasz środkowym albo prawym przyciskiem, kółkiem lub ze wciśniętą spacją, a powiększasz z `Ctrl` i kółkiem albo gestem na touchpadzie.

## Zapisywanie i synchronizacja
Nie trzeba niczego zapisywać: Brulion zapisuje rysunek chwilę po każdej linii, a także gdy wychodzisz z rysunku albo z aplikacji. Notatka od razu pokazuje nowy wygląd.

Gdy ten sam rysunek zmieni się na innym urządzeniu (Syncthing, Nextcloud) w czasie, gdy go rysujesz, Brulion łączy obie wersje: zostaje to, co dorysowano tu i tam, przesunięte albo zmienione tutaj ma pierwszeństwo, a znika to, co zostało starte. Linia, którą właśnie rysujesz, też zostaje.

Jeśli na innym urządzeniu ktoś doda do rysunku w excalidraw.com ramkę albo osadzoną stronę internetową, Brulion pokaże tę wersję tak, jak jest, a to, co narysowano tutaj i czego jeszcze nie zapisał, zapisze obok w nowym pliku „… (to urządzenie).svg” (albo `.excalidraw`, `.excalidraw.md` — w formacie rysunku).

Gdy rysunek zmieni się na dwóch urządzeniach, zanim się zsynchronizowały, **Nextcloud scala go sam**: zostaje to, co dorysowano na obu, a znika to, co zostało starte. Rysunku nigdy nie skleja się wiersz po wierszu, więc nie da się go w ten sposób zepsuć.

Gdy **Syncthing** zostawi kopię konfliktową rysunku (`Rysunek.sync-conflict-….svg`) — albo Nextcloud nie umiał scalić rysunku sam, na przykład z ramką z excalidraw.com — w **Centrum synchronizacji → Konflikty** zobaczysz obie wersje i wynik scalenia, z liczbą elementów w każdej. **Scal rysunki** łączy linie i kształty z obu wersji: twoja wersja zostaje taka, jaka była, a dochodzi to, co jest tylko w kopii. Linie starte tylko na jednym urządzeniu wrócą — zetrzyj je jeszcze raz. Poprzednia wersja rysunku zostaje w historii wersji (także bardzo duża, z wklejonymi zdjęciami), a kopia trafia do kosza, więc wszystko da się cofnąć. Rysunek, którego Brulion nie scali (z ramką albo osadzoną stroną), zachowasz w jednej z wersji: **Zachowaj moją** albo **Zachowaj kopię**.

## Rysunki i inne programy
Plik rysunku to obraz SVG z ukrytą w środku sceną Excalidraw:
- **Obsidian**, **Logseq**, przeglądarki i menedżery plików pokazują go jako zwykły obraz;
- **excalidraw.com** otwiera go do edycji (przeciągnij plik na stronę) — kształty, strzałki i tekst z Brulionu są tam zwykłymi elementami Excalidraw;
- rysunek z Excalidraw wyeksportowany jako SVG z opcją **Embed scene** Brulion też otworzy i możesz go dalej edytować: pismo odręczne, kształty, strzałki, tekst i wklejone obrazy. Brulion rysuje je gładko — „ręczny” wygląd z Excalidraw zostaje w pliku, ale w Brulionie kreski są proste — a tekst pokazuje swoją czcionką, więc może wyjść odrobinę poza swoje pole. Strzałek łamanych Brulion nie układa na nowo, tylko przesuwa;
- rysunki (*canvas*) z **Trilium** import zapisuje jako takie same pliki SVG: obraz narysowany przez Trilium ze sceną Excalidraw w środku i obrazami wklejonymi do rysunku — otworzysz je w edytorze rysunków jak każdy inny rysunek (zobacz [[Import i eksport|Import i eksport]]);
- rysunek z **ramkami** albo **osadzonymi stronami internetowymi** Brulion tylko pokaże (zmienisz go w excalidraw.com), żeby niczego nie zgubić. W ciemnym motywie taki rysunek też ciemnieje, jak w ciemnym trybie Excalidraw, a wklejone do niego zdjęcia i emoji zachowują swoje kolory.

Pliki **`.excalidraw`** (excalidraw.com: „Save to disk”, wtyczki Excalidraw dla Logseq i Obsidiana w trybie zgodności) Brulion też otwiera i edytuje — zapisuje je dalej jako `.excalidraw`.

## Rysunki z Obsidiana (Excalidraw)
Rysunki wtyczki **Excalidraw dla Obsidiana** to pliki `*.excalidraw.md` (na przykład `Rysunek 2026-10-28 14.32.05.excalidraw.md`) — notatki, których nagłówek (frontmatter) ma klucz `excalidraw-plugin`. Brulion rozpoznaje je po tym kluczu, także gdy nazwa nie kończy się na `.excalidraw.md`.
- **W notatce** pokazują się jako rysunek: `![[Rysunek.excalidraw]]`, `![[Rysunek.excalidraw.md]]`, a także `![[Rysunek]]`, gdy ta strona jest rysunkiem. Kliknięcie otwiera edytor rysunków.
- **Z listy stron, linku albo wyszukiwania** taki plik otwiera się jako obraz rysunku z przyciskiem „Edytuj rysunek”. „Pokaż jako tekst” pokazuje jego Markdown (tylko do czytania) — notatek „z tyłu” rysunku nie edytuje się w Brulionie, tylko w Obsidianie.
- **Edycja:** pismo odręczne, kształty, strzałki, tekst i zaznaczanie działają tak samo jak w rysunkach Brulionu.
- **Zapis w tym samym formacie:** Brulion zmienia tylko blok `## Drawing` ze sceną (skompresowany — `compressed-json` — zostaje skompresowany, `json` zostaje JSON-em) i wpisy w „Text Elements” tekstów, które zmieniłeś albo usunąłeś (tekst tylko przesunięty, przekolorowany albo przeniesiony razem z kształtem zachowuje swoje linki). Frontmatter, notatki z tyłu rysunku, linki elementów („Element Links”), osadzone pliki („Embedded Files”) i znaczniki `%%` zostają bajt w bajt. Obsidian otworzy rysunek dalej, a to, co starłeś w Brulionie, zniknie też w Obsidianie, nawet jeśli ma ten rysunek otwarty.
- **Obrazy wklejone w Obsidianie** (sekcja „Embedded Files”, np. `Pasted Image ….png`) Brulion pokazuje — w edytorze i w podglądzie w notatce — szukając ich tak, jak Obsidian szuka linków w tym rysunku. Obraz zostaje w swoim pliku; w rysunku jest tylko odnośnik, jak w Obsidianie. Wzory LaTeX i adresy z tej sekcji, a także obraz, którego nie ma w notatniku, są szarymi prostokątami — można je przesuwać i usuwać.
- **Eksport:** w eksporcie do HTML (strona i dokument) i do Pandoca taki rysunek jest obrazem — wstawiony do notatki, wskazany linkiem `![[Rysunek.excalidraw]]` i na swojej własnej stronie — a nie tekstem wtyczki. Brulion bierze eksport wtyczki leżący obok (`Rysunek.excalidraw.svg` albo `.png`), a gdy go nie ma, sam robi z rysunku obraz SVG (z wklejonymi obrazami).
- **Ograniczenia:** link w tekście zmienionym w Brulionie staje się zwykłym tekstem. Rysunek z **ramkami** albo **osadzonymi stronami** Brulion tylko pokazuje (zmienisz go w Obsidianie); jego podglądem jest eksport wtyczki obok (`Rysunek.excalidraw.svg` albo `.png`), a gdy go nie ma — rysunek z przerywanymi ramkami w miejscu tych elementów. Rysunek w bardzo starym formacie wtyczki (bez bloku kodu) wystarczy raz otworzyć w Obsidianie.
- **Synchronizacja:** gdy taki rysunek zmieni się na dwóch urządzeniach naraz, Nextcloud scala go element po elemencie, jak rysunki Brulionu, i nigdy nie skleja go wiersz po wierszu. Notatki z tyłu rysunku i frontmatter zostają z tej wersji, w której je zmieniono; gdy zmieniono je na obu urządzeniach, Nextcloud zachowa obie wersje (kopię konfliktu).
- **Kopia konfliktowa z innymi notatkami:** gdy kopia takiego rysunku (od Syncthinga albo Nextclouda) ma inne notatki niż twój rysunek — frontmatter, notatki z tyłu, linki, osadzone pliki — centrum synchronizacji nigdy nie nazwie jej „taką samą” ani „starszą”. **Scal rysunki** połączy elementy obu wersji w twoim rysunku (z twoimi notatkami), a kopia zostanie na miejscu. Wtedy centrum pokaże „Różnią się tylko notatki wokół rysunku” i porównanie notatek obu wersji: przenieś w Obsidianie to, czego potrzebujesz, i dopiero potem **Usuń kopię**. Gdy rysunek jest otwarty w Brulionie, wersja z innego urządzenia łączy się z twoją kreska po kresce.
- Nowe rysunki Brulion tworzy zawsze jako SVG.

Zobacz też [[Format plików|Format plików]] i [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]].
