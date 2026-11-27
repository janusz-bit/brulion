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

      # The LLVM project's own release of nixpkgs' Clang version (nix/clang-release.nix; null where
      # nix/clang-release.json has none: an overlay over another nixpkgs).
      clangRelease =
        pkgs: pkgs.callPackage ./nix/clang-release.nix { nixpkgsClang = pkgs.clangStdenv.cc.cc; };

      # The compiler of the Linux builds here: Clang over GCC's libstdc++ (clangStdenv; the C++
      # library Qt uses), linking with LLVM's lld instead of GNU ld — three times faster links,
      # and identical code folded (cmake/BrulionSize.cmake: --icf=safe; docs/PERFORMANCE.md "Build
      # times"). Inside nixpkgs' wrapper the compiler itself is the LLVM project's release where
      # there is one: the same objects as nixpkgs' build of that version makes, in 60 % of the time
      # (docs/PERFORMANCE.md "What Nix costs").
      brulionStdenv =
        pkgs:
        let
          release = clangRelease pkgs;
        in
        pkgs.overrideCC pkgs.clangStdenv (
          pkgs.clangStdenv.cc.override (
            {
              bintools = pkgs.llvmPackages.bintools;
            }
            // lib.optionalAttrs (release != null) { cc = release; }
          )
        );

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
          # The app for Google Play (docs/GOOGLE_PLAY.md): its APK and AAB, arm64-v8a.
          brulion-android-play = apk {
            abi = "arm64-v8a";
            googlePlay = true;
          };
          # Search by meaning: the embedding model's speed on a phone (nix/android/embedbench.nix).
          embedbench-android = pkgs.callPackage ./nix/android/embedbench.nix { };
        };
    in
    {
      # nix build .#brulion                 desktop app (also the default; with bin/brulion-cli)
      # nix build .#brulion-cli             the command line alone (no Qt Quick: small, for servers)
      # nix build .#brulion-android         Android APK, arm64-v8a, unsigned (x86_64-linux hosts)
      # nix build .#brulion-android-x86_64  Android APK for the emulator, unsigned
      # nix build .#brulion-android-aab     arm64-v8a APK + Android App Bundle, unsigned
      # nix build .#brulion-android-play    the app for Google Play: APK + AAB, unsigned (docs/GOOGLE_PLAY.md)
      # nix run .#brulion-android-sign -- result/*.apk   signs an APK with the release key
      packages = forAllSystems (
        pkgs:
        rec {
          # Built with Clang (brulionStdenv: nixpkgs' Clang over GCC's libstdc++, the one Qt uses,
          # and lld): a smaller app and command line than with GCC, the same speed, and the
          # compiler of the Android, browser and macOS builds (docs/PERFORMANCE.md "GCC or Clang").
          brulion = pkgs.callPackage ./nix/package.nix { stdenv = brulionStdenv pkgs; };
          default = brulion;
          brulion-cli = pkgs.callPackage ./nix/cli.nix {
            inherit brulion;
            stdenv = brulionStdenv pkgs;
          };
          brulion-android-sign = pkgs.callPackage ./nix/android/sign.nix { };
        }
        # The compiler of these builds by itself (nix/clang-release.nix): `nix build
        # .#clang-llvm-release` fetches it once, e.g. to push it to a binary cache.
        // lib.optionalAttrs (clangRelease pkgs != null) { clang-llvm-release = clangRelease pkgs; }
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
        brulion = final.callPackage ./nix/package.nix { stdenv = brulionStdenv final; };
        brulion-cli = final.callPackage ./nix/cli.nix { stdenv = brulionStdenv final; };
      };

      # programs.brulion (desktop app, autostart in the tray) and services.brulion-web (the
      # browser version behind nginx): docs/NIXOS.md.
      nixosModules = {
        default = import ./nix/module.nix { inherit self; };
        brulion = self.nixosModules.default;
      };

      devShells = forAllSystems (
        pkgs:
        let
          inherit (self.packages.${pkgs.stdenv.hostPlatform.system}) brulion brulion-cli;
          # The packages' compiler: `cc` / `c++` are Clang, linking with lld (scripts/dev-build.sh).
          mkShell = pkgs.mkShell.override { stdenv = brulionStdenv pkgs; };
          tools = with pkgs; [
            ccache
            clang-tools
            gdb
            nixfmt
          ];
          # With the fortify hardening nixpkgs' compiler wrapper puts -O2 (and _FORTIFY_SOURCE=2) in
          # front of every compile, so a Debug build of this shell was an -O2 one: twice the compile
          # time, five times for the C++ of the QML, and code a debugger cannot follow. Without it the
          # build type decides, as with any other compiler: Debug -O0, Release -O3 with
          # _FORTIFY_SOURCE=3 from cmake/BrulionHardening.cmake (docs/PERFORMANCE.md "Build times").
          # The rest of the list is the package's (nix/package.nix): a build made here has the code
          # the package has, but for the frame pointers of leaf functions, which the wrapper keeps
          # in this shell — for profilers (docs/PERFORMANCE.md "What Nix costs").
          inherit (brulion) hardeningDisable;
          # ccache tells one compiler from another by its file's size and time, and the compiler here
          # is nixpkgs' wrapper script: every wrapper in the store has the time 1 and, for store
          # paths of one length, one size — and what it adds to a compile (the hardening list above)
          # comes from the environment, which ccache never sees. So the cache answered with objects
          # of another compiler or of other flags (docs/PERFORMANCE.md "What Nix costs"): it is told
          # the wrapper's store path and that list instead.
          shellHook = ''
            export CMAKE_GENERATOR=Ninja
            export CMAKE_CXX_COMPILER_LAUNCHER=ccache
            export CCACHE_COMPILERCHECK="string:$NIX_CC $NIX_HARDENING_ENABLE"
          '';
        in
        {
          # scripts/dev-shell.sh enters it from a saved profile: no evaluation on every call.
          default = mkShell {
            inputsFrom = [ brulion ];
            packages =
              tools
              ++ (with pkgs; [
                llama-cpp-vulkan # embedbench on the GPU (scripts/semantic/embedbench, docs/SEMANTIC.md); the app builds its own (3rdparty/llama)
                qt6.qttools
              ]);
            # Run the unwrapped binaries from ./build: point Qt at the plugins/QML modules.
            QT_PLUGIN_PATH = lib.makeSearchPath pkgs.qt6.qtbase.qtPluginPrefix (
              with pkgs.qt6;
              [
                qtbase
                qtdeclarative
                qtsvg
                qtwayland
                # (the package's own, nix/multimedia.nix: its FFmpeg plugin)
                brulion.qtmultimedia
              ]
            );
            QML_IMPORT_PATH = lib.makeSearchPath pkgs.qt6.qtbase.qtQmlPrefix [
              pkgs.qt6.qtdeclarative
              brulion.qtmultimedia
            ];
            # Hunspell's dictionaries for the spell checking of ./build (the package compiles their
            # store paths in instead: nix/package.nix).
            DICPATH = lib.concatMapStringsSep ":" (d: "${d}/share/hunspell") brulion.spellDictionaries;
            # Tesseract and its English and Polish models for the OCR of ./build (the package compiles
            # these store paths in instead: nix/package.nix; src/platform/ocr_tesseract.cpp loads the
            # library at run time).
            BRULION_TESSERACT_LIBRARY = "${brulion.tesseract}/lib/libtesseract.so.5";
            BRULION_TESSDATA_DIR = brulion.tessdata;
            # llama.cpp's sources for the model on the device (cmake/BrulionLocalAi.cmake: a build
            # configured in this shell compiles them in; 3rdparty/llama/pin.json names the release).
            BRULION_LLAMA_SOURCE_DIR = brulion.llamaSrc;
            # whisper.cpp's sources for speech recognition (cmake/BrulionSpeech.cmake: over that
            # llama.cpp's ggml; 3rdparty/whisper/pin.json names the release).
            BRULION_WHISPER_SOURCE_DIR = brulion.whisperSrc;
            inherit hardeningDisable shellHook;
          };

          # The core, brulion-cli and their tests alone (`scripts/dev-shell.sh -s cli …`): the inputs
          # of packages.brulion-cli — Qt's base; no Qt Quick, and no Qt Multimedia over its own FFmpeg
          # to compile first (nix/multimedia.nix: cache.nixos.org does not have it). The shell for the
          # Raspberry Pi. scripts/dev-build.sh configures such a build (BRULION_BUILD_APP=OFF) in a
          # directory of its own, ./build-cli.
          cli = mkShell {
            inputsFrom = [ brulion-cli ];
            packages = tools;
            inherit hardeningDisable;
            shellHook = shellHook + ''
              export BRULION_BUILD_APP=OFF
              export BUILD_DIR="''${BUILD_DIR:-build-cli}"
            '';
          };
        }
      );

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

          # The whole ctest suite, in the build sandbox (`nix build .#checks.<system>.tests -L`).
          tests = self.packages.${system}.brulion.overrideAttrs { doCheck = true; };

          # The builds compile with the LLVM project's release of nixpkgs' Clang version
          # (nix/clang-release.nix), which runs here. Fails when flake.lock has moved to a nixpkgs
          # whose Clang nix/clang-release.json does not know: without it every build would quietly
          # go back to nixpkgs' slower Clang.
          clang-release =
            let
              release = clangRelease pkgs;
              inherit (pkgs.clangStdenv.cc.cc) version;
            in
            if release != null then
              release
            else
              pkgs.runCommand "brulion-clang-release-missing" { } ''
                echo "nix/clang-release.json has no release of Clang ${version} for ${system}:" >&2
                echo "run scripts/clang-release-pin.sh ${version}" >&2
                exit 1
              '';

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
