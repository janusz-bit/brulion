# CI i wydania (GitHub Actions)

Wszystkie buildy z GitHub Actions da się powtórzyć lokalnie tymi samymi skryptami z `scripts/`
(na Raspberry Pi tylko przez `laptop-run` — patrz CLAUDE.md).

Testy w GitHub Actions nie startują same przy każdym pushu. Uruchamia się je:

* **ręcznie** — Actions → wybrany workflow → *Run workflow*, albo
  `gh workflow run ci.yml --ref main` (tak samo `android.yml`, `appimage.yml`, `windows.yml`,
  `wasm.yml -f deploy=false`);
* **znacznikiem `[ci]`** w opisie któregoś z wypychanych commitów na `main` (np.
  `git commit -m "Poprawka eksportu [ci]"`) — wtedy rusza `ci.yml`, a `android.yml`,
  `appimage.yml`, `windows.yml` i `wasm.yml`, gdy push zmienia ich ścieżki; push bez znacznika pokazuje się
  w Actions jako przebieg z pominiętymi zadaniami;
* **zawsze przy wydaniu** — `release.yml` uruchamia `ci.yml` i wszystkie buildy z ich testami,
  a wydanie publikuje dopiero, gdy wszystko przejdzie.

## Workflowy

| Plik | Kiedy | Co robi |
|---|---|---|
| `.github/workflows/ci.yml` | ręcznie, push na `main` ze znacznikiem `[ci]`, z `release.yml` | **nix** (x86_64 i aarch64): `nix flake check` + `nix build .#brulion` (pakiet bez testów — `doCheck = false`, testy są w zadaniu **linux**; test VM `web-vm` wymaga KVM — runner bez `/dev/kvm` buduje pozostałe checki i pomija go z ostrzeżeniem); **linux**: CMake + Qt 6.11.2 z aqt (`jurplel/install-qt-action`), `-DBRULION_WERROR=ON`, AddressSanitizer + UBSan, `ctest`, `qmllint` (tylko ostrzeżenia); **lint**: actionlint, shellcheck `scripts/*.sh` i `tests/scripts/*.sh`, clang-format zmienionych linii; **perf** (nie blokuje): `scripts/perf-check.sh -s` na suitach benchmarków w Release — skalowanie czasu z rozmiarem danych i pamięć indeksu, bez bezwzględnych czasów; regresja daje ostrzeżenie i podsumowanie, wyniki jako artefakt `perf-smoke` (docs/PERFORMANCE.md) |
| `.github/workflows/android.yml` | ręcznie, push na `main` ze znacznikiem `[ci]` (zmiany w `src/`, `android/`…), z `release.yml` | `scripts/android-build.sh`: podpisany APK arm64 + testy Kotlina + Android lint → artefakt `brulion-android-apk` |
| `.github/workflows/wasm.yml` | ręcznie, push na `main` ze znacznikiem `[ci]` (zmiany w `src/`, `wasm/`…), z `release.yml` | `scripts/wasm-build.sh --setup`, test dymny w Chrome (`scripts/wasm-smoke.mjs`: start, trwałość notatek, indeks bez wejścia, start offline) → artefakt `brulion-web`; z `main` **wdrożenie na GitHub Pages** (wydanie wdraża swoją wersję po publikacji) |
| `.github/workflows/appimage.yml` | ręcznie, push na `main` ze znacznikiem `[ci]` (zmiany w `src/`, `tests/`…), z `release.yml` | `scripts/appimage-build.sh` dla x86_64 (kontener Ubuntu 22.04) i aarch64 (Ubuntu 24.04): build Release, testy, AppImage, próbne uruchomienie → artefakty `brulion-appimage-<arch>` |
| `.github/workflows/windows.yml` | ręcznie, push na `main` ze znacznikiem `[ci]` (zmiany w `src/`, `tests/`, `packaging/windows/`…), z `release.yml` | `scripts/windows-build.ps1` na `windows-2022` (Qt 6.11.2 MSVC 2022 z aqt, MSVC, Ninja): build Release, testy offscreen, `windeployqt`, przenośny ZIP i instalator Inno Setup, sprawdzenie obu bez Qt w `PATH` (rozpakowany ZIP, cicha instalacja dla użytkownika, autotest powłoki, ciche odinstalowanie) → artefakt `brulion-windows` (wersja testowa, docs/WINDOWS.md) |
| `.github/workflows/release.yml` | tag `v*` albo ręcznie | sprawdza wersję, tworzy szkic wydania, uruchamia testy (`ci.yml`), cztery powyższe buildy + `git archive`, dołącza pliki i `SHA256SUMS.txt`, publikuje wydanie i wdraża jego wersję przeglądarkową na GitHub Pages (wydanie testowe nie) |

