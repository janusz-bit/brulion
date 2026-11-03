# Do zrobienia

Lista pomysłów i znanych braków, podzielona na kategorie. Kolejność niczego nie przesądza. Punkt,
który zostanie zrobiony w repozytorium, po prostu znika z listy.

## Funkcje

* **Wsparcie dla AI** — zakres do ustalenia (dziś agenci AI korzystają z `brulion-cli`, serwera
  MCP i sterowania aplikacją: docs/CLI.md). U konkurencji: asystent w edytorze z własnym kluczem
  API (AFFiNE), asystent pisania (SiYuan); do rozważenia też pytania do notatek i wyszukiwanie
  semantyczne na lokalnym modelu.
* **Rozszerzalność jak w Emacsie i TiddlyWiki** — użytkownik zmienia i rozszerza Brulion z poziomu
  notatnika (własne polecenia, widoki, makra, zachowania); zakres i bezpieczeństwo do ustalenia
  (notatki przychodzą z synchronizacji i importu).
* **Ustawienia notatnika w notatkach** — ustawienia notatnika zapisane jako notatka (strona
  w notatniku, edytowalna jak każda inna i synchronizowana razem z nim) zamiast
  `.brulion/config.json`; do ustalenia format, przeniesienie istniejących ustawień i co zostaje
  ustawieniem urządzenia.
* **Zdjęcia z internetu — sprawdzić na prawdziwym telefonie.** Emulator Androida
  (`scripts/android-emulator-test.sh --web-pictures`) i przeglądarki Chromium i Firefox
  (`tests/web`, GitHub Actions) sprawdzają to automatycznie; zostaje prawdziwy telefon
  (docs/ANDROID.md „Obrazy z internetu”): przesuwanie powiększonego zdjęcia palcem i gest Wstecz
  z krawędzi, wklejenie adresu skopiowanego w Chrome przez „Wklej” z menu tekstu, eksport
  z „Dołącz kopie”.
* **Rysunki — dalsze pomysły i sprawdzenie na urządzeniach.** Edytor rysunków (`/Rysunek`,
  docs/UI.md „Drawing editor”) ma pismo odręczne, zakreślacz, gumkę, papier, kształty, strzałki,
  tekst, zaznaczanie lassem i prostokątem, uchwyty rozmiaru i obrotu, styl zaznaczenia, odbicia, grupy,
  podkłady (obraz, slajd, strona PDF) z przechodzeniem do sąsiednich stron PDF, ciemne tło i własny
  kolor tła, wyrównywanie i rozkładanie, napisy w wielokątach, schowek zgodny z excalidraw.com,
  obrazy z „Embedded Files” rysunków z Obsidiana i ich eksport jako obrazów. Zostaje:
  * sprawdzić ręcznie „Scal rysunki” na prawdziwych urządzeniach: rysunek zmieniony bez sieci na
    telefonie i na laptopie, Syncthing zostawia kopię `*.sync-conflict-….svg` (albo
    `*.excalidraw.sync-conflict-….md` rysunku z Obsidiana) → „Scal rysunki” w centrum
    synchronizacji na jednym z nich; to samo z prawdziwym Nextcloudem (rysunek zmieniony na dwóch
    urządzeniach scala się sam, bez kopii). Kopie od prawdziwego Syncthinga (dwie instancje na
    jednym komputerze) scala `tests/app/io/syncthing-conflict.sh`, Nextcloud sprawdzono tylko na
    udawanym serwerze WebDAV (`tst_sync_devices`);
  * na prawdziwym tablecie z rysikiem (Android, Natalia — w aplikacji i w przeglądarce) i
    telefonie: czy linia grubieje przy mocniejszym nacisku, czy drugi koniec rysika i przycisk
    boczny ścierają, zaznaczanie lassem rysikiem, polskie znaki z klawiatury ekranowej w tekście na
    rysunku; wstawić strony PDF-a z wykładu („/Rysunek na obrazie lub PDF”; PDF wskazany w
    systemowym oknie wyboru z folderu notatnika ma zostać na miejscu, bez kopii w załącznikach),
    pisać rysikiem po slajdzie, sprawdzić ciemny podkład i czy obrócona strona PDF wygląda dobrze
    (automatycznie sprawdzone: Chromium i Firefox — `tests/web`, emulator Androida — `--stylus`,
    `--drawing`, strona PDF przez PdfRenderer — samotest mostka, zgodność z excalidraw.com w obie
    strony — `tests/app/drawing/excalidraw-roundtrip.sh`, wymaga internetu);
  * sprawdzić ręcznie uchwyty rozmiaru i obrotu oraz panel „Styl” rysikiem i palcem na prawdziwym
    tablecie i telefonie;
  * sprawdzić ręcznie w prawdziwym excalidraw.com (wymaga internetu): wklejenie elementów
    skopiowanych w Brulionie (`Ctrl+C`) i odwrotnie, oraz napis w trójkącie czy gwieździe zapisany
    przez Brulion (tekst zgrupowany z kształtem) — czy stoi w tym samym miejscu i przesuwa się razem
    z kształtem; w Obsidianie: obraz z „Embedded Files” po edycji rysunku w Brulionie dalej się
    wczytuje;
  * sprawdzić ręcznie rysunki z Obsidiana na prawdziwym skarbcu (testy opierają się na plikach
    napisanych według kodu wtyczki; 22 pliki z publicznych skarbców Brulion czyta i zapisuje
    poprawnie, ale nie otwierano ich potem w Obsidianie): otworzyć `*.excalidraw.md` w Brulionie,
    dorysować coś i przesunąć tekst z linkiem `[[…]]`, otworzyć go w Obsidianie (rysunek się
    wczytuje, link w tekście działa, notatki z tyłu nietknięte); zetrzeć kreskę w Brulionie, gdy
    Obsidian ma ten rysunek otwarty (powinna zniknąć i tam); otworzyć w Obsidianie rysunek na
    slajdzie (podkład widać pod pismem).
