# Plan prac

Rzeczy zaplanowane do zrobienia, po kolei. Na prośbę („zrób następny punkt z TODO”) są
implementowane od góry kolejki: z testami, dokumentacją i tłumaczeniem, sprawdzone na laptopie
(a gdy dotyczą telefonu — na emulatorze Androida). Zrobiony punkt znika z kolejki; jego opis trafia
do „Listy zmian” przy najbliższym wydaniu.

## Kolejka

1. **Menu na przypomnieniach, alarmach i datach.** Prawy przycisk myszy (komputer) albo
   przytrzymanie palcem (telefon) na znaczniku ⏰ przypomnienia lub alarmu otwiera menu do zmiany
   daty i godziny bez szukania tokenu w tekście:
   * „Zmień datę i godzinę…” (kalendarz i zegar, jak w dialogu przypomnienia), szybkie
     przesunięcia (np. „+1 godz.”, „jutro o tej porze”, „za tydzień”), włączenie/wyłączenie
     alarmu, powtarzanie, „Usuń przypomnienie”;
   * to samo dla innych znaczników: 📅 termin, ⏳ zaplanowane, 🛫 początek, ✅ data wykonania,
     ➕ data utworzenia, 🔁 powtarzanie (zmiana reguły), priorytet;
   * podobnie dla innych elementów tekstu, gdzie menu ma sens: `#tag` i `[[link]]` (otwórz,
     otwórz w panelu bocznym, kopiuj link), daty w tekście.

   Dziś kliknięcie ⏰ otwiera dialog przypomnienia, a pozostałe znaczniki menu nie mają. Zmiana
   zapisuje się w pliku jak edycja ręczna (bajt po bajcie poza zmienionym tokenem), daje się cofnąć
   i działa w trybie konspektu i dokumentu, w strumieniu dziennika, w wynikach zapytań i w widoku
   zadań oraz w przeglądarce.

## Do decyzji

Pomysły i znane braki zebrane z dokumentacji i przeglądów — nie są jeszcze w kolejce. Przenieś do
kolejki to, co ma być zrobione, w wybranej kolejności.

* **Android: „Udostępnij → Brulion”** — plik `.md`/`.txt` albo tekst z innej aplikacji do
  dziennika lub nowej strony (docs/ANDROID.md).
* **Wydajność** (docs/PERFORMANCE.md, „Next steps”): indeksowanie w wątkach roboczych (L1),
  pamięć podręczna indeksu na dysku (L2), przyrostowe listy zadań/kalendarza/przypomnień (L3),
  szybsze cofanie (L5), mniejsze zużycie pamięci przez indeks (L8), natywne okna wyboru plików
  (L9: APK −15 MB, wersja webowa −5 MB), wykres powiązań na wątku roboczym (L16).
* **Kopiowanie bloków**: format HTML tworzony dopiero wtedy, gdy inna aplikacja o niego poprosi
  (kopia dużej strony ~3× szybsza); zaznaczanie bloków z kilku dni dziennika naraz.
* **Synchronizacja Nextcloud**: zmiana nazwy folderu zrobiona na serwerze dociera do innych
  urządzeń jako pobranie i kosz, więc historia wersji, ulubione i zwinięcia nie idą za stroną.
* **brulion-cli po polsku** — komunikaty wiersza poleceń mają dziś tylko wersję angielską.
* **Stare błędy do sprawdzenia** (mogły zniknąć przy innych zmianach): obliczenie w komórce
  tabeli nieodświeżane po zmianie, kod `` `…` `` z `=` w nowej linii nieprzerysowany, zły wiersz po
  cofnięciu wstawienia na początku strony, „/” zaraz po tokenie przypomnienia, wielkie litery
  Gboard na początku bloku.
* **Pasek przewijania w długich listach** skacze (ListView szacuje wysokość niewidocznych
  wierszy; docs/UI.md, „Still there”).
