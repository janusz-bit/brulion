# brulion-cli alone (packages.<system>.brulion-cli, docs/CLI.md): the command line for scripts and
# AI agents, built with BRULION_BUILD_APP=OFF — Qt's base only (no Qt Quick, QML, SVG, Wayland or
# Qt's tools), so its closure is small and it builds quickly on a Raspberry Pi. The app's package
# (nix/package.nix) ships brulion-cli too.
{
  lib,
  stdenv,
  cmake,
  ninja,
  qt6,
  tzdata,
  brulion,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "brulion-cli";
  inherit (brulion) version src;

  strictDeps = true;

  nativeBuildInputs = [
    cmake
    ninja
  ];

  buildInputs = [ qt6.qtbase ];

  cmakeFlags = [
    (lib.cmakeBool "BRULION_BUILD_APP" false)
    (lib.cmakeBool "BUILD_TESTING" finalAttrs.finalPackage.doCheck)
  ];

  # QtCore only: no plugins to point it at.
  dontWrapQtApps = true;

  postPatch = ''
    patchShebangs tests
  '';

  # The core and command-line tests (`brulion-cli.overrideAttrs { doCheck = true; }`).
  doCheck = false;
  preCheck = ''
    export HOME=$TMPDIR
    export QT_QPA_PLATFORM=offscreen
    export TZDIR=${tzdata}/share/zoneinfo
  '';

  # It runs: a notebook made, a capture written with a Polish date, the notebook read back.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    export HOME=$TMPDIR
    $out/bin/brulion-cli version
    $out/bin/brulion-cli init "$TMPDIR/Notatki"
    $out/bin/brulion-cli --vault "$TMPDIR/Notatki" --now "2026-10-14 10:30" capture "Zadzwonić jutro o 9"
    grep -q -- "- \[ \] Zadzwonić ⏰ 2026-10-15 09:00" "$TMPDIR/Notatki/journals/2026-10-14.md"
    $out/bin/brulion-cli --vault "$TMPDIR/Notatki" info >/dev/null
    test -f $out/share/brulion/agent-skills/brulion/SKILL.md
    runHook postInstallCheck
  '';

  meta = brulion.meta // {
    description = "Brulion's command line for scripts and AI agents (also an MCP server)";
    mainProgram = "brulion-cli";
  };
})