Wersje narzędzi są przypięte w skryptach (Qt 6.11.2, aqtinstall 3.3.0, NDK r27c, Emscripten
4.0.7, linuxdeploy z sumami SHA-256); `dependabot.yml` co miesiąc aktualizuje akcje GitHuba
jednym PR-em. Pobrane Qt, NDK i emsdk są w `actions/cache` (klucze z wersjami — po podbiciu
wersji pobierają się raz od nowa).

### Formatowanie (clang-format)

Styl kodu opisuje `.clang-format` (clang-format 21 z `nix develop`); `.clang-format-ignore`
wyłącza kod obcy i pliki z JavaScriptem w `EM_JS`. CI sprawdza **tylko zmienione linie**
(`git clang-format`) — istniejący kod nie jest przeformatowywany hurtem: przy pushu ze
znacznikiem `[ci]` linie wypchniętych commitów, przy ręcznym uruchomieniu linie zmienione od
ostatniego wydania, przy wydaniu — od poprzedniego. Różnica w formatowaniu jest tylko
ostrzeżeniem. Przed commitem:

```sh
nix develop -c git clang-format          # formatuje linie zmienione względem HEAD
```

## Jednorazowa konfiguracja repozytorium

1. **GitHub Pages:** Settings → Pages → Build and deployment → Source: **GitHub Actions**.
   Wersja przeglądarkowa trafia pod `https://janusz-bit.github.io/brulion/`. Żeby wydanie
   mogło ją wdrożyć: Settings → Environments → `github-pages` → *Deployment branches and
   tags*: gałąź `main` i tag `v*` (z wiersza poleceń:
   `gh api -X POST repos/janusz-bit/brulion/environments/github-pages/deployment-branch-policies -f name='v*' -f type=tag`).
2. **Klucz podpisu Androida:** `scripts/android-new-keystore.sh` tworzy klucz poza
   repozytorium i wypisuje polecenia `gh secret set` dla sekretów `QT_ANDROID_KEYSTORE_BASE64`,
   `QT_ANDROID_KEYSTORE_STORE_PASS`, `QT_ANDROID_KEYSTORE_KEY_PASS`, `QT_ANDROID_KEYSTORE_ALIAS`.
   Bez nich APK jest podpisany kluczem jednorazowym (ostrzeżenie w podsumowaniu i w opisie
   wydania). **Kopia zapasowa klucza i hasła jest obowiązkowa** — aktualizacje muszą mieć ten
   sam podpis.
3. Runnery `ubuntu-24.04-arm` (build Nix aarch64, AppImage aarch64) są bezpłatne dla
   repozytoriów publicznych.

## Wydanie

Wszystkie commity idą na `main`: rozwój, notatki wydaniowe (`.github/release-notes.md`)
i commit z wersją „Release vX.Y.Z”. Gałąź `release` ma **wyłącznie commity z main** — żadnego
własnego commita ani commita scalającego: zawsze jest przodkiem `main` (albo samą `main`) i stoi
na commicie ostatniego wydania, więc gałęzie różnią się tylko liczbą commitów
(`git rev-list main..release` jest puste). Wydać znaczy przesunąć `release` do przodu na
commit „Release vX.Y.Z” z main (fast-forward — rebase, w którym nie ma czego przenosić,
i scalenie bez commita scalającego) i dać temu commitowi tag `vX.Y.Z`: ten sam commit na obu
gałęziach. Wydania to tagi: `git tag -l 'v*' --sort=v:refname`, a w historii
`git log --oneline --decorate release` (tag przy każdym commicie „Release vX.Y.Z”). Robi to
`scripts/release.sh`:

```sh
git switch main && git pull --ff-only
# .github/release-notes.md + sekcja "## X.Y.Z — <data>" w src/app/shell/guide/Lista zmian.md
#   -> commit "Update release notes for vX.Y.Z" (na main)
scripts/release.sh --dry-run patch     # podgląd: wersje, commity, ostrzeżenia
scripts/release.sh patch --push        # wersja na main, release przesunięta na nią + tag, push
```

