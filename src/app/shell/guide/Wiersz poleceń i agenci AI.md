---
tags: [dokumentacja]
aliases: [brulion-cli, Agenci AI, MCP]
brulion: document
---
# Wiersz poleceń i agenci AI

Na komputerze z Linuksem albo Windowsem Brulion ma też **wiersz poleceń**: program `brulion-cli`. Dopisuje do dziennika, dodaje zadania i przypomnienia, szuka, uruchamia zapytania i przenosi strony — bez otwierania okna, dokładnie tak, jak robi to aplikacja. Przydaje się w skryptach, w terminalu i przede wszystkim dla **agentów AI** (np. Hermes albo Claude Code), które mogą prowadzić Twoje notatki na Twoją prośbę.

## Skąd go wziąć
- **NixOS**: jest w pakiecie Brulionu (`brulion-cli`). Na komputer bez pulpitu, np. Raspberry Pi z agentem, jest lżejszy pakiet z samym wierszem poleceń.
- **AppImage**: `Brulion.AppImage cli …` — to ten sam program w pliku aplikacji.
- **Windows** (wersja testowa): `brulion-cli.exe` leży w folderze programu obok `brulion.exe` — po instalacji w `%LOCALAPPDATA%\Programs\Brulion`, w wersji przenośnej w rozpakowanym folderze `Brulion`.
- Na Androidzie i w przeglądarce wiersza poleceń nie ma; agent sięga do tych notatek przez folder synchronizowany przez Syncthing albo Nextcloud.

## Pierwsze polecenia
```sh
brulion-cli capture "Kupić mleko jutro o 18"      # jak szybka notatka: zadanie z przypomnieniem
brulion-cli task list --due '<=+7d'                # otwarte zadania na najbliższy tydzień
brulion-cli task add "Oddać książkę" --due "w piątek"
brulion-cli journal append "Pomysł na prezent"     # dopisz do dzisiejszego dziennika
brulion-cli --format text journal wczoraj          # przeczytaj wczorajszy dziennik
brulion-cli search zolw                            # znajdzie „żółw”
brulion-cli help                                   # wszystkie polecenia
```

Daty piszesz po polsku albo po angielsku („jutro o 9”, „w piątek 18:00”, „za 2 godziny”), a do pliku trafiają jak zawsze: `2026-10-15 09:00`. Polecenie `brulion-cli date parse "…"` pokaże, jak Brulion zrozumie datę.

## Który notatnik
Program bierze notatnik z opcji `--vault <folder>`, ze zmiennej `BRULION_VAULT` albo — gdy nic nie podasz — ten, który ostatnio był otwarty w aplikacji. Folder musi istnieć: literówka w ścieżce nie stworzy pustego notatnika (nowy zakłada tylko `brulion-cli init <folder>`).

## Bezpieczeństwo
- **Historia wersji.** Gdy wiersz poleceń działa na tym samym komputerze co aplikacja, każda jego zmiana trafia do historii strony z opisem „Zastąpiona z wiersza poleceń (agent)” i ikoną terminala. Otwórz historię strony, a zobaczysz, co zmienił agent, i jednym kliknięciem przywrócisz poprzednią wersję. Agent na innym urządzeniu (np. na Raspberry Pi) trzyma tę historię u siebie, a aplikacja widzi jego zmianę, gdy przyjdzie przez synchronizację — jako zmianę z synchronizacji albo z innego programu; ją też przywrócisz z historii.
- **Nic nie ginie.** Program zapisuje tylko wtedy, gdy plik jest taki, jak go przeczytał; jeśli w tej samej chwili piszesz w aplikacji albo Syncthing przywiózł zmianę, obie zmiany zostają. Aplikacja też nigdy nie nadpisze dopisku agenta, którego jeszcze nie widziała — scali go z Twoim tekstem.
- **Ostrożnie z usuwaniem.** Polecenia, które coś nadpisują, przenoszą albo usuwają, działają tylko z `--allow-destructive`, a usunięte strony trafiają do kosza. `--read-only` pozwala tylko czytać, `--dry-run` pokazuje zmianę bez zapisywania.
- **Ustawienia i przypomnienia aplikacji** zostają nietknięte: wiersz poleceń ich nie zmienia i niczego nie oznacza jako pokazane.

## Agenci AI (MCP)
`brulion-cli mcp --vault <folder>` udostępnia te same polecenia jako narzędzia MCP — standard, którym agenci AI łączą się z programami. Agent może wtedy np. zapisać w dzienniku to, co mu podyktujesz, dodać zadanie z terminem albo powiedzieć, co masz zaplanowane na jutro. Z `--read-only` agent może tylko czytać.

