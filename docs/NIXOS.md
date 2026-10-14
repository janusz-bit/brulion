# Brulion na NixOS

Repozytorium jest flake'iem Nixa. Daje:

| Wyjście | Co to jest |
|---|---|
| `packages.<system>.brulion` (= `default`) | aplikacja na komputer (Tauri): `bin/brulion`, wpis w menu, ikony |
| `packages.<system>.brulion-web` | wersja przeglądarkowa (PWA) — statyczne pliki do serwowania |
| `nixosModules.default` | moduł NixOS: `programs.brulion` i `services.brulion-web` |
| `overlays.default` | dodaje `pkgs.brulion` i `pkgs.brulion-web` |
| `devShells.<system>.default` | środowisko programisty (Node 22, Rust, cargo-tauri, webkitgtk…) |
| `checks.<system>.*` | budowanie pakietów, testy jednostkowe, moduł, test w maszynie wirtualnej |

Systemy: `x86_64-linux` i `aarch64-linux`. Brulion nie ma (jeszcze) własnego binarnego cache'u,
więc pierwszy build kompiluje część w Ruście lokalnie (kilka do kilkunastu minut, zależnie od
procesora); wszystkie zależności (webkitgtk, Node, Rust) przychodzą gotowe z cache.nixos.org.

## NixOS

### Instalacja przez flake systemu

W `flake.nix` konfiguracji systemu:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    brulion = {
      url = "github:janusz-bit/brulion";
      # Opcjonalnie (dla systemu na nixos-unstable): zbuduj Brulion z tym samym nixpkgs co
      # system — webkitgtk itd. nie są wtedy pobierane drugi raz. Bez tej linii Brulion używa
      # nixpkgs z własnego flake.lock, czyli wersji, z którą jest testowany.
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, brulion, ... }: {
    nixosConfigurations.moj-komputer = nixpkgs.lib.nixosSystem {
      modules = [
        ./configuration.nix
        brulion.nixosModules.default
        {
          programs.brulion.enable = true;
          # Start z sesją graficzną, ukryty w zasobniku — przypomnienia działają cały czas.
          programs.brulion.autostart = true;
        }
      ];
    };
  };
}
```

Potem jak zwykle `sudo nixos-rebuild switch --flake .#moj-komputer`. Brulion pojawi się w menu
aplikacji (kategoria „Biuro”), z terminala: `brulion`.

Bez modułu wystarczy sam pakiet (`brulion` trafia do modułów przez
`specialArgs = { inherit brulion; };` w `nixosSystem`):

