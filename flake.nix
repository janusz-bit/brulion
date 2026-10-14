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
    in
    {
      packages = forAllSystems (pkgs: rec {
        brulion = pkgs.callPackage ./nix/package.nix { };
        default = brulion;
      });

      # pkgs.brulion built with the nixpkgs of whoever applies the overlay.
      overlays.default = final: _prev: {
        brulion = final.callPackage ./nix/package.nix { };
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
          # Builds the app and runs the unit tests (ctest) and the desktop file/AppStream checks.
          inherit (self.packages.${system}) brulion;

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
