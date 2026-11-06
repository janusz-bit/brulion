# Brulion on NixOS

The repository is a Nix flake. It provides:

| Output | What it is |
|---|---|
| `packages.<system>.brulion` (= `default`) | the desktop app (C++/Qt 6.11 from nixpkgs): `bin/brulion`, menu entry, icons, AppStream metadata |
| `nixosModules.default` | NixOS module: `programs.brulion` (the app, autostart in the tray) and `services.brulion-web` (the browser version through nginx) |
| `overlays.default` | adds `pkgs.brulion` (built with the nixpkgs of whoever uses the overlay) |
| `devShells.<system>.default` | developer environment (Qt 6.11, CMake, Ninja, ccache, clang-tools, gdb…) |
| `checks.<system>.*` | package with tests, NixOS module, formatting of `.nix` files, test in a virtual machine |
| `formatter.<system>` | `nix fmt` (nixfmt for all `.nix` files) |

Systems: `x86_64-linux` and `aarch64-linux` (e.g. Raspberry Pi). nixpkgs is pinned in
`flake.lock` to the same version as the developer's system — we do not update it in passing.
Brulion does not have its own binary cache (yet): the first build compiles the app
locally (a few minutes; longer on a Raspberry Pi 4), Qt and the other dependencies come prebuilt
from cache.nixos.org.

## Installing on NixOS

### System flake (`/etc/nixos`)

In the system configuration's `flake.nix` (the author's: `github:janusz-bit/nixos`, directory `/etc/nixos`)
add the `brulion` input and the module:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    brulion = {
      url = "github:janusz-bit/brulion";
      # Build Brulion with the same nixpkgs as the system: Qt is then the same as in the rest of the
      # system (Qt theme plugins match, nothing is downloaded twice). Without this line
      # Brulion uses the nixpkgs from its own flake.lock — the version it is tested with.
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
            # Start with the graphical session, hidden in the tray — reminders work all the time.
            autostart = true;
          };
        }
      ];
    };
  };
}
```

Then as usual:

```sh
sudo nixos-rebuild switch --flake /etc/nixos#moj-komputer
```

Brulion appears in the application menu (categories "Office" and "Utility"), from a terminal: `brulion`.
Updating to the latest version from GitHub: `nix flake update brulion` in `/etc/nixos`, then
`nixos-rebuild switch` again. Pinning to a release: `url = "github:janusz-bit/brulion/v0.1.0";`.

### `programs.brulion` options

| Option | Default | Description |
|---|---|---|
| `enable` | `false` | installs Brulion (`environment.systemPackages`: program, menu entry, icons) |
| `package` | the package from this flake | another package, e.g. `pkgs.brulion` from the overlay or your own build |
| `autostart` | `false` | an entry `/etc/xdg/autostart/io.github.januszbit.brulion.desktop` that runs `brulion --hidden` at login: the app starts hidden in the system tray |

### Without the module

The package alone (the flake input passed to the modules through `specialArgs = { inherit brulion; };`):

```nix
{ pkgs, brulion, ... }:
{
  environment.systemPackages = [ brulion.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

or the overlay: `nixpkgs.overlays = [ brulion.overlays.default ];` and `pkgs.brulion`.
Trying it out without installing:

```sh
nix run github:janusz-bit/brulion
```

### Tray, autostart, notifications

* Reminders and alarms are shown by the running Brulion (freedesktop notifications over D-Bus).
  Closing the window hides it in the **system tray**; with `autostart = true` it is there from
  login. Each user can turn autostart off for themselves in the desktop's autostart settings
  (KDE: System Settings → Autostart; GNOME: Tweaks → Startup Applications) —
  this creates an entry with `Hidden=true` in `~/.config/autostart/`.
* **Tray calendar**: icon menu → **Calendar** opens a small window with the month,
  the tasks and reminders of the selected day, **Open Brulion** and **Quick capture…**.
  *Settings → Application → Click on the tray icon → Calendar* opens it with a plain
  click on the icon (by default on Linux a click shows the window). Under X11 the window appears
  at the cursor and disappears after a click beside it; under Wayland the compositor places it where it wants, and may
  not give it focus — then you close it with the **×** button or by clicking the icon again.
  Details: docs/WINDOWS.md, "Tray calendar".
* KDE Plasma, XFCE, Cinnamon, Budgie, waybar/swaybar: the tray works out of the box. **GNOME** has
  no tray — it needs the AppIndicator extension:

  ```nix
  environment.systemPackages = [ pkgs.gnomeExtensions.appindicator ];
  services.udev.packages = [ pkgs.gnome-settings-daemon ];
  ```

  and turning it on in the "Extensions" app.
* Notifications are shown by the desktop's notification daemon (graphical desktop environments always have one; with
  a bare window manager e.g. `mako` or `dunst`).
* **Sounds** (*Settings → Reminders → Reminder sound / Alarm sound*): "Default"
  (reminders — like other desktop notifications, alarms — `alarm-clock-elapsed`), "Ringtone"
  (**Choose ringtone…**: a sound from the sound theme — `message-new-instant`, `bell`,
  `complete`, `alarm-clock-elapsed`, `phone-incoming-call` — or your own `.oga`/`.ogg`/`.wav` file)
  and "Silent". Brulion passes them in the notification as the `sound-name`, `sound-file`
  or `suppress-sound` hints; the sound is played by the notification daemon (GNOME Shell, KDE Plasma — with a sound
  theme package, e.g. `sound-theme-freedesktop`). Daemons without sounds (`mako`, `dunst`)
  ignore them. **Play sample** sends a short test notification with the chosen sound.
* Desktop integration files: `share/applications/io.github.januszbit.brulion.desktop`,
  `share/metainfo/io.github.januszbit.brulion.metainfo.xml`, icon
  `share/icons/hicolor/{scalable,256x256}/apps/io.github.januszbit.brulion.{svg,png}`.

### File dialogs

Import, export ("Save…"), "Image…", "File…", "Sound file…" and choosing a notebook folder open
the desktop's file dialog if the Qt platform theme has one: GNOME (the `gtk3` theme, which Qt picks
there by itself) — a GTK dialog, KDE Plasma (`plasma-integration`) — a KDE dialog. On other desktops (sway,
Hyprland, i3…) Qt has no such theme and Brulion shows Qt's own file dialog (in Brulion's
colours: background, text and accent as in the app, dark too — regardless of the desktop's Qt style). You get the desktop's dialog through **xdg-desktop-portal** by setting

