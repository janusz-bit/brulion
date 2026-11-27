# Brulion on NixOS

The repository is a Nix flake. It provides:

| Output | What it is |
|---|---|
| `packages.<system>.brulion` (= `default`) | the desktop app (C++/Qt 6.12 from nixpkgs, built with Clang): `bin/brulion`, menu entry, icons, AppStream metadata |
| `nixosModules.default` | NixOS module: `programs.brulion` (the app, autostart in the tray), `services.brulion-web` (the browser version through nginx), `services.brulion-mcp` (the remote MCP server for claude.ai and ChatGPT) and `services.brulion-acp` (AI agents of this machine for the app on a phone) |
| `overlays.default` | adds `pkgs.brulion` (built with the nixpkgs of whoever uses the overlay) |
| `devShells.<system>.default` | developer environment (Qt 6.12, Clang, CMake, Ninja, ccache, clang-tools, gdb…) |
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
| `webClipper` | `true` | connects Chromium, Chrome, Edge, Brave and `programs.firefox` to the Web Clipper extension (the native messaging host manifests, [below](#web-clipper-browser-extension)) |

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
* **Tray calendar**: a click on the icon (or icon menu → **Calendar**) opens a small window with
  the month, the tasks and reminders of the selected day, **Open Brulion** and **Quick capture…**;
  it disappears after a click beside it. *Settings → Application → Click on the tray icon →
  Window* makes a click show the window instead. Under X11 the calendar appears at the cursor.
  Under Wayland it sits in the bottom-right corner of the screen, above the panel (a layer-shell
  surface: KDE Plasma, sway, Hyprland and other wlroots compositors); GNOME has no layer shell, so
  there the compositor places it where it wants and may not give it focus — then you close it with
  the **×** button or by clicking the icon again. The AppImage has no layer shell either.
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

### Text recognition (OCR) for the search

The search reads the text of PDFs and pictures (docs/UI.md "Searching attachments"); scanned pages
and pictures need **Tesseract**, which the Nix package brings: `tesseract` from nixpkgs without its
own models (`passthru.tesseract`, 5 MB; Leptonica and image libraries already in the closure or
small) and the English and Polish "fast" models of `tessdata_fast` 4.1.0 (`passthru.tessdata`,
9 MB, fetched by `nix/package.nix`). The program loads the library at run time from the store path
compiled into it (`BRULION_TESSERACT_LIBRARY`, `BRULION_TESSDATA_DIRS`): nothing to install, and OCR
stays off until Settings → Search turns it on. More languages: their `*.traineddata` files in
`~/.local/share/Brulion/Brulion/tessdata` (the user's folder is looked in first and replaces the
built-in ones; copy `eng` and `pol` there too). `nix develop` sets `BRULION_TESSERACT_LIBRARY` and
`BRULION_TESSDATA_DIR` for `./build`, so `tst_ocr` and the OCR parts of `tst_attachmentindexer` and
the shell self-test run there.

### The model on the device

The Nix package has the AI provider "On this device" (docs/EXTENSIONS.md "AI"): llama.cpp is
compiled into the build from the sources of one release (`3rdparty/llama/pin.json`, fetched by
`nix/package.nix`; nixpkgs' own `llama-cpp` is not used: the flake pins a nixpkgs older than the
models) as `lib/brulion/libbrulion_localai.so` with llama.cpp's libraries and processor plugins
beside it — 15 MB that the program loads only when a model is first asked. The models are
downloaded by the app into `~/.local/share/Brulion/Brulion/models` (1.0–1.2 GB each). Without it:
`brulion.override { withLocalAi = false; }`. `nix develop` sets `BRULION_LLAMA_SOURCE_DIR`, so
`./build` has it too; `brulion-cli` alone never does.

### Spell checking

The Nix package checks spelling with Hunspell from nixpkgs (`hunspell`) and the dictionaries
`hunspellDicts.pl_PL` and `hunspellDicts.en_US`: their paths in `/nix/store` are compiled
into the program (`BRULION_SPELL_DICTIONARY_DIRS`, `nix/package.nix`, `passthru.spellDictionaries`), so
nothing needs to be installed alongside. Brulion also looks in `DICPATH`, in `share/hunspell` of the
`XDG_DATA_DIRS` folders (e.g. dictionaries from `environment.systemPackages`) and in `/usr/share/hunspell`.
`nix develop` sets `DICPATH` to the same dictionaries for `./build`. Without Hunspell:
`-DBRULION_SPELL_BACKEND=none` (the spelling settings disappear). docs/UI.md "Spell checking".

### Open with

The package's desktop entry (`Exec=brulion --open %U`, `StartupNotify=true`) lets you open a
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
(`MimeType` names only `x-scheme-handler/brulion`, below): a desktop without a default program for some type (sway, i3 or Hyprland without
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

### brulion:// links

The desktop entry also takes **brulion:// links** (`MimeType=x-scheme-handler/brulion;` — no
other program opens them, so Brulion is their default at once): `xdg-open 'brulion://daily'`, a
link in a browser or an e-mail, a keyboard shortcut of the desktop (GNOME's Settings → Keyboard →
Custom Shortcuts, KDE's Shortcuts: the command `xdg-open 'brulion://capture'`), a script — Brulion
starts or the running one gets the address and does what it asks (docs/UI.md "Automation"). The
AppImage's entry declares it too (once a tool integrated the AppImage). Writing without the
notebook's automation key asks first; a script can also write without the app:
`brulion-cli uri run 'brulion://append?…'` (docs/CLI.md "brulion:// addresses").

### Web Clipper (browser extension)

The Brulion Web Clipper ([CLIPPER.md](CLIPPER.md)) saves web pages into the notebook through
`brulion-cli`, which the browser starts as a native messaging host. With `programs.brulion.enable`
the module connects the browsers (`programs.brulion.webClipper`, `true` by default): the host
manifest naming the package's `brulion-cli` in `/etc/chromium/native-messaging-hosts`,
`/etc/opt/chrome/native-messaging-hosts`, `/etc/opt/edge/native-messaging-hosts` and
`/etc/opt/brave/native-messaging-hosts`, and the package in `programs.firefox.nativeMessagingHosts.packages`
(the Firefox of `programs.firefox`). The packages carry the manifests themselves:
`lib/mozilla/native-messaging-hosts/io.github.januszbit.brulion.json` (for a Firefox wrapped with
`nativeMessagingHosts`, also in Home Manager) and
`share/brulion/native-messaging-hosts/io.github.januszbit.brulion.json` (Chromium's). A Firefox
installed otherwise is connected for one user with `brulion-cli clip install --browser firefox`
(the manifest then names `/run/current-system/sw/bin/brulion-cli`, which survives upgrades);
`clip install --system` is refused on NixOS — the module does it. The extension itself is loaded from
its package ([CLIPPER.md](CLIPPER.md#installing)).

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
never starts reminders and does not change the app's settings; it syncs only in `acp serve
--nextcloud` (below, "AI agents for the phone").

## Remote MCP server (`services.brulion-mcp`)

`brulion-cli mcp serve` ([CLI.md](CLI.md#remote-mcp-server)) as a hardened system service: the
notebook on this machine — typically the copy Syncthing keeps — becomes a custom connector for
claude.ai, the Claude apps on the phone and the desktop, and ChatGPT. You sign in once per app with
your own password and decide on a consent page whether the app may only read or also write.

```nix
{
  services.brulion-mcp = {
    enable = true;
    vault = "/var/lib/syncthing/Notatki";
    publicUrl = "https://notes.example.com";     # what the apps reach; paste …/mcp into them
    # readOnly = false;                          # let the owner allow writing, app by app
    # user = config.services.syncthing.user;     # to write the files Syncthing keeps
    # passwordFile = config.age.secrets.brulion-mcp.path;   # else: sudo brulion-mcp password
  };

  # HTTPS in front of 127.0.0.1:8780 — Caddy gets the certificate by itself:
  services.caddy = {
    enable = true;
    virtualHosts."notes.example.com".extraConfig = "reverse_proxy 127.0.0.1:8780";
  };
  networking.firewall.allowedTCPPorts = [ 80 443 ];
}
```

Then, once (unless `passwordFile` is set):

```sh
sudo brulion-mcp password        # typed twice; kept as a salted hash in /var/lib/brulion-mcp
sudo brulion-mcp clients         # the apps connected, later
sudo brulion-mcp revoke --all    # end connections (also: revoke <bgr_…>)
```

and add `https://notes.example.com/mcp` in claude.ai (Settings → Connectors → Add custom
connector) or in ChatGPT (developer mode: a new MCP app with OAuth); the step-by-step guide for
both is in [CLI.md](CLI.md#setting-it-up).

**A home machine without a public address**: let a tunnel give it one, the server stays on
`127.0.0.1` — Tailscale Funnel (`services.tailscale.enable = true;`, then once
`sudo tailscale funnel --bg 8780` and `publicUrl = "https://<machine>.<tailnet>.ts.net";`) or a
Cloudflare Tunnel (`services.cloudflared`, ingress `http://127.0.0.1:8780`). With nginx instead of
Caddy: `locations."/".proxyPass = "http://127.0.0.1:8780";` with `proxy_buffering off;` in its
`extraConfig`, on a virtual host with `enableACME` and `forceSSL`.

**HTTPS without a proxy**: `tls = { certFile = …; keyFile = …; };` with `listenAddress =
"0.0.0.0"` and `openFirewall = true` (a port below 1024 gets `CAP_NET_BIND_SERVICE`). With ACME let
the service read the certificate (`security.acme.certs."notes.example.com".group =
"brulion-mcp";`); renewed certificates are taken without a restart.

The service: user `brulion-mcp` (or `user`), its state in `/var/lib/brulion-mcp` (the server's
state file `mcp-remote.json` — the password hash, the registered apps, the connections with
hashes of their tokens — and the notebook's device state: version history, index cache), the
password file handed over as a systemd credential, and hardening: `ProtectSystem=strict`, home
directories hidden (`ProtectHome=tmpfs`) except the notebook bound in (read-only unless `readOnly =
false`), `NoNewPrivileges`, `PrivateTmp`, `PrivateDevices`, `MemoryDenyWriteExecute`, a system
call filter, only IP and local sockets, no capabilities. `brulion-mcp` (the admin command) runs
`brulion-cli mcp password | clients | revoke` as the service's user on its notebook and state file.

### `services.brulion-mcp` options

| Option | Default | Description |
|---|---|---|
| `enable` | `false` | runs `brulion-cli mcp serve` as `brulion-mcp.service` |
| `package` | the flake's `brulion-cli` | a package with `bin/brulion-cli` (the app's package has it too) |
| `vault` | — | the notebook folder; the service user must read it (and write it unless `readOnly`) |
| `listenAddress` | `"127.0.0.1"` | the address it listens on; another one needs `publicUrl` (not used with `tailscale`) |
| `tailscale` | `false` | serve in the machine's tailnet (`--tailscale`): its tailnet address and name, its own user's devices paired without a code |
| `tailnetUsers` | `[ ]` | more tailnet logins whose devices pair without a code |
| `port` | `8780` | the port |
| `publicUrl` | `null` | the address the apps reach (`https://…`; the endpoint is this + `/mcp`); `null`: this machine only |
| `passwordFile` | `null` | a file with the password or its hash (`brulion-cli mcp password --print-hash`), outside the Nix store; `null`: `sudo brulion-mcp password` |
| `readOnly` | `true` | no app may change notes |
| `allowDestructive` | `false` | the destructive tools (replace, move, delete pages) for apps allowed to write |
| `tls` | `null` | `{ certFile; keyFile; }` — HTTPS without a proxy |
| `allowedOrigins` | `[ ]` | origins of browser-based MCP clients (CORS), e.g. the MCP Inspector |
| `allowedHosts` | `[ ]` | more `Host` names, for a proxy that rewrites it |
| `user`, `group` | `"brulion-mcp"` | who it runs as (the default user and group are created) |
| `openFirewall` | `false` | open `port` (with `tailscale`: on `tailscale0` only) |
| `extraArgs` | `[ ]` | more options of `mcp serve` |

The flake's check `checks.<system>.nixos-module` evaluates the module with the server and builds
its unit and admin command; `checks.x86_64-linux.web-vm` runs it in a virtual machine: sign-in,
consent, tokens, a tool call and `brulion-mcp revoke` against the hardened service.

## AI agents for the phone (`services.brulion-acp`)

The app's AI chat can be answered by an AI agent of your own — Claude Code, Codex, OpenCode
(docs/UI.md "AI agents (ACP)") — with the subscription its own program signs in to. A phone starts
no programs, so `brulion-cli acp serve` ([CLI.md](CLI.md#ai-agents-for-other-devices)) runs the agent
on a machine of yours instead, in that machine's copy of the notebook, and the app on the phone
talks to it: *Settings → Extensions → AI agents → Add → An agent on another computer*. The agent's
files, commands and Brulion's MCP tools are that machine's; the phone may sleep while it works.

**Reach it only in your own network.** A paired phone drives an agent that runs commands on the
machine; the service runs as a user of its own with nothing else to lose, and its address should be
one only your devices reach — a tailnet (Tailscale or your own headscale) or a VPN, never the open
internet. On a tailnet with MagicDNS and an ACME certificate (as Nextcloud is served there):

```nix
{ config, lib, pkgs, ... }:
{
  services.brulion-acp = {
    enable = true;
    agents.claude = { name = "Claude Code"; command = lib.getExe pkgs.claude-agent-acp; };
    # agents.codex = { name = "Codex"; command = lib.getExe pkgs.codex-acp; };
    path = with pkgs; [ bashInteractive coreutils findutils gnugrep gnused git ripgrep claude-code ];
    publicUrl = "https://agent.example.com";        # what `brulion-acp pair` shows the phone
    # The notebook: its own copy, synced with your Nextcloud as one more device of it
    # (or `vault = "/var/lib/syncthing/Notatki";` for a copy Syncthing keeps).
    nextcloud = {
      url = "https://nextcloud.example.com";
      user = "jan";
      folder = "Brulion";                           # as in the app's sync settings
      passwordFile = config.age.secrets.brulion-acp-nextcloud.path;   # an app password
    };
  };

  # HTTPS in front of 127.0.0.1:8790, on the tailnet address only.
  services.nginx.virtualHosts."agent.example.com" = {
    listenAddresses = [ "100.64.0.1" ];
    forceSSL = true;
    useACMEHost = "agent.example.com";
    locations."/" = {
      proxyPass = "http://127.0.0.1:8790";
      extraConfig = ''
        proxy_buffering off;          # a receive waits up to 30 s for the agent
        proxy_read_timeout 90s;
        client_max_body_size 16m;     # pictures of a prompt
      '';
    };
  };
  networking.firewall.interfaces.tailscale0.allowedTCPPorts = [ 443 ];
}
```

Simpler in a tailnet, without a proxy and without a code for your own devices
(`acp serve --tailscale`, [CLI.md](CLI.md#in-a-tailnet)): it listens on the machine's tailnet
address, gives the phone its MagicDNS name, and a phone of the machine's own tailnet user pairs with
nothing typed — tailscaled vouches for it (plain HTTP: the tailnet encrypts it):

```nix
{
  services.tailscale.enable = true;
  services.brulion-acp = {
    enable = true;
    agents.claude = { name = "Claude Code"; command = lib.getExe pkgs.claude-agent-acp; };
    tailscale = true;              # http://<machine>.<tailnet>.ts.net:8790
    # tailnetUsers = [ "ola@github" ];   # more users whose devices pair without a code
    openFirewall = true;           # port 8790 on tailscale0 only
  };
}
```

On a desktop the app does the same without a service: Settings → Extensions → AI agents → **Share
agents with the phone** (docs/UI.md "AI agents (ACP)").

Then, once:

```sh
sudo brulion-acp login claude    # the agent's own sign-in as the service's user: /login, a subscription
sudo brulion-acp pair            # a QR code for the phone's camera, and the code (10 minutes, one device)
sudo brulion-acp clients         # the paired devices, later
sudo brulion-acp revoke --all    # unpair (also: revoke <dev_…>): the token stops working at once
```

`brulion-acp login <program> …` runs any program of `path` as the service's user with its home
(`/var/lib/brulion-acp`), where the agents keep their sign-in (Claude Code: `~/.claude`). The agents
are pinned by nixpkgs rather than fetched by `npx` when they start.

The service: user `brulion-acp` (or `user`), its state in `/var/lib/brulion-acp` (the state file
`acp-remote.json` — the paired devices with hashes of their tokens, the code waiting —, the
notebook's device state, the agents' homes, and `notebook/`, the notebook's own copy when `vault`
is not set), a private runtime folder `/run/brulion-acp` (`XDG_RUNTIME_DIR`: the socket through
which the agents' tools reach the app on the phone — its live tools when the phone lets the agent
change the app, [CLI.md](CLI.md#live-changes) — and the screenshots they take of it), the
Nextcloud password as a systemd credential, `KillMode=control-group` (the agents
and their commands end with it), and the hardening an agent leaves room for: `ProtectSystem=strict`,
home directories hidden (`ProtectHome=tmpfs`) except a `vault` bound in, `NoNewPrivileges`,
`PrivateTmp`, `PrivateDevices`, no capabilities — but no `MemoryDenyWriteExecute` (Node.js) and
no system call filter or namespace restriction (the agents run programs and may sandbox them).

### `services.brulion-acp` options

| Option | Default | Description |
|---|---|---|
| `enable` | `false` | runs `brulion-cli acp serve` as `brulion-acp.service` |
| `package` | the flake's `brulion-cli` | a package with `bin/brulion-cli` |
| `agents` | `{ }` | `<id> = { name; command; }` — the agents a device may start, by id |
| `path` | bash, coreutils, findutils, grep, sed, git | programs on the agents' PATH (and of `brulion-acp login`) |
| `vault` | `null` | the notebook folder; `null`: `/var/lib/brulion-acp/notebook`, the service's own copy |
| `nextcloud` | `null` | `{ url; user; folder ? "Brulion"; passwordFile; interval ? 300; deviceName ? null; }` — keep the notebook in sync with Nextcloud |
| `tools` | `true` | sessions get Brulion's MCP server |
| `listenAddress` | `"127.0.0.1"` | the address it listens on; another one needs `publicUrl` |
| `port` | `8790` | the port |
| `publicUrl` | `null` | the address the phone reaches (`https://…`) |
| `tls` | `null` | `{ certFile; keyFile; }` — HTTPS without a proxy |
| `allowedHosts` | `[ ]` | more `Host` names, for a proxy that rewrites it |
| `allowedOrigins` | `[ ]` | sites of Brulion in a browser that may use the agents (CORS) |
| `user`, `group` | `"brulion-acp"` | who it runs as (the default user and group are created) |
| `openFirewall` | `false` | open `port` |
| `extraArgs` | `[ ]` | more options of `acp serve` |

`checks.<system>.nixos-module` evaluates the module with the service and builds its unit and admin
command.

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
| `frameAncestors` | `"'self'"` | who may show the app inside a frame: a `Content-Security-Policy: frame-ancestors …` header of its own (and `X-Frame-Options: SAMEORIGIN` for the default), so that no other site's page can frame the app and lead clicks into it; name another origin for a site of your own that embeds Brulion (`"'self' https://cloud.example.com"`), `null` sends no header |
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
nix develop                    # Qt 6.12, Clang, cmake, ninja, ccache, clang-tools, gdb, nixfmt
scripts/dev-build.sh           # configure + build in ./build (Debug, -Werror, with a lock; Clang)
scripts/dev-build.sh -t tst_dates tst_dates   # just one test (target + regex)
./build/src/app/brulion        # run; without a screen: QT_QPA_PLATFORM=offscreen
```

`nix develop` sets `QT_PLUGIN_PATH` and `QML_IMPORT_PATH`, so the unwrapped program from
`./build` finds the Qt plugins and QML modules. Details and rules: `CLAUDE.md`,
[ARCHITECTURE.md](ARCHITECTURE.md).

For one command after another — scripts, AI agents — enter the shell through its saved profile:

```sh
scripts/dev-shell.sh scripts/dev-build.sh -t tst_dates tst_dates   # a command in the dev shell
scripts/dev-shell.sh                                               # or an interactive shell
scripts/dev-shell.sh -s cli scripts/dev-build.sh -t tst_dates tst_dates   # the light shell
```

`nix develop` evaluates the flake on every call and, from a tree with uncommitted changes, copies
the tree into the Nix store first; `scripts/dev-shell.sh` does that once for a `flake.nix`,
`flake.lock` and `nix/`, keeps the result as a Nix profile under `~/.cache/brulion/dev-shell` (a
garbage collector root, so a collection leaves the compiler and Qt Multimedia where they are;
shared by every checkout and worktree) and enters it in half a second. `-s cli` is
`devShells.cli`: the core, `brulion-cli` and their tests alone, with Qt's base — no Qt Quick, and
no Qt Multimedia to compile first, which makes it the shell for a Raspberry Pi; there
`scripts/dev-build.sh` builds with `BRULION_BUILD_APP=OFF` into `./build-cli`.

**The compiler** of the dev shells, the packages and the checks is Clang inside nixpkgs' compiler
wrapper, and the Clang itself is the LLVM project's own release of nixpkgs' version
(`nix/clang-release.nix`): it makes the same objects as nixpkgs' build of that version in 60 % of
the time ([PERFORMANCE.md](PERFORMANCE.md), "What Nix costs"). Its first use downloads the release
archive (1.9 GB, of which the compiler is kept); `nix build .#clang-llvm-release` fetches it by
itself, e.g. to push it to a binary cache of one's own. When `flake.lock` moves to a nixpkgs with
another Clang, `scripts/clang-release-pin.sh <version>` pins that release
(`nix/clang-release.json`) — the check `clang-release` fails until then. With the overlay over a
nixpkgs whose Clang has no pin, the builds use nixpkgs' Clang.

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
| `clang-release` | the LLVM project's release of nixpkgs' Clang version is pinned (`nix/clang-release.json`) and runs; fails after a nixpkgs update with another Clang until `scripts/clang-release-pin.sh` pins it |
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
| `nix/clang-release.nix`, `nix/clang-release.json` | the compiler: the LLVM project's release of Clang inside nixpkgs' wrapper (`packages.<system>.clang-llvm-release`) and its pins |
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