```nix
{ pkgs, brulion, ... }: {
  environment.systemPackages = [ brulion.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

albo nakładka: `nixpkgs.overlays = [ brulion.overlays.default ];` i `pkgs.brulion`.

Wypróbowanie bez instalowania:

```sh
nix run github:janusz-bit/brulion
```

Aktualizacja do najnowszej wersji z GitHuba: `nix flake update brulion` w katalogu konfiguracji
systemu, potem `nixos-rebuild switch`.

### Opcje `programs.brulion`

| Opcja | Domyślnie | Opis |
|---|---|---|
| `enable` | `false` | instaluje Brulion (pakiet w `environment.systemPackages`) |
| `package` | pakiet z tego flake'a | inny pakiet, np. `pkgs.brulion` z nakładki |
| `autostart` | `false` | wpis `/etc/xdg/autostart/brulion.desktop` uruchamiający `brulion --hidden` przy logowaniu: aplikacja startuje schowana w zasobniku systemowym |

### Zasobnik, powiadomienia, przypomnienia

* Na komputerze przypomnienia i alarmy pokazuje działający Brulion. Zamknięcie okna chowa go do
  **zasobnika systemowego** (ikona przy zegarze; „Zakończ” w jej menu zamyka aplikację), więc
  przypomnienia działają dalej. Z `autostart = true` Brulion jest w zasobniku od zalogowania.
* KDE Plasma, XFCE, Cinnamon, Budgie, waybar/swaybar: zasobnik działa od razu. **GNOME** nie ma
  zasobnika — dodaj rozszerzenie AppIndicator:

  ```nix
  environment.systemPackages = [ pkgs.gnomeExtensions.appindicator ];
  services.udev.packages = [ pkgs.gnome-settings-daemon ];
  ```

  i włącz je w aplikacji „Rozszerzenia”. Bez zasobnika zamknięcie okna kończy Brulion.
* Powiadomienia pokazuje demon powiadomień pulpitu (w środowiskach graficznych jest zawsze; przy
  samym menedżerze okien np. `mako` albo `dunst`).

### Notatki i Syncthing

Domyślny notatnik na komputerze to folder `~/Brulion`. Żeby synchronizować go z telefonem i innymi
komputerami, wystarczy Syncthing z NixOS:

```nix
services.syncthing = {
  enable = true;
  user = "janusz";                     # Twój użytkownik
  dataDir = "/home/janusz";
  openDefaultPorts = true;
};
```

Folder `~/Brulion` dodajesz w panelu Syncthing (<http://127.0.0.1:8384>). Brulion sam zauważa
zmiany zrobione przez Syncthing i pokazuje konflikty (`*.sync-conflict-*`) do scalenia.
Alternatywnie Nextcloud: Ustawienia → Synchronizacja w Brulionie (WebDAV, bez dodatkowej
konfiguracji systemu).

### Gdy coś nie działa

* **Puste albo migające okno** (niektóre karty NVIDIA, starsze sterowniki Mesa):
  `WEBKIT_DISABLE_DMABUF_RENDERER=1 brulion`. Na stałe:
  `environment.sessionVariables.WEBKIT_DISABLE_DMABUF_RENDERER = "1";`
* **Brak ikony w zasobniku** — patrz wyżej (GNOME). Pakiet sam dostarcza bibliotekę
  `libayatana-appindicator` (wrapper ustawia `LD_LIBRARY_PATH`).
* **Błędy TLS przy synchronizacji** — wrapper `bin/brulion` dodaje moduł TLS (`glib-networking`)
  i resztę środowiska GTK/WebKit; uruchamiaj Brulion przez niego, nie bezpośrednio plik
  `bin/.brulion-wrapped`.

## Wersja przeglądarkowa na własnym serwerze (`services.brulion-web`)

Moduł serwuje PWA przez nginx: poprawne nagłówki cache (`index.html`, `sw.js` zawsze
sprawdzane na nowo, pliki z `assets/` z hashem w nazwie — na rok, `immutable`), przekierowanie
nieznanych ścieżek do aplikacji (głębokie linki), nagłówek CSP jak w aplikacji na komputer
i opcjonalnie pośrednik do WebDAV Nextclouda. Moduł dodajesz tak samo jak wyżej
(`brulion.nixosModules.default` w `modules`), potem w konfiguracji serwera:

```nix
{
  services.brulion-web = {
    enable = true;
    hostName = "notatki.example.com";
    # basePath = "/notatki/";              # gdy aplikacja ma działać pod podścieżką
    nextcloudUrl = "https://cloud.example.com";
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

Przeglądarki pozwalają na service workera (praca offline, „Zainstaluj aplikację”) tylko przez
**HTTPS** albo na `localhost` — na serwerze włącz TLS.

### Opcje `services.brulion-web`

| Opcja | Domyślnie | Opis |
|---|---|---|
| `enable` | `false` | serwuje Brulion przez nginx |
| `package` | pakiet z tego flake'a | budowany ponownie dla `basePath` (`.override { basePath = …; }`) |
| `hostName` | `"localhost"` | nazwa hosta wirtualnego nginx; TLS, ACME, adresy i porty (`listen`) ustawiasz w `services.nginx.virtualHosts.<hostName>` |
| `basePath` | `"/"` | ścieżka URL aplikacji, np. `"/notatki/"` (zaczyna się i kończy `/`); `/` przekierowuje wtedy pod nią |
| `nextcloudUrl` | `null` | adres Nextclouda, do którego nginx przekazuje WebDAV (niżej) |
| `maxUploadSize` | `"512M"` | największy plik wysyłany przez pośrednika WebDAV |
| `contentSecurityPolicy` | jak w aplikacji na komputer | nagłówek CSP; `null` wyłącza |
| `openFirewall` | `false` | otwiera porty hosta (80, z TLS także 443) |

### Nextcloud bez problemów z CORS

Wersja przeglądarkowa łączy się z WebDAV z poziomu strony, a przeglądarka blokuje zapytania do
innego serwera, jeśli ten nie zezwala na CORS — Nextcloud nie zezwala. Rozwiązanie: z
`nextcloudUrl` nginx przekazuje ścieżkę `/remote.php/dav/` hosta Brulionu do Nextclouda (z tą
samą ścieżką, więc odpowiedzi WebDAV pasują do adresów, o które pytała aplikacja).

W Brulionie (Ustawienia → Synchronizacja → Nextcloud) jako adres serwera podaj wtedy **adres
Brulionu** zamiast adresu Nextclouda, np. `https://notatki.example.com` zamiast
`https://cloud.example.com`. Jeśli Nextcloud działa pod podścieżką (`nextcloudUrl =
"https://example.com/nextcloud"`), dopisz ją: `https://notatki.example.com/nextcloud`.
Logowanie: nazwa użytkownika i **hasło aplikacji** (Nextcloud → Ustawienia → Bezpieczeństwo).
Ciasteczka Nextclouda nie są przekazywane — Brulion uwierzytelnia każde zapytanie.

Aplikacja na komputer i na Androida nie potrzebuje pośrednika (zapytania idą z Rusta, nie
z przeglądarki) — tam podajesz zwykły adres Nextclouda.

### Same pliki, bez modułu

```sh
nix build github:janusz-bit/brulion#brulion-web   # wynik: ./result (index.html, sw.js, assets/)
```

Pod inną ścieżką niż `/`:

```sh
nix build --impure --expr \
  '(builtins.getFlake "github:janusz-bit/brulion").packages.x86_64-linux.brulion-web.override { basePath = "/notatki/"; }'
```

Na dowolnym serwerze: nieznane ścieżki → `index.html`, `sw.js` i `index.html` z
`Cache-Control: no-cache`.

## Praca nad kodem

```sh
nix develop          # albo: direnv allow (plik .envrc ładuje środowisko automatycznie)
npm ci
npm run tauri dev    # aplikacja na komputer z przeładowaniem na żywo
npm run dev          # sama wersja przeglądarkowa: http://localhost:1420
```

Środowisko zawiera Node 22, Rust (`cargo`, `clippy`, `rustfmt`, `rust-analyzer`), `cargo-tauri`,
biblioteki GTK/WebKit oraz `actionlint`, `shellcheck`, `nixfmt`. Ustawia też to, czego Tauri
potrzebuje na NixOS: `XDG_DATA_DIRS` (schematy GSettings), `GIO_EXTRA_MODULES` (TLS przez
glib-networking) i `LD_LIBRARY_PATH` (zasobnik). Przy pustym oknie odkomentuj
`WEBKIT_DISABLE_DMABUF_RENDERER=1` w `flake.nix` (albo wyeksportuj tę zmienną).

Przydatne polecenia:

```sh
nix build .#brulion -L         # pakiet na komputer -> ./result/bin/brulion
nix build .#brulion-web -L     # PWA -> ./result
nix flake check -L             # wszystko: pakiety, testy (vitest + cargo test), moduł, test VM
nix fmt                        # formatowanie plików .nix
cargo test --workspace --manifest-path src-tauri/Cargo.toml   # aplikacja + wtyczka brulion-android
```

Test w maszynie wirtualnej (`checks.<system>.web-vm`) wymaga KVM. Budowanie na Androida: zobacz
[ANDROID.md](ANDROID.md).

## Aktualizacja hashy

Pakiety Nixa budują się bez sieci, więc zależności npm są pobierane wcześniej jako jedna
pochodna o stałym hashu (`nix/npm-deps.nix`). Ten hash obejmuje cały `package-lock.json` —
**po każdej zmianie tego pliku** (nowa lub zaktualizowana zależność, ale też podbicie wersji)
uruchom:

```sh
scripts/update-nix-hashes.sh           # przelicza i wpisuje nowy hash
scripts/update-nix-hashes.sh --check   # tylko sprawdza (kod 1, gdy hash jest nieaktualny)
```

i zatwierdź `nix/npm-deps.nix` razem z `package-lock.json`. Nieaktualny hash objawia się
w `nix build` błędem `hash mismatch in fixed-output derivation ... brulion-npm-deps`
albo `npmDepsHash is out of date` (CI sprawdza to na początku zadania „Nix flake”).
`scripts/release.sh` robi to sam przy podbiciu wersji, jeśli Nix jest zainstalowany. Pull
requesty Dependabota z aktualizacjami npm wymagają tego kroku ręcznie.

Zależności Rusta nie mają hasha do pilnowania: `nix/package.nix` czyta je wprost
z `src-tauri/Cargo.lock` (tylko zależności z gita wymagałyby `cargoLock.outputHashes`).
Wersję aplikacji Nix bierze z `package.json`.

Nowszy nixpkgs: `nix flake update`, potem `nix flake check -L`.
