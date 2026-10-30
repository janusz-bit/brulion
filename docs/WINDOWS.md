# Brulion na Windows (wersja testowa)

Od wersji 0.4.5 Brulion ma **testową** wersję na Windows 10 i 11 (x86_64): ten sam program co na
Linuksie (C++20 + Qt 6.11.2, Qt Quick), zbudowany kompilatorem MSVC 2022. Jest w każdym wydaniu
w dwóch postaciach:

| Plik | Co to jest |
|---|---|
| `Brulion-<wersja>-windows-x86_64-setup.exe` | instalator (Inno Setup) — instalacja dla bieżącego użytkownika, bez uprawnień administratora |
| `brulion-<wersja>-windows-x86_64.zip` | wersja przenośna — folder `Brulion` z `brulion.exe`, `brulion-cli.exe`, bibliotekami Qt i środowiskiem MSVC |

„Testowa” znaczy: buduje się i przechodzi testy w CI, ale nie była jeszcze długo używana na co dzień,
pliki nie są podpisane i ma ograniczenia opisane niżej. Od wersji 0.5.0 przypomnienia i alarmy są
natywnymi powiadomieniami Windows z przyciskami, a Brulion może się uruchamiać po zalogowaniu. Błędy zgłaszaj w
[issues](https://github.com/janusz-bit/brulion/issues) (z rejestrem zdarzeń — README, „Gdy coś nie
działa”).

## Instalacja

* **Instalator:** uruchom `Brulion-<wersja>-windows-x86_64-setup.exe`. Program trafia do
  `%LOCALAPPDATA%\Programs\Brulion`, skrót do menu Start, ikona na pulpicie na życzenie (zadanie
  „Utwórz ikonę na pulpicie”) i — tylko przy pierwszej instalacji, zaznaczone — „Uruchamiaj Brulion
  po zalogowaniu” (niżej, „Autostart”). Brulion pojawia się też w **Otwórz za pomocą** dla plików
  `.md`, `.markdown` i `.txt`, bez zmiany programu domyślnego (niżej, „Otwórz za pomocą”). Język
  instalatora: polski albo angielski (według systemu).
  Nowa wersja instaluje się na poprzednią (ten sam `AppId`); uruchomiony Brulion zostanie poproszony
  o zamknięcie.
* **SmartScreen:** pliki nie są podpisane certyfikatem, więc Windows ostrzega przy pierwszym
  uruchomieniu: „System Windows ochronił ten komputer” → **Więcej informacji** → **Uruchom mimo to**.
  Sumy SHA-256 są w `SHA256SUMS.txt` wydania.
* **Wersja przenośna:** rozpakuj ZIP w dowolnym miejscu i uruchom `Brulion\brulion.exe`. Nic się
  nie instaluje; notatki i ustawienia są w tych samych miejscach co przy instalatorze (niżej), nie
  w folderze programu.
* **Odinstalowanie:** Ustawienia → Aplikacje → Brulion → Odinstaluj (albo `unins000.exe` w folderze
  programu). Usuwa tylko pliki programu, skróty, wpis autostartu tej instalacji, rejestrację
  powiadomień i „Otwórz za pomocą” — **notatki, ustawienia i rejestr zdarzeń zostają**.
* Instalacja bez okien (np. skrypt): `Brulion-<wersja>-windows-x86_64-setup.exe /VERYSILENT
  /SUPPRESSMSGBOXES /CURRENTUSER` (z autostartem: `/TASKS=autostart`, bez: `/TASKS=""`),
  odinstalowanie: `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES`. Administrator może zainstalować dla
  wszystkich (`/ALLUSERS`, do `C:\Program Files\Brulion`; „Otwórz za pomocą” wtedy dla wszystkich);
  autostart i rejestracja powiadomień są wtedy i tak wpisami użytkownika, który instaluje (każdy
  inny włącza autostart w ustawieniach).

## Gdzie są dane

Ścieżki z `QStandardPaths` i `QSettings` (organizacja i aplikacja „Brulion”):

