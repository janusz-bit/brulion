---
tags: [dokumentacja]
brulion: document
---
# Pierwsze kroki

## Notatniki i foldery
Brulion przechowuje notatki jako zwykłe pliki `.md` w wybranym folderze — ten folder to Twój **notatnik**. Nie ma konta ani bazy danych; kopię zapasową zrobisz, kopiując folder, a ten sam folder otworzysz w Obsidianie czy Logsequ.

Przy pierwszym uruchomieniu wybierasz folder, tworzysz nowy notatnik albo otwierasz „Przykładowy brulion” z notatkami do wypróbowania. Między notatnikami przełączasz się przyciskiem „Przełącz notatnik” — pierwszą literą nazwy notatnika na samej górze paska ikon po lewej. Jego menu ma listę ostatnich notatników oraz pozycje „Dokumentacja Brulionu” i „Otwórz inny notatnik…”. Ctrl+Shift+B przenosi do dokumentacji i z powrotem.

## Pierwszy wpis
1. Otwórz **Dziennik** (Ctrl+J) — to strona na dziś.
2. Zacznij pisać. Każda linia, która zaczyna się od myślnika, to osobny blok.
3. Zmiany zapisują się same; nie ma przycisku „Zapisz”.

Strona dnia powstaje dopiero przy pierwszej zmianie — samo zaglądanie niczego nie tworzy.

## Pisanie na telefonie
Gdy piszesz, pasek narzędzi leży tuż nad klawiaturą — w dzienniku i na każdej stronie, także gdy przewijasz. Ma wcięcia, przenoszenie bloków, zadanie, link `[[`, tag `#`, polecenia `/`, termin, przypomnienie, obraz, cofanie i ponawianie oraz **Więcej** (⋯) — menu bloku, np. z kopiowaniem odnośnika do niego; gdy przycisków jest więcej, niż mieści się na ekranie, przesuń pasek palcem w bok. Dolna nawigacja chowa się, gdy klawiatura jest otwarta; pasek boczny otwiera przycisk menu (trzy kreski) w lewym górnym rogu, który jest tam zawsze.

- **Ukryj klawiaturę** (ostatni przycisk paska) kończy pisanie: chowa klawiaturę i pasek, a dolna nawigacja wraca.
- Gdy klawiaturę schowasz przyciskiem Wstecz albo klawiszem na samej klawiaturze, wraca dolna nawigacja (Dziennik, Strony, Szukaj…), a pasek zostaje tuż nad nią z wszystkimi narzędziami — dotknij tekstu, a klawiatura wróci. Przycisk nawigacji kończy pisanie i otwiera swój widok, a **+** na górnym pasku kończy pisanie i otwiera szybką notatkę.
- W komórce tabeli pasek ma Cofnij, Ponów, **Następna komórka** i Ukryj klawiaturę; w źródle Markdown (Więcej → Tryb → Źródło) — `[[`, `#`, `/`, Cofnij, Ponów i Ukryj klawiaturę.
- Okna wyświetlane od dołu (szybka notatka, przypomnienie, zmiana nazwy), podpowiedzi przy pisaniu i pola formularzy (np. w ustawieniach) też zawsze leżą nad klawiaturą.

## Nowa strona
- **Ctrl+N**, przycisk z ołówkiem obok nazwy notatnika albo „+” przy sekcji **Strony** w pasku bocznym (→ „Nowa strona”). Gdy jesteś na stronie w folderze, nowa strona trafi do tego samego folderu — widać to w oknie jako „W folderze: Projekty”; krzyżyk przy nim albo „Zmień…” wybiera inne miejsce. Nazwa może też zawierać `/`, wtedy strona trafi do folderu: `Projekty/Dom`.
- W oknie wybierasz tryb: dokument albo konspekt (zobacz [[Dokument i konspekt|Dokument i konspekt]]).
- Skrót: wpisz w dowolnej notatce `[[Nazwa nowej strony]]` i kliknij link — strona powstanie.

## Szybka notatka
**Ctrl+Shift+N** (na telefonie **+** na górnym pasku) otwiera jedno pole tekstowe. Wpisany tekst trafia na koniec dzisiejszego dziennika (z dokumentacji Brulion najpierw wraca do Twojego notatnika). Daty w nim stają się terminami i przypomnieniami: „zadzwonić do Ani jutro o 9”.

