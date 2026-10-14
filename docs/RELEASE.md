# CI i wydania (GitHub Actions)

Wszystkie buildy z GitHub Actions da się powtórzyć lokalnie tymi samymi skryptami z `scripts/`
(na Raspberry Pi tylko przez `laptop-run` — patrz CLAUDE.md).

## Workflowy

| Plik | Kiedy | Co robi |
|---|---|---|
| `.github/workflows/ci.yml` | każdy push na `main`, każdy pull request | **nix** (x86_64 i aarch64): `nix flake check` + `nix build .#brulion` (z testami; test VM `web-vm` wymaga KVM — runner bez `/dev/kvm` buduje pozostałe checki i pomija go z ostrzeżeniem); **linux**: CMake + Qt 6.11.2 z aqt (`jurplel/install-qt-action`), `-DBRULION_WERROR=ON`, AddressSanitizer + UBSan, `ctest`, `qmllint` (tylko ostrzeżenia); **lint**: actionlint, shellcheck `scripts/*.sh`, clang-format zmienionych linii |
| `.github/workflows/android.yml` | push na `main` (zmiany w `src/`, `android/`…), ręcznie, z `release.yml` | `scripts/android-build.sh`: podpisany APK arm64 + testy Kotlina + Android lint → artefakt `brulion-android-apk` |
| `.github/workflows/wasm.yml` | push na `main` (zmiany w `src/`, `wasm/`…), ręcznie, z `release.yml` | `scripts/wasm-build.sh --setup`, test dymny w Chrome (`scripts/wasm-smoke.mjs`: start, trwałość notatek, start offline) → artefakt `brulion-web`; z `main` **wdrożenie na GitHub Pages** |
| `.github/workflows/appimage.yml` | push na `main` (zmiany w `src/`, `tests/`…), ręcznie, z `release.yml` | `scripts/appimage-build.sh` dla x86_64 (kontener Ubuntu 22.04) i aarch64 (Ubuntu 24.04): build Release, testy, AppImage, próbne uruchomienie → artefakty `brulion-appimage-<arch>` |
| `.github/workflows/release.yml` | tag `v*` albo ręcznie | sprawdza wersję, tworzy szkic wydania, uruchamia trzy powyższe buildy + `git archive`, dołącza pliki i `SHA256SUMS.txt`, publikuje wydanie |

Wersje narzędzi są przypięte w skryptach (Qt 6.11.2, aqtinstall 3.3.0, NDK r27c, Emscripten
4.0.7, linuxdeploy z sumami SHA-256); `dependabot.yml` co miesiąc aktualizuje akcje GitHuba
jednym PR-em. Pobrane Qt, NDK i emsdk są w `actions/cache` (klucze z wersjami — po podbiciu
wersji pobierają się raz od nowa).

### Formatowanie (clang-format)

Styl kodu opisuje `.clang-format` (clang-format 21 z `nix develop`); `.clang-format-ignore`
wyłącza kod obcy i pliki z JavaScriptem w `EM_JS`. CI sprawdza **tylko zmienione linie**
(`git clang-format`) — istniejący kod nie jest przeformatowywany hurtem. W pull requestach
różnica w formatowaniu jest błędem, przy pushu na `main` tylko ostrzeżeniem. Przed commitem:

```sh
nix develop -c git clang-format          # formatuje linie zmienione względem HEAD
```

## Jednorazowa konfiguracja repozytorium

1. **GitHub Pages:** Settings → Pages → Build and deployment → Source: **GitHub Actions**.
   Wersja przeglądarkowa trafia pod `https://janusz-bit.github.io/brulion/`.
2. **Klucz podpisu Androida:** `scripts/android-new-keystore.sh` tworzy klucz poza
   repozytorium i wypisuje polecenia `gh secret set` dla sekretów `QT_ANDROID_KEYSTORE_BASE64`,
   `QT_ANDROID_KEYSTORE_STORE_PASS`, `QT_ANDROID_KEYSTORE_KEY_PASS`, `QT_ANDROID_KEYSTORE_ALIAS`.
   Bez nich APK jest podpisany kluczem jednorazowym (ostrzeżenie w podsumowaniu i w opisie
   wydania). **Kopia zapasowa klucza i hasła jest obowiązkowa** — aktualizacje muszą mieć ten
   sam podpis.
3. Runnery `ubuntu-24.04-arm` (build Nix aarch64, AppImage aarch64) są bezpłatne dla
   repozytoriów publicznych.

