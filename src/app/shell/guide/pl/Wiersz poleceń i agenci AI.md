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
brulion-cli week append "Przegląd celów"           # dopisz do notatki tego tygodnia
brulion-cli --format text month "zeszły miesiąc"   # przeczytaj notatkę poprzedniego miesiąca
brulion-cli --format text habit list                # nawyki: dzisiejsze odhaczenia, serie
brulion-cli habit tick Medytacja                   # odhacz nawyk na dziś
brulion-cli --format text card due                 # fiszki do nauki teraz
brulion-cli card review 1a2b3c4d dobrze            # oceń jedną (znowu, trudne, dobrze, łatwe)
brulion-cli --format text card notes --due         # notatki z #review do przeczytania teraz
brulion-cli card review-note "Plan" dobrze         # oceń całą notatkę (trudne, dobrze, łatwe)
brulion-cli search zolw                            # znajdzie „żółw”
brulion-cli search "ciasto z jabłkami" --semantic  # też po znaczeniu (zob. Szukanie)
brulion-cli clip install                           # połącz Web Clipper przeglądarek (zob. Import i eksport)
brulion-cli publish run Blog                       # opublikuj ponownie strony miejsca (zob. Import i eksport)
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

## Twoje notatki w claude.ai i ChatGPT
Same aplikacje AI — claude.ai, aplikacja Claude na telefonie, ChatGPT — też mogą korzystać z Twoich notatek, przez **zdalny serwer MCP**: `brulion-cli mcp serve` na Twoim komputerze, który ma notatki (np. domowy serwer z folderem synchronizowanym przez Syncthing). Dodajesz go raz w aplikacji jako własny łącznik i logujesz się własnym hasłem; na stronie logowania decydujesz, czy aplikacja może notatki tylko czytać, czy także w nich pisać.

**Na tym komputerze Brulion uruchamia go za Ciebie**, gdy jest otwarty: **Ustawienia → Rozszerzenia → claude.ai i ChatGPT**.

1. Wpisz **Hasło** (co najmniej 8 znaków; zostaje na tym urządzeniu) i włącz **Serwer łącznika**.
2. Udostępnij port przez HTTPS — np. `tailscale funnel --bg 8780` — i wpisz ten adres jako **Adres publiczny**.
3. **Kopiuj** przy **Adresie łącznika** i dodaj go w claude.ai albo ChatGPT jak niżej. **Tylko odczyt** sprawia, że żadna aplikacja nie zmieni notatek; **Zapytania** pokazują, o co aplikacje prosiły.

Na serwerze bez aplikacji:

1. Ustaw hasło: `brulion-cli mcp password` (zapisywany jest tylko jego skrót).
2. Uruchom serwer: `brulion-cli mcp serve --public-url https://notatki.example.com`. Aplikacje łączą się z nim przez internet, więc potrzebuje adresu HTTPS: odwrotnego proxy (Caddy, nginx) albo — dla komputera w domu — tunelu, np. Tailscale Funnel lub Cloudflare Tunnel. Na NixOS to jedna opcja: `services.brulion-mcp`.
3. W **claude.ai**: Ustawienia → Łączniki → dodaj własny łącznik (Settings → Connectors → Add custom connector), adres `https://notatki.example.com/mcp`, potem Połącz. Otworzy się strona Brulionu: wpisz hasło i wybierz **Zezwól**. Łącznik działa potem także w aplikacji Claude na telefonie. W **ChatGPT**: włącz tryb dewelopera i dodaj aplikację MCP z tym samym adresem i OAuth.
4. `brulion-cli mcp clients` pokazuje połączone aplikacje, `brulion-cli mcp revoke` od razu je odłącza.

> [!warning] Wybierz długie hasło
> Kto zna hasło, może połączyć aplikację z Twoimi notatkami. Na zapis pozwalaj tylko zaufanym aplikacjom: każda zmiana trafia do historii wersji, a usuwanie stron pozostaje wyłączone, dopóki nie uruchomisz serwera z `--allow-destructive`.

Szczegóły oraz konfiguracja proxy albo tunelu: „Remote MCP server” w `docs/CLI.md` w repozytorium; usługa NixOS w `docs/NIXOS.md`.

