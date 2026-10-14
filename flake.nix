{
  description = "Brulion — offline-first Markdown notes (outliner + journals, calendar, reminders) in C++/Qt";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: rec {
        brulion = pkgs.callPackage ./nix/package.nix { };
        default = brulion;
      });

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
          QT_PLUGIN_PATH = pkgs.lib.makeSearchPath pkgs.qt6.qtbase.qtPluginPrefix (
            with pkgs.qt6;
            [
              qtbase
              qtdeclarative
              qtsvg
              qtwayland
            ]
          );
          QML_IMPORT_PATH = pkgs.lib.makeSearchPath pkgs.qt6.qtbase.qtQmlPrefix [ pkgs.qt6.qtdeclarative ];
          shellHook = ''
            export CMAKE_GENERATOR=Ninja
            export CMAKE_CXX_COMPILER_LAUNCHER=ccache
          '';
        };
      });

      formatter = forAllSystems (pkgs: pkgs.nixfmt);
    };
}