```sh
QT_QPA_PLATFORMTHEME=xdgdesktopportal brulion
```

(permanently e.g. `environment.sessionVariables.QT_QPA_PLATFORMTHEME = "xdgdesktopportal";` — works
with any portal backend: `xdg-desktop-portal-gtk`, `-gnome`, `-kde`, `-hyprland`, `-wlr` with
`-gtk` for files). **The AppImage** does this by itself when the variable is not set; without a working
portal it falls back to the Qt dialog. The Nix package sets nothing here: the Qt theme chosen by the
desktop stays (a global variable also changes other Qt programs).

### PDFs: viewer and drawing backdrop

The Nix package draws PDFs (PDF thumbnails in notes, the PDF viewer with search, the drawing
backdrop, "Drawing on a picture or PDF…") through
**poppler-qt6** (`kdePackages.poppler`, licence GPL-2.0-or-later, compatible with Brulion's
AGPL-3.0-or-later): nixpkgs has Qt PDF only inside QtWebEngine, which would grow the package's closure
by about 615 MiB, and poppler by about 26 MiB (poppler-qt6, poppler-data, nss, nspr, openjpeg, lcms2;
from cache.nixos.org for x86_64 and aarch64). CMake finds it through pkg-config
(`src/platform/CMakeLists.txt`), the developer environment (`nix develop`) gets it from the package, so
the `tst_pdfpages` tests check the poppler backend there. Building without PDFs:
`-DBRULION_PDF_BACKEND=none`.

### Spell checking