| Co | Gdzie |
|---|---|
| Notatki (domyślny nowy notatnik) | `Dokumenty\Brulion` — albo dowolny wybrany folder (np. folder Syncthing) |
| Ustawienia (lista notatników, język, motyw…) | `%APPDATA%\Brulion\Brulion.ini` |
| Dane aplikacji: przykładowy notatnik, „Dokumentacja Brulionu”, historia wersji i stan urządzenia notatników, hasła Nextcloud | `%APPDATA%\Brulion\Brulion\` |
| Rejestr zdarzeń (`brulion.log`) | `%LOCALAPPDATA%\Brulion\Brulion\State\logs\` |
| Pamięć podręczna (obrazy z internetu) | `%LOCALAPPDATA%\Brulion\Brulion\cache\` |
| Program (instalator) | `%LOCALAPPDATA%\Programs\Brulion\` |
| Jedna kopia programu (single instance) | potok nazwany `\\.\pipe\<%TEMP%>/brulion-<skrót>` i plik blokady `%TEMP%\brulion-<skrót>.lock` |
| Rejestracja powiadomień (nazwa i ikona toastów) | rejestr: `HKCU\Software\Classes\AppUserModelId\JanuszBit.Brulion` (`DisplayName`, `IconUri` → `brulion.ico` obok programu) |
| Autostart | rejestr: wartość `Brulion` w `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; wyłączenie w Menedżerze zadań: `…\Explorer\StartupApproved\Run\Brulion` |
| „Otwórz za pomocą” (instalator) | rejestr: `HKCU\Software\Classes\JanuszBit.Brulion.Note`, wartości `JanuszBit.Brulion.Note` w `…\Classes\.md`, `.markdown`, `.txt` `\OpenWithProgids`, `…\Classes\Applications\brulion.exe` (niżej, „Otwórz za pomocą”) |

`%APPDATA%` to zwykle `C:\Users\<użytkownik>\AppData\Roaming`, `%LOCALAPPDATA%` —
`…\AppData\Local`, `%TEMP%` — `…\AppData\Local\Temp`.

**Osobny profil:** zmienna środowiskowa `BRULION_PROFILE=<nazwa>` (litery, cyfry, `-`, `_`, do 32
znaków) zamienia organizację „Brulion” na „Brulion-<nazwa>” — ustawienia, dane, rejestr i gniazdo
jednej kopii są wtedy osobne (`%APPDATA%\Brulion-<nazwa>\…`). Tak samo dla `brulion.exe`
i `brulion-cli.exe`. Służy testom (na Windows zmienne `XDG_*` z testów linuksowych nic nie
zmieniają) i do wypróbowania czegoś bez ruszania własnego profilu; działa też na Linuksie.

## Otwórz za pomocą

Instalator dodaje Brulion do **Otwórz za pomocą** dla plików `.md`, `.markdown` i `.txt`: prawy
przycisk na pliku w Eksploratorze → **Otwórz za pomocą** → **Brulion** (albo **Wybierz inną
aplikację**, jeśli go tam jeszcze nie ma). Otwiera się okno **Zapisz w Brulionie** — to samo co
„Udostępnij → Brulion” na Androidzie: plik trafia do **dzisiejszego dziennika** (jako blok) albo
jako **nowa strona** (tytuł z nazwy pliku, treść bez zmian). Brulion zapisuje **kopię** w otwartym
notatniku, a sam plik zostaje na miejscu, nietknięty; **Anuluj** niczego nie zapisuje. Plik, który
już jest stroną otwartego notatnika, po prostu się otwiera. Kilka zaznaczonych plików trafia do
jednego okna (Windows uruchamia Brulion osobno dla każdego pliku, a działająca kopia je zbiera).

* **Program domyślny się nie zmienia.** Instalator tylko proponuje Brulion: nie zapisuje
  domyślnego programu typu ani wyboru użytkownika (`UserChoice`). Po instalacji Windows może raz
  zapytać, jak otwierać pliki `.txt` — wybierz dotychczasowy program (np. **Notatnik**), a wszystko
  zostanie jak było. Brulion jako domyślny ustawisz sam: **Otwórz za pomocą → Wybierz inną
  aplikację → Brulion → Zawsze**.
* **Wersja przenośna (ZIP)** niczego nie rejestruje: tam zostaje **Wybierz inną aplikację →
  Wybierz aplikację na tym komputerze** i `brulion.exe` (Windows wywoła go wtedy bez `--open`, więc
  Brulion tylko się otworzy) — albo instalator.
