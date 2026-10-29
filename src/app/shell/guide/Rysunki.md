---
tags: [dokumentacja]
aliases: [Rysunek, Rysowanie, Notatki odręczne, Schematy, Kształty]
brulion: document
---
# Rysunki

W notatce możesz rysować i pisać odręcznie — rysikiem na tablecie, palcem na telefonie albo myszką na komputerze — a także rysować schematy: prostokąty, elipsy, romby, linie, strzałki i tekst. Rysunek to zwykły obraz w notatniku, więc widać go także w Obsidianie, w przeglądarce i w menedżerze plików, a Syncthing synchronizuje go jak każdy inny plik.

## Nowy rysunek
Wpisz w bloku `/Rysunek` (albo `/szkic`, `/rysuj`) i naciśnij Enter. Na telefonie jest też przycisk **Rysunek** na pasku nad klawiaturą, a w menu bloku (prawy przycisk myszy albo przytrzymanie punktora) — **Wstaw rysunek**. Brulion zapisuje w folderze załączników pusty rysunek `Rysunek 2026-10-27 14.32.svg`, wstawia go do notatki jako `![[Rysunek 2026-10-27 14.32.svg]]` i od razu otwiera do rysowania.

Rysunek w notatce ma w rogu ikonę pióra. Kliknij go albo stuknij, żeby rysować dalej. W widoku samego obrazu (link do pliku) jest przycisk **Edytuj rysunek**.

