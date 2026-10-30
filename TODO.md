# Do zrobienia

Lista pomysłów i znanych braków, podzielona na kategorie. Kolejność niczego nie przesądza. Punkt,
który zostanie zrobiony w repozytorium, po prostu znika z listy.

## Funkcje

* **Zdjęcia z internetu — sprawdzić na prawdziwym telefonie.** Emulator Androida
  (`scripts/android-emulator-test.sh --web-pictures`) i przeglądarki Chromium i Firefox
  (`tests/web`, GitHub Actions) sprawdzają to automatycznie; zostaje prawdziwy telefon
  (docs/ANDROID.md „Obrazy z internetu”): przesuwanie powiększonego zdjęcia palcem i gest Wstecz
  z krawędzi, wklejenie adresu skopiowanego w Chrome przez „Wklej” z menu tekstu, eksport
  z „Dołącz kopie”.
* **Rysunki — dalsze kroki.** Edytor rysunków (`/Rysunek`, docs/UI.md „Drawing editor”) ma
  pismo odręczne, zakreślacz, gumkę, papier, kształty, strzałki, tekst i zaznaczanie lassem.
  Do zrobienia:
  * sprawdzić ręcznie „Scal rysunki” na prawdziwych urządzeniach: rysunek zmieniony bez sieci na
    telefonie i na laptopie, Syncthing zostawia kopię `*.sync-conflict-….svg` (albo
    `*.excalidraw.sync-conflict-….md` rysunku z Obsidiana) → „Scal rysunki” w centrum
    synchronizacji na jednym z nich; to samo z prawdziwym Nextcloudem (rysunek zmieniony na dwóch
    urządzeniach scala się sam, bez kopii). Kopie od prawdziwego Syncthinga (dwie instancje na
    jednym komputerze) scala `tests/app/io/syncthing-conflict.sh`, Nextcloud sprawdzono tylko na
    udawanym serwerze WebDAV (`tst_sync_devices`);
  * sprawdzić ręcznie zaznaczanie lassem rysikiem na tablecie (Natalia) i polskie znaki z
    klawiatury ekranowej w tekście na rysunku na prawdziwym telefonie (pisanie z klawiatury
    sprawdza `scripts/android-emulator-test.sh --drawing`, zgodność z excalidraw.com w obie strony
    `tests/app/drawing/excalidraw-roundtrip.sh`, wymaga internetu);
  * zmiana rozmiaru i obracanie zaznaczenia (uchwyty na ramce), zaznaczanie prostokątem;
  * kopiowanie elementów między Brulionem a excalidraw.com przez schowek;
  * rysunki `*.excalidraw.md` wtyczki Excalidraw dla Obsidiana: obrazy z sekcji „Embedded Files”
    (dziś zastępcze prostokąty), eksport (HTML, Pandoc) takich rysunków jako obrazów (dziś
    osadzone jako strona);
  * sprawdzić ręcznie rysunki z Obsidiana na prawdziwym skarbcu (testy opierają się na plikach
    napisanych według kodu wtyczki; 22 pliki z publicznych skarbców Brulion czyta i zapisuje
    poprawnie, ale nie otwierano ich potem w Obsidianie): otworzyć `*.excalidraw.md` w Brulionie,
    dorysować coś i przesunąć tekst z linkiem `[[…]]`, otworzyć go w Obsidianie (rysunek się
    wczytuje, link w tekście działa, notatki z tyłu nietknięte); zetrzeć kreskę w Brulionie, gdy
    Obsidian ma ten rysunek otwarty (powinna zniknąć i tam);
  * notatki na slajdach i PDF-ach z wykładu: rysowanie na obrazie w tle (strona PDF, slajd);
  * na prawdziwym tablecie z rysikiem (Android, Natalia — w aplikacji i w przeglądarce): czy
    linia grubieje przy mocniejszym nacisku, czy drugi koniec rysika i przycisk boczny ścierają
    (automatycznie sprawdzone: Chromium i Firefox — `tests/web`, emulator Androida — `--stylus`).
* **Udostępnianie poza Androidem — sprawdzić na prawdziwych urządzeniach.** Automatycznie
  sprawdzone (testy, CI): odczyt udostępnienia, okno, przekazanie do działającej kopii, wpis
  `.desktop`, rejestr Windows (instalacja, aktualizacja, odinstalowanie), wersja webowa w Chromium
  bez okna (`wasm-smoke.mjs --share`). Zostało ręcznie: Chrome lub Edge na Androidzie (wersja
  webowa zainstalowana jako aplikacja → Udostępnij → Brulion ze strony i z zaznaczonego tekstu,
  przy zamkniętym i otwartym Brulionie); Eksplorator Windows („Otwórz za pomocą → Brulion” na
  `.md` i `.txt`, kilka plików naraz, okno na wierzchu, program domyślny bez zmian); menedżer
  plików na Linuksie (GNOME Pliki, Dolphin: „Otwórz za pomocą”, okno wychodzi na wierzch pod
  Waylandem, dwuklik na `.md` otwiera dalej dotychczasowy program; Cinnamon/Xfce pod X11: kursor
  nie kręci się po otwarciu pliku w działającym Brulionie); AppImage zintegrowany przez Gear Lever
  albo AppImageLauncher.
* **Dopracowanie interfejsu — porównanie z Obsidianem.** Przejść ekran po ekranie (edytor
  w trybie dokumentu i konspektu, pasek boczny, wyszukiwarka, kalendarz, ustawienia; telefon
  i komputer) z Obsidianem obok i spisać różnice w układzie, odstępach, typografii, kolorach,
  animacjach i drobnych zachowaniach (kursor, zaznaczanie, menu, skróty). Z listy różnic
  wybrać te, które poprawiają odbiór interfejsu, i poprawić je w QML (docs/UI.md).

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
