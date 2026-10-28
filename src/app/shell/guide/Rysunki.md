---
tags: [dokumentacja]
aliases: [Rysunek, Rysowanie, Notatki odręczne]
brulion: document
---
# Rysunki

W notatce możesz rysować i pisać odręcznie — rysikiem na tablecie, palcem na telefonie albo myszką na komputerze. Rysunek to zwykły obraz w notatniku, więc widać go także w Obsidianie, w przeglądarce i w menedżerze plików, a Syncthing synchronizuje go jak każdy inny plik.

## Nowy rysunek
Wpisz w bloku `/Rysunek` (albo `/szkic`, `/rysuj`) i naciśnij Enter. Na telefonie jest też przycisk **Rysunek** na pasku nad klawiaturą, a w menu bloku (prawy przycisk myszy albo przytrzymanie punktora) — **Wstaw rysunek**. Brulion zapisuje w folderze załączników pusty rysunek `Rysunek 2026-10-27 14.32.svg`, wstawia go do notatki jako `![[Rysunek 2026-10-27 14.32.svg]]` i od razu otwiera do rysowania.

Rysunek w notatce ma w rogu ikonę pióra. Kliknij go albo stuknij, żeby rysować dalej. W widoku samego obrazu (link do pliku) jest przycisk **Edytuj rysunek**.

## Narzędzia
- **Pióro** (klawisz P) — linia grubieje, gdy mocniej naciskasz rysik. Myszką i palcem linia jest cieńsza, gdy prowadzisz szybciej, jak prawdziwy atrament.
- **Zakreślacz** (H) — szeroka, półprzezroczysta linia, np. do zaznaczania fragmentów notatek z wykładu.
- **Gumka** (E) — usuwa każdą linię, której dotknie. Drugi koniec rysika (gumka na rysiku) albo wciśnięty przycisk na rysiku ściera zawsze, bez przełączania narzędzia.
- **Kolory i grubości** — sześć kolorów pióra (atrament, niebieski, czerwony, zielony, pomarańczowy, fioletowy), pięć kolorów zakreślacza i trzy grubości każdego. Na telefonie są pod kółkiem z bieżącym kolorem. Brulion pamięta ostatni wybór na tym urządzeniu.
- **Papier** — gładki, w linie albo w kratkę (na telefonie w menu **Więcej**).
- **Tło rysunku** — jasne albo ciemne, zobacz niżej [[#Jasne i ciemne tło|Jasne i ciemne tło]] (na telefonie w menu **Więcej**).
- **Cofnij** i **Ponów** (`Ctrl+Z`, `Ctrl+Shift+Z`) — cofają linie, starcie gumką, zmianę papieru i tła.
- **Pokaż cały rysunek** (`Ctrl+0`) i **Gotowe** — powrót do notatki.

## Jasne i ciemne tło
Przycisk **Tło rysunku** (słońce z księżycem, słońce albo księżyc na pasku narzędzi) ma trzy możliwości:
- **Jak motyw** (domyślnie) — rysunek jest jasny w jasnym motywie Brulionu i ciemny w ciemnym, a zmienia się od razu, gdy przełączysz motyw (także wieczorem, gdy motyw idzie za systemem);
- **Jasne** — zawsze biała kartka;
- **Ciemne** — zawsze ciemna kartka, także w jasnym motywie.

Wybór zapisuje się w pliku rysunku, więc każde urządzenie pokaże go tak samo. Działa i w edytorze rysunku, i w podglądzie w notatce, i w widoku samego obrazu.

Na ciemnej kartce kolory są dopasowane tak samo jak w ciemnym trybie Excalidraw: atrament jest jasnoszary, niebieski, czerwony i fioletowy jaśniejsze, linie papieru ciemniejsze. Zakreślacz zostaje w swoim kolorze, tylko trochę mocniejszy, żeby był widoczny na ciemnym tle, a pismo pod nim dało się przeczytać. Same kolory w pliku się nie zmieniają: przełączenie z powrotem na jasne tło pokaże dokładnie to, co narysowałeś. Kółka kolorów na pasku pokazują kolory tak, jak wyglądają na bieżącej kartce.

W innych programach:
- rysunek z tłem **Ciemne** jest ciemny wszędzie;
- rysunek z tłem **Jak motyw** jest ciemny w przeglądarkach, a często też w Obsidianie i na GitHubie, gdy używają ciemnego motywu — plik ma w środku opis ciemnych kolorów dla przeglądarek (`prefers-color-scheme`). Program, który go nie rozumie, pokaże jasny rysunek;
- **excalidraw.com** pokazuje rysunek w swoim motywie, niezależnie od tego wyboru.

## Rysik, palec i mysz
- **Rysik** rysuje. **Palce** wtedy nie rysują, tylko przesuwają kartkę (jeden palec) i powiększają ją (dwa palce) — możesz więc oprzeć dłoń na ekranie. Dopóki rysik dotyka ekranu, dłoń niczego nie zmienia: nie przesuwa kartki i nie przerywa linii.
- Na telefonie bez rysika **palec rysuje**, a kartkę przesuwasz i powiększasz dwoma palcami. Gdy Brulion pierwszy raz zobaczy rysik, przestawia się na rysowanie rysikiem. Sam wybierzesz to przełącznikiem **Rysuj palcem**.
- **Myszką** rysujesz z wciśniętym lewym przyciskiem. Kartkę przesuwasz środkowym albo prawym przyciskiem, kółkiem lub ze wciśniętą spacją, a powiększasz z `Ctrl` i kółkiem albo gestem na touchpadzie.

## Zapisywanie i synchronizacja
Nie trzeba niczego zapisywać: Brulion zapisuje rysunek chwilę po każdej linii, a także gdy wychodzisz z rysunku albo z aplikacji. Notatka od razu pokazuje nowy wygląd.

Gdy ten sam rysunek zmieni się na innym urządzeniu (Syncthing, Nextcloud) w czasie, gdy go rysujesz, Brulion łączy obie wersje: zostają linie dorysowane tu i tam, a znikają te, które zostały starte. Linia, którą właśnie rysujesz, też zostaje.

Jeśli na innym urządzeniu ktoś doda do rysunku kształty albo tekst w excalidraw.com, Brulion pokaże tę wersję tak, jak jest, a linie narysowane tutaj, których jeszcze nie zapisał, zapisze obok w nowym pliku „… (to urządzenie).svg”.

## Rysunki i inne programy
Plik rysunku to obraz SVG z ukrytą w środku sceną Excalidraw:
- **Obsidian**, **Logseq**, przeglądarki i menedżery plików pokazują go jako zwykły obraz;
- **excalidraw.com** otwiera go do edycji (przeciągnij plik na stronę) — możesz tam dodać kształty i tekst;
- rysunek z Excalidraw wyeksportowany jako SVG z opcją **Embed scene** Brulion też otworzy. Jeśli ma tylko pismo odręczne, możesz w nim rysować dalej; jeśli ma kształty, strzałki albo tekst, Brulion tylko go pokaże (zmienisz go w excalidraw.com), żeby niczego nie zgubić.

Pliki `*.excalidraw.md` wtyczki Excalidraw dla Obsidiana zostają zwykłymi notatkami. Zobacz też [[Format plików|Format plików]] i [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]].