* **Udostępnianie poza Androidem — sprawdzić na prawdziwych urządzeniach.** Automatycznie sprawdzone
  (testy, CI): odczyt udostępnienia, okno, przekazanie do działającej kopii, wpis `.desktop`,
  rejestr Windows (instalacja, aktualizacja, odinstalowanie), wersja webowa w Chromium bez okna
  (`wasm-smoke.mjs --share`). Zostało ręcznie: Chrome lub Edge na Androidzie (wersja webowa
  zainstalowana jako aplikacja → Udostępnij → Brulion ze strony i z zaznaczonego tekstu, przy
  zamkniętym i otwartym Brulionie); Eksplorator Windows („Otwórz za pomocą → Brulion” na `.md` i
  `.txt`, kilka plików naraz, okno na wierzchu, program domyślny bez zmian); menedżer plików na
  Linuksie (GNOME Pliki, Dolphin: „Otwórz za pomocą → Inny program”, okno wychodzi na wierzch pod
  Waylandem, dwuklik na `.md` otwiera dalej dotychczasowy program; Cinnamon/Xfce pod X11: kursor nie
  kręci się po otwarciu pliku w działającym Brulionie); AppImage zintegrowany przez Gear Lever albo
  AppImageLauncher.
* **Przeglądarka PDF — sprawdzić na urządzeniach i dalsze pomysły.** Miniatury PDF-ów w notatkach
  i przeglądarka PDF (docs/UI.md „PDF viewer”) są sprawdzone automatycznie: testy z Qt PDF i
  popplerem, samotest powłoki (`tst_shell_qml`), emulator Androida (`--pdf`: szczypanie, szukanie
  na API 36, brak szukania na API 30, „Otwórz w innej aplikacji”, „Rysuj na tej stronie”,
  pamięć), Chromium i Firefox (`tests/web/pdf.spec.mjs`: nowa karta; Firefox Playwrighta ma
  wyłączone pdf.js, więc tam sprawdzane jest pobranie). Zostaje:
  * ręcznie na prawdziwym telefonie i tablecie: płynność szczypania i przewijania, gest Wstecz
    z krawędzi, szukanie na Androidzie 15+, „Otwórz w innej aplikacji” z prawdziwymi aplikacjami
    do PDF-ów (Android, domyślna aplikacja Windows, GNOME i KDE), Safari na iOS (nowa karta albo
    pobranie), zwykły Firefox z pdf.js (nowa karta), duże skany z wykładów (100+ MB) — pamięć na
    telefonie (docs/PERFORMANCE.md, L19);
  * ręcznie na prawdziwym telefonie, tablecie i komputerze: zaznaczanie tekstu (przeciąganie myszą,
    przytrzymanie palcem i przesuwanie), spis treści, linki, wpisywanie hasła, dwie strony obok
    siebie, adnotacje z prawdziwych PDF-ów (zakreślenia, notatki), „Otwórz w innej aplikacji” dla
    DOCX / XLSX / MP3 z prawdziwymi aplikacjami;
  * na Androidzie 15+ (API 35) podłączyć do PdfRenderer zaznaczanie tekstu (`selectContent`), linki
    (`getLinkContents`, `getGotoLinks`) i otwieranie PDF-ów z hasłem (`LoadParams.setPassword`) —
    wymaga sprawdzenia na emulatorze lub telefonie; spisu treści PdfRenderer nie podaje wcale;
  * spis treści PDF-a w wersjach z Qt PDF (Windows, AppImage): `QPdfBookmarkModel` nie broni się
    przed zapętlonym spisem (rekurencja do przepełnienia stosu — `tst_pdfpages` `loopingOutline`),
    więc dziś spis treści jest tylko z popplerem; potrzebny własny, bezpieczny odczyt zakładek;
  * tworzenie własnych adnotacji (zakreślenia, notatki) zapisywanych w PDF-ie — dziś istniejące są
    tylko pokazywane, nowe robi się w innej aplikacji.