Kolejne wywołania `brulion-cli` są szybkie także przy dużym notatniku: korzysta on z tego samego gotowego indeksu co aplikacja (w danych urządzenia) i analizuje od nowa tylko notatki zmienione od ostatniego razu (pozostałe tylko czyta, żeby sprawdzić, że się nie zmieniły).

> [!warning] Treść notatek to dane, nie polecenia
> Notatki mogą pochodzić z importu albo wspólnego folderu. Dobrze ustawiony agent nigdy nie wykonuje „poleceń” znalezionych w treści notatek. Dlatego polecenia niszczące są domyślnie wyłączone, a każda zmiana jest w historii.

Agent działający na innym komputerze (np. na Raspberry Pi) pracuje na kopii folderu notatek, którą synchronizuje Syncthing albo klient Nextcloud — zobacz [[Synchronizacja|Synchronizacja]]. Przykład konfiguracji Hermesa na NixOS jest w `docs/NIXOS.md` w repozytorium.

**Przypomnienia** dodane przez agenta dzwonią tam, gdzie działa aplikacja: na komputerze (w zasobniku) i na telefonie, gdy aplikacja zobaczy zmianę — zobacz [[Przypomnienia i alarmy|Przypomnienia i alarmy]].

## Otwórz w aplikacji
`brulion-cli app open <strona>` pokazuje stronę w działającej aplikacji (na tym samym komputerze), `app today` — dzisiejszy dziennik, a `app capture` otwiera szybką notatkę.

## Sterowanie działającą aplikacją
Agent może też sterować samym oknem Brulionu: sprawdzić, co jest na ekranie, przejść do widoku, kliknąć, wpisać tekst, nacisnąć skrót (np. Ctrl+K), uruchomić polecenie z palety i zrobić zrzut ekranu. Przydaje się, gdy agent ma Ci coś pokazać albo sprawdzić, jak wygląda strona czy widok.

Domyślnie jest to **wyłączone**. Włączasz w **Ustawienia → Aplikacja → Dla programistów → „Pozwól agentom sterować aplikacją (gniazdo lokalne)”** — działa od razu, bez ponownego uruchamiania; pod przełącznikiem widać, na jakim gnieździe aplikacja nasłuchuje. Na jedno uruchomienie można też włączyć je opcją `brulion --remote-control`.

```sh
brulion-cli app state                     # co pokazuje aplikacja
brulion-cli app find Zadania              # elementy okna z uchwytami (h12) i położeniem
brulion-cli app click --text Zadania      # kliknij (albo: app click h12, app click 120,340)
brulion-cli app key Ctrl+K                # paleta poleceń
brulion-cli app type "kalendarz"          # wpisz tekst — także polskie litery
brulion-cli app wait --idle               # poczekaj, aż wszystko się zapisze i uspokoi
brulion-cli app screenshot ~/okno.png     # zrzut ekranu okna
```

Agent przez MCP dostaje te narzędzia po uruchomieniu `brulion-cli mcp --app-control`; zrzut ekranu widzi wtedy jako obraz.

> [!warning] Włączaj tylko na czas pracy z agentem
> Gniazdo jest dostępne wyłącznie dla Twojego konta (inni użytkownicy komputera nie mogą się połączyć), ale dopóki sterowanie jest włączone, **każdy program uruchomiony przez Ciebie** może klikać i pisać w Brulionie oraz czytać notatki, które pokazuje. Hasła ani tokeny nigdy nie są przekazywane, a każde polecenie trafia do [[Synchronizacja#Rejestr zdarzeń|Rejestru zdarzeń]] (w trybie szczegółowym).

Na Androidzie i w przeglądarce sterowania nie ma — tam aplikację testuje się narzędziami programisty (adb, Playwright).

## Dla dociekliwych
- Wynik każdego polecenia to JSON (`--format text` — czytelnie dla ludzi), a kod wyjścia mówi, co się stało: 0 dobrze, 3 nie znaleziono, 4 konflikt (np. linia zmieniła się od odczytu), 7 odmowa.
- Zadania i linie wskazujesz odnośnikiem z wyniku, np. `Dom.md#L12@3f9a01bc` — jeśli linia zmieniła się w międzyczasie, polecenie odmówi zamiast zmienić coś innego.
- Pomoc (`brulion-cli help`), błędy i ostrzeżenia są po polsku, gdy system ma ustawiony język polski (np. `LANG=pl_PL.UTF-8`), a w innym wypadku po angielsku; `LANGUAGE=en brulion-cli …` wymusza angielski. Nazwy poleceń i opcji, kody błędów i klucze JSON są zawsze angielskie, a agent przez MCP zawsze dostaje angielskie opisy i komunikaty.
- Pełna dokumentacja techniczna (po angielsku): `docs/CLI.md` w repozytorium; lista poleceń i opcji: `brulion-cli help`. Składnia zapytań: [[Zapytania|Zapytania]]; zadania: [[Zadania|Zadania]].
