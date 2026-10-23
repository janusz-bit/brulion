---
tags: [dokumentacja]
brulion: document
---
# Dokument i konspekt

Każda notatka ma jeden z dwóch trybów. **Zawartość pliku jest taka sama** — zmienia się tylko sposób pokazywania i edycji.

| Tryb | Wygląd | Dobry do |
|---|---|---|
| **Dokument** (jak Obsidian) | ciągły tekst: nagłówki, akapity, zwykłe listy | opisów, artykułów, notatek ze spotkań |
| **Konspekt** (jak Logseq) | punkt z kropką dla każdego bloku, wcięcia, zwijanie, przybliżanie | dziennik, listy myśli, planowanie |

Ta strona jest dokumentem; [[Zadania|Zadania]] też. Dziennik domyślnie jest konspektem.

## Wybór i przełączanie trybu
- Przy tworzeniu strony (Ctrl+N) wybierasz tryb w oknie.
- Później przełączysz go ikoną obok tytułu, w menu strony („Przełącz na dokument” / „Przełącz na konspekt”) albo poleceniem „Przełącz tryb notatki” w palecie.
- Domyślne tryby nowych stron i dziennika ustawisz w *Ustawienia → Notatnik*.
- Wybór zapisuje się w samej notatce jako właściwość `brulion: document` albo `brulion: outline`, więc na każdym urządzeniu wygląda tak samo.
- Notatki bez tego klucza zgadują tryb po zawartości: strona z samych punktów otwiera się jako konspekt, strona z nagłówków i akapitów jako dokument.

## Bloki
Blokiem jest każdy punkt listy, a poza listami — każdy akapit, nagłówek, tabela, cytat, blok kodu. Kliknij blok, żeby go edytować; poza edycją widzisz go sformatowanego.

| Klawisze | Działanie |
|---|---|
| Enter | podziel blok w miejscu kursora (nowy blok) |
| Shift+Enter | nowa linia wewnątrz bloku |
| Tab / Shift+Tab | wcięcie / wysunięcie bloku razem z dziećmi |
| Alt+↑ / Alt+↓ | przeniesienie bloku z dziećmi |
| Ctrl+. | zwinięcie / rozwinięcie dzieci (konspekt) |
| Esc | koniec edycji i zaznaczenie bloku |
| Shift+↑ / Shift+↓ | zaznaczenie kilku bloków (z edytowanego: gdy zaznaczenie tekstu dojdzie do brzegu bloku) |
| Ctrl+A dwa razy | tekst bloku, potem wszystkie bloki |
| Ctrl+C / Ctrl+X / Ctrl+V | kopiuj / wytnij / wklej — zaznaczone bloki albo tekst w bloku |
| Ctrl+Enter | zmiana statusu zadania |

W konspekcie kliknij kropkę bloku, żeby się do niego **przybliżyć** — widzisz tylko ten blok i jego dzieci. W dokumencie Enter na pustym punkcie listy kończy listę, a Tab działa tylko na punktach listy.