* **Sprawdzanie pisowni — sprawdzić na urządzeniach i dalsze pomysły.** Podkreślanie błędów
  w edytowanym bloku, podpowiedzi, słownik użytkownika i „Ignoruj” (docs/UI.md „Spell checking”)
  są sprawdzone automatycznie na komputerze (Hunspell, testy i samotest edytora). Zostaje:
  * na prawdziwym telefonie z Androidem: systemowe sprawdzanie pisowni (Gboard, polski
    i angielski) — podkreślenia, dotknięcie słowa i podpowiedzi, wyłączone sprawdzanie w ustawieniach
    Androida (ustawienia pisowni Brulionu znikają); kod Kotlin/JNI (`SpellCheck.kt`) nie był
    uruchomiony na urządzeniu;
  * AppImage i Windows: zbudować w CI i sprawdzić, że słowniki z pakietu się wczytują (Windows:
    folder programu z polskimi znakami w ścieżce);
  * słownik użytkownika synchronizowany między urządzeniami (dziś ustawienie urządzenia — Nextcloud
    przenosi z `.brulion/` tylko `config.json`, a scalanie po kluczach gubiłoby słowa);
  * podkreślenia także poza edytowanym blokiem i w trybie źródła Markdown.
* **Statystyki i tryb skupienia** — liczba słów i znaków, czas czytania; widok bez pasków bocznych,
  kursor trzymany na środku ekranu.
* **Własne skróty klawiszowe** — zmiana skrótów w ustawieniach, opcjonalnie tryb Vim (Obsidian).
* **Wygląd stron** — ikona strony, okładka; kolorowe zakreślenia (Obsidian) obok dzisiejszego
  `==…==`.

## Widoki i dane

* **Widoki baz danych z właściwości** — blok albo plik pokazujący strony jako edytowalną tabelę,
  tablicę kanban (także zadania według statusu), galerię, kalendarz; filtry, grupy, kolumny,
  formuły. Obsidian ma Bases (tabela, lista, mapa, kanban), Logseq tabelę i galerię, Anytype i
  AFFiNE tablice i galerie. Bloki `query` pokazują dziś tylko listę i nie filtrują po właściwościach
  stron (`status:: w toku`, `koszt > 100`).
* **Zgodność z Obsidianem: `.base`, `.canvas`, Dataview** — pliki `.base` i `.canvas` ze wspólnego
  skarbca oraz bloki `dataview` są dziś w Brulionie plikiem albo zwykłym tekstem
  (docs/IMPORT.md); do ustalenia, co czytać, a co też edytować.
* **Tablica (nieskończone płótno)** — notatki, obrazy, PDF-y i rysunki rozłożone na płótnie, z
  połączeniami; najlepiej w formacie JSON Canvas (Obsidian Canvas, AFFiNE). Edytor rysunków
  pokrywa szkice, ale nie osadza w nich notatek.
* **Wykresy** — wykres z tabeli z formułami, bloku `calc` albo wyników zapytania.
* **Sortowanie i filtrowanie tabel Markdown** — z nagłówka kolumny, bez ręcznego przestawiania
  wierszy; import i eksport CSV pojedynczej tabeli.