The Nix package checks spelling with Hunspell from nixpkgs (`hunspell`) and the dictionaries
`hunspellDicts.pl_PL` and `hunspellDicts.en_US`: their paths in `/nix/store` are compiled
into the program (`BRULION_SPELL_DICTIONARY_DIRS`, `nix/package.nix`, `passthru.spellDictionaries`), so
nothing needs to be installed alongside. Brulion also looks in `DICPATH`, in `share/hunspell` of the
`XDG_DATA_DIRS` folders (e.g. dictionaries from `environment.systemPackages`) and in `/usr/share/hunspell`.
`nix develop` sets `DICPATH` to the same dictionaries for `./build`. Without Hunspell:
`-DBRULION_SPELL_BACKEND=none` (the spelling settings disappear). docs/UI.md "Spell checking".

### Open with

The package's desktop entry (`Exec=brulion --open %F`, `StartupNotify=true`) lets you open a
`.md` or `.txt` file in Brulion from the file manager: **Open With → Other Application → Brulion**
(GNOME Files: **Open With…** → "View All Apps"; Dolphin, Thunar, Nemo:
**Open With → Other Application…**). The **Save to Brulion** dialog opens, the same as "Share → Brulion"
on Android: the file goes into today's journal or becomes a new page, and Brulion
saves a **copy**, while the file itself stays where it is, unchanged. A file that already is a page of the open
notebook (also in a folder linked into the notebook from elsewhere, or in a folder whose name starts
with a dot) simply opens. Several files at once go into one dialog; when Brulion is already
running, the running instance gets them and comes to the front — under Wayland with an activation token,
which the file manager provides thanks to `StartupNotify=true` (a launcher without a token makes
GNOME show only "Brulion is ready"); under X11 the second start ends the startup notification
itself, so the cursor does not keep spinning.

**Brulion does not change the default program.** That is why the entry declares no file types
(`MimeType`): a desktop without a default program for some type (sway, i3 or Hyprland without
`mimeapps.list`; GNOME for `text/markdown`) takes the first program that declares it, by
name — and `io.github.januszbit.brulion` comes before `nvim`, `org.gnome.TextEditor` or
`org.kde.kate`, so Brulion would then silently open every text file. If you want
Brulion to open Markdown files on double-click:

```sh
xdg-mime default io.github.januszbit.brulion.desktop text/markdown
```

From the command line: `brulion --open notatka.md …` (everything after `--open` is files; `brulion --open`
without files starts Brulion as usual). **The AppImage** has the same entry inside: tools that
integrate AppImages with the menu (Gear Lever, AppImageLauncher, appimaged) install it, so "Open
with" works with the AppImage too.

### Notes and Syncthing

Notes are plain `.md` files in a chosen folder. Sync with a phone and other
computers through Syncthing on NixOS:

```nix
services.syncthing = {
  enable = true;
  user = "janusz";              # your user
  dataDir = "/home/janusz";
  openDefaultPorts = true;
};
```

