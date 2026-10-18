# Brulion for Android, built in the Nix sandbox: `nix build .#brulion-android` (arm64-v8a, phones)
# or `.#brulion-android-x86_64` (the emulator) gives result/brulion-<version>-<abi>-unsigned.apk,
# an UNSIGNED release APK (zip-aligned for 16 KB pages). Sign it with
# `nix run .#brulion-android-sign -- result/*.apk` (nix/android/sign.nix): keys never enter the
# Nix store. x86_64-linux only (the NDK and the SDK's build tools are x86_64 binaries).
#
# The same build as scripts/android-build.sh (docs/ANDROID.md), with every input pinned:
#   * Qt for Android: Qt's prebuilt archives, per module and ABI (nix/android/qt.nix,
#     qt-android.json);
#   * host Qt (moc, rcc, qmlcachegen, qmlimportscanner, androiddeployqt, lrelease): nixpkgs' Qt,
#     which must be the same version (asserted below);
#   * Android SDK platform 36, build-tools 36.0.0, NDK r27c, CMake 3.31.6 (+ Ninja): nixpkgs'
#     androidenv (the SDK's CMake, because nixpkgs' CMake does not search the NDK sysroot's
#     /usr and misses its EGL/GLES headers);
#   * Gradle 9 from nixpkgs (JDK 21) instead of the Gradle wrapper of Qt's template; Android
#     Gradle Plugin, Kotlin and androidx from a mitm-cache of recorded downloads
#     (nix/android/gradle-deps.json).
# Update the locks: nix/android/update-locks.sh.
{
  lib,
  stdenvNoCC,
  callPackage,
  androidenv,
  qt6,
  gradle_9,
  jdk21_headless,
  symlinkJoin,
  fetchFromGitHub,
  runtimeShell,
  unzip,
  gnugrep,

  # Brulion's version (the desktop package's, kept in sync by scripts/release.sh).
  version,
  # Android ABI: arm64-v8a (phones), x86_64 (emulator), armeabi-v7a or x86.
  abi ? "arm64-v8a",
  # Warnings in Brulion's own C++ code are errors (like BRULION_WERROR=ON of the script).
  werror ? false,
  # Also an unsigned Android App Bundle (.aab, Gradle's bundleRelease after the APK).
  withAab ? false,
}:

let
  # Keep in sync with scripts/android-build.sh and src/app/CMakeLists.txt (compile/target SDK).
  ndkVersion = "27.2.12479018"; # r27c, the NDK Qt 6.11 is built with
  buildToolsVersion = "36.0.0";
  platformVersion = "36";
  cmakeVersion = "3.31.6";

  # The license is accepted by the nixpkgs config (flake.nix: android_sdk.accept_license).
  androidComposition = androidenv.composeAndroidPackages {
    platformVersions = [ platformVersion ];
    buildToolsVersions = [ buildToolsVersion ];
    includeNDK = true;
    ndkVersions = [ ndkVersion ];
    includeCmake = true;
    cmakeVersions = [ cmakeVersion ];
    includeEmulator = false;
    includeSystemImages = false;
    includeSources = false;
    toolsVersion = null; # the obsolete "tools" package
  };
  sdk = "${androidComposition.androidsdk}/libexec/android-sdk";
  ndk = "${sdk}/ndk/${ndkVersion}";
  buildTools = "${sdk}/build-tools/${buildToolsVersion}";

  qtAndroid = callPackage ./android/qt.nix { inherit abi; };

  # One prefix with the host tools of every Qt module the build uses (QT_HOST_PATH): CMake
  # finds Qt6CoreTools, Qt6QmlTools, Qt6LinguistTools… under $QT_HOST_PATH/lib/cmake.
  qtHost = symlinkJoin {
    name = "qt-host-${qt6.qtbase.version}";
    paths = with qt6; [
      qtbase
      qtdeclarative
      qtsvg
      qttools
    ];
  };

  jdk = jdk21_headless;
  gradle = gradle_9.override { java = jdk; };

  # OpenSSL 3 for Android (KDAB's android_openssl, prebuilt libssl/libcrypto per ABI) for HTTPS:
  # nix/android/openssl.json pins the commit src/app/CMakeLists.txt names, which checks the
  # libraries by SHA-256 (without a pin, CMake would download them: not in the sandbox).
  opensslLock = lib.importJSON ./android/openssl.json;
  androidOpenssl =
    if opensslLock.rev == null then
      null
    else
      fetchFromGitHub {
        inherit (opensslLock)
          owner
          repo
          rev
          hash
          ;
      };

  is64bit = lib.elem abi [
    "arm64-v8a"
    "x86_64"
  ];
  androidBuildDir = "build/src/app/android-build";
  qtAndroidView = "$TMPDIR/qt-android";