* **Fiszki z powtórkami** — karty z bloków (`pytanie :: odpowiedź`, ukrywane fragmenty), powtórki
  w odstępach (np. FSRS), karty do powtórki na dziś, stan zapisany w plikach i scalany przez
  synchronizację (Logseq, SiYuan).

## Zadania i kalendarz

* **Kalendarze CalDAV** — dwukierunkowa synchronizacja zadań i wydarzeń (Nextcloud, Google,
  Thunderbird, DAVx⁵) i subskrypcja kalendarzy ICS tylko do podglądu w widoku kalendarza. Dziś
  jest jednorazowy import i eksport iCalendar.
* **Zależności zadań i postęp podzadań** — składnia Obsidian Tasks `🆔` / `⛔` jest rozpoznawana,
  ale nic nie robi: zadania zablokowane w widokach i zapytaniach (`is:blocked`), licznik „3/5”
  przy zadaniu nadrzędnym.
* **Śledzenie czasu i historia statusów** — licznik czasu przy zadaniu, podsumowania; zapis zmian
  statusu (Logseq; wpisy `LOGBOOK` z Logseq są dziś tylko rozpoznawane).
* **Nawyki** — powtarzalne punkty dnia z serią i widokiem miesiąca, zapisane w dzienniku.

## Przechwytywanie

* **Web Clipper** — rozszerzenie Chrome i Firefoksa zapisujące artykuł, zaznaczenie albo zrzut
  jako Markdown z obrazami, z wyborem miejsca i szablonem (Obsidian, Joplin). Dziś: „Udostępnij”
  z tytułem i adresem, bez treści artykułu.
* **Notatki głosowe** — nagranie w aplikacji jako załącznik i transkrypcja na urządzeniu, także
  po polsku, bez wysyłania dźwięku do chmury (Joplin, Obsidian dla iOS w przygotowaniu).
* **Dźwięk i wideo w notatkach** — odtwarzacz dla `![[nagranie.mp3]]` i `![[film.mp4]]`; dziś takie
  pliki otwierają się tylko do odczytu jako tekst.
* **Zdjęcie i skan z aparatu** — `/Zdjęcie` na telefonie, prostowanie i przycinanie dokumentu,
  skan do PDF-a.
* **Automatyzacja na urządzeniu** — adresy `brulion://` (otwórz stronę, dopisz pod nagłówkiem),
  intencje dla Taskera na Androidzie, kafelek szybkich ustawień (Obsidian: URI, Siri i Skróty).
* **Więcej źródeł importu** — Apple Notes, OneNote (Obsidian Importer), Todoist, Airtable i CSV
  jako strony z właściwościami (Obsidian).

## Wyszukiwanie

* **Szukanie w załącznikach i OCR** — tekst PDF-ów i obrazów w wyszukiwarce całego notatnika, w
  tle i bez sieci, z przejściem do trafienia na stronie (Joplin, SiYuan). Dziś PDF przeszukuje się
  tylko po jego otwarciu.

## Prywatność i bezpieczeństwo

* **Szyfrowanie end-to-end synchronizacji** — pliki na serwerze Nextcloud lub WebDAV nieczytelne
  bez hasła użytkownika (Joplin, SiYuan, Logseq, Anytype, Obsidian Sync); do ustalenia zgodność z
  założeniem „zwykłe pliki”, klucz na urządzeniach, odzyskiwanie i scalanie konfliktów.
* **Blokada aplikacji** — PIN albo biometria na telefonie, ukrycie treści na ekranie ostatnich
  aplikacji i w widżetach.
* **Zaszyfrowane notatki** — pojedyncze strony albo fragmenty pod hasłem, zgodne z synchronizacją
  (Trilium: notatki chronione).
* **Kopie zapasowe: dalsze kroki** — przywracanie ZIP-a spoza listy i plików ponad 1 GB (Vault
  trzyma zapisywany plik w pamięci), folder Androida wskazany przez dokument w chmurze, w przeglądarce
  dzielenie kompresji dużego pliku na kawałki (dziś plik ponad 4 MB jest zapisywany bez kompresji).

## Udostępnianie i współpraca