You add the notes folder in the Syncthing panel (<http://127.0.0.1:8384>). Alternatively Nextcloud
(WebDAV) in Brulion's sync settings — the desktop app needs no system
configuration for that.

### AI agent (Hermes) and the command line

The package also has `brulion-cli` — the command line for scripts and AI agents ([CLI.md](CLI.md)): it reads
and changes notes with the same code as the app, and `brulion-cli mcp` offers the commands as
MCP tools. For a computer without a desktop (e.g. a Raspberry Pi with an agent) the light
**`brulion-cli`** package from this flake is enough — Qt Core only, without Qt Quick, QML, SVG and Wayland, so it builds
quickly and takes little space:

```nix
environment.systemPackages = [ brulion.packages.${pkgs.stdenv.hostPlatform.system}.brulion-cli ];
```

or without installing: `nix run github:janusz-bit/brulion#brulion-cli -- --vault ~/Notatki task list`.

An agent on a different computer than the app gets the notes as a **synced folder** —
through Syncthing (recommended) or the Nextcloud client — and works on its local copy. Example:
Hermes as the user `hermes` on a Raspberry Pi, the folder `/var/lib/hermes/Brulion` shared
from the computer through Syncthing, which on the Pi runs as the same user:

```nix
{ pkgs, lib, brulion, ... }:
let
  brulionCli = brulion.packages.${pkgs.stdenv.hostPlatform.system}.brulion-cli;
in
{
  services.syncthing = {
    enable = true;
    user = "hermes";                       # the files belong to the agent
    dataDir = "/var/lib/hermes";
    configDir = "/var/lib/hermes/.config/syncthing";
  };

  services.hermes-agent.mcpServers.brulion = {
    command = lib.getExe brulionCli;
    args = [ "mcp" "--vault" "/var/lib/hermes/Brulion" ];   # + "--read-only": read only
    connect_timeout = 60;                  # the first run indexes the notebook
    timeout = 120;
  };
}
```

You add the `Brulion` folder in the Syncthing panel on both computers (on the Pi e.g. through
`ssh -L 8384:127.0.0.1:8384 pi`). The agent gets the tools `brulion_capture`, `brulion_task_add`,
`brulion_task_list`, `brulion_search`, `brulion_query`, `brulion_journal_append` and others; destructive
commands (replacing a whole page, moving, deleting) appear only with
`--allow-destructive`. Every change by the agent goes into the version history with the description
"Replaced from the command line (agent)" — on the device where `brulion-cli` runs (device state in the
agent user's `~/.local/share/Brulion/Brulion/vaults/<id>`). A guide for the agent (agentskills
format) is in the package: `share/brulion/agent-skills/brulion/SKILL.md`.

**Reminders** added by the agent ring where the app runs: on the computer (in the
tray, `autostart = true`) and on the phone, once the app sees the change. `brulion-cli` itself
never starts reminders or sync and does not change the app's settings.

## The browser version on your own server (`services.brulion-web`)

The module serves the WebAssembly version (a PWA, works offline) through nginx: the correct MIME type
`application/wasm` (streaming compilation), precompressed files (gzip and brotli,
`brulion.wasm` about 3× smaller), cache headers suited to the service worker and optionally
a proxy to Nextcloud's WebDAV (no CORS problems). Details of the browser version itself:
[WEB.md](WEB.md).

### Where to get the files

nixpkgs has no Qt for WebAssembly, so Nix does not build the browser version itself. There are two
ways:

1. **A release from GitHub** (recommended): every release comes with
   `brulion-web-<version>.zip`. The module downloads it by itself:

   ```nix
   services.brulion-web.release = {
     version = "0.1.0";
     hash = "";   # the first build fails and prints the right hash: "got: sha256-…"
   };
   ```

   Test releases (tag with a suffix) — `release.tag = "v0.2.0-rc.1";` (the files have version
   `0.2.0`).
2. **Your own build** with `scripts/wasm-build.sh` (directory `dist-wasm/`, see WEB.md):
   * `services.brulion-web.package = ./dist-wasm;` — a copy goes into the Nix store (in a flake the
     directory must be added to git), with precompressed files;
   * or a path outside the store as a string, e.g. `package = "/srv/brulion-web";` — nginx serves the
     directory directly (on-the-fly compression, gzip), and replacing the files (`rsync`) does not need
     `nixos-rebuild`. The directory must be readable by the `nginx` user.

### Example

```nix
{
  services.brulion-web = {
    enable = true;
    release = { version = "0.1.0"; hash = "sha256-…"; };
    hostName = "notatki.example.com";
    nextcloudUrl = "https://cloud.example.com";   # optional, below
    openFirewall = true;
  };

  # TLS and the other host settings — directly in nginx:
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

Browsers allow the service worker (offline work, "Install app"),
notifications and persistent IndexedDB only over **HTTPS** or on `localhost` — on a server,
turn on TLS.

### `services.brulion-web` options

| Option | Default | Description |
|---|---|---|
| `enable` | `false` | serves Brulion through nginx |
| `package` | `null` | directory with the browser version (`index.html`, `brulion.wasm`, `brulion.js`, `qtloader.js`, `sw.js`…): a path/package in the store or a string with a path outside the store; takes precedence over `release` |
| `release` | `null` | `{ version; hash; tag ? null; }` — the browser version from a GitHub release |
| `hostName` | `"localhost"` | name of the nginx virtual host; TLS, ACME, addresses and ports are set in `services.nginx.virtualHosts.<hostName>` |
| `basePath` | `"/"` | the app's URL path, e.g. `"/notatki/"` (starts and ends with `/`); the addresses in the build are relative, so it works without a rebuild; `/notatki` redirects to `/notatki/` |
| `nextcloudUrl` | `null` | address of the Nextcloud to which nginx forwards WebDAV (below) |
| `maxUploadSize` | `"512M"` | the largest file uploaded through the WebDAV proxy |
| `precompress` | `true` | `.gz`/`.br` files next to the originals, computed once at build time (only for files in the store) |
| `brotli` | `true` | also serves brotli (the `ngx_brotli` module, like `services.nginx.recommendedBrotliSettings`) |
| `contentSecurityPolicy` | `null` | the `Content-Security-Policy` header; before turning it on, check the policy in the browser console (the HTML shell has an inline script, Emscripten's code may need `'unsafe-eval'`) |
| `openFirewall` | `false` | opens the host's ports (80, with TLS also 443) |

Headers: `brulion.js`, `brulion.wasm` and `qtloader.js` are fetched with `?v=<content hash>` —
such addresses have `Cache-Control: public, max-age=31536000, immutable`; everything else
(`index.html`, `sw.js`, the manifest, icons) `no-cache`, i.e. revalidated on every use
(a `304` response when nothing changed — the ETag of nginx from nixpkgs comes from the hash of the store
path, so a new version always has a new ETag). In addition `X-Content-Type-Options: nosniff` and
`Referrer-Policy: no-referrer`.

### Nextcloud without CORS problems

The browser version connects to WebDAV from the page, and the browser blocks requests to
another server unless that server allows CORS — Nextcloud does not. With `nextcloudUrl` nginx
forwards the path `<Nextcloud path>/remote.php/dav/` of Brulion's host to Nextcloud under the same
path (WebDAV responses with `/…/remote.php/dav/files/…` addresses then match the
addresses the app asked for).

In Brulion (sync settings → Nextcloud) enter **Brulion's address** as the server address
instead of Nextcloud's, e.g. `https://notatki.example.com` instead of
`https://cloud.example.com`. If Nextcloud runs under a subpath
(`nextcloudUrl = "https://example.com/nextcloud"`), append it:
`https://notatki.example.com/nextcloud`. Login: user name and an **app password**
(Nextcloud → Settings → Security). Nextcloud's cookies are not forwarded —
Brulion authenticates every request. Nextcloud's HTTPS certificate is verified (system
CAs).

