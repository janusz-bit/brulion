# Brulion na NixOS

Repozytorium jest flake'iem Nixa. Daje:

| Wyjście | Co to jest |
|---|---|
| `packages.<system>.brulion` (= `default`) | aplikacja na komputer (C++/Qt 6.11 z nixpkgs): `bin/brulion`, wpis w menu, ikony, metadane AppStream |
| `nixosModules.default` | moduł NixOS: `programs.brulion` (aplikacja, autostart w zasobniku) i `services.brulion-web` (wersja przeglądarkowa przez nginx) |
| `overlays.default` | dodaje `pkgs.brulion` (zbudowany z nixpkgs tego, kto używa nakładki) |
| `devShells.<system>.default` | środowisko programisty (Qt 6.11, CMake, Ninja, ccache, clang-tools, gdb…) |
| `checks.<system>.*` | pakiet z testami, moduł NixOS, formatowanie plików `.nix`, test w maszynie wirtualnej |
| `formatter.<system>` | `nix fmt` (nixfmt dla wszystkich plików `.nix`) |

Systemy: `x86_64-linux` i `aarch64-linux` (np. Raspberry Pi). nixpkgs jest przypięty w
`flake.lock` do tej samej wersji co system dewelopera — nie aktualizujemy go przy okazji.
Brulion nie ma (jeszcze) własnego binarnego cache'u: pierwszy build kompiluje aplikację
lokalnie (kilka minut; na Raspberry Pi 4 dłużej), Qt i reszta zależności przychodzą gotowe
z cache.nixos.org.

## Instalacja na NixOS

### Flake systemu (`/etc/nixos`)

