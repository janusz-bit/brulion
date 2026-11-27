# Clang as the LLVM project releases it, in place of nixpkgs' own build of the same version inside
# nixpkgs' compiler wrapper (flake.nix: brulionStdenv). The same sources and version, and the same
# output — an object differs in nothing but the compiler's name in its .comment section
# (docs/PERFORMANCE.md "What Nix costs") — but a faster program: the release is one statically
# linked binary built with ThinLTO, profile-guided optimisation and BOLT (llvm-project's
# clang/cmake/caches/Release.cmake), where nixpkgs' clang is a small driver over the shared libLLVM
# and libclang-cpp built with none of them. A source file of Brulion compiles in 60–64 % of the
# time (arm64, -O0 and -O3 alike).
#
# null when nix/clang-release.json knows no release for this nixpkgs' Clang version and system
# (an overlay over another nixpkgs): the caller then keeps nixpkgs' Clang.
#
# Only the compiler changes hands: the wrapper around it, its flags, the builtin headers and
# compiler-rt (the wrapper's resource directory), libstdc++, glibc and the linker (lld) stay
# nixpkgs' own.
{
  lib,
  stdenv,
  fetchurl,
  autoPatchelfHook,
  zlib,
  # nixpkgs' Clang without its wrapper (clangStdenv.cc.cc): its version picks the release, and the
  # wrapper reads from it what the compiler can do.
  nixpkgsClang,
}:

let
  inherit (nixpkgsClang) version;
  major = lib.versions.major version;
  pins = (lib.importJSON ./clang-release.json).releases;
  pin = pins.${version}.${stdenv.hostPlatform.system} or null;

  # The release archive is 1.9 GB of every LLVM tool, each linked statically; only the compiler
  # (0.3 GB) is kept. The hash is of that folder, not of the archive, so the store path is the same
  # under any nixpkgs: one download — or one push to a binary cache — serves every later build. The
  # archive itself is checked against the digest GitHub publishes for it.
  binary = fetchurl {
    name = "clang-${version}-llvm-release-binary";
    url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-${version}/${pin.archive}.tar.xz";
    downloadToTemp = true;
    recursiveHash = true;
    postFetch = ''
      echo "${pin.sha256}  $downloadedFile" | sha256sum --check --strict -
      mkdir -p "$out/bin"
      tar -xJf "$downloadedFile" -C "$out/bin" --strip-components=2 \
        "${pin.archive}/bin/clang-${major}"
      chmod 755 "$out/bin/clang-${major}"
    '';
    inherit (pin) hash;
  };
in

if pin == null then
  null
else
  stdenv.mkDerivation {
    pname = "clang-llvm-release";
    inherit version;

    dontUnpack = true;
    dontConfigure = true;
    dontBuild = true;
    # (the release is stripped already; nothing to gain from reading 0.3 GB again)
    dontStrip = true;

    # The binary looks for glibc's loader in /lib and for libz, libstdc++ and libgcc_s on the
    # system's library path: patched to nixpkgs' own.
    nativeBuildInputs = [ autoPatchelfHook ];
    buildInputs = [
      zlib
      stdenv.cc.cc.lib
    ];

    installPhase = ''
      runHook preInstall
      install -Dm755 ${binary}/bin/clang-${major} $out/bin/clang-${major}
      ln -s clang-${major} $out/bin/clang
      ln -s clang $out/bin/clang++
      ln -s clang $out/bin/clang-cpp
      ln -s clang-cpp $out/bin/cpp
      # The download stays alive as long as the compiler does: a newer nixpkgs patches the binary
      # again (another glibc, another store path) without fetching 1.9 GB again.
      mkdir -p $out/nix-support
      echo ${binary} >$out/nix-support/llvm-release-binary
      runHook postInstall
    '';

    doInstallCheck = true;
    installCheckPhase = ''
      runHook preInstallCheck
      $out/bin/clang --version | grep -F "clang version ${version}"
      runHook postInstallCheck
    '';

    passthru = {
      inherit binary;
      # What nixpkgs' compiler wrapper asks its compiler (pkgs/build-support/cc-wrapper): the same
      # answers as nixpkgs' Clang gives, and that Clang's library output — the wrapper's resource
      # directory is its builtin headers either way.
      isClang = true;
      langC = true;
      langCC = true;
      lib = lib.getLib nixpkgsClang;
    }
    // lib.optionalAttrs (nixpkgsClang ? hardeningUnsupportedFlagsByTargetPlatform) {
      inherit (nixpkgsClang) hardeningUnsupportedFlagsByTargetPlatform;
    }
    // lib.optionalAttrs (nixpkgsClang ? hardeningUnsupportedFlags) {
      inherit (nixpkgsClang) hardeningUnsupportedFlags;
    };

    meta = {
      description = "Clang ${version} as released by the LLVM project (ThinLTO, PGO and BOLT: a faster compiler than nixpkgs' build)";
      homepage = "https://github.com/llvm/llvm-project/releases/tag/llvmorg-${version}";
      license = with lib.licenses; [
        asl20
        llvm-exception
      ];
      sourceProvenance = [ lib.sourceTypes.binaryNativeCode ];
      platforms = lib.attrNames (pins.${version} or { });
    };
  }