## Narzędzia
- **Pióro** (klawisz P) — linia grubieje, gdy mocniej naciskasz rysik. Myszką i palcem linia jest cieńsza, gdy prowadzisz szybciej, jak prawdziwy atrament.
- **Zakreślacz** (H) — szeroka, półprzezroczysta linia, np. do zaznaczania fragmentów notatek z wykładu.
- **Gumka** (E) — usuwa każdą linię, kształt albo tekst, którego dotknie (kształt razem z napisem w środku). Drugi koniec rysika (gumka na rysiku) albo wciśnięty przycisk na rysiku ściera zawsze, bez przełączania narzędzia.
- **Zaznacz** (V, ikona lassa) — zobacz niżej [[#Zaznaczanie lassem|Zaznaczanie lassem]].
- **Kształty** — prostokąt (R), elipsa (O), romb (D), linia (L) i strzałka (A), zobacz niżej [[#Kształty i strzałki|Kształty i strzałki]].
- **Tekst** (T) — zobacz niżej [[#Tekst|Tekst]].
- **Kolory i grubości** — sześć kolorów pióra (atrament, niebieski, czerwony, zielony, pomarańczowy, fioletowy), pięć kolorów zakreślacza i trzy grubości każdego; dla tekstu cztery rozmiary: mały, średni, duży i bardzo duży. Na telefonie są pod kółkiem z bieżącym kolorem. Brulion pamięta ostatni wybór na tym urządzeniu.
- **Papier** — gładki, w linie albo w kratkę (na telefonie w menu **Więcej**).
- **Tło rysunku** — jasne albo ciemne, zobacz niżej [[#Jasne i ciemne tło|Jasne i ciemne tło]] (na telefonie w menu **Więcej**).
- **Cofnij** i **Ponów** (`Ctrl+Z`, `Ctrl+Shift+Z`) — cofają linie, kształty, tekst, starcie gumką, każdą zmianę zaznaczenia (przesunięcie, kolor, kopię, usunięcie) oraz zmianę papieru i tła.
- **Pokaż cały rysunek** (`Ctrl+0`) i **Gotowe** — powrót do notatki.

Na telefonie pasek narzędzi jest na dole ekranu. Gdy telefon trzymasz pionowo, pasek ma dwa wiersze: u góry narzędzia i kółko koloru, niżej **Cofnij**, **Ponów**, **Więcej** i **Gotowe** — wszystkie przyciski są na ekranie.

## Kształty i strzałki
Przycisk kształtu na pasku pokazuje ostatnio używany kształt; kliknij go jeszcze raz, gdy kształt jest wybrany, a rozwinie się lista: **Prostokąt** (R), **Elipsa** (O), **Romb** (D), **Linia** (L) i **Strzałka** (A).
- Przeciągnij po kartce — kształt rośnie razem z ruchem rysika, palca albo myszy. Z wciśniętym `Shift` prostokąt staje się kwadratem, elipsa kołem, a linia i strzałka obracają się co 15°.
- Kształt ma kolor pióra i dwa razy większą grubość (cienka, średnia, gruba). Prostokąty mają zaokrąglone rogi, jak w Excalidraw.
- Narzędzie zostaje wybrane, więc możesz narysować kilka kształtów po kolei. Pióro wybierzesz klawiszem P.

## Tekst
- Wybierz **Tekst** (T) i stuknij w kartkę — pojawia się pole do pisania. `Enter` zaczyna nowy wiersz.
- Kliknięcie obok pola, `Esc`, `Ctrl+Enter` albo przycisk ✓ (na ekranie dotykowym) kończy pisanie. Pusty tekst znika.
- Stuknij w istniejący tekst, żeby go poprawić. Stuknij w środek prostokąta, elipsy albo rombu, a napiszesz w nim etykietę: wyśrodkowaną i zawiniętą tak jak w Excalidraw — w elipsie i rombie węższą, żeby zmieściła się w środku. Gdy tekst się nie mieści, kształt rośnie. Etykieta przesuwa się i usuwa razem z kształtem.
- Pole zamknięte bez zmian niczego w rysunku nie zmienia — tekst z Excalidraw zostaje dokładnie taki, jaki był.
- Narzędziem **Zaznacz** to samo robi podwójne kliknięcie (albo dwa stuknięcia).
- Na telefonie kartka przesuwa się tak, żeby pisany tekst był nad klawiaturą.

## Zaznaczanie lassem
Wybierz **Zaznacz** (V) i obrysuj rysikiem, palcem albo myszką to, co chcesz zaznaczyć — zaznaczy się wszystko, co jest przynajmniej w połowie w środku. Stuknięcie zaznacza jeden element, stuknięcie w puste miejsce zdejmuje zaznaczenie, a `Shift` dodaje do zaznaczenia (myszką).
- **Przeciągnij** wewnątrz ramki zaznaczenia, żeby je przesunąć. Strzałki przyczepione do przesuwanych kształtów idą za nimi.
- Nad zaznaczeniem pojawia się pasek: **kolory** (zmiana koloru), **Duplikuj** (`Ctrl+D`), **Na wierzch** (`Ctrl+Shift+]`), **Na spód** (`Ctrl+Shift+[`) i **Usuń** (`Delete`).
- Klawisze: `Ctrl+A` zaznacza wszystko, strzałki przesuwają zaznaczenie (z `Shift` dziesięć razy dalej), `Ctrl+]` i `Ctrl+[` przesuwają je o jedno piętro w górę i w dół, `Esc` zdejmuje zaznaczenie.
- Grupy z Excalidraw zaznaczają się w całości. Elementów zablokowanych w Excalidraw — ani napisów w zablokowanych kształtach — nie da się zaznaczyć, zetrzeć ani zmienić.
- Gdy **Rysuj palcem** jest wyłączone, palec tylko przesuwa kartkę — dłoń oparta na zaznaczeniu go nie przesunie.

## Jasne i ciemne tło
Przycisk **Tło rysunku** (słońce z księżycem, słońce albo księżyc na pasku narzędzi) ma trzy możliwości:
- **Jak motyw** (domyślnie) — rysunek jest jasny w jasnym motywie Brulionu i ciemny w ciemnym, a zmienia się od razu, gdy przełączysz motyw (także wieczorem, gdy motyw idzie za systemem);
- **Jasne** — zawsze biała kartka;
- **Ciemne** — zawsze ciemna kartka, także w jasnym motywie.

Wybór zapisuje się w pliku rysunku, więc każde urządzenie pokaże go tak samo. Działa i w edytorze rysunku, i w podglądzie w notatce, i w widoku samego obrazu.

Na ciemnej kartce kolory są dopasowane tak samo jak w ciemnym trybie Excalidraw: atrament jest jasnoszary, niebieski, czerwony i fioletowy jaśniejsze, linie papieru ciemniejsze. Zakreślacz zostaje w swoim kolorze, tylko trochę mocniejszy, żeby był widoczny na ciemnym tle, a pismo pod nim dało się przeczytać. Same kolory w pliku się nie zmieniają: przełączenie z powrotem na jasne tło pokaże dokładnie to, co narysowałeś. Kółka kolorów na pasku pokazują kolory tak, jak wyglądają na bieżącej kartce.

W innych programach:
- rysunek z tłem **Ciemne** jest ciemny wszędzie;
- rysunek z tłem **Jak motyw** jest ciemny w Obsidianie z ciemnym motywem (na komputerze także wtedy, gdy system jest jasny), a w przeglądarkach i zwykle też na GitHubie, gdy używają ciemnego motywu — plik ma w środku opis ciemnych kolorów dla przeglądarek (`prefers-color-scheme`). Program, który go nie rozumie, pokaże jasny rysunek;
- **excalidraw.com** pokazuje rysunek w swoim motywie, niezależnie od tego wyboru.

## Rysik, palec i mysz
- **Rysik** rysuje. **Palce** wtedy nie rysują, tylko przesuwają kartkę (jeden palec) i powiększają ją (dwa palce) — możesz więc oprzeć dłoń na ekranie. Dopóki rysik dotyka ekranu, dłoń niczego nie zmienia: nie przesuwa kartki i nie przerywa linii.
- Na telefonie bez rysika **palec rysuje**, a kartkę przesuwasz i powiększasz dwoma palcami. Gdy Brulion pierwszy raz zobaczy rysik, przestawia się na rysowanie rysikiem. Sam wybierzesz to przełącznikiem **Rysuj palcem**.
- **Myszką** rysujesz z wciśniętym lewym przyciskiem. Kartkę przesuwasz środkowym albo prawym przyciskiem, kółkiem lub ze wciśniętą spacją, a powiększasz z `Ctrl` i kółkiem albo gestem na touchpadzie.

## Zapisywanie i synchronizacja
Nie trzeba niczego zapisywać: Brulion zapisuje rysunek chwilę po każdej linii, a także gdy wychodzisz z rysunku albo z aplikacji. Notatka od razu pokazuje nowy wygląd.

Gdy ten sam rysunek zmieni się na innym urządzeniu (Syncthing, Nextcloud) w czasie, gdy go rysujesz, Brulion łączy obie wersje: zostaje to, co dorysowano tu i tam, przesunięte albo zmienione tutaj ma pierwszeństwo, a znika to, co zostało starte. Linia, którą właśnie rysujesz, też zostaje.

Jeśli na innym urządzeniu ktoś doda do rysunku w excalidraw.com ramkę albo osadzoną stronę internetową, Brulion pokaże tę wersję tak, jak jest, a to, co narysowano tutaj i czego jeszcze nie zapisał, zapisze obok w nowym pliku „… (to urządzenie).svg” (albo `.excalidraw`, `.excalidraw.md` — w formacie rysunku).

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
- **Ograniczenia:** obrazy wklejone w Obsidianie (z sekcji „Embedded Files”) Brulion pokazuje jako szare prostokąty — można je przesuwać i usuwać. Link w tekście zmienionym w Brulionie staje się zwykłym tekstem. Rysunek z **ramkami** albo **osadzonymi stronami** Brulion tylko pokazuje (zmienisz go w Obsidianie); jego podglądem jest eksport wtyczki obok (`Rysunek.excalidraw.svg` albo `.png`), a gdy go nie ma — rysunek z przerywanymi ramkami w miejscu tych elementów. Rysunek w bardzo starym formacie wtyczki (bez bloku kodu) wystarczy raz otworzyć w Obsidianie.
- **Synchronizacja:** gdy taki rysunek zmieni się na dwóch urządzeniach naraz, Nextcloud zachowa obie wersje (kopię konfliktu), nigdy nie skleja ich wiersz po wierszu. Gdy rysunek jest otwarty w Brulionie, wersja z innego urządzenia łączy się z twoją kreska po kresce, jak w rysunkach Brulionu.
- Nowe rysunki Brulion tworzy zawsze jako SVG.

Zobacz też [[Format plików|Format plików]] i [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]].