Notatki wydaniowe to opis wydania na GitHubie (nowości tej wersji); strona „Lista zmian” w
notatniku z dokumentacją (`src/app/shell/guide/Lista zmian.md`, po polsku, najnowsze na górze)
zbiera wszystkie zmiany wszystkich wersji — każde wydanie dodaje na jej początku swoją sekcję
(Nowe / Zmiany / Poprawki / Uwaga). Jej przykłady składni muszą być w kodzie (`⏰ …`, `#tag`,
`- [ ]`), bo to żywa notatka. `release.sh` ostrzega, gdy notatki się nie zmieniły albo gdy na
liście brakuje sekcji wydawanej wersji (wydanie testowe jej nie potrzebuje).

Dzięki temu `main` zawsze ma wydaną wersję i jej notatki — z niego budują się
`nix run github:janusz-bit/brulion` (flake bez taga) i artefakty CI (APK z main nie ma
niższego `versionCode` niż wydany). Na `release` nie commitować niczego: poprawka do wydania
to commit na main i kolejne wydanie. Historia: do v0.3.4 wydania powstawały na main, a v0.3.5
na `release` (jej notatki i commit z wersją trafiły na main przed v0.3.6). v0.3.6 i v0.3.7 były
commitami scalającymi main do `release` („Merge main into release for vX.Y.Z”, drzewo jak
w „Release vX.Y.Z” na main) i ich tagi zostały na tych scaleniach — wskazują na nie opublikowane
wydania. Od 2026-10-23 `release` zaczyna się od nowa na commicie „Release v0.3.7” z main
i jest już tylko prefiksem main.

Argumenty `scripts/release.sh`:

```sh
scripts/release.sh minor --push        # 0.3.7 -> 0.4.0: "Release v0.4.0" na main, release na nim z tagiem v0.4.0, push
scripts/release.sh 0.4.0-rc.1          # wydanie testowe (pre-release); pliki mają wersję 0.4.0
scripts/release.sh 0.4.0-rc.2          # wersja już jest w plikach: bez commita z wersją, release na HEAD main + tag
scripts/release.sh 0.4.0               # main bez zmian od -rc.2: tag v0.4.0 na tym samym commicie
scripts/release.sh --dry-run patch     # tylko pokazuje, co by zrobił (z każdej gałęzi)
```

Prawdziwy przebieg działa tylko na `main` (z innej gałęzi umiera od razu) i z czystym drzewem
roboczym:

1. **Sprawdza.** Pobiera z origin `main` i `release` (`git fetch`, bez tagów) i odmawia, gdy
   main nie ma commitów z `origin/main` (`git pull --ff-only` — także gdy wydanie zrobiono na
   innym komputerze: `release` wyprzedza wtedy lokalną main, ale niczego nie trzeba cofać);
   gdy lokalna `release` i `origin/release` się rozeszły; gdy na `release` (lokalnej albo
   w origin) jest commit, którego main nie ma (`git rev-list main..release` musi być puste) —
   własny commit albo scalenie, np. stara `release` ze scaleniem v0.3.7: skrypt wypisuje te
   commity i podaje, jak cofnąć `release` do ostatniego commita main, który ma
   (`git branch -f release <commit>`, a gdy to samo jest w origin, także
   `git push --force-with-lease origin <commit>:refs/heads/release`); zmianę, której main
   potrzebuje, trzeba najpierw przenieść na main (`git cherry-pick`); gdy `release` jest
   wybrana w innym worktree, a ma się przesunąć (rozjechałoby to tamten checkout); gdy tag już
   jest (lokalnie albo w origin); przy `patch`/`minor`/`major`, gdy bieżąca wersja ma tylko
   wydania testowe (np. po `0.4.0-rc.2` `patch` pominąłby 0.4.0 — trzeba podać wersję wprost).
   Ostrzega, gdy `.github/release-notes.md` nie zmienił się od ostatniego wydania (porównuje
   z commitem, na którym stoi `release`; wydanie powtórzyłoby poprzednie notatki), i gdy
   lokalna `release` wyprzedza `origin/release` z tagiem wcześniejszego wydania bez `--push` —
   push wyśle jej commity, ale nie jej tag (skrypt podaje `git push origin <tag>`).
2. **Wersja na main.** Ustawia wersję w `CMakeLists.txt` (`project(... VERSION ...)` — z niej
   biorą się wersja aplikacji, `versionName` i `versionCode` Androida = major·1000000 +
   minor·1000 + patch) i w `nix/package.nix` i robi na main commit „Release vX.Y.Z”. Gdy
   wersja już jest w plikach (kolejne `-rc.N`, wydanie końcowe po `-rc`), tego commita nie ma.
