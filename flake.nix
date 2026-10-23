{
  description = "Brulion — offline-first Markdown notes (outliner + journals, calendar, reminders) in C++/Qt";

  # Pinned by flake.lock (the same nixpkgs revision as the developer's NixOS system).
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

      # The Android SDK and NDK are unfree: a nixpkgs that allows them (and accepts the Android
      # SDK license), used for the Android packages only.
      androidPkgs =
        system:
        import nixpkgs {
          inherit system;
          config = {
            android_sdk.accept_license = true;
            allowUnfreePredicate =
              pkg: lib.hasPrefix "https://developer.android.com/" (pkg.meta.homepage or "");
          };
        };

      # Android (x86_64-linux only): an unsigned release APK per ABI, nix/android.nix.
      androidPackages =
        system:
        let
          pkgs = androidPkgs system;
          apk =
            args:
            pkgs.callPackage ./nix/android.nix (args // { inherit (self.packages.${system}.brulion) version; });
        in
        {
          brulion-android = apk { abi = "arm64-v8a"; };
          brulion-android-x86_64 = apk { abi = "x86_64"; };
          # The arm64-v8a APK and an Android App Bundle (.aab).
          brulion-android-aab = apk {
            abi = "arm64-v8a";
            withAab = true;
          };
        };
    in
    {
      # nix build .#brulion                 desktop app (also the default; with bin/brulion-cli)
      # nix build .#brulion-cli             the command line alone (no Qt Quick: small, for servers)
      # nix build .#brulion-android         Android APK, arm64-v8a, unsigned (x86_64-linux hosts)
      # nix build .#brulion-android-x86_64  Android APK for the emulator, unsigned
      # nix build .#brulion-android-aab     arm64-v8a APK + Android App Bundle, unsigned
      # nix run .#brulion-android-sign -- result/*.apk   signs an APK with the release key
      packages = forAllSystems (
        pkgs:
        rec {
          brulion = pkgs.callPackage ./nix/package.nix { };
          default = brulion;
          brulion-cli = pkgs.callPackage ./nix/cli.nix { inherit brulion; };
          brulion-android-sign = pkgs.callPackage ./nix/android/sign.nix { };
        }
        // lib.optionalAttrs (pkgs.stdenv.hostPlatform.system == "x86_64-linux") (
          androidPackages pkgs.stdenv.hostPlatform.system
        )
      );

      # nix run .#brulion-cli -- --vault ~/Notatki task list   the command line (docs/CLI.md)
      apps = forAllSystems (pkgs: {
        brulion-cli = {
          type = "app";
          program = lib.getExe self.packages.${pkgs.stdenv.hostPlatform.system}.brulion-cli;
          meta.description = "Brulion's command line for scripts and AI agents (brulion-cli mcp: MCP server)";
        };
      });

      # pkgs.brulion (and pkgs.brulion-cli) built with the nixpkgs of whoever applies the overlay.
      overlays.default = final: _prev: {
        brulion = final.callPackage ./nix/package.nix { };
        brulion-cli = final.callPackage ./nix/cli.nix { };
      };

      # programs.brulion (desktop app, autostart in the tray) and services.brulion-web (the
      # browser version behind nginx): docs/NIXOS.md.
      nixosModules = {
        default = import ./nix/module.nix { inherit self; };
        brulion = self.nixosModules.default;
      };

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.brulion ];
          packages = with pkgs; [
            ccache
            clang-tools
            gdb
            qt6.qttools
            nixfmt
          ];
          # Run the unwrapped binaries from ./build: point Qt at the plugins/QML modules.
          QT_PLUGIN_PATH = lib.makeSearchPath pkgs.qt6.qtbase.qtPluginPrefix (
            with pkgs.qt6;
            [
              qtbase
              qtdeclarative
              qtsvg
              qtwayland
            ]
          );
          QML_IMPORT_PATH = lib.makeSearchPath pkgs.qt6.qtbase.qtQmlPrefix [ pkgs.qt6.qtdeclarative ];
          shellHook = ''
            export CMAKE_GENERATOR=Ninja
            export CMAKE_CXX_COMPILER_LAUNCHER=ccache
          '';
        };
      });

      # `nix fmt`: every .nix file of the repository.
      formatter = forAllSystems (pkgs: pkgs.nixfmt-tree);

      checks = forAllSystems (
        pkgs:
        let
          inherit (pkgs.stdenv.hostPlatform) system;
        in
        {
          # Builds the app (no unit tests: doCheck = false) and checks the desktop file/AppStream data;
          # the command line alone runs a capture on a notebook of its own.
          inherit (self.packages.${system}) brulion brulion-cli;

          # The NixOS module evaluates; its nginx config and autostart entry build.
          nixos-module = import ./nix/tests/module-eval.nix {
            inherit pkgs lib nixpkgs;
            module = self.nixosModules.default;
          };

          # The .nix files are formatted (`nix fmt`).
          nixfmt =
            pkgs.runCommand "brulion-nixfmt"
              {
                nativeBuildInputs = [ pkgs.nixfmt ];
                src = lib.fileset.toSource {
                  root = ./.;
                  fileset = lib.fileset.fileFilter (file: file.hasExt "nix") ./.;
                };
              }
              ''
                cd "$src"
                find . -name '*.nix' -print0 | xargs -0 nixfmt --check
                touch "$out"
              '';
        }
        # services.brulion-web and programs.brulion in a VM (needs KVM; x86_64 only, so the
        # Raspberry Pi and the aarch64 CI runner skip it).
        // lib.optionalAttrs (system == "x86_64-linux") {
          web-vm = import ./nix/tests/web-vm.nix {
            inherit pkgs;
            module = self.nixosModules.default;
          };
        }
      );
    };
}