* Bez otwartego notatnika (pierwsze uruchomienie) Brulion najpierw prosi o otwarcie albo
  utworzenie notatnika; plik czeka.

Co zapisuje instalator (dla bieżącego użytkownika w `HKCU\Software\Classes`, z `/ALLUSERS`
w `HKLM\Software\Classes`; odinstalowanie usuwa dokładnie to):

| Klucz | Zawartość |
|---|---|
| `JanuszBit.Brulion.Note` | typ „Notatka (Brulion)”: ikona i polecenie `"…\brulion.exe" --open "%1"` |
| `.md\OpenWithProgids`, `.markdown\OpenWithProgids`, `.txt\OpenWithProgids` | wartość `JanuszBit.Brulion.Note` (pusta) — Brulion na liście „Otwórz za pomocą” |
| `Applications\brulion.exe` | `FriendlyAppName` „Brulion”, `SupportedTypes` (`.md`, `.markdown`, `.txt`), to samo polecenie |

`--open` w wierszu poleceń: `brulion.exe --open "C:\ścieżka\notatka.md" …` — wszystko po `--open`
to pliki (także nazwa zaczynająca się od `-`); ścieżki względne liczą się od bieżącego folderu.

## Powiadomienia

Przypomnienia ⏰ i alarmy ⏰! są powiadomieniami Windows (toastami; `src/app/notify/windowstoastbus.h`,
C++/WinRT, Windows 10 1809 lub nowszy):

* **Przypomnienie** pokazuje tekst przypomnienia, pod nim stronę i termin, z przyciskami
  **„Drzemka 10 min”** (długość z Ustawień → Przypomnienia), przy zadaniu **„Zrobione”** (zadanie
  oznaczone jako zrobione, `[x]` z `✅`, bez otwierania notatki) i **„OK”**. Kliknięcie w powiadomienie
  otwiera notatkę na tej linii i wyciąga okno Brulionu na wierzch. Na ekranie zostaje ok. 25 s,
  potem czeka w Centrum akcji (tam też działa, dopóki Brulion jest uruchomiony).
* **Alarm** (scenariusz „alarm” Windows) zostaje na ekranie, aż odpowiesz, a jego dźwięk się
  powtarza przez czas dzwonienia alarmu (domyślnie 10 min); potem powiadomienie zostaje, już
  ciche — jak na Linuksie. Zapętlony dźwięk (Alarm 1–10, Połączenie 1–10, domyślny dźwięk alarmu)
  gra samo powiadomienie; krótki dźwięk (np. Poczta) Brulion odtwarza ponownie co 30 s, pokazując
  powiadomienie jeszcze raz.
* **Dźwięki** (Ustawienia → Przypomnienia): „Domyślny” to dźwięk powiadomień Windows (alarm:
  zapętlony dźwięk alarmu Windows), „Dzwonek” — jeden z dźwięków powiadomień Windows (Powiadomienie,
  Wiadomość błyskawiczna, Poczta, Przypomnienie, SMS, Alarm 1–10, Połączenie 1–10), „Cisza” — bez
  dźwięku. Własnych plików dźwiękowych powiadomienia programu bez pakietu MSIX odtwarzać nie mogą
  (pozycja „Plik dźwiękowy…” jest wyłączona; plik wybrany na Linuksie gra jako dźwięk domyślny).
  „Odtwórz próbkę” pokazuje krótkie powiadomienie z wybranym dźwiękiem.
* Nazwę i ikonę powiadomień daje rejestracja `JanuszBit.Brulion` (AppUserModelID), którą Brulion
  zapisuje przy każdym starcie (`src/platform/appidentity.h`) — także wersja przenośna, bez skrótu
  w menu Start; skrót instalatora ma ten sam identyfikator (grupowanie na pasku zadań).
* Przy „Zakończ” Brulion usuwa swoje powiadomienia z Centrum akcji: kliknięcie w nie nie miałoby już
  dokąd trafić. Przypomnienia, które minęły przy zamkniętym programie, czekają w dzienniku
  (i wracają przy następnym starcie).
