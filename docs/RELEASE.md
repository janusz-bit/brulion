# CI and releases (GitHub Actions)

Every build in GitHub Actions can be repeated locally with the same scripts from `scripts/`
(on a Raspberry Pi only through `laptop-run` — see CLAUDE.md).

Tests in GitHub Actions do not start on their own at every push. They run:

* **by hand** — Actions → the workflow → *Run workflow*, or
  `gh workflow run ci.yml --ref main` (likewise `android.yml`, `appimage.yml`, `windows.yml`,
  `wasm.yml -f deploy=false`);
* **with the `[ci]` marker** in the message of one of the commits pushed to `main` (e.g.
  `git commit -m "Fix the export [ci]"`) — then `ci.yml` starts, and `android.yml`,
  `appimage.yml`, `windows.yml` and `wasm.yml` when the push changes their paths; a push without
  the marker shows up in Actions as a run with skipped jobs;
* **always on a release** — `release.yml` runs `ci.yml` and every build with its tests, and
  publishes the release only when everything passes.

## Workflows

| File | When | What it does |
|---|---|---|
| `.github/workflows/ci.yml` | by hand, a push to `main` with the `[ci]` marker, from `release.yml` | **nix** (x86_64 and aarch64): `nix flake check` (with the whole `ctest` in the Nix sandbox: `checks.<system>.tests`) + `nix build .#brulion` (the package without tests — `doCheck = false`; the VM test `web-vm` needs KVM — a runner without `/dev/kvm` builds the other checks and skips it with a warning); **linux**: CMake + Qt 6.11.2 from aqt (`jurplel/install-qt-action`), `-DBRULION_WERROR=ON`, AddressSanitizer + UBSan at `-O1` (assertions on), ccache between runs (`actions/cache`, saved right after the build), `ctest` (GUI tests alongside the others, the longest start first — `tests/ctest-costs.txt`; test times as the artifact `ctest-junit-linux-asan`), `qmllint` (warnings only); **lint**: actionlint, shellcheck of `scripts/*.sh` and `tests/scripts/*.sh`, clang-format of the changed lines; **perf** (non-blocking): `scripts/perf-check.sh -s` on the benchmark suites in Release — how the time scales with the data size and the index's memory, no absolute times; a regression gives a warning and a summary, the results as the artifact `perf-smoke` (docs/PERFORMANCE.md); the benchmarks have a time limit (`bench.sh -T 1500`, the steps their own timeouts, the job 80 min), so a slow runner does not cancel the run; `release.yml` skips it (`skip-perf-smoke: true`) — a release does not wait for it |
| `.github/workflows/android.yml` | by hand, a push to `main` with the `[ci]` marker (changes in `src/`, `android/`…), from `release.yml` | `scripts/android-build.sh`: a signed arm64 APK + Kotlin tests + Android lint → the artifact `brulion-android-apk` |
| `.github/workflows/wasm.yml` | by hand, a push to `main` with the `[ci]` marker (changes in `src/`, `wasm/`…), from `release.yml` | `scripts/wasm-build.sh --setup`, a smoke test in Chrome (`scripts/wasm-smoke.mjs`: start, notes that persist, the index without input, an offline start; the single HTML file from `file://`) → the artifacts `brulion-web` (the site) and `brulion-web-html` (a single file, docs/WEB.md "Single HTML file"); **browsers**: Playwright tests in Chromium and Firefox (`scripts/web-test.sh`, `tests/web`: pictures from the internet — CORS, mixed content, no preflight, export with copies —, stylus pressure and eraser, address parameters, the single HTML file from disk; the report and the traces of failed tests → the artifact `web-browser-tests`, docs/WEB.md "Browser tests"); **keyboard-android** (not for releases: `skip-emulator`): the on-screen keyboard in browsers on an Android emulator. Deploys nothing — a release that calls `wasm.yml` also waits for `browsers` |
| `.github/workflows/fdroid.yml` | by hand (from `main` or a `v*` tag), from `release.yml` | Brulion's own F-Droid repository on GitHub Pages: `scripts/fdroid-repo.sh` (fdroidserver 2.4.5 from PyPI) with the APKs of the last three releases, the index signed with the repository's key (the `FDROID_*` secrets), a page with the QR code → deployed to Pages (docs/ANDROID.md "Brulion's own F-Droid repository") |
| `.github/workflows/appimage.yml` | by hand, a push to `main` with the `[ci]` marker (changes in `src/`, `tests/`…), from `release.yml` | `scripts/appimage-build.sh` for x86_64 (an Ubuntu 22.04 container) and aarch64 (Ubuntu 24.04): a Release build, tests, the AppImage, a trial run → the artifacts `brulion-appimage-<arch>` |
| `.github/workflows/windows.yml` | by hand, a push to `main` with the `[ci]` marker (changes in `src/`, `tests/`, `packaging/windows/`…), from `release.yml` | `scripts/windows-build.ps1` on `windows-2022` (Qt 6.11.2 MSVC 2022 from aqt, MSVC, Ninja): a Release build, offscreen tests, `windeployqt`, the portable ZIP and the Inno Setup installer, both checked without Qt in `PATH` (the unpacked ZIP, a silent per-user install, the shell's self-test, a silent uninstall) → the artifact `brulion-windows` (the test version, docs/WINDOWS.md) |
| `.github/workflows/release.yml` | a `v*` tag or by hand | checks the version, creates a draft release, runs the tests (`ci.yml` without **perf**), the four builds above + `git archive`, attaches the files and `SHA256SUMS.txt`, publishes the release and rebuilds the F-Droid repository on GitHub Pages with its APK (`fdroid.yml`; not for a pre-release or an APK signed with a throwaway key) |

The tools' versions are pinned in the scripts (Qt 6.11.2, aqtinstall 3.3.0, NDK r27c, Emscripten
4.0.7, linuxdeploy with SHA-256 sums); `dependabot.yml` updates the GitHub actions once a month in
one PR. The downloaded Qt, NDK and emsdk are in `actions/cache` (keys with the versions — after a
version bump they are downloaded afresh once).