## Twój agent dla telefonu
`brulion-cli acp serve` uruchamia agenta AI tego komputera — Claude Code, Codex, OpenCode — dla Brulionu na Twoim telefonie, który sam go nie uruchomi ([[AI#Agent na innym komputerze]]). Agent pracuje tutaj, w kopii notatnika na tym komputerze: jego pliki, polecenia i narzędzia Brulionu są tego komputera, a loguje się tutaj (Twoja subskrypcja). Aplikacja na komputer uruchamia go sama: **Ustawienia → Rozszerzenia → Agenci AI → Udostępnij agentów telefonowi**; na komputerze bez aplikacji:

1. Uruchom go: `brulion-cli --vault ~/Notatki acp serve --agent claude --tailscale` — w Twoim tailnecie: nasłuchuje na adresie tego komputera w tailnecie, przedstawia się jego nazwą w tailnecie, a Twoje własne urządzenia parują się tam **bez kodu** (Tailscale za nie ręczy); na NixOS usługa `services.brulion-acp` z `tailscale = true`. Bez tailnetu: `--public-url https://agent.example.com`, nasłuchując na `127.0.0.1:8790`, dla odwrotnego proxy, które dodaje HTTPS **tylko w Twojej własnej sieci** (VPN): sparowany telefon steruje agentem, który wykonuje tu polecenia.
2. `brulion-cli --format text acp pair` pokazuje kod QR dla aparatu telefonu (otwiera parowanie w Brulionie, już wypełnione) i kod — 10 minut, jedno urządzenie, pięć błędnych prób; albo w Brulionie na telefonie: **Ustawienia → Rozszerzenia → Agenci AI → Dodaj → Agent na innym komputerze…**.
3. `brulion-cli acp clients` pokazuje sparowane urządzenia, `brulion-cli acp revoke <dev_…>` (albo `--all`) od razu odłącza urządzenie, a jego agenci się zatrzymują.

Z `--nextcloud https://cloud.example.com --nextcloud-user jan --nextcloud-folder Brulion --nextcloud-password-file F` (hasło aplikacji) komputer synchronizuje swoją kopię notatnika z Twoim Nextcloudem przed każdą turą — tura na to czeka, więc notatki, które przed chwilą zapisałeś na telefonie, już tam są — i po niej. Tura trwa dalej, gdy telefonu nie ma, a komputer przechowuje dla niego odpowiedź przez dobę; pytanie agenta też czeka na telefon, domyślnie godzinę (`--question-wait` w sekundach; `0`: od razu odrzucone). Także Brulion w przeglądarce może korzystać z agentów, gdy `acp serve` wymienia jego stronę: `--allowed-origin https://janusz-bit.github.io`. Szczegóły: „AI agents for other devices” w `docs/CLI.md` w repozytorium.

## Asystent Brulionu w edytorze
Edytory, które rozmawiają z agentami AI — **Zed** i środowiska **JetBrains** — mogą mieć wśród nich [[AI#Asystent AI|asystenta AI]] Brulionu: `brulion-cli acp agent`. Odpowiada o Twoich notatkach i zmienia je jak w czacie AI, a przed każdą zmianą pyta w edytorze (**Zgoda**, **Zgoda na wszystkie zmiany tego pytania**, **Nie**). Pyta model z **Ustawienia → Rozszerzenia → AI** z jego kluczem — Claude albo serwer zgodny z OpenAI; model na urządzeniu działa tylko w aplikacji.

W Zed dodaj go do `settings.json`:

```json
{
  "agent_servers": {
    "Brulion": {
      "type": "custom",
      "command": "brulion-cli",
      "args": ["acp", "agent", "--vault", "/home/ala/Notatki"]
    }
  }
}
```

W środowisku JetBrains: czat AI Assistant → **Add Custom Agent**, i ten sam wpis (bez `"type"`) w `~/.jetbrains/acp.json`. Bez `--vault` używa notatnika otwartego w aplikacji. Szczegóły: „The assistant in Zed and JetBrains” w `docs/CLI.md` w repozytorium.

## Rozszerzenia pisane przez agenta
Agent może też pisać [[Rozszerzenia|rozszerzenia]] — polecenia, widoki i reakcje w Luau — i wypróbować je, zanim je zobaczysz:

```sh
brulion-cli extension docs views                     # opis API (po angielsku) albo jego część
brulion-cli extension eval 'b.query("type:tasks status:open").total'
brulion-cli extension write "Licznik słów" --code "$(cat licznik.luau)" --description "Liczy słowa"
brulion-cli extension run "Licznik słów" policz --page Dom  # zmiany wracają jako różnica, nic nie jest zapisywane
brulion-cli extension list                           # „approval”: czeka na Ciebie
brulion-cli extension diff "Licznik słów"             # co się zmieniło, odkąd je tu zatwierdziłeś
brulion-cli extension settings Siłownia --set rest=90 # ustawienia rozszerzenia na tym urządzeniu
brulion-cli extension copy "Licznik słów" --to ~/Notatki/Praca  # do innego notatnika, gdzie też czeka
```

Kod nie uruchomi się w aplikacji, dopóki go tam nie zatwierdzisz (**Przeczytaj i uruchom** na stronie albo **Ustawienia → Rozszerzenia**), a każda zmiana agenta znowu na Ciebie czeka.

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

## Rozmowy w działającej aplikacji
Skrypty i agenci mogą też rozmawiać z czatem AI aplikacji: zacząć rozmowę z dowolnym rozmówcą (agentem AI, asystentem), poczekać na odpowiedź, przeczytać ją, wysłać następną wiadomość, zatrzymać — i zarządzać [[AI#Rozmowy według harmonogramu|rozmowami według harmonogramu]].

```sh
brulion-cli app chat partners                                     # kto może odpowiadać
brulion-cli app chat start "Czy ten plan ma sens?" --partner acp:codex --wait
brulion-cli app chat start "Sprawdź pociągi" --parent "Plan wyjazdu" --wake  # jej koniec budzi „Plan wyjazdu”
brulion-cli app chat list                                         # rozmowy i ich stan
brulion-cli app chat read "Czy ten plan ma sens"                  # jej wiadomości
brulion-cli app chat schedule add "Zaplanuj mi dzień" --when "w dni robocze o 7:30"
```

`wait` kończy się kodem 0 i odpowiedzią, 1 gdy tura się nie udała, 5 gdy czeka na Twoją odpowiedź w aplikacji (pozwolenie), 3 gdy wciąż trwa. Przy włączonym sterowaniu (wyżej) może to każdy skrypt; agent AI uruchomiony przez aplikację dostaje te narzędzia dla własnej rozmowy sam (`brulion-cli mcp --app-chat`) — to, co zacznie, to podrozmowy tej rozmowy ([[AI#Agenci pytają innych agentów]]); z `--wake` nie musi czekać: koniec tury podrozmowy wraca do jego rozmowy jako wiadomość.

O sekret, który możesz dać tylko Ty — token, hasło — agent prosi przez `brulion-cli app chat request-secret "Token GitHuba" --reason "…"` (narzędzie MCP `brulion_app_chat_request_secret`): odpowiadasz w prywatnej karcie aplikacji, a agent dostaje plik, który możesz przeczytać tylko Ty, usuwany, gdy kończy się jego odpowiedź ([[AI#Sekret, o który prosi agent]]). [[AI#Webhooki|Webhooki]] rozmów według harmonogramu należą tylko do Ciebie: `app chat schedule list` mówi, że harmonogram go ma, ale nigdy nie podaje adresu, i żaden agent nie może go utworzyć.

## Dla dociekliwych
- Wynik każdego polecenia to JSON (`--format text` — czytelnie dla ludzi), a kod wyjścia mówi, co się stało: 0 dobrze, 3 nie znaleziono, 4 konflikt (np. linia zmieniła się od odczytu), 7 odmowa.
- Zadania i linie wskazujesz odnośnikiem z wyniku, np. `Dom.md#L12@3f9a01bc` — jeśli linia zmieniła się w międzyczasie, polecenie odmówi zamiast zmienić coś innego.
- Pomoc (`brulion-cli help`), błędy i ostrzeżenia są po polsku, gdy system ma ustawiony język polski (np. `LANG=pl_PL.UTF-8`), a w innym wypadku po angielsku; `LANGUAGE=en brulion-cli …` wymusza angielski. Nazwy poleceń i opcji, kody błędów i klucze JSON są zawsze angielskie, a agent przez MCP zawsze dostaje angielskie opisy i komunikaty.
- Pełna dokumentacja techniczna (po angielsku): `docs/CLI.md` w repozytorium; lista poleceń i opcji: `brulion-cli help`. Składnia zapytań: [[Zapytania|Zapytania]]; zadania: [[Zadania|Zadania]].
