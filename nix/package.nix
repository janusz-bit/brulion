# The Brulion desktop app (Qt 6 from nixpkgs): bin/brulion, bin/brulion-cli (the command line for
# scripts and AI agents, docs/CLI.md; nix/cli.nix builds it alone), desktop entry, AppStream
# metainfo, icons and the agent skill (share/brulion/agent-skills). `nix build .#brulion` builds it without the unit tests; `nix flake check` runs them
# (checks.<system>.tests: this package with doCheck = true), as do CI's Linux job,
# scripts/dev-build.sh and scripts/remote-build.sh.
{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  qt6,
  kdePackages,
  appstream,
  desktop-file-utils,
  tzdata,
  git,
  python3,
  nodejs,
  fontconfig,
  hostname,
  ripgrep,
  dejavu_fonts,
  makeFontsConf,
  hunspell,
  hunspellDicts,
  tesseract,
  fetchurl,
  linkFarm,
  fetchFromGitHub,
  callPackage,
  # Audio and video in notes, voice notes (docs/UI.md "Audio and video"): Qt Multimedia without
  # what the app never uses (nix/multimedia.nix: FFmpeg for the formats notes play, no GStreamer).
  # false: a build without it (media embeds then offer "Open in another app").
  withMultimedia ? true,
  # The model on the device (docs/EXTENSIONS.md "AI"): llama.cpp compiled in from its pinned
  # sources as the plugin lib/brulion/libbrulion_localai.so with llama.cpp's libraries beside it.
  # false: a build without it (no "On this device" in the settings).
  withLocalAi ? true,
}:

let
  # Qt Multimedia over a FFmpeg of the formats notes play and record (nix/multimedia.nix).
  qtmultimedia = callPackage ./multimedia.nix { };
  # llama.cpp's sources, one release (3rdparty/llama/pin.json; cmake/BrulionLocalAi.cmake).
  llamaPin = lib.importJSON ../3rdparty/llama/pin.json;
  llamaSrc = fetchFromGitHub {
    inherit (llamaPin)
      owner
      repo
      tag
      hash
      ;
  };
  # whisper.cpp's sources for speech recognition over that llama.cpp's ggml (3rdparty/whisper/pin.json;
  # cmake/BrulionSpeech.cmake).
  whisperPin = lib.importJSON ../3rdparty/whisper/pin.json;
  whisperSrc = fetchFromGitHub {
    inherit (whisperPin)
      owner
      repo
      tag
      hash
      ;
  };
in

