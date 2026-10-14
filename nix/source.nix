# Source trees for the Nix builds. Only the files a build reads are included, so editing docs,
# CI, e2e tests or the Nix files themselves does not rebuild the app, and stray local state
# (node_modules, dist, src-tauri/target, ...) never leaks into a build.
{ lib }:
let
  fs = lib.fileset;
  root = ../.;

  # Unit tests (`*.test.ts(x)`) are only needed by the `tests` check.
  unitTests = fs.fileFilter (f: lib.hasInfix ".test." f.name) ../src;

  frontend = fs.unions [
    ../package.json
    ../package-lock.json
    ../index.html
    ../vite.config.ts
    ../tsconfig.json
    ../public
    ../src
    # Component gallery, built by vite.config.ts only once its entry module exists.
    (fs.maybeMissing ../gallery.html)
  ];

  tauri = fs.unions [
    (fs.difference ../src-tauri (
      fs.unions (
        map fs.maybeMissing [
          ../src-tauri/target
          ../src-tauri/gen
          ../src-tauri/plugins/brulion-android/android/.tauri
          ../src-tauri/plugins/brulion-android/android/build
        ]
      )
    ))
    # The Rust tests check the IPC contract documented here (include_str!).
    ../docs/ARCHITECTURE.md
  ];
in
{
  # PWA build (nix/web.nix).
  web = fs.toSource {
    inherit root;
    fileset = fs.difference frontend unitTests;
  };

  # PWA sources with the unit tests (`npm test`).
  webWithTests = fs.toSource {
    inherit root;
    fileset = frontend;
  };

  # Desktop app: frontend + Tauri shell (nix/package.nix).
  app = fs.toSource {
    inherit root;
    fileset = fs.unions [
      (fs.difference frontend unitTests)
      tauri
    ];
  };
}
