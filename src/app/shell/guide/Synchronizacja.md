---
tags: [dokumentacja]
brulion: document
---
# Synchronizacja

Brulion nie ma własnego serwera. Notatki to zwykły folder, więc synchronizujesz go tym, czym lubisz. Zapis jest zawsze lokalny i natychmiastowy; synchronizacja dzieje się w tle i nie blokuje pisania.

## Syncthing (polecane do pracy offline)
1. Udostępnij folder brulionu w Syncthing (na Androidzie: Syncthing-Fork).
2. Na każdym urządzeniu wskaż w Brulionie ten sam folder (na Androidzie np. `/storage/emulated/0/Brulion` i zgoda „Dostęp do wszystkich plików”).
3. Gdy ta sama notatka zmieni się na dwóch urządzeniach, Syncthing zostawia kopię `*.sync-conflict-*`. Brulion nie pokazuje jej jako strony, tylko w centrum synchronizacji jako **konflikt do scalenia**.

Opcjonalnie Brulion pokazuje stan z API Syncthinga.

## Nextcloud (WebDAV)
*Ustawienia → Synchronizacja* (albo centrum synchronizacji): adres serwera, użytkownik i **hasło aplikacji** wygenerowane w Nextcloudzie. Zmiany z wielu urządzeń scalane są trójstronnie, więc zwykle nic nie ginie i nie musisz nic robić. Gdy scalenie jest niemożliwe, zostają obie wersje i dostajesz widok scalania.

Usunięcia są wstrzymywane do Twojej decyzji, gdy dotyczą wielu plików naraz — to zabezpieczenie przed pomyłką.

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
- Dane robocze synchronizacji (historia, ślady scalania) leżą poza folderem brulionu, więc nie synchronizują się między urządzeniami.

Zobacz też [[Format plików|Format plików]].