in
assert lib.assertMsg (qtAndroid.version == qt6.qtbase.version) ''
  Qt for Android ${qtAndroid.version} (nix/android/qt-android.json) differs from the host Qt
  ${qt6.qtbase.version} of nixpkgs: run nix/android/update-locks.sh.'';

stdenvNoCC.mkDerivation (finalAttrs: {
  # One pname for every ABI, so that all of them share one Gradle dependency cache.
  pname = "brulion-android";
  name = "brulion-android-${abi}${lib.optionalString withAab "-aab"}-${version}";
  inherit version;

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../src
      ../i18n
      ../assets
      ../android
      ../3rdparty
    ];
  };

  strictDeps = true;
  nativeBuildInputs = [
    gradle
    jdk
    unzip
  ];

  # Gradle's dependencies (AGP, Kotlin, androidx…), served by mitm-cache's proxy in the build.
  # Shared by all ABIs; recorded by `nix build .#brulion-android.mitmCache.updateScript && ./result`
  # (nix/android/update-locks.sh), which runs this build up to Gradle with network access.
  mitmCache = gradle.fetchDeps {
    pkg = finalAttrs.finalPackage;
    data = ./android/gradle-deps.json;
    # The update run's bubblewrap has no /bin/sh, which Ninja runs its commands with.
    bwrapFlags = ''--ro-bind "$PWD" "$PWD" --symlink ${runtimeShell} /bin/sh'';
  };
  # The update run records what `assembleRelease` and `bundleRelease` (withAab) download
  # (preGradleUpdate below builds the Gradle project first), not every configuration of the
  # project.
  gradleUpdateTask = "assembleRelease bundleRelease";
  # The SDK's aapt2 (patched to run on NixOS) instead of the prebuilt one AGP takes from Maven.
  gradleFlags = [ "-Pandroid.aapt2FromMavenOverride=${buildTools}/aapt2" ];

  env = {
    ANDROID_SDK_ROOT = sdk;
    ANDROID_HOME = sdk;
    ANDROID_NDK_ROOT = ndk;
    JAVA_HOME = jdk.home;
  };

  # The configure options of scripts/android-build.sh (and the toolchain file of Qt for Android,
  # in configurePhase). Extend with overrideAttrs (cmakeFlags) or cmakeFlagsArray in a
  # preConfigure hook.
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DQT_HOST_PATH=${qtHost}"
    "-DANDROID_SDK_ROOT=${sdk}"
    "-DANDROID_NDK_ROOT=${ndk}"
    "-DQT_ANDROID_ABIS=${abi}"
    "-DBUILD_TESTING=OFF"
    # NDK r27 links 64-bit libraries with 16 KB segment alignment only when asked.
    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
    "-DBRULION_WERROR=${if werror then "ON" else "OFF"}"
    # Qt's own Polish texts (qtbase_pl.qm…), bundled as resources.
    "-DBRULION_QT_TRANSLATIONS_DIR=${qt6.qttranslations}/translations"
  ]
  ++ lib.optionals (androidOpenssl != null) [
    # The pinned checkout instead of CMake's download (src/app/CMakeLists.txt).
    "-DBRULION_ANDROID_OPENSSL_DIR=${androidOpenssl}"
  ];

  configurePhase = ''
    runHook preConfigure

    # AGP and Gradle write to ~/.android and ~/.gradle (GRADLE_USER_HOME is set by the hook).
    export HOME=$TMPDIR/home ANDROID_USER_HOME=$TMPDIR/home/.android
    mkdir -p "$ANDROID_USER_HOME"
    # The SDK's CMake and Ninja.
    export PATH=${sdk}/cmake/${cmakeVersion}/bin:$PATH

    # Qt for Android as a tree of symlinks with a writable copy of src/ (the Java/Gradle
    # templates): androiddeployqt copies template files into android-build/ and then edits some
    # (res/values/libs.xml…), but its copies keep the read-only mode of files in the store.
    cp -rs --no-preserve=mode ${qtAndroid} ${qtAndroidView}
    rm -r ${qtAndroidView}/src
    cp -r --no-preserve=mode ${qtAndroid}/src ${qtAndroidView}/src

    brulionCmakeFlags=()
    concatTo brulionCmakeFlags cmakeFlags cmakeFlagsArray
    cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=${qtAndroidView}/lib/cmake/Qt6/qt.toolchain.cmake \
      "''${brulionCmakeFlags[@]}"

    runHook postConfigure
  '';

  # androiddeployqt (target brulion_make_apk) copies Qt's Gradle template into android-build/
  # and runs its ./gradlew, which would download Gradle. A gradlew written beforehand stays:
  # androiddeployqt only overwrites a file with a NEWER template file, and the templates (the
  # copy of src/ made in configurePhase) are older than this gradlew, written in buildPhase —
  # keep that order. Ours runs nixpkgs' Gradle with the flags its setup hook gives the `gradle`
  # shell function (the mitm-cache proxy and its CA or --offline, the init script, gradleFlags).
  writeGradlew = ''
    mkdir -p ${androidBuildDir}
    brulionGradleFlags=()
    concatTo brulionGradleFlags gradleFlags gradleFlagsArray
    {
      echo '#!${runtimeShell}'
      echo '# Written by nix/android.nix: nixpkgs Gradle instead of the Gradle wrapper.'
      printf 'exec %q' ${gradle}/bin/gradle
      printf ' %q' "''${brulionGradleFlags[@]}"
      echo ' "$@"'
    } >${androidBuildDir}/gradlew
    chmod +x ${androidBuildDir}/gradlew
  '';

  buildApk = ''
    cmake --build build --parallel "$NIX_BUILD_CORES" --target brulion_make_apk
  '';

  buildPhase = ''
    runHook preBuild
    eval "$writeGradlew"
    eval "$buildApk"
    ${lib.optionalString withAab ''
      # The bundle of the Gradle project androiddeployqt prepared (the release build is done).
      (cd ${androidBuildDir} && gradle bundleRelease)
    ''}
    runHook postBuild
  '';

  # Only in the lock update run (gradleUpdateScript of the Gradle hook: no buildPhase): build
  # the Gradle project the same way, then the hook runs `gradle $gradleUpdateTask` in it.
  preGradleUpdate = ''
    eval "$writeGradlew"
    eval "$buildApk"
    cd ${androidBuildDir}
  '';

  installPhase = ''
    runHook preInstall
    apk=${androidBuildDir}/build/outputs/apk/release/android-build-release-unsigned.apk
    [[ -f $apk ]] || { echo "no unsigned APK at $apk" >&2; exit 1; }
    mkdir -p $out
    # 16 KB alignment of the uncompressed native libraries (Android 15+ with 16 KB pages),
    # which apksigner keeps when signing (brulion-android-sign checks it).
    ${buildTools}/zipalign -P 16 -f 4 "$apk" $out/brulion-${version}-${abi}-unsigned.apk
    ${lib.optionalString withAab ''
      cp ${androidBuildDir}/build/outputs/bundle/release/android-build-release.aab \
        $out/brulion-${version}-${abi}-unsigned.aab
    ''}
    runHook postInstall
  '';

  # The checks of scripts/android-build.sh: package, permissions, components, Kotlin classes and
  # Qt's Java classes (R8), the native library, OpenSSL, no QML debugging plugins and 16 KB page
  # alignment.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    apk=$out/brulion-${version}-${abi}-unsigned.apk
    fail() { echo "APK check failed: $*" >&2; exit 1; }

    badging=$(${buildTools}/aapt2 dump badging "$apk")
    ${gnugrep}/bin/grep -E "^(package|minSdkVersion|targetSdkVersion|application-label|native-code):" <<<"$badging"
    [[ $badging == *"package: name='io.github.januszbit.brulion'"* ]] || fail "wrong package name"
    [[ $badging == *"versionName='${version}'"* ]] || fail "versionName is not ${version}"
    [[ $badging == *"native-code: '${abi}'"* ]] || fail "native code is not ${abi}"
    for permission in MANAGE_EXTERNAL_STORAGE POST_NOTIFICATIONS SCHEDULE_EXACT_ALARM USE_EXACT_ALARM \
      RECEIVE_BOOT_COMPLETED VIBRATE WAKE_LOCK USE_FULL_SCREEN_INTENT INTERNET ACCESS_NETWORK_STATE; do
      [[ $badging == *"uses-permission: name='android.permission.$permission'"* ]] ||
        fail "permission $permission missing from the merged manifest"
    done
    manifest=$(${buildTools}/aapt2 dump xmltree --file AndroidManifest.xml "$apk")
    for component in BrulionActivity AlarmActivity ReminderReceiver ActionReceiver RingTimeoutReceiver BootReceiver; do
      [[ $manifest == *"io.github.januszbit.brulion.android.$component\""* ]] ||
        fail "$component missing from the merged manifest"
    done
    dex=$(unzip -p "$apk" 'classes*.dex' | ${gnugrep}/bin/grep -a -o 'Lio/github/januszbit/brulion/android/[A-Za-z]*;' | sort -u)
    for class in BrulionAndroid BrulionActivity AlarmScheduler ReminderNotifier AlarmActivity BootReceiver; do
      [[ $dex == *"/$class;"* ]] || fail "class $class missing from classes.dex"
    done
    # R8 (android/settings.gradle) must keep the Java classes C++ calls by name.
    qtDex=$(unzip -p "$apk" 'classes*.dex' | ${gnugrep}/bin/grep -a -o 'Lorg/qtproject/qt/android/Qt[A-Za-z]*;' | sort -u)
    for class in QtNative QtActivityBase QtLoader; do
      [[ $qtDex == *"/$class;"* ]] || fail "Qt's Java class $class missing from classes.dex (android/proguard-rules.pro)"
    done
    # ... and the androidx members that Qt's C++ calls (FileProvider.getUriForFile, font download).
    androidxNames=$(unzip -p "$apk" 'classes*.dex' | LC_ALL=C ${gnugrep}/bin/grep -a -o -E 'getUriForFile|fetchFonts|Landroidx/core/provider/FontRequest;' | sort -u)
    for name in getUriForFile fetchFonts 'Landroidx/core/provider/FontRequest;'; do
      ${gnugrep}/bin/grep -qxF "$name" <<<"$androidxNames" || fail "$name missing from classes.dex (android/proguard-rules.pro)"
    done
    listing=$(unzip -l "$apk")
    [[ $listing == *"lib/${abi}/libbrulion_${abi}.so"* ]] || fail "lib/${abi}/libbrulion_${abi}.so missing"
    ${lib.optionalString (androidOpenssl != null) ''
      for ssl in libssl_3.so libcrypto_3.so; do
        [[ $listing == *"lib/${abi}/$ssl"* ]] || fail "OpenSSL ($ssl) missing: no HTTPS"
      done
    ''}
    [[ $listing != *libplugins_qmltooling_* ]] || fail "QML debugging plugins (qmltooling) in a release APK"
    ${lib.optionalString withAab ''
      [[ $(unzip -l $out/brulion-${version}-${abi}-unsigned.aab) == *"base/lib/${abi}/libbrulion_${abi}.so"* ]] ||
        fail "the AAB has no base/lib/${abi}/libbrulion_${abi}.so"
    ''}
    echo "native libraries: $(${gnugrep}/bin/grep -c 'lib/.*\.so$' <<<"$listing"), Kotlin classes: $(wc -l <<<"$dex")"

    ${buildTools}/zipalign -c -P 16 4 "$apk" >/dev/null || fail "not zip-aligned for 16 KB pages"
    ${lib.optionalString is64bit ''
      # 64-bit libraries: ELF LOAD segments aligned to 16 KB. Brulion's own and OpenSSL must be;
      # Qt's are reported.
      mkdir elf && unzip -q "$apk" 'lib/${abi}/*.so' -d elf
      misaligned=()
      for so in elf/lib/${abi}/*.so; do
        min=$(${ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf -lW "$so" |
          awk '$1 == "LOAD" { a = strtonum($NF); if (min == "" || a < min) min = a } END { print min + 0 }')
        if ((min < 16384)); then
          [[ $so != */libbrulion_* && $so != */libssl_3.so && $so != */libcrypto_3.so ]] ||
            fail "$so: LOAD segments aligned to $min bytes"
          misaligned+=("''${so##*/} ($min)")
        fi
      done
      if ((''${#misaligned[@]})); then
        echo "warning: not 16 KB aligned: ''${misaligned[*]}"
      else
        echo "16 KB page alignment: all 64-bit libraries OK"
      fi
    ''}
    runHook postInstallCheck
  '';

  passthru = {
    inherit
      abi
      withAab
      qtAndroid
      qtHost
      androidComposition
      androidOpenssl
      gradle
      ;
  };

  meta = {
    description = "Brulion for Android (${abi}): unsigned release APK${lib.optionalString withAab " and AAB"}";
    homepage = "https://github.com/janusz-bit/brulion";
    license = lib.licenses.agpl3Plus;
    # Brulion's code built from source, with Qt's prebuilt libraries and Java libraries (androidx).
    sourceProvenance = with lib.sourceTypes; [
      fromSource
      binaryNativeCode
      binaryBytecode
    ];
    platforms = [ "x86_64-linux" ];
  };
})