stdenv.mkDerivation (finalAttrs: {
  pname = "brulion";
  # Updated by scripts/release.sh together with CMakeLists.txt (the first `version = "...";` line).
  version = "0.32.0";

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions (
      [
        ../CMakeLists.txt
        ../cmake
        ../src
        ../tests
        ../i18n
        ../assets
        ../packaging
        ../3rdparty
        # Built into the core as a resource (src/core/extensions: the extensions reference).
        ../docs/EXTENSIONS.md
      ]
      # What the tests read besides their own folder; the package itself does not rebuild for these.
      ++ lib.optionals finalAttrs.doCheck [
        ../docs
        ../scripts
        ../fastlane
        ../wasm
        # tst_assistantbench: the AI assistant's tasks and their notebooks.
        ../finetune
        # tst_scripts_android_backup_rules: the manifest and the backup rules it names.
        ../android/AndroidManifest.xml
        ../android/res/xml
        # tst_clipper_extension: the Web Clipper extension's scripts and its unit tests.
        ../clipper
      ]
    );
  };

  strictDeps = true;

  nativeBuildInputs = [
    cmake
    ninja
    qt6.wrapQtAppsHook
    qt6.qttools
    # poppler-qt6 is found through pkg-config (src/platform/CMakeLists.txt).
    pkg-config
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg
    # Qt's own Polish texts (dialog buttons, file dialogs): bundled into the app as resources
    # (also found by the dev shell, which takes its inputs from here).
    qt6.qttranslations
    # PDF pages as a drawing's backdrop (src/platform/pdfpages_poppler.cpp): nixpkgs has Qt PDF only
    # inside QtWebEngine (+615 MiB); poppler-qt6 adds some 26 MiB (docs/PERFORMANCE.md "Drawing
    # backdrops: PDF renderers and what they cost").
    kdePackages.poppler
    # Spell checking in the editor (src/platform/spell_hunspell.cpp, docs/UI.md "Spell checking");
    # the dictionaries are found through BRULION_SPELL_DICTIONARY_DIRS below.
    hunspell
  ]
  ++ lib.optionals withMultimedia [ qtmultimedia ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    qt6.qtwayland
    # The tray calendar next to the panel under Wayland: a layer-shell surface
    # (src/platform/traywindow_layershell.cpp).
    kdePackages.layer-shell-qt
  ];

  # _FORTIFY_SOURCE=3 (glibc's checked memcpy, snprintf…) comes from cmake/BrulionHardening.cmake,
  # as on every other Linux build: nixpkgs' Clang wrapper knows only level 2 and, with its fortify
  # hardening on, passes -D_FORTIFY_SOURCE=2 after the build's own flags, so the package ended up
  # with 2 (docs/PERFORMANCE.md "Build times"). The rest of nixpkgs' hardening stays.
  hardeningDisable = [
    "fortify"
    "fortify3"
  ];

  # The programs and libraries without their symbol tables (nixpkgs strips only the debug
  # information by default): the local symbols are mostly the long names of the C++ that qmlcachegen
  # writes for the QML, a third of the app's file (docs/PERFORMANCE.md "Build times"). A crash
  # report names functions from the dynamic symbols (src/platform/crashreport.h), which stay; a
  # profiler or debugger wants a build of one's own (scripts/dev-build.sh).
  stripAllList = [
    "bin"
    "lib"
  ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" finalAttrs.finalPackage.doCheck)
    (lib.cmakeFeature "BRULION_QT_TRANSLATIONS_DIR" "${qt6.qttranslations}/translations")
    # The Polish and English Hunspell dictionaries (store paths compiled in; DICPATH and the
    # system's folders are searched too): docs/UI.md "Spell checking".
    (lib.cmakeFeature "BRULION_SPELL_DICTIONARY_DIRS" (
      lib.concatMapStringsSep ";" (d: "${d}/share/hunspell") finalAttrs.passthru.spellDictionaries
    ))
    # Text recognition (OCR) of pictures and scanned PDF pages for the search (docs/UI.md "Search",
    # src/platform/ocr_tesseract.cpp): Tesseract is loaded at run time, from this store path (its
    # string in the program keeps it in the closure); its English and Polish models from tessdata
    # below. Elsewhere (the AppImage, Windows) the system's Tesseract is looked for instead.
    (lib.cmakeFeature "BRULION_TESSERACT_LIBRARY" "${finalAttrs.passthru.tesseract}/lib/libtesseract.so.5")
    (lib.cmakeFeature "BRULION_TESSDATA_DIRS" "${finalAttrs.passthru.tessdata}")
    (lib.cmakeFeature "BRULION_LOCAL_AI" (if withLocalAi then "ON" else "OFF"))
    (lib.cmakeFeature "BRULION_MULTIMEDIA" (if withMultimedia then "ON" else "OFF"))
    # The relative relocations packed (RELR, cmake/BrulionSize.cmake): the package runs on the
    # glibc it was built with, always 2.36 or newer.
    (lib.cmakeBool "BRULION_PACK_RELOCATIONS" true)
  ]
  ++ lib.optionals withLocalAi [
    (lib.cmakeFeature "BRULION_LLAMA_SOURCE_DIR" "${llamaSrc}")
    # Speech recognition (voice notes' transcripts) needs the model runner's ggml.
    (lib.cmakeFeature "BRULION_SPEECH" "ON")
    (lib.cmakeFeature "BRULION_WHISPER_SOURCE_DIR" "${whisperSrc}")
  ];

  # llama.cpp's sources for the dev shell (flake.nix: BRULION_LLAMA_SOURCE_DIR).
  passthru.llamaSrc = llamaSrc;
  # whisper.cpp's sources for the dev shell (flake.nix: BRULION_WHISPER_SOURCE_DIR).
  passthru.whisperSrc = whisperSrc;
  # The Qt Multimedia it is built with (the dev shell's plugin and QML paths, flake.nix).
  passthru.qtmultimedia = qtmultimedia;

  # Dictionaries of the editor's spell checking (also on DICPATH in the dev shell, flake.nix).
  passthru.spellDictionaries = [
    hunspellDicts.pl_PL
    hunspellDicts.en_US
  ];

  # OCR (src/platform/ocr_tesseract.cpp, docs/PERFORMANCE.md "Searching attachments"): Tesseract
  # without nixpkgs' models (its standard ones are 43 MB for these two languages) and the "fast"
  # integer models of tessdata_fast instead — 9 MB, two to three times faster, plenty for search.
  # Also the dev shell's (flake.nix: BRULION_TESSERACT_LIBRARY, BRULION_TESSDATA_DIR).
  passthru.tesseract = tesseract.override { enableLanguages = [ ]; };
  passthru.tessdata = linkFarm "brulion-tessdata" [
    {
      name = "eng.traineddata";
      path = fetchurl {
        url = "https://github.com/tesseract-ocr/tessdata_fast/raw/4.1.0/eng.traineddata";
        hash = "sha256-fUMivSp3SXJIeWg/w5EstULxmQbIO8waUhMlVkJxcLI=";
      };
    }
    {
      name = "pol.traineddata";
      path = fetchurl {
        url = "https://github.com/tesseract-ocr/tessdata_fast/raw/4.1.0/pol.traineddata";
        hash = "sha256-xEds28DjPYmNMjRRIre+HL+FrOFfkg8Gx3FHVuHvebI=";
      };
    }
  ];

  # The QML smoke tests are `#!/usr/bin/env bash` scripts; the build sandbox has no /usr/bin/env.
  postPatch = ''
    patchShebangs tests ${lib.optionalString finalAttrs.doCheck "scripts"}
  '';

  # The Web Clipper's native messaging host manifests (docs/CLIPPER.md, docs/NIXOS.md): Firefox's where
  # programs.firefox.nativeMessagingHosts.packages (and the Firefox wrapper) find them, the Chromium
  # browsers' one for the NixOS module to link into /etc. Written by brulion-cli itself.
  postInstall = ''
    export HOME=$TMPDIR
    mkdir -p $out/lib/mozilla/native-messaging-hosts $out/share/brulion/native-messaging-hosts
    $out/bin/brulion-cli clip manifest firefox --program $out/bin/brulion-cli \
      >$out/lib/mozilla/native-messaging-hosts/io.github.januszbit.brulion.json
    $out/bin/brulion-cli clip manifest chromium --program $out/bin/brulion-cli \
      >$out/share/brulion/native-messaging-hosts/io.github.januszbit.brulion.json
  '';

  doCheck = false;
  # Without these the tests that need them skip themselves: git (tst_scripts_release, and real
  # repositories for the "Code" mode's tst_gitcli_repo and tst_codegit), python3
  # (tst_scripts_wasm_single_file) and hostname (tst_scripts_bench_budget), node (tst_uibench_*),
  # fc-match and DejaVu (tst_fallback_fonts_*).
  nativeCheckInputs = [
    git
    python3
    nodejs
    fontconfig
    hostname
    # (Code mode's project search tests rg itself where it is: docs/CODE-FILES.md.)
    ripgrep
  ];
  env = lib.optionalAttrs finalAttrs.doCheck {
    FONTCONFIG_FILE = makeFontsConf { fontDirectories = [ dejavu_fonts ]; };
  };
  preCheck = ''
    export CTEST_PARALLEL_LEVEL=$NIX_BUILD_CORES
    export CTEST_OUTPUT_ON_FAILURE=1
    export QT_QPA_PLATFORM=offscreen
    export HOME=$TMPDIR
    # The sandbox has no time zone database; the import/export tests use Europe/Warsaw.
    export TZDIR=${tzdata}/share/zoneinfo
    # The QML self-tests run the unwrapped binary: point Qt at the plugins (SVG image format,
    # platform plugins) and QML modules that wrapQtAppsHook only adds to the installed wrapper.
    export QT_PLUGIN_PATH=${
      lib.makeSearchPath qt6.qtbase.qtPluginPrefix (
        [
          qt6.qtbase
          qt6.qtsvg
          qt6.qtdeclarative
        ]
        ++ lib.optionals withMultimedia [ qtmultimedia ]
      )
    }''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
    export QML_IMPORT_PATH=${
      lib.makeSearchPath qt6.qtbase.qtQmlPrefix (
        [ qt6.qtdeclarative ] ++ lib.optionals withMultimedia [ qtmultimedia ]
      )
    }''${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}
  '';

  # The installed desktop integration files are valid and consistent with each other
  # (validate-tree also checks that the metainfo's launchable desktop file and icon exist).
  doInstallCheck = true;
  nativeInstallCheckInputs = [
    appstream
    desktop-file-utils
  ];
  installCheckPhase = ''
    runHook preInstallCheck
    id=io.github.januszbit.brulion
    test -x $out/bin/brulion
    test -x $out/bin/brulion-cli
    test -f $out/share/brulion/agent-skills/brulion/SKILL.md
    # The Web Clipper's host manifests name this package's brulion-cli.
    grep -qF "\"path\": \"$out/bin/brulion-cli\"" $out/lib/mozilla/native-messaging-hosts/io.github.januszbit.brulion.json
    grep -qF "brulion-clipper@januszbit.github.io" $out/lib/mozilla/native-messaging-hosts/io.github.januszbit.brulion.json
    grep -qF "chrome-extension://calcbkckojhmdfecpcohaangneahmkgg/" $out/share/brulion/native-messaging-hosts/io.github.januszbit.brulion.json
    ${lib.optionalString withLocalAi ''
      # The model on the device: the plugin, llama.cpp's libraries and a processor plugin beside
      # it, each finding the others there (no library left unresolved).
      test -f $out/lib/brulion/libbrulion_localai.so
      # … the embedding model's plugin for search by meaning, and the speech recognizer's, over
      # the same ggml.
      test -f $out/lib/brulion/libbrulion_semantic.so
      test -f $out/lib/brulion/libbrulion_speech.so
      ls $out/lib/brulion/libggml-cpu-*.so >/dev/null
      # … where the app looks for it: lib/brulion, from its program's folder.
      grep -aqF -- '../lib/brulion' $out/bin/.brulion-wrapped
      for lib in $out/lib/brulion/*.so*; do
        if ldd "$lib" | grep -F "not found"; then
          echo "$lib: a library it needs is not found" >&2
          exit 1
        fi
      done
    ''}
    # The command line runs (on a notebook of its own; no display, no settings touched).
    export HOME=$TMPDIR
    $out/bin/brulion-cli version
    $out/bin/brulion-cli init "$TMPDIR/Notatki"
    $out/bin/brulion-cli --vault "$TMPDIR/Notatki" --now "2026-10-14 10:30" capture "Zadzwonić jutro o 9"
    grep -q -- "- \[ \] Zadzwonić ⏰ 2026-10-15 09:00" "$TMPDIR/Notatki/journals/2026-10-14.md"
    $out/bin/brulion-cli --vault "$TMPDIR/Notatki" info >/dev/null
    test -f $out/share/icons/hicolor/scalable/apps/$id.svg
    test -f $out/share/icons/hicolor/256x256/apps/$id.png
    desktop-file-validate $out/share/applications/$id.desktop
    # "Open with → Other application → Brulion" for Markdown and text files (docs/NIXOS.md), and
    # brulion:// links (docs/UI.md "Automation"): files and addresses as %U.
    grep -qx 'Exec=brulion --open %U' $out/share/applications/$id.desktop
    # No file types — a desktop without a default program would make Brulion the default for
    # them —, only the brulion:// scheme.
    grep -qx 'MimeType=x-scheme-handler/brulion;' $out/share/applications/$id.desktop
    if [ "$(grep -c '^MimeType=' $out/share/applications/$id.desktop)" != 1 ]; then
      echo "the desktop entry declares file types" >&2
      exit 1
    fi
    # Launchers hand over an activation token: the running Brulion comes to the front.
    grep -qx 'StartupNotify=true' $out/share/applications/$id.desktop
    appstreamcli validate --no-net --explain $out/share/metainfo/$id.metainfo.xml
    appstreamcli validate-tree --no-net --explain $out
    runHook postInstallCheck
  '';

  meta = {
    description = "Offline-first Markdown notes: outliner + journals, calendar, reminders, calculations";
    homepage = "https://github.com/janusz-bit/brulion";
    license = lib.licenses.agpl3Plus;
    mainProgram = "brulion";
    platforms = lib.platforms.linux;
  };
})
