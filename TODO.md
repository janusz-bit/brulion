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
* **Rysunki w notatkach** — sprawdzić, czy da się dodać rysowanie (coś jak Excalidraw
  w Obsidianie, ale wygodniejsze na tablecie). Przypadek użycia: Natalia na studia,
  z synchronizacją przez Syncthing.
* **Dostosowanie wyglądu interfejsu** — własne ustawienia UI (do doprecyzowania, np. kolory
  i motywy, czcionka i jej rozmiar, układ i widoczność elementów).
* **„Zrobione” w powiadomieniu przypomnienia** — przycisk obok „Odłóż” i „OK”, który od razu
  oznacza zadanie z przypomnieniem jako zrobione (`[x]` z datą ✅, jak zmiana statusu w edytorze),
  bez otwierania notatki; na Linuksie, Androidzie i Windows.

## Android i telefon

* **„Udostępnij → Brulion”** — plik `.md`/`.txt` albo tekst z innej aplikacji trafia do
  dziennika lub na nową stronę (docs/ANDROID.md).
* **Widgety**:
  * z zadaniami: podgląd listy i szybkie dodawanie bez wchodzenia do aplikacji (wzór: widget To Do
    3×2 „My Day”);
  * kalendarza, który pokazuje też zadania z ustawionym czasem;
  * z notatkami (niski priorytet).

## Windows

* **Pełna wersja zamiast testowej.** Dziś przypomnienia pokazuje tylko ikona w zasobniku, nie ma
  autostartu, a pliki nie są podpisane (docs/WINDOWS.md).
* **Podgląd kalendarza z paska zadań**, podobny do domyślnego z Windows 7/10. Opcjonalnie mógłby
  zastąpić domyślny kalendarz Windows.
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

* **Pasek przewijania w długich listach** skacze, bo ListView szacuje wysokość niewidocznych
  wierszy (docs/UI.md, „Still there”).
* **Enter na końcu przybliżonego nagłówka lub akapitu** (konspekt, „Przybliż” na bloku, który
  nie jest punktem listy) wstawia nowy punkt listy poza przybliżeniem: kursor trafia do
  niewidocznego bloku (`PageController::split`, gałąź `row == zoom && cursor == len`).
