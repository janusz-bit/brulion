# `npm test` (vitest, all unit tests) in the Nix sandbox: offline, UTC, no display.
{ lib, brulion-web }:

let
  sources = import ./source.nix { inherit lib; };
in
brulion-web.overrideAttrs {
  pname = "brulion-tests";
  src = sources.webWithTests;

  dontNpmBuild = true;
  doCheck = true;
  checkPhase = ''
    runHook preCheck
    npm test
    runHook postCheck
  '';

  installPhase = ''
    touch $out
  '';
  doInstallCheck = false;
}
