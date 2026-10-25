# Brulion na Windows (wersja testowa)

Od wersji 0.4.5 Brulion ma **testową** wersję na Windows 10 i 11 (x86_64): ten sam program co na
Linuksie (C++20 + Qt 6.11.2, Qt Quick), zbudowany kompilatorem MSVC 2022. Jest w każdym wydaniu
w dwóch postaciach:

| Plik | Co to jest |
|---|---|
| `Brulion-<wersja>-windows-x86_64-setup.exe` | instalator (Inno Setup) — instalacja dla bieżącego użytkownika, bez uprawnień administratora |
| `brulion-<wersja>-windows-x86_64.zip` | wersja przenośna — folder `Brulion` z `brulion.exe`, `brulion-cli.exe`, bibliotekami Qt i środowiskiem MSVC |

„Testowa” znaczy: buduje się i przechodzi testy w CI, ale nie była jeszcze długo używana na co dzień
i ma ograniczenia opisane niżej. Błędy zgłaszaj w
[issues](https://github.com/janusz-bit/brulion/issues) (z rejestrem zdarzeń — README, „Gdy coś nie
działa”).

## Instalacja

* **Instalator:** uruchom `Brulion-<wersja>-windows-x86_64-setup.exe`. Program trafia do
  `%LOCALAPPDATA%\Programs\Brulion`, skrót do menu Start, ikona na pulpicie na życzenie (zadanie
  „Utwórz ikonę na pulpicie”). Język instalatora: polski albo angielski (według systemu). Nowa
  wersja instaluje się na poprzednią (ten sam `AppId`); uruchomiony Brulion zostanie poproszony
  o zamknięcie.
* **SmartScreen:** pliki nie są podpisane certyfikatem, więc Windows ostrzega przy pierwszym
  uruchomieniu: „System Windows ochronił ten komputer” → **Więcej informacji** → **Uruchom mimo to**.
  Sumy SHA-256 są w `SHA256SUMS.txt` wydania.
* **Wersja przenośna:** rozpakuj ZIP w dowolnym miejscu i uruchom `Brulion\brulion.exe`. Nic się
  nie instaluje; notatki i ustawienia są w tych samych miejscach co przy instalatorze (niżej), nie
  w folderze programu.
* **Odinstalowanie:** Ustawienia → Aplikacje → Brulion → Odinstaluj (albo `unins000.exe` w folderze
  programu). Usuwa tylko pliki programu i skróty — **notatki, ustawienia i rejestr zdarzeń zostają**.
* Instalacja bez okien (np. skrypt): `Brulion-<wersja>-windows-x86_64-setup.exe /VERYSILENT
  /SUPPRESSMSGBOXES /CURRENTUSER`, odinstalowanie: `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES`.
  Administrator może zainstalować dla wszystkich (`/ALLUSERS`, do `C:\Program Files\Brulion`).

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

`%APPDATA%` to zwykle `C:\Users\<użytkownik>\AppData\Roaming`, `%LOCALAPPDATA%` —
`…\AppData\Local`, `%TEMP%` — `…\AppData\Local\Temp`.

**Osobny profil:** zmienna środowiskowa `BRULION_PROFILE=<nazwa>` (litery, cyfry, `-`, `_`, do 32
znaków) zamienia organizację „Brulion” na „Brulion-<nazwa>” — ustawienia, dane, rejestr i gniazdo
jednej kopii są wtedy osobne (`%APPDATA%\Brulion-<nazwa>\…`). Tak samo dla `brulion.exe`
i `brulion-cli.exe`. Służy testom (na Windows zmienne `XDG_*` z testów linuksowych nic nie
zmieniają) i do wypróbowania czegoś bez ruszania własnego profilu; działa też na Linuksie.

## Ograniczenia wersji testowej

* **Przypomnienia tylko przez ikonę w zasobniku systemowym.** Nie ma natywnych powiadomień Windows
  z przyciskami: przypomnienie i alarm pokazują się jako dymek ikony Brulionu (Windows wyświetla go
  jak powiadomienie), bez „Odłóż”/„OK” — kliknięcie otwiera Brulion. Dźwięk to dźwięk powiadomienia
  systemu; wybór dźwięku w ustawieniach i „Odtwórz próbkę” działają tylko na Linuksie (D-Bus).
  Przypomnienia odlicza uruchomiony Brulion — zamknięcie okna zostawia go w zasobniku (ustawienie
  „Działaj dalej w zasobniku systemowym”, domyślnie włączone; „Zakończ” w menu ikony go zamyka);
  przypomnienia, które minęły przy zamkniętym programie, czekają w dzienniku.
* **Brak autostartu.** Brulion nie uruchamia się sam po zalogowaniu (na NixOS robi to moduł
  `programs.brulion`). Można dodać skrót do `brulion.exe --hidden` w folderze `shell:startup`.
* **Niepodpisane pliki** (SmartScreen, wyżej) i **brak automatycznych aktualizacji** — nową wersję
  instaluje się instalatorem na poprzednią.
* **Wiersz poleceń:** `brulion-cli.exe` (obok `brulion.exe`) działa jak na Linuksie
  (docs/CLI.md). `brulion.exe cli …` też działa, ale `brulion.exe` jest programem okienkowym —
  w konsoli nie widać jego wyjścia; w skryptach i dla agentów AI używaj `brulion-cli.exe`.
  Zdalne sterowanie aplikacją (`--remote-control`, `brulion-cli app …`) działa przez potok
  nazwany, ale jego test end-to-end jest tylko linuksowy (niżej).
