---
tags: [dokumentacja]
brulion: document
---
# Synchronizacja

Brulion nie ma własnego serwera. Notatki to zwykły folder, więc synchronizujesz go tym, czym lubisz. Zapis jest zawsze lokalny i natychmiastowy; synchronizacja dzieje się w tle i nie blokuje pisania.

## Syncthing (polecane do pracy offline)
1. Udostępnij folder notatnika w Syncthing (na Androidzie: Syncthing-Fork).
2. Na każdym urządzeniu wskaż w Brulionie ten sam folder (na Androidzie np. `/storage/emulated/0/Brulion` i zgoda „Dostęp do wszystkich plików”).
3. Gdy ta sama notatka zmieni się na dwóch urządzeniach, Syncthing zostawia kopię `*.sync-conflict-*`. Brulion nie pokazuje jej jako strony, tylko w centrum synchronizacji jako **konflikt do scalenia**.

Opcjonalnie Brulion pokazuje stan z API Syncthinga.

## Nextcloud (WebDAV)
*Ustawienia → Synchronizacja* (albo centrum synchronizacji): adres serwera, użytkownik i **hasło aplikacji** wygenerowane w Nextcloudzie. Zmiany z wielu urządzeń scalane są trójstronnie, więc zwykle nic nie ginie i nie musisz nic robić. Gdy scalenie jest niemożliwe, zostają obie wersje i dostajesz widok scalania.

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
- Folder albo stronę, którym zmienisz nazwę albo które przeniesiesz w Brulionie, Brulion przenosi na serwerze jednym poleceniem — pliki nie są wysyłane od nowa. Zmiany z innego urządzenia trafiają wtedy pod nową nazwę. Gdy serwer na to nie pozwala, pliki są wysyłane pod nową nazwą, a stare usuwane — nic nie ginie.
- Folder albo strona, którym zmieniono nazwę albo które przeniesiono na innym urządzeniu albo w Nextcloudzie w przeglądarce (także w programie Nextcloud na komputerze), dostają tę samą nową nazwę tutaj. Notatki nie są pobierane od nowa i nie trafiają do kosza: idzie z nimi historia wersji, ulubione i zwinięte bloki. Brulion poznaje je po identyfikatorze pliku na serwerze (albo po jego wersji, gdy serwer identyfikatorów nie podaje).
- Gdy nowa nazwa jest tu już zajęta albo folderu nie da się przemianować (np. na Windows inny program trzyma otwarty plik z tego folderu), notatki są pobierane pod nową nazwą, a stare trafiają do kosza — nic nie ginie.
- Gdy w tym czasie ktoś zmienił notatkę w przenoszonym folderze, żadna wersja nie ginie: zmiany są scalane, a gdy się nie da — zostają obie.
- Syncthing przenosi i usuwa foldery sam, nic nie trzeba ustawiać.

> [!note] Strony dokumentacji w Twoim notatniku
> W wersji 0.3.0 otwarcie tej dokumentacji przy włączonym Nextcloudzie synchronizowało ją z Twoim folderem na serwerze. Jej strony (np. „Spis treści”, „Synchronizacja”) mogły więc trafić do Twojego notatnika na wszystkich urządzeniach, a do dokumentacji — kopie Twoich notatek. Strony dokumentacji znajdziesz w swoim notatniku w widoku **Tagi**, pod tagiem `dokumentacja`, i możesz je tam usunąć: dokumentacja ma własne. Twoje notatki, które pojawiły się w dokumentacji, to tylko kopie.

## Centrum synchronizacji
Menu **Więcej** (⋮) → *Centrum synchronizacji*, albo kliknięcie stanu synchronizacji na dole paska bocznego. Zakładki: Przegląd, Syncthing, Nextcloud, Konflikty.

- Na bieżąco widać, co się dzieje: etap, liczbę plików, czas trwania i pliki w drodze.
- Jeśli serwer długo nie odpowiada, pojawia się ostrzeżenie, a po 180 s próba jest przerywana i ponawiana — panel nie „synchronizuje” w nieskończoność.
- Po błędzie widać odliczanie do kolejnej próby i przycisk „Spróbuj teraz”.
- Historia ostatnich uruchomień pokazuje wynik, powód i czas; kliknięcie otwiera szczegóły.

## Rejestr zdarzeń
Gdy coś nie działa, zajrzyj do **Rejestru zdarzeń** (*Ustawienia → Aplikacja*, albo „rejestr” w palecie poleceń). Można go filtrować i przeszukiwać, a potem skopiować lub zapisać do wysłania. Rejestr nie zawiera haseł, tokenów ani treści notatek; zostaje na urządzeniu, dopóki sam go nie udostępnisz. Przy problemach z synchronizacją włącz najpierw „Szczegółowy rejestr”, powtórz synchronizację i dopiero wtedy wyślij rejestr.

## Historia wersji i kosz
- Zanim notatka zostanie nadpisana, jej poprzednia wersja trafia do historii na urządzeniu. Menu strony → **Historia** pozwala porównać wersje i przywrócić dowolną.
- Usunięte strony wpadają do **Kosza**, skąd można je odzyskać.
- Dane robocze synchronizacji (historia, ślady scalania) leżą poza folderem notatnika, więc nie synchronizują się między urządzeniami.

Zobacz też [[Format plików|Format plików]].