* **Wyłączone powiadomienia:** gdy w Ustawieniach Windows → System → Powiadomienia Brulion (albo
  wszystkie powiadomienia) jest wyłączony, przypomnienia pokazuje dymek ikony w zasobniku, jak
  wcześniej — bez przycisków. Tak samo, gdy identyfikator `JanuszBit.Brulion` nie jest
  zarejestrowany (wtedy Windows przyjąłby powiadomienie i go nie pokazał). Zanim Brulion pokaże
  pierwsze powiadomienie, Windows nie zna jeszcze jego ustawienia — liczy się to jako włączone. **„Nie przeszkadzać” / Asystent skupienia** chowa powiadomienia od
  razu do Centrum akcji bez dźwięku; alarmy przebiją się tylko wtedy, gdy Brulion jest na liście
  priorytetowej.
* Przypomnienia odlicza uruchomiony Brulion — zamknięcie okna zostawia go w zasobniku (ustawienie
  „Działaj dalej w zasobniku systemowym”, domyślnie włączone; „Zakończ” w menu ikony go zamyka).

## Autostart

**Ustawienia → Aplikacja → „Uruchamiaj po zalogowaniu”** (albo zadanie instalatora): Windows
uruchamia Brulion po zalogowaniu ukryty w zasobniku (`brulion.exe --hidden`), więc przypomnienia
dzwonią od początku sesji. To wartość `Brulion` klucza
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (`src/platform/autostart.h`):

* Widać ją w **Menedżerze zadań → Aplikacje autostartu**; wyłączona tam pokazuje się w Brulionie
  jako wyłączona, a włączona ponownie w Brulionie znosi wyłączenie z Menedżera zadań.
* Gdy Brulion już działa, uruchomienie z `--hidden` nie pokazuje okna (druga kopia tylko się kończy).
* **Wersja przenośna:** wpis wskazuje ten `brulion.exe`, z którego go włączono. Gdy folder
  przeniesiesz, przy następnym ręcznym starcie wpis sam wskaże nowe miejsce (o ile pod starą
  ścieżką nie ma już programu — wpisu innej, istniejącej kopii Brulion nie rusza).
* Z osobnym profilem (`BRULION_PROFILE`) przełącznik jest wyłączony: wpis autostartu nie ustawi
  zmiennej środowiskowej, więc uruchomiłby profil domyślny.
* Zadanie instalatora jest tylko przy pierwszej instalacji: aktualizacja zostawia autostart taki,
  jak go ustawiono w Brulionie albo w Menedżerze zadań.
* Odinstalowanie usuwa wpis, gdy wskazuje tę instalację.

## Kalendarz z zasobnika

Kliknięcie ikony Brulionu w zasobniku systemowym (obok zegara) otwiera **kalendarz**, podobny do
tego, który Windows 7 i 10 pokazują po kliknięciu zegara (`ShellTrayCalendar.qml`):

* małe okno tuż nad ikoną (przy pasku zadań u góry albo z boku — przy tej krawędzi), zawsze na
  wierzchu, bez przycisku na pasku zadań: dzisiejsza data, miesiąc z kropkami w dniach z wpisami
  w dzienniku, terminami 📅, zaplanowanymi ⏳ i przypomnieniami ⏰ oraz lista zadań i przypomnień
  wybranego dnia;
* kliknięcie zadania albo „Otwórz dziennik” chowa kalendarz i pokazuje ten dzień w oknie Brulionu;
  na dole **„Otwórz Brulion”** i **„Szybka notatka…”**;
* kalendarz znika po kliknięciu gdziekolwiek indziej, po Escape, po ponownym kliknięciu ikony
  i po Alt+F4 — Brulion działa dalej w zasobniku;
* bez otwartego notatnika pokazuje sam miesiąc i prośbę o otwarcie notatnika.

**Ustawienia → Aplikacja → „Kliknięcie ikony w zasobniku”**: **Kalendarz** (domyślnie na Windows)
albo **Okno** (pokazuje lub chowa okno Brulionu, jak przed wersją z kalendarzem). Podwójne kliknięcie
zawsze pokazuje okno, a kalendarz jest też w menu ikony (prawy przycisk → **Kalendarz**).

