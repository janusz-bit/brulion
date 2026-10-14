# Brulion desktop app (Tauri 2) for Linux, built the way nixpkgs builds Tauri v2 apps:
# `cargo tauri build --bundles deb` through cargo-tauri.hook, which runs the frontend build
# (`beforeBuildCommand` = `npm run build`) and installs the deb's usr/ tree (bin/brulion, the
# .desktop file and the hicolor icons) into $out. wrapGAppsHook3 then wraps bin/brulion.
{
  lib,
  callPackage,
  rustPlatform,
  cargo-tauri,
  nodejs_22,
  npmHooks,
  pkg-config,
  wrapGAppsHook3,
  desktop-file-utils,
  glib,
  glib-networking,
  gsettings-desktop-schemas,
  gtk3,
  libayatana-appindicator,
  librsvg,
  libsoup_3,
  openssl,
  webkitgtk_4_1,
  xdg-utils,
  # Shared with brulion-web (one download for both).
  npmDeps ? callPackage ./npm-deps.nix { },
}:

let
  sources = import ./source.nix { inherit lib; };
  # Same Node.js major version as CI (.github/workflows).
  npmHooks' = npmHooks.override { nodejs = nodejs_22; };
in
rustPlatform.buildRustPackage (finalAttrs: {
  pname = "brulion";
  # Single source of the version: package.json (scripts/release.sh bumps it).
  inherit (lib.importJSON ../package.json) version;

  src = sources.app;

  cargoRoot = "src-tauri";
  buildAndTestSubdir = finalAttrs.cargoRoot;
  # Vendored from Cargo.lock; no hash to maintain (add `outputHashes` for git dependencies).
  cargoLock.lockFile = ../src-tauri/Cargo.lock;

  inherit npmDeps;

  nativeBuildInputs = [
    cargo-tauri.hook
    nodejs_22
    npmHooks'.npmConfigHook
    pkg-config
    wrapGAppsHook3
    desktop-file-utils
  ];

  buildInputs = [
    glib
    glib-networking # TLS for the webview (WebDAV sync, images over https)
    gsettings-desktop-schemas
    gtk3
    libayatana-appindicator
    librsvg
    libsoup_3
    openssl
    webkitgtk_4_1
  ];

  # The Rust unit tests (incl. the IPC contract tests against docs/ARCHITECTURE.md) run here
  # too, so `nix build` / `nix flake check` test the app on NixOS. Debug profile: the release
  # profile (fat LTO, one codegen unit) would link every test binary with LTO, and
  # `cargo test` builds without tauri's custom-protocol feature anyway.
  checkType = "debug";
  # The app and the brulion-android plugin crate (its Rust <-> Kotlin <-> docs contract tests).
  cargoTestFlags = [ "--workspace" ];
  # A writable home for the Rust tests (the sandbox's /homeless-shelter is not).
  preCheck = ''
    export HOME=$(mktemp -d)
  '';

  postInstall = ''
    desktop-file-validate $out/share/applications/*.desktop
  '';

  preFixup = ''
    gappsWrapperArgs+=(
      # The tray icon backend dlopen()s libayatana-appindicator3.so.1 at run time (and
      # src-tauri/src/tray.rs probes for it first); without it Brulion runs without a tray.
      --prefix LD_LIBRARY_PATH : ${lib.makeLibraryPath [ libayatana-appindicator ]}
      # Opening links and attachments in other applications (fallback when the desktop
      # session provides no xdg-open of its own).
      --suffix PATH : ${lib.makeBinPath [ xdg-utils ]}
    )
  '';

  passthru = {
    inherit npmDeps;
  };

  meta = {
    description = "Offline-first Markdown notes: outliner and documents, journals, calendar, reminders, calculations; sync via Syncthing or Nextcloud";
    longDescription = ''
      Brulion is an open-source, offline-first notes app. Notes are plain Markdown files in a
      folder (Obsidian-style documents and Logseq-style outlines with daily journals), with
      tasks, a calendar, reminders with alarms, inline calculations and tables, import and
      export from other note apps, and sync through Syncthing or Nextcloud (WebDAV).
    '';
    homepage = "https://github.com/janusz-bit/brulion";
    changelog = "https://github.com/janusz-bit/brulion/releases";
    license = lib.licenses.agpl3Plus;
    mainProgram = "brulion";
    platforms = lib.platforms.linux;
  };
})
