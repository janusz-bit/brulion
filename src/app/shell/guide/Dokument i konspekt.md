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
- Później przełączysz go na komputerze ikoną obok tytułu, a wszędzie w menu strony **⋯** („Przełącz na dokument” / „Przełącz na konspekt”) albo poleceniem „Przełącz tryb notatki” w palecie; na telefonie ikony obok tytułu nie ma — tryb zmienisz w menu strony.
- Domyślne tryby nowych stron i dziennika ustawisz w *Ustawienia → Notatnik*.
- Wybór zapisuje się w samej notatce jako właściwość `brulion: document` albo `brulion: outline`, więc na każdym urządzeniu wygląda tak samo.
- Notatki bez tego klucza zgadują tryb po zawartości: strona z samych punktów otwiera się jako konspekt, strona z nagłówków i akapitów jako dokument.

## Bloki
Blokiem jest każdy punkt listy, a poza listami — każdy akapit, nagłówek, tabela, cytat, blok kodu. Kliknij blok, żeby go edytować; poza edycją widzisz go sformatowanego.

| Klawisze | Działanie |
|---|---|
| Enter | podziel blok w miejscu kursora (nowy blok); na początku tekstu: pusty blok nad nim (przy zadaniu puste zadanie — tekst zostaje ze swoim statusem i podpunktami) |
| Shift+Enter | nowa linia wewnątrz bloku |
| Tab / Shift+Tab | wcięcie / wysunięcie bloku razem z dziećmi |
| Alt+↑ / Alt+↓ | przeniesienie bloku z dziećmi |
| Ctrl+. | zwinięcie / rozwinięcie dzieci (konspekt) |
| Esc | koniec edycji i zaznaczenie bloku |
| Shift+↑ / Shift+↓ | zaznaczenie kilku bloków (z edytowanego: gdy zaznaczenie tekstu dojdzie do brzegu bloku) |
| Ctrl+A dwa razy | tekst bloku, potem wszystkie bloki |
| Ctrl+C / Ctrl+X / Ctrl+V | kopiuj / wytnij / wklej — zaznaczone bloki albo tekst w bloku |
| Ctrl+Enter | zmiana statusu zadania |

W konspekcie kliknij kropkę bloku, żeby się do niego **przybliżyć** — widzisz tylko ten blok i jego dzieci. Enter w przybliżonym punkcie listy dodaje nowy podpunkt, który zostaje widoczny. Nagłówek i akapit nie mają dzieci: Enter w przybliżonym nagłówku albo akapicie (poza jego początkiem) kończy przybliżenie i dodaje nowy blok pod nim — Ctrl+Z wraca do przybliżenia. W dokumencie Enter na pustym punkcie listy kończy listę, a Tab działa tylko na punktach listy.

