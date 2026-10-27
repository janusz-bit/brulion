# Do zrobienia

Lista pomysłów i znanych braków, podzielona na kategorie. Kolejność niczego nie przesądza. Punkt,
który zostanie zrobiony w repozytorium, po prostu znika z listy.

## Funkcje

* **Zdjęcia z internetu — sprawdzić na telefonie i w przeglądarce.** Funkcja jest zrobiona, ale
  nie była uruchomiona na Androidzie ani w wersji webowej (listy kontrolne: docs/ANDROID.md
  „Obrazy z internetu”, docs/WEB.md):
  * Android: czy `http://` działa przy `cleartextTrafficPermitted` (jeśli nie — zdecydować
    w `network_security_config.xml`), pamięć podręczna po restarcie i offline, dotknięcie →
    przeglądarka zdjęcia i gest Wstecz, długie przytrzymanie → menu, wklejenie adresu z Chrome,
    eksport z „Dołącz kopie”;
  * przeglądarka: obraz z serwera z CORS i bez (komunikat nazywa CORS), `http://` na stronie
    `https://` (komunikat o mieszanej treści), brak zapytania wstępnego (preflight) w Firefoksie,
    eksport z kopiami.
* **Rysunki — dalsze kroki.** Edytor rysunków (`/Rysunek`, docs/UI.md „Drawing editor”) ma
  pismo odręczne, zakreślacz, gumkę i papier. Do zrobienia:
  * kopie konfliktowe Syncthinga (`*.sync-conflict-….svg`): „Scal rysunki” w centrum
    synchronizacji, łączące linie obu wersji po ich identyfikatorach (jak przy zmianie z innego
    urządzenia przy otwartym rysunku);
  * kształty, strzałki, tekst i zaznaczanie lassem (dziś rysunek z Excalidraw z takimi
    elementami Brulion tylko pokazuje);
  * pliki `*.excalidraw.md` wtyczki Excalidraw dla Obsidiana (LZ-String w Markdownie);
  * import z Trilium: notatka typu „canvas” jako jeden plik SVG ze sceną Excalidraw (dziś
    importer zapisuje obok siebie SVG i plik `.excalidraw`);
  * notatki na slajdach i PDF-ach z wykładu: rysowanie na obrazie w tle (strona PDF, slajd);
  * sprawdzić na tablecie z rysikiem (Android, Natalia) i w przeglądarce, czy nacisk rysika
    dociera do Brulionu.
* **Udostępnianie do Brulionu poza Androidem** — w wersji w przeglądarce cel udostępniania PWA
  (Web Share Target, jak w poprzedniej aplikacji: `archive/web-v1`), na komputerze „Otwórz
  w Brulionie” dla plików `.md`/`.txt` (wpis `.desktop` z `MimeType`, skojarzenie plików
  w Windows) — to samo okno co „Udostępnij → Brulion” na Androidzie (docs/ANDROID.md).

## Windows

* **Podpisane pliki** — instalator i programy podpisane certyfikatem, bez ostrzeżenia SmartScreen;
  potem zdjąć „(wersja testowa)” (docs/WINDOWS.md).
* **Opinie o wersji desktopowej** — zebrać je od testera (obejrzy ją później).

## Wydajność

Szczegóły w docs/PERFORMANCE.md, „Next steps”:
* indeksowanie w wątkach roboczych (L1);
* pamięć podręczna indeksu na dysku (L2);
* przyrostowe listy zadań, kalendarza i przypomnień (L3);
* szybsze cofanie (L5);
* mniejsze zużycie pamięci przez indeks (L8);
* natywne okna wyboru plików (L9: APK −15 MB, wersja webowa −5 MB);
* wykres powiązań na wątku roboczym (L16).

## Poprawki

* **Enter na końcu przybliżonego nagłówka lub akapitu** (konspekt, „Przybliż” na bloku, który
  nie jest punktem listy) wstawia nowy punkt listy poza przybliżeniem: kursor trafia do
  niewidocznego bloku (`PageController::split`, gałąź `row == zoom && cursor == len`).