3. **`release` do przodu.** Przesuwa `release` na nowy HEAD main (commit „Release vX.Y.Z”) —
   zawsze fast-forward (jak `git merge --ff-only main` na `release`), nigdy commit scalający —
   i daje temu commitowi tag z adnotacją `vX.Y.Z`: ten sam commit jest na `main` i na
   `release`. Gałęzi nie przełącza, przesuwa tylko ref — pliki w drzewie roboczym nie zmieniają
   dat, a build się nie przebudowuje. Brakującą lokalną `release` tworzy z `origin/release`,
   nieaktualną najpierw przewija do `origin/release`. Gdy main nie wnosi nic nowego (wersja już
   zapisana, a `release` już na HEAD main, np. `0.4.0` po `0.4.0-rc.2` bez zmian na main),
   mówi to i tylko taguje ten commit.
4. **Push.** Z `--push` wysyła atomowo (wszystko albo nic)
   `git push --atomic origin main release refs/tags/vX.Y.Z`. Bez `--push` wypisuje to
   polecenie i to, jak cofnąć wydanie przed wysłaniem.

Gdy któryś krok się nie uda (np. hook albo odrzucony push), skrypt cofa wszystko, co zrobił —
tag, przesunięcie `release`, commit z wersją na main — pisze, co cofnął, i zostawia czysty main;
wystarczy usunąć przyczynę i uruchomić go ponownie. `--dry-run` niczego nie zmienia (nawet nie
pobiera z origin, tylko pyta o jego refy) i działa z każdej gałęzi: poza `main` najpierw
ostrzega, a potem pokazuje, co zrobiłby prawdziwy przebieg na `main` (wersje czyta z lokalnej
`main`, a bez niej z `origin/main`); to, czego prawdziwy przebieg by odmówił, wypisuje jako
ostrzeżenia. `--yes` pomija pytanie o potwierdzenie.

Push taga uruchamia `release.yml`:

1. **draft** — tag musi się zgadzać z wersją w `CMakeLists.txt` i `nix/package.nix`; powstaje
   szkic wydania z opisem z `.github/release-notes.md` (po polsku; `{{VERSION}}`,
   `{{FILE_VERSION}}`, `{{TAG}}`, `{{REPO}}`, `{{PAGES_URL}}` są podstawiane) i automatyczną
   listą zmian.
2. Równolegle: **Tests** (`ci.yml`: build Nix x86_64 i aarch64, build z sanitizerami
   i `ctest`, lint, pomiar wydajności), **Android** (`android.yml`), **Linux**
   (`appimage.yml`), **Web** (`wasm.yml`, bez wdrożenia na Pages) i **źródła**
   (`git archive`).
3. **publish** — pliki do wydania:
   `brulion-<wersja>-arm64-v8a.apk`, `Brulion-<wersja>-x86_64.AppImage`,
   `Brulion-<wersja>-aarch64.AppImage` (+ `.zsync` do aktualizacji przez AppImageUpdate),
   `brulion-web-<wersja>.zip`, `brulion-<wersja>-source.tar.gz`, `SHA256SUMS.txt`; potem
   szkic staje się wydaniem (tag z sufiksem, np. `-rc.1`, → pre-release). Wcześniej
   `scripts/appimage-update-check.sh` sprawdza, że każdy AppImage ma swój `.zsync`, który go
   wskazuje, i że informacja o aktualizacji wbudowana w AppImage
   (`gh-releases-zsync|janusz-bit|brulion|latest|Brulion-*-<arch>.AppImage.zsync`) pasuje
   dokładnie do tego pliku — AppImageUpdate, Gear Lever i AppImageLauncher pobierają go
   z najnowszego wydania. Szkic czeka na wszystkie testy (**Tests**) i buildy.
4. **pages** — opublikowane wydanie wdraża swoją wersję przeglądarkową (ten sam build co
   `brulion-web-<wersja>.zip`) na GitHub Pages; wydanie testowe (`-rc.1`) i ręczne
   uruchomienie z gałęzi (nie z taga) zostawiają stronę bez zmian.

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
# (z LDAI_UPDATE_INFORMATION, jak w CI, także Brulion-<wersja>-x86_64.AppImage.zsync obok)
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
nix shell nixpkgs#shellcheck -c shellcheck scripts/*.sh tests/scripts/*.sh
tests/scripts/tst_release.sh                  # release.sh w jednorazowych repozytoriach
tests/scripts/tst_appimage_update_check.sh    # (oba też w ctest: tst_scripts_*)
```

Na Raspberry Pi shellcheck 0.11 potrafi się zawiesić (losowo, 100% CPU) — pomaga przypięcie do
jednego rdzenia i limit czasu: `taskset -c 1 timeout 60 shellcheck …`; actionlint bez
shellchecka: `actionlint -shellcheck=`.
