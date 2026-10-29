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
* **Dopracowanie interfejsu — porównanie z Obsidianem.** Przejść ekran po ekranie (edytor
  w trybie dokumentu i konspektu, pasek boczny, wyszukiwarka, kalendarz, ustawienia; telefon
  i komputer) z Obsidianem obok i spisać różnice w układzie, odstępach, typografii, kolorach,
  animacjach i drobnych zachowaniach (kursor, zaznaczanie, menu, skróty). Z listy różnic
  wybrać te, które poprawiają odbiór interfejsu, i poprawić je w QML (docs/UI.md).
* **Edytowanie właściwości strony** — dodawanie, zmiana i usuwanie właściwości (frontmatter YAML
  i `klucz:: wartość`) w panelu „Właściwości” i w nagłówku strony, bez edycji surowego tekstu:
  pola według typu wartości (tekst, liczba, data, lista, znaczniki, aliasy), podpowiedzi
  istniejących kluczy z notatnika, zachowanie reszty frontmattera bajt w bajt (docs/SYNTAX.md
  „Page properties”, docs/UI.md).

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
