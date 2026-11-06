# embedbench for Android phones (arm64-v8a): `nix build .#embedbench-android` gives
# result/bin/embedbench, a static executable with llama.cpp's CPU (NEON, dotprod, fp16) and Vulkan
# backends — the speed of the semantic search prototype's embedding model on a real phone:
#
#   adb push result/bin/embedbench model-q8_0.gguf /data/local/tmp/
#   adb shell 'cd /data/local/tmp && ./embedbench -m model-q8_0.gguf'
#
# Same NDK as the app (nix/android.nix, r27c), llama.cpp sources of the pinned nixpkgs (the version
# the desktop prototype links). x86_64-linux only (the NDK is an x86_64 binary).
{
  lib,
  stdenv,
  androidenv,
  cmake,
  ninja,
  shaderc,
  vulkan-headers,
  spirv-headers,
  llama-cpp,
  # The CPU baseline: armv8.2-a with dotprod and fp16 runs on phones from about 2018 on
  # (Cortex-A55/A75); "armv8.6-a+i8mm" is faster on 2021+ cores.
  armArch ? "armv8.2-a+dotprod+fp16",
  # The minimum API level: Vulkan 1.1 is guaranteed from Android 10 (API 29) on 64-bit devices.
  apiLevel ? 29,
  withVulkan ? true,
}:

let
  ndkVersion = "27.2.12479018";
  androidComposition = androidenv.composeAndroidPackages {
    platformVersions = [ "36" ];
    includeNDK = true;
    ndkVersions = [ ndkVersion ];
    includeEmulator = false;
    includeSystemImages = false;
    includeSources = false;
    toolsVersion = null;
  };
  ndk = "${androidComposition.androidsdk}/libexec/android-sdk/ndk/${ndkVersion}";
in
stdenv.mkDerivation {
  pname = "embedbench-android";
  version = llama-cpp.version;
  src = ../../scripts/semantic/embedbench;

  # cmake/ninja/glslc and a host C++ compiler (ggml builds its Vulkan shader generator for the
  # build machine) run here; everything else is compiled by the NDK's clang.
  nativeBuildInputs = [
    cmake
    ninja
  ]
  ++ lib.optional withVulkan shaderc;
  dontUseCmakeConfigure = true;
  dontStrip = true; # the host strip does not know aarch64: llvm-strip of the NDK below

  buildPhase = ''
    runHook preBuild
    cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=${ndk}/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-${toString apiLevel} \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLAMA_SRC=${llama-cpp.src} \
      -DBUILD_SHARED_LIBS=OFF \
      -DGGML_NATIVE=OFF \
      -DGGML_CPU_ARM_ARCH=${armArch} \
      -DGGML_OPENMP=OFF \
      -DGGML_VULKAN=${if withVulkan then "ON" else "OFF"} \
      ${lib.optionalString withVulkan ''
        -DVulkan_INCLUDE_DIR=${vulkan-headers}/include \
        -DSPIRV-Headers_DIR=${spirv-headers}/share/cmake/SPIRV-Headers \
        -DCMAKE_CXX_FLAGS=-I${spirv-headers}/include \
        -DVulkan_GLSLC_EXECUTABLE=${shaderc.bin}/bin/glslc \
        -DVulkan_LIBRARY=${ndk}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/${toString apiLevel}/libvulkan.so \
      ''} \
      -DCMAKE_INSTALL_PREFIX=$out
    cmake --build build --target embedbench -j $NIX_BUILD_CORES
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 build/embedbench $out/bin/embedbench
    ${ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip $out/bin/embedbench
    runHook postInstall
  '';

  meta = {
    description = "Embedding model speed (CPU and Vulkan) on Android, for Brulion's semantic search prototype";
    platforms = [ "x86_64-linux" ];
  };
}