* nginx resolves Nextcloud's name at startup; if DNS is sometimes unavailable then, set
  `services.nginx.proxyResolveWhileRunning = true;` and `services.nginx.resolver.addresses`.
* Nextcloud on **the same server**: instead of the proxy, give Nextcloud's host as `hostName`
  and a subpath, e.g.
  `services.brulion-web = { hostName = config.services.nextcloud.hostName; basePath = "/brulion/"; };`
  — the app and Nextcloud then have the same origin, `nextcloudUrl` is not needed, and in
  Brulion you enter Nextcloud's usual address.
* The desktop and Android apps are not subject to CORS — there it is always Nextcloud's usual address.

## Working on the code

```sh
nix develop                    # Qt 6.11, cmake, ninja, ccache, clang-tools, gdb, nixfmt
scripts/dev-build.sh           # configure + build in ./build (Debug, -Werror, with a lock)
scripts/dev-build.sh -t tst_dates tst_dates   # just one test (target + regex)
./build/src/app/brulion        # run; without a screen: QT_QPA_PLATFORM=offscreen
```

`nix develop` sets `QT_PLUGIN_PATH` and `QML_IMPORT_PATH`, so the unwrapped program from
`./build` finds the Qt plugins and QML modules. Details and rules: `CLAUDE.md`,
[ARCHITECTURE.md](ARCHITECTURE.md).

Nix commands:

```sh
nix build .#brulion -L         # package -> ./result (no tests; .desktop/AppStream validation)
nix build .#brulion-cli -L     # the command line alone (BRULION_BUILD_APP=OFF, no Qt Quick)
nix flake check -L             # package, all tests, NixOS module, formatting, VM test (x86_64)
nix build .#checks.x86_64-linux.tests -L   # tests only (ctest in the Nix sandbox)
nix fmt                        # formatting of .nix files (nixfmt)
```

What `nix flake check` checks:

