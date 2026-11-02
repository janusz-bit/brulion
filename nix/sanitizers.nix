# The whole ctest suite under AddressSanitizer and UndefinedBehaviorSanitizer, in the build sandbox:
# `nix build .#brulion-sanitizers -L` (CI's sanitizers job; a package, not a check, so that
# `nix flake check` does not build everything twice). Debug at -O1 (what the sanitizers recommend;
# about half of -O0's time in the CPU-bound tests) keeps Q_ASSERT on, -g1 the line tables the
# reports need, -Werror the warnings out. The result holds only the test times (ctest-junit.xml).
{ brulion }:

brulion.overrideAttrs (old: {
  pname = "brulion-sanitizers";
  doCheck = true;
  doInstallCheck = false;
  dontWrapQtApps = true;
  dontStrip = true;
  # _FORTIFY_SOURCE's checked libc calls hide what ASan intercepts.
  hardeningDisable = (old.hardeningDisable or [ ]) ++ [ "fortify" ];
  cmakeBuildType = "Debug";
  cmakeFlags = old.cmakeFlags ++ [ "-DBRULION_WERROR=ON" ];
  # (Flags with spaces: not in cmakeFlags.)
  preConfigure = (old.preConfigure or "") + ''
    cmakeFlagsArray+=(
      "-DCMAKE_CXX_FLAGS_DEBUG=-g1 -O1"
      "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -fno-optimize-sibling-calls"
      "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
    )
  '';
  env = old.env // {
    # Leak checking off: Qt keeps global singletons alive until exit.
    ASAN_OPTIONS = "detect_leaks=0:strict_string_checks=1:detect_stack_use_after_return=1";
    UBSAN_OPTIONS = "print_stacktrace=1:halt_on_error=1";
  };
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure --parallel "$NIX_BUILD_CORES" --timeout 300 \
      --output-junit "$NIX_BUILD_TOP/ctest-junit.xml"
    runHook postCheck
  '';
  installPhase = ''
    runHook preInstall
    install -D "$NIX_BUILD_TOP/ctest-junit.xml" $out/ctest-junit.xml
    runHook postInstall
  '';
  meta = old.meta // {
    description = "Brulion's tests under AddressSanitizer and UndefinedBehaviorSanitizer";
  };
})
