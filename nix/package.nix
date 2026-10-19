# The Brulion desktop app (Qt 6 from nixpkgs): bin/brulion, desktop entry, AppStream metainfo
# and icons. `nix build .#brulion` builds it without the unit tests (they run in CI's Linux job
# and with scripts/dev-build.sh / scripts/remote-build.sh; `brulion.overrideAttrs { doCheck = true; }`
# turns them on here).
{
  lib,
  stdenv,
  cmake,
  ninja,
  qt6,
  appstream,
  desktop-file-utils,
  tzdata,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "brulion";
  # Updated by scripts/release.sh together with CMakeLists.txt (the first `version = "...";` line).
  version = "0.2.1";

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../src
      ../tests
      ../i18n
      ../assets
      ../packaging
      ../3rdparty
    ];
  };

  strictDeps = true;

  nativeBuildInputs = [
    cmake
    ninja
    qt6.wrapQtAppsHook
    qt6.qttools
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg
    # Qt's own Polish texts (dialog buttons, file dialogs): bundled into the app as resources
    # (also found by the dev shell, which takes its inputs from here).
    qt6.qttranslations
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [ qt6.qtwayland ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" finalAttrs.finalPackage.doCheck)
    (lib.cmakeFeature "BRULION_QT_TRANSLATIONS_DIR" "${qt6.qttranslations}/translations")
  ];

  # The QML smoke tests are `#!/usr/bin/env bash` scripts; the build sandbox has no /usr/bin/env.
  postPatch = ''
    patchShebangs tests
  '';

  doCheck = false;
  preCheck = ''
    export QT_QPA_PLATFORM=offscreen
    export HOME=$TMPDIR
    # The sandbox has no time zone database; the import/export tests use Europe/Warsaw.
    export TZDIR=${tzdata}/share/zoneinfo
    # The QML self-tests run the unwrapped binary: point Qt at the plugins (SVG image format,
    # platform plugins) and QML modules that wrapQtAppsHook only adds to the installed wrapper.
    export QT_PLUGIN_PATH=${
      lib.makeSearchPath qt6.qtbase.qtPluginPrefix [
        qt6.qtbase
        qt6.qtsvg
        qt6.qtdeclarative
      ]
    }''${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
    export QML_IMPORT_PATH=${
      lib.makeSearchPath qt6.qtbase.qtQmlPrefix [ qt6.qtdeclarative ]
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
    test -f $out/share/icons/hicolor/scalable/apps/$id.svg
    test -f $out/share/icons/hicolor/256x256/apps/$id.png
    desktop-file-validate $out/share/applications/$id.desktop
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