### Formatting (clang-format)

The code style is in `.clang-format` (clang-format 21 from `nix develop`); `.clang-format-ignore`
leaves out foreign code and the files with JavaScript in `EM_JS`. CI checks **only the changed
lines** (`git clang-format`) — existing code is not reformatted wholesale: on a push with the
`[ci]` marker the lines of the pushed commits, on a run by hand the lines changed since the last
release, on a release those since the previous one. A formatting difference is only a warning.
Before a commit:

```sh
nix develop -c git clang-format          # formats the lines changed against HEAD
```

## One-time repository setup

1. **GitHub Pages:** Settings → Pages → Build and deployment → Source: **GitHub Actions**.
   `https://janusz-bit.github.io/brulion/` holds the F-Droid repository (`fdroid.yml`). For a
   release to be able to deploy it: Settings → Environments → `github-pages` → *Deployment
   branches and tags*: the branch `main` and the tag `v*` (from the command line:
   `gh api -X POST repos/janusz-bit/brulion/environments/github-pages/deployment-branch-policies -f name='v*' -f type=tag`).
   The F-Droid repository's key: `scripts/fdroid-new-keystore.sh` (the secrets
   `FDROID_KEYSTORE_BASE64`, `FDROID_KEYSTORE_PASS`, `FDROID_KEY_ALIAS`; docs/ANDROID.md
   "Brulion's own F-Droid repository"), then the first deployment: `gh workflow run fdroid.yml`.
2. **The Android signing key:** `scripts/android-new-keystore.sh` creates a key outside the
   repository and prints the `gh secret set` commands for the secrets `QT_ANDROID_KEYSTORE_BASE64`,
   `QT_ANDROID_KEYSTORE_STORE_PASS`, `QT_ANDROID_KEYSTORE_KEY_PASS`, `QT_ANDROID_KEYSTORE_ALIAS`.
   Without them the APK is signed with a throwaway key (a warning in the summary and in the
   release notes). **A backup of the key and the password is mandatory** — updates must have the
   same signature.
