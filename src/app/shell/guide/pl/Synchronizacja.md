---
tags: [dokumentacja]
brulion: document
---
# Synchronizacja

Brulion nie ma własnego serwera. Notatki to zwykły folder, więc synchronizujesz go tym, czym lubisz. Zapis jest zawsze lokalny i natychmiastowy; synchronizacja dzieje się w tle i nie blokuje pisania.

## Syncthing (polecane do pracy offline)
1. Udostępnij folder notatnika w Syncthing (na Androidzie: Syncthing-Fork).
2. Na każdym urządzeniu wskaż w Brulionie ten sam folder (na Androidzie np. `/storage/emulated/0/Brulion` i zgoda „Dostęp do wszystkich plików”; aplikacja z Google Play: niżej).
3. Gdy ta sama notatka zmieni się na dwóch urządzeniach, Syncthing zostawia kopię `*.sync-conflict-*`. Brulion nie pokazuje jej jako strony, tylko w centrum synchronizacji jako **konflikt do scalenia** — także kopię rysunku, którą scalisz przyciskiem **Scal rysunki**, albo tablicy (**Scal tablice**).

Opcjonalnie Brulion pokazuje stan z API Syncthinga.

### Folder wybrany na telefonie (aplikacja z Google Play)
Brulion z Google Play nie ma zgody „Dostęp do wszystkich plików”. Jego notatnik leży w pamięci aplikacji, a Brulion synchronizuje go z folderem, który wskażesz w systemowym okienku wyboru folderu — na przykład z tym, który synchronizuje Syncthing-Fork:

1. Na ekranie startowym stuknij **Wybierz folder…** (albo, w notatniku, który już jest w pamięci aplikacji: centrum synchronizacji → **Syncthing** → **Folder na tym telefonie** → **Wybierz folder…**).
2. Wskaż folder i pozwól Brulionowi z niego korzystać. Za pierwszym razem Brulion kopiuje notatki z folderu do swojego notatnika i go otwiera.
3. Od tej pory zmiany płyną w obie strony: kilka sekund po Twoich edycjach, co kilka minut, po powrocie do aplikacji i — gdy jest zamknięta — razem ze sprawdzaniem przypomnień w tle. Zmiany zrobione po obu stronach naraz są scalane jak przy Nextcloudzie; gdy się nie da, obie wersje zostają jako konflikt do scalenia.

Pełny APK z GitHuba albo F-Droid też to potrafi (dla notatnika w pamięci aplikacji), obok bezpośredniego otwierania folderu na telefonie. **Przestań synchronizować** zostawia notatki w obu miejscach.

## Nextcloud (WebDAV)
*Ustawienia → Synchronizacja* (albo centrum synchronizacji): adres serwera, użytkownik i **hasło aplikacji** wygenerowane w Nextcloudzie. Zmiany z wielu urządzeń scalane są trójstronnie (rysunki element po elemencie, a tablice karta po karcie, nigdy wiersz po wierszu), więc zwykle nic nie ginie i nie musisz nic robić. Gdy scalenie jest niemożliwe, zostają obie wersje i dostajesz widok scalania.

Serwer i konto są wspólne, ale **włączenie synchronizacji i folder na serwerze należą do notatnika**: inny notatnik (także ta dokumentacja) startuje bez synchronizacji. Dzięki temu notatki z jednego notatnika nie trafią do drugiego.

- Folder na serwerze Brulion proponuje osobno dla każdego notatnika: przy pierwszym koncie `Notatki`, dla kolejnych notatników ich nazwę (np. `Praca`, a gdy jest zajęta — `Praca 2`). Na drugim urządzeniu wpisz ten sam folder co na pierwszym.
- Jeden folder na serwerze należy do jednego notatnika: Brulion nie zapisze folderu, z którym synchronizuje się już inny notatnik na tym urządzeniu (ani folderu w nim albo nad nim), i powie, który to notatnik.
- Jeśli dwa notatniki już synchronizują się z jednym folderem (mogły to ustawić wersje 0.3.1–0.3.5), żaden z nich się nie synchronizuje — ani sam, ani po „Synchronizuj teraz” — bo ich notatki by się mieszały. Stan synchronizacji (na dole paska bocznego) mówi, z którym notatnikiem dzielą folder; w centrum synchronizacji wybierz „Wybierz inny folder” i podaj nowy folder dla jednego z nich (albo wyłącz w nim synchronizację).
- **Wyloguj** wyłącza synchronizację we wszystkich notatnikach na tym urządzeniu, bo hasło aplikacji jest wspólne; po ponownym zalogowaniu włącz ją w każdym notatniku osobno. Tak samo zapisanie innego serwera albo użytkownika wyłącza ją w pozostałych notatnikach — żaden nie zacznie sam wysyłać notatek na nowe konto.

