{
  description = "Brulion — offline-first Markdown notes (outliner + documents, journals, calendar, reminders, calculations; sync via Syncthing or Nextcloud)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      # Minimal system with both parts of the module enabled (checks.<system>.module).
      moduleTestSystem =
        system:
        lib.nixosSystem {
          modules = [
            self.nixosModules.default
            {
              nixpkgs.hostPlatform = system;
              boot.isContainer = true;
              system.stateVersion = lib.trivial.release;
              programs.brulion = {
                enable = true;
                autostart = true;
              };
              services.brulion-web = {
                enable = true;
                hostName = "notatki.example.com";
                basePath = "/notatki/";
                nextcloudUrl = "https://cloud.example.com";
                openFirewall = true;
              };
            }
          ];
        };
    in
    {
      overlays.default = final: _prev: {
        brulion = final.callPackage ./nix/package.nix { };
        brulion-web = final.callPackage ./nix/web.nix { };
      };

      packages = forAllSystems (
        pkgs:
        let
          npmDeps = pkgs.callPackage ./nix/npm-deps.nix { };
        in
        rec {
          brulion = pkgs.callPackage ./nix/package.nix { inherit npmDeps; };
          # Static PWA; other base paths: brulion-web.override { basePath = "/notatki/"; }
          brulion-web = pkgs.callPackage ./nix/web.nix { inherit npmDeps; };
          default = brulion;
        }
      );

      # Imports nix/module.nix with the packages of this flake as defaults.
      nixosModules.default =
        { lib, pkgs, ... }:
        let
          flakePackages = self.packages.${pkgs.stdenv.hostPlatform.system};
        in
        {
          imports = [ ./nix/module.nix ];
          programs.brulion.package = lib.mkDefault flakePackages.brulion;
          services.brulion-web.package = lib.mkDefault flakePackages.brulion-web;
        };

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            nodejs_22
            cargo
            rustc
            clippy
            rustfmt
            rust-analyzer
            cargo-tauri
            pkg-config
            actionlint
            shellcheck
            nixfmt
          ];

          buildInputs = with pkgs; [
            openssl
            webkitgtk_4_1
            libsoup_3
            gtk3
            glib
            glib-networking
            librsvg
            libayatana-appindicator
            gsettings-desktop-schemas
          ];

          # rust-analyzer finds the standard library sources here.
          RUST_SRC_PATH = "${pkgs.rustPlatform.rustLibSrc}";

          # What the wrapper of the packaged app sets, so `npm run tauri dev` works on NixOS.
          shellHook = ''
            # GSettings schemas (GTK file chooser of the dialog plugin, settings lookups).
            export XDG_DATA_DIRS="${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}:${pkgs.gtk3}/share/gsettings-schemas/${pkgs.gtk3.name}''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
            # TLS in the webview (glib-networking); GIO_EXTRA_MODULES is the additive form of
            # GIO_MODULE_DIR, so modules from the session (dconf) keep working.
            export GIO_EXTRA_MODULES="${pkgs.glib-networking}/lib/gio/modules''${GIO_EXTRA_MODULES:+:$GIO_EXTRA_MODULES}"
            # The tray backend dlopen()s libayatana-appindicator3.so.1.
            export LD_LIBRARY_PATH="${
              lib.makeLibraryPath [ pkgs.libayatana-appindicator ]
            }''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            # Blank or flickering window (some NVIDIA / older Mesa setups)? Try:
            # export WEBKIT_DISABLE_DMABUF_RENDERER=1
          '';
        };
      });

      formatter = forAllSystems (pkgs: pkgs.nixfmt-tree);

      checks = forAllSystems (
        pkgs:
        let
          inherit (pkgs.stdenv.hostPlatform) system;
          flakePackages = self.packages.${system};
        in
        {
          inherit (flakePackages) brulion brulion-web;

          # Vitest unit tests (npm test).
          tests = pkgs.callPackage ./nix/tests.nix { inherit (flakePackages) brulion-web; };

          # The NixOS module evaluates and the system (nginx config included) builds.
          module = (moduleTestSystem system).config.system.build.toplevel;

          # services.brulion-web in a VM (needs KVM).
          web-vm = import ./nix/vm-test.nix {
            inherit pkgs;
            module = self.nixosModules.default;
          };

          # Nix files are formatted (nix fmt).
          nix-format = pkgs.runCommand "brulion-nix-format" { nativeBuildInputs = [ pkgs.nixfmt ]; } ''
            nixfmt --check ${./flake.nix} ${./nix}/*.nix
            touch $out
          '';
        }
      );
    };
}
