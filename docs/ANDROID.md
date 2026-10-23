# Brulion na Androidzie

Brulion na Androidzie to ta sama aplikacja C++/Qt Quick co na komputerze (Qt 6.11 for Android),
z natywną częścią w Kotlinie (`android/src/io/github/januszbit/brulion/android/`), która:

* daje dostęp do folderu z notatkami na pamięci współdzielonej („Dostęp do wszystkich plików”),
* planuje przypomnienia i alarmy w systemie (`AlarmManager`) — działają przy zamkniętej
  aplikacji i po restarcie telefonu,
* otwiera właściwą notatkę po stuknięciu w powiadomienie.

C++ rozmawia z nią przez JNI (`src/platform/android/androidbridge.{h,cpp}`), patrz
[Most JNI](#most-jni-c--kotlin).

Notatki to zwykłe pliki `.md` w folderze. Na telefonie folder może:

* być **folderem Syncthing** na pamięci współdzielonej (np. `/storage/emulated/0/Brulion`) —
  wymaga uprawnienia „Dostęp do wszystkich plików”, opis niżej;
* leżeć w **prywatnym katalogu aplikacji** (bez dodatkowych uprawnień) i być synchronizowany
  z **Nextcloud** (WebDAV) z poziomu Brulionu — Ustawienia → Synchronizacja (przez HTTPS, patrz
  [HTTPS i OpenSSL](#https-i-openssl)).

Wszystko działa offline. Synchronizacja (Syncthing czy Nextcloud) nigdy nie blokuje pisania.

Wymagany **Android 9 (API 28)** lub nowszy — tyle obsługuje Qt 6.11 (cel: API 36, Android 16).

## Folder Syncthing na telefonie

1. Zainstaluj **Syncthing-Fork** (np. z F-Droid). Pierwotna aplikacja „Syncthing” na Androida nie
   jest już rozwijana — fork jest jej kontynuacją.
2. W Syncthing-Fork dodaj folder udostępniany z komputera (albo utwórz nowy) i ustaw jego ścieżkę
   na pamięci telefonu, np. `/storage/emulated/0/Brulion` lub `/storage/emulated/0/Syncthing/Notatki`.
   Typ folderu: „Wysyłanie i odbieranie”.
3. Otwórz Brulion → wybór notatnika → wskaż ten sam folder.
4. Gdy Brulion poprosi o **„Dostęp do wszystkich plików”**, otworzy się ekran systemowy —
   włącz przełącznik dla Brulionu i wróć do aplikacji (sprawdzi uprawnienie sama).
   Ręcznie: *Ustawienia → Aplikacje → Specjalny dostęp aplikacji → Dostęp do wszystkich plików →
   Brulion* (nazwy menu różnią się między producentami).

Dlaczego to uprawnienie? Folder Syncthing leży na pamięci współdzielonej, a Brulion czyta i zapisuje
notatki zwykłym systemem plików (szybka lista plików, zapis atomowy, wykrywanie zmian zrobionych
przez Syncthing). Bez tego uprawnienia Android 11+ widzi tam tylko pliki multimedialne.
Brulion nie zagląda poza wybrany folder.

Starsze wersje Androida:

* **Android 10** — zamiast „Dostępu do wszystkich plików” wystarcza zwykłe uprawnienie do plików
  (Brulion korzysta z `requestLegacyExternalStorage`).
* **Android 9** — Brulion poprosi o uprawnienie „Pamięć” (pliki i multimedia). Jeśli wcześniej
  wybrano „Nie pytaj ponownie”, otworzą się ustawienia aplikacji.

Bez folderu na pamięci współdzielonej notatnik powstaje w prywatnym katalogu Brulionu. Do niego
Syncthing nie ma dostępu — wtedy synchronizuj przez Nextcloud.

Wskazówki do Syncthing:

* W folderze warto mieć plik `.stignore` pomijający pliki tymczasowe (Brulion ma gotowy
  szablon). Dane urządzenia (historia zmian, indeksy) Brulion trzyma poza folderem, więc nie
  powodują konfliktów.
* Konflikty Syncthing (`*.sync-conflict-*.md`) Brulion wykrywa i proponuje scalenie.
* Wersjonowanie plików w Syncthing („Proste” lub „Przedziałowe”) jest dobrym, dodatkowym
  zabezpieczeniem.

## Przypomnienia i alarmy

Przypomnienie (`⏰ 2026-10-14 09:00` w notatce) staje się powiadomieniem systemowym, które pojawi
się **także przy zamkniętej aplikacji i po restarcie telefonu**. **Alarm** (`⏰! 2026-10-14 06:30`)
dzwoni jak budzik. Składnia: `docs/SYNTAX.md`.

### Jak to działa

* Gdy Brulion działa (przy uruchomieniu i po zmianach w notatkach), przekazuje systemowi listę
  nadchodzących przypomnień, zastępując poprzednią. Część natywna zapisuje ją w pamięci aplikacji
  i nastawia budziki systemowe (`AlarmManager`): alarmy przez `setAlarmClock` (budzi telefon,
  w pasku stanu widać ikonę budzika), zwykłe przypomnienia przez `setExactAndAllowWhileIdle`.
  Naraz planowanych jest najwyżej 400 najbliższych przypomnień.
* **Kanały powiadomień**: „Przypomnienia” (wysoki priorytet, dźwięk powiadomienia), „Alarmy”
  (dźwięk budzika na strumieniu alarmu, wibracje, dzwoni aż do reakcji, widoczne na ekranie
  blokady) i „Przegapione alarmy” (ciche). Dźwięk przypomnień i alarmów wybierzesz w samym
  Brulionie (niżej: [Dźwięki](#dźwięki-przypomnień-i-alarmów)); ustawienia systemowe kanałów
  są w *Ustawienia → Aplikacje → Brulion → Powiadomienia*.
* **Alarm bez reakcji** przestaje dzwonić sam po **10 minutach** (aplikacja może ustawić inny
  czas dla danego alarmu, od 1 do 60 minut) — także przy zamkniętej aplikacji. Zamienia się
  wtedy w ciche powiadomienie **„Przegapiony alarm”**, a ekran alarmu się zamyka; stuknięcie
  w powiadomienie nadal otwiera notatkę.
* **Alarm przy wygaszonym ekranie** otwiera pełnoekranowy ekran alarmu nad ekranem blokady
  z przyciskami **Drzemka 10 min**, **OK** i **Otwórz notatkę**.
* Przyciski w powiadomieniu: **Drzemka 10 min** (przypomnienie wróci za 10 minut — działa także
  przy zamkniętej aplikacji) i **OK**. Stuknięcie w powiadomienie otwiera notatkę w miejscu
  przypomnienia. Przy alarmie każda z tych reakcji wycisza go i zamyka ekran alarmu.
* **Po restarcie** telefonu (po pierwszym odblokowaniu), po aktualizacji aplikacji i po zmianie
  godziny przypomnienia są nastawiane ponownie. Te, które przypadły, gdy telefon był wyłączony
  (do 24 godzin wstecz), pojawią się od razu jako „Przegapione”.
* **Zmiana strefy czasowej**: przypomnienia trzymają się czasu lokalnego — „09:00” zostaje
  o 09:00 w nowej strefie (drzemki nie są przesuwane).

### Dźwięki przypomnień i alarmów

*Ustawienia → Przypomnienia → Dźwięk przypomnień* i *Dźwięk alarmów* (osobno dla `⏰` i `⏰!`):

* **Domyślny** — dźwięk powiadomień telefonu (przypomnienia) albo jego dźwięk alarmu (alarmy);
* **Dzwonek** — **Wybierz dzwonek…** otwiera systemowe okno wyboru dzwonka (dla przypomnień
  dźwięki powiadomień, dla alarmów dźwięki alarmów; widać też własne dzwonki dodane
  w telefonie). Wybrany dzwonek Brulion pamięta: po przełączeniu na „Cisza” i z powrotem na
  „Dzwonek” wraca ten sam;
* **Wibracja** — tylko wibracje, bez dźwięku;
* **Cisza** — ani dźwięku, ani wibracji (alarm nadal pokazuje swój ekran nad ekranem blokady).

**Odtwórz próbkę** gra wybrany dźwięk przez około 3 sekundy (dźwięk alarmu na strumieniu
alarmu — z głośnością budzika; „Wibracja” — wibruje). Alarm gra wybrany dźwięk w pętli
(`FLAG_INSISTENT`) aż do reakcji („OK”, „Drzemka 10 min”, stuknięcie) albo do końca czasu
dzwonienia. Przycisk **Ustawienia powiadomień systemowych** (Przypomnienia / Alarmy) otwiera
ustawienia kanału w Androidzie — dla wyjątków: tryb „Nie przeszkadzać”, wibracje na zegarku,
dźwięk zmieniony przez producenta telefonu.

Jak to działa: dźwięku kanału powiadomień nie da się zmienić po jego utworzeniu (Android
zapamiętuje ustawienia kanału, także usuniętego i utworzonego ponownie z tym samym id). Dlatego
każdy nowy dźwięk dostaje **nową wersję kanału**: `reminders` / `alarms` (wersja 0 — kanał
z poprzednich wersji Brulionu, z domyślnym dźwiękiem i tym, co ustawiono dla niego
w systemie), potem `reminders-v1`, `alarms-v2`… z dźwiękiem (`AudioAttributes`
`USAGE_NOTIFICATION_EVENT` dla przypomnień, `USAGE_ALARM` dla alarmów) i wibracjami;
poprzednia wersja jest usuwana (w ustawieniach systemowych Brulionu widać wtedy liczbę
„usuniętych kategorii”). Wybór i wersje zapisuje część natywna w `SharedPreferences`
(`…android.sounds`), bo alarmy dzwonią przy zamkniętej aplikacji: każde powiadomienie
(zaplanowane, po restarcie, przegapione, po drzemce) trafia do bieżącego kanału — zmiana
dźwięku i wysłanie powiadomienia idą pod wspólną blokadą, więc powiadomienie nigdy nie trafi do
kanału, który właśnie usunięto (Android by je odrzucił). Ustawienia
dźwięku są **lokalne dla urządzenia** (dzwonek z jednego telefonu nie istnieje na innym) —
nie synchronizują się z notatnikiem. Zmiana dźwięku w czasie, gdy alarm dzwoni, zamyka jego
powiadomienie (kanał jest usuwany).

Ograniczenie: Brulion planuje przypomnienia na podstawie notatek, które przeczytał. Przypomnienie
dodane albo zmienione na innym urządzeniu (i zsynchronizowane, gdy Brulion był zamknięty) zostanie
zaplanowane przy najbliższym otwarciu Brulionu. Warto otworzyć aplikację po większych zmianach
na komputerze.

### Uprawnienia

* **Powiadomienia** (Android 13+): Brulion poprosi o zgodę systemowym okienkiem. Gdy system
  już nie pyta (odmowa „na stałe” albo powiadomienia wyłączone ręcznie), ponowna prośba otwiera
  ustawienia powiadomień Brulionu.
* **Alarmy i przypomnienia** (dokładne budziki):
  * Android 13+ — przyznawane przy instalacji (`USE_EXACT_ALARM`), nic nie trzeba robić;
  * Android 12 — *Ustawienia → Aplikacje → Specjalny dostęp aplikacji → Alarmy i przypomnienia*
    (Brulion otworzy ten ekran). Po włączeniu przypomnienia są od razu przestawiane na dokładne;
  * Android 9–11 — nie wymagają zgody;
  * bez tego uprawnienia przypomnienia przychodzą z opóźnieniem (minuty, w trybie uśpienia dłużej).
* **Powiadomienia pełnoekranowe** (Android 14+): potrzebne do ekranu alarmu nad ekranem blokady —
  *Ustawienia → Aplikacje → Specjalny dostęp aplikacji → Powiadomienia pełnoekranowe* (Brulion
  może otworzyć ten ekran; gdy telefon go nie ma — ustawienia powiadomień Brulionu). Bez tego
  uprawnienia alarm i tak dzwoni, ale pokazuje się jako zwykłe powiadomienie.
* **Nie przeszkadzać**: alarmy mają kategorię „alarm”. Jeśli w trybie „Nie przeszkadzać” alarmy
  są wyciszone, zezwól na nie w ustawieniach tego trybu.

### Oszczędzanie baterii

Budziki systemowe działają także w trybie uśpienia (Doze), ale część producentów agresywnie
„ubija” aplikacje w tle, a **wymuszone zatrzymanie kasuje wszystkie budziki aplikacji** aż do jej
ponownego uruchomienia. Dlatego:

* *Ustawienia → Aplikacje → Brulion → Bateria* → **Bez ograniczeń**;
* **Xiaomi / Redmi / POCO** (MIUI, HyperOS): włącz **Autostart** dla Brulionu i ustaw
  „Oszczędzanie baterii” na „Bez ograniczeń”;
* **Samsung**: usuń Brulion z „Aplikacji uśpionych” i „Aplikacji w głębokim uśpieniu”
  (*Ustawienia → Bateria → Limity użycia w tle*);
* **Huawei / Honor**: *Uruchamianie aplikacji* → Brulion → zarządzaj ręcznie (wszystkie trzy
  przełączniki włączone);
* **OnePlus / OPPO / realme**: zezwól na działanie w tle i autostart.

To samo zrób dla **Syncthing-Fork**, żeby synchronizował w tle. Szczegóły dla konkretnych
telefonów: <https://dontkillmyapp.com>.

## Import notatek (Obsidian, Logseq, Notion…)

Gdzie: **Więcej (⋮) → Importuj notatki**, **☰ → Importuj notatki** (szuflada), przycisk
**Importuj notatki** pod pustym dziennikiem nowego notatnika i karta „Notatki w innej aplikacji?”
na ekranie powitalnym (otwiera wybrany notatnik od razu na ekranie importu). Na ekranie importu
wybierz aplikację, z której pochodzą notatki — pod jej opisem są kroki **na telefonie** (gdzie
leżą pliki, co wybrać) — albo od razu wybierz pliki: Brulion sam rozpozna format.

* **Wybierz pliki…** — systemowe okno wyboru plików (Storage Access Framework): ☰ przełącza
  między Pobranymi, pamięcią telefonu, Dyskiem Google i innymi aplikacjami; **długie
  przytrzymanie zaznacza kilka plików**. Okno nie filtruje typów plików (dostawcy różnie je
  nazywają i filtr wyszarzałby np. ZIP z Notion na Dysku albo plik .md w Pobranych) — format
  rozpoznaje Brulion po zawartości. Działają `.md`, `.txt`, `.html`, `.enex`, `.json`, `.zip`
  (cały eksport Obsidian / Logseq / Notion / Google Keep / Trilium), `.jex`, `.ics` i inne.
* **Wybierz folder…** — folder skarbca Obsidian, grafu Logseq albo folder z plikami `.md`:
  otwórz go w oknie systemowym, stuknij **„Użyj tego folderu”** i zezwól na dostęp (tylko do
  odczytu, tylko ten folder). Android **nie pozwala** wybrać całego folderu *Pobrane*, katalogu
  głównego pamięci ani `Android/data` — wybierz folder wewnątrz (np. *Pobrane/Obsidian*) albo
  wpisz ścieżkę.
* **Albo wpisz ścieżkę** — plik lub folder w systemie plików, np.
  `/storage/emulated/0/Download/Obsidian`. Poza prywatnym katalogiem Brulionu wymaga to
  „Dostępu do wszystkich plików” (karta pokazuje przycisk do ustawień systemowych); wybór przez
  okno systemowe żadnego uprawnienia nie potrzebuje.

Przed zapisem Brulion pokazuje, co zaimportuje (strony, notatki dzienne, załączniki, zmienione
nazwy, ostrzeżenia), potem postęp i raport z przyciskiem **„Otwórz zaimportowany folder”**
(domyślnie `Import/<aplikacja>`; notatki dzienne trafiają do dziennika).

Jak to działa: okno systemowe zwraca adresy `content://` — pojedynczy dokument albo **drzewo
dokumentów** (wybrany folder). Brulion przechodzi drzewo folder po folderze przez
`DocumentsContract` (`Documents.kt`, jedno zapytanie `ContentResolver` na folder; most JNI
`documentInfo` / `listDocuments`), a pliki czyta przez `QFile` na adresie `content://` (deskryptor
pliku z `ContentResolver`). Listowanie i czytanie idą poza wątkiem interfejsu, więc duży folder
albo wolny dostawca (Dysk Google) nie zawiesza aplikacji. Gdy wywołanie JNI zawiedzie, Brulion
korzysta z obsługi adresów `content://` w samym Qt (`QDir`/`QFileInfo`; wolniejsze — wiele zapytań
na każdy plik). Pliki istniejące tylko w sieci (np. dokumenty Google) są pomijane z ostrzeżeniem.

Do zrobienia: **„Udostępnij → Brulion”** (pliku `.md`/`.txt` z innej aplikacji do dziennika albo
nowej strony) — wymaga filtra `ACTION_SEND` w manifeście i obsługi intencji w `BrulionActivity`,
której jeszcze nie ma.

## Eksport: „Zapisz w Pobranych”

Gotowy plik eksportu (ZIP, HTML, iCalendar…) zapiszesz na dwa sposoby:

* **Zapisz…** — systemowe okno „Zapisz jako” (dowolne miejsce, także Dysk Google czy karta SD);
* **Zapisz w Pobranych** — jednym stuknięciem do folderu *Download*. Na Androidzie 10+ idzie to
  przez `MediaStore` i **nie wymaga żadnego uprawnienia** (także bez „Dostępu do wszystkich
  plików”); zajęta nazwa dostaje od systemu dopisek „(1)”, a raport podaje nazwę pliku. Na
  Androidzie 9 plik jest zapisywany bezpośrednio w *Download* (potrzebne uprawnienie do pamięci).

## Klawiatura ekranowa

Pasek narzędzi edycji leży zawsze tuż nad klawiaturą ekranową — w dzienniku i na stronach, także
gdy się przewija — a dolna nawigacja chowa się na czas pisania (docs/UI.md „On-screen
keyboard”). Gdy klawiaturę schowa system (Wstecz, klawisz na samej klawiaturze), pasek zostaje
na dole ekranu, nad paskiem nawigacji systemu, dopóki pisanie trwa; dotknięcie tekstu przywraca
klawiaturę. Okna wysuwane od dołu, menu i komunikaty też zawsze leżą nad klawiaturą, a pole
formularza (ustawienia, wybór notatnika, okna) przewija się nad nią, gdy dostaje fokus.

Jak to działa:

* `BrulionActivity` ma w manifeście `android:windowSoftInputMode="adjustResize|stateUnchanged"`:
  okno nigdy nie jest przesuwane („pan”) — bez tego Qt wybierał przesuwanie albo zmianę rozmiaru
  osobno dla każdego pola, a przesunięte okno odrywało pasek od klawiatury. Pól jednowierszowych
  nikt więc nie przesuwa do widoku: robi to aplikacja (`BKeyboardFocusKeeper`, docs/UI.md).
* Na Androidzie 11+ okno jest rysowane pod paskami systemu (edge-to-edge): na 15+ zawsze, na
  11–14 dzięki `Qt::ExpandedClientAreaHint` ustawianemu przez `src/app/ui/keyboardsources.cpp`.
  Takie okno nie zmienia rozmiaru przy klawiaturze i dostaje każdą klatkę jej wysuwania; paski
  aplikacji odsuwają się od pasków systemu o marginesy `SafeArea`, a ikony pasków systemu
  dopasowują się do koloru okna, czyli do motywu aplikacji (ciemne na jasnym, jasne na ciemnym;
  `BrulionAndroid.setSystemBarsLight`, ponownie po zmianie konfiguracji, bo Qt ustawia je wtedy
  według trybu dzień/noc systemu). Android 9–10 zostaje przy oknie wewnątrz pasków.
* `ImeInsets.kt` (instalowany w `onCreate`) dokłada do okna niewidoczny widok 0×0 i z niego
  obserwuje wstawki klawiatury (`WindowInsets` typu `ime()`), nie ruszając słuchaczy Qt (`SafeArea`)
  i niczego nie konsumując: `OnApplyWindowInsetsListener` daje wartość końcową,
  `WindowInsetsAnimation.Callback` (Android 11+) — każdą klatkę wysuwania klawiatury, a słuchacz
  układu widoku dekoracji — stan na starszych systemach, gdzie wstawki mógł zatrzymać widok Qt.
  Liczy, ile pikseli widoku Qt zakrywa klawiatura **na ekranie**: dół widoku Qt minus górna
  krawędź klawiatury. Gdy system zmniejszył okno nad klawiaturą (Android 9–10), wychodzi 0; przy
  oknie edge-to-edge (Android 11+) — cała wysokość klawiatury. Raport idzie przez `BrulionAndroid.imeChanged` → natywne
  `nativeImeChanged` (wątek główny Androida; C++ tylko kolejkuje sygnał
  `AndroidBridge::imeChanged` do wątku Qt), tylko gdy coś się zmieniło.
* C++: `KeyboardInsets` (`src/app/ui/keyboardinsets.h`) przelicza piksele fizyczne na logiczne
  i od pierwszego raportu ignoruje prostokąt klawiatury Qt (`QInputMethod::keyboardRectangle`,
  zostaje jako zapas, gdyby obserwator się nie zainstalował). W trakcie wysuwania (Android 11+)
  pasek idzie za klawiaturą klatka po klatce (bez własnej animacji), a kursor jest przewijany nad
  pasek dopiero, gdy klawiatura się zatrzyma; na Androidzie 9–10 okno zmienia rozmiar raz.
* Diagnostyka: szczegółowy rejestr zdarzeń zapisuje ustaloną wartość („On-screen keyboard shown,
  overlap 336 px (platform)”) i to, gdzie po jej zatrzymaniu kończy się zawartość („Typing: the
  content ends 48 px above the window's bottom (keyboard 0 px, navigation bar 48 px), editing
  toolbar docked”); `adb shell setprop log.tag.BrulionIme DEBUG` (przed startem aplikacji)
  wypisuje w logcat każdy raport obserwatora, także klatki wysuwania.

Sprawdzone na emulatorze (`scripts/android-emulator-test.sh --keyboard`, niżej; profil Pixel 6,
1080×2400, gęstość 420, klawiatura Gboard), w dzienniku i na stronie przewodnika:

* Android 16 (API 36, nawigacja gestami): górna krawędź klawiatury na 1517 px, widok Qt do
  2400 px, obserwator zgłasza 883 px, aplikacja 336 dp (= 883 · 160 / 420); wysuwanie przyszło
  w 8 klatkach; po Wstecz zawartość kończy się 24 dp nad dołem (pasek nawigacji gestami);
* Android 11 (API 30, nawigacja trzema przyciskami): okno też edge-to-edge (0–2400 px, wcześniej
  system zmniejszał je nad klawiaturą bez żadnych klatek): górna krawędź klawiatury na 1533 px,
  obserwator zgłasza 867 px, aplikacja 330 dp; wysuwanie przyszło w 9 klatkach; po Wstecz
  zawartość kończy się 48 dp nad dołem (pasek nawigacji); paski aplikacji leżą pod paskiem
  stanu i nad paskiem nawigacji, ikony systemu ciemne w jasnym motywie i jasne w ciemnym.

Na obu pasek leży dokładnie na klawiaturze, zostaje na niej przy przewijaniu dziennika palcem,
a po Wstecz zostaje na dole tuż nad paskiem nawigacji systemu (dolna nawigacja aplikacji wraca
dopiero po zakończeniu pisania). Android 9–10 i prawdziwe telefony nie były sprawdzane.

## Rejestr zdarzeń: udostępnianie

Rejestr zdarzeń (*Ustawienia → Aplikacja → Rejestr zdarzeń*, docs/ARCHITECTURE.md „Logging”)
leży w katalogu plików aplikacji (`/data/user/0/io.github.januszbit.brulion/files/logs/brulion.log`
i `brulion.log.1`) — niewidoczny dla innych aplikacji. Brulion zapisuje go od razu przy
ostrzeżeniach i przy przejściu w tło (Android potrafi zatrzymać proces bez zamykania aplikacji;
następne uruchomienie odnotowuje taką sesję). Wysłać go można:

* **Udostępnij** — kopia eksportu trafia do `files/share/` (stara jest usuwana) i idzie przez
  systemowy arkusz udostępniania (`ACTION_SEND`, `EXTRA_STREAM` z adresem `content://` z
  FileProvidera Qt `<pakiet>.qtprovider`, który obejmuje tylko `files-path`, stąd katalog pod
  `files/`; odbiorca dostaje jednorazowe prawo odczytu) — `AndroidBridge::shareFile()`, JNI bez
  kodu Kotlina;
* **Zapisz w Pobranych** (`MediaStore`, jak eksport) albo **Zapisz jako…** (systemowe okno);
* **Kopiuj** — do schowka (najnowsze 256 KB).

Eksport zaczyna się nagłówkiem (wersja, urządzenie, co zawiera, czego nie zawiera — hasła i
tokeny są usuwane, ostatnie synchronizacje).

## HTTPS i OpenSSL

Synchronizacja z Nextcloud (WebDAV) idzie przez HTTPS. Qt for Android nie ma własnego TLS:
jego wtyczka TLS (`libplugins_tls_qopensslbackend`) przy pierwszym bezpiecznym połączeniu
wczytuje OpenSSL 3 (`libssl_3.so`, `libcrypto_3.so`), którego Qt nie dostarcza. Dlatego APK
zawiera **OpenSSL 3.1.8** — gotowe biblioteki z
[KDAB/android_openssl](https://github.com/KDAB/android_openssl), tak jak zaleca dokumentacja Qt
(„Adding OpenSSL Support for Android”). Bez nich (do wersji 0.1.1) każde połączenie HTTPS kończyło
się błędem „TLS initialization failed”, a test połączenia na ekranie Nextcloud zgłaszał
niezaufany certyfikat także przy poprawnym serwerze.

* **Certyfikat serwera jest zawsze sprawdzany**; Brulion nigdy nie wysyła hasła do niezaufanego
  serwera. Zaufane są urzędy certyfikacji systemu **i te, które zainstalował użytkownik**
  (Ustawienia → Bezpieczeństwo → Szyfrowanie i dane logowania → Zainstaluj certyfikat → Certyfikat
  CA; `android/res/xml/network_security_config.xml`). Własny Nextcloud z własnym urzędem
  certyfikacji działa więc po zainstalowaniu na telefonie certyfikatu tego urzędu — jak na
  komputerze. Sam certyfikat samopodpisany serwera nie wystarcza: test połączenia pokazuje wtedy
  „Certyfikat serwera nie jest zaufany: mógł wygasnąć, być samopodpisany albo wystawiony na inną
  nazwę…” z podpowiedzią, co zrobić.
* **Skąd biblioteki**: commit KDAB przypięty w `src/app/CMakeLists.txt`
  (`_brulion_openssl_commit`), każdy z 8 plików (`libssl_3.so`, `libcrypto_3.so` dla arm64-v8a,
  armeabi-v7a, x86_64 i x86) sprawdzany po SHA-256. CMake pobiera przy konfiguracji tylko pliki
  budowanych ABI (z `raw.githubusercontent.com` do `<build>/android_openssl/<commit>/`), a
  `androiddeployqt` dokłada je do `lib/<abi>/` (`QT_ANDROID_EXTRA_LIBS`). Bez sieci:
  `BRULION_ANDROID_OPENSSL_DIR=<checkout android_openssl na tym commicie>`. Build Nix bierze ten sam
  commit z `nix/android/openssl.json`. 64-bitowe biblioteki mają segmenty wyrównane do 16 KB.
* **Aktualizacja** (nowe wydanie OpenSSL u KDAB): nowy commit i sumy SHA-256
  (`sha256sum ssl_3/*/lib{ssl,crypto}_3.so` w checkoutcie) w `src/app/CMakeLists.txt`, potem
  `nix/android/update-locks.sh --openssl <commit>`.
* **Sprawdzane przy budowaniu**: `scripts/android-build.sh` i derywacja Nix przerywają build, gdy
  w APK brakuje `libssl_3.so` albo `libcrypto_3.so` dla któregoś ABI; skrypt wypisuje wersję
  OpenSSL.
* Koszt: +4,45 MB w APK arm64 (1,77 MB po kompresji).
* Sprawdzone 2026-10-18 na emulatorze (Android 16) z APK ze skryptu i z
  `nix build .#brulion-android-x86_64`, z lokalnym serwerem WebDAV przez HTTPS: OpenSSL 3.1.8
  wczytany (`QSslSocket::supportsSsl()`), certyfikat samopodpisany i certyfikat nieznanego urzędu
  zgłaszane jako niezaufane, po zainstalowaniu urzędu jako certyfikatu użytkownika „Połączono”,
  synchronizacja w obie strony; wcześniej (0.1.1) każde HTTPS: „TLS initialization failed”.

**Licencja.** OpenSSL 3 i paczka KDAB są na licencji **Apache License 2.0**, zgodnej z AGPL-3.0
Brulionu (licencje GPLv3 i AGPLv3 pozwalają łączyć program z kodem na Apache-2.0). Tekst
licencji jedzie w APK (`assets/licenses/OpenSSL-LICENSE.txt`, z `android/assets/licenses/`) — kto
rozpowszechnia APK (wydania na GitHubie, F-Droid), przekazuje go razem z nim. Biblioteki nie są
modyfikowane.

## Budowanie APK

Całość robi `scripts/android-build.sh` (lokalnie i w GitHub Actions): doinstalowuje brakujące
narzędzia, konfiguruje CMake, buduje bibliotekę `libbrulion_<abi>.so`, pakuje APK przez
`androiddeployqt` (Gradle), podpisuje i sprawdza wynik. Można go uruchamiać wielokrotnie —
istniejący katalog budowania jest wykorzystywany ponownie.

```sh
scripts/android-build.sh            # wynik: dist-android/brulion-<wersja>-arm64-v8a.apk
adb install -r dist-android/brulion-*.apk
scripts/android-build.sh --help     # wszystkie zmienne środowiskowe
```

Wymaga hosta **Linux x86_64** (NDK i narzędzia Qt są tylko na x86_64) — nie Raspberry Pi.

### Wersje (Qt 6.11.2)

| Składnik | Wersja | Skąd |
|---|---|---|
| Qt for Android | 6.11.2 `android_arm64_v8a` (opcjonalnie `armv7`, `x86_64`, `x86`) | aqt (`aqtinstall` 3.3.0) → `~/.cache/brulion-qt` |
| Qt hosta (moc, rcc, qmlcachegen, androiddeployqt…) | 6.11.2 `linux_gcc_64` — ta sama wersja | aqt |
| Android NDK | r27c (`27.2.12479018`) — wymagany przez Qt 6.11 | `sdkmanager` |
| Android SDK | platforma `android-36`, build-tools `36.0.0` | `sdkmanager` |
| CMake + Ninja | 3.31.6 (pakiet SDK `cmake;3.31.6`) | `sdkmanager` |
| JDK | 21 (Qt 6.11 zaleca 21; działa 17+) | system / nixpkgs `jdk21_headless` |
| Gradle / Android Gradle Plugin | 9.3.1 / 9.0.0 (szablon Qt 6.11, wbudowana obsługa Kotlina) | wrapper Gradle z Qt |
| minSdk / targetSdk / compileSdk | 28 / 36 / 36 | `src/app/CMakeLists.txt` |
| OpenSSL (HTTPS) | 3.1.8, KDAB android_openssl `b71f147` | pobierany przez CMake, sprawdzany po SHA-256 (`src/app/CMakeLists.txt`) |

Zmienne (wszystkie opcjonalne): `QT_ROOT`, `QT_VERSION`, `QT_HOST_PATH`, `ANDROID_SDK_ROOT`
(domyślnie `~/.cache/brulion-android-sdk`; brakujące pakiety instaluje jego `cmdline-tools`),
`ANDROID_NDK_ROOT`, `JAVA_HOME`, `BRULION_ANDROID_ABIS` (np. `"arm64-v8a x86_64"` — jeden APK
z kilkoma architekturami), `BRULION_ANDROID_BUILD_DIR` (domyślnie `build-android`),
`BRULION_ANDROID_BUILD_TYPE` (`Release`), `BRULION_ANDROID_AAB=1` (także `.aab`),
`BRULION_ANDROID_UNIT_TESTS=1` (testy Kotlina), `BRULION_ANDROID_LINT=1` (Android lint),
`BRULION_ANDROID_SELFTEST=1` (także APK testowy mostu), `BRULION_ANDROID_VERSION_CODE`,
`BRULION_ANDROID_CMAKE=system`, `BRULION_ANDROID_OPENSSL_DIR` (OpenSSL z checkoutu
KDAB/android_openssl zamiast pobierania), `BRULION_WERROR=ON` (ostrzeżenia w naszym C++ są
błędami), `JOBS`, `SOURCE_DATE_EPOCH` (czas plików w zasobach Qt, rcc; domyślnie czas ostatniego
commita — powtarzalne APK).

`versionCode` = `major·1000000 + minor·1000 + patch` (0.1.0 → 1000) — ten sam wzór co
w poprzedniej wersji Brulionu (Tauri), więc nowe APK aktualizują stare (przy tym samym kluczu).

### Podpisywanie

Skrypt podpisuje APK sam (`zipalign -P 16` + `apksigner`; AAB — `jarsigner`) kluczem z tych
samych zmiennych, które czyta `androiddeployqt`:

* `QT_ANDROID_KEYSTORE_PATH` (albo `QT_ANDROID_KEYSTORE_BASE64` — plik w base64, sekret CI),
* `QT_ANDROID_KEYSTORE_ALIAS`, `QT_ANDROID_KEYSTORE_STORE_PASS`,
  `QT_ANDROID_KEYSTORE_KEY_PASS` (domyślnie hasło magazynu).

Klucz wydań tworzy (poza repozytorium) `scripts/android-new-keystore.sh` — wypisuje też polecenia
`gh secret set` dla GitHub Actions. **Zrób kopię zapasową klucza i hasła**: każda aktualizacja
musi być podpisana tym samym kluczem.

**Bez klucza** skrypt głośno ostrzega i podpisuje APK **kluczem jednorazowym**
(`~/.cache/brulion-android/throwaway.jks`, tworzony raz na maszynę). Taki APK da się zainstalować
i aktualizować kolejnymi kompilacjami z tej samej maszyny, ale nie zaktualizuje wersji podpisanej
kluczem wydań (trzeba ją odinstalować, co usuwa prywatne dane aplikacji).

### NixOS

Na NixOS skrypt sam uruchamia się ponownie w `nix shell` (ccache, `jdk21_headless`, unzip).
Gotowe binaria (NDK, narzędzia Qt z aqt, `aapt2` pobierany przez Gradle, CMake z SDK) wymagają
**nix-ld** (`programs.nix-ld.enable = true`); narzędzia Qt potrzebują dodatkowo glib, krb5,
brotli i keyutils — skrypt dopisuje je do `NIX_LD_LIBRARY_PATH` (z `nix build nixpkgs#…`).
CMake z nixpkgs **nie nadaje się**: jest załatany tak, że nie przeszukuje `/usr` — także
w sysroocie NDK, więc nie znajduje nagłówków EGL/GLES i Qt6Gui się nie konfiguruje. Dlatego
domyślnie używany jest CMake z Android SDK (`BRULION_ANDROID_CMAKE=system` wybiera ten z `PATH`).

### Budowanie na laptopie (z Raspberry Pi)

Pi (aarch64, 3,7 GB RAM) nie buduje Androida. Kompilacja idzie na laptopie x86_64 przez
`laptop-run` (kopiuje śledzone pliki repozytorium do `~/work/note-taking` i tam uruchamia
polecenie; nowe pliki trzeba najpierw oznaczyć `git add -N <ścieżki>`), zawsze pod wspólną blokadą.
Katalog budowania trzymamy poza `~/work` (jest czyszczony przy każdym `laptop-run`), cache: Qt
w `~/.cache/brulion-qt`, SDK w `~/.cache/brulion-android-sdk`, ccache w `~/.cache/ccache`,
Gradle w `~/.gradle`:

```sh
flock <blokada> laptop-run bash -c '
  export BRULION_ANDROID_BUILD_DIR=$HOME/.cache/brulion-android-build
  scripts/android-build.sh > $HOME/.cache/brulion-android-build.log 2>&1; echo "exit=$?"
  tail -n 60 $HOME/.cache/brulion-android-build.log'
```

Pierwsza instalacja narzędzi na laptopie (to samo robi skrypt, gdy czegoś brakuje):

```sh
uvx --from aqtinstall==3.3.0 aqt install-qt all_os android 6.11.2 android_arm64_v8a \
  -O ~/.cache/brulion-qt --autodesktop      # + linux_gcc_64 (Qt hosta)
JAVA_HOME=$(nix build --no-link --print-out-paths nixpkgs#jdk21_headless)/lib/openjdk \
  ~/.cache/brulion-android-sdk/cmdline-tools/latest/bin/sdkmanager \
  --sdk_root=$HOME/.cache/brulion-android-sdk --install "ndk;27.2.12479018" "cmake;3.31.6"
```

### Wynik (zweryfikowane 2026-10-14 na laptopie x86_64, NixOS)

`BRULION_WERROR=ON BRULION_ANDROID_UNIT_TESTS=1 BRULION_ANDROID_LINT=1 scripts/android-build.sh`
(pierwsze budowanie: pobranie Gradle 9.3.1 + zależności AGP, potem Gradle ~45 s; kolejne
przebiegi z ccache i cache Gradle — kilkadziesiąt sekund). Koniec logu:

```
==> Checking dist-android/brulion-0.1.0-arm64-v8a.apk
Signer #1 certificate DN: CN=Brulion unofficial build, O=Throwaway key
package: name='io.github.januszbit.brulion' versionCode='1000' versionName='0.1.0' … compileSdkVersion='36'
minSdkVersion:'28'
targetSdkVersion:'36'
application-label:'Brulion'
native-code: 'arm64-v8a'
permissions: 13 (all required present)
Kotlin classes in dex: 16
native libraries: 72 in lib/{arm64-v8a}
==> Kotlin unit tests (android/tests)          # ReminderJsonTest 5, WallClockTest 4, RingTimeoutTest 3 — 0 błędów
==> Android lint
lint: 0 errors, 2 warnings — in Brulion's files: 1
AndroidManifest.xml:59: Warning: Attribute enableOnBackInvokedCallback is only used in API level 33 and higher … [UnusedAttribute]
==> Done
APK: …/dist-android/brulion-0.1.0-arm64-v8a.apk (58M, signing: throwaway)
```

Scalony manifest (`aapt2 dump xmltree --file AndroidManifest.xml`) zawiera `BrulionActivity`
(LAUNCHER, `singleTop`), cztery odbiorniki (nieeksportowane; `BootReceiver` z akcjami
BOOT_COMPLETED, QUICKBOOT_POWERON, MY_PACKAGE_REPLACED, TIME_SET, TIMEZONE_CHANGED,
SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED), `AlarmActivity`, `requestLegacyExternalStorage`,
ikony `@mipmap/ic_launcher`/`ic_launcher_round` oraz uprawnienia: ACCESS_NETWORK_STATE, INTERNET
(z Qt Network), MANAGE_EXTERNAL_STORAGE, READ_EXTERNAL_STORAGE (max 32), WRITE_EXTERNAL_STORAGE
(max 29), POST_NOTIFICATIONS, SCHEDULE_EXACT_ALARM (max 32), USE_EXACT_ALARM,
RECEIVE_BOOT_COMPLETED, VIBRATE, WAKE_LOCK, USE_FULL_SCREEN_INTENT. Sygnatury metod
skompilowanej klasy `BrulionAndroid` (`javap -s`) zgadzają się z wywołaniami JNI w C++.

Test end-to-end na emulatorze (Android 16 / API 36, x86_64, `google_apis`; ten sam dzień):
`BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 BRULION_WERROR=ON scripts/android-build.sh`
i `scripts/android-emulator-test.sh` — wszystkie 14 sprawdzeń przeszło:

```
==> App: dist-android/brulion-0.1.0-x86_64.apk
  PASS app is running 15 s after start
  PASS no crash (FATAL EXCEPTION / Fatal signal)
  PASS notification channels reminders, alarms, missed-alarms
==> Bridge self-test: dist-android/brulion-selftest-0.1.0-x86_64.apk
  PASS scheduleReminders round 1: 2 armed (past one skipped)
  PASS scheduleReminders round 2: idempotent
  PASS permissions reported (notifications granted, exact alarms)
  PASS cold-start tap delivered to C++
  PASS tap while running delivered to C++ (Polish path)
  PASS AlarmManager holds Brulion's alarms
  PASS reminder 101 fired on channel 'reminders'
  PASS alarm 102 rings on channel 'alarms'
  PASS alarm 102 became 'missed-alarms' after its ring time (1 min)
  PASS no crash in the self-test
W brulion.selftest: SELFTEST permissions(start) allFiles=0 notifications=1 exact=1 fullScreen=1
W brulion.selftest: SELFTEST schedule round=1 ok=1 scheduled=2 exact=1 error=
W brulion.selftest: SELFTEST schedule round=2 ok=1 scheduled=2 exact=1 error=
W brulion.selftest: SELFTEST activated path=dziennik/zimny-start.md line=5
W brulion.selftest: SELFTEST activated path=strony/Łódź.md line=7
```

`dumpsys alarm` pokazał przypomnienie jako dokładny budzik RTC_WAKEUP (`setExactAndAllowWhileIdle`)
i alarm jako budzik z `showIntent` (`setAlarmClock`), oba z akcją `…android.action.FIRE`
i `exactAllowReason=policy_permission` (`USE_EXACT_ALARM`).

### Rozmiar APK

Co buildy wydań (Release, MinSizeRel) pomijają albo zmniejszają — zmierzone 2026-10-18 na APK
arm64-v8a (`tests/bench/mobile/size-report.py apk`):

| Zmiana | Gdzie | APK arm64 |
|---|---|---|
| **R8** zmniejsza kod Javy/Kotlina (biblioteka standardowa Kotlina, androidx) | `android/settings.gradle`, reguły `android/proguard-rules.pro` | `classes.dex` 5,12 MB → 0,35 MB |
| bez wtyczek debugera i profilera QML (`qmltooling`, 12 bibliotek) | `src/app/CMakeLists.txt` (`qt_import_plugins(… EXCLUDE_BY_TYPE qmltooling)`) | −1,0 MB |
| bez QML deweloperskiego: galerii (`--gallery`), placu zabaw edytora (`--editor`), skryptów i samotestów `--editor-script` | opcja `BRULION_DEV_QML` w `src/app/CMakeLists.txt` (domyślne `AUTO`: wyłączona w Release/MinSizeRel na Androidzie i w przeglądarce, ustalane przy każdym `cmake`), zaślepki `src/app/qml/stubs/` | `libbrulion` −0,77 MB |
| OpenSSL 3 (HTTPS, wyżej) | `src/app/CMakeLists.txt` | +4,45 MB |

Razem 83,15 MB → 81,02 MB (do pobrania, po kompresji: 27,46 → 27,18 MB); bez OpenSSL byłoby
76,5 MB. Buildy Debug i RelWithDebInfo (APK pomiarowe) mają `qmltooling` i QML deweloperski.

**R8** działa w każdym buildzie release Gradle'a (APK i AAB z `CMAKE_BUILD_TYPE` Release,
RelWithDebInfo, MinSizeRel; Debug — bez R8). `android/settings.gradle` włącza go w projekcie
Gradle, który `androiddeployqt` składa z szablonu Qt — `build.gradle` z Qt zostaje nietknięty.
Klasy, które C++ woła przez JNI po nazwie, zostają w całości: Javy Qt (`org.qtproject.qt.**`,
także szukane przez refleksję) i Kotlina Brulionu (`io.github.januszbit.brulion.android.**`);
komponenty z manifestu (aktywności, odbiorniki, `FileProvider`) zachowuje sam AGP. C++ Qt woła
po nazwie także kilka metod androidx, których nie woła żaden kod Javy — reguły zachowują je
osobno: `FileProvider.getUriForFile` (lokalny plik jako `content://` dla
`QDesktopServices::openUrl` i przeciągania) oraz `FontRequest` / `FontsContractCompat`
(pobieranie czcionek ikon dla `QIcon::fromTheme`). Brulion dziś z nich nie korzysta (otwiera
tylko linki http/https/mailto, ikony ma własne), ale bez tych reguł nowa funkcja działałaby
w Debug, a w release nie. Nazwy nie są zaciemniane (`-dontobfuscate`: czytelne stosy wywołań
w logcat). `scripts/android-build.sh` i derywacja Nix sprawdzają po buildzie, że w
`classes.dex` są klasy Brulionu, `QtNative`, `QtActivityBase`, `QtLoader` i te metody androidx.

## Budowanie przez Nix (flake)

APK można też zbudować jako wynik flake'a — czysta derywacja w piaskownicy Nixa (bez sieci;
wszystko, co pobierane, ma przypięty hash), na hoście **x86_64-linux** (NDK i narzędzia SDK są
tylko na x86_64; NixOS nie potrzebuje do tego nix-ld):

```sh
nix build .#brulion-android           # arm64-v8a: result/brulion-<wersja>-arm64-v8a-unsigned.apk
nix build .#brulion-android-x86_64    # dla emulatora
nix build .#brulion-android-aab       # arm64-v8a: APK + AAB (bundleRelease, ok. 10 s więcej)
nix run .#brulion-android-sign -- result/brulion-*-unsigned.apk   # → ./brulion-<wersja>-arm64-v8a.apk
```

Wynik jest **niepodpisany** (wyrównany `zipalign -P 16` dla stron 16 KB), a podpis dokłada
dopiero `brulion-android-sign` w chwili uruchomienia — klucz i hasło nigdy nie trafiają do
`/nix/store`. Derywacja sprawdza APK tak jak `scripts/android-build.sh` (pakiet, uprawnienia,
komponenty manifestu, klasy Kotlina i Javy Qt po R8, `libbrulion_<abi>.so`, OpenSSL, brak
`qmltooling`, wyrównanie 16 KB).

| Składnik | Skąd (`nix/android.nix`) |
|---|---|
| Qt for Android 6.11.2 (qtbase, qtdeclarative, qtsvg) | archiwa `.7z` z repozytorium Qt (te same, które pobiera aqt), osobno dla modułu i ABI, hashe SHA-256 publikowane przez Qt — `nix/android/qt-android.json`, `nix/android/qt.nix` |
| Qt hosta (moc, rcc, qmlcachegen, androiddeployqt, lrelease…) | nixpkgs `qt6` — **ta sama wersja** co Qt for Android (sprawdzane przy ewaluacji) |
| SDK (platforma 36, build-tools 36.0.0), NDK r27c, CMake 3.31.6 + Ninja | nixpkgs `androidenv` (licencja SDK akceptowana w `flake.nix`) |
| Gradle / AGP | Gradle 9 z nixpkgs (JDK 21) zamiast wrappera z szablonu Qt; AGP 9.0.0, Kotlin, androidx — z `nix/android/gradle-deps.json` (mitm-cache), wspólne dla wszystkich ABI |
| aapt2 | z build-tools SDK (`android.aapt2FromMavenOverride`) zamiast binarki z Mavena |
| OpenSSL 3 (KDAB android_openssl) | przypięty commit w `nix/android/openssl.json` — ten sam co w `src/app/CMakeLists.txt`, podawany jako `BRULION_ANDROID_OPENSSL_DIR` |

Podpisywanie (`nix run .#brulion-android-sign -- PLIK.apk|PLIK.aab [opcje]`, działa też na
aarch64, np. na Raspberry Pi — apksigner z nixpkgs to Java):

* `--keystore` (albo `BRULION_ANDROID_KEYSTORE`), domyślnie
  `~/.local/share/brulion/brulion-release.jks` — klucz z `scripts/android-new-keystore.sh`;
* hasło: `--password-file` (albo `BRULION_ANDROID_KEYSTORE_PASSWORD_FILE`), inaczej
  `QT_ANDROID_KEYSTORE_STORE_PASS`, inaczej plik obok magazynu z końcówką `.password` zamiast
  `.jks`, inaczej apksigner zapyta;
* `--alias` (albo `BRULION_ANDROID_KEY_ALIAS` / `QT_ANDROID_KEYSTORE_ALIAS`), domyślnie jedyny
  klucz w magazynie; `--out` — plik wynikowy (domyślnie bieżący katalog, nazwa bez `-unsigned`).

Po podpisaniu APK jest sprawdzany (`apksigner verify`, wyrównanie 16 KB); AAB podpisuje
`jarsigner`.

**Aktualizacja locków** — po `nix flake update` (nowe Qt w nixpkgs) albo po zmianie zależności
Gradle (szablon Qt, `android/`):

```sh
nix/android/update-locks.sh            # Qt for Android dla wersji qt6 z nixpkgs + zależności Gradle
nix/android/update-locks.sh --qt       # tylko qt-android.json (kilka sekund)
nix/android/update-locks.sh --gradle   # tylko gradle-deps.json (~3 min: buduje aplikację i nagrywa pobrania Gradle)
nix/android/update-locks.sh --openssl <commit>   # przypina KDAB android_openssl (hash archiwum)
```

Zależności Gradle nagrywa skrypt aktualizacji z `gradle.fetchDeps` (nixpkgs): buduje
aplikację poza piaskownicą (w bubblewrap, z dostępem do sieci przez nagrywające proxy) aż do
`gradle assembleRelease bundleRelease` i zapisuje listę plików z hashami.

Jak to działa (szczegóły w komentarzach `nix/android.nix`): CMake z SDK konfiguruje projekt
z plikiem toolchain Qt for Android; cel `brulion_make_apk` buduje bibliotekę i uruchamia
`androiddeployqt`, który kopiuje szablon Gradle z Qt i woła `./gradlew`. Derywacja zapisuje
wcześniej własny `gradlew` (Gradle z nixpkgs z flagami mitm-cache) — `androiddeployqt` nie
nadpisuje pliku nowszego od szablonu. Qt for Android jest podawane jako drzewo dowiązań
z zapisywalną kopią `src/` (szablonów), bo `androiddeployqt` edytuje skopiowane z nich pliki
(np. `res/values/libs.xml`), a kopie plików ze store'u są tylko do odczytu.

Zmierzone 2026-10-17 na laptopie x86_64 (16 rdzeni):

* od zera (pusty store, 1,3 GiB pobrań, m.in. NDK, SDK, Qt, Gradle, JDK): **250 s**;
* z gotowymi zależnościami (zmiana w źródłach): **~145 s** (C++ ~95 s, Gradle ~43 s, sprawdzenia);
* APK jest **powtarzalny bit w bit**: `nix build --rebuild` i budowanie w pustym store dały ten sam plik;
* to ten sam APK co ze `scripts/android-build.sh`: ten sam kod źródłowy zbudowany skryptem
  z `SOURCE_DATE_EPOCH=315532800` (stała nixpkgs) różni się tylko 20 bajtami build-id
  w `libbrulion_<abi>.so` (zależnymi od ścieżki budowania); z innym `SOURCE_DATE_EPOCH` (skrypt
  domyślnie bierze czas ostatniego commita) jeszcze znacznikami czasu plików w zasobach Qt (rcc).
  Podpis `brulion-android-sign` daje bajt w bajt to samo co podpis skryptu (apksigner
  z build-tools 36, schemat v3);
* APK x86_64 podpisany kluczem jednorazowym działa na emulatorze (Android 16, API 36): przykładowy
  brulion, edycja bloku, przypomnienie (powiadomienie w tle, otwarcie bloku po dotknięciu),
  eksport do ZIP, obrót ekranu; w logcat żadnych awarii. Emulator z `-gpu swangle_indirect`
  rysuje poprawnie (`swiftshader_indirect` zostawia czarne pasy na przyciskach).

## Dla programistów

### Pliki

| Ścieżka | Zawartość |
|---|---|
| `android/AndroidManifest.xml` | manifest na bazie szablonu Qt 6.11.2 (`androiddeployqt` wypełnia znaczniki `%%INSERT_…%%`: pakiet, wersję, nazwę, bibliotekę i uprawnienia modułów Qt): uprawnienia, `BrulionActivity`, odbiorniki, `AlarmActivity`, ikony |
| `android/res/` | teksty (`values` — polski, domyślny; `values-en`), ikona powiadomień `ic_stat_brulion`, ikony aplikacji (`mipmap-*` z logo, adaptacyjna `mipmap-anydpi` z tłem `#283045` i warstwą monochromatyczną), motyw ekranu alarmu |
| `android/src/io/github/januszbit/brulion/android/` | Kotlin: `BrulionAndroid` (fasada JNI), `BrulionActivity` (`QtActivity` + stuknięcia/uprawnienia/wybór dzwonka), `ImeInsets` (klawiatura ekranowa: ile widoku Qt zakrywa), `AlarmScheduler` + `WallClock`, `Reminder` + `ReminderStore`, `ReminderNotifier`, `ReminderSounds` (dźwięki: `SoundChoice`, wersje kanałów `SoundChannel`, `SoundStore`, próbka, okno wyboru dzwonka), `RingTimeout`, `Receivers` (fire, przyciski, boot/zmiana czasu), `AlarmActivity`, `Documents` (wybrane dokumenty i drzewa dokumentów do importu) |
| `android/tests/` | testy jednostkowe Kotlina (JVM) + skrypt init Gradle, który je dołącza |
| `android/lint.xml` | konfiguracja Android lint (wyjątki dla `res/values/libs.xml` generowanego przez Qt) |
| `android/settings.gradle`, `android/proguard-rules.pro` | R8 w buildach release i jego reguły (klasy wołane przez JNI) — [Rozmiar APK](#rozmiar-apk) |
| `android/res/xml/network_security_config.xml` | zaufane urzędy certyfikacji HTTPS: systemowe i zainstalowane przez użytkownika — [HTTPS i OpenSSL](#https-i-openssl) |
| `android/assets/licenses/` | teksty licencji dołączanych bibliotek (OpenSSL, Apache-2.0) |
| `src/platform/android/androidbridge.{h,cpp}` | C++: `brulion::platform::AndroidBridge` (kompilowany tylko gdy `ANDROID`) |
| `src/platform/android/selftest/` | `brulion_android_selftest` — APK testujący most na urządzeniu/emulatorze (tylko z `-DBRULION_ANDROID_SELFTEST=ON`) |
| `scripts/android-build.sh`, `scripts/android-new-keystore.sh` | budowanie + podpis, klucz wydań |
| `scripts/android-emulator-test.sh` | test end-to-end na emulatorze (Android 16, x86_64); `--keyboard`: pasek edycji na klawiaturze |
| `nix/android.nix`, `nix/android/` | APK z flake'a: derywacja, Qt for Android (`qt.nix`), podpisywanie (`sign.nix`), locki (`*.json`) i `update-locks.sh` |

`android/` to `QT_ANDROID_PACKAGE_SOURCE_DIR`: `androiddeployqt` kopiuje go na szablon Qt
w `<build>/src/app/android-build/`. Nie nadpisujemy `build.gradle` z Qt — szablon 6.11 używa
AGP 9 z wbudowaną obsługą Kotlina (`kotlin.srcDirs` obejmuje `src/`), więc pliki `.kt` w
`android/src` kompilują się bez dodatkowej konfiguracji; `androidx.core` (NotificationCompat)
też jest już w szablonie. Buildy release zmniejsza R8 (`android/settings.gradle`, [Rozmiar
APK](#rozmiar-apk)): klasy Kotlina wołane z C++ muszą leżeć w pakiecie
`io.github.januszbit.brulion.android`, który `android/proguard-rules.pro` zachowuje w całości.

### Most JNI (C++ ↔ Kotlin)

`AndroidBridge::instance()` (wątek GUI, po utworzeniu `QGuiApplication`) rejestruje metody
natywne klasy `io.github.januszbit.brulion.android.BrulionAndroid` i woła jej `attach()`.
Aplikacja powinna to zrobić w `main()` zaraz po utworzeniu `QGuiApplication`, przed
załadowaniem QML (żeby stuknięcie, które uruchomiło aplikację, trafiło do UI); dopóki nic nie
wywołuje `AndroidBridge`, linker pomija go w `libbrulion_<abi>.so`.

| C++ (`AndroidBridge`) | Kotlin (`BrulionAndroid`, `@JvmStatic`) |
|---|---|
| `bool hasAllFilesAccess()` / `void requestAllFilesAccess()` | `hasAllFilesAccess(Context): Boolean` / `requestAllFilesAccess(Context)` |
| `bool notificationPermission()` / `bool requestNotificationPermission()` | `notificationPermission(Context)` / `requestNotificationPermission(Context): Boolean` |
| `bool canScheduleExact()` / `void requestExactAlarms()` | `canScheduleExact(Context)` / `requestExactAlarms(Context)` |
| `bool canUseFullScreenIntent()` / `void requestFullScreenIntent()` | `canUseFullScreenIntent(Context)` / `requestFullScreenIntent(Context)` |
| `ScheduleResult scheduleReminders(const QJsonArray &)` | `scheduleReminders(Context, String): String` — `{"scheduled":n,"exact":b}` albo `{"error":…}` |
| `Activation takeActivation()` | `takeActivation(Context): String` — `{"path":…,"line":n}` albo `{}` |
| `static DocumentsResult documentInfo(const QString &uri)` | `documentInfo(Context, String): String` — `{uri, name, dir, size, virtual}` albo `{"error":…}` |
| `static DocumentsResult listDocuments(const QString &folderUri)` | `listDocuments(Context, String): String` — `{"documents":[…]}` albo `{"error":…}` |
| `QJsonObject setReminderSounds(const QJsonObject &)` | `setReminderSounds(Context, String): String` — `{"reminderChannel", "alarmChannel"}` albo `{"error":…}` |
| `bool pickSound(int kind, const QJsonObject &current)` | `pickSound(Context, Int, String): Boolean` — otwiera okno wyboru dzwonka (`startActivityForResult`) |
| `bool previewSound(const QJsonObject &)` | `previewSound(Context, String): Boolean` — próbka ~3 s |
| `void openSoundSettings(int kind)` | `openSoundSettings(Context, Int)` — `ACTION_CHANNEL_NOTIFICATION_SETTINGS` |
| sygnał `activated(QString path, int line)` | natywna `nativeActivated(String, Int)` |
| sygnał `permissionsChanged()` | natywna `nativePermissionsChanged()` + powrót aplikacji na pierwszy plan |
| sygnał `soundPicked(int kind, QJsonObject choice)` | natywna `nativeSoundPicked(Int, String)` (z `BrulionActivity.onActivityResult`) |
| sygnał `imeChanged(int overlap, bool shown, bool animating)` / `void requestImeState()` | natywna `nativeImeChanged(Int, Boolean, Boolean)` (z `ImeInsets`, piksele fizyczne, każda klatka wysuwania na Androidzie 11+) / `requestImeState()` — raport jeszcze raz |
| `void setSystemBarsLight(bool light)` | `setSystemBarsLight(Boolean)` — ikony pasków systemu ciemne nad jasnym oknem (Android 11+, okno pod paskami) |

* Element `scheduleReminders`: `{id: int, title, body, at: ms od epoki, alarm: bool,
  ringMinutes: int (alarmy, 1–60, domyślnie 10), path: ścieżka notatki w notatniku, line: int}`.
  `id` musi być stały między wywołaniami (ponowne nastawienie tego samego `id` zastępuje budzik).
  Wywołanie zastępuje cały zbiór; drzemki z powiadomienia zostają, dopóki aplikacja nie przyśle
  tego samego `id`. Błędny JSON nie kasuje nastawionych budzików.
* Dźwięki (`ReminderSounds.kt`, C++: `src/app/notify/androidpayload.h`): `kind` 0 = przypomnienia,
  1 = alarmy; wybór to `{mode: "default" | "custom" | "vibrate" | "silent", uri, title}`
  (`uri` — dzwonek z okna wyboru, `content://…`), `setReminderSounds` dostaje
  `{reminder: {…}, alarm: {…}}` przy starcie aplikacji i po każdej zmianie w ustawieniach.
  Okno wyboru zwraca przez `nativeSoundPicked` wybór albo `{"error": …}` (brak okna wyboru);
  anulowanie nie wysyła nic. Pozycja „Domyślny” okna wyboru daje `mode: "default"`.
* `documentInfo` / `listDocuments` (import): statyczne, wołane z wątku roboczego (JNI dołącza
  wątek sam), bez instancji mostu. Adres drzewa (`…/tree/<id>`, wynik `ACTION_OPEN_DOCUMENT_TREE`)
  jest zamieniany na dokument jego korzenia; dzieci folderu dają adresy `…/tree/<id>/document/<id
  dziecka>`, które otwiera `QFile`. `bridgeFailed` w wyniku (brak metody, wyjątek) = użyj silnika
  plików Qt.
* Metody przyjmują `Context` (to, co zwraca `QNativeInterface::QAndroidApplication::context()`,
  zwykle aktywność); okna dialogowe uprawnień korzystają z bieżącej `BrulionActivity`.
  Prośby (`request*`) tylko wysyłają okno/ekran ustawień do głównego wątku Androida i wracają od
  razu — wynik sprawdza się ponownie po `permissionsChanged()`.
* **Stuknięcie w powiadomienie** — wybrany wariant: wywołanie zwrotne JNI + odbiór przy starcie:
  * aplikacja działa: `BrulionActivity.onNewIntent` (tryb `singleTop`) → `nativeActivated` →
    sygnał `activated` w wątku Qt (połączenie kolejkowane);
  * zimny start z powiadomienia: `BrulionActivity.onCreate` zachowuje cel, bo C++ jeszcze nie
    słucha; konstruktor `AndroidBridge` po `attach()` odbiera go przez `takeActivation()`
    i emituje `activated` w pierwszym obrocie pętli zdarzeń (po podłączeniu UI);
  * zabezpieczenie: przy każdym powrocie aplikacji na pierwszy plan (`ApplicationActive`) most
    jeszcze raz sprawdza `takeActivation()`. Dostęp do celu chroni blokada po stronie Kotlina,
    więc żadne stuknięcie nie ginie ani nie jest dostarczone dwa razy; intencje odtworzone
    z „Ostatnich” są pomijane.
* `nativeActivated` jest wołana w głównym wątku Androida; C++ tylko kolejkuje emisję sygnału.
  Destruktor `AndroidBridge` woła `detach()` (czeka na blokadę), dopiero potem zeruje wskaźnik.

### Testy i diagnostyka

* Testy jednostkowe Kotlina (czysta logika: przesuwanie czasu ściennego między strefami, czas
  dzwonienia, JSON przypomnień, dźwięki — wybór z C++, odpowiedź okna wyboru dzwonka, wersje
  kanałów i usuwanie starych — z polskimi znakami; ile widoku zakrywa klawiatura — okno
  edge-to-edge, zmniejszone, podzielony ekran, klatki wysuwania: `ImeInsetsTest`):
  `BRULION_ANDROID_UNIT_TESTS=1 scripts/android-build.sh` (uruchamia
  `./gradlew --init-script android/tests/unit-tests.gradle testDebugUnitTest` w wygenerowanym
  projekcie Gradle).
* Android lint: `BRULION_ANDROID_LINT=1 scripts/android-build.sh` (`./gradlew lintRelease`,
  wypisuje uwagi dotyczące plików Brulionu; pełny raport w
  `<build>/src/app/android-build/build/reports/`).
* Test end-to-end na emulatorze (wymaga KVM; na NixOS emulator działa przez `steam-run`):

  ```sh
  BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_SELFTEST=1 scripts/android-build.sh
  scripts/android-emulator-test.sh     # logi i zrzuty ekranu: dist-android/emulator-test/
  ```

  Instaluje emulator i obraz `system-images;android-36;google_apis;x86_64` (rysowanie programowe
  `-gpu swangle_indirect`; `swiftshader_indirect` zostawia czarne pasy na przyciskach), uruchamia APK
  aplikacji (czy działa, kanały powiadomień), potem APK testowy mostu: uprawnienia przez JNI,
  dwa razy `scheduleReminders` (przypomnienie za 45 s, alarm za 75 s dzwoniący 1 min,
  przeszłe — pominięte), budziki w `dumpsys alarm`, powiadomienia na kanałach `reminders`
  i `alarms`, zamiana alarmu w „Przegapiony alarm” po czasie dzwonienia, stuknięcia
  (zimny start i działająca aplikacja, ścieżka z polskimi znakami) dochodzące do C++.
  APK testowy ma ten sam pakiet co aplikacja (zastępuje ją) — tylko na emulatorze/telefonie
  testowym.
* Zaznaczanie i kopiowanie prawdziwym palcem (`adb shell input`, APK debugowalny jak niżej):
  ```sh
  scripts/android-emulator-test.sh --selection
  ```
  Przytrzymanie tekstu bloku w dzienniku — tryb zaznaczania (klawiatura się nie pokazuje, pasek z
  bloku, uchwyty, narzędzia zaznaczenia na dole), stuknięcie w kolejny blok rozszerza zaznaczenie,
  „Kopiuj” (komunikat „Skopiowano 2 bloki”, a Android 13+ pokazuje własny podgląd schowka z
  Markdownem), Wstecz kończy zaznaczanie; potem „Zaznacz tekst” na stronie (`route.open`
  `selectText`): przytrzymanie zaznacza słowo, a **Android sam pokazuje swoje uchwyty i menu
  „Copy / Select all” także dla tekstu tylko do odczytu** (Qt 6.11; własne uchwyty widoku są więc
  tylko w przeglądarce i w dotykowym interfejsie na komputerze) — „Copy” z menu systemu przychodzi
  do aplikacji jako Ctrl+C (dokładny tekst, komunikat „Skopiowano zaznaczony tekst”), uchwyt
  systemu przeciągnięty rozszerza zaznaczenie, „Kopiuj” w pasku widoku, Wstecz zamyka widok.
  Zrzuty `sel-*.png` w `dist-android/emulator-test/`; sprawdzone 2026-10-23 (Android 16, Pixel 6).
* Klawiatura ekranowa na emulatorze (APK debugowalny — tylko takiemu Qt przekazuje argumenty
  i zmienne środowiska z intencji):

  ```sh
  BRULION_ANDROID_ABIS=x86_64 BRULION_ANDROID_BUILD_TYPE=Debug scripts/android-build.sh
  scripts/android-emulator-test.sh --keyboard
  BRULION_EMULATOR_IMAGE="system-images;android-30;google_apis;x86_64" \
    scripts/android-emulator-test.sh --keyboard     # starszy Android
  ```

  Otwiera przykładowy notatnik (`--demo`), stuka palcem w blok dzisiejszego dziennika, czeka na
  klawiaturę (`dumpsys input_method`) i porównuje górną krawędź klawiatury (źródło wstawek `ime`
  w `dumpsys window displays`) z wartością zgłoszoną przez aplikację (±2 dp); na Androidzie 11+
  sprawdza, że okno nie zmieniło rozmiaru, a wysuwanie przyszło klatka po klatce; przewija
  dziennik palcem, chowa klawiaturę przyciskiem Wstecz i sprawdza w logu, że pasek został tuż nad
  paskiem nawigacji systemu, potem to samo na stronie przewodnika (otwartej jak ze stuknięcia w
  powiadomienie), na koniec zrzut w ciemnym motywie (ikony pasków systemu). Zrzuty ekranu
  (`keyboard-*.png`) i logi w `dist-android/emulator-test/`.
* Skrypt po zbudowaniu sprawdza: podpis (`apksigner verify`), pakiet i wszystkie wymagane
  uprawnienia (`aapt2 dump badging`), komponenty w scalonym manifeście (`aapt2 dump xmltree`),
  klasy Kotlina i Javy Qt (`QtNative`, `QtActivityBase`, `QtLoader`) oraz metody androidx
  wołane przez Qt (`getUriForFile`, `fetchFonts` — reguły R8) w `classes.dex`,
  `lib/<abi>/libbrulion_<abi>.so`, OpenSSL (`libssl_3.so`, `libcrypto_3.so` dla każdego ABI),
  brak wtyczek `qmltooling` w Release, a także zgodność ze
  **stronami pamięci 16 KB** (Android 15+; telefony z takimi stronami nie wczytają innych
  bibliotek): `zipalign -c -P 16` i wyrównanie segmentów `LOAD` 64-bitowych bibliotek
  (`llvm-readelf`) — źle wyrównana `libbrulion_<abi>.so` przerywa build (NDK r27 dostaje
  `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`; tak samo OpenSSL), biblioteki Qt są tylko
  zgłaszane.
* Logi: `adb logcat -s BrulionAndroid:* brulion.android:* Qt:*`.
* Nastawione budziki: `adb shell dumpsys alarm | grep -B2 -A6 io.github.januszbit.brulion`.
* Test ponownego nastawiania bez restartu (odbiornik nie jest eksportowany, więc potrzebny jest
  `adb root`, np. emulator bez Google Play):
  `adb shell am broadcast -a android.intent.action.BOOT_COMPLETED -p io.github.januszbit.brulion`.

### Uwaga o dystrybucji

Google Play ogranicza uprawnienia `MANAGE_EXTERNAL_STORAGE` (dostęp do wszystkich plików)
i `USE_EXACT_ALARM` do wybranych kategorii aplikacji. Brulion jest przeznaczony do instalacji
z pliku APK (np. z wydań na GitHubie) lub z F-Droid.