**Bloki kodu:** wpisz `` ``` `` z nazwą języka (np. `` ```js ``) i naciśnij Enter. W bloku kodu widzisz sam kod — linie ogrodzenia oraz identyfikator bloku (`^id`) pod nim są ukryte. Linia `` ``` `` wpisana w kodzie zostaje kodem: Brulion sam wydłuża ogrodzenie. Gdy zaczniesz kod linią `` ``` `` w akapicie albo punkcie listy, Brulion dopisze linię zamykającą na końcu bloku, więc kod nie obejmie kolejnych bloków. Samą linię otwierającą (np. `` ```js `` bez Entera) Brulion zamyka, gdy przejdziesz do innego bloku — zostaje pusty blok kodu. Enter i wklejanie między tymi liniami działają jak w bloku kodu.

## Zaznaczanie i kopiowanie
W edytowanym bloku zaznaczasz tekst jak zwykle. Zaznaczenie, które wychodzi poza blok, obejmuje **całe bloki** razem z ich dziećmi (także zwiniętymi) — tak samo w dokumencie (akapity, nagłówki, punkty list) i w konspekcie.

- **Klawiatura:** Esc zaznacza edytowany blok, Shift+↑ / Shift+↓ powiększa i zmniejsza zaznaczenie, Ctrl+A dwa razy zaznacza wszystkie bloki.
- **Mysz:** Shift+kliknięcie zaznacza od edytowanego (albo zaznaczonego) bloku do klikniętego; przeciągnięcie z jednego bloku na drugi zaznacza wszystkie po drodze, a przy krawędzi okna strona sama się przewija. Przeciągnięcie myszą nie przewija już strony — przewija kółko i touchpad.
- **Telefon:** przytrzymaj palec na tekście bloku (nie edytowanego) — blok zostaje zaznaczony, klawiatura znika, a na dole pojawiają się narzędzia zaznaczenia. Nie odrywając palca, przesuń go, żeby zaznaczyć więcej. Potem stuknięcie w inny blok przesuwa koniec zaznaczenia, a dwa okrągłe uchwyty na początku i końcu zaznaczenia pozwalają je poprawić. „Gotowe” albo Wstecz kończy zaznaczanie. Przycisk „Zaznacz” jest też w pasku nad klawiaturą i w menu kropki (przytrzymanej). Wewnątrz edytowanego bloku przytrzymanie dalej zaznacza słowo z uchwytami systemu.
- **Co z zaznaczeniem:** Kopiuj, Wytnij, Usuń (Ctrl+Z przywraca), wcięcie i wysunięcie, przeniesienie w górę i w dół, status zadania — wszystko naraz. Na komputerze przy dwóch i więcej blokach u góry jest pasek z tymi przyciskami; prawy przycisk myszy na zaznaczonym bloku otwiera menu działające na całe zaznaczenie. Ctrl+V zastępuje zaznaczone bloki zawartością schowka.
- **Co trafia do schowka:** bloki jako Markdown — wklejone do Brulionu, Obsidiana albo zwykłego pola tekstowego zostają listą z wcięciami, a w poczcie czy edytorze dokumentów wklejają się sformatowane (lista, nagłówki, pola zadań). „Kopiuj jako tekst” (menu *Więcej* przy zaznaczeniu, menu bloku) daje zwykły tekst: punktory •, ☐ i ☑ zamiast pól zadań, bez znaczników. Za duże zaznaczenie (ponad ok. 800 KB) nie jest kopiowane — Brulion o tym mówi, zamiast uciąć tekst.
- **Wklejanie:** Markdown z kilkoma blokami staje się blokami z zachowanymi wcięciami; tekst skopiowany ze strony WWW albo z edytora dokumentów przychodzi jako Markdown (listy, nagłówki, linki, pogrubienia). Wklejone identyfikatory bloków (`^abc123`), które już są gdzie indziej w notatniku, są pomijane, żeby `((odwołania))` nie wskazywały dwóch bloków — po wycięciu i wklejeniu (przeniesieniu) zostają.

**Zaznacz tekst** (menu *Więcej* ⋮, menu strony, Ctrl+K → „Zaznacz tekst”) pokazuje całą stronę jako jeden tekst tylko do odczytu, w którym zaznaczysz dowolny fragment — także kawałki kilku akapitów. Na komputerze przeciągnij myszą (Shift+strzałki, Ctrl+A); na telefonie przytrzymaj słowo i przeciągnij uchwyty. „Kopiuj” kopiuje zaznaczenie (bez zaznaczenia: całą stronę) jako zwykły tekst; „Zamknij”, Esc albo Wstecz wraca do edycji.

## Menu `/`
Wpisz `/` na początku bloku, a pojawi się lista poleceń: zadanie, przypomnienie, alarm, termin, powtarzanie, tabela, obliczenia, zapytanie, nagłówek, cytat, ramka, kod, szablon, obraz, osadzenie i odwołanie do bloku. Polskie znaki w filtrze są opcjonalne.

## Podpowiedzi podczas pisania
- `[[` — strony (z możliwością utworzenia nowej),
- `((` — bloki z całego notatnika,
- `#` — tagi,
- po `📅`, `⏳`, `⏰` albo `@` — daty słowami: „jutro o 9”, „w piątek 18:00”.

## Ramki, wyróżnienia, komentarze
> [!note] Ramka
> Rodzaje: `note`, `tip`, `warning`, `danger`, `info`, `todo`, `quote`. Wpisujesz `> [!tip] Tytuł`, a w kolejnych liniach `> treść`.

==Zakreślenie== piszesz między podwójnymi znakami równości, ~~przekreślenie~~ w `~~tyldach~~`, a komentarz widoczny tylko w edycji w `%%procentach%%`.

## Tryb źródła
**Ctrl+E** pokazuje cały plik jako surowy Markdown z podświetleniem składni. Przydaje się do hurtowych zmian; kolejne naciśnięcie wraca do bloków. Na telefonie tekst przesuwasz palcem, dotknięcie stawia kursor, a przytrzymanie zaznacza słowo. (Do kopiowania fragmentów kilku bloków nie musisz już przechodzić do źródła: zobacz [[#Zaznaczanie i kopiowanie|Zaznaczanie i kopiowanie]].)

## Obrazy i pliki
Wklej obraz (Ctrl+V), upuść pliki na stronę albo użyj `/Obraz…`. Pliki trafiają do folderu załączników (`assets/`) i są osadzane jako `![[plik.png]]`.

Kliknięcie osadzonego obrazu pokazuje samo zdjęcie dopasowane do widoku (mały obraz zostaje w naturalnej wielkości, bez rozmywającego powiększania). Gdy zdjęcie jest większe od widoku, kliknięcie na nim albo przycisk „Pokaż naturalny rozmiar” (na telefonie i w wąskim widoku ikona lupy) pokazuje je w naturalnej wielkości z możliwością przeciągania. Pod zdjęciem są notatki, które je osadzają albo do niego linkują (na szerokim ekranie także w panelu bocznym, w zakładce Linki).

„Pokaż jako tekst” w pasku albo w menu Więcej (albo Ctrl+E) pokazuje plik jako tekst — tylko do odczytu, przydatne dla plików SVG (treść binarnego obrazu to nieczytelne znaki; z dużego pliku widać pierwsze 200 KB; zmiana pliku, np. z synchronizacji, od razu się tu pokazuje); „Pokaż obraz” albo ponownie Ctrl+E wraca do zdjęcia. Jeśli urządzenie nie ma dekodera danego formatu (np. WebP albo AVIF na telefonie lub w przeglądarce), Brulion pisze o tym zamiast pokazywać błąd — także w obrazie osadzonym w notatce.

Zobacz też: [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]], [[Skróty klawiszowe|Skróty klawiszowe]].