* Tylko x86_64 (bez Windows na ARM) i tylko Windows 10/11.

## Budowanie

### W CI

`.github/workflows/windows.yml` (runner `windows-2022`): Qt 6.11.2 `win64_msvc2022_64` z aqt
(`jurplel/install-qt-action`, w cache; aqtinstall z przypiętego commita gałęzi master, bo wydanie
3.3.0 nie zna układu repozytorium Qt 6.11 dla Windows), MSVC z `ilammy/msvc-dev-cmd`, CMake +
Ninja, Release, potem `scripts/windows-build.ps1` krok po kroku: build, testy, paczki, sprawdzenie
paczek → artefakt `brulion-windows` (ZIP + instalator). Uruchamia się jak `appimage.yml`: ręcznie
(`gh workflow run windows.yml --ref main`), przy pushu na `main` ze znacznikiem `[ci]` (gdy zmienia
`src/`, `tests/`, `packaging/windows/`…) i z `release.yml`, który dołącza oba pliki do wydania
(i do `SHA256SUMS.txt`) — wydanie publikuje się tylko, gdy przejdzie także Windows.

### Lokalnie

Potrzebne: Visual Studio 2022 z obciążeniem „Programowanie aplikacji klasycznych w C++” (ma CMake
i Ninja), Qt 6.11.2 dla MSVC (instalator Qt albo aqt nowszy niż 3.3.0:
`aqt install-qt windows desktop 6.11.2 win64_msvc2022_64 -O C:\Qt`), Inno Setup 6 (`winget install JRSoftware.InnoSetup`). Klon z Git for Windows
zostawia końce linii LF dzięki `.gitattributes` (`* -text`) — testy porównują bajty. W PowerShellu
w katalogu repozytorium:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64
# tylko wybrane kroki:
scripts\windows-build.ps1 -QtDir C:\Qt\6.11.2\msvc2022_64 -Steps build,test
```

Bez środowiska MSVC (brak `cl.exe` w `PATH`) skrypt sam je wczytuje (`vswhere` + `vcvars64.bat`).
Kroki:

1. **build** — `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` w `build-windows`. MSVC dostaje
   `/utf-8` (polskie napisy w kodzie), `/permissive-`, `/Zc:__cplusplus`, `/bigobj` i ostrzeżenia
   `/W4` bez `/WX` (CMakeLists.txt, `cmake/BrulionWarnings.cmake`); `brulion.exe`
   i `brulion-cli.exe` mają ikonę `packaging/windows/brulion.ico` i wersję w zasobach
   (`packaging/windows/brulion.rc.in`, `cmake/BrulionWindows.cmake`).
2. **test** — `ctest` offscreen (`QT_QPA_PLATFORM=offscreen`) z Qt w `PATH`.
3. **package** — do `build-windows\package\Brulion`: `brulion.exe`, `brulion-cli.exe`,
   `windeployqt --release --qmldir src\app\qml --no-translations --no-opengl-sw` (biblioteki Qt,
   wtyczki — platformy, formaty obrazów z SVG, TLS przez Schannel bez OpenSSL — i moduły QML) oraz
   biblioteki MSVC (`Microsoft.VC143.CRT`, obok programu — bez instalowania Visual C++
   Redistributable). Z tego folderu: `dist-windows\brulion-<wersja>-windows-x86_64.zip` i
   instalator z `packaging/windows/brulion.iss` →
   `dist-windows\Brulion-<wersja>-windows-x86_64-setup.exe`.
4. **check** — bez Qt w `PATH` i bez zmiennych Qt: ZIP rozpakowany do nowego folderu, potem
   instalator zainstalowany po cichu dla bieżącego użytkownika; z obu `brulion-cli.exe --version`
   i autotest powłoki prawdziwej aplikacji (`brulion.exe --demo --route journals --selftest-shell
   --no-single-instance …`, offscreen, profil `BRULION_PROFILE=package-check`) — brak biblioteki
   albo modułu QML go oblewa; na koniec ciche odinstalowanie, po którym ustawienia profilu muszą
   zostać.

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
* autotesty QML (`tst_shell_qml*`, `tst_shell_journey*`, `tst_notify_sounds_qml*`,
  `tst_appio_import_qml*`, `tst_content_journals_qml`) dostają osobny profil
  `BRULION_PROFILE=test-<nazwa>` (`brulion_test_profile()` w `tests/CMakeLists.txt`), który ich
  fixture usuwa przed uruchomieniem — zamiast folderów `XDG_*`.

## Jedna kopia programu i zdalne sterowanie

`QLocalServer` na Windows to potok nazwany. Klucz jednej kopii leży w folderze tymczasowym
użytkownika (`%TEMP%`, w profilu użytkownika; `RuntimeLocation` z Qt byłby samym katalogiem
domowym) — tam jest też plik blokady, a nazwa potoku zawiera tę ścieżkę. `UserAccessOption` daje
potokowi listę dostępu tylko dla tego użytkownika (odpowiednik `0600` gniazda na Linuksie). Nazwy
potoków są wspólne dla całego komputera, więc jak `SO_PEERCRED` na Linuksie obie strony sprawdzają
proces po drugiej stronie (`GetNamedPipeClientProcessId`/`GetNamedPipeServerProcessId`) i SID jego
użytkownika (`src/ipc/controlprotocol.cpp`, `peerUid()`): potoku, który inny użytkownik założył
pierwszy pod tą nazwą, Brulion nie używa.
