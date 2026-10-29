---
tags: [dokumentacja]
aliases: [Zmiany, Historia zmian, Changelog]
brulion: document
---
# Lista zmian

Tu znajdziesz zmiany w każdej wersji Brulionu, od najnowszej. Pełne opisy wydań z plikami do pobrania są na GitHubie: https://github.com/janusz-bit/brulion/releases

## 0.5.1 — w przygotowaniu

**Nowe**
- **Ciemne tło rysunków** — przycisk **Tło rysunku** w edytorze rysunku (na telefonie w menu **Więcej**) ma trzy możliwości: **Jak motyw** (domyślnie: rysunek ciemnieje i jaśnieje razem z motywem Brulionu, od razu), **Jasne** i **Ciemne**. Na ciemnej kartce kolory są dopasowane jak w ciemnym trybie Excalidraw — atrament jasny, kolory czytelne, linie papieru przygaszone, a zakreślacz zostaje widoczny. Kolory zapisane w rysunku się nie zmieniają, a wybór zapisuje się w pliku. Podglądy rysunków w notatkach i widok obrazu też idą za motywem, a rysunek „jak motyw” ciemnieje również w Obsidianie z ciemnym motywem i w przeglądarce, która używa ciemnego motywu. Zobacz [[Rysunki#Jasne i ciemne tło|Jasne i ciemne tło]].
- **Edycja właściwości strony** — panel **Właściwości** ma teraz pola według typu: tekst (z działającymi linkami), liczba (także „3,5” i „1 200”), pole wyboru, data (także „jutro”, „w piątek”), data i godzina, lista, tagi i aliasy. Właściwości dodasz (**Ctrl+;** albo **Dodaj właściwość** w menu strony — także na stronie bez żadnych właściwości), zmienisz im nazwę albo typ i usuniesz bez edycji tekstu; podpowiedzi pokazują klucze i wartości z całego notatnika. Tagi i aliasy dodasz i usuniesz też w nagłówku strony. Zmienia się tylko ta jedna właściwość — komentarze, kolejność i zapis reszty nagłówka zostają bez zmian — w nagłówku YAML i w stylu Logseq (`klucz:: wartość`). Każdą zmianę cofniesz, a rozwinięty panel zostaje rozwinięty. Zobacz [[Tagi i właściwości#Edycja w panelu Właściwości|Edycja w panelu Właściwości]].

**Zmiany**
- **Szybsze otwieranie dużych notatników i mniej pamięci** — notatki indeksują się na kilku rdzeniach naraz, a gotowy indeks zostaje zapisany na urządzeniu (w danych Brulionu, nie w notatniku), więc przy kolejnym otwarciu Brulion analizuje od nowa tylko notatki zmienione od ostatniego razu (wszystkie pliki nadal czyta, żeby to sprawdzić). Notatnik z 10 000 notatek jest na komputerze w pełni zaindeksowany po niecałej sekundzie zamiast ok. 1,9 s, a indeks zajmuje o 60 % mniej pamięci (cała aplikacja: ok. 340 zamiast 510 MB). Na komputerze i telefonie wyszukiwanie jest kompletne od chwili, gdy indeks jest gotowy. Przeglądarka też zapamiętuje gotowy indeks (osobno od historii wersji i ustawień, więc gdy zabraknie jej miejsca, przepada tylko on) i przy kolejnych wizytach od razu ma pełne wyszukiwanie; przy pierwszej wizycie wyszukiwanie, jak dotąd, dobudowuje się chwilę po wczytaniu notatek. `brulion-cli` korzysta z tego samego indeksu co aplikacja. Pierwsze otwarcie po aktualizacji Brulionu buduje indeks od nowa. Zobacz [[Format plików|Format plików]].
- **Telefon: dolna nawigacja wraca po schowaniu klawiatury** — gdy schowasz klawiaturę przyciskiem Wstecz albo jej własnym klawiszem, a pisanie trwa dalej, dolny pasek z Dziennikiem, Kalendarzem i Zadaniami znów jest na ekranie, a pasek narzędzi leży tuż nad nim (tak samo narzędzia zaznaczenia bloków). Wysuwana klawiatura zakrywa nawigację i pasek wjeżdża razem z nią, bez podskoku. Dotknięcie przycisku nawigacji kończy pisanie i otwiera ten widok. Działa w aplikacji na Androida i w wersji w przeglądarce na telefonie. Zobacz [[Pierwsze kroki#Pisanie na telefonie|Pisanie na telefonie]].

**Poprawki**
- **Wersja w przeglądarce na telefonie: pasek narzędzi leży na klawiaturze** — w Brave na Androidzie między paskiem formatowania a klawiaturą był pusty pas (Brave podaje za małą wysokość widocznej części strony, gdy chowa swój dolny pasek). Teraz przeglądarka sama zmniejsza stronę nad klawiaturą, więc pasek przylega do niej w Brave, Chrome i Firefoksie (w Chrome i Brave między nimi zostaje tylko pasek autouzupełniania samej przeglądarki), a górny pasek aplikacji nie ucieka już z ekranu po stuknięciu w blok nisko na stronie.
- **Enter w przybliżonym nagłówku albo akapicie** — w konspekcie, po „Przybliż” na nagłówku albo akapicie, Enter na końcu (albo w środku) tekstu wstawiał blok poza przybliżeniem i kursor znikał. Teraz przybliżenie się kończy, a nowy blok pojawia się pod nim z kursorem; Ctrl+Z wraca do przybliżenia. W przybliżonym punkcie listy Enter w środku tekstu przenosi resztę do nowego podpunktu, który zostaje widoczny (także gdy punkt nie miał dzieci albo był zwinięty). Cofanie i ponawianie nie zostawiają już kursora w ukrytym bloku: przybliżenie, które go nie obejmuje, się kończy, a zwinięte punkty nad nim się rozwijają. Oddalenie (strzałka albo ścieżka nad blokami) w trakcie pisania w podpunkcie zwiniętego punktu rozwija go, więc pisany blok zostaje na ekranie.

## 0.5.0 — 28 października 2026

**Nowe**
- **Rysunki w notatkach** — polecenie `/Rysunek` (na telefonie także przycisk **Rysunek** nad klawiaturą) wstawia rysunek i otwiera go do rysowania: pióro z naciskiem rysika, zakreślacz, gumka (także drugim końcem rysika), kolory i grubości, papier gładki, w linie albo w kratkę, cofanie. Gdy rysujesz rysikiem, palce tylko przesuwają i powiększają kartkę; na telefonie bez rysika rysuje palec. Rysunek to plik SVG w załącznikach: Obsidian pokazuje go jako obraz, excalidraw.com otwiera do edycji, Syncthing go synchronizuje, a zmiany z dwóch urządzeń łączą się linia po linii. Kliknięcie rysunku w notatce otwiera go ponownie. Zobacz [[Rysunki|Rysunki]].
- **Import z Microsoft To Do** — **Importuj notatki → Microsoft To Do → Zaloguj się w Microsoft**: wpisujesz kod na microsoft.com/devicelogin (także na telefonie), a Brulion pobiera wszystkie listy — każda staje się stroną z zadaniami: stan, ważność, termin, przypomnienie, powtarzanie, kategorie jako tagi, notatka, kroki, linki i pliki. Logowanie wymaga własnej, bezpłatnej rejestracji aplikacji w Microsoft Entra — przewodnik prowadzi przez nią krok po kroku. Bez niej zaimportujesz plik JSON zapisany z Graph Explorera. Zobacz [[Import i eksport#Microsoft To Do|Microsoft To Do]].
- **Menu na znacznikach i linkach** — prawy przycisk myszy albo przytrzymanie palcem otwiera menu także na 🔁 (zmiana reguły, gotowe reguły, „Nie powtarzaj”), na priorytecie (od „Najwyższy” do „Bez priorytetu”), na linku do dnia `[[2026-10-20]]` (otwórz, kopiuj, przesuń datę), na `#tagu` i na `[[linku]]` („Otwórz”, „Otwórz w panelu bocznym”, „Kopiuj link”). Menu przypomnienia ma podmenu **Powtarzanie** i **Priorytet**. Menu dat działa też w wynikach zapytań, we wstawionych blokach, w komórkach tabel, w widoku **Zadania** i w kalendarzu — zmiana trafia do notatki z zadaniem i cofniesz ją przyciskiem **Cofnij**. Zmienia się tylko ten jeden znacznik. W widoku Zadania przytrzymanie tagu otwiera teraz menu tagu (strona tagu jest w nim, a także pod Ctrl+kliknięciem). Zobacz [[Zadania|Zadania]] i [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]].
- **Ustawienia ze skarbca Obsidiana** — folder Obsidiana otwarty jako notatnik sam przejmuje folder, nazwy plików i szablon dziennika, folder szablonów i folder załączników z ustawień Obsidiana (`.obsidian/`), dopóki notatnik nie ma własnych ustawień. **Ustawienia → Notatnik** pokazują, co przejął, a **Zapisz w notatniku** zapisuje to w `.brulion/config.json`. Zobacz [[Import i eksport#Skarbiec Obsidiana bez importu|Skarbiec Obsidiana bez importu]].
- **Zaznaczanie bloków z kilku dni dziennika** — w widoku **Dziennik** zaznaczysz bloki kilku dni naraz: Shift+kliknięciem w blok innego dnia, przeciągnięciem myszą, Shift+strzałkami za brzeg dnia albo na telefonie stuknięciem w blok innego dnia. Takie zaznaczenie kopiujesz (także jako tekst), a bloki każdego dnia trafiają do schowka po linku do tego dnia. Zobacz [[Dokument i konspekt#Zaznaczanie i kopiowanie|Zaznaczanie i kopiowanie]].
- **Zmiana nazwy na serwerze dociera jako zmiana nazwy** — folder albo strona, którym zmieniono nazwę albo które przeniesiono na innym urządzeniu albo w Nextcloudzie w przeglądarce, dostają tu tę samą nową nazwę. Notatki nie są pobierane od nowa i nie trafiają do kosza, a ich historia wersji, ulubione i zwinięte bloki idą za nimi. Stronę, której nazwę zmienisz w Brulionie, synchronizacja przenosi na serwerze jednym poleceniem, tak jak folder — także gdy zaraz potem piszesz w niej dalej. Zobacz [[Synchronizacja#Foldery|Foldery]].
- **Zdjęcia z internetu: powiększenie, wklejanie i eksport** — kliknięcie pokazanego zdjęcia otwiera je na całe okno (dopasowane albo w rzeczywistym rozmiarze). Wklejony adres obrazu („Kopiuj adres obrazu” w przeglądarce) i fragment strony ze zdjęciami Brulion proponuje od razu zapisać w notatniku. Eksport do HTML i Pandoca pyta, czy zostawić adresy zdjęć, czy dołączyć ich kopie. Zobacz [[Linki i odnośniki zwrotne#Zdjęcia z internetu|Zdjęcia z internetu]].
- **Wygląd na twoją miarę** — nowa sekcja **Ustawienia → Wygląd**: kolor akcentu (sześć gotowych albo własny — Brulion sam dba, żeby tekst w nim był czytelny), białe tło zamiast papieru i czarne tło dla ekranów OLED, czcionka interfejsu, zwarta gęstość na komputerze, a w części **Układ** — które pozycje pokazuje pasek boczny, jakie cztery przyciski ma dolna nawigacja na telefonie i czy widać przycisk szybkiej notatki. W **Ustawieniach → Edytor** dowolna zainstalowana czcionka notatek i kodu oraz odstęp między wierszami. Motyw, rozmiar tekstu i ograniczenie animacji przeszły z sekcji Ogólne do Wyglądu. Zobacz [[Pierwsze kroki#Wygląd|Wygląd]].
- **Powiadomienia Windows z przyciskami i autostart** — na Windows przypomnienia i alarmy są teraz powiadomieniami Windows z przyciskami **Drzemka** i **OK**, a kliknięcie otwiera notatkę. Alarm zostaje na ekranie i dzwoni, aż odpowiesz; dźwięk wybierzesz spośród dźwięków powiadomień Windows (także **Odtwórz próbkę**). Nowy przełącznik **Ustawienia → Aplikacja → Uruchamiaj po zalogowaniu** (i zadanie w instalatorze) uruchamia Brulion ukryty w zasobniku zaraz po zalogowaniu, żeby przypomnienia dzwoniły od początku dnia. Zobacz [[Przypomnienia i alarmy#Powiadomienia|Powiadomienia]].
- **Udostępnij → Brulion** — na Androidzie Brulion jest w menu **Udostępnij** innych aplikacji: stronę z przeglądarki, zaznaczony tekst albo pliki `.md` i `.txt` (także kilka naraz) zapiszesz w dzisiejszym dzienniku albo jako nową stronę. Przed zapisaniem tekst można poprawić, a tytuł strony zmienić; nic nie zapisuje się bez Twojej zgody i nic nie nadpisuje istniejących stron. Zobacz [[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]].
- **Widżety na Androidzie** — na ekranie głównym telefonu: **Zadania — Mój dzień** (zaległe i dzisiejsze zadania, w większym widżecie także jutrzejsze), **Kalendarz** (miesiąc z kropkami i agenda — najpierw zadania z godziną) i **Notatki** (dzisiejszy dziennik, szybka notatka, ulubione i ostatnie strony). Stuknięcie w zadanie otwiera notatkę na tym wierszu, a **+** dodaje zadanie bez otwierania aplikacji: dzień, godzinę i alarm wybierasz przyciskami, a zadanie trafia do dzisiejszego dziennika od razu, także przy zamkniętym Brulionie. Zobacz [[Zadania#Widżety na Androidzie|Widżety na Androidzie]].
- **„Zrobione” w powiadomieniu przypomnienia** — przypomnienie przy zadaniu ma w powiadomieniu przycisk **Zrobione** obok **Drzemka** i **OK**: oznacza zadanie jako zrobione (`[x]` z datą `✅`, zadanie cykliczne dostaje następny termin) bez otwierania notatki. Działa na Linuksie, na Windows, na Androidzie (także na ekranie alarmu i przy zamkniętej aplikacji — zmiana trafia do notatki przy następnym uruchomieniu, z datą stuknięcia) i na banerze przegapionych przypomnień. Zobacz [[Przypomnienia i alarmy#Powiadomienia|Powiadomienia]].
- **Kalendarz z ikony w zasobniku** — na komputerze ikona Brulionu obok zegara otwiera mały kalendarz, jak zegar w Windows 7 i 10: dzisiejsza data, miesiąc z kropkami i zadania i przypomnienia wybranego dnia, a do tego **Otwórz Brulion** i **Szybka notatka…**. Kliknięcie zadania pokazuje ten dzień w oknie Brulionu, a kliknięcie obok chowa kalendarz. Na Windows otwiera go samo kliknięcie ikony, na Linuksie pozycja **Kalendarz** w jej menu. Zobacz [[Dziennik, kalendarz i szablony#Kalendarz z ikony w zasobniku|Kalendarz z ikony w zasobniku]].

**Zmiany**
- **Kliknięcie ikony w zasobniku na Windows** otwiera teraz kalendarz zamiast okna. Dawne zachowanie przywrócisz w *Ustawienia → Aplikacja → Kliknięcie ikony w zasobniku → Okno*. Podwójne kliknięcie zawsze pokazuje okno.
- **Menu zdjęcia z internetu** otwierasz teraz prawym przyciskiem myszy albo przytrzymaniem palca (kliknięcie powiększa zdjęcie). W menu jest nowa pozycja **Powiększ**.
- **Szybsze kopiowanie bloków na komputerze z Linuksem** — sformatowana wersja kopii (dla poczty i edytorów dokumentów) powstaje dopiero wtedy, gdy wklejasz ją do takiego programu. Skopiowanie całej strony z 5000 bloków zajmuje teraz kilka milisekund zamiast około 40, a wklejenie do Brulionu, edytora kodu czy terminala w ogóle jej nie tworzy.

**Poprawki**
- **Enter na początku tekstu zadania** wstawia nad nim puste zadanie, tak jak w zwykłym punkcie listy. Tekst zostaje w swoim bloku razem ze statusem (zrobione zadanie nie wraca jako niezrobione) i podpunktami — wcześniej blok dzielił się za ukrytym `[ ]`, a tekst przechodził do nowego bloku. Zobacz [[Dokument i konspekt|Dokument i konspekt]].
- **Ponowienie (Ctrl+Shift+Z) nie gubi pustego akapitu** — po Enterze na początku akapitu albo nagłówka i cofnięciu ponowienie przywraca też wstawiony pusty blok, a kursor wraca do niego.
- Eksport HTML i Pandoc: zdjęcie wstawione jako `![[https://…/kot.jpg]]` było „brakującym załącznikiem” — teraz jest zdjęciem.
- Wersja w przeglądarce: zdjęcie z internetu, którego strona nie pozwala pobrać innym witrynom (CORS) albo spod `http://` w Brulionie otwartym przez `https://`, pokazuje ten powód zamiast „Brak połączenia”. Zapytania o zdjęcia nie wysyłają już nagłówków, przez które część serwerów odrzucała je w przeglądarce.
- **Pasek przewijania nie skacze** — w długich stronach, w dzienniku, w widoku **Zadania**, w agendzie kalendarza, w historii wersji i w rejestrze zdarzeń suwak paska przesuwa się tylko razem z treścią, zamiast skakać tam i z powrotem podczas przewijania. Jego położenie liczy się w blokach (w dzienniku: w dniach), więc przez bardzo długi dzień przesuwa się wolniej niż przez krótki, a zwinięte bloki się nie liczą.

## 0.4.5 — 26 października 2026

**Nowe**
- **Zdjęcia z internetu** — obraz spod adresu `https://…` w notatce, `![opis](https://example.com/kot.jpg)` albo poleceniem `/` → **Obraz z internetu…**. Brulion nic nie pobiera, dopóki nie klikniesz **Pokaż** (w **Ustawieniach → Edytor** włączysz pokazywanie bez pytania). Pobrane zdjęcie działa też bez sieci, a **Zapisz w notatniku** zapisuje jego kopię w załącznikach. Zobacz [[Linki i odnośniki zwrotne#Zdjęcia z internetu|Zdjęcia z internetu]].
- **Wersja na Windows (testowa)** — instalator albo wersja przenośna (ZIP) dla Windows 10 i 11. Przypomnienia pokazuje ikona w zasobniku systemowym.

**Poprawki**
- Synchronizacja Nextcloud na telefonie z Androidem i na Windows: gdy na innym urządzeniu zmieniła się tylko wielkość liter w nazwie folderu (`projekty` → `Projekty`), notatki z tego folderu mogły trafić do kosza. Teraz folder zmienia nazwę.
- Zapis notatki, który system na chwilę odrzucił (np. gdy plik sprawdza program antywirusowy), jest ponawiany.

## 0.4.4 — 25 października 2026

**Nowe**
- **Eksport dla programu Pandoc** — format „Pandoc Markdown (ZIP)” w widoku **Import i eksport**: jeden dokument `.md` z całym notatnikiem, folderem albo stroną i załącznikami, do których prowadzą linki. `pandoc "Notatnik.md" -o "Notatnik.docx"` w rozpakowanym folderze zrobi z niego plik Worda (także LibreOffice, PDF albo EPUB). Linki `[[…]]` i odwołania do bloków prowadzą do miejsc w dokumencie, osadzenia są wstawione, a wzory, callouty, tabele, zadania i przypisy zostają wzorami, ramkami, tabelami, polami wyboru i przypisami. Daty i przypomnienia są zapisane słowami („termin: 2026-10-20”). Zobacz [[Import i eksport#Dokument dla programu Pandoc|Import i eksport]].

## 0.4.2 — 24 października 2026

**Nowe**
- **Zmiana daty z menu** — kliknij znacznik daty (📅, ⏳, 🛫, ✅, ➕, ❌) prawym przyciskiem myszy albo przytrzymaj go palcem: „Zmień datę…” (kalendarz z godziną), „Dziś”, „Jutro”, „Dzień później”, „Tydzień później”, „Usuń datę”. Na przypomnieniu ⏰ menu ma „Zmień datę i godzinę…”, „Godzinę później”, „Jutro o tej porze”, „Tydzień później”, przełącznik „Alarm” i „Usuń przypomnienie”. Zmienia się tylko ta data — reszta wiersza zostaje bez zmian — a zmianę cofniesz `Ctrl+Z`. Zwykłe kliknięcie znacznika dalej zaczyna edycję. Działa w notatkach i w dzienniku. Zobacz [[Zadania|Zadania]] i [[Przypomnienia i alarmy|Przypomnienia i alarmy]].

## 0.4.1 — 24 października 2026

**Zmiany**
- **Usunięty widok „Zaznacz tekst”** z wersji 0.4.0 — na telefonie nie działał dobrze. Do kopiowania zaznaczaj całe bloki: przytrzymaj tekst bloku na telefonie, a na komputerze użyj Shift+strzałek, przeciągnięcia myszą albo Ctrl+klik. Fragment kilku akapitów zaznaczysz znak po znaku w trybie źródła Markdown (`Ctrl+E` albo **Więcej → Źródło Markdown**). Zobacz [[Dokument i konspekt|Dokument i konspekt]].

## 0.4.0 — 24 października 2026

**Uwaga**
- **Tagi są teraz linkami do stron** (jak w Logseq): kliknięcie `#praca` otwiera stronę „praca”, a jej odnośniki pokazują bloki z tym tagiem. Dawny widok tagu (podtagi, zadania, strony oznaczone w całości) jest pod „Widok tagu” w nagłówku strony i na liście tagów. Zmiana nazwy strony poprawia też tagi, które ją nazywają. Zobacz [[Tagi i właściwości|Tagi i właściwości]].
- **Przeniesienie albo zmiana nazwy strony może poprawić linki i tagi w innych notatkach**, żeby dalej prowadziły do tych samych plików: gdy dwie strony mają tę samą nazwę, `[[Dom]]` staje się `[[Projekty/Dom]]`, a `#dom` — `#Projekty/Dom`. Za stroną idą też filtry zapytań (`path:`, `page:`, `tag:`, `ref:`) i linki Markdown. Przed przeniesieniem widać, ile notatek się zmieni.
- **Nextcloud synchronizuje teraz także foldery** — puste foldery w obie strony, a zmiana nazwy folderu idzie na serwer jedną operacją. Puste foldery, które zostały po zmianach nazw w starszych wersjach, pojawią się raz po drugiej stronie; usuń niepotrzebne, usunięcie też się zsynchronizuje. Gdy cały notatnik leży w jednym folderze, zmienisz jego nazwę i jeszcze przed synchronizacją usuniesz albo zmienisz w nim notatkę, Brulion zapyta o potwierdzenie usunięć. Zobacz [[Synchronizacja|Synchronizacja]].
- **Na komputerze przeciąganie myszą po tekście zaznacza bloki** zamiast przewijać stronę — przewijasz kółkiem albo touchpadem (palcem na ekranie dotykowym jak dotąd).
- **Na telefonie podczas pisania chowa się dolna nawigacja**, a pasek narzędzi siedzi na klawiaturze. Na Androidzie 11–14 okno sięga teraz pod paski systemowe, jak na 15 i nowszych.

**Nowe**
- **Przenoszenie stron i folderów** — „Przenieś do…” w menu strony i folderu w drzewie (prawy przycisk myszy albo „⋯” na telefonie), w menu **Więcej**, w menu strony, na liście stron i w palecie poleceń; na komputerze także przeciągnięcie myszą na folder albo na nagłówek „Strony” (główny folder notatnika). Linki, osadzenia, odwołania do nagłówków i bloków, tagi i filtry zapytań wskazujące na przeniesione pliki są poprawiane, a przeniesienie cofniesz z komunikatu. „/” w nowej nazwie przenosi stronę (np. `Archiwum/Dom`). Zobacz [[Pierwsze kroki|Pierwsze kroki]].
- **Nowe foldery wszędzie** — „Nowy folder” w menu „+” przy nagłówku „Strony” (także w głównym folderze), w palecie poleceń i w oknie przenoszenia. Nowa strona trafia od razu do folderu otwartej strony; w oknie nowej strony możesz to zmienić.
- **Drzewo folderów** pokazuje otwartą stronę, sortuje po polsku (liczby według wartości), ma folder dziennika na górze z czytelnymi datami i pamięta rozwinięte foldery także po zmianie nazwy. „Wszystkie strony” przeglądają foldery po kolei („Z podfolderami”).
- **Kopiowanie odnośnika do punktu, nagłówka i strony** — menu bloku → „Kopiuj odnośnik”: `((id))`, `[[Strona#^id]]`, `[[Strona#Nagłówek]]` albo osadzenie. Menu bloku otworzysz prawym przyciskiem myszy na każdym bloku (także akapicie i nagłówku), a na telefonie przyciskiem „Więcej” na pasku nad klawiaturą; `Ctrl+Shift+C` kopiuje `((id))`, są też polecenia w palecie. Kliknięty odnośnik przewija do celu i go podświetla. Zobacz [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]].
- **Zaznaczanie i kopiowanie grup bloków** — Shift+strzałki, przeciągnięcie myszą, Shift+klik i Ctrl+klik (pojedyncze punkty), a na telefonie przytrzymanie tekstu bloku: uchwyty, tryb „Pojedynczo” i pasek zaznaczenia. Kopiuj, Wytnij, Usuń, Kopiuj jako tekst, wcięcia i przenoszenie całych bloków z ich punktami; tekst z innych aplikacji (HTML) wkleja się jako Markdown. Zobacz [[Dokument i konspekt|Dokument i konspekt]].
- **„Zaznacz tekst”** — cała strona jako jeden tekst do zaznaczania znak po znaku przez akapity (na Androidzie systemowymi uchwytami); w menu strony, w **Więcej** i w palecie poleceń.
- **Pasek narzędzi zawsze przy klawiaturze** (telefon) — w dzienniku, na stronie, w komórkach tabeli i w trybie źródła; podąża za wysuwaną klawiaturą. Okna, menu i komunikaty nie chowają się pod klawiaturą ani nie zasłaniają pisanej linii.
- **Wiersz poleceń dla agentów AI** — `brulion-cli` czyta i zapisuje notatki, dziennik, zadania, przypomnienia z polskimi datami („jutro 9:00”), zapytania, tagi i odnośniki, przenosi strony; wyniki w JSON. `brulion-cli mcp` to serwer MCP dla agentów, a po włączeniu w ustawieniach (*Aplikacja → Dla programistów*) agent może też sterować otwartą aplikacją (`brulion-cli app …`). Zobacz [[Wiersz poleceń i agenci AI|Wiersz poleceń i agenci AI]].

**Zmiany**
- `#` podpowiada tagi i strony, a filtr `ref:` w zapytaniach obejmuje też tagi. Zobacz [[Zapytania|Zapytania]].
- Odnośniki strony pokazują bloki z jej tagiem; strona, której jeszcze nie ma, pokazuje, co już do niej prowadzi.
- Zmiana nazwy albo przeniesienie folderu dziennika, szablonów, załączników albo nowych stron aktualizuje ustawienia notatnika.
- Linia dopisana przez inny program (np. agenta albo synchronizację) obok miejsca, w którym piszesz, scala się bez konfliktu.
- Notatkę dziennika zmienisz albo przeniesiesz jak plik (w drzewie, na liście stron, `F2`); widok dziennika według daty tych poleceń nie ma.

**Poprawki**
- Edytor nie przeskakuje na górę strony, gdy nagłówek strony zmieni wysokość, np. po zwężeniu okna.
- Wyniki zapytań nie nachodzą na tekst pod nimi, znacznik „dziś” w kalendarzach przechodzi o północy, okno przypomnienia zmienia język razem z aplikacją, a wyniki obliczeń idą za zawijanym tekstem.
- Android: pierwsza pozycja wysokiego menu nie chowa się pod paskiem stanu, a słowo dopiero pisane na klawiaturze (Gboard) liczy się w oknach nowej strony, folderu, szybkiej notatki i zmiany nazwy.
- Usunięty albo przemianowany pusty folder od razu znika z drzewa; nazwy nowych folderów są oczyszczane jak nazwy stron, a nazwa, przez którą folder byłby ukryty, jest odrzucana.
- Pisanie nie gubi klawiatury, gdy plik zmieni się z zewnątrz.
- Odnośnik do bloku, w którym jest Markdown, pokazuje sam tekst bloku.

## 0.3.7 — 22 października 2026

**Nowe**
- **Lista zmian** — ta strona: wszystkie zmiany Brulionu od pierwszej wersji, od najnowszej, z uwagami, gdy trzeba coś zrobić samemu. Link do niej jest w [[Spis treści|Spisie treści]].

## 0.3.6 — 22 października 2026

**Uwaga**
- Wzory zapisane od 0.3.3 jako `\(…\)` albo `\[…\]` pokazują się teraz jako zwykły tekst w nawiasach — zamień je na `$…$` i `$$…$$`.
- Wywołanie funkcji z przecinkiem, który może być i dziesiętny, i rozdzielać argumenty (np. `round(2,5)`), daje teraz błąd — dopisz średnik albo użyj kropki.
- Jeśli dwa notatniki na tym urządzeniu synchronizują się z jednym folderem Nextcloud (mogły to ustawić wersje 0.3.1–0.3.5), żaden z nich się nie synchronizuje, także po „Synchronizuj teraz”. Stan synchronizacji na dole paska bocznego mówi, z którym notatnikiem dzielą folder; w centrum synchronizacji wybierz „Wybierz inny folder” i podaj nowy folder dla jednego z nich.
- Po „Wyloguj” włącz synchronizację Nextcloud w każdym notatniku osobno.
- Jeśli w 0.3.0 otwierałeś dokumentację przy włączonym Nextcloudzie, jej strony mogły trafić do Twojego notatnika. Znajdziesz je w widoku **Tagi** pod tagiem `dokumentacja` i możesz je bezpiecznie usunąć — zobacz [[Synchronizacja|Synchronizacja]].

**Nowe**
- **Menu statusu zadania** — kliknij pole wyboru zadania prawym przyciskiem myszy albo przytrzymaj je palcem i wybierz: Do zrobienia, W toku, Zrobione, Oczekujące, Anulowane albo Bez statusu (zadanie staje się wtedy zwykłym punktem). Działa wszędzie, gdzie widać pole zadania: w notatce i dzienniku, w wynikach zapytań, we wstawionych blokach, w odnośnikach zwrotnych, na stronach tagów, w kalendarzu i w widoku Zadania. Zwykłe kliknięcie nadal odhacza zadanie. Zobacz [[Zadania|Zadania]].
- **Ctrl+Shift+Enter** otwiera to samo menu w edytorze, a na telefonie — przytrzymanie przycisku **Zadanie** na pasku nad klawiaturą.
- **Status poleceniem `/`** — „Zadanie: do zrobienia”, „Zadanie: w toku”, „Zadanie: zrobione”, „Zadanie: oczekujące”, „Zadanie: anulowane” i „Zadanie: bez statusu”. Wystarczą też słowa takie jak `/czeka`, `/odłożone`, `/bez`, `/todo` czy `/done`.
- **Dokumentacja aktualizuje się razem z aplikacją** — dochodzą nowe strony, te, których nie zmieniałeś, dostają nową wersję (poprzednia zostaje w historii strony), a niezmienione strony, których nowa wersja już nie ma, trafiają do kosza. Strony, które zmieniłeś albo usunąłeś, zostają takie, jak je zostawiłeś. Dotąd dokumentacja założona w starszej wersji nie dostawała nowych stron ani poprawek — na przykład strony [[Wzory matematyczne|Wzory matematyczne]] z 0.3.3.
- **Linux: aktualizacja AppImage** — wydania mają pliki `.zsync`, więc AppImage (także z wcześniejszych wersji) zaktualizujesz przez AppImageUpdate, Gear Lever albo AppImageLauncher.

**Zmiany**
- **Kiedy dolar zaczyna wzór** — tuż przed otwierającym `$` i tuż po zamykającym nie może stać litera ani cyfra, a `$` po spacji niczego nie zamyka. Dzięki temu `Netflix 15$, Spotify 10$`, `10$/h` czy `$PATH:$HOME` zostają zwykłym tekstem i nie połykają linków ani przypomnień. Wzór w tekście nie przechodzi przez pustą linię, a samotny `$$` albo niezamknięty `$` nie ukrywa już dalszych wzorów. Zobacz [[Wzory matematyczne|Wzory matematyczne]].
- **Ukośnik i nawias to zwykły nawias** — `\(` i `\[` znów pokazują nawias, jak w Obsidianie i jak zapisują je importy z innych programów; w 0.3.3–0.3.5 nawiasy znikały, a tekst między nimi stawał się wzorem. Wzory to `$…$`, `$$…$$` i blok `math`.
- **Wzór między liniami `$$`** — gdy każdy `$$` stoi w osobnej linii, wszystkie linie między nimi należą do jednego wzoru, także te zaczynające się od `+`, `-`, `>` albo `1.` — nie rozpadają się na listę ani cytat, również w punkcie konspektu. Pusta linia przerywa taki wzór.
- **Co we wzorze, to wzór** — tag, link, termin zadania czy przypomnienie wpisane wewnątrz wzoru nie trafiają już do tagów, odnośników ani kalendarza, a takie przypomnienie nie dzwoni (edytor i tak pokazywał je jako część wzoru). Identyfikator bloku (`^id`, `id::`) i `collapsed::` działają zawsze.
- **Dolary w imporcie** — import tekstu z HTML, Evernote, Google Keep, baz Notion, Trilium i kalendarzy dopisuje ukośnik przed `$` tam, gdzie dwa dolary utworzyłyby wzór: `Cena $10, Netflix 15$` zapisze się jako `Cena \$10, Netflix 15$`. Dolary bez pary, kod, adresy i wartości właściwości zostają bez zmian. Zobacz [[Import i eksport|Import i eksport]].
- **Przecinek dziesiętny w funkcjach** — w wywołaniu bez średnika przecinek między cyframi jest dziesiętny, gdy tylko to odczytanie pasuje do funkcji: `sqrt(2,25)` daje 1,5 (dotąd błąd). Gdy pasują oba odczytania, jak w `round(2,5)` czy `zaokr(netto * 1,23)`, Brulion pokazuje błąd zamiast zgadywać (dotąd `round(2,5)` dawało po cichu 2). Zobacz [[Obliczenia|Obliczenia]].
- **Tekst obrazu tylko do odczytu** — „Pokaż jako tekst” pokazuje plik bez możliwości edycji (dotąd przypadkowo wpisany znak zapisywał się w miejsce obrazu), z dużego pliku tylko pierwsze 200 KB, i od razu widać w nim zmiany pliku na dysku, np. z synchronizacji. Ctrl+E przełącza między obrazem a tekstem w obie strony, a menu **Więcej** przy obrazie ma „Pokaż jako tekst” i „Pokaż obraz” zamiast zmiany trybu notatki.
- **Nextcloud: własny folder dla każdego notatnika** — Brulion proponuje folder na serwerze osobno dla każdego notatnika: przy pierwszym koncie „Notatki”, dla kolejnych notatników ich nazwę (np. „Praca”, a gdy jest zajęta — „Praca 2”). Folderu, z którym synchronizuje się już inny notatnik na tym urządzeniu (ani folderu w nim albo nad nim), nie zapiszesz — Brulion powie, który to notatnik. Dotąd każdy notatnik dostawał propozycję „Brulion”. Zobacz [[Synchronizacja|Synchronizacja]].
- **Nextcloud: „Wyloguj”** wyłącza synchronizację we wszystkich notatnikach na tym urządzeniu, bo hasło aplikacji jest wspólne, a zapisanie innego serwera albo użytkownika wyłącza ją w pozostałych notatnikach. Dotąd inne notatniki mogły same zacząć wysyłać notatki na nowe konto.

**Poprawki**
- **Bezpieczeństwo wzorów** — zsynchronizowana albo zaimportowana notatka nie może już przez wzór z `\left<` … `\right>` wstawić własnego HTML (zdalnego obrazka wczytywanego bez kliknięcia, skryptu w eksporcie HTML). Interfejs Brulionu w ogóle nie wczytuje niczego z sieci; synchronizacja działa jak dotąd.
- **Stabilność wzorów** — bardzo głęboko zagnieżdżony wzór nie wywraca już aplikacji (część głębsza niż 48 poziomów pokazuje się jako źródło), a notatka z setkami tysięcy niezamkniętych `\sqrt[` nie zamraża jej na wiele sekund.
- **Rysowanie wzorów** — poprawnie wychodzą `\left\{`, `\big\{` i `\left\|` (zbiory, funkcje określone przedziałami), `\bigcup`, `\bigcap` i podobne, `\limits`, `\nolimits` i `\displaystyle`, emoji albo `𝔸` w indeksie czy potędze, `\&` i nawiasy klamrowe w komórkach macierzy oraz macierz w macierzy. `\text{…}` pokazuje `\&`, `\%` i `\$` jako same znaki.
- **Eksport HTML** pokazuje jako wzór także blok `math`, który jest punktem konspektu albo dziennika (tak zapisuje go `/Wzór matematyczny` w konspekcie); inne bloki kodu w punktach listy wychodzą jako kod.
- **Import z Trilium** — wzór, który w akapicie odczytałby się inaczej niż w Trilium (np. przez `$` w tekście obok), zostaje kodem ze źródłem LaTeX, jak przed 0.3.3, a słowo z krzyżykiem (np. `#pilne`) w tekście obok dolarów nie staje się już przypadkiem tagiem.
- **Przeglądarka: emoji zamiast pustych kwadratów** — dzwonki przypomnień i alarmów (`⏰`, `🔔`), znaczniki zadań i inne emoji rysuje teraz własna czcionka zapasowa Brulionu (Noto Emoji i Noto Sans Math, licencja SIL OFL 1.1): jednokolorowo, w kolorze tekstu. Działa też dla dłoni z odcieniem skóry z klawiatury telefonu i dla symboli wśród tekstu pisanego cyrylicą czy po grecku.
- **Linux: emoji i symbole** — bez czcionki z emoji Brulion używa tych samych czcionek zapasowych, a symbol osadzenia bloku ⧉ i rzadkie symbole matematyczne rysuje nimi także obok kolorowej czcionki emoji.
- **Bloki kodu (błąd z 0.3.5)** — zmiana w bloku kodu, pod którym jest identyfikator `^id` (dopisywany przez „Kopiuj odwołanie do bloku” i `((`), właściwość `id::` albo dalszy tekst tego samego punktu, nie zamienia już po zapisie reszty strony (albo dzieci punktu) w kod. Linia `` ``` `` wpisana w niezamkniętym bloku kodu znów go zamyka.
- **Kod zaczęty w akapicie** — linia `` ``` `` wpisana w akapicie albo punkcie listy i wklejony kod bez zamknięcia dostają linię zamykającą, a sama linia otwierająca (np. `` ```js ``) zamyka się, gdy przejdziesz do innego bloku. Kod nie zabiera więc kolejnych bloków razem z ich zadaniami i przypomnieniami. Enter i wklejanie między tymi liniami działają jak w bloku kodu. Zobacz [[Dokument i konspekt|Dokument i konspekt]].
- Enter na końcu zadania, którego tekst zaczyna się od `` ```js ``, zaczyna kolejne zadanie zamiast wstawiać do niego blok kodu.
- Zadanie, którego tekst zaczyna się od kodu w linii, pokazuje się w widoku Zadania w całości, a import z Logseq nie gubi tego kodu.
- **Dokumentacja** — na Androidzie jej otwarcie nie wyłącza już alarmów i przypomnień Twojego notatnika. Po ponownym uruchomieniu otwiera się Twój notatnik, a nie dokumentacja; szybka notatka trafia do Twojego notatnika, a dotknięcie przypomnienia podczas czytania otwiera notatkę w Twoim notatniku. Dokumentacja znika też z listy ostatnich notatników. Zobacz [[Przypomnienia i alarmy|Przypomnienia i alarmy]].
- Powrót do notatnika, którego folder zniknął (usunięty, przeniesiony, na odłączonej karcie), przez Ctrl+Shift+B albo menu „Przełącz notatnik” nie tworzy już w jego miejscu pustego notatnika: Brulion ostrzega, że folder jest niedostępny, i usuwa go z listy ostatnich.
- **Nextcloud: ustawienia z 0.3.0** trafiają do notatnika, który był wtedy synchronizowany. Od 0.3.1 przejmował je pierwszy otwarty notatnik — także inny, np. gdy synchronizowany leżał na niepodłączonej karcie — i to on synchronizował się ze starym folderem. Notatnik, który tak stracił ustawienia, po ponownym włączeniu synchronizacji wraca do swojego folderu.
- **Nextcloud: jeden folder, jedna synchronizacja** — ten sam folder notatnika otwarty inną ścieżką (np. na Androidzie `/sdcard/…` i `/storage/emulated/0/…`) ma jedno ustawienie synchronizacji; dotąd włączenie jej przez drugą ścieżkę mogło połączyć notatnik z innym folderem na serwerze.
- **Obrazy** — pod otwartym obrazem widać notatki, które go osadzają albo do niego linkują; zakładka Linki w panelu bocznym znów je pokazuje (od 0.3.4 była przy obrazie pusta). Mały obraz nie jest rozciągany przy dopasowaniu do widoku. Formaty, których urządzenie nie odczyta (WebP i AVIF na Androidzie, w przeglądarce i w pakiecie dla NixOS), są nazwane zamiast błędu wczytywania — także w obrazie osadzonym w notatce. W wąskim widoku przycisk rozmiaru jest ikoną lupy, więc widać nazwę pliku.
- **Zegar** — przeciąganie wskazówki działa także wtedy, gdy zaczniesz od liczby (np. od zaznaczonej wartości), a dotknięcie środka tarczy nie przełącza już na minuty. Na telefonie trzymanym poziomo tarcza się zmniejsza, a pola godzin i minut stają obok niej, więc cały zegar widać bez przewijania. Obramowanie fokusu pokazuje się tylko przy obsłudze klawiaturą.
- Po wybraniu domyślnej godziny przypomnień na zegarze (*Ustawienia → Notatnik*) godzina zmieniona na innym urządzeniu od razu się pokazuje, a wyjście z pola nie zapisuje już starej wartości.
- **Tryb źródła na telefonie** — tekst przewijasz palcem, dotknięcie stawia kursor, a przytrzymanie zaznacza słowo; dotąd tekstu nie dało się przewinąć dalej niż pierwszy ekran.
- Nazwy folderów, notatników i stron w menu i w ścieżce nawigacji pokazują się dokładnie tak, jak są zapisane — nazwa wyglądająca jak znaczniki HTML (np. z synchronizacji albo importu) nie zmienia już wyglądu.
- **Przeglądarka** — strona powitalna przykładowego notatnika odsyła do stron dokumentacji w repozytorium projektu, a nie do notatnika dokumentacji, którego w przeglądarce nie ma.
- Wersja w przeglądarce i uruchamiana przez `nix run github:janusz-bit/brulion` pokazuje właściwy numer wersji (po wydaniu 0.3.5 pokazywała jeszcze 0.3.4).
- **Strony dokumentacji** poprawione tam, gdzie nie zgadzały się z aplikacją — m.in. domyślną godzinę przypomnień ustawiasz w *Ustawienia → Notatnik*, a Ctrl+Enter po „zrobione” wraca do braku statusu. Strona [[Przypomnienia i alarmy|Przypomnienia i alarmy]] wyjaśnia, który notatnik przypomina, a [[Spis treści|Spis treści]] — jak aktualizuje się dokumentacja.

## 0.3.5 — 21 października 2026

**Nowe**
- **Godzina na zegarze** — godzinę w oknie przypomnienia, w oknie dodawania w kalendarzu i domyślną godzinę przypomnień w ustawieniach wybierasz na okrągłym zegarze, jak w Androidzie: godziny 00–11 na zewnętrznym pierścieniu, 12–23 na wewnętrznym, minuty opisane co pięć, a dokładną minutę ustawisz dotknięciem albo przeciągnięciem po tarczy. Zegar obsługuje też klawiaturę, a godzinę nadal możesz wpisać ręcznie.

**Zmiany**
- **Bloki kodu** — linia `` ``` `` wpisana albo wklejona w bloku kodu zostaje jego treścią: Brulion sam wydłuża ukryte ogrodzenie bloku. Enter otwiera blok kodu także po linii z dłuższym ogrodzeniem i z dowolną nazwą języka, a osadzony blok kodu pokazuje sam kod, bez linii ogrodzenia.

## 0.3.4 — 20 października 2026

**Nowe**
- **Obrazy jako zdjęcia** — kliknięcie osadzonego obrazu albo linku do pliku obrazu pokazuje samo zdjęcie, dopasowane do widoku. Kliknięcie na nim (albo przycisk „Pokaż naturalny rozmiar”) pokazuje naturalną wielkość, którą przesuwasz, przeciągając; „Dopasuj do widoku” wraca.

**Zmiany**
- Plik obrazu nie jest już wczytywany do edytora jako tekst — wychodziły z tego nieczytelne znaki. Ten starszy widok otwiera „Pokaż jako tekst” w pasku obrazu albo Ctrl+E, a „Pokaż obraz” w menu strony wraca do zdjęcia.

## 0.3.3 — 20 października 2026

**Nowe**
- **Wzory matematyczne** — LaTeX w notatce Brulion od razu rysuje jako czytelny wzór, bez internetu i bez uruchamiania kodu: w tekście między `$…$`, w osobnej linii między `$$…$$` albo w bloku `math` (także `latex` i `tex`), który edytujesz w miejscu i wstawiasz poleceniem `/Wzór matematyczny`. Brulion rozumie greckie litery, indeksy i potęgi, ułamki, pierwiastki, macierze, układy równań (`cases`) i wyrównanie (`aligned`); `\$` to sam znak dolara. Plik zachowuje źródło LaTeX, a eksport HTML pokazuje gotowe wzory. W 0.3.3–0.3.5 wzorami były też `\(…\)` i `\[…\]` — od 0.3.6 to znów zwykłe nawiasy.
- **Import z Trilium** przenosi wzory jako wzory, a nie jako kod.
- **Dokumentacja** — nowa strona [[Wzory matematyczne|Wzory matematyczne]] z żywymi przykładami. Do dokumentacji założonej we wcześniejszej wersji trafiła dopiero w 0.3.6.

## 0.3.2 — 20 października 2026

**Poprawki**
- Menu „Przełącz notatnik” otwiera wybrany notatnik albo dokumentację pierwszym kliknięciem — dotąd pierwsze kliknięcie nic nie robiło.

## 0.3.1 — 19 października 2026

**Zmiany**
- **Nextcloud w każdym notatniku osobno** — włączenie synchronizacji i folder na serwerze należą teraz do notatnika, a serwer i konto zostają wspólne. Inny notatnik, także dokumentacja, startuje bez synchronizacji. Dotychczasowe ustawienie przejmuje pierwszy notatnik otwarty po aktualizacji (poza dokumentacją i przykładowym notatnikiem).

**Poprawki**
- Notatki nie przechodzą już między notatnikami. W 0.3.0 synchronizacja Nextcloud była wspólna dla wszystkich notatników, więc po przełączeniu notatnika (np. do dokumentacji) ten sam folder na serwerze synchronizował się z drugim notatnikiem i notatki jednego trafiały do drugiego.

## 0.3.0 — 19 października 2026

**Nowe**
- **Dokumentacja napisana w Brulionie** — osobny notatnik „Dokumentacja Brulionu”: 14 stron z żywymi przykładami (tryby, linki, tagi, zadania, przypomnienia, obliczenia, zapytania, dziennik i szablony, synchronizacja, import i eksport, skróty, format plików). Powstaje przy pierwszym otwarciu i możesz go dowolnie edytować. Zacznij od [[Spis treści|Spisu treści]]. W przeglądarce, która ma jeden notatnik, dokumentacji nie ma.
- **Szybkie przełączanie notatników** — przycisk „Przełącz notatnik” obok nazwy notatnika w pasku bocznym otwiera menu z ostatnimi notatnikami, dokumentacją i pozycją „Otwórz inny notatnik…”.
- **Ctrl+Shift+B** przenosi do dokumentacji i z powrotem do poprzedniego notatnika; to samo polecenie znajdziesz w palecie poleceń (Ctrl+K).

## 0.2.1 — 19 października 2026

**Nowe**
- **Rejestr zdarzeń** pokazuje, co robiły aplikacja i synchronizacja, także podczas poprzednich uruchomień. Otwierasz go z *Ustawienia → Aplikacja → Rejestr zdarzeń*, z *Ustawienia → Synchronizacja*, z centrum synchronizacji albo wpisując „rejestr” w palecie poleceń (Ctrl+K). Wpisy możesz filtrować (np. Synchronizacja, Sieć, Notatnik, same ostrzeżenia i błędy) i przeszukiwać, także bez polskich znaków.
- **Rejestr łatwo wysłać**, gdy coś nie działa. **Kopiuj** działa wszędzie, na Androidzie są też **Udostępnij** i **Zapisz w Pobranych**, na komputerze **Zapisz jako…** i **Otwórz folder**, a w przeglądarce **Pobierz**. Hasła, hasła aplikacji, tokeny i ciasteczka Brulion usuwa, zanim cokolwiek trafi do rejestru; treści notatek rejestr nie zawiera (nazwy notatek i adres serwera mogą się w nim pojawić). Rejestr zostaje na urządzeniu, dopóki sam go nie udostępnisz, a w przeglądarce obejmuje tylko bieżącą sesję.
- **Szczegółowy rejestr** (przełącznik w *Ustawienia → Aplikacja*) zapisuje też każde żądanie do serwera. Gdy synchronizacja sprawia kłopot, włącz go, powtórz synchronizację i dopiero wtedy wyślij rejestr.
- **Postęp synchronizacji na żywo** w centrum synchronizacji: etap (np. „Sprawdzanie plików na serwerze… folder 12 z ok. 40”), przesyłane pliki i czas trwania, a przycisk **Zatrzymaj** przerywa synchronizację.
- **Historia synchronizacji** — ostatnie 20 przebiegów na tym urządzeniu: kiedy i dlaczego się zaczęły, ile trwały, co przesłały i jakie były błędy (opisane po polsku). **Szczegóły** pokazują, na co poszedł czas, ile było zapytań do serwera i których plików nie udało się zsynchronizować. Przebiegi, które czekają na Ciebie (konflikt do scalenia, wstrzymane masowe usuwanie), są wyróżnione.

**Zmiany**
- NixOS: instalacja z flake’a (`nix build`, `nix run`, `programs.brulion`) nie kompiluje już i nie uruchamia testów, więc trwa krócej.

**Poprawki**
- **Synchronizacja nie trwa bez końca** — gdy serwer Nextcloud przestaje odpowiadać, po ok. 20 s pojawia się ostrzeżenie, a po 3 minutach bez postępu przebieg kończy się błędem z podanym powodem. Brulion sam ponawia synchronizację: widzisz, kiedy będzie kolejna próba, i przycisk **Spróbuj teraz**.

## 0.2.0 — 18 października 2026

**Nowe**
- **Import z Trilium Notes** — eksport ZIP („Ta notatka i wszystkie jej elementy podrzędne”, w formacie HTML albo Markdown) z Trilium 0.10x, TriliumNext 0.9x i zadam/trilium 0.6x. Drzewo notatek staje się stronami i folderami, notatki dnia trafiają do dziennika, etykiety stają się tagami i właściwościami, relacje linkami, a obrazy i pliki załącznikami. Zobacz [[Import i eksport|Import i eksport]].
- Dla programistów: APK na Androida zbudujesz przez Nix: `nix build github:janusz-bit/brulion/v0.2.0#brulion-android`.

**Zmiany**
- **Szybszy start** — przy notatniku z 10 000 notatek okno pojawia się na komputerze po ok. 0,2 s zamiast 0,45 s, a na słabszym urządzeniu po 0,5 s zamiast 2,3 s; treść dziennika też pojawia się wcześniej. W przeglądarkach opartych na Chromium ponowne otwarcie Brulionu jest o mniej więcej jedną czwartą szybsze, bo przeglądarka zapamiętuje skompilowany kod aplikacji.
- **Duże notatki nie zamrażają aplikacji** — po starcie i po każdym autozapisie bardzo dużej notatki (od ok. 200 KB) Brulion przestawał reagować: na komputerze na 0,9–1,6 s, na słabszym urządzeniu nawet na kilkanaście sekund. Teraz to najwyżej ułamek sekundy. Wyszukiwanie w dużych notatkach też jest szybsze.
- **Szybsze widoki i pisanie** — widok Zadania otwiera się dwa razy szybciej, Kalendarz i Agenda o 10–30 % szybciej, a graf na słabszym urządzeniu układa się w 2,6 s zamiast ponad 40 s. Na bardzo długich stronach (tysiące bloków) znaki pojawiają się na słabszym urządzeniu mniej więcej dwa razy szybciej. Podpowiedzi bloków po `((` i stron po `[[` też są szybsze.
- **Szybszy import długich list z HTML** (np. z Evernote albo Notion) — 20 000 punktów wczytuje się w 0,2 s zamiast 13 s.
- **Mniejsze pliki do pobrania** — APK na Androida jest mniejszy (87,1 → 84,9 MB), choć teraz zawiera OpenSSL, a plik aplikacji w przeglądarce — o ponad pół megabajta.
- **Zapytania na ekranie dotykowym** — blok zapytania pokazuje najpierw 20 wyników zamiast 50 (przy pracy myszą nadal 50); resztę dodaje przycisk **Pokaż jeszcze**.
- **Tabele** — przy pracy myszą przyciski **Wiersz** i **Kolumna** pod tabelą pojawiają się po zaznaczeniu tabeli albo w trakcie edycji komórki, a nie po samym najechaniu kursorem, więc tabela nie rośnie pod kursorem podczas przewijania. Na ekranie dotykowym przyciski są widoczne zawsze.

**Poprawki**
- **Android: HTTPS i Nextcloud działają** — w aplikacji brakowało OpenSSL, więc każde połączenie HTTPS kończyło się błędem „TLS initialization failed”, a test połączenia zgłaszał niezaufany certyfikat nawet przy poprawnym serwerze. Teraz APK zawiera OpenSSL 3, a Brulion ufa też certyfikatom CA zainstalowanym przez Ciebie w telefonie, więc własny serwer Nextcloud z własnym CA działa jak na komputerze.
- **Przewijanie bez skoków** — długi dziennik, duże strony i widok Zadania nie szarpią, a treść nie przeskakuje (wcześniej nawet o ponad 1500 px). Na Androidzie przewijanie rzutem palca startuje z prędkością palca (wcześniej ok. 0,7 tej prędkości), a złapanie trwającego rzutu nie szarpie.
- **Android: wpisane daty i godziny są przyjmowane** — data wpisana na klawiaturze dotykowej w oknie przypomnienia albo daty przepadała, a godzina liczyła się tylko wtedy, gdy stuknięcie gdzie indziej przeniosło fokus. Teraz wpis zatwierdzają Enter, wyjście z pola, schowanie klawiatury i przycisk **Zapisz**; tak samo działają okna terminu i powtarzania. Klawisz akcji na klawiaturze to teraz „Gotowe” zamiast „Dalej”.
- **Android: eksport podpowiada nazwę pliku** — systemowe okno zapisu otwierało się z pustą nazwą, a teraz proponuje np. `Brulion-2026-10-18.zip` w folderze Pobrane.
- **Przeglądarka: indeksowanie nie staje w połowie** — indeks notatek zatrzymywał się, dopóki nie poruszyłeś myszą; teraz kończy się sam.
- **Import: bezpieczne nazwy stron** (we wszystkich formatach) — nazwy zastrzeżone w Windows (`CON`, `NUL`…) dostają `_`, więc taki notatnik zsynchronizuje się także z Windows, a niewidoczne znaki zmieniające kierunek tekstu znikają z nazw plików.

## 0.1.1 — 16 października 2026

**Nowe**
- **Dźwięk przypomnień i alarmów** — w *Ustawienia → Przypomnienia* wybierasz osobno **Dźwięk przypomnień** i **Dźwięk alarmów**: Domyślny, Dzwonek albo Cisza; **Odtwórz próbkę** pozwala go posłuchać. To ustawienie dotyczy tylko tego urządzenia.
- **Dźwięk na Androidzie** — **Wybierz dzwonek…** otwiera systemową listę dzwonków (także tych, które dodałeś sam); jest też tryb Wibracja i przycisk **Ustawienia powiadomień systemowych**.
- **Dźwięk na Linuksie** — dźwięk z motywu dźwiękowego pulpitu albo własny plik `.oga`, `.ogg` lub `.wav`; odtwarza go usługa powiadomień pulpitu (np. w GNOME albo KDE Plasma).
- **Dźwięk w przeglądarce** — do wyboru Domyślny albo Cisza; alarm z dźwiękiem domyślnym pika przez kilka sekund, gdy karta Brulionu jest otwarta.
- **Import notatek łatwiej znaleźć** — **Importuj notatki** jest w menu **Więcej** (⋮) i w szufladzie na telefonie; nowy notatnik ma przycisk importu pod pustym dziennikiem, a ekran powitalny — kartę „Notatki w innej aplikacji?”.
- **Android: import z telefonu** — **Wybierz pliki…** (przytrzymanie palca zaznacza kilka plików), **Wybierz folder…** (np. cały skarbiec Obsidiana albo graf Logseq) albo wpisana ścieżka. Format Brulion rozpoznaje sam, po zawartości plików, a duży folder ani pliki z Dysku Google nie zawieszają aplikacji.

**Zmiany**
- Pusty folder w drzewie stron na pasku bocznym po rozwinięciu pokazuje napis „Pusty folder”.

**Poprawki**
- **Android: stuknięcie robi to, co powinno** — stuknięcie palcem w pozycję szuflady, listy Wszystkie strony, ulubionych lub widoku Zadania albo w punkt bloku otwierało menu kontekstowe, więc strona, folder ani zadanie się nie otwierały. Teraz menu kontekstowe otwiera przytrzymanie palca (albo prawy przycisk myszy).

## 0.1.0 — 15 października 2026

Pierwsze wydanie Brulionu — na Androida, Linuksa i do przeglądarki. Od początku umie:

- **Dwa tryby notatki** — konspekt z blokami, wcięciami, zwijaniem i przybliżaniem (jak w Logseq) albo dokument z ciągłym tekstem i podglądem na żywo (jak w Obsidianie). Tryb wybierasz przy tworzeniu strony i zmieniasz w każdej chwili — zobacz [[Dokument i konspekt|Dokument i konspekt]].
- **Dziennik** ze stroną na każdy dzień i szablony — zobacz [[Dziennik, kalendarz i szablony|Dziennik, kalendarz i szablony]].
- **Linki** między stronami, odwołania do bloków, odnośniki zwrotne i graf powiązań, a do tego tagi i właściwości — zobacz [[Linki i odnośniki zwrotne|Linki i odnośniki zwrotne]] oraz [[Tagi i właściwości|Tagi i właściwości]].
- **Wyszukiwanie** w treści wszystkich notatek (także bez polskich znaków) i szybkie przechodzenie do stron.
- **Zadania** zgodne z wtyczką Obsidian Tasks: terminy, priorytety, powtarzanie, widok Zadania i kalendarz (miesiąc, tydzień, agenda) — zobacz [[Zadania|Zadania]].
- **Przypomnienia i alarmy** (`⏰` i `⏰!`) — na Androidzie działają także przy zamkniętej aplikacji i po restarcie telefonu. Daty możesz wpisywać słowami, po polsku albo po angielsku. Zobacz [[Przypomnienia i alarmy|Przypomnienia i alarmy]].
- **Obliczenia** w blokach `calc` (zmienne, procenty, jednostki, waluty), tabele z formułami jak w arkuszu i rachunki w tekście — zobacz [[Obliczenia|Obliczenia]].
- **Zapytania** — blok `query` na bieżąco zbiera zadania i bloki z całego notatnika; zobacz [[Zapytania|Zapytania]].
- **Synchronizacja** przez Syncthing (zwykły folder, scalanie kopii konfliktów) albo Nextcloud/WebDAV (w tle, scalanie trójstronne, ochrona przed masowym usuwaniem) — zobacz [[Synchronizacja|Synchronizacja]].
- **Historia wersji i kosz** — poprzednie wersje notatki porównasz i przywrócisz, a usunięte strony odzyskasz.
- **Import** z Obsidiana, Logseq, Notion, Evernote, Google Keep, Joplin, Simplenote, Roam, Bear, plików Markdown i TXT, HTML i iCalendar oraz **eksport** do 9 formatów: ZIP z Markdownem, układ Obsidiana, układ Logseq, witryna HTML, jeden plik HTML, JSON, iCalendar, OPML i CSV zadań — zobacz [[Import i eksport|Import i eksport]].
- Paleta poleceń (Ctrl+K), szybka notatka do dziennika, pasek narzędzi nad klawiaturą na telefonie i „Przykładowy brulion” z notatkami do wypróbowania.
- Interfejs po polsku (angielski do wyboru), jasny i ciemny motyw.
- **Platformy** — Android 9 lub nowszy (APK arm64), Linux (AppImage x86_64 i aarch64, pakiet dla NixOS) i przeglądarka, także zainstalowana jako aplikacja i bez internetu.
