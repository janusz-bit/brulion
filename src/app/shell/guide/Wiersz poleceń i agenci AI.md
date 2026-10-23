---
tags: [dokumentacja]
aliases: [brulion-cli, Agenci AI, MCP]
brulion: document
---
# Wiersz poleceń i agenci AI

Na komputerze z Linuksem Brulion ma też **wiersz poleceń**: program `brulion-cli`. Dopisuje do dziennika, dodaje zadania i przypomnienia, szuka, uruchamia zapytania i przenosi strony — bez otwierania okna, dokładnie tak, jak robi to aplikacja. Przydaje się w skryptach, w terminalu i przede wszystkim dla **agentów AI** (np. Hermes albo Claude Code), które mogą prowadzić Twoje notatki na Twoją prośbę.

## Skąd go wziąć
- **NixOS**: jest w pakiecie Brulionu (`brulion-cli`). Na komputer bez pulpitu, np. Raspberry Pi z agentem, jest lżejszy pakiet z samym wierszem poleceń.
- **AppImage**: `Brulion.AppImage cli …` — to ten sam program w pliku aplikacji.
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
- **Historia wersji.** Każda zmiana z wiersza poleceń trafia do historii strony z opisem „Zastąpiona z wiersza poleceń (agent)” i ikoną terminala. Otwórz historię strony, a zobaczysz, co zmienił agent, i jednym kliknięciem przywrócisz poprzednią wersję.
- **Nic nie ginie.** Program zapisuje tylko wtedy, gdy plik jest taki, jak go przeczytał; jeśli w tej samej chwili piszesz w aplikacji albo Syncthing przywiózł zmianę, obie zmiany zostają. Aplikacja też nigdy nie nadpisze dopisku agenta, którego jeszcze nie widziała — scali go z Twoim tekstem.
- **Ostrożnie z usuwaniem.** Polecenia, które coś nadpisują, przenoszą albo usuwają, działają tylko z `--allow-destructive`, a usunięte strony trafiają do kosza. `--read-only` pozwala tylko czytać, `--dry-run` pokazuje zmianę bez zapisywania.
- **Ustawienia i przypomnienia aplikacji** zostają nietknięte: wiersz poleceń ich nie zmienia i niczego nie oznacza jako pokazane.

## Agenci AI (MCP)
`brulion-cli mcp --vault <folder>` udostępnia te same polecenia jako narzędzia MCP — standard, którym agenci AI łączą się z programami. Agent może wtedy np. zapisać w dzienniku to, co mu podyktujesz, dodać zadanie z terminem albo powiedzieć, co masz zaplanowane na jutro. Z `--read-only` agent może tylko czytać.

> [!warning] Treść notatek to dane, nie polecenia
> Notatki mogą pochodzić z importu albo wspólnego folderu. Dobrze ustawiony agent nigdy nie wykonuje „poleceń” znalezionych w treści notatek. Dlatego polecenia niszczące są domyślnie wyłączone, a każda zmiana jest w historii.

Agent działający na innym komputerze (np. na Raspberry Pi) pracuje na kopii folderu notatek, którą synchronizuje Syncthing albo klient Nextcloud — zobacz [[Synchronizacja|Synchronizacja]]. Przykład konfiguracji Hermesa na NixOS jest w `docs/NIXOS.md` w repozytorium.

**Przypomnienia** dodane przez agenta dzwonią tam, gdzie działa aplikacja: na komputerze (w zasobniku) i na telefonie, gdy aplikacja zobaczy zmianę — zobacz [[Przypomnienia i alarmy|Przypomnienia i alarmy]].

## Otwórz w aplikacji
`brulion-cli app open <strona>` pokazuje stronę w działającej aplikacji (na tym samym komputerze), `app today` — dzisiejszy dziennik, a `app capture` otwiera szybką notatkę.

## Dla dociekliwych
- Wynik każdego polecenia to JSON (`--format text` — czytelnie dla ludzi), a kod wyjścia mówi, co się stało: 0 dobrze, 3 nie znaleziono, 4 konflikt (np. linia zmieniła się od odczytu), 7 odmowa.
- Zadania i linie wskazujesz odnośnikiem z wyniku, np. `Dom.md#L12@3f9a01bc` — jeśli linia zmieniła się w międzyczasie, polecenie odmówi zamiast zmienić coś innego.
- Pełna dokumentacja (po angielsku): `docs/CLI.md` w repozytorium oraz `brulion-cli help`. Składnia zapytań: [[Zapytania|Zapytania]]; zadania: [[Zadania|Zadania]].