3. The `ubuntu-24.04-arm` runners (the Nix aarch64 build, the aarch64 AppImage) are free for
   public repositories.

## Releasing

Every commit goes on `main`: the development, the release notes (`.github/release-notes.md`) and
the version commit "Release vX.Y.Z". The `release` branch has **nothing but commits of main** — no
commit of its own and no merge commit: it is always an ancestor of `main` (or `main` itself) and
sits at the last release's commit, so the branches differ only in how many commits they have
(`git rev-list main..release` is empty). Releasing means moving `release` forward to main's
"Release vX.Y.Z" commit (a fast-forward — a rebase with nothing to move, and a merge without a
merge commit) and giving that commit the tag `vX.Y.Z`: the same commit on both branches. The
releases are the tags: `git tag -l 'v*' --sort=v:refname`, and in the history
`git log --oneline --decorate release` (a tag at every "Release vX.Y.Z" commit). This is what
`scripts/release.sh` does:

```sh
git switch main && git pull --ff-only
# .github/release-notes.md + a section "## X.Y.Z — <date>" in src/app/shell/guide/en/Changelog.md
#   and src/app/shell/guide/pl/Lista zmian.md
#   -> commit "Update release notes for vX.Y.Z" (on main); release.sh writes the fastlane changelogs
scripts/release.sh --dry-run patch     # preview: versions, commits, warnings
scripts/release.sh patch --push        # the version on main, release moved onto it + the tag, push
```

The release notes are the release's description on GitHub (the news of this version). They are in
English first, with the same text in Polish below in a `<details>` section ("Po polsku"): the
English part has the sections **What's new in {{VERSION}}** (the news as bullets, then a "From
version …" paragraph summarising the earlier versions), **Highlights**, the download table,
**Installation** and the checksums; the Polish part has the same in Polish (**Nowości w
{{VERSION}}** with its "Z wersji …" paragraph, **Najważniejsze**, the table, **Instalacja**).
Update both languages. The page "Changelog" in the documentation notebook
(`src/app/shell/guide/en/Changelog.md`, newest on top; in Polish "Lista zmian",
`src/app/shell/guide/pl/Lista zmian.md`) collects every change of every version — each release
adds its section at the top of both pages (New / Changes / Fixes / Note; in Polish Nowe / Zmiany /
Poprawki / Uwaga). Their syntax examples must be in code (`⏰ …`, `#tag`, `- [ ]`), because they
are live notes. `release.sh` warns when the notes have not changed or when the Polish list lacks
the section of the version being released (a pre-release needs none).

