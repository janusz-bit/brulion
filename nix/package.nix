{
  lib,
  stdenv,
  cmake,
  ninja,
  qt6,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "brulion";
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
      ../3rdparty
    ];
  };

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

  meta = {
    description = "Offline-first Markdown notes: outliner + journals, calendar, reminders, calculations";
    homepage = "https://github.com/janusz-bit/brulion";
    license = lib.licenses.agpl3Plus;
    mainProgram = "brulion";
    platforms = lib.platforms.linux;
  };
})
