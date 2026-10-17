# Qt for Android (one ABI): Qt's prebuilt libraries, plugins, QML modules, CMake packages and
# Java/Gradle templates, unpacked from the archives of Qt's online repository (the files aqt and
# the Qt installer download), pinned in qt-android.json (nix/android/update-locks.sh).
#
# Why per-archive fetches and not one fixed-output derivation running aqt: every archive is
# checked against the SHA-256 Qt publishes for it, a mirror can serve it, nothing depends on
# aqt's version or behaviour, and a new ABI or module is one more pinned file. aqt's own changes
# to an Android install only concern qmake/qtpaths scripts (bin/target_qt.conf), which a CMake
# build does not use.
{
  lib,
  stdenvNoCC,
  fetchurl,
  _7zz,
  abi,
}:

let
  lock = lib.importJSON ./qt-android.json;
  abiLock = lock.abis.${abi} or (throw "qt-android.json has no Qt for Android ABI ${abi}");

  # Qt's online repository and some of its mirrors (download.qt.io redirects to one of them).
  mirrors = [
    "https://download.qt.io/online/qtsdkrepository"
    "https://ftp.fau.de/qtproject/online/qtsdkrepository"
    "https://mirrors.ocf.berkeley.edu/qt/online/qtsdkrepository"
    "https://qt.mirror.constant.com/online/qtsdkrepository"
  ];

  archives = lib.mapAttrsToList (
    module: archive:
    fetchurl {
      name = "${module}-${lock.version}-${abiLock.qtArch}.7z";
      urls = map (mirror: "${mirror}/${archive.path}") mirrors;
      inherit (archive) hash;
    }
  ) abiLock.archives;
in
stdenvNoCC.mkDerivation {
  pname = "qt-android-${abi}";
  inherit (lock) version;

  srcs = archives;
  nativeBuildInputs = [ _7zz ];

  # Every archive unpacks into the same prefix (lib/, plugins/, qml/, src/android/…).
  unpackPhase = ''
    runHook preUnpack
    mkdir -p $out
    for archive in $srcs; do
      7zz x -y -bd -o"$out" "$archive" >/dev/null
    done
    runHook postUnpack
  '';
  dontConfigure = true;
  dontBuild = true;
  dontInstall = true;
  # Android ELF files and Qt's scripts as Qt shipped them: no stripping, no patchelf, no shebang
  # rewriting.
  dontFixup = true;

  passthru = {
    inherit abi;
    inherit (abiLock) qtArch;
  };

  meta = {
    description = "Qt ${lock.version} for Android (${abi}), prebuilt by The Qt Company";
    homepage = "https://doc.qt.io/qt-6/android.html";
    license = with lib.licenses; [
      lgpl3Only
      gpl2Only
      gpl3Only
    ];
    sourceProvenance = [ lib.sourceTypes.binaryNativeCode ];
    platforms = [ "x86_64-linux" ];
  };
}