## Wydanie

```sh
scripts/release.sh minor --push        # 0.1.0 -> 0.2.0: commit "Release v0.2.0" + tag v0.2.0, push
scripts/release.sh 0.1.0 --push        # pierwsze wydanie: wersja już jest w plikach — sam tag
scripts/release.sh 0.3.0-rc.1          # wydanie testowe (pre-release); pliki mają wersję 0.3.0
scripts/release.sh --dry-run patch     # tylko pokazuje, co by zrobił
```

`scripts/release.sh` ustawia wersję w `CMakeLists.txt` (`project(... VERSION ...)` — z niej
biorą się wersja aplikacji, `versionName` i `versionCode` Androida = major·1000000 +
minor·1000 + patch) i w `nix/package.nix`, robi commit i tag z adnotacją. Wymaga czystego
drzewa roboczego. Push taga uruchamia `release.yml`:

1. **draft** — tag musi się zgadzać z wersją w `CMakeLists.txt` i `nix/package.nix`; powstaje
   szkic wydania z opisem z `.github/release-notes.md` (po polsku; `{{VERSION}}`,
   `{{FILE_VERSION}}`, `{{TAG}}`, `{{REPO}}`, `{{PAGES_URL}}` są podstawiane) i automatyczną
   listą zmian.
2. Równolegle: **Android** (`android.yml`), **Linux** (`appimage.yml`), **Web**
   (`wasm.yml`, bez wdrożenia na Pages) i **źródła** (`git archive`).
3. **publish** — pliki do wydania:
   `brulion-<wersja>-arm64-v8a.apk`, `Brulion-<wersja>-x86_64.AppImage`,
   `Brulion-<wersja>-aarch64.AppImage` (+ `.zsync` do aktualizacji przez AppImageUpdate),
   `brulion-web-<wersja>.zip`, `brulion-<wersja>-source.tar.gz`, `SHA256SUMS.txt`; potem
   szkic staje się wydaniem (tag z sufiksem, np. `-rc.1`, → pre-release).

Nieudany build: „Re-run failed jobs” w zakładce Actions (szkic wydania jest używany
ponownie). Ręczne uruchomienie (Actions → Release → Run workflow) dla istniejącego taga trzeba
uruchomić **z tego taga** („Use workflow from” → Tags); nowy tag powstaje na wybranej gałęzi
przy publikacji.

## AppImage lokalnie

`scripts/appimage-build.sh` buduje na zwykłej dystrybucji z glibc (nie na NixOS — tam
`nix build .#brulion`). Najprościej w kontenerze, jak w CI:

```sh
podman run --rm -v "$PWD:/src:Z" -w /src docker.io/library/ubuntu:22.04 \
  bash -c 'scripts/appimage-build.sh --install-deps && scripts/appimage-build.sh'
# wynik: dist-appimage/Brulion-<wersja>-x86_64.AppImage
```

Na hoście NixOS z `programs.appimage.binfmt` jądro przekazuje każdy AppImage do interpretera,
którego w kontenerze nie ma („No such file or directory”) — skrypt uruchamia więc linuxdeploy
i sprawdzany AppImage z kopii z wyzerowanymi bajtami magicznymi AppImage (zwykły plik ELF).
Zweryfikowane 2026-10-14 na laptopie (podman, `ubuntu:22.04`, g++-12): build, 12/12 testów,
AppImage x86_64 43 MB z wtyczkami xcb/Wayland/offscreen, start bez ekranu przez 10 s.

AppImage zawiera Qt 6.11.2 (biblioteki, wtyczki xcb/Wayland/offscreen, moduły QML używane
w `src/app/qml`) i działa na systemach z glibc nie starszym niż system budujący: x86_64
— Ubuntu 22.04+, Debian 12+, Fedora 36+; aarch64 — glibc ≥ 2.39 (Qt dla arm64 jest budowane
na Ubuntu 24.04).

## Sprawdzanie workflowów lokalnie

```sh
nix shell nixpkgs#actionlint nixpkgs#shellcheck -c actionlint
nix shell nixpkgs#shellcheck -c shellcheck scripts/*.sh
```

Na Raspberry Pi shellcheck 0.11 potrafi się zawiesić (losowo, 100% CPU) — pomaga przypięcie do
jednego rdzenia i limit czasu: `taskset -c 1 timeout 60 shellcheck …`; actionlint bez
shellchecka: `actionlint -shellcheck=`.
