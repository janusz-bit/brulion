# Do zrobienia

Lista pomysłów i znanych braków, podzielona na kategorie. Kolejność niczego nie przesądza. Punkt,
który zostanie zrobiony w repozytorium, po prostu znika z listy.

## Funkcje

* **Zdjęcia z internetu — sprawdzić na telefonie i w przeglądarce.** Na emulatorze Androida
  sprawdzone (`scripts/android-emulator-test.sh --web-pictures`: `https://` i `http://`, pamięć
  podręczna po restarcie i offline, przeglądarka zdjęcia i Wstecz, menu z przytrzymania, zapis
  w notatniku); wersja webowa nie była uruchomiona (listy kontrolne: docs/ANDROID.md „Obrazy
  z internetu”, docs/WEB.md):
  * prawdziwy telefon: przesuwanie powiększonego zdjęcia palcem i gest Wstecz z krawędzi,
    wklejenie adresu skopiowanego w Chrome przez „Wklej” z menu tekstu, eksport z „Dołącz kopie”;
  * przeglądarka: obraz z serwera z CORS i bez (komunikat nazywa CORS), `http://` na stronie
    `https://` (komunikat o mieszanej treści), brak zapytania wstępnego (preflight) w Firefoksie,
    eksport z kopiami.
* **Rysunki — dalsze kroki.** Edytor rysunków (`/Rysunek`, docs/UI.md „Drawing editor”) ma
  pismo odręczne, zakreślacz, gumkę, papier, kształty, strzałki, tekst i zaznaczanie lassem.
  Do zrobienia:
  * kopie konfliktowe Syncthinga (`*.sync-conflict-….svg`): „Scal rysunki” w centrum
    synchronizacji, łączące linie obu wersji po ich identyfikatorach (jak przy zmianie z innego
    urządzenia przy otwartym rysunku);
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
  * import z Trilium: notatka typu „canvas” jako jeden plik SVG ze sceną Excalidraw (dziś
    importer zapisuje obok siebie SVG i plik `.excalidraw`);
  * notatki na slajdach i PDF-ach z wykładu: rysowanie na obrazie w tle (strona PDF, slajd);
  * sprawdzić na tablecie z rysikiem (Android, Natalia) i w przeglądarce, czy nacisk rysika
    dociera do Brulionu.
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