**Kalendarza zegara Windows Brulion nie zastępuje — i nie da się tego zrobić porządnie.** Okienko
zegara należy do powłoki systemu (Eksplorator, w Windows 10 `ShellExperienceHost`, w Windows 11
centrum powiadomień): nie ma publicznego API, które pozwalałoby je podmienić, rozszerzyć albo
dopisać do niego wpisy. Windows 10 pokazuje w nim tylko wydarzenia kont aplikacji Kalendarz
(Outlook, Google…) — program bez pakietu MSIX nie może się tam dopisać — a Windows 11 tej listy
już nie ma. Przełącznik w rejestrze, który przywracał kalendarz z Windows 7
(`UseWin32TrayClockExperience`), przestał działać we wczesnych wersjach Windows 10. Programy, które
zmieniają zegar (ExplorerPatcher, mody Windhawk, ElevenClock, T-Clock), wstrzykują kod do
Eksploratora albo przykrywają go własnym oknem: psują się po aktualizacjach Windows, antywirusy je
zgłaszają, a niepodpisany program tym bardziej nie powinien tego robić.

Można za to używać kalendarza Brulionu zamiast systemowego:

1. Ukryj zegar systemowy — Windows 10: Ustawienia → Personalizacja → Pasek zadań → „Włącz lub
   wyłącz ikony systemowe” → Zegar; Windows 11: Ustawienia → Czas i język → Data i godzina →
   „Pokaż godzinę i datę w zasobniku systemowym” (nowsze wydania).
2. Przypnij ikonę Brulionu na stałe w zasobniku — Ustawienia paska zadań → „Inne ikony zasobnika
   systemowego” (Windows 11) albo „Wybierz ikony wyświetlane na pasku zadań” (Windows 10) → Brulion
   włączony.
3. Włącz „Uruchamiaj po zalogowaniu” (wyżej), żeby ikona była od początku sesji.

Na Linuksie kalendarz działa tak samo (menu ikony → **Kalendarz**; kliknięcie ikony domyślnie
pokazuje okno, ustawienie jak wyżej). Pod X11 okienko pojawia się przy kursorze; pod Waylandem
okna nie da się ustawić samemu — kompozytor kładzie je, gdzie chce (zwykle na środku), i może nie
oddać mu aktywności: wtedy nie znika po kliknięciu obok i nie dostaje klawiatury (Escape do niego
nie trafia) — zamkniesz je przyciskiem **×** w jego rogu, wybierając wpis albo **Otwórz Brulion**,
albo ponownym kliknięciem ikony.

## Ograniczenia wersji testowej

* **Niepodpisane pliki** (SmartScreen, wyżej) i **brak automatycznych aktualizacji** — nową wersję
  instaluje się instalatorem na poprzednią.
* **Powiadomienia działają, dopóki Brulion jest uruchomiony** (bez aktywatora COM): kliknięcie
  w powiadomienie po zamknięciu programu nic by nie zrobiło, dlatego „Zakończ” je usuwa (wyżej).
* **Wiersz poleceń:** `brulion-cli.exe` (obok `brulion.exe`) działa jak na Linuksie
  (docs/CLI.md). `brulion.exe cli …` też działa, ale `brulion.exe` jest programem okienkowym —
  w konsoli nie widać jego wyjścia; w skryptach i dla agentów AI używaj `brulion-cli.exe`.
  Zdalne sterowanie aplikacją (`--remote-control`, `brulion-cli app …`) działa przez potok
  nazwany, ale jego test end-to-end jest tylko linuksowy (niżej).
* **Nazw folderów notatnika nie zmienia się w Eksploratorze, gdy Brulion jest uruchomiony.**
  Brulion obserwuje foldery notatnika (`QFileSystemWatcher` trzyma otwarty uchwyt każdego z nich),
  a Windows nie przenosi folderu, w którym coś jest otwarte — Eksplorator zgłasza, że folder jest
  używany. Tak samo Syncthing, gdy przenosi folder przemianowany na innym urządzeniu: zgłosi błąd
  i spróbuje ponownie. W Brulionie (pasek boczny, „Zmień nazwę”, „Przenieś do…”) działa normalnie;
  usuwanie folderów i wszystko, co inne programy robią z plikami (Syncthing, edytory), też.
  Folder przemianowany na innym urządzeniu albo w Nextcloudzie synchronizacja Nextcloud
  przemianowuje tu sama; gdy inny program trzyma otwarty plik z tego folderu, Windows na to nie
  pozwala i notatki są przenoszone pojedynczo, a ta otwarta jest pobierana pod nową nazwą (stara
  trafi do kosza, gdy program ją zwolni) — nic nie ginie.
