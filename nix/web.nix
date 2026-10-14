# Brulion web version (PWA): the static Vite build, ready to be served by any web server.
#
# The app must know the URL path it is served from at build time:
#   brulion-web.override { basePath = "/notatki/"; }
# (services.brulion-web does this for you from its `basePath` option).
{
  lib,
  callPackage,
  buildNpmPackage,
  nodejs_22,
  # URL path the app is served from; must start and end with "/".
  basePath ? "/",
  # Shared with brulion (one download for both).
  npmDeps ? callPackage ./npm-deps.nix { },
}:

assert lib.assertMsg (builtins.match "/([A-Za-z0-9._~-]+/)*" basePath != null)
  "brulion-web: basePath must start and end with '/' and contain only [A-Za-z0-9._~-] segments (got '${basePath}')";

let
  sources = import ./source.nix { inherit lib; };
in
buildNpmPackage (finalAttrs: {
  pname = "brulion-web";
  inherit (lib.importJSON ../package.json) version;

  src = sources.web;

  nodejs = nodejs_22;
  inherit npmDeps;

  # Read by vite.config.ts (`base`).
  env.BRULION_BASE = basePath;

  # npmBuildHook runs `npm run build` (vite build -> dist/).
  installPhase = ''
    runHook preInstall
    cp -r dist $out
    runHook postInstall
  '';

  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    test -f $out/index.html
    # Asset URLs must carry the base path, or the app breaks when served below "/".
    grep -q 'src="${basePath}assets/' $out/index.html
    runHook postInstallCheck
  '';

  passthru = {
    inherit basePath npmDeps;
  };

  meta = {
    description = "Brulion web version (PWA): offline-first Markdown notes in the browser";
    homepage = "https://github.com/janusz-bit/brulion";
    license = lib.licenses.agpl3Plus;
    platforms = lib.platforms.all;
  };
})
