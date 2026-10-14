# The Brulion desktop app (Qt 6 from nixpkgs): bin/brulion, desktop entry, AppStream metainfo
# and icons. `nix build .#brulion` builds it and runs the unit tests (ctest).
{
  lib,
  stdenv,
  cmake,
  ninja,
  qt6,
  appstream,
  desktop-file-utils,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "brulion";
  # Updated by scripts/release.sh together with CMakeLists.txt (the first `version = "...";` line).
  version = "0.1.0";

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
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [ qt6.qtwayland ];

  cmakeFlags = [ (lib.cmakeBool "BUILD_TESTING" finalAttrs.finalPackage.doCheck) ];

  doCheck = true;
  preCheck = ''
    export QT_QPA_PLATFORM=offscreen
    export HOME=$TMPDIR
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
