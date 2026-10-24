# Plan prac

Rzeczy zaplanowane do zrobienia, po kolei. Na prośbę („zrób następny punkt z TODO”) są
implementowane od góry kolejki: z testami, dokumentacją i tłumaczeniem, sprawdzone na laptopie
(a gdy dotyczą telefonu — na emulatorze Androida). Zrobiony punkt znika z kolejki; jego opis trafia
do „Listy zmian” przy najbliższym wydaniu.

## Kolejka

1. **Menu na pozostałych znacznikach i elementach tekstu.** Menu dat jest już w edytorze
   (prawy przycisk / przytrzymanie na 📅 ⏳ 🛫 ✅ ➕ ❌ i ⏰: `DateChipMenu`, docs/UI.md). Zostało:
   * to samo menu dat w wynikach zapytań, osadzeniach, komórkach tabel, w widoku zadań i
     w kalendarzu (zmiana idzie do pliku zadania, jak status: `QueryResultModel.setTaskStatus`);
   * 🔁 powtarzanie (zmiana reguły), priorytet, powtarzanie w menu przypomnienia;
   * `#tag` i `[[link]]` (otwórz, otwórz w panelu bocznym, kopiuj link), daty w tekście
     (`[[2026-10-20]]`).

   Zmiana zapisuje się w pliku jak edycja ręczna (bajt po bajcie poza zmienionym tokenem) i daje
   się cofnąć.

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
* **Menu statusu na wyniku zapytania znika**, gdy wyniki odświeżą się z zewnątrz (synchronizacja,
  zmiana w innej notatce), bo wiersze są tworzone od nowa — nic złego się nie zapisuje; można
  wstrzymać odświeżenie, póki menu jest otwarte.
* **Stare błędy do sprawdzenia** (mogły zniknąć przy innych zmianach): obliczenie w komórce
  tabeli nieodświeżane po zmianie, kod `` `…` `` z `=` w nowej linii nieprzerysowany, zły wiersz po
  cofnięciu wstawienia na początku strony, „/” zaraz po tokenie przypomnienia, wielkie litery
  Gboard na początku bloku.
* **Pasek przewijania w długich listach** skacze (ListView szacuje wysokość niewidocznych
  wierszy; docs/UI.md, „Still there”).
