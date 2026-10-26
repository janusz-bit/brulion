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
* **Rysunki w notatkach** — sprawdzić, czy da się dodać rysowanie (coś jak Excalidraw
  w Obsidianie, ale wygodniejsze na tablecie). Przypadek użycia: Natalia na studia,
  z synchronizacją przez Syncthing.
* **Kopiowanie bloków**:
  * format HTML tworzony dopiero wtedy, gdy inna aplikacja o niego poprosi (kopia dużej strony
    ~3× szybsza);
  * zaznaczanie bloków z kilku dni dziennika naraz.
* **Dostosowanie wyglądu interfejsu** — własne ustawienia UI (do doprecyzowania, np. kolory
  i motywy, czcionka i jej rozmiar, układ i widoczność elementów).
* **brulion-cli po polsku** — komunikaty wiersza poleceń mają dziś tylko wersję angielską.

## Import i synchronizacja

* **Import zadań z Microsoft To Do** (link z researchem:
  https://share.google/aimode/IkspZGTIWv9OVFLLT).
* **Istniejący folder Obsidiana** — odpowiedzieć na pytanie, czy można po prostu otworzyć folder
  z notatkami Obsidiana synchronizowany Syncthingiem, czy lepiej użyć synchronizacji, którą
  proponuje aplikacja. Ewentualnie dopisać odpowiedź do dokumentacji lub onboardingu.
* **Zmiana nazwy folderu w Nextcloud** — zrobiona na serwerze dociera do innych urządzeń jako
  pobranie i kosz, więc historia wersji, ulubione i zwinięcia nie idą za stroną.

## Android i telefon

* **Boczne menu gestem** — otwieranie szuflady przesunięciem palca od krawędzi ekranu, nie tylko
  przyciskiem.
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

* **Menu statusu na wyniku zapytania znika**, gdy wyniki odświeżą się z zewnątrz (synchronizacja,
  zmiana w innej notatce), bo wiersze są tworzone od nowa. Nic złego się nie zapisuje. Można
  wstrzymać odświeżenie, póki menu jest otwarte.
* **Pasek przewijania w długich listach** skacze, bo ListView szacuje wysokość niewidocznych
  wierszy (docs/UI.md, „Still there”).
* **Stare błędy do sprawdzenia** (mogły zniknąć przy innych zmianach):
  * obliczenie w komórce tabeli nie odświeża się po zmianie;
  * kod `` `…` `` z `=` w nowej linii nie jest przerysowany;
  * zły wiersz po cofnięciu wstawienia na początku strony;
  * „/” zaraz po tokenie przypomnienia;
  * wielkie litery Gboard na początku bloku.
