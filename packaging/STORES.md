# Stores and repositories: Flathub, winget, AUR, Homebrew

The manifests are in this folder, made for each release by `scripts/store-manifests.py` from the
release's `SHA256SUMS.txt`; nothing in the repository sends them anywhere. Submitting them is the
owner's part, below. (Google Play and F-Droid: docs/GOOGLE_PLAY.md, packaging/fdroid.)

| store | files | what it installs | checked here |
|---|---|---|---|
| Flathub | `flathub/io.github.januszbit.brulion.yml` | built from the release's source tarball with the KDE runtime (Qt 6.12), without the model on the device and the update check | built and installed with `flatpak-builder` against `org.kde.Sdk//6.12-beta` (the stable 6.12 runtime is not on Flathub yet), AppStream composed, `brulion-cli --version` run in the sandbox (Qt 6.12.0) |
| winget | `winget/manifests/j/janusz-bit/Brulion/<version>/` (4 files) | the Windows setup (`Brulion-<version>-windows-x86_64-setup.exe`, Inno Setup, per user) | the four files valid against winget's JSON schemas 1.6.0 |
| AUR | `aur/brulion-bin/PKGBUILD`, `.SRCINFO` | the x86_64 / aarch64 AppImage, unpacked into `/opt/brulion` | `makepkg` built the package (checksum, unpacking, files); `.SRCINFO` equals `makepkg --printsrcinfo` |
| Homebrew | `homebrew/brulion.rb` | built from the source tarball (Linux: there is no macOS build yet) | not built (needs Homebrew's Qt 6.12) |

`tests/scripts/tst_store_manifests.sh` (ctest `tst_scripts_store_manifests`) checks the generator.

## After each release

```sh
gh release download v0.33.0 -R janusz-bit/brulion -p SHA256SUMS.txt
scripts/store-manifests.py --version 0.33.0 --sums SHA256SUMS.txt
git add packaging && git commit -m "Store manifests for 0.33.0"
```

Then update each store as below (Flathub and the AUR: the new version of the same files; winget: a
new folder of manifests).

## Flathub (once, then a PR per release on the app's own Flathub repository)

1. **Wait for the KDE runtime 6.12** on Flathub's stable repository: `flatpak remote-ls flathub
   --runtime | grep org.kde.Platform` must list `6.12` (today only `6.12-beta` is there, on
   flathub-beta). Brulion needs Qt 6.12.
2. **The app id.** Flathub checks that `io.github.<user>.<app>` belongs to that GitHub account, and
   turns a hyphen of the account's name into an underscore: for `github.com/janusz-bit` it expects
   `io.github.janusz_bit.brulion`. Brulion's id is `io.github.januszbit.brulion` (the desktop file,
   the AppStream metadata, the Android package). Either ask Flathub's reviewers to accept it with
   proof of the account (they do for older ids), or rename the desktop and AppStream ids for Flathub
   only (a decision of its own: the settings' folder stays, the desktop entry's name changes).
3. Fork https://github.com/flathub/flathub, make a branch from `new-pr`, add
   `io.github.januszbit.brulion.yml` from this folder, and open a pull request **against
   `new-pr`**. Build it locally first:
   `flatpak-builder --user --force-clean --install build io.github.januszbit.brulion.yml` and
   `flatpak run io.github.januszbit.brulion`; Flathub's linter:
   `flatpak run --command=flathub-build org.flatpak.Builder --install io.github.januszbit.brulion.yml`.
4. The reviewers ask about the permissions: `--filesystem=home` (a notebook is any folder of the
   user's files, as with Obsidian), `--share=network` (Nextcloud, calendars, AI providers the user
   sets up), notifications and the tray. After the merge Flathub makes the repository
   `flathub/io.github.januszbit.brulion`; later versions are PRs there.

## winget (a PR per release)

1. Fork https://github.com/microsoft/winget-pkgs and copy
   `winget/manifests/j/janusz-bit/Brulion/<version>/` to the same path in it.
2. On Windows: `winget validate --manifest manifests\j\janusz-bit\Brulion\<version>` and
   `winget install --manifest manifests\j\janusz-bit\Brulion\<version>` (then start Brulion, and
   `winget uninstall janusz-bit.Brulion`).
3. Open the pull request; the bots install it in a sandbox. (Or `wingetcreate submit <folder>` with
   a GitHub token.) The setup is not code-signed: winget takes it, SmartScreen may warn on the
   first start.

## AUR (once an account, then a push per release)

1. An account on https://aur.archlinux.org with an SSH key.
2. `git clone ssh://aur@aur.archlinux.org/brulion-bin.git`, copy `PKGBUILD` and `.SRCINFO` from
   `aur/brulion-bin/`, on Arch: `makepkg -si` (installs it), start `brulion`, then
   `git commit -am "Brulion <version>" && git push`.
3. A package built from source (`brulion`) can follow; it needs Qt 6.12 in Arch's repositories.

## Homebrew (an own tap)

homebrew-core takes only well-known projects that build on macOS; an own tap fits now:

1. A repository `janusz-bit/homebrew-brulion` with `Formula/brulion.rb` from `homebrew/`.
2. On Linux with Homebrew: `brew install --build-from-source janusz-bit/brulion/brulion`, then
   `brew test brulion` and `brew audit --strict brulion`. Homebrew's `qt` must be 6.12 or later
   (`brew info qt`).
3. Users install it with `brew install janusz-bit/brulion/brulion`.