## Szukanie i przechodzenie
- **Ctrl+O** albo pole **Szukaj lub wpisz polecenie…** na górnym pasku — szybkie przejście do strony po kilku literach nazwy. Ostatnia pozycja listy szuka wpisanych słów w treści wszystkich notatek, a znak `>` na początku przełącza na polecenia (jak paleta poleceń).
- **Ctrl+Shift+F** — wyszukiwanie w treści wszystkich notatek; polskie znaki można pomijać („zolw” znajdzie „żółw”). Gdy otwierasz wyszukiwanie (także przez **Szukaj** w dolnej nawigacji telefonu), pole od razu czeka na tekst: wysuwa się klawiatura, a poprzednie zapytanie jest zaznaczone, więc pisanie je zastępuje.
- **Ctrl+F** — znajdź w otwartej notatce (na telefonie: menu strony → **Znajdź i zamień…**), **Ctrl+H** — od razu z zamianą. Polskie znaki i wielkość liter można pomijać; przyciski „Aa”, „ąa” i „Słowo” zawężają wyniki. Szukanie obejmuje tekst Markdown notatki, także adresy linków. „Zamień wszystko” cofa jedno **Ctrl+Z**. W strumieniu dziennika szukasz w dniu, w którym piszesz.
- **Zamień w notatniku** (menu **Więcej** → **Notatnik**, paleta poleceń, „Zamień…” w wyszukiwaniu) — zamiana we wszystkich notatkach. Najpierw widać każde trafienie „przed → po”; odznacz, czego nie chcesz zmieniać. Trafienie wewnątrz linku jest oznaczone, bo link zacznie prowadzić gdzie indziej: żeby zmienić nazwę strony razem z linkami, użyj zmiany nazwy. Po zamianie przycisk **Cofnij** przywraca notatki, a starą treść każdej zachowuje też **Historia wersji**. Kosz, rysunki i inne pliki pomijamy.
- **Ctrl+K** — paleta poleceń.
- **Alt+←** / **Alt+→** — wstecz i dalej, jak w przeglądarce.

## Pasek boczny
Przy lewej krawędzi okna stoi wąski pasek ikon: na górze notatnik (jego pierwsza litera — przełączanie notatników), pod nią Dziennik, Kalendarz, Zadania, Nawyki, Wszystkie strony, Tagi, Graf i Szukaj (nazwę i skrót ikony pokazuje dymek po najechaniu myszą), a na dole stan synchronizacji (kliknięcie otwiera centrum synchronizacji) i Ustawienia. Obok jest panel stron: nazwa notatnika z przyciskiem nowej strony i sekcje **Ulubione**, **Ostatnie** i **Strony** (drzewo folderów). **Ulubione** pojawiają się, gdy oznaczysz gwiazdką pierwszą stronę (gwiazdka w nagłówku strony, menu **Więcej** albo menu strony w drzewie), a **Ostatnie** — gdy otworzysz kolejną stronę (otwartej strony tam nie ma, pokazuje ją drzewo). Kolejność sekcji zmienisz w *Ustawienia → Wygląd → Układ*, np. drzewo na górze, a ulubione na dole. Przycisk po lewej stronie górnego paska chowa panel stron — pasek ikon zostaje. Na telefonie pasek boczny otwiera przycisk menu (trzy kreski) w lewym górnym rogu albo przesunięcie palca od lewej krawędzi ekranu; dolna nawigacja ma najczęściej używane widoki: Dziennik, Strony (wszystkie strony i foldery), Szukaj, Zadania i Kalendarz. Na Androidzie górny pasek nie ma strzałki wstecz: do poprzedniego widoku wraca systemowy przycisk lub gest Wstecz.