| Check | What it does |
|---|---|
| `brulion` | package build, `desktop-file-validate` and `appstreamcli validate-tree` on the installed files, `brulion-cli version` and saving a quick capture in a scratch notebook |
| `brulion-cli` | the command line alone (without Qt Quick): the same save test |
| `tests` | the package with `doCheck = true`: the whole `ctest` — unit tests, QML self-tests and script tests. Left out are the tests that need browsers (`tests/web`), the Android emulator, the internet or real devices, and the benchmarks |
| `nixos-module` | a minimal NixOS system with `programs.brulion` (+ autostart) and `services.brulion-web` (+ Nextcloud proxy) evaluates (`toplevel.drvPath`, without building the system); the nginx configuration passes `gixy`, the autostart entry `desktop-file-validate`; a variant with `release` and `basePath`; an assertion when the package is missing |
| `nixfmt` | all `.nix` files are formatted (`nix fmt`) |
| `web-vm` (x86_64 only, needs KVM) | a virtual machine with the module: MIME types, gzip/brotli, cache headers and ETag, the `basePath` redirect, the WebDAV proxy (a fake Nextcloud: paths with Polish characters, cookies, a large `PUT`), the autostart entry and a headless app start |

**The Raspberry Pi (dev machine) does not do heavy builds** — run `nix flake check` and
`nix build` on the laptop (x86_64) through `laptop-run`, always under the shared lock
(new files first `git add -N`, because `laptop-run` copies only files tracked by git):

```sh
git add -N <new files>
flock <lock> laptop-run bash -c 'nix flake check -L && nix build .#brulion -L'
```

### Files

| Path | Contents |
|---|---|
| `flake.nix`, `flake.lock` | flake outputs, pinned nixpkgs |
| `nix/package.nix` | the package (`version` is changed by `scripts/release.sh` together with `CMakeLists.txt`) |
| `nix/cli.nix` | `brulion-cli` alone (`packages.<system>.brulion-cli`, `apps.brulion-cli`) |
| `nix/module.nix` | NixOS module (`programs.brulion`, `services.brulion-web`) |
| `nix/web-release.nix` | downloading `brulion-web-<version>.zip` from a release |
| `nix/web-site.nix` | a copy of the browser version with `.gz`/`.br` files |
| `nix/tests/` | `module-eval.nix`, `web-vm.nix`, `fake-web-build.nix` (a stand-in "browser version" for tests) |
| `cmake/BrulionInstall.cmake` | `cmake --install` rules for the desktop files (Linux) |
| `packaging/` | `io.github.januszbit.brulion.desktop`, `.metainfo.xml`, 256 px PNG icon, `agent/brulion/SKILL.md` (a guide for AI agents) |

The PNG icon is generated from `assets/logo.svg`; after a logo change:

```sh
nix shell nixpkgs#librsvg nixpkgs#optipng -c sh -c '
  rsvg-convert -w 256 -h 256 assets/logo.svg \
    -o packaging/icons/hicolor/256x256/apps/io.github.januszbit.brulion.png &&
  optipng -o5 -strip all packaging/icons/hicolor/256x256/apps/io.github.januszbit.brulion.png'
```

You can check the desktop files locally without building:

```sh
nix shell nixpkgs#desktop-file-utils nixpkgs#appstream -c sh -c '
  desktop-file-validate packaging/io.github.januszbit.brulion.desktop &&
  appstreamcli validate --no-net packaging/io.github.januszbit.brulion.metainfo.xml'
```

## Troubleshooting

* **No icon in the tray** — see "Tray, autostart, notifications" (GNOME needs the AppIndicator extension).
* **Window without a theme / warnings about Qt plugins** — Brulion built with a different nixpkgs than the
  system loads the system's Qt plugins of another version. Add `inputs.nixpkgs.follows = "nixpkgs"`
  (above).
* **Run `brulion`, not `bin/.brulion-wrapped`** — the wrapper sets the paths of the Qt plugins and
  QML modules.
* **Browser version: `hash mismatch` with `release`** — a new version has a new zip file:
  enter the hash from the message (`got: sha256-…`).
* **nginx does not start: `host not found in upstream`** — DNS unavailable at startup, see
  `proxyResolveWhileRunning` above.
