# The browser build of a Brulion release: brulion-web-<version>.zip, attached to every GitHub
# release by .github/workflows/release.yml (the site's files at the top level of the zip).
# nixpkgs has no Qt for WebAssembly, so this is the way to get it from Nix.
#
#   pkgs.callPackage ./web-release.nix { version = "0.1.0"; hash = "sha256-…"; }
#
# With hash = "" the build fails and prints the right hash ("got: sha256-…").
{
  fetchzip,
  version,
  tag ? null,
  hash ? "",
}:

fetchzip {
  name = "brulion-web-${version}";
  url = "https://github.com/janusz-bit/brulion/releases/download/${
    if tag != null then tag else "v${version}"
  }/brulion-web-${version}.zip";
  inherit hash;
  stripRoot = false;
}