W `flake.nix` konfiguracji systemu (u autora: `github:janusz-bit/nixos`, katalog `/etc/nixos`)
dopisz wejście `brulion` i moduł:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    brulion = {
      url = "github:janusz-bit/brulion";
      # Zbuduj Brulion z tym samym nixpkgs co system: Qt jest wtedy ten sam co w reszcie
      # systemu (wtyczki motywów Qt pasują, nic nie pobiera się drugi raz). Bez tej linii
      # Brulion używa nixpkgs z własnego flake.lock — wersji, z którą jest testowany.
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, brulion, ... }: {
    nixosConfigurations.moj-komputer = nixpkgs.lib.nixosSystem {
      modules = [
        ./configuration.nix
        brulion.nixosModules.default
        {
          programs.brulion = {
            enable = true;
            # Start z sesją graficzną, schowany w zasobniku — przypomnienia działają cały czas.
            autostart = true;
          };
        }
      ];
    };
  };
}
```

Potem jak zwykle:

```sh
sudo nixos-rebuild switch --flake /etc/nixos#moj-komputer
```

Brulion pojawi się w menu aplikacji (kategorie „Biuro” i „Narzędzia”), z terminala: `brulion`.
Aktualizacja do najnowszej wersji z GitHuba: `nix flake update brulion` w `/etc/nixos`, potem
znów `nixos-rebuild switch`. Przypięcie do wydania: `url = "github:janusz-bit/brulion/v0.1.0";`.

### Opcje `programs.brulion`

| Opcja | Domyślnie | Opis |
|---|---|---|
| `enable` | `false` | instaluje Brulion (`environment.systemPackages`: program, wpis w menu, ikony) |
| `package` | pakiet z tego flake'a | inny pakiet, np. `pkgs.brulion` z nakładki albo własny build |
| `autostart` | `false` | wpis `/etc/xdg/autostart/io.github.januszbit.brulion.desktop` uruchamiający `brulion --hidden` przy logowaniu: aplikacja startuje schowana w zasobniku systemowym |

### Bez modułu

Sam pakiet (wejście flake'a przekazane do modułów przez `specialArgs = { inherit brulion; };`):

```nix
{ pkgs, brulion, ... }:
{
  environment.systemPackages = [ brulion.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

albo nakładka: `nixpkgs.overlays = [ brulion.overlays.default ];` i `pkgs.brulion`.
Wypróbowanie bez instalowania:

```sh
nix run github:janusz-bit/brulion
```

### Zasobnik, autostart, powiadomienia

* Przypomnienia i alarmy pokazuje działający Brulion (powiadomienia freedesktop przez D-Bus).
  Zamknięcie okna chowa go do **zasobnika systemowego**; z `autostart = true` jest tam od
  zalogowania. Każdy użytkownik może wyłączyć autostart u siebie w ustawieniach autostartu
  pulpitu (KDE: Ustawienia systemowe → Autostart; GNOME: Dostrajanie → Programy startowe) —
  to tworzy wpis z `Hidden=true` w `~/.config/autostart/`.
* **Kalendarz z zasobnika**: menu ikony → **Kalendarz** otwiera małe okienko z miesiącem,
  zadaniami i przypomnieniami wybranego dnia, **Otwórz Brulion** i **Szybka notatka…**.
  *Ustawienia → Aplikacja → Kliknięcie ikony w zasobniku → Kalendarz* otwiera je samym
  kliknięciem ikony (domyślnie na Linuksie kliknięcie pokazuje okno). Pod X11 okienko pojawia się
  przy kursorze i znika po kliknięciu obok; pod Waylandem kompozytor kładzie je, gdzie chce, i może
  nie oddać mu aktywności — wtedy zamkniesz je przyciskiem **×** albo ponownym kliknięciem ikony.
  Szczegóły: docs/WINDOWS.md, „Kalendarz z zasobnika”.
* KDE Plasma, XFCE, Cinnamon, Budgie, waybar/swaybar: zasobnik działa od razu. **GNOME** nie
  ma zasobnika — potrzebne jest rozszerzenie AppIndicator:

  ```nix
  environment.systemPackages = [ pkgs.gnomeExtensions.appindicator ];
  services.udev.packages = [ pkgs.gnome-settings-daemon ];
  ```

  i włączenie go w aplikacji „Rozszerzenia”.
* Powiadomienia pokazuje demon powiadomień pulpitu (w środowiskach graficznych jest zawsze; przy
  samym menedżerze okien np. `mako` albo `dunst`).
* **Dźwięki** (*Ustawienia → Przypomnienia → Dźwięk przypomnień / Dźwięk alarmów*): „Domyślny”
  (przypomnienia — jak inne powiadomienia pulpitu, alarmy — `alarm-clock-elapsed`), „Dzwonek”
  (**Wybierz dzwonek…**: dźwięk z motywu dźwiękowego — `message-new-instant`, `bell`,
  `complete`, `alarm-clock-elapsed`, `phone-incoming-call` — albo własny plik `.oga`/`.ogg`/`.wav`)
  i „Cisza”. Brulion przekazuje je w powiadomieniu jako podpowiedzi `sound-name`, `sound-file`
  albo `suppress-sound`; dźwięk odtwarza demon powiadomień (GNOME Shell, KDE Plasma — z pakietem
  motywu dźwięków, np. `sound-theme-freedesktop`). Demony bez dźwięków (`mako`, `dunst`) je
  pomijają. **Odtwórz próbkę** wysyła krótkie powiadomienie próbne z wybranym dźwiękiem.
* Pliki integracji z pulpitem: `share/applications/io.github.januszbit.brulion.desktop`,
  `share/metainfo/io.github.januszbit.brulion.metainfo.xml`, ikona
  `share/icons/hicolor/{scalable,256x256}/apps/io.github.januszbit.brulion.{svg,png}`.

### Notatki i Syncthing

Notatki to zwykłe pliki `.md` w wybranym folderze. Synchronizacja z telefonem i innymi
komputerami przez Syncthing z NixOS:

```nix
services.syncthing = {
  enable = true;
  user = "janusz";              # Twój użytkownik
  dataDir = "/home/janusz";
  openDefaultPorts = true;
};
```

Folder notatek dodajesz w panelu Syncthing (<http://127.0.0.1:8384>). Alternatywnie Nextcloud
(WebDAV) w ustawieniach synchronizacji Brulionu — aplikacja na komputer nie potrzebuje do tego
żadnej konfiguracji systemu.

### Agent AI (Hermes) i wiersz poleceń

Pakiet ma też `brulion-cli` — wiersz poleceń dla skryptów i agentów AI ([CLI.md](CLI.md)): czyta
i zmienia notatki tym samym kodem co aplikacja, a `brulion-cli mcp` udostępnia polecenia jako
narzędzia MCP. Na komputer bez pulpitu (np. Raspberry Pi z agentem) wystarczy lekki pakiet
**`brulion-cli`** z tego flake'a — sam Qt Core, bez Qt Quick, QML, SVG i Waylanda, więc buduje się
szybko i zajmuje mało miejsca:

```nix
environment.systemPackages = [ brulion.packages.${pkgs.stdenv.hostPlatform.system}.brulion-cli ];
```

albo bez instalowania: `nix run github:janusz-bit/brulion#brulion-cli -- --vault ~/Notatki task list`.

Agent na innym komputerze niż aplikacja dostaje notatki jako **folder synchronizowany** —
Syncthingiem (polecane) albo klientem Nextcloud — i pracuje na jego lokalnej kopii. Przykład:
Hermes jako użytkownik `hermes` na Raspberry Pi, folder `/var/lib/hermes/Brulion` udostępniony
z komputera Syncthingiem, który na Pi działa jako ten sam użytkownik:

```nix
{ pkgs, lib, brulion, ... }:
let
  brulionCli = brulion.packages.${pkgs.stdenv.hostPlatform.system}.brulion-cli;
in
{
  services.syncthing = {
    enable = true;
    user = "hermes";                       # pliki należą do agenta
    dataDir = "/var/lib/hermes";
    configDir = "/var/lib/hermes/.config/syncthing";
  };

  services.hermes-agent.mcpServers.brulion = {
    command = lib.getExe brulionCli;
    args = [ "mcp" "--vault" "/var/lib/hermes/Brulion" ];   # + "--read-only": tylko czytanie
    connect_timeout = 60;                  # pierwsze uruchomienie indeksuje notatnik
    timeout = 120;
  };
}
```

Folder `Brulion` dodajesz w panelu Syncthinga na obu komputerach (na Pi np. przez
`ssh -L 8384:127.0.0.1:8384 pi`). Agent dostaje narzędzia `brulion_capture`, `brulion_task_add`,
`brulion_task_list`, `brulion_search`, `brulion_query`, `brulion_journal_append` i inne; polecenia
niszczące (zmiana całej strony, przenoszenie, usuwanie) pojawiają się dopiero z
`--allow-destructive`. Każda zmiana agenta trafia do historii wersji z opisem „Zastąpiona z
wiersza poleceń (agent)” — na urządzeniu, na którym działa `brulion-cli` (stan urządzenia w
`~/.local/share/Brulion/Brulion/vaults/<id>` użytkownika agenta). Poradnik dla agenta (format
agentskills) leży w pakiecie: `share/brulion/agent-skills/brulion/SKILL.md`.

**Przypomnienia** dodane przez agenta dzwonią tam, gdzie działa aplikacja: na komputerze (w
zasobniku, `autostart = true`) i na telefonie, gdy aplikacja zobaczy zmianę. Sam `brulion-cli`
nigdy nie uruchamia przypomnień ani synchronizacji i nie zmienia ustawień aplikacji.

## Wersja przeglądarkowa na własnym serwerze (`services.brulion-web`)

Moduł serwuje wersję WebAssembly (PWA, działa offline) przez nginx: poprawny typ MIME
`application/wasm` (kompilacja strumieniowa), pliki skompresowane z góry (gzip i brotli,
`brulion.wasm` ok. 3× mniejszy), nagłówki cache dobrane do service workera i opcjonalnie
pośrednik do WebDAV Nextclouda (bez problemów z CORS). Szczegóły samej wersji przeglądarkowej:
[WEB.md](WEB.md).

### Skąd wziąć pliki

nixpkgs nie ma Qt dla WebAssembly, więc Nix nie buduje wersji przeglądarkowej sam. Są dwie
drogi:

1. **Wydanie z GitHuba** (zalecane): do każdego wydania dołączony jest
   `brulion-web-<wersja>.zip`. Moduł pobiera go sam:

   ```nix
   services.brulion-web.release = {
     version = "0.1.0";
     hash = "";   # pierwszy build się nie uda i wypisze właściwy hash: "got: sha256-…"
   };
   ```

   Wydania testowe (tag z sufiksem) — `release.tag = "v0.2.0-rc.1";` (pliki mają wersję
   `0.2.0`).
2. **Własny build** z `scripts/wasm-build.sh` (katalog `dist-wasm/`, patrz WEB.md):
   * `services.brulion-web.package = ./dist-wasm;` — kopia trafia do Nix store (we flake'u
     katalog musi być dodany do gita), z plikami skompresowanymi z góry;
   * albo ścieżka poza store jako napis, np. `package = "/srv/brulion-web";` — nginx serwuje
     katalog wprost (kompresja w locie, gzip), a podmiana plików (`rsync`) nie wymaga
     `nixos-rebuild`. Katalog musi być czytelny dla użytkownika `nginx`.

### Przykład

```nix
{
  services.brulion-web = {
    enable = true;
    release = { version = "0.1.0"; hash = "sha256-…"; };
    hostName = "notatki.example.com";
    nextcloudUrl = "https://cloud.example.com";   # opcjonalnie, niżej
    openFirewall = true;
  };

  # TLS i pozostałe ustawienia hosta — bezpośrednio w nginx:
  services.nginx.virtualHosts."notatki.example.com" = {
    enableACME = true;
    forceSSL = true;
  };
  security.acme = {
    acceptTerms = true;
    defaults.email = "ja@example.com";
  };
}
```

Przeglądarki pozwalają na service workera (praca offline, „Zainstaluj aplikację”),
powiadomienia i trwałe IndexedDB tylko przez **HTTPS** albo na `localhost` — na serwerze
włącz TLS.

### Opcje `services.brulion-web`

| Opcja | Domyślnie | Opis |
|---|---|---|
| `enable` | `false` | serwuje Brulion przez nginx |
| `package` | `null` | katalog z wersją przeglądarkową (`index.html`, `brulion.wasm`, `brulion.js`, `qtloader.js`, `sw.js`…): ścieżka/pakiet w store albo napis ze ścieżką poza store; ma pierwszeństwo przed `release` |
| `release` | `null` | `{ version; hash; tag ? null; }` — wersja przeglądarkowa z wydania na GitHubie |
| `hostName` | `"localhost"` | nazwa hosta wirtualnego nginx; TLS, ACME, adresy i porty ustawiasz w `services.nginx.virtualHosts.<hostName>` |
| `basePath` | `"/"` | ścieżka URL aplikacji, np. `"/notatki/"` (zaczyna się i kończy `/`); adresy w buildzie są względne, więc działa bez przebudowy; `/notatki` przekierowuje na `/notatki/` |
| `nextcloudUrl` | `null` | adres Nextclouda, do którego nginx przekazuje WebDAV (niżej) |
| `maxUploadSize` | `"512M"` | największy plik wysyłany przez pośrednika WebDAV |
| `precompress` | `true` | pliki `.gz`/`.br` obok oryginałów, liczone raz przy budowaniu (tylko dla plików w store) |
| `brotli` | `true` | serwuje też brotli (moduł `ngx_brotli`, jak `services.nginx.recommendedBrotliSettings`) |
| `contentSecurityPolicy` | `null` | nagłówek `Content-Security-Policy`; przed włączeniem sprawdź politykę w konsoli przeglądarki (powłoka HTML ma skrypt w treści strony, kod Emscriptena może wymagać `'unsafe-eval'`) |
| `openFirewall` | `false` | otwiera porty hosta (80, z TLS także 443) |

Nagłówki: `brulion.js`, `brulion.wasm` i `qtloader.js` są pobierane z `?v=<hash treści>` —
takie adresy mają `Cache-Control: public, max-age=31536000, immutable`; wszystko inne
(`index.html`, `sw.js`, manifest, ikony) `no-cache`, czyli sprawdzenie przy każdym użyciu
(odpowiedź `304`, gdy nic się nie zmieniło — ETag nginx z nixpkgs bierze się z hasha ścieżki
w store, więc nowa wersja zawsze ma nowy ETag). Do tego `X-Content-Type-Options: nosniff` i
`Referrer-Policy: no-referrer`.

### Nextcloud bez problemów z CORS

Wersja przeglądarkowa łączy się z WebDAV z poziomu strony, a przeglądarka blokuje zapytania do
innego serwera, jeśli ten nie zezwala na CORS — Nextcloud nie zezwala. Z `nextcloudUrl` nginx
przekazuje ścieżkę `<ścieżka Nextclouda>/remote.php/dav/` hosta Brulionu do Nextclouda pod tę
samą ścieżkę (odpowiedzi WebDAV z adresami `/…/remote.php/dav/files/…` pasują wtedy do
adresów, o które pytała aplikacja).

W Brulionie (ustawienia synchronizacji → Nextcloud) jako adres serwera podaj wtedy **adres
Brulionu** zamiast adresu Nextclouda, np. `https://notatki.example.com` zamiast
`https://cloud.example.com`. Jeśli Nextcloud działa pod podścieżką
(`nextcloudUrl = "https://example.com/nextcloud"`), dopisz ją:
`https://notatki.example.com/nextcloud`. Logowanie: nazwa użytkownika i **hasło aplikacji**
(Nextcloud → Ustawienia → Bezpieczeństwo). Ciasteczka Nextclouda nie są przekazywane —
Brulion uwierzytelnia każde zapytanie. Certyfikat HTTPS Nextclouda jest sprawdzany (systemowe
CA).

* nginx rozwiązuje nazwę Nextclouda przy starcie; jeśli DNS bywa wtedy niedostępny, ustaw
  `services.nginx.proxyResolveWhileRunning = true;` i `services.nginx.resolver.addresses`.
* Nextcloud na **tym samym serwerze**: zamiast pośrednika podaj jako `hostName` host
  Nextclouda i podścieżkę, np.
  `services.brulion-web = { hostName = config.services.nextcloud.hostName; basePath = "/brulion/"; };`
  — aplikacja i Nextcloud mają wtedy ten sam origin, `nextcloudUrl` jest niepotrzebny, a w
  Brulionie podajesz zwykły adres Nextclouda.
* Aplikacje na komputer i na Androida nie podlegają CORS — tam zawsze zwykły adres Nextclouda.

## Praca nad kodem

```sh
nix develop                    # Qt 6.11, cmake, ninja, ccache, clang-tools, gdb, nixfmt
scripts/dev-build.sh           # konfiguracja + build w ./build (Debug, -Werror, z blokadą)
scripts/dev-build.sh -t tst_dates tst_dates   # tylko jeden test (cel + regex)
./build/src/app/brulion        # uruchomienie; bez ekranu: QT_QPA_PLATFORM=offscreen
```

`nix develop` ustawia `QT_PLUGIN_PATH` i `QML_IMPORT_PATH`, więc niezawinięty program z
`./build` znajduje wtyczki Qt i moduły QML. Szczegóły i zasady: `CLAUDE.md`,
[ARCHITECTURE.md](ARCHITECTURE.md).

Polecenia Nixa:

```sh
nix build .#brulion -L         # pakiet -> ./result (bez testów; walidacja .desktop/AppStream)
nix build .#brulion-cli -L     # sam wiersz poleceń (BRULION_BUILD_APP=OFF, bez Qt Quick)
nix flake check -L             # pakiet, moduł NixOS, formatowanie, test VM (x86_64)
nix fmt                        # formatowanie plików .nix (nixfmt)
```

Co sprawdza `nix flake check`:

| Check | Co robi |
|---|---|
| `brulion` | build pakietu, testy jednostkowe (`ctest`), `desktop-file-validate` i `appstreamcli validate-tree` na zainstalowanych plikach, `brulion-cli version` i zapis szybkiej notatki w próbnym notatniku |
| `brulion-cli` | sam wiersz poleceń (bez Qt Quick): ten sam test zapisu |
| `nixos-module` | minimalny system NixOS z `programs.brulion` (+ autostart) i `services.brulion-web` (+ pośrednik Nextcloud) się ewaluuje (`toplevel.drvPath`, bez budowania systemu); konfiguracja nginx przechodzi `gixy`, wpis autostartu `desktop-file-validate`; wariant z `release` i `basePath`; asercja przy braku pakietu |
| `nixfmt` | wszystkie pliki `.nix` są sformatowane (`nix fmt`) |
| `web-vm` (tylko x86_64, wymaga KVM) | maszyna wirtualna z modułem: typy MIME, gzip/brotli, nagłówki cache i ETag, przekierowanie `basePath`, pośrednik WebDAV (fałszywy Nextcloud: ścieżki z polskimi znakami, ciasteczka, duży `PUT`), wpis autostartu i start aplikacji bez ekranu |

**Raspberry Pi (maszyna deweloperska) nie robi ciężkich buildów** — `nix flake check` i
`nix build` uruchamiaj na laptopie (x86_64) przez `laptop-run`, zawsze pod wspólną blokadą
(nowe pliki najpierw `git add -N`, bo `laptop-run` kopiuje tylko pliki śledzone przez gita):

```sh
git add -N <nowe pliki>
flock <lock> laptop-run bash -c 'nix flake check -L && nix build .#brulion -L'
```

### Pliki

| Ścieżka | Zawartość |
|---|---|
| `flake.nix`, `flake.lock` | wyjścia flake'a, przypięty nixpkgs |
| `nix/package.nix` | pakiet (`version` zmienia `scripts/release.sh` razem z `CMakeLists.txt`) |
| `nix/cli.nix` | sam `brulion-cli` (`packages.<system>.brulion-cli`, `apps.brulion-cli`) |
| `nix/module.nix` | moduł NixOS (`programs.brulion`, `services.brulion-web`) |
| `nix/web-release.nix` | pobieranie `brulion-web-<wersja>.zip` z wydania |
| `nix/web-site.nix` | kopia wersji przeglądarkowej z plikami `.gz`/`.br` |
| `nix/tests/` | `module-eval.nix`, `web-vm.nix`, `fake-web-build.nix` (zastępcza „wersja przeglądarkowa” do testów) |
| `cmake/BrulionInstall.cmake` | reguły `cmake --install` dla plików pulpitu (Linux) |
| `packaging/` | `io.github.januszbit.brulion.desktop`, `.metainfo.xml`, ikona PNG 256 px, `agent/brulion/SKILL.md` (poradnik dla agentów AI) |

Ikona PNG jest wygenerowana z `assets/logo.svg`; po zmianie logo:

```sh
nix shell nixpkgs#librsvg nixpkgs#optipng -c sh -c '
  rsvg-convert -w 256 -h 256 assets/logo.svg \
    -o packaging/icons/hicolor/256x256/apps/io.github.januszbit.brulion.png &&
  optipng -o5 -strip all packaging/icons/hicolor/256x256/apps/io.github.januszbit.brulion.png'
```

Pliki pulpitu sprawdzisz lokalnie bez budowania:

```sh
nix shell nixpkgs#desktop-file-utils nixpkgs#appstream -c sh -c '
  desktop-file-validate packaging/io.github.januszbit.brulion.desktop &&
  appstreamcli validate --no-net packaging/io.github.januszbit.brulion.metainfo.xml'
```

## Gdy coś nie działa

* **Brak ikony w zasobniku** — patrz „Zasobnik” (GNOME potrzebuje rozszerzenia AppIndicator).
* **Okno bez motywu / ostrzeżenia o wtyczkach Qt** — Brulion zbudowany z innym nixpkgs niż
  system ładuje systemowe wtyczki Qt innej wersji. Dodaj `inputs.nixpkgs.follows = "nixpkgs"`
  (wyżej).
* **Uruchamiaj `brulion`, nie `bin/.brulion-wrapped`** — wrapper ustawia ścieżki wtyczek Qt i
  modułów QML.
* **Wersja przeglądarkowa: `hash mismatch` przy `release`** — nowa wersja ma nowy plik zip:
  wpisz hash z komunikatu (`got: sha256-…`).
* **nginx nie startuje: `host not found in upstream`** — DNS niedostępny przy starcie, patrz
  `proxyResolveWhileRunning` wyżej.