* **Nazwy, których Windows nie dopuszcza:** notatka z innego urządzenia o nazwie ze znakiem
  `< > : " \ | ? *` (albo `CON`, `NUL`, `AUX`… czy kropką na końcu) nie zapisze się na Windows —
  synchronizacja zgłasza ją jako błąd przy każdym przebiegu, aż nazwa zostanie zmieniona na innym
  urządzeniu.
* **Notatki otwartej w innym programie bez współdzielenia** (np. zablokowanej przez Worda) Brulion
  nie zapisze, dopóki ten program ją trzyma: Windows nie podmienia pliku otwartego bez
  `FILE_SHARE_DELETE`, a Brulion zapisuje atomowo (nowy plik w miejsce starego). Zamknij ją tam,
  zanim zmienisz ją w Brulionie; synchronizacja ponawia taki plik przy następnym przebiegu.
* Dowiązania symboliczne i punkty połączenia (junction) w notatniku nie są testowane na Windows.
* Tylko x86_64 (bez Windows na ARM) i tylko Windows 10/11.
* Kalendarz z zasobnika nie zastępuje kalendarza zegara Windows (wyżej, „Kalendarz z zasobnika”).
  Jego położenie przy ikonie (pasek zadań z każdej strony, kilka monitorów) i znikanie po kliknięciu
  obok czekają na sprawdzenie na prawdziwym Windows.

## Budowanie

### W CI

`.github/workflows/windows.yml` (runner `windows-2022`): Qt 6.11.2 `win64_msvc2022_64` z aqt
(`jurplel/install-qt-action`, w cache; aqtinstall z przypiętego commita gałęzi master, bo wydanie
3.3.0 nie zna układu repozytorium Qt 6.11 dla Windows) razem z modułem **Qt PDF** (`modules:
qtpdf`, rozszerzenie repozytorium Qt: strony PDF jako podkład rysunku, `src/platform/
pdfpages_qtpdf.cpp`; `windeployqt` dokłada `Qt6Pdf.dll`, ok. 5 MB), MSVC z `ilammy/msvc-dev-cmd`, ccache
z Chocolatey (pamięć podręczna kompilacji w cache Actions, zapisywana zaraz po budowaniu), CMake +
Ninja, Release, potem `scripts/windows-build.ps1` krok po kroku: build, testy, paczki, sprawdzenie
paczek → artefakt `brulion-windows` (ZIP + instalator). Uruchamia się jak `appimage.yml`: ręcznie
(`gh workflow run windows.yml --ref main`), przy pushu na `main` ze znacznikiem `[ci]` (gdy zmienia
`src/`, `tests/`, `packaging/windows/`…) i z `release.yml`, który dołącza oba pliki do wydania
(i do `SHA256SUMS.txt`) — wydanie publikuje się tylko, gdy przejdzie także Windows.

### Lokalnie