This way `main` always has the released version and its notes — `nix run
github:janusz-bit/brulion` (the flake without a tag) and the CI artifacts are built from it (an APK
from main has no lower `versionCode` than the released one). Do not commit anything on `release`:
a fix for a release is a commit on main and another release. History: up to v0.3.4 releases were
made on main, and v0.3.5 on `release` (its notes and version commit reached main before v0.3.6).
v0.3.6 and v0.3.7 were merge commits of main into `release` ("Merge main into release for
vX.Y.Z", the tree as in main's "Release vX.Y.Z") and their tags stayed on those merges — the published
releases point to them. Since 2026-10-23 `release` starts afresh at main's "Release v0.3.7" commit
and is only ever a prefix of main.

The arguments of `scripts/release.sh`:

```sh
scripts/release.sh minor --push        # 0.3.7 -> 0.4.0: "Release v0.4.0" on main, release at it with the tag v0.4.0, push
scripts/release.sh 0.4.0-rc.1          # a test release (pre-release); the files have version 0.4.0
scripts/release.sh 0.4.0-rc.2          # the version is in the files already: no version commit, release at main's HEAD + tag
scripts/release.sh 0.4.0               # main unchanged since -rc.2: the tag v0.4.0 on the same commit
scripts/release.sh --dry-run patch     # only shows what it would do (from any branch)
```

A real run works only on `main` (from another branch it stops at once) and with a clean working
tree:

1. **Checks.** It fetches `main` and `release` from origin (`git fetch`, without tags) and refuses
   when main lacks commits of `origin/main` (`git pull --ff-only` — also when the release was made
   on another computer: `release` is then ahead of the local main, but nothing has to be undone);
   when the local `release` and `origin/release` have diverged; when `release` (the local one or
   origin's) has a commit that main lacks (`git rev-list main..release` must be empty) — a commit
   of its own or a merge, e.g. the old `release` with the v0.3.7 merge: the script lists those
   commits and says how to put `release` back at the last commit of main it has
   (`git branch -f release <commit>`, and when origin has the same, also
   `git push --force-with-lease origin <commit>:refs/heads/release`); a change main needs has to
   go to main first (`git cherry-pick`); when `release` is checked out in another worktree and is
   to move (that checkout would be left behind); when the tag exists already (locally or on
   origin); with `patch`/`minor`/`major`, when the current version has only test releases (e.g.
   after `0.4.0-rc.2` `patch` would skip 0.4.0 — give the version itself). It warns when
   `.github/release-notes.md` has not changed since the last release (it compares with the commit
   `release` is at; the release would repeat the previous notes) — only the news count, in either
   language, not the "From version …" / "Z wersji …" summaries —, and when the local `release` is
   ahead of `origin/release` with the tag of an earlier release made without `--push` — the push
   sends its commits, but not its tag (the script gives `git push origin <tag>`).
2. **The version on main.** It sets the version in `CMakeLists.txt` (`project(... VERSION ...)` —
   the app's version, the Android `versionName` and `versionCode` = major·1000000 + minor·1000 +
   patch come from it) and in `nix/package.nix`, writes the fastlane changelogs for F-Droid and
   IzzyOnDroid (below) and makes the commit "Release vX.Y.Z" with these files on main. When the
   version is in the files already (a further `-rc.N`, the final release after `-rc`) and the
   changelogs do not change, there is no such commit; when only the news in the notes changed, the
   commit has only the changelogs.

   **The fastlane changelogs** (docs/ANDROID.md, "F-Droid and IzzyOnDroid"): for each language in
   `fastlane/metadata/android/`, the file `<language>/changelogs/<versionCode>.txt` (e.g.
   `8001.txt` for 0.8.1), up to 500 characters — the store's "what's new". English (`en-US`, and
   every other language except Polish) lists the news of the "What's new in …" section of
   `.github/release-notes.md` under "What's new in X.Y.Z:", Polish (`pl`) those of the "Nowości w
   …" section under "Nowości w X.Y.Z:" (a bullet's bold lead, else the bullet up to its " — ";
   without the "From version …" / "Z wersji …" lines; what does not fit ends in "• …"), each with
   a link to the release notes ("Full release notes: …" / "Pełny opis: …"). Without its own
   section a language takes the other language's news, without either section every bullet of the
   notes. They are written from the notes on every release of the version, also a test one (`-rc`
   has the same `versionCode`) — fix the notes, not these files. `--dry-run` previews the texts; a
   failed run removes them with the rest. A repository without `fastlane/metadata/android` (e.g. in
   the tests) gets no changelogs.
3. **`release` forward.** It moves `release` to main's new HEAD (the "Release vX.Y.Z" commit) —
   always a fast-forward (like `git merge --ff-only main` on `release`), never a merge commit —
   and gives that commit the annotated tag `vX.Y.Z`: the same commit is on `main` and on
   `release`. It does not switch branches, it only moves the ref — the files in the working tree
   keep their dates and nothing gets rebuilt. A missing local `release` is created from
   `origin/release`, an outdated one is fast-forwarded to `origin/release` first. When main brings
   nothing new (the version recorded already and `release` already at main's HEAD, e.g. `0.4.0`
   after `0.4.0-rc.2` with no change on main), it says so and only tags that commit.