* **Publikowanie** — strona lub zaznaczone notatki pod adresem, opcjonalnie z hasłem, jednym
  poleceniem: GitHub Pages, własny serwer albo publiczny link Nextclouda (Obsidian Publish, Logseq
  Publish, Joplin). Dziś jest eksport statycznego HTML, który publikuje się ręcznie.
* **Współdzielone notatniki** — notatnik wspólny z inną osobą: kto co zmienił, komentarze; do
  ustalenia, czy wystarczy Nextcloud, czy edycja na żywo (Logseq RTC, AFFiNE, Obsidian:
  „Multiplayer” w planach).
* **Drukowanie i bezpośredni PDF/DOCX** — druk i eksport strony do PDF-a oraz DOCX bez
  instalowania Pandoca i LaTeX-a, także na telefonie (Obsidian, SiYuan).
* **Eksport jednej strony** — z menu strony, bez przechodzenia przez eksport notatnika.

## Platformy i dystrybucja

* **iOS, iPadOS i macOS** — dziś tylko wersja w przeglądarce; Obsidian, Joplin, Anytype i inni mają
  tam aplikacje. Na iOS trzeba by rozwiązać alarmy, dostęp do folderu notatnika i Syncthing.
* **Sklepy i repozytoria** — F-Droid, Flathub, winget, AUR, Homebrew. Google Play ogranicza dostęp
  do wszystkich plików, którego wymaga folder Syncthinga (docs/ANDROID.md).
* **Więcej języków interfejsu** — tłumaczenia poza polskim i angielskim, a z nimi daty w języku
  naturalnym, kalkulator i wprowadzenie (Obsidian: wiele języków, także na start).
* **Motywy** — pliki motywów wymieniane między użytkownikami (kolory, czcionki, odstępy), poza
  ustawieniami wyglądu (Obsidian, Joplin); bez kodu z notatek.

## Windows

* **Podpisane pliki** — instalator i programy podpisane certyfikatem, bez ostrzeżenia SmartScreen;
  potem zdjąć „(wersja testowa)” (docs/WINDOWS.md).
* **Opinie o wersji desktopowej** — zebrać je od testera (obejrzy ją później).

## Wydajność

Szczegóły w docs/PERFORMANCE.md, „Next steps”:
* szybsze łączenie gotowych notatek w indeks w wątku interfejsu, mniej odświeżania widoku w trakcie
  wczytywania (w przeglądarce pierwsze wczytanie bez gotowego indeksu jest o 11–16 % dłuższe niż
  przed falą 5) i pomiar indeksowania na Raspberry Pi i telefonie (L1b);
* pomiar odświeżania list po zapisie (zadania, kalendarz, przypomnienia, zapytania) na Raspberry Pi
  (przed zmianą 0,95 s, cel < 0,2 s) i obniżenie jego wiersza bazowego w
  tests/bench/baselines/pi.csv (L3b);
* pomiar szybszego cofania na Raspberry Pi (przed zmianą 214 ms, cel < 50 ms) i zapis jego wiersza
  bazowego w tests/bench/baselines/pi.csv — do tego czasu sprawdzanie wydajności na Pi go pomija
  (L5b);
* QML deweloperski i samotesty (`qml/dev`, `qml/content/*SelfTest.qml`) poza `src/app`, żeby
  wydania nie pakowały QtTest, QuickTest i Qt.labs.folderlistmodel (L9b: APK −0,84 MB, wersja
  webowa −0,27 MB przed wasm-opt);
* **Okna wyboru plików — sprawdzić ręcznie**: Windows (Otwórz/Zapisz w imporcie, eksporcie,
  „Obraz…” i dzienniku zdarzeń), AppImage w prawdziwej sesji GNOME i KDE (okno portalu; bez sesji
  pulpitu sprawdzone tylko z xdg-desktop-portal-gtk pod Xvfb), Android z Dyskiem Google
  (import i „Zapisz…”), przeglądarka na telefonie (kilka plików naraz);
* **Graf — sprawdzić ręcznie i zmierzyć na telefonie** (układ na wątku roboczym i rysowanie przez
  kartę graficzną zrobione, L16b): na prawdziwym telefonie z dużym notatnikiem przesuwanie,
  powiększanie i przeciąganie stron w trakcie układania; Windows z kartą graficzną (Direct3D 11)
  i przeglądarka na telefonie; pomiar na Raspberry Pi (rysowanie programowe) i w emulatorze
  Androida.