**Bloki kodu:** wpisz `` ``` `` z nazwą języka (np. `` ```js ``) i naciśnij Enter. W bloku kodu widzisz sam kod — linie ogrodzenia oraz identyfikator bloku (`^id`) pod nim są ukryte. Linia `` ``` `` wpisana w kodzie zostaje kodem: Brulion sam wydłuża ogrodzenie. Gdy zaczniesz kod linią `` ``` `` w akapicie albo punkcie listy, Brulion dopisze linię zamykającą na końcu bloku, więc kod nie obejmie kolejnych bloków. Samą linię otwierającą (np. `` ```js `` bez Entera) Brulion zamyka, gdy przejdziesz do innego bloku — zostaje pusty blok kodu. Enter i wklejanie między tymi liniami działają jak w bloku kodu.

## Zaznaczanie i kopiowanie
W edytowanym bloku zaznaczasz tekst jak zwykle. Zaznaczenie, które wychodzi poza blok, obejmuje **całe bloki** razem z ich dziećmi (także zwiniętymi) — tak samo w dokumencie (akapity, nagłówki, punkty list) i w konspekcie.

- **Klawiatura:** Esc zaznacza edytowany blok, Shift+↑ / Shift+↓ powiększa i zmniejsza zaznaczenie, Ctrl+A dwa razy zaznacza wszystkie bloki.
- **Mysz:** Shift+kliknięcie zaznacza od edytowanego (albo zaznaczonego) bloku do klikniętego — także obliczeń, tabeli, zapytania, obrazu czy kropki; przeciągnięcie z jednego bloku na drugi zaznacza wszystkie po drodze, a przy krawędzi okna strona sama się przewija. Przeciągnięcie myszą nie przewija już strony — przewija kółko i touchpad. Ctrl+kliknięcie dobiera pojedynczy blok do zaznaczenia albo go z niego wyjmuje — tak zaznaczysz na przykład punkty 1., 3. i 5., z przerwami między nimi; Shift+kliknięcie zaznacza potem znowu ciąg bloków od ostatnio dobranego.
- **Telefon:** przytrzymaj palec na bloku (nie edytowanym: na tekście, obliczeniach, komórce tabeli, zapytaniu) — blok zostaje zaznaczony, klawiatura znika, a na dole pojawiają się narzędzia zaznaczenia. Nie odrywając palca, przesuń go, żeby zaznaczyć więcej. Potem stuknięcie w inny blok przesuwa koniec zaznaczenia, a dwa okrągłe uchwyty na początku i końcu zaznaczenia pozwalają je poprawić. Przycisk „Pojedynczo” w narzędziach zaznaczenia sprawia, że stuknięcia dobierają albo wyjmują pojedyncze bloki (z przerwami, bez uchwytów); drugie stuknięcie w niego to wyłącza. „Gotowe” albo Wstecz kończy zaznaczanie. Przycisk „Zaznacz” jest też w pasku nad klawiaturą i w menu kropki (przytrzymanej). Wewnątrz edytowanego bloku przytrzymanie dalej zaznacza słowo z uchwytami systemu.
- **Co z zaznaczeniem:** Kopiuj, Wytnij, Usuń (Ctrl+Z albo „Cofnij” w powiadomieniu „Usunięto…” / „Wycięto…” przywraca), wcięcie i wysunięcie, przeniesienie w górę i w dół, status zadania — wszystko naraz. Zaznaczenie z przerwami kopiujesz, wycinasz i usuwasz tak samo, a wcięcia i przenoszenie działają tylko na bloki zaznaczone bez przerw — Brulion mówi wtedy, dlaczego nic się nie stało. Gdy zaznaczony jest jeden blok z dziećmi, Ctrl+Enter zmienia status tylko jemu. Na komputerze przy dwóch i więcej blokach u góry jest pasek z tymi przyciskami; prawy przycisk myszy na zaznaczonym bloku otwiera menu działające na całe zaznaczenie. Ctrl+V zastępuje zaznaczone bloki zawartością schowka.
- **Co trafia do schowka:** bloki jako Markdown — wklejone do Brulionu, Obsidiana albo zwykłego pola tekstowego zostają listą z wcięciami, a w poczcie czy edytorze dokumentów wklejają się sformatowane (lista, nagłówki, pola zadań). „Kopiuj jako tekst” (menu *Więcej* przy zaznaczeniu, menu bloku) daje zwykły tekst: punktory •, ☐ i ☑ zamiast pól zadań, bez znaczników. Za duże zaznaczenie (ponad ok. 800 KB) nie jest kopiowane — Brulion o tym mówi, zamiast uciąć tekst.
- **Kilka dni dziennika naraz:** w widoku **Dziennik** zaznaczenie może objąć bloki kilku dni. Zaznacz blok w jednym dniu i kliknij z Shiftem blok innego dnia, przeciągnij myszą z jednego dnia do następnego albo Shift+↓ / Shift+↑ przejdź za ostatni / pierwszy blok dnia; na telefonie, gdy zaznaczanie jest włączone, stuknij blok innego dnia. Dni pomiędzy są zaznaczone w całości. Takie zaznaczenie można tylko **skopiować** (Kopiuj, Ctrl+C, „Kopiuj jako tekst”): bloki każdego dnia trafiają do schowka po linku do tego dnia (`[[2026-10-27]]`, w zwykłym tekście po dacie, np. „wtorek, 27 października 2026”). Wytnij, Usuń, wcięcia i przenoszenie są wtedy wyszarzone i mówią „Na kilku dniach naraz działa tylko kopiowanie”. Kliknięcie w blok, Esc albo „Gotowe” kończy zaznaczenie we wszystkich dniach. Przeciągając myszą, przewiniesz dziennik o mniej więcej ekran poza dzień, w którym zaczęło się zaznaczanie — dłuższe zaznaczenie zrób kółkiem myszy i Shift+kliknięciem.
- **Wklejanie:** Markdown z kilkoma blokami staje się blokami z zachowanymi wcięciami; skopiowany jeden punkt wklejony w pusty punkt nie dostaje drugiego znacznika (zadanie zostaje zadaniem); tekst skopiowany ze strony WWW albo z edytora dokumentów przychodzi jako Markdown (listy, nagłówki, linki, pogrubienia). Wklejone identyfikatory bloków (`^abc123`), które już są gdzie indziej w notatniku, są pomijane, żeby `((odwołania))` nie wskazywały dwóch bloków — po wycięciu i wklejeniu (przeniesieniu) zostają.

**Kawałki kilku akapitów** (koniec jednego i początek następnego): zaznacz całe bloki i skopiuj je albo przełącz stronę na źródło Markdown (Ctrl+E, menu *Więcej* → *Tryb* → „Źródło”) i tam zaznacz dokładnie te znaki — przeciągnięciem myszy, a na telefonie przytrzymaniem słowa.

## Menu `/`
Wpisz `/` na początku bloku, a pojawi się lista poleceń: zadanie, przypomnienie, alarm, termin, powtarzanie, tabela, obliczenia, wzór, diagram, zapytanie, nagłówek, cytat, ramka, kod, szablon, obraz, osadzenie i odwołanie do bloku. Polskie znaki w filtrze są opcjonalne.

## Podpowiedzi podczas pisania
- `[[` — strony; gdy wpisanej nazwy nie ma jeszcze w notatniku, pierwsza pozycja, „Utwórz stronę…”, wstawia link do nowej strony o tej nazwie. Enter wybiera podświetloną pozycję: najlepiej pasującą istniejącą stronę (do nowej przejdziesz strzałką w górę), a gdy nic nie pasuje — nową stronę,
- `((` — bloki z całego notatnika,
- `#` — tagi,
- po `📅`, `⏳`, `⏰` albo `@` — daty słowami: „jutro o 9”, „w piątek 18:00”.

## Ramki, wyróżnienia, komentarze
> [!note] Ramka
> Rodzaje: `note`, `tip`, `warning`, `danger`, `info`, `todo`, `quote`. Wpisujesz `> [!tip] Tytuł`, a w kolejnych liniach `> treść`.

==Zakreślenie== piszesz między podwójnymi znakami równości, ~~przekreślenie~~ w `~~tyldach~~`, a komentarz widoczny tylko w edycji w `%%procentach%%`.

### Kolorowe zakreślenia
Zakreślenie może mieć kolor: <mark style="background: #FF5582A6;">czerwony</mark>, <mark style="background: #BBFABBA6;">zielony</mark>, <mark style="background: #ADCCFFA6;">niebieski</mark> i inne. Zaznacz tekst w edytowanym bloku, kliknij go prawym przyciskiem i wybierz **Zakreśl…** (na telefonie przycisk z zakreślaczem na pasku nad klawiaturą), a potem kolor. Gdy kursor albo zaznaczenie jest w zakreśleniu, ten sam wybór zmienia jego kolor, a **Usuń zakreślenie** je zdejmuje. Każdą zmianę cofniesz (Ctrl+Z).

W pliku kolorowe zakreślenie wygląda tak, jak zapisuje je wtyczka Highlightr do Obsidiana: `<mark style="background: #FF5582A6;">tekst</mark>` — więc Obsidian, GitHub i Pandoc pokażą je bez Brulionu. Brulion czyta też `<mark class="hltr-red">` i inne kolory, ale zawsze pokazuje je w jednym z dziewięciu kolorów swojej palety: nic z wyglądu zapisanego w notatce (styl, skrypt) nie trafia do aplikacji. Domyślny kolor to zwykłe zakreślenie między podwójnymi znakami równości.

## Tryb źródła
**Ctrl+E** pokazuje cały plik jako surowy Markdown z podświetleniem składni. Przydaje się do hurtowych zmian; kolejne naciśnięcie wraca do bloków. Na telefonie tekst przesuwasz palcem, dotknięcie stawia kursor, a przytrzymanie zaznacza słowo. (Do kopiowania fragmentów kilku bloków nie musisz już przechodzić do źródła: zobacz [[#Zaznaczanie i kopiowanie|Zaznaczanie i kopiowanie]].)

## Statystyki i tryb skupienia
W prawym dolnym rogu strony (w szerokim oknie) widać, ile ma słów i znaków i ile zajmie jej przeczytanie — przy zaznaczeniu tekstu albo bloków liczby dotyczą zaznaczenia. Najedź na nie, a zobaczysz też liczbę znaków bez spacji. To samo jest zawsze na górze menu strony (**⋯** obok tytułu), także na telefonie. Licznik wyłączysz w *Ustawienia → Edytor → Liczba słów*.

Brulion liczy to, co czytelnik widzi: bez znaczników Markdown, nagłówka YAML i właściwości `klucz:: wartość`, identyfikatorów bloków, dat zadań i przypomnień, komentarzy `%%…%%`, obrazów i osadzeń; link liczy się jako jego tekst (`[[Strona|etykieta]]` to „etykieta”), a bloki kodu, zapytań, obliczeń i diagramów oraz wzory w `$$` nie liczą się wcale. Słowo to wszystko między odstępami, co ma choć jedną literę albo cyfrę: „biało-czerwony”, „m.in.”, „3,14” i adres strony to po jednym słowie, a samotny myślnik „—” czy emoji — żadne. Twarda spacja po „w” albo „i” rozdziela słowa, a między cyframi („1 000 000”) nie. Czas czytania liczony jest dla 200 słów na minutę.

**Tryb skupienia** (F8, w palecie „Tryb skupienia” albo *Ustawienia → Edytor*) chowa pasek boczny, górny pasek, panel boczny i dolną nawigację — na stronie i w dzienniku zostaje sam tekst, na środku ekranu. Wychodzisz z niego klawiszem F8, Esc (kiedy edytor nie potrzebuje go już do zamknięcia podpowiedzi czy zaznaczenia), przyciskiem w prawym górnym rogu albo gestem Wstecz na telefonie. Brulion pamięta go na tym urządzeniu, także po ponownym uruchomieniu; inne widoki (zadania, kalendarz, ustawienia) pokazują swoje paski jak zwykle.

**Przewijanie jak w maszynie do pisania** (*Ustawienia → Edytor*, albo „Przewijanie jak w maszynie do pisania” w palecie) trzyma wiersz, w którym piszesz, na środku ekranu — także w trybie źródła i w dzienniku. Pod ostatnim blokiem jest wtedy miejsce, by i ostatni wiersz mógł stanąć na środku.

## Obrazy i pliki
Wklej obraz (Ctrl+V), upuść pliki na stronę albo użyj `/Obraz…` — okno wyboru plików przyjmie kilka obrazów naraz, także w przeglądarce. Pliki trafiają do folderu załączników (`assets/`) i są osadzane jako `![[plik.png]]`.

Plik dowolnego typu wstawisz jako link poleceniem `/Plik…`: w tekście pojawi się `[[raport.docx]]` (kilka wybranych plików — kilka linków). Plik spoza notatnika trafia do folderu załączników, a plik, który już jest w notatniku, zostaje na swoim miejscu. Notatka, obraz i PDF otwierają się po kliknięciu jak zwykle, a inne pliki w Brulionie tylko do odczytu, jako tekst.

Kliknięcie osadzonego obrazu pokazuje samo zdjęcie dopasowane do widoku (mały obraz zostaje w naturalnej wielkości, bez rozmywającego powiększania). Gdy zdjęcie jest większe od widoku, kliknięcie na nim albo przycisk „Pokaż naturalny rozmiar” (na telefonie i w wąskim widoku ikona lupy) pokazuje je w naturalnej wielkości z możliwością przeciągania. Pod zdjęciem są notatki, które je osadzają albo do niego linkują (na szerokim ekranie także w panelu bocznym, w zakładce Linki).

„Pokaż jako tekst” w pasku albo w menu Więcej (albo Ctrl+E) pokazuje plik jako tekst — tylko do odczytu, przydatne dla plików SVG (treść binarnego obrazu to nieczytelne znaki; z dużego pliku widać pierwsze 200 KB; zmiana pliku, np. z synchronizacji, od razu się tu pokazuje); „Pokaż obraz” albo ponownie Ctrl+E wraca do zdjęcia. Jeśli urządzenie nie ma dekodera danego formatu (np. WebP albo AVIF na telefonie lub w przeglądarce), Brulion pisze o tym zamiast pokazywać błąd — także w obrazie osadzonym w notatce.

Inne pliki, które nie są notatką, obrazem ani PDF-em (np. DOCX albo ZIP), otwierają się tylko do odczytu jako tekst — nic, co wpiszesz, ich nie nadpisze.

## PDF-y
PDF osadzony w notatce (`![[wykład.pdf]]`, tak jak obraz: wklejony, upuszczony albo wybrany przez `/Obraz…`) pokazuje **miniaturę pierwszej strony** — białą kartkę w cienkiej ramce, także w ciemnym motywie — a pod nią nazwę pliku i liczbę stron. `![[wykład.pdf#page=3]]` (zapis z Obsidiana) pokazuje trzecią stronę. Miniatura rysuje się raz i zostaje w pamięci podręcznej urządzenia, więc dziennik przewija się płynnie, a po ponownym uruchomieniu widać ją od razu.

Kliknięcie miniatury, odnośnik `[[wykład.pdf]]` (albo `[[wykład.pdf#page=3]]` — od razu na trzeciej stronie) lub plik wybrany w drzewie czy szybkim przełączaniu otwiera **przeglądarkę PDF**:
- strony przewijasz w pionie; rysują się dopiero, gdy do nich dojdziesz, więc nawet gruby skrypt otwiera się od razu;
- **powiększanie**: dwoma palcami, `Ctrl` + kółko myszy, przyciski **+** i **−** albo procent w pasku (np. 100 %, 200 %, **Dopasuj do szerokości**); dwukrotne stuknięcie przełącza między szerokością strony a dwukrotnym powiększeniem; powiększoną stronę przesuwasz palcem albo myszą;
- **„Strona 3 z 12”** w pasku (na telefonie „3/12”) — stuknij, żeby przejść do wybranej strony;
- **szukanie** (lupa, `Ctrl+F`) bez polskich znaków i wielkości liter: „gesla” znajdzie „gęślą”; trafienia są podświetlone, a `Enter` / strzałki przechodzą do następnego. W skanie (PDF bez warstwy tekstu) nie ma czego szukać — Brulion to powie. Na Androidzie szukanie działa od wersji 15; na starszych lupy nie ma;
- **zaznaczanie tekstu**: przeciągnij myszą po stronie (kursor zmienia się w pionową kreskę) albo przytrzymaj palec na słowie — zaznaczy się całe słowo — i nie odrywając go, przesuń dalej. **Kopiuj** nad zaznaczeniem, `Ctrl+C` albo prawy przycisk myszy kopiuje tekst do schowka, `Ctrl+A` zaznacza całą bieżącą stronę, `Esc` albo stuknięcie obok kończy zaznaczanie. Zaznaczenie obejmuje jedną stronę. W skanie (bez warstwy tekstu) nie ma czego zaznaczać; na Androidzie zaznaczania jeszcze nie ma;
- **spis treści** — przycisk z listą w pasku, gdy PDF ma spis (zakładki): pokazuje rozdziały z numerami stron, zaznacza ten, w którym jesteś, a stuknięcie przenosi do rozdziału. Spis treści działa na razie w wersji z popplerem (np. pakiet dla NixOS); w wersji dla Windowsa, w AppImage i na Androidzie przycisku jeszcze nie ma;
- **linki w PDF-ie**: odnośnik do innej strony tego PDF-a (np. w spisie treści na pierwszej stronie) przenosi tam, a adres strony WWW albo e-mail otwiera się w przeglądarce czy programie pocztowym — tak jak linki w notatkach, tylko `http`, `https` i `mailto`; skrypty i uruchamianie programów z PDF-a nigdy nie działają. Po najechaniu myszą widać, dokąd prowadzi link;
- **dwie strony obok siebie** (menu powiększenia, na telefonie **⋯**) — jak w otwartej książce; z **Okładką osobno** pierwsza strona stoi sama, a dalej 2–3, 4–5… Brulion pamięta ten wybór na urządzeniu;
- **adnotacje** zapisane w PDF-ie (zakreślenia, notatki, pieczątki) są widoczne na stronach; nowe dodasz w innej aplikacji (**Otwórz w innej aplikacji**);
- **Rysuj na tej stronie** — robi rysunek, którego podkładem jest bieżąca strona (zobacz [[Rysunki#Notatki na slajdach i PDF-ach|Notatki na slajdach i PDF-ach]]);
- **Otwórz w innej aplikacji** (w menu **⋯** albo **Więcej**) — przekazuje PDF systemowej aplikacji do PDF-ów, np. żeby go wydrukować albo dopisać adnotacje;
- **Wstecz**, `Esc` albo `Alt+←` zamyka przeglądarkę. Pod ostatnią stroną są notatki, które osadzają PDF albo do niego linkują.

PDF **chroniony hasłem** pokazuje kłódkę, nazwę i rozmiar pliku oraz pole na hasło: wpisz je i stuknij **Otwórz PDF** (albo naciśnij `Enter`). Przy złym haśle Brulion poprosi o nie jeszcze raz. Hasła nigdzie nie zapisuje — przy każdym otwarciu PDF-a trzeba je wpisać od nowa — dlatego miniatura takiego PDF-a w notatce zostaje kłódką, a **Rysuj na tej stronie** nie jest wtedy dostępne. Na Androidzie systemowy moduł PDF nie otwiera jeszcze plików z hasłem: tam zostaje komunikat „Ten PDF jest chroniony hasłem — Brulion go nie otworzy.”, a **Otwórz w innej aplikacji** przekaże plik programowi, który to umie. Uszkodzony PDF pokazuje tyle, ile się da, a strona, której nie da się narysować, mówi o tym zamiast zawieszać aplikację.

**Inne załączniki** — dokumenty Worda i LibreOffice, arkusze, prezentacje, e-booki, nagrania, filmy, pliki tekstowe i CSV (`.docx`, `.xlsx`, `.pptx`, `.odt`, `.ods`, `.odp`, `.epub`, `.doc`, `.xls`, `.ppt`, `.rtf`, `.mp3`, `.m4a`, `.ogg`, `.opus`, `.wav`, `.flac`, `.mp4`, `.mov`, `.webm`, `.mkv`, `.txt`, `.csv`, `.tsv`) — Brulion pokazuje tylko do odczytu jako tekst, a przycisk **Otwórz w innej aplikacji** w pasku (i w menu **Więcej**) przekazuje je programowi, który je otwiera. Brulion sprawdza przy tym nie tylko nazwę, ale i początek pliku: program, skrypt, strona HTML czy archiwum — także przemianowane na `.docx` — nigdy nie trafią do innej aplikacji.

**W przeglądarce** Brulion nie rysuje PDF-ów: osadzony PDF jest kafelkiem pliku, a kliknięcie go albo odnośnika otwiera PDF w **nowej karcie**, gdzie przewijanie, powiększanie i szukanie daje wbudowana przeglądarka PDF. Gdy przeglądarka zablokuje nową kartę, PDF się pobiera, a Brulion to mówi.

Zobacz też: [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]], [[Skróty klawiszowe|Skróty klawiszowe]].