## Foldery
Foldery porządkują strony jak katalogi na dysku: strona `Projekty/Dom` to plik `Dom.md` w folderze `Projekty`.
- **Nowy folder**: „+” przy sekcji **Strony** → „Nowy folder” (w głównym folderze notatnika) albo menu folderu → „Nowy folder…” (w nim). Nazwy z niedozwolonymi znakami są poprawiane, a ukryte (z kropką na początku, np. `.prywatne`) odrzucane.
- **Menu** strony lub folderu w drzewie otwiera prawy przycisk myszy albo przytrzymanie palcem; na telefonie także przycisk „⋯” przy każdym wierszu. Folder ma tam m.in. „Pokaż strony folderu”, „Szukaj w folderze” i „Eksportuj folder…”, strona — „Kopiuj link”.
- **Przenoszenie**: „Przenieś do…” (w menu drzewa, w menu **Więcej** strony, na liście **Wszystkie strony** i w palecie poleceń) pokazuje foldery z wyszukiwaniem, a ostatni wiersz „Nowy folder „…”” od razu zakłada folder o wpisanej nazwie. Przed przeniesieniem widać, ile linków się zmieni. Na komputerze stronę lub folder możesz też przeciągnąć myszą na inny folder (zamknięty otworzy się po chwili) albo na nagłówek **Strony** — do głównego folderu; Esc przerywa przeciąganie. Ukośnik w oknie zmiany nazwy (F2) też przenosi: „Archiwum/Dom” (w tytule edytowanym w nagłówku strony ukośnik jest zwykłym znakiem tytułu).
- Linki i tagi zawsze prowadzą dalej do tej samej strony (zobacz [[Linki i odnośniki zwrotne#Zmiana nazwy i przenoszenie|Zmiana nazwy i przenoszenie]]). Przeniesienie folderu dziennika, szablonów albo załączników poprawia też ustawienia notatnika.
- Dzień dziennika to też zwykły plik: w drzewie, na liście **Wszystkie strony**, klawiszem F2 i w menu **Więcej** dnia otwartego jako strona („Otwórz jako stronę”) zmienisz mu nazwę albo go przeniesiesz — np. wpis zapisany pod złą datą. Widok **Dziennik** pokazuje dni po datach i tych poleceń nie ma.
- Drzewo samo pokazuje i podświetla otwartą stronę (wyłączysz to w menu „+” → „Pokazuj otwartą stronę”), a „Zwiń wszystkie” zamyka wszystkie foldery. Folder dziennika jest na górze, dni od najnowszego.
- **Wszystkie strony** w folderze („Pokaż strony folderu” albo okruszki nad stroną) pokazują jego podfoldery nad stronami — tak przeglądasz notatnik folder po folderze także na telefonie. „Z podfolderami” decyduje, czy lista obejmuje też strony podfolderów.

Widoki bez własnego przycisku — centrum synchronizacji, import i eksport, kosz, sprzątanie — znajdziesz w menu **Więcej** (⋯, na telefonie ⋮) → **Notatnik** oraz w palecie poleceń; **Ustawienia** są też na dole paska ikon. Menu **Więcej** ma u góry akcje strony jako ikony (Ulubione, Nazwa, Przenieś, Historia), przełącznik **Tryb** (Dokument, Konspekt, Źródło) i — na komputerze — **Otwórz w** (Nowa karta, Okno, Panel boczny, Podziel). Rejestr zdarzeń jest w *Ustawienia → Aplikacja* i w palecie.

## Wygląd
W *Ustawienia → Wygląd* dopasujesz Brulion do siebie — osobno na każdym urządzeniu:
- **Motyw** jasny, ciemny albo zgodny z systemem; **kolor akcentu** (koralowy — domyślny —, czerwony, niebieski, zielony, morski, fioletowy, grafitowy albo **Własny…** — dowolny kolor z siatki lub wpisany jako `#3b7dd8`). Akcent wyróżnia dzisiejszy dzień, bieżącą kartę i zaznaczenia; tam, gdzie wybrany kolor byłby nieczytelny, Brulion sam bierze jego jaśniejszy albo ciemniejszy odcień. Ostrzeżenia i usuwanie zostają czerwone, przypomnienia i alarmy bursztynowe.
- **Jasne tło**: papier albo białe; **ciemne tło**: czarne (domyślne; oszczędza baterię na ekranach OLED) albo granatowe.
- **Rozmiar tekstu**, **czcionka interfejsu** (wbudowana Inter albo dowolna zainstalowana) i **gęstość** — zwarta mieści więcej na ekranie komputera (na telefonie przyciski zostają duże).
- **Układ**: które pozycje i sekcje pokazuje pasek boczny, w jakiej kolejności są Ulubione, Ostatnie i Strony, jakie pięć widoków ma dolna nawigacja na telefonie i czy górny pasek ma przycisk szybkiej notatki **+**. Ukryte widoki zostają w palecie poleceń i pod skrótami.

Czcionkę notatek (bezszeryfowa — Inter, domyślna —, szeryfowa — Literata — albo **Inna…**), czcionkę kodu (JetBrains Mono), rozmiar tekstu i **odstęp między wierszami** ustawisz w *Ustawienia → Edytor*. „Przywróć domyślny wygląd” wraca do kolorów i czcionek z początku. Brulion ma te trzy czcionki w sobie, więc notatki wyglądają tak samo na każdym urządzeniu. W przeglądarce nie ma wyboru czcionek — działają tylko wbudowane.

Na komputerze wzdłuż dołu okna biegnie cienki **pasek stanu**: po lewej stan synchronizacji (zielone „zsynchronizowano”; kliknięcie otwiera centrum synchronizacji), po prawej edytowany blok („Blok 3 z 12”), liczba słów i klawisz palety poleceń.

## Nowa wersja
Na Androidzie i Windows Brulion raz dziennie pyta GitHuba o najnowsze wydanie. Gdy jest nowsze, pod górnym paskiem pojawia się komunikat, a **Zobacz na GitHubie** otwiera stronę wydania, skąd je pobierzesz. Krzyżyk ukrywa komunikat do następnej wersji. GitHub widzi adres urządzenia, nic o Twoich notatkach nie jest wysyłane. Wyłączysz to w **Ustawienia → Aplikacja → Sprawdzaj nowe wersje**.

## Co dalej
- [[Dokument i konspekt|Dokument i konspekt]]
- [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]]
- [[Spis treści|Spis treści]]