Potrzebne: Visual Studio 2022 z obciążeniem „Programowanie aplikacji klasycznych w C++” (ma CMake
i Ninja), Qt 6.11.2 dla MSVC (instalator Qt albo aqt nowszy niż 3.3.0:
`aqt install-qt windows desktop 6.11.2 win64_msvc2022_64 -O C:\Qt -m qtpdf`; bez `qtpdf` Brulion
buduje się bez PDF-ów jako podkładu rysunku), Inno Setup 6 (`winget install JRSoftware.InnoSetup`). Klon z Git for Windows
zostawia końce linii LF dzięki `.gitattributes` (`* -text`) — testy porównują bajty. W PowerShellu
w katalogu repozytorium:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64
# tylko wybrane kroki:
scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64 -Steps build,test
```

Bez środowiska MSVC (brak `cl.exe` w `PATH`) skrypt sam je wczytuje (`vswhere` + `vcvars64.bat`);
`ccache` w `PATH` (4.6 lub nowszy) jest używany sam.
Kroki:

1. **build** — `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` w `build-windows`. MSVC dostaje
   `/utf-8` (polskie napisy w kodzie), `/permissive-`, `/Zc:__cplusplus`, `/bigobj` i ostrzeżenia
   `/W4` bez `/WX` (CMakeLists.txt, `cmake/BrulionWarnings.cmake`); `brulion.exe`
   i `brulion-cli.exe` mają ikonę `packaging/windows/brulion.ico` i wersję w zasobach
   (`packaging/windows/brulion.rc.in`, `cmake/BrulionWindows.cmake`). Powiadomienia używają
   nagłówków C++/WinRT z Windows SDK (`<winrt/Windows.UI.Notifications.h>`, folder `cppwinrt`, który
   środowisko MSVC ma w `INCLUDE`) i `WindowsApp.lib`; nic dodatkowego się nie instaluje.
2. **test** — `ctest` offscreen (`QT_QPA_PLATFORM=offscreen`) z Qt w `PATH`.
3. **package** — do `build-windows\package\Brulion`: `brulion.exe`, `brulion-cli.exe`,
   `windeployqt --release --qmldir src\app\qml --no-translations --no-opengl-sw` (biblioteki Qt,
   wtyczki — platformy, formaty obrazów z SVG, TLS przez Schannel bez OpenSSL — i moduły QML), do
   tego `platforms\qoffscreen.dll` (uruchomienia bez ekranu: sprawdzenie paczek, zrzuty) oraz
   biblioteki MSVC (`Microsoft.VC143.CRT`, obok programu — bez instalowania Visual C++
   Redistributable). Z tego folderu: `dist-windows\brulion-<wersja>-windows-x86_64.zip` i
   instalator z `packaging/windows/brulion.iss` →
   `dist-windows\Brulion-<wersja>-windows-x86_64-setup.exe`.
4. **check** — bez Qt w `PATH` i bez zmiennych Qt: ZIP rozpakowany do nowego folderu, potem
   instalator zainstalowany po cichu dla bieżącego użytkownika z zadaniem `autostart` (wartość
   `Run\Brulion` musi uruchamiać zainstalowany `brulion.exe --hidden`); z obu `brulion-cli.exe
   --version` i autotest powłoki prawdziwej aplikacji (`brulion.exe --demo --route journals
   --selftest-shell --no-single-instance …`, offscreen, profil `BRULION_PROFILE=package-check`) —
   brak biblioteki albo modułu QML go oblewa; każde uruchomienie musi zarejestrować identyfikator
   powiadomień profilu (`AppUserModelId\JanuszBit.Brulion.package-check`, ikona obok programu),
   a wersja przenośna w osobnym profilu nie może dopisać autostartu; potem ten sam instalator
   jeszcze raz po cichu jako aktualizacja, po wyłączeniu autostartu — nie może go włączyć z powrotem;
   na koniec ciche odinstalowanie, po którym ustawienia profilu muszą zostać, a wpisu autostartu ma
   nie być.

`AppId` instalatora (`{5E3B8C1A-7F42-4D6B-9A0E-2C8D4B6F1E37}`) jest stały — po nim Windows poznaje
zainstalowanego Brulionu; nie zmieniać.

### Testy na Windows

Te same testy co na Linuksie (`QTEST`, testy QML prawdziwej aplikacji, `brulion-cli` jako proces),
z wyjątkami:

* nie są budowane/uruchamiane: testy skryptów bash (`tests/scripts`, `qml-smoke.sh` w
  `tst_content_qml`, `tst_planning_qml`, `tst_appio_qml`, `scroll-jumps.sh` w
  `tst_scroll_jumps_qml`) i `tst_remote_control_qml` (jego prywatny katalog domowy to foldery
  `XDG_*`, gniazda w katalogu uruchomieniowym i uprawnienia uniksowe);
* pomijane (`QSKIP` z powodem): w `tst_ipc_singleinstance` i `tst_ipc_control` przypadki
  uprawnień folderów, dowiązań symbolicznych i porzuconego pliku gniazda — na Windows gniazdo to
  potok nazwany z listą dostępu użytkownika, bez pliku;
* pomijane według możliwości systemu plików (`tests/common/filesystemtraits.h`): przypadki
  z **dowiązaniami symbolicznymi** (`QFile::link()` robi na Windows skrót `.lnk`) — w `tst_vault`,
  `tst_vault_edge`, `tst_vault_safety`, `tst_shell_settings`, `tst_shell_nextcloud`; z
  **uprawnieniami uniksowymi** (folder bez prawa odczytu, plik tylko dla właściciela; Windows ma
  tylko atrybut „tylko do odczytu”, a resztę załatwiają listy dostępu profilu) — w
  `tst_vault_safety`, `tst_sync_engine`, `tst_sync_folders` (`renameHereRefusedFallsBack`: folder
  tylko do odczytu, którego synchronizacja nie może przemianować), `tst_log_logger`,
  `tst_shell_settings`; z **dwoma
  plikami różniącymi się tylko wielkością liter** (NTFS ich nie odróżnia) — w `tst_vault`,
  `tst_notebook_journals`, `tst_shell_workspace`, `tst_sync_folders` (tam na takim systemie plików
  test sprawdza zamiast tego, że folder, któremu inne urządzenie zmieniło wielkość liter, dostaje
  tu tę samą nazwę bez przesyłania i bez kosza);
* w `tst_vault_watch`: `watcherDetectsFolderRenames` (inny program nie zmieni na Windows nazwy
  obserwowanego folderu) i koniec `inPlaceRewriteOfAnOpenFileIsReportedAtOnce` (zapis notatki,
  którą inny program trzyma otwartą) — oba opisane wyżej w „Ograniczeniach”;
* tylko na Windows: `tst_windowstoastbus` — prawdziwe powiadomienia pod testowym identyfikatorem
  (pokazane, zastąpione, zamknięte, wyczyszczone); gdy powiadomienia są na danym komputerze
  wyłączone (np. runner CI), te przypadki są pomijane;
* autotesty QML (`tst_shell_qml*`, `tst_shell_journey*`, `tst_notify_sounds_qml*`,
  `tst_appio_import_qml*`, `tst_content_journals_qml`) dostają osobny profil
  `BRULION_PROFILE=test-<nazwa>` (`brulion_test_profile()` w `tests/CMakeLists.txt`), który ich
  fixture usuwa przed uruchomieniem — zamiast folderów `XDG_*`. W takim profilu przełącznik
  autostartu jest wyłączony, więc żaden test nie pisze do klucza `Run`; każdy profil rejestruje
  własny identyfikator powiadomień (`JanuszBit.Brulion.test-<nazwa>`). Testy C++ w procesie
  (np. `tst_shell_navigation`) nie ustawiają nazw Brulionu i dostają identyfikator
  `JanuszBit.Brulion.unnamed`, którego nikt nie rejestruje: ich powiadomienia idą do zasobnika,
  a sprzątanie nie usuwa powiadomień prawdziwego Brulionu.

## Jedna kopia programu i zdalne sterowanie

`QLocalServer` na Windows to potok nazwany. Klucz jednej kopii leży w folderze tymczasowym
użytkownika (`%TEMP%`, w profilu użytkownika; `RuntimeLocation` z Qt byłby samym katalogiem
domowym) — tam jest też plik blokady, a nazwa potoku zawiera tę ścieżkę. `UserAccessOption` daje
potokowi listę dostępu tylko dla tego użytkownika (odpowiednik `0600` gniazda na Linuksie). Nazwy
potoków są wspólne dla całego komputera, więc jak `SO_PEERCRED` na Linuksie obie strony sprawdzają
proces po drugiej stronie (`GetNamedPipeClientProcessId`/`GetNamedPipeServerProcessId`) i SID jego
użytkownika (`src/ipc/controlprotocol.cpp`, `peerUid()`): potoku, który inny użytkownik założył
pierwszy pod tą nazwą, Brulion nie używa.

Drugie uruchomienie z plikami (`brulion.exe --open "%1"` z „Otwórz za pomocą”) zamienia ich ścieżki
na bezwzględne (od swojego bieżącego folderu) i wysyła działającej kopii komunikat `share`
(`src/ipc/appmessage.h`), a przed wysłaniem pozwala jej wyjść na pierwszy plan
(`AllowSetForegroundWindow`, `platform::allowForegroundActivation()`): okno Brulionu przychodzi
na wierzch, a nie tylko miga na pasku zadań. Kilka plików zaznaczonych naraz to kilka uruchomień —
pierwsze, gdy Brulion nie działa, staje się główną kopią, pozostałe czekają, aż zacznie słuchać,
a pliki trafiają do jednego okna. Brulion starszy niż 0.5.1, gdy jeszcze działa, komunikatu
`share` nie zna i go pomija — wtedy pomaga zamknięcie go i ponowne „Otwórz za pomocą”.