Usunięcia są wstrzymywane do Twojej decyzji, gdy dotyczą wielu plików naraz — to zabezpieczenie przed pomyłką.

### Foldery
- **Pusty folder** założony na jednym urządzeniu pojawia się na serwerze i na pozostałych urządzeniach.
- **Usunięty folder** znika też po drugiej stronie, ale tylko wtedy, gdy nic w nim nie zostało. Folder, w którym jest notatka jeszcze niezsynchronizowana albo plik, którego Brulion nie synchronizuje (np. `.git`), zostaje na swoim miejscu.
- **Przeniesienie i zmiana nazwy** (folderu albo strony) to dla synchronizacji przeniesienie, a nie usunięcie: nie pojawia się pytanie o masowe usuwanie, nawet gdy przenosisz folder z setkami notatek. Prawdziwe usunięcie wielu notatek dalej czeka na Twoje potwierdzenie.
- Folder albo stronę, którym zmienisz nazwę albo które przeniesiesz w Brulionie, Brulion przenosi na serwerze jednym poleceniem — pliki nie są wysyłane od nowa. Dotyczy to także strony, w której zaraz po zmianie nazwy piszesz dalej: najpierw jest przenoszona, potem wysyłana jest tylko zmiana. Zmiany z innego urządzenia trafiają wtedy pod nową nazwę. Gdy serwer na to nie pozwala, pliki są wysyłane pod nową nazwą, a stare usuwane — nic nie ginie.
- Nazwę zmienioną poza Brulionem (np. w menedżerze plików) Brulion rozpoznaje tylko po niezmienionej treści. Stronę, którą potem zmieniono, wysyła pod nową nazwą, a starą usuwa na serwerze — nic nie ginie, ale na innych urządzeniach stara trafia do kosza.
- Folder albo strona, którym zmieniono nazwę albo które przeniesiono na innym urządzeniu albo w Nextcloudzie w przeglądarce (także w programie Nextcloud na komputerze), dostają tę samą nową nazwę tutaj. Notatki nie są pobierane od nowa i nie trafiają do kosza: idzie z nimi historia wersji, ulubione i zwinięte bloki. Brulion poznaje je po identyfikatorze pliku na serwerze (albo po jego wersji, gdy serwer identyfikatorów nie podaje).
- Gdy folder o nowej nazwie jest tu już zajęty albo folderu nie da się przemianować (np. na Windows inny program trzyma otwarty plik z tego folderu), Brulion przenosi notatki pojedynczo. Notatka, której nie da się przenieść, jest pobierana pod nową nazwą, a stara trafia do kosza — nic nie ginie.
- Gdy w tym czasie ktoś zmienił notatkę w przenoszonym folderze, żadna wersja nie ginie: zmiany są scalane, a gdy się nie da — zostają obie.
- Syncthing przenosi i usuwa foldery sam, nic nie trzeba ustawiać.

> [!note] Strony dokumentacji w Twoim notatniku
> W wersji 0.3.0 otwarcie tej dokumentacji przy włączonym Nextcloudzie synchronizowało ją z Twoim folderem na serwerze. Jej strony (np. „Spis treści”, „Synchronizacja”) mogły więc trafić do Twojego notatnika na wszystkich urządzeniach, a do dokumentacji — kopie Twoich notatek. Strony dokumentacji znajdziesz w swoim notatniku w widoku **Tagi**, pod tagiem `dokumentacja`, i możesz je tam usunąć: dokumentacja ma własne. Twoje notatki, które pojawiły się w dokumentacji, to tylko kopie.

## GitHub
Notatki trzymane w **repozytorium na GitHubie** — w całym repozytorium albo w jednym jego folderze — też mogą być notatnikiem. Brulion je pobiera, sam ściąga nowe wersje i odsyła twoje zmiany, kiedy o to poprosisz. Nie potrzebujesz do tego gita, a w przeglądarce działa to tak samo.

