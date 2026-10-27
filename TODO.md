# Do zrobienia

Lista pomysłów i znanych braków, podzielona na kategorie. Kolejność niczego nie przesądza. Punkt,
który zostanie zrobiony w repozytorium, po prostu znika z listy.

## Funkcje

* **Menu na pozostałych znacznikach i elementach tekstu.** Menu dat działa już w edytorze: prawy
  przycisk albo przytrzymanie na 📅 ⏳ 🛫 ✅ ➕ ❌ i ⏰ (`DateChipMenu`, docs/UI.md). Brakuje:
  * tego samego menu dat w wynikach zapytań, osadzeniach, komórkach tabel, w widoku zadań i
    w kalendarzu (zmiana idzie do pliku zadania, jak status: `QueryResultModel.setTaskStatus`);
  * 🔁 powtarzania (zmiana reguły), priorytetu i powtarzania w menu przypomnienia;
  * menu na `#tag` i `[[link]]` (otwórz, otwórz w panelu bocznym, kopiuj link) oraz na datach
    w tekście (`[[2026-10-20]]`).

  Zmiana zapisuje się w pliku jak edycja ręczna (bajt po bajcie poza zmienionym tokenem) i daje
  się cofnąć.
* **Zdjęcia z internetu.** Dziś obraz pokazuje się tylko jako załącznik z folderu notatek
  (`![[plan.png]]`, `![](assets/plan.png)`); `![](https://…/foto.jpg)` jest zwykłym linkiem.
  Brakuje:
  * wyświetlania zdjęć spod adresu `http(s)://` w edytorze, osadzeniach i widoku zdjęcia (te
    same rozszerzenia co `paths::pictureExtensions()`, a bez rozszerzenia — po typie odpowiedzi),
    z miejscem zarezerwowanym na czas ładowania i czytelnym błędem bez sieci;
  * pamięci podręcznej na dysku (zdjęcie widać offline po pierwszym pobraniu, limit rozmiaru);
    samo indeksowanie niczego nie pobiera;
  * ustawienia „ładuj obrazy z internetu” (zawsze / po kliknięciu), bo otwarcie notatki
    z importu lub synchronizacji zdradzałoby adres IP; limit rozmiaru i czasu pobrania, tylko
    formaty obrazów;
  * „Zapisz w notatkach” — pobranie zdjęcia do `assets/` i podmiana linku na lokalny (jedna
    zmiana do cofnięcia), także przy wklejaniu obrazu lub adresu z przeglądarki;
  * w eksporcie HTML/Pandoc wyboru: zostawić adres albo dołączyć pobraną kopię; osobno
    sprawdzić wersję webową (CORS) i Androida.
* **Rysunki w notatkach** — sprawdzić, czy da się dodać rysowanie (coś jak Excalidraw
  w Obsidianie, ale wygodniejsze na tablecie). Przypadek użycia: Natalia na studia,
  z synchronizacją przez Syncthing.
* **Kopiowanie bloków**:
  * format HTML tworzony dopiero wtedy, gdy inna aplikacja o niego poprosi (kopia dużej strony
    ~3× szybsza);
  * zaznaczanie bloków z kilku dni dziennika naraz.
* **Dostosowanie wyglądu interfejsu** — własne ustawienia UI (do doprecyzowania, np. kolory
  i motywy, czcionka i jej rozmiar, układ i widoczność elementów).
* **„Zrobione” w powiadomieniu przypomnienia** — przycisk obok „Odłóż” i „OK”, który od razu
  oznacza zadanie z przypomnieniem jako zrobione (`[x]` z datą ✅, jak zmiana statusu w edytorze),
  bez otwierania notatki; na Linuksie, Androidzie i Windows.

## Import i synchronizacja

* **Import zadań z Microsoft To Do** (link z researchem:
  https://share.google/aimode/IkspZGTIWv9OVFLLT).
* **Zmiana nazwy folderu w Nextcloud** — zrobiona na serwerze dociera do innych urządzeń jako
  pobranie i kosz, więc historia wersji, ulubione i zwinięcia nie idą za stroną.

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
