# npm dependency cache (fixed-output derivation) shared by `brulion` and `brulion-web`.
#
# The hash covers package-lock.json itself, so it changes with every change to that file
# (new dependencies, but also a version bump). Recompute it with:
#
#   scripts/update-nix-hashes.sh
{ lib, fetchNpmDeps }:

fetchNpmDeps {
  name = "brulion-npm-deps";
  # Only the lock file: the derivation (and the download) stays the same until it changes.
  src = lib.fileset.toSource {
    root = ../.;
    fileset = ../package-lock.json;
  };
  hash = "sha256-HJ0Nr0hW9c8I5EbVXesX9p2QG3jcW0CIkPOw7irJNzk=";
}