- **Nowy notatnik z GitHuba:** na ekranie startowym (*Przełącz notatnik → Otwórz inny notatnik…*) wybierz **Otwórz z GitHuba…** i wklej adres: `właściciel/nazwa` albo adres strony repozytorium lub jednego z jego folderów na github.com (na przykład `https://github.com/janusz-bit/brulion/tree/main/src/app/shell/guide/pl`). Gałąź i folder Brulion bierze z adresu. Notatnik otwiera się na karcie **GitHub** w Centrum synchronizacji, a jego notatki dochodzą po chwili.
- **Ten notatnik na GitHubie:** *Centrum synchronizacji → GitHub → Trzymaj ten notatnik na GitHubie*. Notatki, które są tylko tutaj, zostają tutaj, dopóki ich nie zatwierdzisz; przy notatkach, które się różnią, pojawia się wersja z GitHuba — jako konflikt do scalenia.
- **Nowe wersje** przychodzą przy otwarciu notatnika, co 15 minut, gdy Brulion jest na wierzchu, i po **Pobierz najnowszą wersję**. Notatka zmieniona i tutaj, i na GitHubie jest scalana wiersz po wierszu (rysunek — po elementach, tablica — po kartach); kiedy się nie da, zostaje twoja wersja, a wersja z GitHuba czeka obok jako `Notatka.conflict-GitHub-….md` w **Konfliktach**. Notatka usunięta na GitHubie trafia tutaj do kosza, a przemianowana lub przeniesiona — zmienia nazwę także tutaj.
- **Twoje zmiany** czekają na liście *Jeszcze nie na GitHubie*. **Zatwierdź…** wysyła zaznaczone notatki jednym commitem do gałęzi; **Zaproponuj zmiany…** wysyła je jako **pull request** — z twojej kopii repozytorium (forka), gdy nie możesz do niego pisać. Kolejna propozycja, póki pull request jest otwarty, dopisuje do niego nowe zmiany. Notatki z czekającym konfliktem idą dopiero po scaleniu.
- **Token:** publiczne repozytorium otwiera się bez konta, tylko do czytania. Prywatne, commity i pull requesty wymagają *osobistego tokenu dostępu* z ustawień GitHuba (*Settings → Developer settings → Personal access tokens*; klasycznego z zakresem `repo`, dla publicznych repozytoriów wystarczy `public_repo`). Wklej go w karcie **Konto GitHub**; zostaje tylko na tym urządzeniu i trafia tylko do GitHuba.
- Ukryte pliki repozytorium (`.gitignore`, `.github/…`) zostają nietknięte; pliki większe niż 50 MB i pliki trzymane w Git LFS zostają na GitHubie. Tak działa tylko GitHub — nie GitLab ani inne serwery Gita.

> [!tip] Poprawki w tej dokumentacji
> Ta dokumentacja mieszka w repozytorium Brulionu na GitHubie. W notatniku z dokumentacją *Centrum synchronizacji → GitHub → Otwórz dokumentację z GitHuba…* otwiera ją jako osobny notatnik, w którym poprawiasz stronę i proponujesz poprawkę jako pull request.