4. **Push.** With `--push` it sends atomically (all or nothing)
   `git push --atomic origin main release refs/tags/vX.Y.Z`. Without `--push` it prints that
   command and how to undo the release before pushing.

When a step fails (e.g. a hook or a rejected push), the script undoes everything it did — the
tag, the move of `release`, the version commit on main — says what it undid and leaves a clean
main; remove the cause and run it again. `--dry-run` changes nothing (it does not even fetch from
origin, it only asks for its refs) and works from any branch: off `main` it first warns, then shows
what a real run on `main` would do (it reads the versions from the local `main`, without one from
`origin/main`); what a real run would refuse, it prints as warnings. `--yes` skips the
confirmation question.

Pushing the tag starts `release.yml`:

1. **draft** — the tag must match the version in `CMakeLists.txt` and `nix/package.nix`; a draft
   release is created with the description from `.github/release-notes.md` (English, with the
   Polish text below; `{{VERSION}}`, `{{FILE_VERSION}}`, `{{TAG}}`, `{{REPO}}`, `{{PAGES_URL}}`
   are filled in) and the automatic list of changes.
2. In parallel: **Tests** (`ci.yml`: the Nix build on x86_64 and aarch64, the build with
   sanitizers and `ctest`, lint — without the non-blocking performance measurement, which a
   release does not wait for), **Android** (`android.yml`), **Linux** (`appimage.yml`), **Web**
   (`wasm.yml`: the site and the single HTML file) and the **sources** (`git archive`).
3. **publish** — the release's files:
   `brulion-<version>-arm64-v8a.apk`, `Brulion-<version>-x86_64.AppImage`,
   `Brulion-<version>-aarch64.AppImage` (+ `.zsync` for updates through AppImageUpdate),
   `brulion-web-<version>.zip`, `brulion-web-<version>.html` (a single file, opened from disk),
   `brulion-<version>-source.tar.gz`, `SHA256SUMS.txt`; then the draft becomes the release (a tag
   with a suffix, e.g. `-rc.1`, → a pre-release). Before that,
   `scripts/appimage-update-check.sh` checks that every AppImage has its `.zsync`, which points to
   it, and that the update information embedded in the AppImage
   (`gh-releases-zsync|janusz-bit|brulion|latest|Brulion-*-<arch>.AppImage.zsync`) matches exactly
   that file — AppImageUpdate, Gear Lever and AppImageLauncher download it from the latest
   release. The draft waits for all tests (**Tests**) and builds. An APK signed with a throwaway
   key adds a warning (in English and Polish) to the release notes.
4. **fdroid** — after publishing, `fdroid.yml` rebuilds the F-Droid repository on GitHub Pages
   with the APK of this release and the two previous ones; a test release (`-rc.1`), a run by hand
   from a branch (not from a tag) and an APK signed with a throwaway key leave the repository as
   it is. Without the `FDROID_*` secrets this job fails (the release is published already).

The app on Android and Windows asks once a day for the latest release (`releases/latest`) and
shows a notice with its tag's page (`shell/appupdates.h`). Only a `vX.Y.Z` tag counts: a draft and
a test release show it to no one, and the notice appears only after publishing. Builds with the
CMake option `BRULION_UPDATE_CHECK=OFF` (for F-Droid and IzzyOnDroid, docs/ANDROID.md) never ask.

A failed build: "Re-run failed jobs" in the Actions tab (the draft release is reused). A run by
hand (Actions → Release → Run workflow) for an existing tag has to be started **from that tag**
("Use workflow from" → Tags); a new tag is created on the selected branch when publishing.

## F-Droid and IzzyOnDroid

The F-Droid recipe (the draft: `packaging/fdroid/io.github.januszbit.brulion.yml`; the
description, the signing decision and the submission texts: docs/ANDROID.md "Building from source (F-Droid)") has `AutoUpdateMode: Version` and `UpdateCheckMode: Tags`: once accepted into
fdroiddata, F-Droid's bot notices every new tag `vX.Y.Z` by itself (it skips the `-rc` test
releases), adds the build and builds it from the tag's sources — nothing to do at release time.
The F-Droid build takes long (Qt and OpenSSL from source), and a new version usually shows up in
F-Droid after a few days.

