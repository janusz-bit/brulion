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
* przyrostowe listy zadań, kalendarza i przypomnień (L3);
* szybsze cofanie (L5);
* natywne okna wyboru plików (L9: APK −15 MB, wersja webowa −5 MB);
* wykres powiązań na wątku roboczym (L16).