## Kalendarze (CalDAV i subskrypcje)
Karta *Centrum synchronizacji → Kalendarze* synchronizuje z tym notatnikiem **inne kalendarze**: subskrypcje kalendarzy po adresie (wydarzenia tylko do odczytu) i konta CalDAV — Nextcloud, Fastmail, iCloud, Radicale, Baïkal — których wydarzenia widać w kalendarzu, a zadania stają się liniami zadań w notatkach, synchronizowanymi w obie strony. Zobacz [[Dziennik, kalendarz i szablony#Inne kalendarze: subskrypcje i CalDAV|Inne kalendarze]].

- Kalendarze synchronizują się przy otwarciu notatnika, co kilka minut (5, 15 albo 60 — ustawienie konta), około 20 sekund po zmianie zsynchronizowanego zadania, gdy wraca sieć i gdy wracasz do Brulionu; subskrypcje są pobierane co 1, 6 albo 24 godziny. **Odśwież wszystkie** robi to od razu.
- Na Androidzie sprawdzanie notatek w tle synchronizuje też zadania kalendarzy, gdy Brulion jest zamknięty, więc przypomnienie ustawione przy zadaniu w innej aplikacji zadzwoni na telefonie.
- Gdy naraz miałoby zniknąć wiele zsynchronizowanych zadań (wyczyszczona strona, zepsuty serwer), nic nie zostanie usunięte, dopóki nie potwierdzisz tego przyciskiem **Usuń je**.
- **Co się działo** pokazuje, co zrobiła każda synchronizacja: nowe i zmienione zadania i wydarzenia, zadania zmienione po obu stronach i co zostało zachowane, a także błędy opisane słowami (złe hasło, serwer, który nie odpowiada…).
- Hasła są zapisane tak jak hasło aplikacji Nextcloud, w pliku, który może odczytać tylko Brulion. Używaj hasła aplikacji, nie hasła do konta.
- W wersji przeglądarkowej serwer musi pozwolić stronie Brulionu na odczyt (CORS); wiele serwerów i kanałów kalendarzy na to nie pozwala. Aplikacje na Linuksa, Windows i Androida nie mają tego ograniczenia.

## Centrum synchronizacji
Menu **Więcej** → **Notatnik** → *Centrum synchronizacji*, albo kliknięcie ikony stanu synchronizacji na dole paska ikon po lewej (dymek mówi, jaki to stan). Zakładki: Przegląd, Syncthing, Nextcloud, GitHub, Kalendarze, Konflikty.

- Na bieżąco widać, co się dzieje: etap, liczbę plików, czas trwania i pliki w drodze.
- Jeśli serwer długo nie odpowiada, pojawia się ostrzeżenie, a po 180 s próba jest przerywana i ponawiana — panel nie „synchronizuje” w nieskończoność.
- Po błędzie widać odliczanie do kolejnej próby i przycisk „Spróbuj teraz”.
- Historia ostatnich uruchomień pokazuje wynik, powód i czas; kliknięcie otwiera szczegóły.
- **Konflikty** rysunków pokazują obie wersje i wynik scalenia jako rysunki, nigdy jako tekst. **Scal rysunki** łączy linie i kształty z obu wersji (starte tylko na jednym urządzeniu wrócą), poprzednią wersję zapisuje w historii, a kopię przenosi do kosza. Kopia rysunku z Obsidiana, która ma inne notatki niż twój rysunek, zostaje po scaleniu, dopóki nie przeniesiesz z niej notatek i jej nie usuniesz. Zobacz [[Rysunki#Zapisywanie i synchronizacja|Zapisywanie i synchronizacja rysunków]].
- **Konflikty** tablic pokazują obie wersje jako obrazy z liczbą kart i połączeń w każdej. **Scal tablice** łączy karty i połączenia obu wersji — zostaje to, co w którejkolwiek dodano, przesunięto albo zmieniono (karta przesunięta w jednej i zmieniona w drugiej zachowa obie zmiany) —, poprzednią wersję zapisuje w historii, a kopię przenosi do kosza. Zobacz [[Tablice#Zapis, synchronizacja i eksport|Zapis i synchronizacja tablic]].

## Rejestr zdarzeń
Gdy coś nie działa, zajrzyj do **Rejestru zdarzeń** (*Ustawienia → Aplikacja*, albo „rejestr” w palecie poleceń). Można go filtrować i przeszukiwać, a potem skopiować lub zapisać do wysłania. Rejestr nie zawiera haseł, tokenów ani treści notatek; zostaje na urządzeniu, dopóki sam go nie udostępnisz. Przy problemach z synchronizacją włącz najpierw „Szczegółowy rejestr”, powtórz synchronizację i dopiero wtedy wyślij rejestr.

## Historia wersji i kosz
- Zanim notatka zostanie nadpisana, jej poprzednia wersja trafia do historii na urządzeniu. Menu strony → **Historia** pozwala porównać wersje i przywrócić dowolną.
- Usunięte strony wpadają do **Kosza**, skąd można je odzyskać. Stronę, załącznik albo folder usuniesz z menu **Więcej**, z drzewka stron (także klawiszem `Delete`), ze **Wszystkich stron** i z palety poleceń („Przenieś do kosza”), a **Cofnij** w powiadomieniu od razu je przywraca. Z kosza pliki znikają na dobre dopiero, gdy o to poprosisz (**⋯** przy pliku → **Usuń na zawsze…** albo **Opróżnij kosz**) — usunięcie roznosi się przez synchronizację na inne urządzenia, więc kosz jest zabezpieczeniem.
- **Sprzątanie** (menu **Więcej** i paleta poleceń) pokazuje notatki i załączniki, do których nie prowadzi żaden link, tag ani zapytanie. Wybierasz, co przenieść do kosza: notatki, załączniki albo pojedyncze pliki. Dzienników, szablonów i ulubionych tam nie ma.
- Dane robocze synchronizacji (historia, ślady scalania) leżą poza folderem notatnika, więc nie synchronizują się między urządzeniami.

Zobacz też [[Format plików|Format plików]].