What breaks the F-Droid build (check before tagging when it changes):

* the versions pinned in the scripts (Qt 6.11.2 and the SHA-256 of its modules in
  `scripts/android-qt-from-source.sh`, OpenSSL in `scripts/android-openssl-from-source.sh`, the
  NDK, the SDK packages in `scripts/android-build.sh`) — the recipe in fdroiddata has them
  separately (srclibs `Qt5@v…`, `OpenSSL@openssl-…`, `ndk:`, `sdkmanager …`): after a bump the
  recipe in fdroiddata has to be fixed (an MR), and the draft in this repository with it
  (`tst_scripts_fdroid_recipe` makes sure the draft matches the scripts);
* a new Qt module in the app (it has to go into the Qt script's `HOST_MODULES`/`ANDROID_MODULES`
  and the recipe's `--module-subset`);
* a Gradle dependency from outside Maven Central / Google Maven, or a new binary file in the
  repository (F-Droid's scanner; the test archives are in `scandelete`).

The same as F-Droid, locally: `BRULION_ANDROID_QT=source BRULION_ANDROID_SIGN=0
scripts/android-build.sh` (the first time several tens of minutes: the host Qt and Qt for Android
from source).

**IzzyOnDroid** (once accepted) takes the APK from the GitHub releases
(`brulion-<version>-arm64-v8a.apk`, signed with the release key) — the file name in `release.yml`
must stay the same.

## AppImage locally

`scripts/appimage-build.sh` builds on an ordinary glibc distribution (not on NixOS — there
`nix build .#brulion`). The simplest is a container, as in CI:

```sh
podman run --rm -v "$PWD:/src:Z" -w /src docker.io/library/ubuntu:22.04 \
  bash -c 'scripts/appimage-build.sh --install-deps && scripts/appimage-build.sh'
# result: dist-appimage/Brulion-<version>-x86_64.AppImage
# (with LDAI_UPDATE_INFORMATION, as in CI, also Brulion-<version>-x86_64.AppImage.zsync next to it)
```

On a NixOS host with `programs.appimage.binfmt` the kernel hands every AppImage to an interpreter
that the container does not have ("No such file or directory") — so the script runs linuxdeploy
and the AppImage under test from a copy with the AppImage magic bytes zeroed (a plain ELF file).
Verified 2026-10-14 on the laptop (podman, `ubuntu:22.04`, g++-12): the build, 12/12 tests, an
x86_64 AppImage of 43 MB with the xcb/Wayland/offscreen plugins, a start without a display for
10 s.

The AppImage contains Qt 6.11.2 (the libraries, the xcb/Wayland/offscreen plugins, the QML modules
used in `src/app/qml`) and runs on systems with a glibc no older than the building system's:
x86_64 — Ubuntu 22.04+, Debian 12+, Fedora 36+; aarch64 — glibc ≥ 2.39 (Qt for arm64 is built on
Ubuntu 24.04).

## Checking the workflows locally

```sh
nix shell nixpkgs#actionlint nixpkgs#shellcheck -c actionlint
nix shell nixpkgs#shellcheck -c shellcheck scripts/*.sh tests/scripts/*.sh
tests/scripts/tst_release.sh                  # release.sh in throwaway repositories
tests/scripts/tst_appimage_update_check.sh    # (all of them in ctest too: tst_scripts_*)
tests/scripts/tst_check_fastlane_metadata.sh  # the fastlane texts: limits and the current version's changelog
```

On a Raspberry Pi shellcheck 0.11 can hang (at random, 100% CPU) — pinning it to one core and a
time limit help: `taskset -c 1 timeout 60 shellcheck …`; actionlint without shellcheck:
`actionlint -shellcheck=`.
